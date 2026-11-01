#define _GNU_SOURCE /* cpu_set_t, pthread_attr_setaffinity_np */
#include "rt_setup.h"

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int rt_thread_attr(pthread_attr_t *a, int cpu, int priority, char *err, size_t errlen)
{
  pthread_attr_init(a);
  if (cpu >= 0) {
    long n = sysconf(_SC_NPROCESSORS_CONF);
    if (cpu >= n || cpu >= CPU_SETSIZE) {
      snprintf(err, errlen, "rt.cpu = %d, but this machine has CPUs 0 to %ld", cpu, n - 1);
      pthread_attr_destroy(a);
      return -1;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_attr_setaffinity_np(a, sizeof set, &set);
  }
  if (priority >= 0) {
    int lo = sched_get_priority_min(SCHED_FIFO), hi = sched_get_priority_max(SCHED_FIFO);
    if (priority < lo || priority > hi) {
      snprintf(err, errlen, "rt.priority = %d is outside SCHED_FIFO's %d to %d", priority, lo, hi);
      pthread_attr_destroy(a);
      return -1;
    }
    struct sched_param sp = {.sched_priority = priority};
    pthread_attr_setinheritsched(a, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(a, SCHED_FIFO);
    pthread_attr_setschedparam(a, &sp);
  }
  return 0;
}

void rt_create_error(int e, int cpu, int priority, char *err, size_t errlen)
{
  if (e == EPERM && priority >= 0) {
    snprintf(err, errlen,
             "SCHED_FIFO priority %d is not permitted: run as root, or allow real-time "
             "priorities for this user (rtprio in /etc/security/limits.conf)",
             priority);
  } else if (e == EINVAL && cpu >= 0) {
    snprintf(err, errlen,
             "cannot run the loop on CPU %d (offline, or outside this process's cpuset)", cpu);
  } else {
    snprintf(err, errlen, "cannot start the loop thread: %s", strerror(e));
  }
}

int rt_lock_memory(char *err, size_t errlen)
{
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    snprintf(err, errlen,
             "cannot lock memory (mlockall: %s): run as root, or raise memlock in "
             "/etc/security/limits.conf",
             strerror(errno));
    return -1;
  }
  return 0;
}

void rt_unlock_memory(void) { munlockall(); }

void rt_prefault_stack(void)
{
  volatile unsigned char stack[RT_STACK_PREFAULT];
  for (size_t i = 0; i < sizeof stack; i += 4096) {
    stack[i] = 0;
  }
}
