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
#include "timing_pin.h"
#include "varmap.h"
#include "writer.h"

#define RING_CAPACITY 16384 /* records; about 16 s of buffer at 1 kHz */

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
  bool mem_locked;  /* rt_lock_memory() was called for this load */
  int timing_fd;    /* rt.timing_gpio's line (timing_pin), 0 when none */
  atomic_bool done; /* the loop thread has returned */
  atomic_int stop;
  loop_counters_t ls;

  cmdq_t cmd, reply;
  uint32_t seq;
  input_src_t src[TELEM_MAX_CH]; /* the loop owns it while RUNNING; the session owns the signals */
  /* The arrays of the command in flight, which the loop uses until it answers. `lent`: the seq of
   * one it did not answer in time, so it may still use them. */
  uint32_t *cmd_vr;
  double *cmd_value;
  size_t cmd_cap;
  input_src_t cmd_src[TELEM_MAX_CH];
  uint32_t lent;

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

const modeldesc_t *session_model(const session_t *s) { return s->pkg_open ? &s->pkg.md : NULL; }

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

/* Frees the signals left in the command's input sources (the ones a write replaced, or new ones the
 * loop did not take) and clears them. */
static void release_srcs(session_t *s)
{
  for (size_t i = 0; i < TELEM_MAX_CH; i++) {
    if (s->cmd_src[i].kind == SRC_SIGNAL) {
      signal_free(s->cmd_src[i].sig);
    }
  }
  memset(s->cmd_src, 0, sizeof s->cmd_src);
}

static void describe_failure(session_t *s)
{
  const loop_counters_t *ls = &s->ls;
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
  s->lent = 0; /* the loop is gone: the command arrays are free again */
  release_srcs(s);
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
    return -ERR_BAD_STATE;
  }
  scenario_t *sc = malloc(sizeof *sc); /* large: off the stack */
  if (sc == NULL) {
    snprintf(err, errlen, "out of memory");
    return -ERR_INTERNAL;
  }
  *sc = s->sc;
  const writer_cont_t cont = s->log_cont; /* unload clears it */
  session_summary_t sum;
  session_unload(s, &sum);
  if (out != NULL) {
    *out = sum;
  }
  /* The new run's rows go after the old ones, n and t counting on from where it ended */
  s->log_cont.n = cont.n + sum.steps;
  s->log_cont.t = cont.t + (double)sum.steps * sc->step;
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
  timing_pin_close(s->timing_fd);
  for (size_t i = 0; i < TELEM_MAX_CH; i++) {
    if (s->src[i].kind == SRC_SIGNAL) {
      signal_free(s->src[i].sig);
    }
  }
  release_srcs(s);
  free(s->cmd_vr);
  free(s->cmd_value);
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
    return -ERR_BAD_STATE;
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
  int rc = -ERR_NOT_FOUND;
  if (fmu_open(sc->fmu, &s->pkg, err, errlen) < 0) {
    goto fail;
  }
  s->pkg_open = true;
  rc = -ERR_BAD_SCENARIO;
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
  rc = -ERR_FAULT;
  s->io = io_open(&s->sc, err, errlen);
  if (s->io == NULL) {
    goto fail;
  }
  rc = -ERR_INTERNAL;
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
  s->writer = writer_start(&s->ring, s->sc.log_file, s->append_log ? &s->log_cont : NULL, in_names,
                           sc->n_inputs, out_names, sc->n_outputs, feed_captures, s);
  if (s->writer == NULL) {
    snprintf(err, errlen, "cannot create log file '%s'", sc->log_file);
    goto fail;
  }
  lifecycle_set(&s->state, LC_LOADED);

  /* INITIALIZED: instantiate, set parameters, initialize the model. */
  rc = -ERR_FAULT;
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
      rc = -ERR_INTERNAL;
      goto fail;
    }
    s->mem_locked = true;
  }
  if (s->sc.timing_gpio >= 0) {
    const int fd = timing_pin_open(s->sc.timing_gpio, err, errlen);
    if (fd < 0) {
      goto fail;
    }
    s->timing_fd = fd;
  }
  lifecycle_set(&s->state, LC_INITIALIZED);
  return 0;

fail:
  session_unload(s, NULL);
  return rc;
}

int scenario_check(const scenario_t *sc, char *err, size_t errlen)
{
  varmap_t vm;
  fmu_pkg_t pkg = {0};
  err[0] = '\0';
  int rc = fmu_open(sc->fmu, &pkg, err, errlen) < 0 ? -ERR_NOT_FOUND : 0;
  if (rc == 0 && varmap_build(&pkg.md, sc, &vm, err, errlen) < 0) {
    rc = -ERR_BAD_SCENARIO;
  }
  for (size_t j = 0; rc == 0 && j < sc->n_stimuli; j++) {
    signal_t *g = signal_create(&sc->stimuli[j], err, errlen);
    rc = g != NULL ? 0 : -ERR_BAD_SCENARIO;
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
                    .src = s->src,
                    .timing_fd = s->timing_fd};
  rt_prefault_stack();
  loop_run(&ctx, &s->ls);
  atomic_store(&s->done, true);
  return NULL;
}

int session_start(session_t *s, char *err, size_t errlen)
{
  if (s->state != LC_INITIALIZED) {
    snprintf(err, errlen, "cannot start in state %s", lifecycle_name(s->state));
    return -ERR_BAD_STATE;
  }
  /* The loop thread gets rt.cpu and rt.priority from its creation on. If the OS refuses them,
   * nothing has run and the session stays INITIALIZED. */
  pthread_attr_t attr;
  if (rt_thread_attr(&attr, s->sc.cpu, s->sc.priority, err, errlen) < 0) {
    return -ERR_BAD_SCENARIO;
  }
  atomic_store(&s->done, false);
  memset(&s->ls, 0, sizeof s->ls); /* before the thread exists: session_live reads it */
  int e = pthread_create(&s->thread, &attr, loop_thread, s);
  pthread_attr_destroy(&attr);
  if (e != 0) {
    rt_create_error(e, s->sc.cpu, s->sc.priority, err, errlen);
    return -ERR_INTERNAL;
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
  for (double waited = 0.0;; waited += 0.5) { /* looks before it times out: 0 is a poll */
    if (!s->thread_started || atomic_load(&s->done)) {
      reap(s);
      return true;
    }
    if (timeout_ms >= 0 && waited >= timeout_ms) {
      return false;
    }
    sleep_ms(0.5);
  }
}

void session_stop(session_t *s)
{
  if (s->thread_started) {
    s->stop = 1;
    session_wait(s, -1);
  }
}

/* Room for n value references and values in the command arrays. Refused while a command the loop
 * did not answer in time may still use them. */
static int reserve(session_t *s, size_t n, char *err, size_t errlen)
{
  cmd_t r;
  while (s->lent != 0 && cmdq_pop(&s->reply, &r)) {
    if (r.seq == s->lent) {
      s->lent = 0;
      release_srcs(s);
    }
  }
  if (s->lent != 0) {
    snprintf(err, errlen, "no reply from the loop within %d ms", SESSION_REPLY_TIMEOUT_MS);
    return -ERR_TIMEOUT;
  }
  if (n > s->cmd_cap) {
    uint32_t *vr = realloc(s->cmd_vr, n * sizeof *vr);
    s->cmd_vr = vr != NULL ? vr : s->cmd_vr;
    double *value = realloc(s->cmd_value, n * sizeof *value);
    s->cmd_value = value != NULL ? value : s->cmd_value;
    if (vr == NULL || value == NULL) {
      snprintf(err, errlen, "out of memory");
      return -ERR_INTERNAL;
    }
    s->cmd_cap = n;
  }
  return 0;
}

/* Sends a command to the running loop and waits for its reply. */
static int roundtrip(session_t *s, cmd_t *c, char *err, size_t errlen)
{
  c->seq = ++s->seq;
  if (!cmdq_push(&s->cmd, c)) {
    snprintf(err, errlen, "command queue full");
    return -ERR_INTERNAL;
  }
  for (double waited = 0.0; waited < SESSION_REPLY_TIMEOUT_MS; waited += 0.1) {
    cmd_t r;
    /* read before the pop, so a reply sent just before the end is not missed */
    const bool ended = atomic_load(&s->done);
    while (cmdq_pop(&s->reply, &r)) {
      if (r.seq == c->seq) {
        *c = r;
        return 0;
      }
    }
    if (ended) {
      snprintf(err, errlen, "the run ended before the command was handled");
      return -ERR_BAD_STATE;
    }
    sleep_ms(0.1);
  }
  s->lent = c->seq;
  snprintf(err, errlen, "no reply from the loop within %d ms", SESSION_REPLY_TIMEOUT_MS);
  return -ERR_TIMEOUT;
}

/* The variable `name`, which must be Float64. */
static int find_float64(session_t *s, const char *name, const mvar_t **out, char *err,
                        size_t errlen)
{
  if (!s->pkg_open) {
    snprintf(err, errlen, "no scenario loaded");
    return -ERR_BAD_STATE;
  }
  const mvar_t *v = modeldesc_find(&s->pkg.md, name);
  if (v == NULL) {
    snprintf(err, errlen, "'%s' is not a variable of the model", name);
    return -ERR_NOT_FOUND;
  }
  if (!mvar_is_float64(v)) {
    snprintf(err, errlen, "'%s' is %s, only Float64 is supported", name, v->type);
    return -ERR_BAD_REQUEST;
  }
  *out = v;
  return 0;
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

/* Applies a staged write: the tunable parameters cmd_vr/cmd_value[0..np) (pname names them in a
 * message), then, if the FMU takes them, cmd_src[i] for each input slot i in `inputs`. */
static int apply(session_t *s, size_t np, uint32_t inputs, const char *pname, char *err,
                 size_t errlen)
{
  if (s->state == LC_INITIALIZED) {
    const fmu_result_t r = np > 0 ? fmu_set_real(s->fmu, s->cmd_vr, np, s->cmd_value) : FMU_OK;
    if (r != FMU_OK) {
      snprintf(err, errlen, "'%s': %s (%s)", pname, fmu_result_str(r), fmu_last_message(s->fmu));
      release_srcs(s);
      return -ERR_FAULT;
    }
    for (int i = 0; i < TELEM_MAX_CH; i++) {
      if (inputs & (1u << i)) {
        set_src(s, i, s->cmd_src[i]);
        s->cmd_src[i] = (input_src_t){0};
      }
    }
    return 0;
  }
  cmd_t c = {.kind = CMD_WRITE,
             .n = (uint32_t)np,
             .inputs = inputs,
             .vrs = s->cmd_vr,
             .values = s->cmd_value,
             .srcs = s->cmd_src};
  int rc = roundtrip(s, &c, err, errlen);
  if (rc == -ERR_TIMEOUT) {
    return rc; /* the loop may still take them: cmd_src stays as it is */
  }
  if (rc == 0 && c.status != FMU_OK) {
    snprintf(err, errlen, "'%s': %s", pname, fmu_result_str((fmu_result_t)c.status));
    rc = -ERR_FAULT;
  }
  release_srcs(s); /* the sources replaced, or the new ones the loop did not take */
  return rc;
}

int session_set_signal(session_t *s, const stimulus_t *st, char *err, size_t errlen)
{
  reap(s);
  const mvar_t *v;
  int rc = find_float64(s, st->input, &v, err, errlen);
  if (rc < 0) {
    return rc;
  }
  if (s->state != LC_INITIALIZED && s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot set a signal on '%s' in state %s", st->input,
             lifecycle_name(s->state));
    return -ERR_BAD_STATE;
  }
  if (v->causality != CAUS_INPUT) {
    snprintf(err, errlen, "'%s' is not an input", st->input);
    return -ERR_NOT_WRITABLE;
  }
  int i = input_slot(s, v, err, errlen);
  if (i < 0) {
    return -ERR_NOT_WRITABLE;
  }
  if ((rc = reserve(s, 0, err, errlen)) < 0) {
    return rc;
  }
  signal_t *g = signal_create(st, err, errlen);
  if (g == NULL) {
    return -ERR_BAD_REQUEST;
  }
  s->cmd_src[i] = (input_src_t){.kind = SRC_SIGNAL, .sig = g};
  return apply(s, 0, 1u << i, NULL, err, errlen);
}

/* Checks one value of a write and stages it: an input into cmd_src (bit in *inputs), a tunable
 * parameter into cmd_vr/cmd_value[*np]. */
static int stage(session_t *s, const char *name, double value, size_t *np, uint32_t *inputs,
                 char *err, size_t errlen)
{
  const mvar_t *v;
  int rc = find_float64(s, name, &v, err, errlen);
  if (rc < 0) {
    return rc;
  }
  if (s->state != LC_INITIALIZED && s->state != LC_RUNNING) {
    snprintf(err, errlen, "cannot write '%s' in state %s", name, lifecycle_name(s->state));
    return -ERR_BAD_STATE;
  }
  if (v->causality == CAUS_INPUT) {
    const int i = input_slot(s, v, err, errlen);
    if (i < 0) {
      return -ERR_NOT_WRITABLE;
    }
    s->cmd_src[i] = (input_src_t){.kind = SRC_VALUE, .value = value};
    *inputs |= 1u << i;
  } else if (v->causality == CAUS_PARAMETER && v->tunable) {
    s->cmd_vr[*np] = v->vr;
    s->cmd_value[(*np)++] = value;
  } else if (v->causality == CAUS_PARAMETER) {
    snprintf(err, errlen, "'%s' is a fixed parameter: set it in the scenario (model.params)", name);
    return -ERR_NOT_WRITABLE;
  } else {
    snprintf(err, errlen,
             "'%s' has causality '%s': only inputs and tunable parameters can be written", name,
             v->causality_str);
    return -ERR_NOT_WRITABLE;
  }
  return 0;
}

int session_write(session_t *s, const char *const *names, const double *values, size_t n, char *err,
                  size_t errlen)
{
  reap(s);
  if (n == 0) {
    return 0;
  }
  int rc = reserve(s, n, err, errlen);
  size_t np = 0;
  uint32_t inputs = 0;
  const char *pname = NULL;
  for (size_t i = 0; rc == 0 && i < n; i++) {
    rc = stage(s, names[i], values[i], &np, &inputs, err, errlen);
    pname = pname == NULL && np > 0 ? names[i] : pname;
  }
  if (rc < 0) {
    memset(s->cmd_src, 0, sizeof s->cmd_src); /* values only: nothing to free */
    return rc;
  }
  return apply(s, np, inputs, pname, err, errlen);
}

int session_read(session_t *s, const char *const *names, size_t n, double *values, double *t,
                 char *err, size_t errlen)
{
  reap(s);
  *t = NAN;
  int rc = reserve(s, n, err, errlen);
  for (size_t i = 0; rc == 0 && i < n; i++) {
    const mvar_t *v;
    rc = find_float64(s, names[i], &v, err, errlen);
    s->cmd_vr[i] = rc == 0 ? v->vr : 0;
  }
  if (rc < 0) {
    return rc;
  }
  const lifecycle_t st = s->state;
  if (st != LC_INITIALIZED && st != LC_RUNNING && st != LC_STOPPED) {
    if (n == 0) {
      return 0;
    }
    snprintf(err, errlen, "cannot read '%s' in state %s", names[0], lifecycle_name(st));
    return -ERR_BAD_STATE;
  }
  fmu_result_t r = FMU_OK;
  if (st == LC_RUNNING) {
    cmd_t c = {.kind = CMD_READ, .n = (uint32_t)n, .vrs = s->cmd_vr, .values = s->cmd_value};
    if ((rc = roundtrip(s, &c, err, errlen)) < 0) {
      return rc;
    }
    r = (fmu_result_t)c.status;
    for (size_t i = 0; i < n; i++) {
      values[i] = s->cmd_value[i];
    }
    *t = c.t;
  } else {
    r = n > 0 ? fmu_get_real(s->fmu, s->cmd_vr, n, values) : FMU_OK;
    *t = (double)atomic_load(&s->ls.steps) * s->sc.step;
  }
  if (r != FMU_OK) {
    snprintf(err, errlen, "'%s': %s", names[0], fmu_result_str(r));
    return -ERR_FAULT;
  }
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
  return (size_t)ch < s->sc.n_inputs ? s->sc.inputs[ch]
                                     : s->sc.outputs[(size_t)ch - s->sc.n_inputs];
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
    return -ERR_BAD_STATE;
  }
  int id = 0;
  while (id < SESSION_MAX_CAPTURES && s->caps[id] != NULL) {
    id++;
  }
  if (id == SESSION_MAX_CAPTURES) {
    snprintf(err, errlen, "at most %d captures per run", SESSION_MAX_CAPTURES);
    return -ERR_LIMIT;
  }

  capture_def_t d = {.n_in = s->sc.n_inputs, .duration = spec->duration};
  if (spec->n_vars == 0) {
    d.n_ch = s->sc.n_inputs + s->sc.n_outputs;
    for (size_t i = 0; i < d.n_ch; i++) {
      d.ch[i] = (int)i;
    }
  } else if (spec->n_vars > 2 * TELEM_MAX_CH) {
    snprintf(err, errlen, "a capture takes at most %d variables", 2 * TELEM_MAX_CH);
    return -ERR_LIMIT;
  }
  for (size_t i = 0; i < spec->n_vars; i++) {
    d.ch[i] = channel(s, spec->vars[i]);
    if (d.ch[i] < 0) {
      snprintf(err, errlen,
               "'%s' is not one of the scenario's inputs or outputs (only those are captured)",
               spec->vars[i]);
      return -ERR_NOT_FOUND;
    }
    d.n_ch++;
  }
  if (resolve_cond(s, &spec->start, &d.start, err, errlen) < 0 ||
      resolve_cond(s, &spec->stop, &d.stop, err, errlen) < 0) {
    return -ERR_NOT_FOUND;
  }
  if (!(spec->duration >= 0) || !isfinite(spec->duration)) {
    snprintf(err, errlen, "capture duration must be >= 0");
    return -ERR_BAD_REQUEST;
  }
  d.max_rows = spec->max_rows;
  if (d.max_rows == 0) {
    d.max_rows = spec->duration > 0 ? (size_t)ceil(spec->duration / s->sc.step) + 1 : 100000;
  }
  if (d.max_rows > CAPTURE_MAX_BYTES / ((1 + d.n_ch) * sizeof(double))) {
    snprintf(err, errlen, "capture buffer of %zu rows is over %u MB", d.max_rows,
             CAPTURE_MAX_BYTES >> 20);
    return -ERR_LIMIT;
  }

  capture_t *c = capture_create(&d);
  if (c == NULL) {
    snprintf(err, errlen, "out of memory");
    return -ERR_INTERNAL;
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

int session_capture_fetch(session_t *s, int id, size_t from, size_t max, capture_data_t *out,
                          char *err, size_t errlen)
{
  capture_t *c = find_capture(s, id, err, errlen);
  if (c == NULL) {
    return -ERR_NOT_FOUND;
  }
  *out = (capture_data_t){0};
  pthread_mutex_lock(&s->cap_lock); /* the writer thread adds rows meanwhile */
  const double *rows;
  out->state = capture_state(c);
  out->n_rows = capture_rows(c, &rows);
  out->n_cols = capture_cols(c);
  out->from = from < out->n_rows ? from : out->n_rows;
  out->n = out->n_rows - out->from < max ? out->n_rows - out->from : max;
  const size_t bytes = out->n * out->n_cols * sizeof(double);
  out->rows = malloc(bytes > 0 ? bytes : 1);
  if (out->rows != NULL) {
    memcpy(out->rows, rows + out->from * out->n_cols, bytes);
  }
  pthread_mutex_unlock(&s->cap_lock);
  if (out->rows == NULL) {
    snprintf(err, errlen, "out of memory");
    return -ERR_INTERNAL;
  }
  memcpy(out->names, s->cap_names[id], out->n_cols * sizeof out->names[0]);
  return 0;
}

int session_capture_stop(session_t *s, int id, char *err, size_t errlen)
{
  capture_t *c = find_capture(s, id, err, errlen);
  if (c == NULL) {
    return -ERR_NOT_FOUND;
  }
  pthread_mutex_lock(&s->cap_lock);
  capture_finish(c);
  pthread_mutex_unlock(&s->cap_lock);
  return 0;
}

int session_capture_free(session_t *s, int id, char *err, size_t errlen)
{
  capture_t *c = find_capture(s, id, err, errlen);
  if (c == NULL) {
    return -ERR_NOT_FOUND;
  }
  pthread_mutex_lock(&s->cap_lock);
  s->caps[id] = NULL;
  pthread_mutex_unlock(&s->cap_lock);
  capture_free(c);
  return 0;
}
