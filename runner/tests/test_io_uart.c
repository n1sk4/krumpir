/* The uart backend against a fake controller on the master side of a pty pair. */
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hil.h"
#include "io.h"

/* The fake controller: answers HELLO with `hello_hash`, and PLANT(k) with CMD(k) = 2 * value 0,
 * unless k == drop or k >= mute_from. Sends garbage before every answer. */
typedef struct {
  int fd;
  uint32_t hello_hash;
  size_t n_plant, n_cmd;
  int drop, mute_from;
  atomic_int byes, quit;
} fake_t;

static void send_frame(int fd, const hil_frame_t *f)
{
  uint8_t buf[HIL_FRAME_MAX + 3] = {0xA5, 0x00, 0x5A}; /* garbage first */
  const size_t len = 3 + hil_encode(f, buf + 3);
  assert(write(fd, buf, len) == (ssize_t)len);
}

static void *fake_run(void *arg)
{
  fake_t *k = arg;
  hil_parser_t p;
  hil_parser_init(&p);
  while (!k->quit) {
    struct pollfd pf = {k->fd, POLLIN, 0};
    if (poll(&pf, 1, 10) <= 0) {
      continue;
    }
    uint8_t buf[256];
    const ssize_t r = read(k->fd, buf, sizeof buf);
    for (ssize_t i = 0; i < r; i++) {
      hil_frame_t f, a;
      if (!hil_parse_byte(&p, buf[i], &f)) {
        continue;
      }
      if (f.type == HIL_HELLO) {
        hil_hello(&a, k->n_plant, k->n_cmd, k->hello_hash);
        send_frame(k->fd, &a);
      } else if (f.type == HIL_PLANT && f.seq != k->drop && f.seq < k->mute_from) {
        a = (hil_frame_t){HIL_CMD, f.seq, (uint8_t)k->n_cmd, {2 * f.v[0]}};
        send_frame(k->fd, &a);
      } else if (f.type == HIL_BYE) {
        k->byes++;
      }
    }
  }
  return NULL;
}

static scenario_t sc;
static fake_t fake;
static pthread_t thread;

/* A pty pair; the scenario's device is the slave, the fake runs on the master. */
static void start(const char *io_keys, int drop, int mute_from, bool right_hash)
{
  char text[1024];
  snprintf(text, sizeof text,
           "[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
           "[io]\nbackend = \"uart\"\ndevice = \"/dev/null\"\n"
           "inputs = [\"duty\", \"load\"]\noutputs = [\"speed\"]\n"
           "stimulus = [{ input = \"load\", kind = \"constant\", value = 1 }]\n%s"
           "[log]\nfile = \"o.csv\"\n",
           io_keys);
  char err[256];
  if (config_parse(text, &sc, err, sizeof err) != 0) {
    fprintf(stderr, "config: %s\n", err);
    abort();
  }
  const int m = posix_openpt(O_RDWR | O_NOCTTY);
  assert(m >= 0 && grantpt(m) == 0 && unlockpt(m) == 0);
  snprintf(sc.device, sizeof sc.device, "%s", ptsname(m));
  /* Slots: speed out; duty in (load has a stimulus, so it is not the controller's). */
  const char *plant[] = {"speed"}, *cmd[] = {"duty"};
  fake = (fake_t){.fd = m, .n_plant = 1, .n_cmd = 1, .drop = drop, .mute_from = mute_from};
  fake.hello_hash = hil_names_hash(plant, 1, cmd, 1) + (right_hash ? 0 : 1);
  assert(pthread_create(&thread, NULL, fake_run, &fake) == 0);
}

static void stop(void)
{
  fake.quit = 1;
  pthread_join(thread, NULL);
  close(fake.fd);
}

/* One step: in from the controller, then out = speed. */
static int step(io_t *io, double speed, double in[2])
{
  if (io->api->read(io, 0, in, 2) != 0) {
    return -1;
  }
  return io->api->write(io, 0, &speed, 1);
}

static void test_lockstep(void)
{
  start("", 3, 1000, true);
  char err[256];
  io_t *io = io_open(&sc, err, sizeof err);
  if (io == NULL) {
    fprintf(stderr, "%s\n", err);
  }
  assert(io != NULL && strcmp(io->api->name, "uart") == 0);
  double in[2];
  assert(step(io, 10, in) == 0 && in[0] == 0 && in[1] == 0); /* step 0: nothing sent yet */
  for (int k = 1; k < 8; k++) {
    assert(step(io, 10 + k, in) == 0);
    /* CMD(k-1) answers PLANT(k-1) = 10 + k - 1; PLANT 3 is dropped: step 4 holds CMD(2). */
    const double want = 2 * (k == 4 ? 12 : 10 + k - 1);
    assert(in[0] == want);
    assert(in[1] == 0); /* the stimulus input is the loop's */
  }
  assert(io->late == 1);
  io->api->close(io);
  for (int i = 0; i < 100 && fake.byes == 0; i++) {
    poll(NULL, 0, 1);
  }
  assert(fake.byes == 1);
  stop();
}

static void test_late_faults(void)
{
  start("timeout_ms = 5\nmax_late = 3\n", -1, 2, true);
  char err[256];
  io_t *io = io_open(&sc, err, sizeof err);
  assert(io != NULL);
  double in[2];
  int k = 0, rc = 0;
  while (rc == 0 && k < 20) {
    rc = step(io, k++, in);
  }
  /* CMD 0 and 1 come; reads of steps 3, 4, 5 are late, the third faults. */
  assert(rc != 0 && k == 6);
  assert(strstr(io->error, "for 3 steps in a row") != NULL);
  io->api->close(io);
  stop();
}

static void test_late_holds(void)
{
  start("timeout_ms = 2\nmax_late = 2\nlate = \"hold\"\n", -1, 2, true);
  char err[256];
  io_t *io = io_open(&sc, err, sizeof err);
  assert(io != NULL);
  double in[2];
  for (int k = 0; k < 10; k++) {
    assert(step(io, 5, in) == 0);
  }
  assert(in[0] == 10 && io->late == 7);
  io->api->close(io);
  stop();
}

static void test_wrong_hash(void)
{
  start("", -1, 1000, false);
  char err[300];
  assert(io_open(&sc, err, sizeof err) == NULL);
  assert(strstr(err, "sends [speed] and reads [duty]") != NULL);
  stop();
}

static void test_no_device(void)
{
  start("", -1, 1000, true);
  strcpy(sc.device, "/dev/krumpir-none");
  char err[300];
  assert(io_open(&sc, err, sizeof err) == NULL);
  assert(strstr(err, "/dev/krumpir-none") != NULL);
  stop();
}

int main(void)
{
  test_lockstep();
  test_late_faults();
  test_late_holds();
  test_wrong_hash();
  test_no_device();
  puts("test_io_uart: ok");
  return 0;
}
