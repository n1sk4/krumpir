#ifndef OSAL_PORT_H
#define OSAL_PORT_H

/* Host (PC) port of the OSAL critical section: a C11 spinlock, so it needs no thread library and
 * works the same with gcc, MinGW and clang. */

#include <stdatomic.h>

typedef struct {
  atomic_flag f;
} osal_crit_t;
#define OSAL_CRIT_INIT {ATOMIC_FLAG_INIT}

static inline void osal_crit_init(osal_crit_t *c) { atomic_flag_clear(&c->f); }
static inline void osal_crit_enter(osal_crit_t *c)
{
  while (atomic_flag_test_and_set_explicit(&c->f, memory_order_acquire)) {
  }
}
static inline void osal_crit_exit(osal_crit_t *c)
{
  atomic_flag_clear_explicit(&c->f, memory_order_release);
}

#endif
