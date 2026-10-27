#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include "ring.h"

static telem_rec_t make(uint64_t n) {
  telem_rec_t r = {0};
  r.n = n;
  r.t = (double)n * 0.001;
  r.out[0] = (double)n;
  return r;
}

static void test_init_rejects_bad_capacity(void) {
  ring_t r;
  assert(ring_init(&r, 0) == -1);
  assert(ring_init(&r, 1) == -1);
  assert(ring_init(&r, 3) == -1);
  assert(ring_init(&r, 8) == 0);
  ring_free(&r);
}

static void test_fifo_order_and_wraparound(void) {
  ring_t r;
  assert(ring_init(&r, 4) == 0);
  telem_rec_t rec;
  assert(!ring_pop(&r, &rec));
  /* Several laps around a 4-slot ring. */
  uint64_t next_in = 0, next_out = 0;
  for (int lap = 0; lap < 10; lap++) {
    for (int i = 0; i < 3; i++) {
      rec = make(next_in++);
      assert(ring_push(&r, &rec));
    }
    for (int i = 0; i < 3; i++) {
      assert(ring_pop(&r, &rec));
      assert(rec.n == next_out++);
    }
    assert(!ring_pop(&r, &rec));
  }
  assert(ring_dropped(&r) == 0);
  ring_free(&r);
}

static void test_full_ring_drops_and_counts(void) {
  ring_t r;
  assert(ring_init(&r, 4) == 0);
  telem_rec_t rec;
  for (uint64_t i = 0; i < 4; i++) {
    rec = make(i);
    assert(ring_push(&r, &rec));
  }
  rec = make(99);
  assert(!ring_push(&r, &rec));
  assert(!ring_push(&r, &rec));
  assert(ring_dropped(&r) == 2);
  /* The dropped records did not overwrite anything. */
  for (uint64_t i = 0; i < 4; i++) {
    assert(ring_pop(&r, &rec));
    assert(rec.n == i);
  }
  assert(!ring_pop(&r, &rec));
  ring_free(&r);
}

static void test_full_query(void) {
  ring_t r;
  assert(ring_init(&r, 4) == 0);
  telem_rec_t rec = make(0);
  for (int i = 0; i < 4; i++) {
    assert(!ring_full(&r));
    assert(ring_push(&r, &rec));
  }
  assert(ring_full(&r));
  assert(ring_dropped(&r) == 0); /* asking does not count as a drop */
  assert(ring_pop(&r, &rec));
  assert(!ring_full(&r));
  ring_free(&r);
}

#define STRESS_COUNT 1000000u

static void *producer(void *arg) {
  ring_t *r = arg;
  for (uint64_t i = 0; i < STRESS_COUNT; i++) {
    telem_rec_t rec = make(i);
    /* Retry on full so the test checks ordering and integrity without loss. */
    while (!ring_push(r, &rec)) {
      sched_yield();
    }
  }
  return NULL;
}

static void test_two_thread_stress(void) {
  ring_t r;
  assert(ring_init(&r, 1024) == 0);
  pthread_t th;
  assert(pthread_create(&th, NULL, producer, &r) == 0);
  telem_rec_t rec;
  for (uint64_t i = 0; i < STRESS_COUNT; i++) {
    while (!ring_pop(&r, &rec)) {
      sched_yield();
    }
    assert(rec.n == i);
    assert(rec.out[0] == (double)i);
  }
  pthread_join(th, NULL);
  assert(!ring_pop(&r, &rec));
  ring_free(&r);
}

int main(void) {
  test_init_rejects_bad_capacity();
  test_fifo_order_and_wraparound();
  test_full_ring_drops_and_counts();
  test_full_query();
  test_two_thread_stress();
  puts("test_ring: ok");
  return 0;
}
