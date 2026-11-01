#ifndef KRUMPIR_CAPTURE_H
#define KRUMPIR_CAPTURE_H

#include <stddef.h>

#include "ring.h"

/* A capture records selected channels of the telemetry records between a start and a stop
 * trigger (the Capture of ASAM XIL, with condition and duration watchers). It is fed by the log
 * writer, off the real-time thread, and is not thread-safe by itself.
 *
 * Channels are numbered like a record: 0..n_in-1 are rec.in[], n_in.. are rec.out[]. */
typedef enum { CAP_ARMED, CAP_RUNNING, CAP_FINISHED } cap_state_t;

typedef enum { COND_NONE, COND_GT, COND_GE, COND_LT, COND_LE } cond_op_t;

typedef struct {
  int ch;
  cond_op_t op; /* COND_NONE: no condition */
  double value;
} cond_t;

typedef struct {
  int ch[2 * TELEM_MAX_CH]; /* channels to record, in this order */
  size_t n_ch;
  size_t n_in;      /* channels below n_in are inputs */
  cond_t start;     /* recording starts at the first record where it holds; none: at once */
  cond_t stop;      /* recording ends after the first recorded row where it holds */
  double duration;  /* s, > 0: rows with t - t_first < duration are recorded; 0: no limit */
  size_t max_rows;  /* > 0; the capture finishes when its buffer is full */
} capture_def_t;

typedef struct capture capture_t;

/* NULL when out of memory. The row buffer is allocated here, at its full size. */
capture_t *capture_create(const capture_def_t *d);
void capture_free(capture_t *c);

void capture_feed(capture_t *c, const telem_rec_t *rec);
/* The run ended: whatever state it is in, the capture is finished. */
void capture_finish(capture_t *c);

cap_state_t capture_state(const capture_t *c);
/* Rows recorded so far, each 1 + n_ch doubles: t (the record's time), then the channels. */
size_t capture_rows(const capture_t *c, const double **rows);
size_t capture_cols(const capture_t *c);

const char *cap_state_name(cap_state_t s);

#endif
