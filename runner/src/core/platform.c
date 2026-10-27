#include "platform.h"

#include <time.h>

int64_t plat_now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int plat_sleep_until_ns(int64_t t_ns)
{
  struct timespec ts = {.tv_sec = t_ns / 1000000000LL, .tv_nsec = t_ns % 1000000000LL};
  return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}
