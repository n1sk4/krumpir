#include "loop.h"

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdbool.h>
#include <string.h>

#include "platform.h"

int64_t loop_step_ns(double step) { return (int64_t)llround(step * 1e9); }

/* Handles the queued commands at the start of step n. Never allocates or blocks; a full reply
 * queue drops the reply (the control thread then times out). */
static void handle_commands(const loop_ctx_t *c, uint64_t n)
{
  cmd_t cmd;
  while (cmdq_pop(c->cmd, &cmd)) {
    cmd.status = 0;
    cmd.step = n;
    switch (cmd.kind) {
    case CMD_WRITE_INPUT:
      if (c->override_on != NULL && cmd.index >= 0 && (size_t)cmd.index < c->vm->n_in) {
        c->override_value[cmd.index] = cmd.value;
        c->override_on[cmd.index] = true;
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
static int64_t wait_until(wait_mode_t mode, int64_t target, const volatile sig_atomic_t *stop)
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

  memset(st, 0, sizeof *st);
  st->end = LOOP_DONE;

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

    if (c->cmd != NULL) {
      handle_commands(c, n);
    }

    const double t = loop_time(n, sc->step);
    if (c->io->api->read(c->io, t, in, vm->n_in) != 0) {
      st->end = LOOP_IO_FAIL;
      return;
    }
    if (c->override_on != NULL) {
      for (size_t i = 0; i < vm->n_in; i++) {
        if (c->override_on[i]) {
          in[i] = c->override_value[i];
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

    st->steps++;
    st->sum_step_ns += (double)rec.step_ns;
    if (rec.step_ns > st->max_step_ns) {
      st->max_step_ns = rec.step_ns;
    }
    if (wake_ns > st->max_wake_ns) {
      st->max_wake_ns = wake_ns;
    }
    if (late) {
      st->overruns++;
      if (st->overruns > (uint64_t)sc->max_overruns) {
        st->end = LOOP_TOO_MANY_OVERRUNS;
        return;
      }
    }
  }
}
