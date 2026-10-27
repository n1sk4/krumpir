#include <assert.h>
#include <stdio.h>

#include "loop.h"

int main(void)
{
  /* Step period in whole nanoseconds. */
  assert(loop_step_ns(0.001) == 1000000);
  assert(loop_step_ns(0.0005) == 500000);
  assert(loop_step_ns(1e-7) == 100);
  assert(loop_step_ns(1.0) == 1000000000);

  /* t = n*h is computed from n, so it is exact and strictly increasing for a 10^7-step run
   * (2.8 hours at 1 kHz). */
  const double h = 0.001;
  assert(loop_time(0, h) == 0.0);
  double prev = loop_time(0, h);
  for (uint64_t n = 1; n <= 10000000; n++) {
    double t = loop_time(n, h);
    assert(t == (double)n * h);
    assert(t > prev);
    prev = t;
  }
  assert(loop_time(3600000, h) > 3599.999999 && loop_time(3600000, h) < 3600.000001);

  /* The absolute tick schedule in integer nanoseconds has no rounding at all. */
  const int64_t step_ns = loop_step_ns(h);
  assert((int64_t)10000000 * step_ns == 10000000000000LL);

  puts("test_loop_time: ok");
  return 0;
}
