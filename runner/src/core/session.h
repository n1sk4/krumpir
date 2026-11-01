#ifndef KRUMPIR_SESSION_H
#define KRUMPIR_SESSION_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "lifecycle.h"
#include "modeldesc.h"

/* One simulation run with explicit phases, for callers that drive a run while it goes (the
 * service) as well as for the batch runner:
 *
 *   session_load    IDLE -> INITIALIZED   open the FMU, map the scenario, open the I/O backend and
 *                                         the log, instantiate, set the parameters, initialize
 *   session_start   INITIALIZED -> RUNNING  the step loop runs on its own thread
 *   session_stop    RUNNING -> STOPPED    after the current step
 *   (end of run)    RUNNING -> STOPPED    when the duration is reached; FAULT on a failure
 *   session_unload  any -> IDLE           frees everything; the session can load again
 *
 * While the loop runs, session_write and session_read go through a lock-free command queue that
 * the loop handles at the start of a step, so a change always takes effect at a step boundary.
 * All session functions are for one control thread; only the stop flag may be set from a signal
 * handler or another thread. */
typedef struct session session_t;

typedef struct {
  lifecycle_t state;
  bool stopped_early;
  uint64_t steps;
  uint64_t overruns;
  uint64_t dropped; /* telemetry records lost because the log writer fell behind */
  uint64_t rows;    /* rows written to the CSV (known after session_unload) */
  int64_t max_wake_ns;
  int64_t max_step_ns;
  double mean_step_ns;
} session_summary_t;

session_t *session_create(void);
void session_destroy(session_t *s); /* stops and unloads first */

/* Returns 0, or -1 with a one-line message in err (the session is then IDLE again). The scenario
 * is copied. */
int session_load(session_t *s, const scenario_t *sc, char *err, size_t errlen);
int session_start(session_t *s, char *err, size_t errlen);
void session_stop(session_t *s);
/* Waits for the loop to end: by its duration, a stop or a fault. timeout_ms < 0 waits forever.
 * Returns true when it has ended. */
bool session_wait(session_t *s, int timeout_ms);
/* Frees the run and returns to IDLE. `out` (may be NULL) gets the run's summary. */
void session_unload(session_t *s, session_summary_t *out);

lifecycle_t session_state(session_t *s);
/* Why the run went to FAULT (empty otherwise). */
const char *session_fault(const session_t *s);
/* The loaded model's variables; NULL when IDLE. */
const modeldesc_t *session_model(const session_t *s);
/* Setting it to 1 ends a running loop after the current step (async-signal-safe). */
volatile sig_atomic_t *session_stop_flag(session_t *s);

/* Writes a Float64 variable: an input of the scenario (overrides its stimulus from then on) or a
 * tunable parameter. Fixed parameters, outputs and inputs the scenario does not map are refused.
 * Allowed when INITIALIZED (takes effect at the first step) or RUNNING (at the next step boundary,
 * and the call returns once the loop has applied it). Returns 0, or -1 with a message in err. */
int session_write(session_t *s, const char *name, double value, char *err, size_t errlen);

/* Reads any Float64 variable of the model: when INITIALIZED or STOPPED directly, when RUNNING at
 * the next step boundary. Returns 0, or -1 with a message in err. */
int session_read(session_t *s, const char *name, double *value, char *err, size_t errlen);

#endif
