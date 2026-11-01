#ifndef RTE_H
#define RTE_H

#include <stdbool.h>

#include "osal.h"

/* The controller runtime (the RTE of an AUTOSAR-style component): parameters with limits, the
 * run/stop/clear state, and their hand-over from other tasks (console, calibration) to the control
 * tick, at a tick boundary and without tearing. The component supplies the parameter table and
 * hooks; everything here is platform independent (critical section from osal.h).
 *
 * On the tick:   rte_tick_begin(r);   component tick;   rte_tick_end(r);
 * From any task: rte_set(), rte_get(), rte_set_run(), rte_request_clear(). */

#define RTE_MAX_PARAMS 16

typedef struct {
  const char *name; /* console command and calibration name */
  const char *help; /* console help text */
  const char *hint; /* console argument hint, e.g. "<rpm>" */
  float min, max;   /* accepted range */
  float def;        /* value at boot */
} rte_param_t;

/* The component's side, called on the tick task except trip(). */
typedef struct {
  void (*set_param)(void *ctx, int id, float value); /* a parameter changed; all on the 1st tick */
  bool (*start)(void *ctx);       /* returns false when the start is refused */
  void (*stop)(void *ctx);
  void (*clear)(void *ctx);       /* clears a latched trip */
  const char *(*trip)(void *ctx); /* name of the latched trip, NULL when none; called by any task */
  void *ctx;
} rte_hooks_t;

typedef struct {
  const rte_param_t *params;
  int n;
  rte_hooks_t hooks;
  osal_crit_t lock;
  /* requested: written by any task under the lock */
  float req[RTE_MAX_PARAMS];
  bool run_req, clear_req;
  /* applied: owned by the tick */
  float applied[RTE_MAX_PARAMS];
  bool run_applied, start_refused, first;
} rte_t;

/* Parameters start at their defaults, stopped. n <= RTE_MAX_PARAMS. */
void rte_init(rte_t *r, const rte_param_t *params, int n, const rte_hooks_t *hooks);
int rte_find(const rte_t *r, const char *name); /* parameter id, or -1 */

/* Any task. rte_set returns false (and changes nothing) for an unknown id or a value out of range. */
bool rte_set(rte_t *r, int id, float value);
float rte_get(rte_t *r, int id); /* the requested value */
void rte_set_run(rte_t *r, bool run);
bool rte_running(rte_t *r); /* the requested run flag */
void rte_request_clear(rte_t *r);
const char *rte_trip(const rte_t *r); /* the latched trip, NULL when none */

/* Tick task. begin: a clear request, then changed parameters, then a run change go to the hooks.
 * end: a trip or a refused start drops the run request, so the controller stays stopped until it
 * is started again. */
void rte_tick_begin(rte_t *r);
void rte_tick_end(rte_t *r);

#endif
