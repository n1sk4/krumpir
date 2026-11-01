#ifndef KRUMPIR_LOOP_H
#define KRUMPIR_LOOP_H

#include <signal.h>
#include <stdint.h>

#include "cmdq.h"
#include "config.h"
#include "fmu.h"
#include "io.h"
#include "ring.h"
#include "siggen.h"
#include "varmap.h"

/* The source of one input: the I/O backend, a fixed value, or a signal started at time t0. */
typedef enum { SRC_BACKEND, SRC_VALUE, SRC_SIGNAL } src_kind_t;

typedef struct {
  src_kind_t kind;
  double value;
  signal_t *sig;
  double t0;
} input_src_t;

/* Step period in whole nanoseconds. */
int64_t loop_step_ns(double step);

/* Simulation time of step n: computed as n*step, never accumulated, so it does not drift. */
double loop_time(uint64_t n, double step);

typedef struct {
  const scenario_t *sc;
  const varmap_t *vm;
  fmu_t *fmu;
  io_t *io;
  ring_t *ring;
  const volatile sig_atomic_t
      *stop; /* set from a signal handler or another thread to end the run */
  /* Optional (NULL for none): commands handled at the start of each step, and their replies */
  cmdq_t *cmd;
  cmdq_t *reply;
  /* Optional (NULL: every input from the I/O backend): where each input slot gets its value.
   * CMD_WRITE_INPUT and CMD_SET_SIGNAL change it while the loop runs. */
  input_src_t *src;
} loop_ctx_t;

typedef enum {
  LOOP_DONE,
  LOOP_STOPPED,
  LOOP_FMU_FAIL,
  LOOP_IO_FAIL,
  LOOP_TOO_MANY_OVERRUNS
} loop_end_t;

typedef struct {
  loop_end_t end;
  uint64_t steps;      /* completed steps */
  uint64_t overruns;   /* steps that finished after their deadline (paced runs only) */
  int64_t max_wake_ns; /* worst lateness of waking up for a tick */
  int64_t max_step_ns; /* longest fmu_step() */
  double sum_step_ns;
  uint64_t fail_step;       /* LOOP_FMU_FAIL: index of the failing step */
  fmu_result_t fail_result; /* LOOP_FMU_FAIL */
} loop_stats_t;

/* Runs the step loop of a scenario (the RUNNING state):
 *   wait for the tick, handle the queued commands, read inputs (backend, then the value or signal
 *   of each input that has one), set them on the FMU, fmu_step(),
 *   get outputs, write them to the io backend, push one telemetry record.
 * A command that fails (the FMU refuses a value) is answered with its status; it does not end the
 * run.
 * With wait = none the run is unpaced and lossless (it waits for the log writer instead of
 * dropping). With sleep or busy the ticks are absolute (start + n*step), a late step is counted as
 * an overrun, and the run ends with LOOP_TOO_MANY_OVERRUNS past max_overruns. The telemetry
 * record of step n has t = (n+1)*step, the time its outputs are valid. Does not allocate. */
void loop_run(const loop_ctx_t *c, loop_stats_t *st);

#endif
