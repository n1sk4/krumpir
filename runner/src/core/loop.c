#include "loop.h"

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdbool.h>
#include <string.h>

#include "platform.h"
#include "timing_pin.h"

int64_t loop_step_ns(double step) { return (int64_t)llround(step * 1e9); }

/* Handles the queued commands at the start of step n. Never allocates or blocks; a full reply
 * queue drops the reply (the control thread then times out). */
static void handle_commands(const loop_ctx_t *c, uint64_t n, double t)
{
  cmd_t cmd;
  while (cmdq_pop(c->cmd, &cmd)) {
    cmd.status = 0;
    cmd.step = n;
    switch (cmd.kind) {
    case CMD_WRITE_INPUT:
    case CMD_SET_SIGNAL:
      if (c->src != NULL && cmd.index >= 0 && (size_t)cmd.index < c->vm->n_in) {
        input_src_t *src = &c->src[cmd.index];
        signal_t *old = src->kind == SRC_SIGNAL ? src->sig : NULL;
        if (cmd.kind == CMD_WRITE_INPUT) {
          *src = (input_src_t){.kind = SRC_VALUE, .value = cmd.value};
        } else {
          *src = (input_src_t){.kind = SRC_SIGNAL, .sig = cmd.sig, .t0 = t};
        }
        cmd.sig = old;
      } else {
        cmd.status = FMU_ERROR;
      }
      break;
    case CMD_WRITE_PARAM:
      cmd.status = (int)fmu_set_real(c->fmu, &cmd.vr, 1, &cmd.value);
      break;
    case CMD_READ:
      cmd.status = (int)fmu_get_real(c->fmu, &cmd.vr, 1, &cmd.value);
      break;
    }
    if (c->reply != NULL) {
      cmdq_push(c->reply, &cmd);
    }
  }
}

double loop_time(uint64_t n, double step) { return (double)n * step; }

/* Waits for the absolute time `target` and returns the time it woke up. A stop request ends the
 * wait early. */
static int64_t wait_until(wait_mode_t mode, int64_t target, const atomic_int *stop)
{
  if (mode == WAIT_SLEEP) {
    while (plat_sleep_until_ns(target) == EINTR) {
      if (*stop) {
        break;
      }
    }
  } else {
    while (plat_now_ns() < target) {
      if (*stop) {
        break;
      }
    }
  }
  return plat_now_ns();
}

void loop_run(const loop_ctx_t *c, loop_stats_t *st)
{
  const scenario_t *sc = c->sc;
  const varmap_t *vm = c->vm;
  const int64_t step_ns = loop_step_ns(sc->step);
  const bool paced = sc->wait != WAIT_NONE;
  double in[TELEM_MAX_CH] = {0};
  double out[TELEM_MAX_CH] = {0};
  telem_rec_t rec;

  st->end = LOOP_DONE; /* the caller has zeroed *st */

  /* The device first gets the outputs before step 0 (the initialized model), so the input of
   * every step can come from the plant before it, the first one too. */
  if (vm->n_out > 0) {
    const fmu_result_t r = fmu_get_real(c->fmu, vm->out_vr, vm->n_out, out);
    if (r != FMU_OK) {
      st->end = LOOP_FMU_FAIL;
      st->fail_result = r;
      return;
    }
    if (c->io->api->write(c->io, 0.0, out, vm->n_out) != 0) {
      st->end = LOOP_IO_FAIL;
      return;
    }
  }

  const int64_t start = plat_now_ns();
  for (uint64_t n = 0; n < sc->n_steps; n++) {
    if (*c->stop) {
      st->end = LOOP_STOPPED;
      return;
    }
    const int64_t target = start + (int64_t)n * step_ns;
    int64_t wake_ns = 0;
    if (paced) {
      wake_ns = wait_until(sc->wait, target, c->stop) - target;
      if (*c->stop) {
        st->end = LOOP_STOPPED;
        return;
      }
    }

    if (c->timing_fd > 0) {
      timing_pin_set(c->timing_fd, true);
    }
    const double t = loop_time(n, sc->step);
    if (c->cmd != NULL) {
      handle_commands(c, n, t);
    }

    if (c->io->api->read(c->io, t, in, vm->n_in) != 0) {
      st->end = LOOP_IO_FAIL;
      return;
    }
    if (c->src != NULL) {
      for (size_t i = 0; i < vm->n_in; i++) {
        const input_src_t *src = &c->src[i];
        if (src->kind == SRC_VALUE) {
          in[i] = src->value;
        } else if (src->kind == SRC_SIGNAL) {
          in[i] = signal_value(src->sig, t - src->t0);
        }
      }
    }
    fmu_result_t r = FMU_OK;
    if (vm->n_in > 0) {
      r = fmu_set_real(c->fmu, vm->in_vr, vm->n_in, in);
    }
    const int64_t t0 = plat_now_ns();
    if (r == FMU_OK) {
      r = fmu_step(c->fmu, t, sc->step);
    }
    const int64_t t1 = plat_now_ns();
    if (r == FMU_OK && vm->n_out > 0) {
      r = fmu_get_real(c->fmu, vm->out_vr, vm->n_out, out);
    }
    if (r != FMU_OK) {
      st->end = LOOP_FMU_FAIL;
      st->fail_step = n;
      st->fail_result = r;
      return;
    }
    if (c->io->api->write(c->io, t, out, vm->n_out) != 0) {
      st->end = LOOP_IO_FAIL;
      return;
    }

    const bool late = paced && plat_now_ns() > target + step_ns;
    memset(&rec, 0, sizeof rec);
    rec.n = n;
    rec.t = loop_time(n + 1, sc->step);
    memcpy(rec.in, in, vm->n_in * sizeof in[0]);
    memcpy(rec.out, out, vm->n_out * sizeof out[0]);
    rec.wake_ns = wake_ns;
    rec.step_ns = t1 - t0;
    rec.flags = late ? TELEM_FLAG_OVERRUN : 0;
    if (!paced) {
      /* Unpaced runs have no deadline, so wait for the writer rather than lose rows. */
      while (ring_full(c->ring) && !*c->stop) {
        sched_yield();
      }
    }
    ring_push(c->ring, &rec);
    if (c->timing_fd > 0) {
      timing_pin_set(c->timing_fd, false);
    }

    /* The loop is the only writer, so relaxed load + store is enough (no lock prefix on x86). */
    const memory_order rx = memory_order_relaxed;
    atomic_store_explicit(&st->steps, atomic_load_explicit(&st->steps, rx) + 1, rx);
    st->sum_step_ns += (double)rec.step_ns;
    if (rec.step_ns > atomic_load_explicit(&st->max_step_ns, rx)) {
      atomic_store_explicit(&st->max_step_ns, rec.step_ns, rx);
    }
    if (wake_ns > atomic_load_explicit(&st->max_wake_ns, rx)) {
      atomic_store_explicit(&st->max_wake_ns, wake_ns, rx);
    }
    if (late) {
      const uint64_t overruns = atomic_load_explicit(&st->overruns, rx) + 1;
      atomic_store_explicit(&st->overruns, overruns, rx);
      if (overruns > (uint64_t)sc->max_overruns) {
        st->end = LOOP_TOO_MANY_OVERRUNS;
        return;
      }
    }
  }
}
