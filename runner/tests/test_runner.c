/* End-to-end runs of the runner against the real Reference FMUs (runner/fmus/README.md). Skipped
 * (exit 77) if they are not installed. */
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"
#include "runner.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

#define SKIP 77
#define MAXC 8
#define MAXROWS 6000

static char dir[128];
static char logp[300];

static const char *log_path(const char *name) {
  snprintf(logp, sizeof logp, "%s/%s", dir, name);
  return logp;
}

static int run_text(const char *text, runner_result_t *res, char *err, size_t errlen) {
  scenario_t sc;
  char cerr[256];
  int rc = config_parse(text, &sc, cerr, sizeof cerr);
  if (rc != 0) {
    fprintf(stderr, "config: %s\n", cerr);
  }
  assert(rc == 0);
  return runner_run(&sc, res, err, errlen);
}

/* Feedthrough with a step on its input at t = 0.5 s. */
static void feed_text(char *buf, size_t cap, const char *step, const char *duration,
                      const char *rt, const char *log) {
  snprintf(buf, cap,
           "[model]\nfmu = \"%s/Feedthrough.fmu\"\nstep = %s\nduration = %s\n[rt]\n%s\n"
           "[io]\nbackend = \"null\"\ninputs = [\"Float64_continuous_input\"]\n"
           "outputs = [\"Float64_continuous_output\"]\n"
           "stimulus = [{ input = \"Float64_continuous_input\", kind = \"step\", t = 0.5, "
           "from = 0, to = 2.5 }]\n[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, step, duration, rt, log);
}

static void dahl_text(char *buf, size_t cap, const char *k, const char *log) {
  snprintf(buf, cap,
           "[model]\nfmu = \"%s/Dahlquist.fmu\"\nstep = 0.001\nduration = 2\n"
           "params = { k = %s }\n[rt]\nwait = \"none\"\n"
           "[io]\nbackend = \"null\"\noutputs = [\"x\"]\n[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, k, log);
}

/* Reads a log CSV into numbers. Returns the row count and sets *ncols. */
static double rows[MAXROWS][MAXC];
static size_t read_csv(const char *path, size_t *ncols) {
  FILE *fp = fopen(path, "r");
  assert(fp != NULL);
  char line[512];
  assert(fgets(line, sizeof line, fp) != NULL); /* header */
  size_t c = 1;
  for (const char *p = line; *p != '\0'; p++) {
    c += *p == ',';
  }
  assert(c <= MAXC);
  *ncols = c;
  size_t n = 0;
  while (n < MAXROWS && fgets(line, sizeof line, fp) != NULL) {
    char *p = line;
    for (size_t k = 0; k < c; k++) {
      rows[n][k] = strtod(p, &p);
      if (*p == ',') {
        p++;
      }
    }
    n++;
  }
  fclose(fp);
  return n;
}

static void test_unpaced_feedthrough(void) {
  char text[2048], err[256] = "";
  runner_result_t res;
  feed_text(text, sizeof text, "0.001", "1", "wait = \"none\"", log_path("feed.csv"));
  int rc = run_text(text, &res, err, sizeof err);
  if (rc != RUNNER_OK) {
    fprintf(stderr, "run failed: %s\n", err);
  }
  assert(rc == RUNNER_OK);
  assert(res.state == LC_STOPPED && !res.stopped_early);
  assert(res.steps == 1000 && res.rows == 1000 && res.dropped == 0 && res.overruns == 0);
  assert(res.max_step_ns > 0 && res.mean_step_ns > 0);

  size_t nc;
  size_t n = read_csv(logp, &nc);
  assert(n == 1000 && nc == 7); /* n,t,in,out,wake_ns,step_ns,flags */
  for (size_t i = 0; i < n; i++) {
    assert(rows[i][0] == (double)i);
    assert(rows[i][1] == (double)(i + 1) * 0.001); /* t is the time after the step */
    double expect = (double)i * 0.001 < 0.5 ? 0.0 : 2.5; /* input applied at the start of step i */
    assert(rows[i][2] == expect);
    assert(rows[i][3] == expect); /* the output follows the input */
    assert(rows[i][4] == 0.0);    /* unpaced: no wake-up latency */
    assert(rows[i][5] >= 0.0);    /* a fast step can be below the clock resolution */
    assert(rows[i][6] == 0.0);
  }
  remove(logp);
}

static void test_dahlquist_deterministic_and_parametrized(void) {
  char text[2048], err[256] = "";
  runner_result_t res;

  dahl_text(text, sizeof text, "1.0", log_path("d1.csv"));
  assert(run_text(text, &res, err, sizeof err) == RUNNER_OK);
  size_t nc, n1 = read_csv(logp, &nc);
  assert(n1 == 2000 && nc == 6); /* n,t,x,wake_ns,step_ns,flags */
  static double first[MAXROWS][3];
  for (size_t i = 0; i < n1; i++) {
    memcpy(first[i], rows[i], sizeof first[i]);
  }
  /* The FMU takes 0.1 s Euler steps internally: 2 s is 20 of them. */
  assert(fabs(rows[n1 - 1][2] - pow(0.9, 20)) < 1e-12);
  remove(logp);

  dahl_text(text, sizeof text, "1.0", log_path("d2.csv"));
  assert(run_text(text, &res, err, sizeof err) == RUNNER_OK);
  size_t n2 = read_csv(logp, &nc);
  assert(n2 == n1);
  for (size_t i = 0; i < n2; i++) { /* same run, same numbers, bit for bit */
    assert(rows[i][0] == first[i][0] && rows[i][1] == first[i][1] && rows[i][2] == first[i][2]);
  }
  remove(logp);

  dahl_text(text, sizeof text, "2.0", log_path("d3.csv")); /* the parameter reaches the model */
  assert(run_text(text, &res, err, sizeof err) == RUNNER_OK);
  size_t n3 = read_csv(logp, &nc);
  assert(n3 == 2000 && fabs(rows[n3 - 1][2] - pow(0.8, 20)) < 1e-12);
  remove(logp);
}

static void test_paced(const char *mode, const char *duration, size_t steps, int64_t min_ns) {
  char text[2048], rt[128], err[256] = "";
  runner_result_t res;
  snprintf(rt, sizeof rt, "wait = \"%s\"\nmax_overruns = 1000", mode);
  feed_text(text, sizeof text, "0.001", duration, rt, log_path("paced.csv"));

  int64_t t0 = plat_now_ns();
  int rc = run_text(text, &res, err, sizeof err);
  int64_t elapsed = plat_now_ns() - t0;
  if (rc != RUNNER_OK) {
    fprintf(stderr, "run failed: %s\n", err);
  }
  assert(rc == RUNNER_OK && res.steps == steps && res.rows == steps);
  assert(elapsed >= min_ns && elapsed < 5000000000LL); /* the run really took its wall-clock time */
  fprintf(stderr, "%s: %zu steps in %.1f ms, overruns %llu, max wake %lld ns, max step %lld ns\n",
          mode, steps, (double)elapsed / 1e6, (unsigned long long)res.overruns,
          (long long)res.max_wake_ns, (long long)res.max_step_ns);

  size_t nc, n = read_csv(logp, &nc);
  assert(n == steps);
  uint64_t flagged = 0;
  for (size_t i = 0; i < n; i++) {
    assert(rows[i][4] >= 0.0); /* wake_ns */
    assert(rows[i][5] >= 0.0); /* step_ns */
    flagged += rows[i][6] == 1.0;
  }
  assert(flagged == res.overruns); /* the overrun flag in the log matches the count */
  remove(logp);
}

static void test_overrun_faults(void) {
  /* A 100 ns step cannot be met: the first late step is an overrun and max_overruns is 0. */
  char text[2048], err[256] = "";
  runner_result_t res;
  feed_text(text, sizeof text, "1e-7", "1e-4", "wait = \"sleep\"\nmax_overruns = 0",
            log_path("overrun.csv"));
  int rc = run_text(text, &res, err, sizeof err);
  assert(rc == RUNNER_FAULT);
  assert(res.state == LC_FAULT && res.overruns >= 1 && res.steps >= 1);
  assert(strstr(err, "overruns") != NULL);
  fprintf(stderr, "overrun fault: %s\n", err);
  remove(logp);
}

static void *stopper(void *arg) {
  (void)arg;
  struct timespec ts = {0, 100000000};
  nanosleep(&ts, NULL);
  runner_request_stop();
  return NULL;
}

static void test_stop_request(void) {
  char text[2048], err[256] = "";
  runner_result_t res;
  feed_text(text, sizeof text, "0.001", "5", "wait = \"sleep\"\nmax_overruns = 1000000",
            log_path("stop.csv"));
  pthread_t th;
  assert(pthread_create(&th, NULL, stopper, NULL) == 0);
  int rc = run_text(text, &res, err, sizeof err);
  pthread_join(th, NULL);
  assert(rc == RUNNER_OK);
  assert(res.state == LC_STOPPED && res.stopped_early);
  assert(res.steps > 0 && res.steps < 5000);
  assert(res.rows == res.steps); /* everything logged before the stop is on disk */
  remove(logp);
}

static void expect_setup_error(const char *text, const char *substr) {
  runner_result_t res;
  char err[256] = "";
  int rc = run_text(text, &res, err, sizeof err);
  if (rc != RUNNER_SETUP_ERROR || strstr(err, substr) == NULL) {
    fprintf(stderr, "expected setup error containing '%s', got rc=%d err='%s'\n", substr, rc, err);
  }
  assert(rc == RUNNER_SETUP_ERROR);
  assert(strstr(err, substr) != NULL);
  assert(res.state == LC_FAULT && res.steps == 0);
}

static void test_setup_errors(void) {
  char text[2048];

  snprintf(text, sizeof text,
           "[model]\nfmu = \"/nonexistent/x.fmu\"\nstep = 0.001\nduration = 1\n"
           "[io]\nbackend = \"null\"\n[log]\nfile = \"%s\"\n",
           log_path("none.csv"));
  expect_setup_error(text, "cannot open");

  snprintf(text, sizeof text,
           "[model]\nfmu = \"%s/Feedthrough.fmu\"\nstep = 0.001\nduration = 1\n"
           "[io]\nbackend = \"null\"\noutputs = [\"nope\"]\n[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, log_path("none.csv"));
  expect_setup_error(text, "output 'nope'");

  snprintf(text, sizeof text,
           "[model]\nfmu = \"%s/Dahlquist.fmu\"\nstep = 0.001\nduration = 1\n"
           "params = { zzz = 1 }\n[io]\nbackend = \"null\"\n[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, log_path("none.csv"));
  expect_setup_error(text, "parameter 'zzz'");

  snprintf(text, sizeof text,
           "[model]\nfmu = \"%s/Dahlquist.fmu\"\nstep = 0.001\nduration = 1\n"
           "[io]\nbackend = \"null\"\n[log]\nfile = \"/nonexistent-dir/deep/x.csv\"\n",
           KRUMPIR_FMU_DIR);
  expect_setup_error(text, "cannot create log file");
}

int main(void) {
  char probe[512];
  snprintf(probe, sizeof probe, "%s/Feedthrough.fmu", KRUMPIR_FMU_DIR);
  if (access(probe, R_OK) != 0) {
    fprintf(stderr, "SKIP: %s not found, see runner/fmus/README.md\n", probe);
    return SKIP;
  }
  snprintf(dir, sizeof dir, "/tmp/krumpir_test_runner_XXXXXX");
  assert(mkdtemp(dir) != NULL);

  test_unpaced_feedthrough();
  test_dahlquist_deterministic_and_parametrized();
  test_paced("sleep", "0.2", 200, 195000000);
  test_paced("busy", "0.1", 100, 95000000);
  test_overrun_faults();
  test_stop_request();
  test_setup_errors();

  assert(rmdir(dir) == 0); /* every test removed its log file */
  puts("test_runner: ok");
  return 0;
}
