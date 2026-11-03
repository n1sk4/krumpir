/* HIL backend: a controller on a serial line (a UART, or a pty pair on a PC), libs/hil frames.
 *
 * Before step k the runner sends PLANT(seq k) with the FMU outputs (the loop writes the initial
 * ones before step 0); the controller answers CMD(seq k), which feeds the inputs of step k. With
 * timeout_ms > 0 (a lockstep controller) read() waits that long for CMD(k); with 0 (a free-running
 * controller) it
 * takes the newest CMD that has arrived. A step without its answer holds the last commands and
 * counts as late; io.max_late late steps in a row fault the run when io.late = "fault". */
#define _DEFAULT_SOURCE /* cfmakeraw, B921600 and up */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "hil.h"
#include "io.h"

#define HELLO_WAIT_MS 2000
#define HELLO_EVERY_MS 100

typedef struct {
  io_t io; /* first: the io_t pointer is the uart_t pointer */
  int fd;
  char device[CFG_MAX_PATH];
  hil_parser_t parser;
  size_t n_plant, n_cmd;
  size_t cmd_slot[TELEM_MAX_CH]; /* CMD value i -> model input cmd_slot[i] */
  float hold[TELEM_MAX_CH];      /* the last commands */
  uint16_t seq;                  /* of the next PLANT */
  bool sent;                     /* a PLANT went out, so a CMD is due */
  int timeout_ms;
  bool late_fault;
  int max_late, run_late;
} uart_t;

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int set_error(uart_t *u, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(u->io.error, sizeof u->io.error, fmt, ap);
  va_end(ap);
  return -1;
}

static int send_frame(uart_t *u, const hil_frame_t *f)
{
  uint8_t buf[HIL_FRAME_MAX];
  const size_t len = hil_encode(f, buf);
  for (size_t off = 0; off < len;) {
    const ssize_t w = write(u->fd, buf + off, len - off);
    if (w > 0) {
      off += (size_t)w;
    } else if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
      struct pollfd p = {u->fd, POLLOUT, 0};
      if (poll(&p, 1, 100) == 0) {
        return set_error(u, "%s: cannot write (output full)", u->device);
      }
    } else {
      return set_error(u, "%s: write failed: %s", u->device, strerror(errno));
    }
  }
  return 0;
}

/* Reads what has arrived, waiting until deadline (ms, now_ms clock) for a frame of `type` (with
 * `seq` when want_seq). Returns 1 and the frame, 0 on timeout, -1 if the device failed. With
 * deadline 0 it does not wait and returns the newest frame of the type that had arrived. */
static int receive(uart_t *u, int type, bool want_seq, uint16_t seq, int64_t deadline,
                   hil_frame_t *out)
{
  int got = 0;
  for (;;) {
    uint8_t buf[256];
    const ssize_t r = read(u->fd, buf, sizeof buf);
    if (r < 0 && errno != EAGAIN && errno != EINTR) {
      return set_error(u, "%s: read failed: %s", u->device, strerror(errno));
    }
    for (ssize_t i = 0; i < r; i++) {
      hil_frame_t f;
      if (hil_parse_byte(&u->parser, buf[i], &f) && f.type == type &&
          (!want_seq || f.seq == seq)) {
        *out = f;
        got = 1;
      }
    }
    if (r > 0) {
      if (got && deadline != 0) {
        return 1;
      }
      continue; /* drain */
    }
    const int64_t left = deadline - now_ms();
    if (got || deadline == 0 || left <= 0) {
      return got;
    }
    struct pollfd p = {u->fd, POLLIN, 0};
    poll(&p, 1, (int)left);
  }
}

static int uart_read(io_t *io, double t, double *in, size_t n)
{
  (void)t;
  uart_t *u = (uart_t *)io;
  hil_frame_t f;
  int got = 0;
  if (u->timeout_ms == 0) {
    got = receive(u, HIL_CMD, false, 0, 0, &f);
  } else if (u->sent) {
    got = receive(u, HIL_CMD, true, (uint16_t)(u->seq - 1), now_ms() + u->timeout_ms, &f);
  }
  if (got < 0) {
    return -1;
  }
  if (got && f.n == u->n_cmd) {
    memcpy(u->hold, f.v, u->n_cmd * sizeof f.v[0]);
    u->run_late = 0;
  } else if (u->sent) {
    atomic_fetch_add_explicit(&io->late, 1, memory_order_relaxed);
    if (++u->run_late >= u->max_late && u->late_fault) {
      return set_error(u, "no answer from the controller on %s for %d steps in a row "
                       "(line: %u CRC errors, %u resyncs)",
                       u->device, u->run_late, (unsigned)u->parser.crc_errors,
                       (unsigned)u->parser.resyncs);
    }
  }
  memset(in, 0, n * sizeof *in);
  for (size_t i = 0; i < u->n_cmd; i++) {
    in[u->cmd_slot[i]] = u->hold[i];
  }
  return 0;
}

static int uart_write(io_t *io, double t, const double *out, size_t n)
{
  (void)t;
  uart_t *u = (uart_t *)io;
  hil_frame_t f = {HIL_PLANT, u->seq, (uint8_t)n, {0}};
  for (size_t i = 0; i < n; i++) {
    f.v[i] = (float)out[i];
  }
  if (send_frame(u, &f) < 0) {
    return -1;
  }
  u->seq++;
  u->sent = true;
  return 0;
}

static void send_bye(uart_t *u)
{
  const hil_frame_t f = {HIL_BYE, u->seq, 0, {0}};
  send_frame(u, &f);
}

static void uart_safe_state(io_t *io) { send_bye((uart_t *)io); }

static void uart_close(io_t *io)
{
  uart_t *u = (uart_t *)io;
  send_bye(u);
  close(u->fd);
  free(u);
}

static const io_api_t api = {"uart", uart_read, uart_write, uart_safe_state, uart_close};

static speed_t baud_code(int baud)
{
  static const struct {
    int baud;
    speed_t code;
  } t[] = {{9600, B9600},       {19200, B19200},     {38400, B38400},     {57600, B57600},
           {115200, B115200},   {230400, B230400},   {460800, B460800},   {500000, B500000},
           {921600, B921600},   {1000000, B1000000}, {1500000, B1500000}, {2000000, B2000000},
           {3000000, B3000000}, {4000000, B4000000}};
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
    if (t[i].baud == baud) {
      return t[i].code;
    }
  }
  return B0;
}

static bool has_stimulus(const scenario_t *sc, const char *input)
{
  for (size_t j = 0; j < sc->n_stimuli; j++) {
    if (strcmp(sc->stimuli[j].input, input) == 0) {
      return true;
    }
  }
  return false;
}

/* "[a, b]" */
static void list(char *dst, size_t cap, const char *const *names, size_t n)
{
  size_t k = (size_t)snprintf(dst, cap, "[");
  for (size_t i = 0; i < n && k < cap; i++) {
    k += (size_t)snprintf(dst + k, cap - k, "%s%s", i ? ", " : "", names[i]);
  }
  if (k < cap) {
    snprintf(dst + k, cap - k, "]");
  }
}

io_t *uart_io_open(const scenario_t *sc, char *err, size_t errlen)
{
  const speed_t speed = baud_code(sc->baud);
  if (speed == B0) {
    snprintf(err, errlen, "io.baud %d is not a standard rate", sc->baud);
    return NULL;
  }
  uart_t *u = calloc(1, sizeof *u);
  if (u == NULL) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  u->io.api = &api;
  snprintf(u->device, sizeof u->device, "%s", sc->device);
  u->timeout_ms = (int)(sc->timeout_ms + 0.5);
  u->late_fault = sc->late_fault;
  u->max_late = sc->max_late;
  hil_parser_init(&u->parser);

  const char *plant[TELEM_MAX_CH], *cmd[TELEM_MAX_CH];
  u->n_plant = sc->n_outputs;
  for (size_t i = 0; i < sc->n_outputs; i++) {
    plant[i] = sc->outputs[i];
  }
  for (size_t i = 0; i < sc->n_inputs; i++) {
    if (!has_stimulus(sc, sc->inputs[i])) {
      cmd[u->n_cmd] = sc->inputs[i];
      u->cmd_slot[u->n_cmd++] = i;
    }
  }
  const uint32_t hash = hil_names_hash(plant, u->n_plant, cmd, u->n_cmd);

  u->fd = open(sc->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (u->fd < 0) {
    snprintf(err, errlen, "cannot open io.device '%s': %s", sc->device, strerror(errno));
    free(u);
    return NULL;
  }
  struct termios tio;
  if (tcgetattr(u->fd, &tio) != 0) {
    snprintf(err, errlen, "io.device '%s' is not a serial line: %s", sc->device, strerror(errno));
    goto fail;
  }
  cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD;
  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);
  if (tcsetattr(u->fd, TCSANOW, &tio) != 0) {
    snprintf(err, errlen, "cannot set up io.device '%s': %s", sc->device, strerror(errno));
    goto fail;
  }
  tcflush(u->fd, TCIOFLUSH);

  /* HELLO until the controller answers with its own. */
  hil_frame_t hello, theirs;
  hil_hello(&hello, u->n_plant, u->n_cmd, hash);
  const int64_t end = now_ms() + HELLO_WAIT_MS;
  int got = 0;
  while (got == 0 && now_ms() < end) {
    got = send_frame(u, &hello);
    if (got == 0) {
      got = receive(u, HIL_HELLO, false, 0, now_ms() + HELLO_EVERY_MS, &theirs);
    }
  }
  if (got < 0) {
    snprintf(err, errlen, "%s", u->io.error);
    goto fail;
  }
  if (got == 0) {
    snprintf(err, errlen, "no HELLO from a controller on %s within %d ms", sc->device,
             HELLO_WAIT_MS);
    goto fail;
  }
  if (!hil_hello_matches(&theirs, u->n_plant, u->n_cmd, hash)) {
    char p[400], c[400];
    list(p, sizeof p, plant, u->n_plant);
    list(c, sizeof c, cmd, u->n_cmd);
    snprintf(err, errlen,
             "the controller on %s expects other values (its HELLO: version %g, %g plant, %g cmd):"
             " this scenario sends %s and reads %s",
             sc->device, (double)theirs.v[0], (double)theirs.v[1], (double)theirs.v[2], p, c);
    goto fail;
  }
  return &u->io;

fail:
  close(u->fd);
  free(u);
  return NULL;
}
