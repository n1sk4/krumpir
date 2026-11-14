#ifndef KRUMPIR_RUNNER_H
#define KRUMPIR_RUNNER_H

#include <stddef.h>

#include "session.h"

#define RUNNER_OK 0
#define RUNNER_SETUP_ERROR 1 /* bad scenario, FMU, mapping, log file: nothing was run */
#define RUNNER_FAULT 2       /* the run started and then failed */

/* Runs a scenario end to end: load the FMU, map the variables, open the io backend and the log,
 * initialize, step, then shut everything down. Returns RUNNER_OK, RUNNER_SETUP_ERROR or
 * RUNNER_FAULT; on failure err holds a one-line message. res->state is STOPPED or FAULT. Clears
 * any earlier stop request. */
int runner_run(const scenario_t *sc, session_summary_t *res, char *err, size_t errlen);

/* Asks the running scenario to stop after the current step. Async-signal-safe. */
void runner_request_stop(void);

#endif
