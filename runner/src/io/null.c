/* SIL backend: inputs are generated from the scenario's stimuli, outputs are discarded. */
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"

#define TWO_PI 6.283185307179586476925286766559

typedef struct {
  io_t base; /* must be first */
  stimulus_t stim[TELEM_MAX_CH];
  bool has_stim[TELEM_MAX_CH];
} null_io_t;

double stimulus_value(const stimulus_t *s, double t)
{
  switch (s->kind) {
  case STIM_CONSTANT:
    return s->value;
  case STIM_STEP:
    return t < s->t ? s->from : s->to;
  case STIM_SINE:
    return s->offset + s->amplitude * sin(TWO_PI * s->freq_hz * t);
  }
  return 0.0;
}

static int null_read(io_t *io, double t, double *in, size_t n)
{
  null_io_t *s = (null_io_t *)io;
  for (size_t i = 0; i < n; i++) {
    in[i] = s->has_stim[i] ? stimulus_value(&s->stim[i], t) : 0.0;
  }
  return 0;
}

static int null_write(io_t *io, double t, const double *out, size_t n)
{
  (void)io, (void)t, (void)out, (void)n;
  return 0;
}

static void null_safe_state(io_t *io) { (void)io; }

static void null_close(io_t *io) { free(io); }

static const io_api_t api = {"null", null_read, null_write, null_safe_state, null_close};

io_t *null_io_open(const scenario_t *sc, char *err, size_t errlen)
{
  (void)err, (void)errlen;
  null_io_t *s = calloc(1, sizeof *s);
  if (s == NULL) {
    return NULL;
  }
  s->base.api = &api;
  /* Match each stimulus to its input by name; inputs without one stay at 0. */
  for (size_t i = 0; i < sc->n_inputs; i++) {
    for (size_t j = 0; j < sc->n_stimuli; j++) {
      if (strcmp(sc->stimuli[j].input, sc->inputs[i]) == 0) {
        s->stim[i] = sc->stimuli[j];
        s->has_stim[i] = true;
      }
    }
  }
  return &s->base;
}
