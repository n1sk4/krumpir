#include "rte.h"

#include <string.h>

void rte_init(rte_t *r, const rte_param_t *params, int n, const rte_hooks_t *hooks)
{
  memset(r, 0, sizeof *r);
  osal_crit_init(&r->lock);
  r->params = params;
  r->n = n > RTE_MAX_PARAMS ? RTE_MAX_PARAMS : n;
  r->hooks = *hooks;
  for (int i = 0; i < r->n; i++)
    r->req[i] = params[i].def;
  r->first = true;
}

int rte_find(const rte_t *r, const char *name)
{
  for (int i = 0; i < r->n; i++) {
    if (!strcmp(r->params[i].name, name))
      return i;
  }
  return -1;
}

bool rte_set(rte_t *r, int id, float value)
{
  if (id < 0 || id >= r->n || !(value >= r->params[id].min && value <= r->params[id].max))
    return false;
  osal_crit_enter(&r->lock);
  r->req[id] = value;
  osal_crit_exit(&r->lock);
  return true;
}

float rte_get(rte_t *r, int id)
{
  osal_crit_enter(&r->lock);
  float v = r->req[id];
  osal_crit_exit(&r->lock);
  return v;
}

void rte_set_run(rte_t *r, bool run)
{
  osal_crit_enter(&r->lock);
  r->run_req = run;
  osal_crit_exit(&r->lock);
}

bool rte_running(rte_t *r)
{
  osal_crit_enter(&r->lock);
  bool run = r->run_req;
  osal_crit_exit(&r->lock);
  return run;
}

void rte_request_clear(rte_t *r)
{
  osal_crit_enter(&r->lock);
  r->clear_req = true;
  osal_crit_exit(&r->lock);
}

const char *rte_trip(const rte_t *r) { return r->hooks.trip(r->hooks.ctx); }

void rte_tick_begin(rte_t *r)
{
  /* Take the requests for this tick in one short critical section (no tearing). */
  float req[RTE_MAX_PARAMS];
  osal_crit_enter(&r->lock);
  memcpy(req, r->req, sizeof req);
  bool run = r->run_req;
  bool clear = r->clear_req;
  r->clear_req = false;
  osal_crit_exit(&r->lock);

  void *ctx = r->hooks.ctx;
  if (clear)
    r->hooks.clear(ctx);
  for (int i = 0; i < r->n; i++) {
    if (r->first || req[i] != r->applied[i]) {
      r->hooks.set_param(ctx, i, req[i]);
      r->applied[i] = req[i];
    }
  }
  r->first = false;
  r->start_refused = false;
  if (run != r->run_applied) {
    if (run)
      r->start_refused = !r->hooks.start(ctx); /* refused while a trip is latched */
    else
      r->hooks.stop(ctx);
    r->run_applied = run;
  }
}

void rte_tick_end(rte_t *r)
{
  if (r->run_applied && (r->start_refused || r->hooks.trip(r->hooks.ctx) != NULL)) {
    osal_crit_enter(&r->lock);
    r->run_req = false;
    osal_crit_exit(&r->lock);
  }
}
