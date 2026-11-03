#include "hil_link.h"

#include <string.h>


void hil_link_init(hil_link_t *l, hil_slots_t *slots, const char *const *plant, size_t n_plant,
                   const char *const *cmd, size_t n_cmd, hil_write_t write, void *write_ctx)
{
  memset(l, 0, sizeof *l);
  l->slots = slots;
  l->n_plant = n_plant;
  l->n_cmd = n_cmd;
  l->hash = hil_names_hash(plant, n_plant, cmd, n_cmd);
  l->write = write;
  l->write_ctx = write_ctx;
  hil_parser_init(&l->parser);
  slots->age = HIL_AGE_NONE;
}

static void send(hil_link_t *l, const hil_frame_t *f)
{
  uint8_t buf[HIL_FRAME_MAX];
  l->write(l->write_ctx, buf, hil_encode(f, buf));
}

int hil_link_feed(hil_link_t *l, uint8_t b)
{
  hil_frame_t f;
  if (!hil_parse_byte(&l->parser, b, &f)) {
    return 0;
  }
  if (f.type == HIL_HELLO) {
    l->hello_mismatch = !hil_hello_matches(&f, l->n_plant, l->n_cmd, l->hash);
    hil_frame_t h;
    hil_hello(&h, l->n_plant, l->n_cmd, l->hash);
    send(l, &h);
  } else if (f.type == HIL_PLANT) {
    if (f.n != l->n_plant) {
      l->wrong_size++;
      return 0;
    }
    const uint32_t s = atomic_load_explicit(&l->seq, memory_order_relaxed);
    atomic_store_explicit(&l->seq, s + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    for (size_t i = 0; i < l->n_plant; i++) {
      uint32_t u;
      memcpy(&u, &f.v[i], 4);
      atomic_store_explicit(&l->latest[i], u, memory_order_relaxed);
    }
    atomic_store_explicit(&l->latest_seq, f.seq, memory_order_relaxed);
    atomic_store_explicit(&l->seq, s + 2, memory_order_release);
    atomic_fetch_add_explicit(&l->frames, 1, memory_order_release);
  } else if (f.type == HIL_BYE) {
    atomic_store(&l->bye, true);
    atomic_store(&l->bye_seen, true);
  } else {
    return 0;
  }
  return f.type;
}

void hil_link_tick_begin(hil_link_t *l)
{
  const uint32_t frames = atomic_load_explicit(&l->frames, memory_order_acquire);
  if (frames == l->taken) {
    if (atomic_load(&l->bye_seen) && l->ended++ > 0) {
      l->slots->age = HIL_AGE_NONE; /* the run is over: wait for the next one */
    } else if (l->slots->age < HIL_AGE_NONE) {
      l->slots->age++;
    }
    return;
  }
  atomic_store(&l->bye_seen, false);
  l->ended = 0;
  uint32_t s1, s2, seq;
  float v[HIL_MAX_VALUES];
  do {
    s1 = atomic_load_explicit(&l->seq, memory_order_acquire);
    for (size_t i = 0; i < l->n_plant; i++) {
      const uint32_t u = atomic_load_explicit(&l->latest[i], memory_order_relaxed);
      memcpy(&v[i], &u, 4);
    }
    seq = atomic_load_explicit(&l->latest_seq, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    s2 = atomic_load_explicit(&l->seq, memory_order_relaxed);
  } while ((s1 & 1u) != 0 || s1 != s2);
  memcpy(l->slots->plant, v, l->n_plant * sizeof v[0]);
  l->slots->age = 0;
  l->cmd_seq = (uint16_t)seq;
  l->taken = frames;
}

void hil_link_tick_end(hil_link_t *l)
{
  hil_frame_t f = {HIL_CMD, l->cmd_seq, (uint8_t)l->n_cmd, {0}};
  memcpy(f.v, l->slots->cmd, l->n_cmd * sizeof f.v[0]);
  send(l, &f);
}

bool hil_link_take_bye(hil_link_t *l) { return atomic_exchange(&l->bye, false); }
