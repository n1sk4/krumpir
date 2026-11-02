#ifndef RTE_CALIB_H
#define RTE_CALIB_H

#include <stdbool.h>
#include <stdint.h>

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
  uint32_t req_last;
  bool req_seen;
} rte_calib_t;

/* Run requests on a calibration page: a uint32_t whose low byte is the command and whose upper
 * bytes are a sequence number the tool counts up, so the same command can be sent again. */
enum { RTE_REQ_START = 1, RTE_REQ_STOP = 2, RTE_REQ_CLEAR = 3 };

/* names[i] is the parameter that page value i sets. */
void rte_calib_init(rte_calib_t *c, const rte_t *r, const char *const *names, int n);

/* Call on the tick with the page's current values, before rte_tick_begin(). The first call only
 * records them; after that, a changed page writes every value in range into its parameter (out of
 * range ones are ignored). Returns true when the page changed. */
bool rte_calib_update(rte_calib_t *c, rte_t *r, const float *values);

/* Call on the tick with the page's request value. The first call only records it (a value left on
 * the page from before does nothing); after that, a changed value runs its command as the console
 * would: start (refused at the tick while a trip is latched), stop, or clear. Unknown commands are
 * ignored. Returns the command run, or 0. */
int rte_calib_request(rte_calib_t *c, rte_t *r, uint32_t req);

#endif
