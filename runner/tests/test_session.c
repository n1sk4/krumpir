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
           "[rt]\nwait = \"%s\"\n[io]\nbackend = \"null\"\ninputs = [\"" IN "\"]\n"
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
  session_unload(s, &sum);
  assert(sum.steps == 100 && !sum.stopped_early && sum.rows == 100);

  assert(session_start(s, err, sizeof err) == -1); /* nothing loaded */
  session_destroy(s);
  fprintf(stderr, "drive running: ok\n");
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
  test_failed_load();
  remove(logp);
  return 0;
}
