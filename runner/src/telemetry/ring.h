#ifndef KRUMPIR_RING_H
#define KRUMPIR_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEM_MAX_CH 16

#define TELEM_FLAG_OVERRUN 0x1u

/* One logged step. Fixed size so the ring never allocates after init. */
typedef struct {
  uint64_t n;
  double t;
  double in[TELEM_MAX_CH];
  double out[TELEM_MAX_CH];
  int64_t wake_ns;
  int64_t step_ns;
  uint32_t flags;
} telem_rec_t;

/* Single-producer, single-consumer lock-free ring. The producer never blocks: a full ring drops
 * the record and counts it. */
typedef struct {
  telem_rec_t *buf;
  size_t mask;
  _Atomic size_t head; /* next slot to write, owned by the producer */
  _Atomic size_t tail; /* next slot to read, owned by the consumer */
  _Atomic uint64_t dropped;
} ring_t;

/* capacity must be a power of two, at least 2. Returns 0 on success, -1 otherwise. */
int ring_init(ring_t *r, size_t capacity);
void ring_free(ring_t *r);

/* Producer side. Returns false (and counts a drop) when the ring is full. */
bool ring_push(ring_t *r, const telem_rec_t *rec);

/* Producer side. True if the next push would drop the record. Lets a caller that must not lose
 * data (an unpaced run) wait for the consumer instead of pushing. */
bool ring_full(const ring_t *r);

/* Consumer side. Returns false when the ring is empty. */
bool ring_pop(ring_t *r, telem_rec_t *rec);

uint64_t ring_dropped(const ring_t *r);

#endif
