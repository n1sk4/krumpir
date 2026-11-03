/* A controller on a PC, on the HIL link: the same control task, console and generated tables as
 * the board, with hil_host.c as the platform. Commands on stdin, CSV telemetry on stdout.
 *
 *   <name>_host --device /dev/pts/N [--lockstep | --hz 1000] [--log] [-c "sp 300"]... [--xcp 5555] */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_host.h"
#include "hil_host.h"
#include "rte.h"
#include "telemetry.h"

/* The controller's (src/control_task.c, src/console.c, src/gen/ctrl_gen.c) */
void control_task_start(void);
void console_start(void);
void ctrl_gen_telemetry_start(void);

rte_t *control_task_rte(void);

/* A trip latched or cleared, on stderr with its tick (plant time in lockstep), for the tools */
static void report_trip(uint32_t tick)
{
  static const char *last;
  const char *trip = rte_trip(control_task_rte()); /* a static name, or NULL */
  if (trip != last) {
    fprintf(stderr, "trip: %s at tick %u\n", trip != NULL ? trip : "cleared", (unsigned)tick);
    last = trip;
  }
}

/* xcp_host.c, in a build with -DKRUMPIR_XCP=ON only */
bool xcp_host_start(uint16_t port) __attribute__((weak));

static void run_line(char *line)
{
  line[strcspn(line, "\r\n")] = '\0';
  if (line[0] == '\0') {
    return;
  }
  if (!strcmp(line, "log on") || !strcmp(line, "log off")) {
    telemetry_set_log(!strcmp(line, "log on"));
  } else if (console_host_exec(line) < 0) {
    fprintf(stderr, "unknown command: %s\n", line);
  }
  fflush(stdout);
}

static void *console_thread(void *arg)
{
  (void)arg;
  char line[200];
  while (fgets(line, sizeof line, stdin) != NULL) {
    run_line(line);
  }
  return NULL;
}

static int usage(const char *prog)
{
  fprintf(stderr,
          "usage: %s --device DEV [--lockstep | --hz N] [--log] [-c COMMAND]...\n"
          "  --device DEV  the serial line to the runner (a pty pair from socat, or a UART)\n"
          "  --lockstep    one tick per plant frame (default); --hz N: own clock, N ticks/s\n"
          "  --log         CSV telemetry on stdout from the start (else: `log on` on stdin)\n"
          "  -c COMMAND    a console command at boot, e.g. -c \"sp 300\" -c start\n"
          "  --at S COMMAND  a console command at plant time S seconds (tick S * N; lockstep: the\n"
          "                runner's step at 1 kHz), e.g. --at 0.1 \"sp 3000\"\n"
          "  --mute-at S   stop answering at plant time S (keeps ticking): a broken link, for tests\n"
          "  --xcp PORT    XCP on UDP on this port (a build with -DKRUMPIR_XCP=ON)\n",
          prog);
  return 2;
}

int main(int argc, char **argv)
{
  const char *device = NULL;
  bool lockstep = true, log = false;
  unsigned hz = 1000, xcp_port = 0;
  char *cmds[32], *at_cmd[16];
  double at_s[16], mute_s = -1;
  int n_cmds = 0, n_at = 0;
  for (int i = 1; i < argc; i++) {
    const bool more = i + 1 < argc;
    if (!strcmp(argv[i], "--device") && more) {
      device = argv[++i];
    } else if (!strcmp(argv[i], "--lockstep")) {
      lockstep = true;
    } else if (!strcmp(argv[i], "--hz") && more) {
      lockstep = false;
      hz = (unsigned)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--xcp") && more && xcp_host_start != NULL) {
      xcp_port = (unsigned)strtoul(argv[++i], NULL, 10);
    } else if (!strcmp(argv[i], "--log")) {
      log = true;
    } else if (!strcmp(argv[i], "-c") && more && n_cmds < 32) {
      cmds[n_cmds++] = argv[++i];
    } else if (!strcmp(argv[i], "--mute-at") && more) {
      mute_s = strtod(argv[++i], NULL);
    } else if (!strcmp(argv[i], "--at") && i + 2 < argc && n_at < 16) {
      at_s[n_at] = strtod(argv[++i], NULL);
      at_cmd[n_at++] = argv[++i];
    } else {
      return usage(argv[0]);
    }
  }
  if (device == NULL || hz == 0 || hz > 100000 || xcp_port > 65535) {
    return usage(argv[0]);
  }
  if (xcp_port != 0 && !xcp_host_start((uint16_t)xcp_port)) { /* before the control task */
    return 1;
  }

  char err[300];
  if (hil_host_open(device, err, sizeof err) != 0) {
    fprintf(stderr, "%s\n", err);
    return 1;
  }
  ctrl_gen_telemetry_start();
  telemetry_set_log(log);
  control_task_start(); /* binds the HIL slots; the ticks come from hil_host_run */
  console_start();
  for (int i = 0; i < n_cmds; i++) {
    run_line(cmds[i]);
  }
  hil_host_after_tick(report_trip);
  if (mute_s >= 0) {
    hil_host_mute_at((uint32_t)(mute_s * hz + 0.5));
  }
  for (int i = 0; i < n_at; i++) {
    hil_host_at((uint32_t)(at_s[i] * hz + 0.5), at_cmd[i]);
  }
  pthread_t t;
  pthread_create(&t, NULL, console_thread, NULL);
  fprintf(stderr, "on %s, %s\n", device, lockstep ? "lockstep" : "free-running");
  return hil_host_run(lockstep, hz) == 0 ? 0 : 1;
}
