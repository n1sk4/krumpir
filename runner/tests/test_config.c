#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#define MODEL "[model]\nfmu = \"fmus/a.fmu\"\nstep = 0.001\nduration = 1\n"
#define IO "[io]\nbackend = \"null\"\ninputs = [\"u\"]\noutputs = [\"y\"]\n"
#define LOG "[log]\nfile = \"out.csv\"\n"

static void expect_ok(const char *text, scenario_t *sc)
{
  char err[256] = "";
  int rc = config_parse(text, sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "unexpected error: %s\n", err);
  }
  assert(rc == 0);
}

static void expect_err(const char *text, const char *substr)
{
  scenario_t sc;
  char err[256] = "";
  int rc = config_parse(text, &sc, err, sizeof err);
  if (rc == 0 || strstr(err, substr) == NULL) {
    fprintf(stderr, "expected error containing '%s', got rc=%d err='%s'\n", substr, rc, err);
  }
  assert(rc == -1);
  assert(strstr(err, substr) != NULL);
}

static void test_minimal_defaults(void)
{
  scenario_t sc;
  expect_ok(MODEL IO LOG, &sc);
  assert(strcmp(sc.fmu, "fmus/a.fmu") == 0);
  assert(sc.step == 0.001 && sc.duration == 1.0 && sc.n_steps == 1000);
  assert(sc.n_params == 0 && sc.n_stimuli == 0);
  assert(sc.wait == WAIT_NONE && sc.max_overruns == 0 && sc.cpu == -1 && sc.priority == -1);
  assert(strcmp(sc.backend, "null") == 0);
  assert(sc.n_inputs == 1 && strcmp(sc.inputs[0], "u") == 0);
  assert(sc.n_outputs == 1 && strcmp(sc.outputs[0], "y") == 0);
  assert(strcmp(sc.log_file, "out.csv") == 0);
}

static void test_full(void)
{
  scenario_t sc;
  expect_ok("[model]\nfmu = \"fmus/b.fmu\"\nstep = 0.001\nduration = 3600\n"
            "params = { k = 1.5, n = 2 }\n"
            "[rt]\nwait = \"sleep\"\nmax_overruns = 3\ncpu = 3\npriority = 80\n"
            "[io]\nbackend = \"null\"\ninputs = [\"u\", \"v\", \"w\"]\noutputs = [\"y\", \"z\"]\n"
            "stimulus = [\n"
            "  { input = \"u\", kind = \"step\", t = 1.0, from = 0, to = 2.5 },\n"
            "  { input = \"v\", kind = \"constant\", value = -1 },\n"
            "  { input = \"w\", kind = \"sine\", amplitude = 2, freq_hz = 5 },\n"
            "]\n" LOG,
            &sc);
  assert(sc.n_steps == 3600000);
  assert(sc.n_params == 2);
  assert(strcmp(sc.params[0].name, "k") == 0 && sc.params[0].value == 1.5);
  assert(strcmp(sc.params[1].name, "n") == 0 && sc.params[1].value == 2.0); /* int accepted */
  assert(sc.wait == WAIT_SLEEP && sc.max_overruns == 3 && sc.cpu == 3 && sc.priority == 80);
  assert(sc.n_inputs == 3 && sc.n_outputs == 2);
  assert(sc.n_stimuli == 3);
  assert(sc.stimuli[0].kind == STIM_STEP && sc.stimuli[0].t == 1.0 && sc.stimuli[0].from == 0.0 &&
         sc.stimuli[0].to == 2.5);
  assert(sc.stimuli[1].kind == STIM_CONSTANT && sc.stimuli[1].value == -1.0);
  assert(sc.stimuli[2].kind == STIM_SINE && sc.stimuli[2].offset == 0.0 &&
         sc.stimuli[2].amplitude == 2.0 && sc.stimuli[2].freq_hz == 5.0);
}

static void test_wait_modes(void)
{
  scenario_t sc;
  expect_ok(MODEL "[rt]\nwait = \"busy\"\n" IO LOG, &sc);
  assert(sc.wait == WAIT_BUSY);
  expect_ok(MODEL "[rt]\nwait = \"none\"\n" IO LOG, &sc);
  assert(sc.wait == WAIT_NONE);
}

static void test_missing_pieces(void)
{
  expect_err(IO LOG, "missing [model]");
  expect_err(MODEL LOG, "missing [io]");
  expect_err(MODEL IO, "missing [log]");
  expect_err("[model]\nstep = 0.001\nduration = 1\n" IO LOG, "missing model.fmu");
  expect_err("[model]\nfmu = \"a.fmu\"\nduration = 1\n" IO LOG, "missing model.step");
  expect_err(MODEL "[io]\ninputs = []\n" LOG, "missing io.backend");
  expect_err(MODEL IO "[log]\n", "missing log.file");
}

static void test_bad_values(void)
{
  expect_err("[model]\nfmu = \"a.fmu\"\nstep = 0\nduration = 1\n" IO LOG, "model.step");
  expect_err("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 0.0005\n" IO LOG,
             "at least one step");
  expect_err("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1.0005\n" IO LOG,
             "whole number of steps");
  expect_err("[model]\nfmu = 3\nstep = 0.001\nduration = 1\n" IO LOG, "model.fmu must be a string");
  expect_err("[model]\nfmu = \"a.fmu\"\nstep = \"x\"\nduration = 1\n" IO LOG,
             "model.step must be a number");
  expect_err(
      "[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\nparams = { k = \"x\" }\n" IO LOG,
      "must be a number");
  expect_err(MODEL "[rt]\nwait = \"fast\"\n" IO LOG, "rt.wait");
  expect_err(MODEL "[rt]\nmax_overruns = -1\n" IO LOG, "rt.max_overruns must be between");
  expect_err(MODEL "[rt]\npriority = 100\n" IO LOG, "rt.priority must be between");
  expect_err(MODEL "[io]\nbackend = \"uart\"\n" LOG, "not supported");
  expect_err(MODEL "[io]\nbackend = \"null\"\ninputs = [\"u\", \"u\"]\n" LOG, "duplicated");
  expect_err(MODEL "[io]\nbackend = \"null\"\ninputs = [1]\n" LOG, "only strings");
}

static void test_too_many_channels(void)
{
  char text[2048] = MODEL "[io]\nbackend = \"null\"\ninputs = [";
  for (int i = 0; i < 17; i++) {
    char one[16];
    snprintf(one, sizeof one, "%s\"in%d\"", i ? "," : "", i);
    strcat(text, one);
  }
  strcat(text, "]\n" LOG);
  expect_err(text, "more than 16");
}

static void test_stimulus_errors(void)
{
  expect_err(MODEL IO "stimulus = [{ input = \"nope\", kind = \"constant\", value = 1 }]\n" LOG,
             "not listed in io.inputs");
  expect_err(MODEL IO "stimulus = [{ input = \"u\", kind = \"ramp\" }]\n" LOG, "is not constant");
  expect_err(MODEL IO "stimulus = [{ input = \"u\", kind = \"constant\" }]\n" LOG,
             "missing io.stimulus.value");
  expect_err(MODEL IO
             "stimulus = [{ input = \"u\", kind = \"sine\", amplitude = 1, freq_hz = 0 }]\n" LOG,
             "freq_hz must be > 0");
  expect_err(MODEL IO
             "stimulus = [{ input = \"u\", kind = \"step\", t = -1, from = 0, to = 1 }]\n" LOG,
             "step time");
  expect_err(MODEL IO "stimulus = [{ input = \"u\", kind = \"constant\", value = 1 },"
                      "{ input = \"u\", kind = \"constant\", value = 2 }]\n" LOG,
             "two entries");
  expect_err(MODEL IO
             "stimulus = [{ input = \"u\", kind = \"constant\", value = 1, bogus = 2 }]\n" LOG,
             "unknown key 'bogus' in io.stimulus");
  expect_err(MODEL IO "stimulus = 3\n" LOG, "array of tables");
}

static void test_unknown_keys_and_bad_toml(void)
{
  expect_err("bogus = 1\n" MODEL IO LOG, "unknown key 'bogus' in the top level");
  expect_err("[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\nfoo = 1\n" IO LOG,
             "unknown key 'foo' in [model]");
  expect_err(MODEL "[rt]\nfoo = 1\n" IO LOG, "unknown key 'foo' in [rt]");
  expect_err(MODEL "[io]\nbackend = \"null\"\nfoo = 1\n" LOG, "unknown key 'foo' in [io]");
  expect_err(MODEL IO "[log]\nfile = \"o.csv\"\nfoo = 1\n", "unknown key 'foo' in [log]");
  expect_err("[model\n", "invalid TOML");
  expect_err("", "missing [model]");
}

static void test_load(void)
{
  scenario_t sc;
  char err[256] = "";
  assert(config_load("/nonexistent/scenario.toml", &sc, err, sizeof err) == -1);
  assert(strstr(err, "cannot open") != NULL);

  FILE *fp = fopen("test_config_tmp.toml", "w");
  assert(fp != NULL);
  fputs(MODEL IO LOG, fp);
  fclose(fp);
  assert(config_load("test_config_tmp.toml", &sc, err, sizeof err) == 0);
  assert(sc.n_steps == 1000);
  remove("test_config_tmp.toml");
}

int main(void)
{
  test_minimal_defaults();
  test_full();
  test_wait_modes();
  test_missing_pieces();
  test_bad_values();
  test_too_many_channels();
  test_stimulus_errors();
  test_unknown_keys_and_bad_toml();
  test_load();
  puts("test_config: ok");
  return 0;
}
