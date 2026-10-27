#ifndef KRUMPIR_RUNNER_H
#define KRUMPIR_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "lifecycle.h"

#define RUNNER_OK 0
#define RUNNER_SETUP_ERROR 1 /* bad scenario, FMU, mapping, log file: nothing was run */
#define RUNNER_FAULT 2       /* the run started and then failed */

typedef struct {
  lifecycle_t state; /* final state: STOPPED or FAULT */
  bool stopped_early;
  uint64_t steps;
  uint64_t overruns;
  uint64_t dropped; /* telemetry records lost because the log writer fell behind */
  uint64_t rows;    /* rows written to the CSV */
  int64_t max_wake_ns;
  int64_t max_step_ns;
  double mean_step_ns;
} runner_result_t;

/* Runs a scenario end to end: load the FMU, map the variables, open the io backend and the log,
 * initialize, step, then shut everything down. Returns RUNNER_OK, RUNNER_SETUP_ERROR or
 * RUNNER_FAULT; on failure err holds a one-line message. Clears any earlier stop request. */
int runner_run(const scenario_t *sc, runner_result_t *res, char *err, size_t errlen);

/* Asks the running scenario to stop after the current step. Async-signal-safe. */
void runner_request_stop(void);

#endif
