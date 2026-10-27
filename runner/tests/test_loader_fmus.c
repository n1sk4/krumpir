/* Opens the real Reference FMUs from runner/fmus (see fmus/README.md). Skipped (exit 77) if they
 * are not installed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "loader.h"
#include "varmap.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

#define SKIP 77

static bool dir_exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void open_ok(const char *name, fmu_pkg_t *pkg) {
  char path[512], err[512] = "";
  snprintf(path, sizeof path, "%s/%s.fmu", KRUMPIR_FMU_DIR, name);
  int rc = fmu_open(path, pkg, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "fmu_open(%s): %s\n", path, err);
  }
  assert(rc == 0);
  assert(strcmp(pkg->md.fmi_version, "3.0") == 0);
  assert(strcmp(pkg->md.model_identifier, name) == 0);
  assert(dir_exists(pkg->dir));
  assert(fmu_sym(pkg, "fmi3InstantiateCoSimulation") != NULL);
  assert(fmu_sym(pkg, "fmi3DoStep") != NULL);
  assert(fmu_sym(pkg, "fmi3GetFloat64") != NULL);
  assert(fmu_sym(pkg, "no_such_symbol") == NULL);
}

static void expect_var(const fmu_pkg_t *pkg, const char *name, causality_t caus) {
  const mvar_t *v = modeldesc_find(&pkg->md, name);
  assert(v != NULL);
  assert(mvar_is_float64(v));
  assert(v->causality == caus);
}

static void close_and_check(fmu_pkg_t *pkg) {
  char dir[512];
  snprintf(dir, sizeof dir, "%s", pkg->dir);
  fmu_close(pkg);
  assert(!dir_exists(dir)); /* temp directory deleted */
  assert(pkg->lib == NULL && pkg->md.vars == NULL);
  fmu_close(pkg); /* closing twice is harmless */
}

int main(void) {
  char probe[512];
  snprintf(probe, sizeof probe, "%s/Dahlquist.fmu", KRUMPIR_FMU_DIR);
  if (access(probe, R_OK) != 0) {
    fprintf(stderr, "SKIP: %s not found, see runner/fmus/README.md\n", probe);
    return SKIP;
  }

  fmu_pkg_t pkg;

  open_ok("Dahlquist", &pkg);
  expect_var(&pkg, "x", CAUS_OUTPUT);
  expect_var(&pkg, "k", CAUS_PARAMETER);
  close_and_check(&pkg);

  open_ok("VanDerPol", &pkg);
  expect_var(&pkg, "x0", CAUS_OUTPUT);
  expect_var(&pkg, "x1", CAUS_OUTPUT);
  expect_var(&pkg, "mu", CAUS_PARAMETER);
  close_and_check(&pkg);

  open_ok("Feedthrough", &pkg);
  expect_var(&pkg, "Float64_continuous_input", CAUS_INPUT);
  expect_var(&pkg, "Float64_continuous_output", CAUS_OUTPUT);
  expect_var(&pkg, "Float64_tunable_parameter", CAUS_PARAMETER);
  const mvar_t *i32 = modeldesc_find(&pkg.md, "Int32_input");
  assert(i32 != NULL && !mvar_is_float64(i32));

  /* The scenario names map onto the real model description. */
  scenario_t sc;
  char err[256];
  const char *text =
      "[model]\nfmu = \"Feedthrough.fmu\"\nstep = 0.001\nduration = 1\n"
      "params = { Float64_tunable_parameter = 3 }\n"
      "[io]\nbackend = \"null\"\ninputs = [\"Float64_continuous_input\"]\n"
      "outputs = [\"Float64_continuous_output\"]\n[log]\nfile = \"o.csv\"\n";
  assert(config_parse(text, &sc, err, sizeof err) == 0);
  varmap_t vm;
  assert(varmap_build(&pkg.md, &sc, &vm, err, sizeof err) == 0);
  assert(vm.n_in == 1 && vm.n_out == 1 && vm.n_params == 1 && vm.param_value[0] == 3.0);
  close_and_check(&pkg);

  puts("test_loader_fmus: ok");
  return 0;
}
