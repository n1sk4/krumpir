/* Capture triggers on synthetic records: in[0] = n, out[0] = 10 n, t = (n+1) * 0.001. */
#include <assert.h>
#include <stdio.h>

#include "capture.h"

static void feed(capture_t *c, int from, int to)
{
  for (int n = from; n < to; n++) {
    telem_rec_t r = {.n = (uint64_t)n, .t = (n + 1) * 0.001};
    r.in[0] = n;
    r.out[0] = 10.0 * n;
    capture_feed(c, &r);
  }
}

static capture_def_t def(void)
{
  return (capture_def_t){.ch = {1, 0}, .n_ch = 2, .n_in = 1, .max_rows = 1000};
}

int main(void)
{
  const double *rows;

  /* No triggers: every record, in the channel order asked for (out then in) */
  capture_def_t d = def();
  capture_t *c = capture_create(&d);
  assert(capture_state(c) == CAP_ARMED);
  feed(c, 0, 5);
  assert(capture_state(c) == CAP_RUNNING && capture_rows(c, &rows) == 5 && capture_cols(c) == 3);
  assert(rows[3 * 4 + 0] == 0.005 && rows[3 * 4 + 1] == 40.0 && rows[3 * 4 + 2] == 4.0);
  capture_finish(c);
  feed(c, 5, 10);
  assert(capture_state(c) == CAP_FINISHED && capture_rows(c, &rows) == 5);
  capture_free(c);

  /* Start when in >= 10, stop at the row where out > 145 (n = 15, kept) */
  d = def();
  d.start = (cond_t){.ch = 0, .op = COND_GE, .value = 10};
  d.stop = (cond_t){.ch = 1, .op = COND_GT, .value = 145};
  c = capture_create(&d);
  feed(c, 0, 10);
  assert(capture_state(c) == CAP_ARMED && capture_rows(c, &rows) == 0);
  feed(c, 10, 100);
  assert(capture_state(c) == CAP_FINISHED && capture_rows(c, &rows) == 6);
  assert(rows[2] == 10.0 && rows[3 * 5 + 2] == 15.0);
  capture_free(c);

  /* Duration: 20 ms after the start trigger are 20 rows */
  d = def();
  d.start = (cond_t){.ch = 0, .op = COND_GT, .value = 99.5};
  d.duration = 0.020;
  c = capture_create(&d);
  feed(c, 0, 1000);
  assert(capture_state(c) == CAP_FINISHED && capture_rows(c, &rows) == 20);
  assert(rows[2] == 100.0 && rows[3 * 19 + 2] == 119.0);
  capture_free(c);

  /* A start condition that never holds stays armed; a full buffer finishes the capture */
  d = def();
  d.max_rows = 3;
  d.start = (cond_t){.ch = 1, .op = COND_LE, .value = -1};
  c = capture_create(&d);
  feed(c, 0, 50);
  assert(capture_state(c) == CAP_ARMED);
  capture_free(c);
  d.start = (cond_t){.ch = 1, .op = COND_LT, .value = 1e9};
  c = capture_create(&d);
  feed(c, 0, 50);
  assert(capture_state(c) == CAP_FINISHED && capture_rows(c, &rows) == 3);
  capture_free(c);

  puts("test_capture: ok");
  return 0;
}
