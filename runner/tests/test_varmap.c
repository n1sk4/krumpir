#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "modeldesc.h"
#include "varmap.h"

static const char MD_XML[] =
    "<fmiModelDescription fmiVersion=\"3.0\"><CoSimulation modelIdentifier=\"m\"/>"
    "<ModelVariables>"
    "<Float64 name=\"time\" valueReference=\"0\" causality=\"independent\"/>"
    "<Float64 name=\"u\" valueReference=\"11\" causality=\"input\"/>"
    "<Float64 name=\"v\" valueReference=\"12\" causality=\"input\"/>"
    "<Float64 name=\"y\" valueReference=\"21\" causality=\"output\"/>"
    "<Float64 name=\"z\" valueReference=\"22\" causality=\"output\"/>"
    "<Float64 name=\"k\" valueReference=\"31\" causality=\"parameter\"/>"
    "<Int32 name=\"n\" valueReference=\"32\" causality=\"parameter\"/>"
    "</ModelVariables></fmiModelDescription>";

/* Builds a scenario with the given io/params text on top of a fixed [model] and [log]. */
static void scenario(const char *params, const char *inputs, const char *outputs, scenario_t *sc) {
  char text[1024];
  snprintf(text, sizeof text,
           "[model]\nfmu = \"a.fmu\"\nstep = 0.001\nduration = 1\nparams = { %s }\n"
           "[io]\nbackend = \"null\"\ninputs = [%s]\noutputs = [%s]\n[log]\nfile = \"o.csv\"\n",
           params, inputs, outputs);
  char err[256];
  int rc = config_parse(text, sc, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "scenario error: %s\n", err);
  }
  assert(rc == 0);
}

static void expect_err(const modeldesc_t *md, const scenario_t *sc, const char *substr) {
  varmap_t vm;
  char err[256] = "";
  int rc = varmap_build(md, sc, &vm, err, sizeof err);
  if (rc == 0 || strstr(err, substr) == NULL) {
    fprintf(stderr, "expected error containing '%s', got rc=%d err='%s'\n", substr, rc, err);
  }
  assert(rc == -1);
  assert(strstr(err, substr) != NULL);
}

int main(void) {
  modeldesc_t md;
  char err[256];
  assert(modeldesc_parse(MD_XML, strlen(MD_XML), &md, err, sizeof err) == 0);

  scenario_t sc;
  varmap_t vm;

  /* Order follows the scenario, not the model. */
  scenario("k = 2.5", "\"v\", \"u\"", "\"z\", \"y\"", &sc);
  assert(varmap_build(&md, &sc, &vm, err, sizeof err) == 0);
  assert(vm.n_in == 2 && vm.in_vr[0] == 12 && vm.in_vr[1] == 11);
  assert(vm.n_out == 2 && vm.out_vr[0] == 22 && vm.out_vr[1] == 21);
  assert(vm.n_params == 1 && vm.param_vr[0] == 31 && vm.param_value[0] == 2.5);

  /* Nothing mapped is fine too. */
  scenario("", "", "", &sc);
  assert(varmap_build(&md, &sc, &vm, err, sizeof err) == 0);
  assert(vm.n_in == 0 && vm.n_out == 0 && vm.n_params == 0);

  scenario("", "\"nope\"", "\"y\"", &sc);
  expect_err(&md, &sc, "input 'nope' is not a variable");
  scenario("", "\"y\"", "\"y\"", &sc); /* an output used as an input */
  expect_err(&md, &sc, "input 'y' has causality 'output'");
  scenario("", "\"u\"", "\"u\"", &sc); /* an input used as an output */
  expect_err(&md, &sc, "output 'u' has causality 'input'");
  scenario("", "\"u\"", "\"time\"", &sc);
  expect_err(&md, &sc, "output 'time' has causality 'independent'");
  scenario("n = 1", "\"u\"", "\"y\"", &sc); /* Int32 parameter */
  expect_err(&md, &sc, "parameter 'n' is Int32");
  scenario("u = 1", "\"u\"", "\"y\"", &sc); /* an input used as a parameter */
  expect_err(&md, &sc, "parameter 'u' has causality 'input'");
  scenario("zzz = 1", "", "", &sc);
  expect_err(&md, &sc, "parameter 'zzz' is not a variable");

  modeldesc_free(&md);
  puts("test_varmap: ok");
  return 0;
}
