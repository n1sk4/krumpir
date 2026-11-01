#ifndef KRUMPIR_CMDQ_H
#define KRUMPIR_CMDQ_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Commands from the control thread to the real-time loop, and replies back. The loop handles them
 * at the start of a step, before it reads the inputs, so every change takes effect at a step
 * boundary and never in the middle of one. */
typedef enum {
  CMD_WRITE_INPUT, /* input slot `index` takes `value` from now on (replaces its signal) */
  CMD_SET_SIGNAL,  /* input slot `index` plays signal `sig` from this step on */
  CMD_WRITE_PARAM, /* set the tunable parameter `vr` to `value` */
  CMD_READ         /* read variable `vr`; the reply carries the value */
} cmd_kind_t;

typedef struct {
  cmd_kind_t kind;
  uint32_t seq;   /* echoed in the reply */
  uint32_t vr;
  int32_t index;  /* CMD_WRITE_INPUT, CMD_SET_SIGNAL: position in the scenario's inputs */
  double value;
  void *sig;      /* CMD_SET_SIGNAL: the new signal; reply of both input commands: the replaced
                   * signal (or NULL), for the sender to free */
  int status;     /* reply: 0 ok, otherwise the fmu_result_t of the failed call */
  uint64_t step;  /* reply: the step at whose start the command was handled */
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
