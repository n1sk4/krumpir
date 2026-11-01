#include "cmdq.h"

void cmdq_init(cmdq_t *q)
{
  atomic_store_explicit(&q->head, 0, memory_order_relaxed);
  atomic_store_explicit(&q->tail, 0, memory_order_relaxed);
}

bool cmdq_push(cmdq_t *q, const cmd_t *c)
{
  uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
  uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
  if (head - tail >= CMDQ_CAPACITY) {
    return false;
  }
  q->buf[head & (CMDQ_CAPACITY - 1)] = *c;
  atomic_store_explicit(&q->head, head + 1, memory_order_release);
  return true;
}

bool cmdq_pop(cmdq_t *q, cmd_t *c)
{
  uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
  uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
  if (tail == head) {
    return false;
  }
  *c = q->buf[tail & (CMDQ_CAPACITY - 1)];
  atomic_store_explicit(&q->tail, tail + 1, memory_order_release);
  return true;
}
