#ifndef RING_BUF_H
#define RING_BUF_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Telemetry sample; units as in the CSV: ms, rpm, rpm, duty -1..+1. */
typedef struct {
    uint32_t t_ms;
    float sp, rpm, duty;
} telem_sample_t;

#define RING_BUF_CAP 128 /* power of two */

/* Lock-free single-producer / single-consumer ring. Push never blocks: a full ring drops the
 * sample and counts it. */
typedef struct {
    telem_sample_t buf[RING_BUF_CAP];
    atomic_uint head; /* written by the producer */
    atomic_uint tail; /* written by the consumer */
    atomic_uint dropped;
} ring_buf_t;

void ring_buf_init(ring_buf_t *r);
bool ring_buf_push(ring_buf_t *r, const telem_sample_t *s);
bool ring_buf_pop(ring_buf_t *r, telem_sample_t *out);
uint32_t ring_buf_dropped(ring_buf_t *r);

#endif
