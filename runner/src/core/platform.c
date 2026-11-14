#include "platform.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *plat_read_file(const char *path, size_t max, size_t *len, char *err, size_t errlen)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    snprintf(err, errlen, "cannot open '%s': %s", path, strerror(errno));
    return NULL;
  }
  char *buf = malloc(max + 1);
  const size_t n = buf != NULL ? fread(buf, 1, max, f) : 0;
  const bool failed = buf == NULL || ferror(f), more = !failed && fgetc(f) != EOF;
  fclose(f);
  if (failed || more) {
    if (more) {
      snprintf(err, errlen, "'%s' is larger than %zu bytes", path, max);
    } else {
      snprintf(err, errlen, "cannot read '%s'", path);
    }
    free(buf);
    errno = more ? EFBIG : EIO;
    return NULL;
  }
  buf[n] = '\0';
  if (len != NULL) {
    *len = n;
  }
  return buf;
}

int64_t plat_now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int plat_sleep_until_ns(int64_t t_ns)
{
  struct timespec ts = {.tv_sec = t_ns / 1000000000LL, .tv_nsec = t_ns % 1000000000LL};
  return clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}
