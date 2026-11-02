#include "rte_calib.h"

#include <string.h>

void rte_calib_init(rte_calib_t *c, const rte_t *r, const char *const *names, int n)
{
  memset(c, 0, sizeof *c);
  c->n = n > RTE_MAX_PARAMS ? RTE_MAX_PARAMS : n;
  for (int i = 0; i < c->n; i++)
    c->ids[i] = rte_find(r, names[i]);
}

bool rte_calib_update(rte_calib_t *c, rte_t *r, const float *values)
{
  bool changed = c->seen && memcmp(values, c->last, c->n * sizeof values[0]) != 0;
  memcpy(c->last, values, c->n * sizeof values[0]);
  c->seen = true;
  if (!changed)
    return false;
  for (int i = 0; i < c->n; i++)
    rte_set(r, c->ids[i], values[i]); /* ignored when unknown or out of range */
  return true;
}

int rte_calib_request(rte_calib_t *c, rte_t *r, uint32_t req)
{
  bool changed = c->req_seen && req != c->req_last;
  c->req_last = req;
  c->req_seen = true;
  if (!changed)
    return 0;
  switch (req & 0xFFu) {
  case RTE_REQ_START:
    rte_set_run(r, true);
    return RTE_REQ_START;
  case RTE_REQ_STOP:
    rte_set_run(r, false);
    return RTE_REQ_STOP;
  case RTE_REQ_CLEAR:
    rte_request_clear(r);
    return RTE_REQ_CLEAR;
  }
  return 0;
}
