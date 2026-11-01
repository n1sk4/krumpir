/* Real-time setup: argument checks always; the thread really pinned and SCHED_FIFO, locked
 * memory, and a real-time session run when the OS permits it (root, or rtprio/memlock limits).
 * Otherwise the privileged part reports SKIP (exit 77). */
#define _GNU_SOURCE /* sched_getcpu */
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rt_setup.h"
#include "session.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

#define SKIP 77

/* AddressSanitizer turns mlockall() into a no-op that reports success: no VmLck to check. */
#if defined(__SANITIZE_ADDRESS__)
#define LOCKS_MEMORY 0
#else
#define LOCKS_MEMORY 1
#endif

static int seen_policy, seen_cpu, seen_prio;

static void *probe(void *arg)
{
  (void)arg;
  struct sched_param sp;
  pthread_getschedparam(pthread_self(), &seen_policy, &sp);
  seen_prio = sp.sched_priority;
  seen_cpu = sched_getcpu();
  rt_prefault_stack();
  return NULL;
}

static long locked_kb(void)
{
  FILE *f = fopen("/proc/self/status", "r");
  char line[128];
  long kb = -1;
  while (f != NULL && fgets(line, sizeof line, f) != NULL) {
    if (sscanf(line, "VmLck: %ld", &kb) == 1) {
      break;
    }
  }
  if (f != NULL) {
    fclose(f);
  }
  return kb;
}

static void test_arguments(void)
{
  pthread_attr_t a;
  char err[256];
  assert(rt_thread_attr(&a, 4096, -1, err, sizeof err) == -1 && strstr(err, "rt.cpu = 4096"));
  assert(rt_thread_attr(&a, -1, 100, err, sizeof err) == -1 && strstr(err, "SCHED_FIFO's"));
  assert(rt_thread_attr(&a, -1, -1, err, sizeof err) == 0); /* defaults */
  pthread_attr_destroy(&a);

  rt_create_error(EPERM, 0, 80, err, sizeof err);
  assert(strstr(err, "priority 80 is not permitted") && strstr(err, "rtprio"));
  rt_create_error(EINVAL, 3, -1, err, sizeof err);
  assert(strstr(err, "CPU 3"));
}

/* Returns 0, or SKIP when the OS does not permit SCHED_FIFO. */
static int test_thread(void)
{
  pthread_attr_t a;
  char err[256];
  int cpu = (int)sysconf(_SC_NPROCESSORS_ONLN) - 1;
  assert(rt_thread_attr(&a, cpu, 10, err, sizeof err) == 0);
  pthread_t t;
  int e = pthread_create(&t, &a, probe, NULL);
  pthread_attr_destroy(&a);
  if (e == EPERM) {
    rt_create_error(e, cpu, 10, err, sizeof err);
    fprintf(stderr, "SKIP: %s\n", err);
    return SKIP;
  }
  assert(e == 0);
  pthread_join(t, NULL);
  assert(seen_policy == SCHED_FIFO && seen_prio == 10 && seen_cpu == cpu);

  assert(rt_lock_memory(err, sizeof err) == 0);
  assert(!LOCKS_MEMORY || locked_kb() > 0);
  rt_unlock_memory();
  assert(locked_kb() == 0);
  fprintf(stderr, "thread on CPU %d at SCHED_FIFO 10, memory locked: ok\n", cpu);
  return 0;
}

/* A session with rt.cpu and rt.priority runs and unlocks its memory at unload. */
static void test_session(void)
{
  char text[1024], err[256], log[128];
  snprintf(log, sizeof log, "/tmp/krumpir_test_rt_%d.csv", (int)getpid());
  snprintf(text, sizeof text,
           "[model]\nfmu = \"%s/Feedthrough.fmu\"\nstep = 0.001\nduration = 0.2\n"
           "[rt]\nwait = \"sleep\"\ncpu = 0\npriority = 20\nmax_overruns = 1000000\n"
           "[io]\nbackend = \"null\"\n[log]\nfile = \"%s\"\n",
           KRUMPIR_FMU_DIR, log);
  scenario_t sc;
  assert(config_parse(text, &sc, err, sizeof err) == 0);
  session_t *s = session_create();
  if (session_load(s, &sc, err, sizeof err) != 0) {
    fprintf(stderr, "load: %s\n", err);
    assert(0);
  }
  assert(!LOCKS_MEMORY || locked_kb() > 0);
  assert(session_start(s, err, sizeof err) == 0);
  assert(session_wait(s, 5000) && session_state(s) == LC_STOPPED);
  session_summary_t sum;
  session_unload(s, &sum);
  assert(sum.steps == 200 && locked_kb() == 0);
  session_destroy(s);
  remove(log);
  fprintf(stderr, "real-time session: ok\n");
}

int main(void)
{
  test_arguments();
  if (test_thread() == SKIP) {
    return SKIP;
  }
  char fmu[300];
  snprintf(fmu, sizeof fmu, "%s/Feedthrough.fmu", KRUMPIR_FMU_DIR);
  if (access(fmu, R_OK) == 0) {
    test_session();
  }
  return 0;
}
