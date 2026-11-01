#ifndef RTE_CALIB_H
#define RTE_CALIB_H

#include <stdbool.h>

#include "rte.h"

/* A calibration page (e.g. an XCP calibration segment) mapped onto runtime parameters. The page
 * is the tool's view: when its contents change, the new values are written into the parameters
 * once, so the console and the calibration tool can both set them. The calibration API itself
 * (locking the segment) stays with the caller. */

typedef struct {
  int ids[RTE_MAX_PARAMS]; /* parameter id of each page value, -1 when unknown */
  float last[RTE_MAX_PARAMS];
  int n;
  bool seen;
} rte_calib_t;

/* names[i] is the parameter that page value i sets. */
void rte_calib_init(rte_calib_t *c, const rte_t *r, const char *const *names, int n);

/* Call on the tick with the page's current values, before rte_tick_begin(). The first call only
 * records them; after that, a changed page writes every value in range into its parameter (out of
 * range ones are ignored). Returns true when the page changed. */
bool rte_calib_update(rte_calib_t *c, rte_t *r, const float *values);

#endif
