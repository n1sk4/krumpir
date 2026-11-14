#ifndef KRUMPIR_CMDQ_H
#define KRUMPIR_CMDQ_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Commands from the control thread to the real-time loop, and replies back. The loop handles them
 * at the start of a step, before it reads the inputs, so every change takes effect at a step
 * boundary and never in the middle of one. */
typedef enum {
  CMD_READ, /* read the variables vrs[0..n) into values[]; the reply carries the time in t */
  CMD_WRITE /* set the tunable parameters vrs[0..n) to values[]; if the FMU takes them, each input
             * slot i in `inputs` swaps its source with srcs[i] (from this step on) */
} cmd_kind_t;

/* The arrays belong to the sender, which leaves them alone until the reply. */
typedef struct {
  cmd_kind_t kind;
  uint32_t seq; /* echoed in the reply */
  uint32_t n;
  uint32_t inputs; /* CMD_WRITE: bit i for input slot i */
  const uint32_t *vrs;
  double *values;
  void *srcs; /* CMD_WRITE: input_src_t[TELEM_MAX_CH]; after the reply it holds the sources
               * replaced, for the sender to free their signals */
  double t;   /* reply of CMD_READ: the time of the step boundary it was sampled at */
  int status; /* reply: 0 ok, otherwise the fmu_result_t of the failed call */
} cmd_t;

#define CMDQ_CAPACITY 64 /* power of two */

/* Single-producer, single-consumer lock-free queue of commands, fixed size (no allocation). */
typedef struct {
  cmd_t buf[CMDQ_CAPACITY];
  _Atomic uint32_t head; /* next slot to write, owned by the producer */
  _Atomic uint32_t tail; /* next slot to read, owned by the consumer */
} cmdq_t;

void cmdq_init(cmdq_t *q);
bool cmdq_push(cmdq_t *q, const cmd_t *c); /* false when full */
bool cmdq_pop(cmdq_t *q, cmd_t *c);        /* false when empty */

#endif
