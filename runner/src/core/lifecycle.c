#include "lifecycle.h"

bool lifecycle_can(lifecycle_t from, lifecycle_t to)
{
  if (from >= LC_COUNT || to >= LC_COUNT) {
    return false;
  }
  if (to == LC_FAULT) {
    return true;
  }
  /* The happy path is strictly sequential up to STOPPED; STOPPED and FAULT are terminal. */
  return from < LC_STOPPED && to == from + 1;
}

int lifecycle_set(lifecycle_t *s, lifecycle_t to)
{
  if (!lifecycle_can(*s, to)) {
    return -1;
  }
  *s = to;
  return 0;
}

const char *lifecycle_name(lifecycle_t s)
{
  static const char *const names[LC_COUNT] = {"IDLE",    "LOADED",  "INITIALIZED",
                                              "RUNNING", "STOPPED", "FAULT"};
  return s < LC_COUNT ? names[s] : "?";
}
