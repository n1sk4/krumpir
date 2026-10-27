#ifndef KRUMPIR_LIFECYCLE_H
#define KRUMPIR_LIFECYCLE_H

#include <stdbool.h>

/* IDLE -> LOADED -> INITIALIZED -> RUNNING -> STOPPED, and FAULT from any state. */
typedef enum {
  LC_IDLE,
  LC_LOADED,
  LC_INITIALIZED,
  LC_RUNNING,
  LC_STOPPED,
  LC_FAULT,
  LC_COUNT
} lifecycle_t;

bool lifecycle_can(lifecycle_t from, lifecycle_t to);

/* Moves *s to `to`. Returns 0 on success; returns -1 and leaves *s unchanged if the transition is
 * not allowed. */
int lifecycle_set(lifecycle_t *s, lifecycle_t to);

const char *lifecycle_name(lifecycle_t s);

#endif
