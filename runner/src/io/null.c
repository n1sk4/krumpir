/* SIL backend: no device. Inputs read as 0 (the scenario's signals replace them in the loop),
 * outputs are discarded. */
#include <stdlib.h>
#include <string.h>

#include "io.h"

static int null_read(io_t *io, double t, double *in, size_t n)
{
  (void)io, (void)t;
  memset(in, 0, n * sizeof *in);
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
  (void)sc, (void)err, (void)errlen;
  io_t *io = calloc(1, sizeof *io);
  if (io != NULL) {
    io->api = &api;
  }
  return io;
}
