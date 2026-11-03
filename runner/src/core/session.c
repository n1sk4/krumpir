#include "session.h"

#include <errno.h>
#include <math.h>
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
#include "rt_setup.h"
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
  bool mem_locked; /* rt_lock_memory() was called for this load */
  atomic_bool done; /* the loop thread has returned */
  atomic_int stop;
  loop_stats_t ls;

  cmdq_t cmd, reply;
  uint32_t seq;
  input_src_t src[TELEM_MAX_CH]; /* the loop owns it while RUNNING; the session owns the signals */

  /* Captures, fed by the writer thread (tap) and read by the control thread, under cap_lock */
  pthread_mutex_t cap_lock;
  bool cap_lock_ok;
  capture_t *caps[SESSION_MAX_CAPTURES];
  const char *cap_names[SESSION_MAX_CAPTURES][1 + 2 * TELEM_MAX_CH];
  char fault[512];
  bool append_log;        /* the next load continues the log file (set by session_reset) */
  writer_cont_t log_cont; /* where it continues: the steps and time of the runs before */
};

session_t *session_create(void) { return calloc(1, sizeof(session_t)); }

/* The stop flag is set from signal handlers: only a lock-free atomic is async-signal-safe. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2,
               "the stop flag needs lock-free atomics");

atomic_int *session_stop_flag(session_t *s) { return &s->stop; }

const char *session_fault(const session_t *s) { return s->fault; }

const modeldesc_t *session_model(const session_t *s)
{
  return s->pkg_open ? &s->pkg.md : NULL;
}

const scenario_t *session_scenario(const session_t *s)
{
  return s->state != LC_IDLE ? &s->sc : NULL;
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
    snprintf(s->fault, sizeof s->fault, "io backend failed after %llu steps%s%s",
             (unsigned long long)ls->steps, s->io->error[0] ? ": " : "", s->io->error);
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
  writer_flush(s->writer); /* the captures have seen every record of the run */
  pthread_mutex_lock(&s->cap_lock);
  for (size_t i = 0; i < SESSION_MAX_CAPTURES; i++) {
    if (s->caps[i] != NULL) {
      capture_finish(s->caps[i]);
    }
  }
  pthread_mutex_unlock(&s->cap_lock);
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

int session_reset(session_t *s, session_summary_t *out, char *err, size_t errlen)
{
  const lifecycle_t st = session_state(s);
  if (st != LC_STOPPED && st != LC_FAULT) {
    snprintf(err, errlen, "cannot reset in state %s (only STOPPED or FAULT)", lifecycle_name(st));
    return -1;
  }
  scenario_t *sc = malloc(sizeof *sc); /* large: off the stack */
  if (sc == NULL) {
    snprintf(err, errlen, "out of memory");
    return -1;
  }
  *sc = s->sc;
  session_summary_t sum;
  session_unload(s, &sum);
  if (out != NULL) {
    *out = sum;
  }
  /* The new run's rows go after the old ones, n and t counting on from where it ended */
  s->log_cont.n += sum.steps;
  s->log_cont.t += (double)sum.steps * sc->step;
  s->append_log = true;
  const int rc = session_load(s, sc, err, errlen);
  s->append_log = false;
  free(sc);
  return rc;
}

void session_live(session_t *s, session_summary_t *out)
{
  *out = (session_summary_t){
      .state = session_state(s),
      .steps = atomic_load_explicit(&s->ls.steps, memory_order_relaxed),
      .overruns = atomic_load_explicit(&s->ls.overruns, memory_order_relaxed),
      .max_wake_ns = atomic_load_explicit(&s->ls.max_wake_ns, memory_order_relaxed),
      .max_step_ns = atomic_load_explicit(&s->ls.max_step_ns, memory_order_relaxed),
      .late = s->io != NULL ? atomic_load_explicit(&s->io->late, memory_order_relaxed) : 0,
  };
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
  for (size_t i = 0; i < SESSION_MAX_CAPTURES; i++) {
    capture_free(s->caps[i]); /* the writer thread has ended */
  }
  if (s->cap_lock_ok) {
    pthread_mutex_destroy(&s->cap_lock);
  }
  if (s->ring_ok) {
    sum.dropped = ring_dropped(&s->ring);
    ring_free(&s->ring);
  }
  if (s->io != NULL) {
    sum.late = s->io->late;
    s->io->api->close(s->io);
  }
  if (s->pkg_open) {
    fmu_close(&s->pkg);
  }
  if (s->mem_locked) {
    rt_unlock_memory();
  }
  for (size_t i = 0; i < TELEM_MAX_CH; i++) {
    if (s->src[i].kind == SRC_SIGNAL) {
      signal_free(s->src[i].sig);
    }
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

/* Writer tap: every telemetry record goes to the captures. */
static void feed_captures(void *ctx, const telem_rec_t *rec)
{
  session_t *s = ctx;
  pthread_mutex_lock(&s->cap_lock);
  for (size_t i = 0; i < SESSION_MAX_CAPTURES; i++) {
    if (s->caps[i] != NULL) {
      capture_feed(s->caps[i], rec);
    }
  }
  pthread_mutex_unlock(&s->cap_lock);
}

int session_load(session_t *s, const scenario_t *sc, char *err, size_t errlen)
{
  err[0] = '\0';
  if (s->state != LC_IDLE) {
    snprintf(err, errlen, "a scenario is already loaded");
    return -1;
  }
  s->fault[0] = '\0';
  if (!s->append_log) { /* a new scenario: a new log */
    s->log_cont = (writer_cont_t){0};
  }
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
  for (size_t j = 0; j < sc->n_stimuli; j++) {
    for (size_t i = 0; i < sc->n_inputs; i++) {
      if (strcmp(sc->stimuli[j].input, sc->inputs[i]) == 0) {
        s->src[i].sig = signal_create(&sc->stimuli[j], err, errlen);
        if (s->src[i].sig == NULL) {
          goto fail;
        }
        s->src[i].kind = SRC_SIGNAL;
      }
    }
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
  if (pthread_mutex_init(&s->cap_lock, NULL) != 0) {
    snprintf(err, errlen, "cannot create a mutex");
    goto fail;
  }
  s->cap_lock_ok = true;
  s->writer = writer_start(&s->ring, s->sc.log_file, s->append_log ? &s->log_cont : NULL,
                           in_names, sc->n_inputs, out_names, sc->n_outputs, feed_captures, s);
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
  /* A real-time run locks the process's memory, so the loop never waits for a page. */
  if (s->sc.priority >= 0) {
    if (rt_lock_memory(err, errlen) < 0) {
      goto fail;
    }
    s->mem_locked = true;
  }
  lifecycle_set(&s->state, LC_INITIALIZED);
  return 0;

fail:
  session_unload(s, NULL);
  return -1;
}

int scenario_check(const scenario_t *sc, char *err, size_t errlen)
{
  static varmap_t vm; /* ponytail: large; one control thread */
  fmu_pkg_t pkg = {0};
  err[0] = '\0';
  int rc = fmu_open(sc->fmu, &pkg, err, errlen);
  if (rc == 0) {
    rc = varmap_build(&pkg.md, sc, &vm, err, errlen);
  }
  for (size_t j = 0; rc == 0 && j < sc->n_stimuli; j++) {
    signal_t *g = signal_create(&sc->stimuli[j], err, errlen);
    rc = g != NULL ? 0 : -1;
    signal_free(g);
  }
  fmu_close(&pkg);
  return rc;
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
                    .src = s->src};
  rt_prefault_stack();
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
  /* The loop thread gets rt.cpu and rt.priority from its creation on. If the OS refuses them,
   * nothing has run and the session stays INITIALIZED. */
  pthread_attr_t attr;
  if (rt_thread_attr(&attr, s->sc.cpu, s->sc.priority, err, errlen) < 0) {
    return -1;
  }
  atomic_store(&s->done, false);
  memset(&s->ls, 0, sizeof s->ls); /* before the thread exists: session_live reads it */
  int e = pthread_create(&s->thread, &attr, loop_thread, s);
  pthread_attr_destroy(&attr);
  if (e != 0) {
    rt_create_error(e, s->sc.cpu, s->sc.priority, err, errlen);
    return -1;
  }
  s->thread_started = true;
  lifecycle_set(&s->state, LC_RUNNING); /* only this thread changes the state, so before reap */
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

/* Position of input `v` in the scenario's inputs, or -1 with a message in err. */
static int input_slot(const session_t *s, const mvar_t *v, char *err, size_t errlen)
{
  for (size_t i = 0; i < s->vm.n_in; i++) {
    if (s->vm.in_vr[i] == v->vr) {
      return (int)i;
    }
  }
  snprintf(err, errlen, "input '%s' is not one of the scenario's inputs", v->name);
  return -1;
}

/* Sets the source of input slot i while not RUNNING, freeing the signal it replaces. */
static void set_src(session_t *s, int i, input_src_t src)
{
  if (s->src[i].kind == SRC_SIGNAL) {
    signal_free(s->src[i].sig);
  }
  s->src[i] = src;
}

int session_set_signal(session_t *s, const stimulus_t *st, char *err, size_t errlen)
{
  reap(s);
  const mvar_t *v = find_float64(s, st->input, err, errlen);
  if (v == NULL) {
    return -1;
  }
  if (s->state != LC_INITIALIZED && s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot set a signal on '%s' in state %s", st->input,
             lifecycle_name(s->state));
    return -1;
  }
  if (v->causality != CAUS_INPUT) {
    snprintf(err, errlen, "'%s' is not an input", st->input);
    return -1;
  }
  int i = input_slot(s, v, err, errlen);
  if (i < 0) {
    return -1;
  }
  signal_t *g = signal_create(st, err, errlen);
  if (g == NULL) {
    return -1;
  }
  if (s->state == LC_INITIALIZED) {
    set_src(s, i, (input_src_t){.kind = SRC_SIGNAL, .sig = g});
    return 0;
  }
  cmd_t c = {.kind = CMD_SET_SIGNAL, .index = i, .sig = g};
  if (roundtrip(s, &c, err, errlen) < 0) {
    return -1; /* ponytail: g leaks if the loop never answers; it may still be playing it */
  }
  signal_free(c.sig); /* the replaced one */
  return 0;
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
    c.index = input_slot(s, v, err, errlen);
    if (c.index < 0) {
      return -1;
    }
    if (s->state == LC_INITIALIZED) {
      set_src(s, c.index, (input_src_t){.kind = SRC_VALUE, .value = value});
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
  if (c.kind == CMD_WRITE_INPUT) {
    signal_free(c.sig); /* the replaced one */
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

/* Channel of the scenario input or output `name` (inputs first, as in a record), or -1. */
static int channel(const session_t *s, const char *name)
{
  for (size_t i = 0; i < s->sc.n_inputs; i++) {
    if (strcmp(s->sc.inputs[i], name) == 0) {
      return (int)i;
    }
  }
  for (size_t i = 0; i < s->sc.n_outputs; i++) {
    if (strcmp(s->sc.outputs[i], name) == 0) {
      return (int)(s->sc.n_inputs + i);
    }
  }
  return -1;
}

static const char *channel_name(const session_t *s, int ch)
{
  return (size_t)ch < s->sc.n_inputs ? s->sc.inputs[ch] : s->sc.outputs[(size_t)ch - s->sc.n_inputs];
}

static int resolve_cond(const session_t *s, const capture_cond_t *in, cond_t *out, char *err,
                        size_t errlen)
{
  *out = (cond_t){.op = in->op, .value = in->value};
  if (in->op == COND_NONE) {
    return 0;
  }
  out->ch = channel(s, in->var);
  if (out->ch < 0) {
    snprintf(err, errlen, "trigger variable '%s' is not one of the scenario's inputs or outputs",
             in->var);
    return -1;
  }
  return 0;
}

int session_capture_start(session_t *s, const capture_spec_t *spec, char *err, size_t errlen)
{
  reap(s);
  if (s->state != LC_INITIALIZED && s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot start a capture in state %s", lifecycle_name(s->state));
    return -1;
  }
  int id = 0;
  while (id < SESSION_MAX_CAPTURES && s->caps[id] != NULL) {
    id++;
  }
  if (id == SESSION_MAX_CAPTURES) {
    snprintf(err, errlen, "at most %d captures per run", SESSION_MAX_CAPTURES);
    return -1;
  }

  capture_def_t d = {.n_in = s->sc.n_inputs, .duration = spec->duration};
  if (spec->n_vars == 0) {
    d.n_ch = s->sc.n_inputs + s->sc.n_outputs;
    for (size_t i = 0; i < d.n_ch; i++) {
      d.ch[i] = (int)i;
    }
  } else if (spec->n_vars > 2 * TELEM_MAX_CH) {
    snprintf(err, errlen, "a capture takes at most %d variables", 2 * TELEM_MAX_CH);
    return -1;
  }
  for (size_t i = 0; i < spec->n_vars; i++) {
    d.ch[i] = channel(s, spec->vars[i]);
    if (d.ch[i] < 0) {
      snprintf(err, errlen,
               "'%s' is not one of the scenario's inputs or outputs (only those are captured)",
               spec->vars[i]);
      return -1;
    }
    d.n_ch++;
  }
  if (resolve_cond(s, &spec->start, &d.start, err, errlen) < 0 ||
      resolve_cond(s, &spec->stop, &d.stop, err, errlen) < 0) {
    return -1;
  }
  if (!(spec->duration >= 0) || !isfinite(spec->duration)) {
    snprintf(err, errlen, "capture duration must be >= 0");
    return -1;
  }
  d.max_rows = spec->max_rows;
  if (d.max_rows == 0) {
    d.max_rows = spec->duration > 0 ? (size_t)ceil(spec->duration / s->sc.step) + 1 : 100000;
  }
  if (d.max_rows > CAPTURE_MAX_BYTES / ((1 + d.n_ch) * sizeof(double))) {
    snprintf(err, errlen, "capture buffer of %zu rows is over %u MB", d.max_rows,
             CAPTURE_MAX_BYTES >> 20);
    return -1;
  }

  capture_t *c = capture_create(&d);
  if (c == NULL) {
    snprintf(err, errlen, "out of memory");
    return -1;
  }
  s->cap_names[id][0] = "t";
  for (size_t i = 0; i < d.n_ch; i++) {
    s->cap_names[id][1 + i] = channel_name(s, d.ch[i]);
  }
  pthread_mutex_lock(&s->cap_lock);
  s->caps[id] = c;
  pthread_mutex_unlock(&s->cap_lock);
  return id;
}

static capture_t *find_capture(session_t *s, int id, char *err, size_t errlen)
{
  reap(s);
  if (id < 0 || id >= SESSION_MAX_CAPTURES || s->caps[id] == NULL) {
    snprintf(err, errlen, "no capture %d", id);
    return NULL;
  }
  return s->caps[id];
}

int session_capture_fetch(session_t *s, int id, capture_data_t *out, char *err, size_t errlen)
{
  capture_t *c = find_capture(s, id, err, errlen);
  if (c == NULL) {
    return -1;
  }
  *out = (capture_data_t){0};
  pthread_mutex_lock(&s->cap_lock);
  const double *rows;
  out->state = capture_state(c);
  out->n_rows = capture_rows(c, &rows);
  out->n_cols = capture_cols(c);
  size_t bytes = out->n_rows * out->n_cols * sizeof(double);
  out->rows = malloc(bytes > 0 ? bytes : 1);
  if (out->rows != NULL) {
    memcpy(out->rows, rows, bytes);
  }
  pthread_mutex_unlock(&s->cap_lock);
  if (out->rows == NULL) {
    snprintf(err, errlen, "out of memory");
    return -1;
  }
  memcpy(out->names, s->cap_names[id], out->n_cols * sizeof out->names[0]);
  return 0;
}

int session_capture_stop(session_t *s, int id, char *err, size_t errlen)
{
  capture_t *c = find_capture(s, id, err, errlen);
  if (c == NULL) {
    return -1;
  }
  pthread_mutex_lock(&s->cap_lock);
  capture_finish(c);
  pthread_mutex_unlock(&s->cap_lock);
  return 0;
}
