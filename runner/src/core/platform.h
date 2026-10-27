#ifndef KRUMPIR_PLATFORM_H
#define KRUMPIR_PLATFORM_H

#include <stdint.h>

/* Monotonic clock in nanoseconds. */
int64_t plat_now_ns(void);

/* Sleeps until the absolute monotonic time `t_ns`. Returns 0, or an errno value (EINTR when a
 * signal interrupted the sleep). */
int plat_sleep_until_ns(int64_t t_ns);

/* FMI platform directory name inside an FMU: binaries/<platform>/<modelIdentifier>.so */
#if defined(__linux__) && defined(__x86_64__)
#define KRUMPIR_FMI_PLATFORM "x86_64-linux"
#elif defined(__linux__) && defined(__aarch64__)
#define KRUMPIR_FMI_PLATFORM "aarch64-linux"
#else
#error "unsupported platform: add its FMI platform name here"
#endif

#endif
