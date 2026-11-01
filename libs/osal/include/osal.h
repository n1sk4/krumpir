#ifndef OSAL_H
#define OSAL_H

/* OS abstraction: what the controllers and the shared runtime need from the platform. This folder
 * holds the contract only, free of platform headers; each platform port (libs/esp/espglue for
 * ESP-IDF + FreeRTOS, later Zephyr) implements it:
 *
 *   osal.h          critical section, from the port's osal_port.h:
 *                     osal_crit_t      lock type,  OSAL_CRIT_INIT  its static initializer
 *                     osal_crit_enter(osal_crit_t *), osal_crit_exit(osal_crit_t *)
 *                   Short sections only (copying a settings struct); usable from the tick task
 *                   and from other tasks, on any core.
 *   tick_task.h     the fixed-rate control task
 *   telemetry.h     non-blocking telemetry from the control task
 *   console_util.h  the command console */

#include "osal_port.h"

#endif
