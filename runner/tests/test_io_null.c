#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "io.h"

static void parse(const char *text, scenario_t *sc) {
  char err[256];
  int rc = config_parse(text, sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "config: %s\n", err);
  }
  assert(rc == 0);
}

static void test_stimulus_values(void) {
  stimulus_t s = {0};
  s.kind = STIM_CONSTANT;
  s.value = 3.0;
  assert(stimulus_value(&s, 0.0) == 3.0 && stimulus_value(&s, 100.0) == 3.0);

  s = (stimulus_t){0};
  s.kind = STIM_STEP;
  s.t = 1.0;
  s.from = -1.0;
  s.to = 4.0;
  assert(stimulus_value(&s, 0.0) == -1.0);
  assert(stimulus_value(&s, 0.999999) == -1.0);
  assert(stimulus_value(&s, 1.0) == 4.0); /* the new value applies from t on */
  assert(stimulus_value(&s, 5.0) == 4.0);

  s = (stimulus_t){0};
  s.kind = STIM_SINE;
  s.offset = 1.0;
  s.amplitude = 2.0;
  s.freq_hz = 0.25;
  assert(fabs(stimulus_value(&s, 0.0) - 1.0) < 1e-12);
  assert(fabs(stimulus_value(&s, 1.0) - 3.0) < 1e-12); /* quarter period: sin = 1 */
  assert(fabs(stimulus_value(&s, 2.0) - 1.0) < 1e-12); /* half period */
  assert(fabs(stimulus_value(&s, 3.0) - -1.0) < 1e-12);
}

static void test_null_backend(void) {
  scenario_t sc;
  parse("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
        "[io]\nbackend = \"null\"\ninputs = [\"a\", \"b\", \"c\", \"d\"]\n"
        "stimulus = [\n"
        "  { input = \"c\", kind = \"sine\", offset = 1, amplitude = 2, freq_hz = 0.25 },\n"
        "  { input = \"a\", kind = \"constant\", value = 3 },\n"
        "  { input = \"b\", kind = \"step\", t = 1.0, from = -1, to = 4 },\n"
        "]\n[log]\nfile = \"o.csv\"\n",
        &sc);
  char err[256];
  io_t *io = io_open(&sc, err, sizeof err);
  assert(io != NULL);
  assert(strcmp(io->api->name, "null") == 0);

  /* Stimuli are matched to inputs by name, not by order; input d has none and stays at 0. */
  double in[4];
  assert(io->api->read(io, 0.0, in, 4) == 0);
  assert(in[0] == 3.0 && in[1] == -1.0 && fabs(in[2] - 1.0) < 1e-12 && in[3] == 0.0);
  assert(io->api->read(io, 1.0, in, 4) == 0);
  assert(in[0] == 3.0 && in[1] == 4.0 && fabs(in[2] - 3.0) < 1e-12 && in[3] == 0.0);

  /* Fewer inputs than the scenario declares is fine. */
  assert(io->api->read(io, 1.0, in, 2) == 0);

  double out[2] = {1.0, 2.0};
  assert(io->api->write(io, 1.0, out, 2) == 0);
  io->api->safe_state(io);
  io->api->close(io);
}

static void test_unknown_backend(void) {
  scenario_t sc;
  parse("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\n"
        "[io]\nbackend = \"null\"\n[log]\nfile = \"o.csv\"\n",
        &sc);
  strcpy(sc.backend, "uart");
  char err[256] = "";
  assert(io_open(&sc, err, sizeof err) == NULL);
  assert(strstr(err, "'uart' is not supported") != NULL);
}

int main(void) {
  test_stimulus_values();
  test_null_backend();
  test_unknown_backend();
  puts("test_io_null: ok");
  return 0;
}
