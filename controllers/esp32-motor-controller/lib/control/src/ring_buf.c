#include "ring_buf.h"

void ring_buf_init(ring_buf_t *r)
{
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    atomic_init(&r->dropped, 0);
}

bool ring_buf_push(ring_buf_t *r, const telem_sample_t *s)
{
    unsigned head = atomic_load_explicit(&r->head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head - tail >= RING_BUF_CAP) {
        atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
        return false;
    }
    r->buf[head % RING_BUF_CAP] = *s;
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return true;
}

bool ring_buf_pop(ring_buf_t *r, telem_sample_t *out)
{
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail == head) return false;
    *out = r->buf[tail % RING_BUF_CAP];
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return true;
}

uint32_t ring_buf_dropped(ring_buf_t *r)
{
    return atomic_load_explicit(&r->dropped, memory_order_relaxed);
}
