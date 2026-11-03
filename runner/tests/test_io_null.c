#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "io.h"

static void parse(const char *text, scenario_t *sc)
{
  char err[256];
  int rc = config_parse(text, sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "config: %s\n", err);
  }
  assert(rc == 0);
}

static void test_null_backend(void)
{
  scenario_t sc;
  parse("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
        "[io]\nbackend = \"null\"\ninputs = [\"a\", \"b\"]\n"
        "stimulus = [{ input = \"a\", kind = \"constant\", value = 3 }]\n"
        "[log]\nfile = \"o.csv\"\n",
        &sc);
  char err[256];
  io_t *io = io_open(&sc, err, sizeof err);
  assert(io != NULL);
  assert(strcmp(io->api->name, "null") == 0);

  /* No device: every input reads 0 (stimuli are played by the loop, not the backend). */
  double in[2] = {7.0, 7.0};
  assert(io->api->read(io, 1.0, in, 2) == 0);
  assert(in[0] == 0.0 && in[1] == 0.0);

  double out[2] = {1.0, 2.0};
  assert(io->api->write(io, 1.0, out, 2) == 0);
  io->api->safe_state(io);
  io->api->close(io);
}

static void test_unknown_backend(void)
{
  scenario_t sc;
  parse("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
        "[io]\nbackend = \"null\"\n[log]\nfile = \"o.csv\"\n",
        &sc);
  strcpy(sc.backend, "can");
  char err[256] = "";
  assert(io_open(&sc, err, sizeof err) == NULL);
  assert(strstr(err, "'can' is not supported") != NULL);
}

int main(void)
{
  test_null_backend();
  test_unknown_backend();
  puts("test_io_null: ok");
  return 0;
}
