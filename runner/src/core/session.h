#ifndef KRUMPIR_SESSION_H
#define KRUMPIR_SESSION_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "capture.h"
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
  uint64_t late;    /* steps the io device did not answer in time (uart) */
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
/* STOPPED or FAULT -> INITIALIZED: unloads the run (`out`, may be NULL, gets its summary) and
 * loads the same scenario again, so the next start runs from t = 0 with a fresh FMU and signals.
 * The log continues: the new run's rows are appended, n and t counting on from where the ended
 * run stopped (its own time, as session_live and captures see it, starts at 0). Values written
 * and signals set during the run are gone.
 * Returns 0, or -1 with a message in err (in the wrong state nothing changes; if loading fails
 * the session is IDLE). */
int session_reset(session_t *s, session_summary_t *out, char *err, size_t errlen);

/* What session_load checks before it starts anything, without a session: opens the FMU, maps the
 * scenario's names onto it and builds each stimulus's signal (reading its data files), then frees
 * it all. No I/O backend, no log file, no instance. Returns 0, or -1 with a message in err. */
int scenario_check(const scenario_t *sc, char *err, size_t errlen);

lifecycle_t session_state(session_t *s);
/* The run's counters so far (steps, overruns, max_wake_ns, max_step_ns, state), also while it
 * runs; the other fields stay 0. */
void session_live(session_t *s, session_summary_t *out);
/* Why the run went to FAULT (empty otherwise). */
const char *session_fault(const session_t *s);
/* The loaded model's variables; NULL when IDLE. */
const modeldesc_t *session_model(const session_t *s);
/* The loaded scenario; NULL when IDLE. */
const scenario_t *session_scenario(const session_t *s);
/* Setting it to 1 ends a running loop after the current step. Lock-free, so it may be set from a
 * signal handler as well as from another thread. */
atomic_int *session_stop_flag(session_t *s);

/* Plays a signal on the scenario input `st->input` from now on (replacing its value or signal):
 * from t = 0 when INITIALIZED, or from the next step boundary when RUNNING. Data files are read
 * here, before the loop sees the signal. Returns 0, or -1 with a message in err. */
int session_set_signal(session_t *s, const stimulus_t *st, char *err, size_t errlen);

/* Writes a Float64 variable: an input of the scenario (replaces its signal from then on) or a
 * tunable parameter. Fixed parameters, outputs and inputs the scenario does not map are refused.
 * Allowed when INITIALIZED (takes effect at the first step) or RUNNING (at the next step boundary,
 * and the call returns once the loop has applied it). Returns 0, or -1 with a message in err. */
int session_write(session_t *s, const char *name, double value, char *err, size_t errlen);

/* Reads any Float64 variable of the model: when INITIALIZED or STOPPED directly, when RUNNING at
 * the next step boundary. Returns 0, or -1 with a message in err. */
int session_read(session_t *s, const char *name, double *value, char *err, size_t errlen);

/* Captures: record some of the scenario's inputs and outputs between a start and a stop trigger,
 * off the real-time thread (the log writer feeds them). Up to SESSION_MAX_CAPTURES per load; they
 * are freed by session_unload. */
#define SESSION_MAX_CAPTURES 8
#define CAPTURE_MAX_BYTES (256u << 20) /* row buffer limit per capture */

typedef struct {
  cond_op_t op; /* COND_NONE: no condition */
  char var[CFG_MAX_NAME];
  double value;
} capture_cond_t;

typedef struct {
  char vars[2 * TELEM_MAX_CH][CFG_MAX_NAME]; /* n_vars = 0: every input, then every output */
  size_t n_vars;
  capture_cond_t start; /* none: starts with the next step */
  capture_cond_t stop;  /* checked on each recorded row, the row is kept */
  double duration;      /* s, > 0: records this long after the start; 0: no limit */
  size_t max_rows;      /* 0: duration / step + 1, or 100000 without a duration */
} capture_spec_t;

typedef struct {
  cap_state_t state;
  size_t n_rows, n_cols;
  const char *names[1 + 2 * TELEM_MAX_CH]; /* "t", then the variables; valid until unload */
  double *rows;                            /* n_rows * n_cols, malloc'd: the caller frees it */
} capture_data_t;

/* Arms a capture when INITIALIZED or RUNNING. Returns its id (>= 0), or -1 with a message. */
int session_capture_start(session_t *s, const capture_spec_t *spec, char *err, size_t errlen);
/* A copy of what capture `id` has recorded so far. Once the run has ended every capture is
 * FINISHED and holds every row. Returns 0, or -1 with a message. */
int session_capture_fetch(session_t *s, int id, capture_data_t *out, char *err, size_t errlen);
/* Ends capture `id` now (it keeps its rows). Returns 0, or -1 with a message. */
int session_capture_stop(session_t *s, int id, char *err, size_t errlen);

#endif
