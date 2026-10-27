#include "writer.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct writer {
  ring_t *ring;
  FILE *fp;
  size_t n_in;
  size_t n_out;
  atomic_bool stop;
  pthread_t thread;
  uint64_t rows;
};

static void write_row(writer_t *w, const telem_rec_t *rec) {
  fprintf(w->fp, "%" PRIu64 ",%.17g", rec->n, rec->t);
  for (size_t i = 0; i < w->n_in; i++) {
    fprintf(w->fp, ",%.17g", rec->in[i]);
  }
  for (size_t i = 0; i < w->n_out; i++) {
    fprintf(w->fp, ",%.17g", rec->out[i]);
  }
  fprintf(w->fp, ",%" PRId64 ",%" PRId64 ",%" PRIu32 "\n", rec->wake_ns, rec->step_ns, rec->flags);
  w->rows++;
}

static void *drain(void *arg) {
  writer_t *w = arg;
  telem_rec_t rec;
  for (;;) {
    /* Read the stop flag before draining so a record pushed before writer_stop() is never lost. */
    bool stopping = atomic_load(&w->stop);
    bool any = false;
    while (ring_pop(w->ring, &rec)) {
      write_row(w, &rec);
      any = true;
    }
    if (stopping) {
      break;
    }
    if (!any) {
      struct timespec ts = {0, 1000000}; /* ponytail: 1 ms poll, use a condvar if latency matters */
      nanosleep(&ts, NULL);
    }
  }
  return NULL;
}

writer_t *writer_start(ring_t *ring, const char *path, const char *const *in_names, size_t n_in,
                       const char *const *out_names, size_t n_out) {
  if (n_in > TELEM_MAX_CH || n_out > TELEM_MAX_CH) {
    return NULL;
  }
  writer_t *w = calloc(1, sizeof(*w));
  if (w == NULL) {
    return NULL;
  }
  w->ring = ring;
  w->n_in = n_in;
  w->n_out = n_out;
  atomic_init(&w->stop, false);
  w->fp = fopen(path, "w");
  if (w->fp == NULL) {
    free(w);
    return NULL;
  }
  fputs("n,t", w->fp);
  for (size_t i = 0; i < n_in; i++) {
    fprintf(w->fp, ",%s", in_names[i]);
  }
  for (size_t i = 0; i < n_out; i++) {
    fprintf(w->fp, ",%s", out_names[i]);
  }
  fputs(",wake_ns,step_ns,flags\n", w->fp);
  if (pthread_create(&w->thread, NULL, drain, w) != 0) {
    fclose(w->fp);
    free(w);
    return NULL;
  }
  return w;
}

uint64_t writer_stop(writer_t *w) {
  atomic_store(&w->stop, true);
  pthread_join(w->thread, NULL);
  fclose(w->fp);
  uint64_t rows = w->rows;
  free(w);
  return rows;
}
