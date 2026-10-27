#include "runner.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "fmu.h"
#include "io.h"
#include "loader.h"
#include "loop.h"
#include "ring.h"
#include "varmap.h"
#include "writer.h"

#define RING_CAPACITY 16384 /* records; about 16 s of buffer at 1 kHz */

static volatile sig_atomic_t g_stop;

void runner_request_stop(void) { g_stop = 1; }

/* Creates the log file's directory if it is a single missing level (e.g. runs/). */
static void ensure_parent_dir(const char *path)
{
  char dir[CFG_MAX_PATH];
  snprintf(dir, sizeof dir, "%s", path);
  char *slash = strrchr(dir, '/');
  if (slash != NULL && slash != dir) {
    *slash = '\0';
    mkdir(dir, 0755);
  }
}

static void describe_failure(const loop_stats_t *ls, const scenario_t *sc, const fmu_t *fmu,
                             char *err, size_t errlen)
{
  switch (ls->end) {
  case LOOP_FMU_FAIL: {
    const char *msg = fmu_last_message(fmu);
    snprintf(err, errlen, "step %llu: %s%s%s%s", (unsigned long long)ls->fail_step,
             fmu_result_str(ls->fail_result), msg[0] != '\0' ? " (" : "", msg,
             msg[0] != '\0' ? ")" : "");
    break;
  }
  case LOOP_IO_FAIL:
    snprintf(err, errlen, "io backend failed after %llu steps", (unsigned long long)ls->steps);
    break;
  case LOOP_TOO_MANY_OVERRUNS:
    snprintf(err, errlen,
             "%llu overruns (rt.max_overruns = %d): the step did not finish within "
             "%g s",
             (unsigned long long)ls->overruns, sc->max_overruns, sc->step);
    break;
  default:
    break;
  }
}

int runner_run(const scenario_t *sc, runner_result_t *res, char *err, size_t errlen)
{
  memset(res, 0, sizeof *res);
  err[0] = '\0';
  g_stop = 0;

  lifecycle_t state = LC_IDLE;
  int rc = RUNNER_SETUP_ERROR;
  fmu_pkg_t pkg = {0};
  varmap_t vm;
  io_t *io = NULL;
  ring_t ring;
  bool ring_ok = false;
  writer_t *writer = NULL;
  fmu_t *fmu = NULL;
  loop_stats_t ls = {0};

  /* LOADED: everything slow and fallible happens before the loop starts. */
  if (fmu_open(sc->fmu, &pkg, err, errlen) < 0 || varmap_build(&pkg.md, sc, &vm, err, errlen) < 0) {
    goto out;
  }
  io = io_open(sc, err, errlen);
  if (io == NULL) {
    goto out;
  }
  if (ring_init(&ring, RING_CAPACITY) < 0) {
    snprintf(err, errlen, "out of memory");
    goto out;
  }
  ring_ok = true;
  const char *in_names[TELEM_MAX_CH], *out_names[TELEM_MAX_CH];
  for (size_t i = 0; i < sc->n_inputs; i++) {
    in_names[i] = sc->inputs[i];
  }
  for (size_t i = 0; i < sc->n_outputs; i++) {
    out_names[i] = sc->outputs[i];
  }
  ensure_parent_dir(sc->log_file);
  writer = writer_start(&ring, sc->log_file, in_names, sc->n_inputs, out_names, sc->n_outputs);
  if (writer == NULL) {
    snprintf(err, errlen, "cannot create log file '%s'", sc->log_file);
    goto out;
  }
  lifecycle_set(&state, LC_LOADED);

  /* INITIALIZED: instantiate, set parameters, initialize the model. */
  fmu = fmi3_create(&pkg, err, errlen);
  if (fmu == NULL) {
    goto out;
  }
  if (vm.n_params > 0) {
    fmu_result_t r = fmu_set_real(fmu, vm.param_vr, vm.n_params, vm.param_value);
    if (r != FMU_OK) {
      snprintf(err, errlen, "setting parameters: %s (%s)", fmu_result_str(r),
               fmu_last_message(fmu));
      goto out;
    }
  }
  fmu_result_t r = fmu_init(fmu, 0.0);
  if (r != FMU_OK) {
    snprintf(err, errlen, "initialization: %s (%s)", fmu_result_str(r), fmu_last_message(fmu));
    goto out;
  }
  lifecycle_set(&state, LC_INITIALIZED);

  /* RUNNING */
  lifecycle_set(&state, LC_RUNNING);
  loop_ctx_t ctx = {.sc = sc, .vm = &vm, .fmu = fmu, .io = io, .ring = &ring, .stop = &g_stop};
  loop_run(&ctx, &ls);

  if (ls.end == LOOP_DONE || ls.end == LOOP_STOPPED) {
    lifecycle_set(&state, LC_STOPPED);
    res->stopped_early = ls.end == LOOP_STOPPED;
    rc = RUNNER_OK;
  } else {
    describe_failure(&ls, sc, fmu, err, errlen);
    io->api->safe_state(io);
    rc = RUNNER_FAULT;
  }

out:
  if (state != LC_STOPPED) {
    lifecycle_set(&state, LC_FAULT);
  }
  if (fmu != NULL) {
    fmu_destroy(fmu);
  }
  if (writer != NULL) {
    res->rows = writer_stop(writer);
  }
  if (ring_ok) {
    res->dropped = ring_dropped(&ring);
    ring_free(&ring);
  }
  if (io != NULL) {
    io->api->close(io);
  }
  fmu_close(&pkg);

  res->state = state;
  res->steps = ls.steps;
  res->overruns = ls.overruns;
  res->max_wake_ns = ls.max_wake_ns;
  res->max_step_ns = ls.max_step_ns;
  res->mean_step_ns = ls.steps > 0 ? ls.sum_step_ns / (double)ls.steps : 0.0;
  return rc;
}
