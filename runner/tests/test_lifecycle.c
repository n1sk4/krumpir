#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lifecycle.h"

int main(void)
{
  /* allowed[from][to]: the whole 6x6 matrix spelled out. */
  static const bool allowed[LC_COUNT][LC_COUNT] = {
      /*                IDLE   LOADED INIT   RUN    STOP   FAULT */
      /* IDLE    */ {false, true, false, false, false, true},
      /* LOADED  */ {false, false, true, false, false, true},
      /* INIT    */ {false, false, false, true, false, true},
      /* RUNNING */ {false, false, false, false, true, true},
      /* STOPPED */ {false, false, false, false, false, true},
      /* FAULT   */ {false, false, false, false, false, true},
  };

  for (lifecycle_t from = LC_IDLE; from < LC_COUNT; from++) {
    for (lifecycle_t to = LC_IDLE; to < LC_COUNT; to++) {
      assert(lifecycle_can(from, to) == allowed[from][to]);

      lifecycle_t s = from;
      int rc = lifecycle_set(&s, to);
      if (allowed[from][to]) {
        assert(rc == 0 && s == to);
      } else {
        assert(rc == -1 && s == from); /* unchanged on a rejected transition */
      }
    }
  }

  /* Out-of-range values are rejected, not indexed. */
  assert(!lifecycle_can(LC_COUNT, LC_IDLE));
  assert(!lifecycle_can(LC_IDLE, LC_COUNT));
  assert(!lifecycle_can((lifecycle_t)-1, LC_IDLE));

  assert(strcmp(lifecycle_name(LC_INITIALIZED), "INITIALIZED") == 0);
  assert(strcmp(lifecycle_name(LC_COUNT), "?") == 0);

  puts("test_lifecycle: ok");
  return 0;
}
