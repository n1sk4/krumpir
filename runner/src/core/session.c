#include "session.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cmdq.h"
#include "fmu.h"
#include "io.h"
#include "loader.h"
#include "loop.h"
#include "ring.h"
#include "varmap.h"
#include "writer.h"

#define RING_CAPACITY 16384  /* records; about 16 s of buffer at 1 kHz */
#define REPLY_TIMEOUT_MS 2000 /* a command must be handled within this time */

struct session {
  lifecycle_t state;
  scenario_t sc;
  fmu_pkg_t pkg;
  bool pkg_open;
  varmap_t vm;
  io_t *io;
  ring_t ring;
  bool ring_ok;
  writer_t *writer;
  fmu_t *fmu;

  pthread_t thread;
  bool thread_started;
  atomic_bool done; /* the loop thread has returned */
  volatile sig_atomic_t stop;
  loop_stats_t ls;

  cmdq_t cmd, reply;
  uint32_t seq;
  bool override_on[TELEM_MAX_CH];
  double override_value[TELEM_MAX_CH];
  char fault[512];
};

session_t *session_create(void) { return calloc(1, sizeof(session_t)); }

volatile sig_atomic_t *session_stop_flag(session_t *s) { return &s->stop; }

const char *session_fault(const session_t *s) { return s->fault; }

const modeldesc_t *session_model(const session_t *s)
{
  return s->pkg_open ? &s->pkg.md : NULL;
}

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

static void describe_failure(session_t *s)
{
  const loop_stats_t *ls = &s->ls;
  switch (ls->end) {
  case LOOP_FMU_FAIL: {
    const char *msg = fmu_last_message(s->fmu);
    snprintf(s->fault, sizeof s->fault, "step %llu: %s%s%s%s", (unsigned long long)ls->fail_step,
             fmu_result_str(ls->fail_result), msg[0] != '\0' ? " (" : "", msg,
             msg[0] != '\0' ? ")" : "");
    break;
  }
  case LOOP_IO_FAIL:
    snprintf(s->fault, sizeof s->fault, "io backend failed after %llu steps",
             (unsigned long long)ls->steps);
    break;
  case LOOP_TOO_MANY_OVERRUNS:
    snprintf(s->fault, sizeof s->fault,
             "%llu overruns (rt.max_overruns = %d): the step did not finish within %g s",
             (unsigned long long)ls->overruns, s->sc.max_overruns, s->sc.step);
    break;
  default:
    break;
  }
}

/* Joins the loop thread once it has returned and settles the state. */
static void reap(session_t *s)
{
  if (!s->thread_started || !atomic_load(&s->done)) {
    return;
  }
  pthread_join(s->thread, NULL);
  s->thread_started = false;
  if (s->ls.end == LOOP_DONE || s->ls.end == LOOP_STOPPED) {
    lifecycle_set(&s->state, LC_STOPPED);
  } else {
    describe_failure(s);
    s->io->api->safe_state(s->io);
    lifecycle_set(&s->state, LC_FAULT);
  }
}

lifecycle_t session_state(session_t *s)
{
  reap(s);
  return s->state;
}

void session_unload(session_t *s, session_summary_t *out)
{
  session_stop(s);
  session_summary_t sum = {0};
  sum.state = s->state;
  sum.stopped_early = s->ls.end == LOOP_STOPPED;
  sum.steps = s->ls.steps;
  sum.overruns = s->ls.overruns;
  sum.max_wake_ns = s->ls.max_wake_ns;
  sum.max_step_ns = s->ls.max_step_ns;
  sum.mean_step_ns = s->ls.steps > 0 ? s->ls.sum_step_ns / (double)s->ls.steps : 0.0;

  if (s->fmu != NULL) {
    fmu_destroy(s->fmu);
  }
  if (s->writer != NULL) {
    sum.rows = writer_stop(s->writer);
  }
  if (s->ring_ok) {
    sum.dropped = ring_dropped(&s->ring);
    ring_free(&s->ring);
  }
  if (s->io != NULL) {
    s->io->api->close(s->io);
  }
  if (s->pkg_open) {
    fmu_close(&s->pkg);
  }
  if (out != NULL) {
    *out = sum;
  }
  char fault[sizeof s->fault];
  memcpy(fault, s->fault, sizeof fault); /* stays readable until the next load */
  memset(s, 0, sizeof *s);
  memcpy(s->fault, fault, sizeof fault);
  s->state = LC_IDLE;
}

void session_destroy(session_t *s)
{
  if (s != NULL) {
    session_unload(s, NULL);
    free(s);
  }
}

int session_load(session_t *s, const scenario_t *sc, char *err, size_t errlen)
{
  err[0] = '\0';
  if (s->state != LC_IDLE) {
    snprintf(err, errlen, "a scenario is already loaded");
    return -1;
  }
  s->fault[0] = '\0';
  s->sc = *sc;
  s->stop = 0;
  cmdq_init(&s->cmd);
  cmdq_init(&s->reply);

  /* LOADED: everything slow and fallible happens before the loop starts. */
  if (fmu_open(sc->fmu, &s->pkg, err, errlen) < 0) {
    goto fail;
  }
  s->pkg_open = true;
  if (varmap_build(&s->pkg.md, &s->sc, &s->vm, err, errlen) < 0) {
    goto fail;
  }
  s->io = io_open(&s->sc, err, errlen);
  if (s->io == NULL) {
    goto fail;
  }
  if (ring_init(&s->ring, RING_CAPACITY) < 0) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }
  s->ring_ok = true;
  const char *in_names[TELEM_MAX_CH], *out_names[TELEM_MAX_CH];
  for (size_t i = 0; i < sc->n_inputs; i++) {
    in_names[i] = s->sc.inputs[i];
  }
  for (size_t i = 0; i < sc->n_outputs; i++) {
    out_names[i] = s->sc.outputs[i];
  }
  ensure_parent_dir(sc->log_file);
  s->writer =
      writer_start(&s->ring, s->sc.log_file, in_names, sc->n_inputs, out_names, sc->n_outputs);
  if (s->writer == NULL) {
    snprintf(err, errlen, "cannot create log file '%s'", sc->log_file);
    goto fail;
  }
  lifecycle_set(&s->state, LC_LOADED);

  /* INITIALIZED: instantiate, set parameters, initialize the model. */
  s->fmu = fmi3_create(&s->pkg, err, errlen);
  if (s->fmu == NULL) {
    goto fail;
  }
  if (s->vm.n_params > 0) {
    fmu_result_t r = fmu_set_real(s->fmu, s->vm.param_vr, s->vm.n_params, s->vm.param_value);
    if (r != FMU_OK) {
      snprintf(err, errlen, "setting parameters: %s (%s)", fmu_result_str(r),
               fmu_last_message(s->fmu));
      goto fail;
    }
  }
  fmu_result_t r = fmu_init(s->fmu, 0.0);
  if (r != FMU_OK) {
    snprintf(err, errlen, "initialization: %s (%s)", fmu_result_str(r), fmu_last_message(s->fmu));
    goto fail;
  }
  lifecycle_set(&s->state, LC_INITIALIZED);
  return 0;

fail:
  session_unload(s, NULL);
  return -1;
}

static void *loop_thread(void *arg)
{
  session_t *s = arg;
  loop_ctx_t ctx = {.sc = &s->sc,
                    .vm = &s->vm,
                    .fmu = s->fmu,
                    .io = s->io,
                    .ring = &s->ring,
                    .stop = &s->stop,
                    .cmd = &s->cmd,
                    .reply = &s->reply,
                    .override_on = s->override_on,
                    .override_value = s->override_value};
  loop_run(&ctx, &s->ls);
  atomic_store(&s->done, true);
  return NULL;
}

int session_start(session_t *s, char *err, size_t errlen)
{
  if (s->state != LC_INITIALIZED) {
    snprintf(err, errlen, "cannot start in state %s", lifecycle_name(s->state));
    return -1;
  }
  atomic_store(&s->done, false);
  lifecycle_set(&s->state, LC_RUNNING);
  int e = pthread_create(&s->thread, NULL, loop_thread, s);
  if (e != 0) {
    snprintf(err, errlen, "cannot start the loop thread: %s", strerror(e));
    lifecycle_set(&s->state, LC_FAULT);
    return -1;
  }
  s->thread_started = true;
  return 0;
}

static void sleep_ms(double ms)
{
  struct timespec ts = {(time_t)(ms / 1000.0), (long)(((long long)(ms * 1e6)) % 1000000000LL)};
  nanosleep(&ts, NULL);
}

bool session_wait(session_t *s, int timeout_ms)
{
  for (double waited = 0.0; timeout_ms < 0 || waited < timeout_ms; waited += 0.5) {
    if (!s->thread_started || atomic_load(&s->done)) {
      reap(s);
      return true;
    }
    sleep_ms(0.5);
  }
  return false;
}

void session_stop(session_t *s)
{
  if (s->thread_started) {
    s->stop = 1;
    session_wait(s, -1);
  }
}

/* Sends a command to the running loop and waits for its reply. */
static int roundtrip(session_t *s, cmd_t *c, char *err, size_t errlen)
{
  c->seq = ++s->seq;
  if (!cmdq_push(&s->cmd, c)) {
    snprintf(err, errlen, "command queue full");
    return -1;
  }
  for (double waited = 0.0; waited < REPLY_TIMEOUT_MS; waited += 0.1) {
    cmd_t r;
    while (cmdq_pop(&s->reply, &r)) {
      if (r.seq == c->seq) {
        *c = r;
        return 0;
      }
    }
    if (atomic_load(&s->done)) {
      snprintf(err, errlen, "the run ended before the command was handled");
      return -1;
    }
    sleep_ms(0.1);
  }
  snprintf(err, errlen, "no reply from the loop within %d ms", REPLY_TIMEOUT_MS);
  return -1;
}

/* The variable `name`, which must be Float64. */
static const mvar_t *find_float64(session_t *s, const char *name, char *err, size_t errlen)
{
  if (!s->pkg_open) {
    snprintf(err, errlen, "no scenario loaded");
    return NULL;
  }
  const mvar_t *v = modeldesc_find(&s->pkg.md, name);
  if (v == NULL) {
    snprintf(err, errlen, "'%s' is not a variable of the model", name);
    return NULL;
  }
  if (!mvar_is_float64(v)) {
    snprintf(err, errlen, "'%s' is %s, only Float64 is supported", name, v->type);
    return NULL;
  }
  return v;
}

int session_write(session_t *s, const char *name, double value, char *err, size_t errlen)
{
  reap(s);
  const mvar_t *v = find_float64(s, name, err, errlen);
  if (v == NULL) {
    return -1;
  }
  if (s->state != LC_INITIALIZED && s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot write '%s' in state %s", name, lifecycle_name(s->state));
    return -1;
  }

  cmd_t c = {.vr = v->vr, .value = value, .index = -1};
  if (v->causality == CAUS_INPUT) {
    for (size_t i = 0; i < s->vm.n_in; i++) {
      if (s->vm.in_vr[i] == v->vr) {
        c.index = (int32_t)i;
      }
    }
    if (c.index < 0) {
      snprintf(err, errlen, "input '%s' is not one of the scenario's inputs", name);
      return -1;
    }
    if (s->state == LC_INITIALIZED) {
      s->override_value[c.index] = value;
      s->override_on[c.index] = true;
      return 0;
    }
    c.kind = CMD_WRITE_INPUT;
  } else if (v->causality == CAUS_PARAMETER && v->tunable) {
    if (s->state == LC_INITIALIZED) {
      fmu_result_t r = fmu_set_real(s->fmu, &c.vr, 1, &c.value);
      if (r != FMU_OK) {
        snprintf(err, errlen, "'%s': %s (%s)", name, fmu_result_str(r), fmu_last_message(s->fmu));
        return -1;
      }
      return 0;
    }
    c.kind = CMD_WRITE_PARAM;
  } else if (v->causality == CAUS_PARAMETER) {
    snprintf(err, errlen, "'%s' is a fixed parameter: set it in the scenario (model.params)",
             name);
    return -1;
  } else {
    snprintf(err, errlen,
             "'%s' has causality '%s': only inputs and tunable parameters can be written", name,
             v->causality_str);
    return -1;
  }

  if (roundtrip(s, &c, err, errlen) < 0) {
    return -1;
  }
  if (c.status != FMU_OK) {
    snprintf(err, errlen, "'%s': %s", name, fmu_result_str((fmu_result_t)c.status));
    return -1;
  }
  return 0;
}

int session_read(session_t *s, const char *name, double *value, char *err, size_t errlen)
{
  reap(s);
  const mvar_t *v = find_float64(s, name, err, errlen);
  if (v == NULL) {
    return -1;
  }
  if (s->state == LC_INITIALIZED || s->state == LC_STOPPED) {
    uint32_t vr = v->vr;
    fmu_result_t r = fmu_get_real(s->fmu, &vr, 1, value);
    if (r != FMU_OK) {
      snprintf(err, errlen, "'%s': %s", name, fmu_result_str(r));
      return -1;
    }
    return 0;
  }
  if (s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot read '%s' in state %s", name, lifecycle_name(s->state));
    return -1;
  }
  cmd_t c = {.kind = CMD_READ, .vr = v->vr};
  if (roundtrip(s, &c, err, errlen) < 0) {
    return -1;
  }
  if (c.status != FMU_OK) {
    snprintf(err, errlen, "'%s': %s", name, fmu_result_str((fmu_result_t)c.status));
    return -1;
  }
  *value = c.value;
  return 0;
}
