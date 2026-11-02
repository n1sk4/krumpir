#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "loader.h"
#include "runner.h"
#include "scaffold.h"
#include "serve.h"

static int cmd_info(const char *path)
{
  fmu_pkg_t pkg;
  char err[512];
  if (fmu_open(path, &pkg, err, sizeof err) < 0) {
    fprintf(stderr, "krumpir: %s\n", err);
    return 1;
  }
  printf("fmu:             %s\n", path);
  printf("fmiVersion:      %s\n", pkg.md.fmi_version);
  printf("modelIdentifier: %s\n", pkg.md.model_identifier);
  if (pkg.md.step_size > 0 || pkg.md.stop_time > 0) {
    printf("default:         step %g s, stop %g s\n", pkg.md.step_size, pkg.md.stop_time);
  }
  printf("variables:       %zu\n\n", pkg.md.n_vars);
  printf("%-32s %6s  %-8s %-12s %-12s %-10s %s\n", "name", "vr", "type", "causality", "start",
         "unit", "description");
  for (size_t i = 0; i < pkg.md.n_vars; i++) {
    const mvar_t *v = &pkg.md.vars[i];
    printf("%-32s %6u  %-8s %-12s %-12s %-10s %s\n", v->name, v->vr, v->type, v->causality_str,
           v->start, v->unit, v->description);
  }
  fmu_close(&pkg);
  return 0;
}

static void on_signal(int sig)
{
  (void)sig;
  runner_request_stop();
}

static int cmd_run(const char *path)
{
  scenario_t sc;
  char err[512];
  if (config_load(path, &sc, err, sizeof err) < 0) {
    fprintf(stderr, "krumpir: %s: %s\n", path, err);
    return RUNNER_SETUP_ERROR;
  }

  /* Ctrl-C and SIGTERM end the run cleanly after the current step. No SA_RESTART, so a sleeping
   * loop wakes up at once. */
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  runner_result_t res;
  int rc = runner_run(&sc, &res, err, sizeof err);
  if (rc != RUNNER_OK) {
    fprintf(stderr, "krumpir: %s\n", err);
  }
  if (rc == RUNNER_SETUP_ERROR) {
    return rc;
  }
  printf("state:     %s%s\n", lifecycle_name(res.state),
         res.stopped_early ? " (stopped early)" : "");
  if (sc.n_steps == UINT64_MAX) {
    printf("steps:     %llu (no duration: until stopped)\n", (unsigned long long)res.steps);
  } else {
    printf("steps:     %llu of %llu\n", (unsigned long long)res.steps,
           (unsigned long long)sc.n_steps);
  }
  printf("overruns:  %llu\n", (unsigned long long)res.overruns);
  printf("dropped:   %llu\n", (unsigned long long)res.dropped);
  printf("rows:      %llu -> %s\n", (unsigned long long)res.rows, sc.log_file);
  printf("wake_ns:   max %lld\n", (long long)res.max_wake_ns);
  printf("step_ns:   mean %.0f, max %lld\n", res.mean_step_ns, (long long)res.max_step_ns);
  return rc;
}

int main(int argc, char **argv)
{
  if (argc == 3 && strcmp(argv[1], "info") == 0) {
    return cmd_info(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "init") == 0) {
    char err[512];
    if (scaffold_scenario(stdout, argv[2], err, sizeof err) < 0) {
      fprintf(stderr, "krumpir: %s\n", err);
      return 1;
    }
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "run") == 0) {
    return cmd_run(argv[2]);
  }
  if ((argc == 2 || argc == 3) && strcmp(argv[1], "serve") == 0) {
    /* [host:]port, default 127.0.0.1:7700 */
    char host[64] = "127.0.0.1";
    int port = 7700;
    const char *arg = argc == 3 ? argv[2] : "";
    const char *colon = strrchr(arg, ':');
    if (colon != NULL && (size_t)(colon - arg) < sizeof host) {
      snprintf(host, sizeof host, "%.*s", (int)(colon - arg), arg);
      arg = colon + 1;
    }
    char *end;
    if (*arg != '\0') {
      port = (int)strtol(arg, &end, 10);
      if (*end != '\0') {
        port = -1;
      }
    }
    return serve(host, port);
  }
  fprintf(stderr, "usage: krumpir run <scenario.toml>\n"
                  "       krumpir serve [[host:]port]   (default 127.0.0.1:7700)\n"
                  "       krumpir info <model.fmu>\n"
                  "       krumpir init <model.fmu>      (prints a starter scenario)\n");
  return 2;
}
