#ifndef KRUMPIR_RT_SETUP_H
#define KRUMPIR_RT_SETUP_H

#include <pthread.h>
#include <stddef.h>

#define RT_STACK_PREFAULT (256 * 1024) /* bytes of the loop thread's stack touched before the loop */

/* Real-time setup of the loop thread and the process (Linux).
 *
 * Initializes `a` for the loop thread: pinned to `cpu` and scheduled SCHED_FIFO at `priority`,
 * each only if >= 0 (else the default). The settings are checked here as far as possible; whether
 * the OS permits them shows when pthread_create() is called with `a` (EPERM): rt_create_error()
 * turns that into a message. Returns 0, or -1 with a message in err. */
int rt_thread_attr(pthread_attr_t *a, int cpu, int priority, char *err, size_t errlen);
void rt_create_error(int e, int cpu, int priority, char *err, size_t errlen);

/* Locks all current and future memory of the process into RAM (no page faults in the loop). */
int rt_lock_memory(char *err, size_t errlen);
void rt_unlock_memory(void);

/* Touches RT_STACK_PREFAULT bytes of the calling thread's stack, so the loop never faults on it. */
void rt_prefault_stack(void);

#endif
