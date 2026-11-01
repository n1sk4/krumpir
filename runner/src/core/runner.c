#include "runner.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "session.h"

/* The stop flag of the run in progress, for runner_request_stop (signal handlers, other threads).
 * A request that arrives before the run starts is cleared by the run, as before. */
static _Atomic(atomic_int *) g_stop;

void runner_request_stop(void)
{
  atomic_int *stop = g_stop;
  if (stop != NULL) {
    *stop = 1;
  }
}

int runner_run(const scenario_t *sc, runner_result_t *res, char *err, size_t errlen)
{
  memset(res, 0, sizeof *res);
  err[0] = '\0';

  session_t *s = session_create();
  if (s == NULL) {
    snprintf(err, errlen, "out of memory");
    res->state = LC_FAULT;
    return RUNNER_SETUP_ERROR;
  }

  int rc = RUNNER_SETUP_ERROR;
  if (session_load(s, sc, err, errlen) == 0) {
    g_stop = session_stop_flag(s);
    if (session_start(s, err, errlen) == 0) {
      session_wait(s, -1);
      if (session_state(s) == LC_STOPPED) {
        rc = RUNNER_OK;
      } else {
        snprintf(err, errlen, "%s", session_fault(s));
        rc = RUNNER_FAULT;
      }
    }
  }

  session_summary_t sum;
  session_unload(s, &sum);
  g_stop = NULL;
  session_destroy(s);

  res->state = rc == RUNNER_OK ? LC_STOPPED : LC_FAULT;
  res->stopped_early = sum.stopped_early;
  res->steps = sum.steps;
  res->overruns = sum.overruns;
  res->dropped = sum.dropped;
  res->rows = sum.rows;
  res->max_wake_ns = sum.max_wake_ns;
  res->max_step_ns = sum.max_step_ns;
  res->mean_step_ns = sum.mean_step_ns;
  return rc;
}
