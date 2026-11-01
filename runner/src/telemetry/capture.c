#include "capture.h"

#include <stdbool.h>
#include <stdlib.h>

#define T_EPS 1e-9 /* s; t is n*step, so differences of record times carry rounding */

struct capture {
  capture_def_t d;
  cap_state_t state;
  double t_first;
  size_t n_rows;
  double *rows;
};

capture_t *capture_create(const capture_def_t *d)
{
  capture_t *c = calloc(1, sizeof *c);
  if (c == NULL) {
    return NULL;
  }
  c->d = *d;
  c->rows = malloc(d->max_rows * (1 + d->n_ch) * sizeof(double));
  if (c->rows == NULL) {
    free(c);
    return NULL;
  }
  return c;
}

void capture_free(capture_t *c)
{
  if (c != NULL) {
    free(c->rows);
    free(c);
  }
}

static double chan(const capture_t *c, const telem_rec_t *rec, int ch)
{
  return (size_t)ch < c->d.n_in ? rec->in[ch] : rec->out[(size_t)ch - c->d.n_in];
}

static bool holds(const capture_t *c, const cond_t *k, const telem_rec_t *rec)
{
  double v = chan(c, rec, k->ch);
  switch (k->op) {
  case COND_GT:
    return v > k->value;
  case COND_GE:
    return v >= k->value;
  case COND_LT:
    return v < k->value;
  case COND_LE:
    return v <= k->value;
  case COND_NONE:
    break;
  }
  return false;
}

void capture_feed(capture_t *c, const telem_rec_t *rec)
{
  if (c->state == CAP_FINISHED) {
    return;
  }
  if (c->state == CAP_ARMED) {
    if (c->d.start.op != COND_NONE && !holds(c, &c->d.start, rec)) {
      return;
    }
    c->state = CAP_RUNNING;
    c->t_first = rec->t;
  }
  if (c->d.duration > 0 && rec->t - c->t_first >= c->d.duration - T_EPS) {
    c->state = CAP_FINISHED;
    return;
  }
  double *row = c->rows + c->n_rows * (1 + c->d.n_ch);
  row[0] = rec->t;
  for (size_t i = 0; i < c->d.n_ch; i++) {
    row[1 + i] = chan(c, rec, c->d.ch[i]);
  }
  c->n_rows++;
  if (c->n_rows == c->d.max_rows || (c->d.stop.op != COND_NONE && holds(c, &c->d.stop, rec))) {
    c->state = CAP_FINISHED;
  }
}

void capture_finish(capture_t *c) { c->state = CAP_FINISHED; }

cap_state_t capture_state(const capture_t *c) { return c->state; }

size_t capture_rows(const capture_t *c, const double **rows)
{
  *rows = c->rows;
  return c->n_rows;
}

size_t capture_cols(const capture_t *c) { return 1 + c->d.n_ch; }

const char *cap_state_name(cap_state_t s)
{
  static const char *const names[] = {"ARMED", "RUNNING", "FINISHED"};
  return names[s];
}
