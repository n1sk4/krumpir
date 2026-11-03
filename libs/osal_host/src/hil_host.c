/* PC port of a controller on the HIL link: see hil_host.h. The link logic is libs/hil
 * (hil_port.c, hil_link.c); this is the tty, the tick task and the two ways of ticking. */
#define _DEFAULT_SOURCE /* cfmakeraw */

#include "hil_host.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "console_host.h"
#include "hil_port.h"
#include "telemetry.h"
#include "tick_task.h"

static int s_fd = -1;
static pthread_mutex_t s_write_mu = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool s_muted;

/* tick_task_start() */
static tick_init_t s_init;
static tick_body_t s_body;
static void *s_ctx;
static uint32_t s_tick;
static uint32_t s_hz;

void tick_task_start(const tick_cfg_t *cfg, tick_init_t init, tick_body_t body, void *ctx)
{
  (void)cfg; /* the rate is hil_host_run's; pin, core and stack are the board's */
  s_init = init;
  s_body = body;
  s_ctx = ctx;
}

// Limitation: no period/exec statistics on the PC yet; the runner measures the loop.
void tick_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns)
{
  loop_stats_reset(period);
  loop_stats_reset(exec);
  *overruns = 0;
}

/* Telemetry: rows straight to stdout from the tick; a PC can afford the printf. */
static const char *s_header;
static telemetry_row_t s_row;
static atomic_bool s_log;

int telemetry_round(float x) { return (int)(x < 0.0f ? x - 0.5f : x + 0.5f); }
void telemetry_start(const char *csv_header, telemetry_row_t print_row)
{
  s_header = csv_header;
  s_row = print_row;
}
void telemetry_push(const telem_sample_t *s)
{
  if (s_log) {
    s_row(s);
  }
}
void telemetry_set_log(bool on)
{
  if (on && !s_log) {
    printf("%s\n", s_header);
  }
  s_log = on;
}
bool telemetry_log(void) { return s_log; }
uint32_t telemetry_dropped(void) { return 0; }

int hil_host_open(const char *device, char *err, size_t errlen)
{
  s_fd = open(device, O_RDWR | O_NOCTTY);
  if (s_fd < 0) {
    snprintf(err, errlen, "cannot open %s: %s", device, strerror(errno));
    return -1;
  }
  struct termios tio;
  if (tcgetattr(s_fd, &tio) != 0) {
    snprintf(err, errlen, "%s is not a serial line: %s", device, strerror(errno));
    return -1;
  }
  cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD;
  cfsetispeed(&tio, B921600);
  cfsetospeed(&tio, B921600);
  if (tcsetattr(s_fd, TCSANOW, &tio) != 0) {
    snprintf(err, errlen, "cannot set up %s: %s", device, strerror(errno));
    return -1;
  }
  tcflush(s_fd, TCIOFLUSH);
  return 0;
}

/* hil_port.h: the line, from the tick (commands) and the reader (HELLO answers) */
void hil_port_write(const uint8_t *buf, size_t n)
{
  if (s_muted) {
    return;
  }
  pthread_mutex_lock(&s_write_mu);
  for (size_t off = 0; off < n;) {
    const ssize_t w = write(s_fd, buf + off, n - off);
    if (w > 0) {
      off += (size_t)w;
    } else if (errno != EINTR) {
      break; /* the runner went away: it sees the CMD missing */
    }
  }
  pthread_mutex_unlock(&s_write_mu);
}

static uint32_t s_mute_at = UINT32_MAX;
void hil_host_mute_at(uint32_t tick) { s_mute_at = tick; }

static void (*s_after)(uint32_t tick);
void hil_host_after_tick(void (*fn)(uint32_t tick)) { s_after = fn; }

/* Timed console commands (hil_host_at) */
static struct {
  uint32_t tick;
  const char *line;
} s_at[16];
static size_t s_n_at;

bool hil_host_at(uint32_t tick, const char *line)
{
  if (s_n_at == sizeof s_at / sizeof s_at[0]) {
    return false;
  }
  s_at[s_n_at].tick = tick;
  s_at[s_n_at++].line = line;
  return true;
}

/* One tick. The body takes the plant values and sends its commands (hil_port_tick_begin/end). */
static void tick(void)
{
  for (size_t i = 0; i < s_n_at; i++) {
    if (s_at[i].line != NULL && s_at[i].tick <= s_tick) {
      console_host_exec(s_at[i].line); /* applied by this tick's rte_tick_begin */
      s_at[i].line = NULL;
    }
  }
  if (s_tick == s_mute_at) {
    fprintf(stderr, "hil: muted at tick %u\n", (unsigned)s_tick);
    s_muted = true;
  }
  s_body(s_ctx, s_tick);
  if (s_after != NULL) {
    s_after(s_tick);
  }
  s_tick++;
}

// Limitation: plain SCHED_OTHER; set SCHED_FIFO here once free-running runs on a real-time host.
static void *tick_thread(void *arg)
{
  (void)arg;
  const long period_ns = 1000000000L / (long)s_hz;
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  for (;;) {
    next.tv_nsec += period_ns;
    if (next.tv_nsec >= 1000000000L) {
      next.tv_nsec -= 1000000000L;
      next.tv_sec++;
    }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {
    }
    tick();
  }
  return NULL;
}

int hil_host_run(bool lockstep, uint32_t hz)
{
  s_hz = hz > 0 ? hz : 1000;
  if (s_init != NULL) {
    s_init(s_ctx);
  }
  bool started = false;
  for (;;) {
    struct pollfd pf = {s_fd, POLLIN, 0};
    if (poll(&pf, 1, -1) < 0 && errno != EINTR) {
      break;
    }
    uint8_t buf[512];
    const ssize_t r = read(s_fd, buf, sizeof buf);
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
      fprintf(stderr, "hil: the device %s\n", r == 0 ? "closed" : strerror(errno));
      return -1;
    }
    for (ssize_t i = 0; i < r; i++) {
      const int type = hil_port_feed(buf[i]);
      if (type == -HIL_HELLO) {
        fprintf(stderr, "hil: the runner's HELLO is for other values; it will refuse the link\n");
      } else if (type == HIL_PLANT && lockstep) {
        tick(); /* one tick per plant frame, answered with its seq */
      } else if (type == HIL_PLANT && !started) {
        pthread_t t; /* free-running: the clock starts with the first plant frame */
        started = pthread_create(&t, NULL, tick_thread, NULL) == 0;
      } else if (type == HIL_BYE) {
        fprintf(stderr, "hil: BYE from the runner: stop\n");
        if (lockstep) {
          tick(); /* the control task sees the BYE and stops; the runner no longer reads */
        }
        fflush(stdout);
      }
    }
  }
  fprintf(stderr, "hil: %s\n", strerror(errno));
  return -1;
}
