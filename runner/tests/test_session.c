/* The session API against the Reference FMU Feedthrough (input -> output, a fixed and a tunable
 * parameter): explicit phases, writes and reads before, during and after a run, refusals, and a
 * second load in the same session. Skipped (exit 77) if the FMU is not installed. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "session.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

#define SKIP 77
#define IN "Float64_continuous_input"
#define OUT "Float64_continuous_output"

static char logp[300];

static void scenario(scenario_t *sc, const char *wait, const char *duration)
{
  char text[2048], err[256];
  snprintf(text, sizeof text,
           "[model]\nfmu = \"%s/Feedthrough.fmu\"\nstep = 0.001\nduration = %s\n"
           /* overruns are tolerated: CI and parallel ctest load the machine */
           "[rt]\nwait = \"%s\"\nmax_overruns = 1000000000\n[io]\nbackend = \"null\"\n"
           "inputs = [\"" IN "\"]\n"
           "outputs = [\"" OUT "\"]\n"
           "stimulus = [{ input = \"" IN "\", kind = \"constant\", value = 1.0 }]\n"
           "[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, duration, wait, logp);
  int rc = config_parse(text, sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "config: %s\n", err);
  }
  assert(rc == 0);
}

static void sleep_ms(int ms)
{
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static double read_ok(session_t *s, const char *name)
{
  char err[256];
  double v = NAN;
  int rc = session_read(s, name, &v, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "read %s: %s\n", name, err);
  }
  assert(rc == 0);
  return v;
}

static void write_ok(session_t *s, const char *name, double v)
{
  char err[256];
  int rc = session_write(s, name, v, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "write %s: %s\n", name, err);
  }
  assert(rc == 0);
}

static void write_refused(session_t *s, const char *name, double v, const char *why)
{
  char err[256] = "";
  assert(session_write(s, name, v, err, sizeof err) == -1);
  if (strstr(err, why) == NULL) {
    fprintf(stderr, "write %s: expected '%s', got '%s'\n", name, why, err);
  }
  assert(strstr(err, why) != NULL);
}

/* A paced run driven while it goes: overrides, tunable parameter, reads, stop. */
static void test_drive_running(void)
{
  session_t *s = session_create();
  scenario_t sc;
  char err[256];
  scenario(&sc, "sleep", "10");
  assert(session_load(s, &sc, err, sizeof err) == 0);
  assert(session_state(s) == LC_INITIALIZED);

  /* Before the start: input override, refusals, reads */
  write_ok(s, IN, 2.0);
  write_refused(s, "Float64_fixed_parameter", 1.0, "fixed parameter");
  write_refused(s, OUT, 1.0, "causality 'output'");
  write_refused(s, "Float64_discrete_input", 1.0, "not one of the scenario's inputs");
  write_refused(s, "nope", 1.0, "not a variable");
  double v;
  assert(session_read(s, "nope", &v, err, sizeof err) == -1);

  assert(session_start(s, err, sizeof err) == 0);
  assert(session_state(s) == LC_RUNNING);
  sleep_ms(50);
  assert(read_ok(s, OUT) == 2.0); /* the override replaced the stimulus from the first step */

  write_ok(s, IN, 3.0); /* returns once the loop has applied it */
  assert(read_ok(s, OUT) == 3.0);

  write_ok(s, "Float64_tunable_parameter", 7.5);
  assert(read_ok(s, "Float64_tunable_parameter") == 7.5);
  write_refused(s, "Float64_fixed_parameter", 1.0, "fixed parameter");

  session_stop(s);
  assert(session_state(s) == LC_STOPPED);
  assert(read_ok(s, OUT) == 3.0); /* readable after the stop */
  write_refused(s, IN, 4.0, "state STOPPED");

  session_summary_t sum;
  session_unload(s, &sum);
  assert(sum.state == LC_STOPPED && sum.stopped_early && sum.steps > 20);
  assert(sum.rows == sum.steps && sum.dropped == 0);
  assert(session_state(s) == LC_IDLE && session_model(s) == NULL);

  /* The same session loads again; an unpaced run ends by its duration */
  scenario(&sc, "none", "0.1");
  assert(session_load(s, &sc, err, sizeof err) == 0);
  assert(session_start(s, err, sizeof err) == 0);
  assert(session_wait(s, 5000));
  assert(session_state(s) == LC_STOPPED);
  assert(read_ok(s, OUT) == 1.0); /* no override in this run: the stimulus */

  /* Reset: STOPPED -> INITIALIZED, the same scenario again from t = 0 */
  assert(session_reset(s, &sum, err, sizeof err) == 0);
  assert(sum.steps == 100 && sum.state == LC_STOPPED);
  assert(session_state(s) == LC_INITIALIZED && session_model(s) != NULL);
  assert(session_reset(s, NULL, err, sizeof err) == -1 && strstr(err, "INITIALIZED"));
  assert(session_state(s) == LC_INITIALIZED); /* a refused reset changes nothing */
  assert(session_start(s, err, sizeof err) == 0);
  assert(session_wait(s, 5000));
  session_unload(s, &sum);
  assert(sum.steps == 100 && !sum.stopped_early && sum.rows == 100);
  /* the log continued: one header, both runs' rows, n and t rising through the restart */
  FILE *fp = fopen(logp, "r");
  char line[512];
  int lines = 0, headers = 0;
  unsigned long long n, prev_n = 0;
  double t, prev_t = 0;
  while (fp != NULL && fgets(line, sizeof line, fp)) {
    lines++;
    if (strncmp(line, "n,t,", 4) == 0) {
      headers++;
      continue;
    }
    assert(sscanf(line, "%llu,%lf", &n, &t) == 2);
    assert(lines == 2 || (n == prev_n + 1 && fabs(t - prev_t - 0.001) < 1e-9));
    prev_n = n;
    prev_t = t;
  }
  assert(fp != NULL && lines == 201 && headers == 1);
  assert(prev_n == 199 && fabs(prev_t - 0.2) < 1e-9);
  fclose(fp);

  assert(session_reset(s, NULL, err, sizeof err) == -1); /* nothing loaded */
  assert(session_start(s, err, sizeof err) == -1);
  session_destroy(s);
  fprintf(stderr, "drive running: ok\n");
}

/* Signals replaced before and during a run; a write replaces a signal. */
static void test_signals(void)
{
  session_t *s = session_create();
  scenario_t sc;
  char err[256];
  scenario(&sc, "sleep", "10");
  assert(session_load(s, &sc, err, sizeof err) == 0);

  stimulus_t st = {.kind = STIM_SEGMENTS, .n_segs = 1, .loops = 1};
  snprintf(st.input, sizeof st.input, "%s", IN);
  st.segs[0] = (segment_t){.kind = SEG_CONST, .value = 5.0, .duration = INFINITY};
  assert(session_set_signal(s, &st, err, sizeof err) == 0); /* replaces the scenario's stimulus */
  assert(session_start(s, err, sizeof err) == 0);
  sleep_ms(50);
  assert(read_ok(s, OUT) == 5.0);

  /* A ramp 0 -> 1000 over 1000 s started now: tiny but non-zero, and rising */
  st.segs[0] = (segment_t){.kind = SEG_RAMP, .from = 0, .has_from = true, .to = 1000,
                           .duration = 1000};
  assert(session_set_signal(s, &st, err, sizeof err) == 0);
  sleep_ms(50);
  double a = read_ok(s, OUT);
  sleep_ms(50);
  double b = read_ok(s, OUT);
  assert(a > 0.0 && a < 1.0 && b > a);

  write_ok(s, IN, 9.0); /* a value replaces the signal (and frees it) */
  assert(read_ok(s, OUT) == 9.0);

  /* A capture started while running, stopped by hand */
  capture_spec_t all = {0};
  int id = session_capture_start(s, &all, err, sizeof err);
  assert(id >= 0);
  sleep_ms(50);
  capture_data_t d;
  assert(session_capture_fetch(s, id, &d, err, sizeof err) == 0);
  assert(d.state == CAP_RUNNING && d.n_rows > 10 && d.rows[d.n_cols * (d.n_rows - 1) + 1] == 9.0);
  free(d.rows);
  assert(session_capture_stop(s, id, err, sizeof err) == 0);
  assert(session_capture_fetch(s, id, &d, err, sizeof err) == 0 && d.state == CAP_FINISHED);
  free(d.rows);

  stimulus_t bad = st;
  snprintf(bad.input, sizeof bad.input, "%s", OUT);
  assert(session_set_signal(s, &bad, err, sizeof err) == -1 && strstr(err, "not an input"));
  session_unload(s, NULL); /* frees the signal still in place */
  session_destroy(s);
  fprintf(stderr, "signals: ok\n");
}

static int capture_ok(session_t *s, const capture_spec_t *spec)
{
  char err[256];
  int id = session_capture_start(s, spec, err, sizeof err);
  if (id < 0) {
    fprintf(stderr, "capture: %s\n", err);
  }
  assert(id >= 0);
  return id;
}

static capture_data_t fetch_ok(session_t *s, int id)
{
  char err[256];
  capture_data_t d;
  int rc = session_capture_fetch(s, id, &d, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "fetch: %s\n", err);
  }
  assert(rc == 0);
  return d;
}

/* Captures with start and stop triggers over an unpaced run of a ramp 0 -> 1 in 0.1 s. */
static void test_captures(void)
{
  session_t *s = session_create();
  scenario_t sc;
  char err[256];
  scenario(&sc, "none", "0.1");
  assert(session_load(s, &sc, err, sizeof err) == 0);
  stimulus_t st = {.kind = STIM_SEGMENTS, .n_segs = 1, .loops = 1};
  snprintf(st.input, sizeof st.input, "%s", IN);
  st.segs[0] = (segment_t){.kind = SEG_RAMP, .has_from = true, .to = 1, .duration = 0.1};
  assert(session_set_signal(s, &st, err, sizeof err) == 0);

  capture_spec_t a = {.n_vars = 1, .duration = 0.02};
  snprintf(a.vars[0], sizeof a.vars[0], "%s", OUT);
  a.start = (capture_cond_t){.op = COND_GE, .value = 0.245};
  snprintf(a.start.var, sizeof a.start.var, "%s", OUT);
  int ia = capture_ok(s, &a);

  capture_spec_t b = {0}; /* every input and output, until OUT > 0.9 */
  b.stop = (capture_cond_t){.op = COND_GT, .value = 0.9};
  snprintf(b.stop.var, sizeof b.stop.var, "%s", OUT);
  int ib = capture_ok(s, &b);

  capture_spec_t never = {0};
  never.start = (capture_cond_t){.op = COND_LT, .value = -1};
  snprintf(never.start.var, sizeof never.start.var, "%s", IN);
  int in = capture_ok(s, &never);

  capture_spec_t bad = {.n_vars = 1};
  snprintf(bad.vars[0], sizeof bad.vars[0], "Float64_tunable_parameter");
  assert(session_capture_start(s, &bad, err, sizeof err) == -1 && strstr(err, "only those"));
  bad = never;
  snprintf(bad.start.var, sizeof bad.start.var, "nope");
  assert(session_capture_start(s, &bad, err, sizeof err) == -1 && strstr(err, "trigger"));

  assert(session_start(s, err, sizeof err) == 0);
  assert(session_wait(s, 5000) && session_state(s) == LC_STOPPED);

  capture_data_t d = fetch_ok(s, ia);
  assert(d.state == CAP_FINISHED && d.n_rows == 20 && d.n_cols == 2);
  assert(strcmp(d.names[0], "t") == 0 && strcmp(d.names[1], OUT) == 0);
  assert(d.rows[1] >= 0.245 && d.rows[1] < 0.26 && d.rows[2 * 19 + 1] > d.rows[1]);
  free(d.rows);

  d = fetch_ok(s, ib);
  assert(d.state == CAP_FINISHED && d.n_cols == 3 && strcmp(d.names[1], IN) == 0);
  double last = d.rows[3 * (d.n_rows - 1) + 2], before = d.rows[3 * (d.n_rows - 2) + 2];
  assert(d.rows[2] == 0.0 && last > 0.9 && before <= 0.9);
  free(d.rows);

  d = fetch_ok(s, in);
  assert(d.state == CAP_FINISHED && d.n_rows == 0); /* finished by the end of the run */
  free(d.rows);
  assert(session_capture_fetch(s, 7, &d, err, sizeof err) == -1);
  assert(session_capture_start(s, &a, err, sizeof err) == -1); /* STOPPED */
  session_destroy(s); /* frees the captures */
  fprintf(stderr, "captures: ok\n");
}

/* A load that fails leaves the session IDLE and reusable. */
static void test_failed_load(void)
{
  session_t *s = session_create();
  scenario_t sc;
  char err[256];
  scenario(&sc, "none", "0.1");
  snprintf(sc.fmu, sizeof sc.fmu, "%s/missing.fmu", KRUMPIR_FMU_DIR);
  assert(session_load(s, &sc, err, sizeof err) == -1);
  assert(session_state(s) == LC_IDLE);
  scenario(&sc, "none", "0.1");
  assert(session_load(s, &sc, err, sizeof err) == 0);
  session_destroy(s); /* unloads a loaded, never started session */
  fprintf(stderr, "failed load: ok\n");
}

int main(void)
{
  char fmu[300];
  snprintf(fmu, sizeof fmu, "%s/Feedthrough.fmu", KRUMPIR_FMU_DIR);
  if (access(fmu, R_OK) != 0) {
    fprintf(stderr, "SKIP: %s not found\n", fmu);
    return SKIP;
  }
  snprintf(logp, sizeof logp, "/tmp/krumpir_test_session_%d.csv", (int)getpid());
  test_drive_running();
  test_signals();
  test_captures();
  test_failed_load();
  remove(logp);
  return 0;
}
