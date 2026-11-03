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
  writer_tap_t tap;
  void *tap_ctx;
  _Atomic size_t done; /* records written and tapped */
  writer_cont_t off;   /* added to n and t in the file */
};

static void write_row(writer_t *w, const telem_rec_t *rec)
{
  fprintf(w->fp, "%" PRIu64 ",%.17g", rec->n + w->off.n, rec->t + w->off.t);
  for (size_t i = 0; i < w->n_in; i++) {
    fprintf(w->fp, ",%.17g", rec->in[i]);
  }
  for (size_t i = 0; i < w->n_out; i++) {
    fprintf(w->fp, ",%.17g", rec->out[i]);
  }
  fprintf(w->fp, ",%" PRId64 ",%" PRId64 ",%" PRIu32 "\n", rec->wake_ns, rec->step_ns, rec->flags);
  w->rows++;
}

static void *drain(void *arg)
{
  writer_t *w = arg;
  telem_rec_t rec;
  for (;;) {
    /* Read the stop flag before draining so a record pushed before writer_stop() is never lost. */
    bool stopping = atomic_load(&w->stop);
    bool any = false;
    while (ring_pop(w->ring, &rec)) {
      write_row(w, &rec);
      if (w->tap != NULL) {
        w->tap(w->tap_ctx, &rec);
      }
      atomic_fetch_add(&w->done, 1);
      any = true;
    }
    if (stopping) {
      break;
    }
    if (!any) {
      struct timespec ts = {0, 1000000}; /* Limitation: 1 ms poll, use a condvar if latency matters */
      nanosleep(&ts, NULL);
    }
  }
  return NULL;
}

writer_t *writer_start(ring_t *ring, const char *path, const writer_cont_t *cont,
                       const char *const *in_names, size_t n_in, const char *const *out_names,
                       size_t n_out, writer_tap_t tap, void *tap_ctx)
{
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
  w->tap = tap;
  w->tap_ctx = tap_ctx;
  atomic_init(&w->stop, false);
  atomic_init(&w->done, 0);
  if (cont != NULL) {
    w->off = *cont;
  }
  w->fp = fopen(path, cont != NULL ? "a" : "w");
  if (w->fp == NULL) {
    free(w);
    return NULL;
  }
  if (fseek(w->fp, 0, SEEK_END) == 0 && ftell(w->fp) == 0) { /* a new or empty file */
    fputs("n,t", w->fp);
    for (size_t i = 0; i < n_in; i++) {
      fprintf(w->fp, ",%s", in_names[i]);
    }
    for (size_t i = 0; i < n_out; i++) {
      fprintf(w->fp, ",%s", out_names[i]);
    }
    fputs(",wake_ns,step_ns,flags\n", w->fp);
  }
  if (pthread_create(&w->thread, NULL, drain, w) != 0) {
    fclose(w->fp);
    free(w);
    return NULL;
  }
  return w;
}

void writer_flush(writer_t *w)
{
  /* head counts the records pushed (drops are not pushed); the writer pops them in order */
  size_t target = atomic_load(&w->ring->head);
  while (atomic_load(&w->done) < target) {
    struct timespec ts = {0, 200000};
    nanosleep(&ts, NULL);
  }
}

uint64_t writer_stop(writer_t *w)
{
  atomic_store(&w->stop, true);
  pthread_join(w->thread, NULL);
  fclose(w->fp);
  uint64_t rows = w->rows;
  free(w);
  return rows;
}
