#ifndef HIL_LINK_H
#define HIL_LINK_H

/* The controller's end of the HIL link, without the transport: what a platform port (hil_port.h)
 * runs on its serial line. A reader (a task or thread) feeds the received bytes; the control tick
 * takes the newest plant values before the controller and sends its commands after it.
 *
 * One reader and one tick, on any cores: the newest PLANT goes from the reader to the tick through
 * a seqlock (C11 atomics), so neither ever waits and no OS lock is needed. */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hil.h"
#include "hil_port.h"

/* Sends bytes on the line; must not block the tick for long (a TX buffer). */
typedef void (*hil_write_t)(void *ctx, const uint8_t *buf, size_t n);

typedef struct {
  hil_slots_t *slots;
  size_t n_plant, n_cmd;
  uint32_t hash;
  hil_write_t write;
  void *write_ctx;

  /* reader side */
  hil_parser_t parser;
  uint32_t wrong_size; /* PLANT frames with another value count, ignored */
  bool hello_mismatch; /* the runner's HELLO is for other values */
  /* reader -> tick: the newest PLANT (seqlock: odd while being written) */
  _Atomic uint32_t seq;
  _Atomic uint32_t latest[HIL_MAX_VALUES]; /* float bit patterns */
  _Atomic uint32_t latest_seq;             /* its frame seq */
  _Atomic uint32_t frames;                 /* PLANT frames so far */
  atomic_bool bye;      /* for hil_link_take_bye */
  atomic_bool bye_seen; /* since the last plant frame */

  /* tick side */
  uint32_t taken;   /* `frames` at the last take */
  uint32_t ended;   /* ticks without a plant frame since a BYE (0: none) */
  uint16_t cmd_seq; /* seq of the plant values in use: the CMD answers it */
} hil_link_t;

/* The names give the HELLO hash (hil_names_hash). */
void hil_link_init(hil_link_t *l, hil_slots_t *slots, const char *const *plant, size_t n_plant,
                   const char *const *cmd, size_t n_cmd, hil_write_t write, void *write_ctx);

/* Reader: one received byte. Answers HELLO (through write). Returns the type of a frame it
 * completed (HIL_HELLO, HIL_PLANT, HIL_BYE), else 0. */
int hil_link_feed(hil_link_t *l, uint8_t b);

/* Tick, before the controller: the newest plant values into the slots (age 0), or the last ones
 * one tick older when none came. After a BYE, one tick runs on (so the stop takes effect), then
 * age is HIL_AGE_NONE again: the controller waits for the next run, as at boot. */
void hil_link_tick_begin(hil_link_t *l);

/* Tick, after the controller: its commands as CMD, seq = that of the plant values it used. */
void hil_link_tick_end(hil_link_t *l);

/* True once after the runner said BYE. */
bool hil_link_take_bye(hil_link_t *l);

#endif
