#ifdef KRUMPIR_IO_HIL /* the plant behind the HIL link: the PC build (host/) and esp32dev-hil */
#include "io_hil.h"

#include "t12_frontend.h"
#include "t12_hw.h"

enum { V_AMP, I_HEATER, T_HANDLE }; /* plant values */
enum { HEATER, SERIES_ON };         /* commands */

static bool lost(const t12_io_hil_t *h) { return h->slots.age > HIL_MAX_AGE; }

static void heater_set(void *ctx, bool on)
{
  ((t12_io_hil_t *)ctx)->slots.cmd[HEATER] = on ? 1.0f : 0.0f;
}

static uint16_t tc_read(void *ctx)
{
  const t12_io_hil_t *h = ctx;
  return lost(h) ? T12_ADC_MAX : t12_tc_counts(h->slots.plant[V_AMP]);
}

static uint16_t ntc_read(void *ctx)
{
  return t12_ntc_counts(((t12_io_hil_t *)ctx)->slots.plant[T_HANDLE]);
}

static uint16_t current_read(void *ctx)
{
  return t12_current_counts(((t12_io_hil_t *)ctx)->slots.plant[I_HEATER]);
}

static void heartbeat_toggle(void *ctx) { ((t12_io_hil_t *)ctx)->hb_age = 0; }

static void safe_state(void *ctx) { ((t12_io_hil_t *)ctx)->slots.cmd[HEATER] = 0.0f; }

void t12_io_hil_init(t12_io_hil_t *h, t12_io_t *io_out, double dt)
{
  h->slots.cmd[HEATER] = 0.0f;
  h->slots.cmd[SERIES_ON] = 0.0f;
  h->hb_age = 0;
  h->hb_timeout = (uint32_t)(T12_HEARTBEAT_TIMEOUT_S / dt + 0.5);
  io_out->heater_set = heater_set;
  io_out->tc_read = tc_read;
  io_out->ntc_read = ntc_read;
  io_out->current_read = current_read;
  io_out->heartbeat_toggle = heartbeat_toggle;
  io_out->safe_state = safe_state;
  io_out->ctx = h;
}

void t12_io_hil_step(t12_io_hil_t *h)
{
  h->slots.cmd[SERIES_ON] = h->hb_age <= h->hb_timeout ? 1.0f : 0.0f;
  if (h->hb_age <= h->hb_timeout) {
    h->hb_age++;
  }
}
#endif
