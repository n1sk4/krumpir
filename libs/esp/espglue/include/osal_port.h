#ifndef OSAL_PORT_H
#define OSAL_PORT_H

/* ESP-IDF + FreeRTOS port of the OSAL critical section (libs/osal/include/osal.h): a spinlock that
 * also masks interrupts on this core, so it works across both cores and from the tick task. */

#include "freertos/FreeRTOS.h"

typedef portMUX_TYPE osal_crit_t;
#define OSAL_CRIT_INIT portMUX_INITIALIZER_UNLOCKED

static inline void osal_crit_init(osal_crit_t *c) { portMUX_INITIALIZE(c); }
static inline void osal_crit_enter(osal_crit_t *c) { portENTER_CRITICAL(c); }
static inline void osal_crit_exit(osal_crit_t *c) { portEXIT_CRITICAL(c); }

#endif
