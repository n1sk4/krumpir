#include "ring.h"

#include <stdlib.h>

int ring_init(ring_t *r, size_t capacity) {
  if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
    return -1;
  }
  r->buf = calloc(capacity, sizeof(telem_rec_t));
  if (r->buf == NULL) {
    return -1;
  }
  r->mask = capacity - 1;
  atomic_init(&r->head, 0);
  atomic_init(&r->tail, 0);
  atomic_init(&r->dropped, 0);
  return 0;
}

void ring_free(ring_t *r) {
  free(r->buf);
  r->buf = NULL;
}

bool ring_push(ring_t *r, const telem_rec_t *rec) {
  size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
  size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
  if (head - tail > r->mask) {
    atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
    return false;
  }
  r->buf[head & r->mask] = *rec;
  atomic_store_explicit(&r->head, head + 1, memory_order_release);
  return true;
}

bool ring_full(const ring_t *r) {
  size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
  size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
  return head - tail > r->mask;
}

bool ring_pop(ring_t *r, telem_rec_t *rec) {
  size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
  size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
  if (tail == head) {
    return false;
  }
  *rec = r->buf[tail & r->mask];
  atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
  return true;
}

uint64_t ring_dropped(const ring_t *r) {
  return atomic_load_explicit(&r->dropped, memory_order_relaxed);
}
