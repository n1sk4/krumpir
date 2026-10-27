#ifndef KRUMPIR_IO_H
#define KRUMPIR_IO_H

#include <stddef.h>

#include "config.h"

/* I/O backend: where the model's inputs come from and where its outputs go. Swapping it is the
 * SIL <-> HIL switch. The runner calls read() before each step and write() after it. */
typedef struct io io_t;

typedef struct io_api {
  const char *name;
  /* Fills in[0..n) with the model inputs for the step starting at time t. Returns 0 on success. */
  int (*read)(io_t *io, double t, double *in, size_t n);
  /* Sends out[0..n), the model outputs after the step, to the device. Returns 0 on success. */
  int (*write)(io_t *io, double t, const double *out, size_t n);
  void (*safe_state)(io_t *io); /* called once when the run faults */
  void (*close)(io_t *io);
} io_api_t;

struct io {
  const io_api_t *api;
};

/* Opens the backend named by sc->backend. Returns NULL and writes a message to err on failure. */
io_t *io_open(const scenario_t *sc, char *err, size_t errlen);

/* Value of a stimulus at time t (used by the null backend, exposed for tests). */
double stimulus_value(const stimulus_t *s, double t);

io_t *null_io_open(const scenario_t *sc, char *err, size_t errlen);

#endif
