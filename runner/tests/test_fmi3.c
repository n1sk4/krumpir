/* FMI 3.0 backend against the real Reference FMUs (runner/fmus/README.md). Skipped (exit 77) if
 * they are not installed. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fmu.h"

#ifndef KRUMPIR_FMU_DIR
#error "KRUMPIR_FMU_DIR must be defined by the build"
#endif

#define SKIP 77
#define H 0.001

static void open_pkg(const char *name, fmu_pkg_t *pkg) {
  char path[512], err[512] = "";
  snprintf(path, sizeof path, "%s/%s.fmu", KRUMPIR_FMU_DIR, name);
  int rc = fmu_open(path, pkg, err, sizeof err);
  if (rc != 0) {
    fprintf(stderr, "fmu_open(%s): %s\n", path, err);
  }
  assert(rc == 0);
}

static fmu_t *create(const fmu_pkg_t *pkg) {
  char err[512] = "";
  fmu_t *m = fmi3_create(pkg, err, sizeof err);
  if (m == NULL) {
    fprintf(stderr, "fmi3_create: %s\n", err);
  }
  assert(m != NULL);
  return m;
}

static uint32_t vr_of(const fmu_pkg_t *pkg, const char *name) {
  const mvar_t *v = modeldesc_find(&pkg->md, name);
  assert(v != NULL);
  return v->vr;
}

/* Dahlquist: dx/dt = -k x, x(0) = 1. Runs n steps of H and returns x, checking it never grows. */
static double run_dahlquist(const fmu_pkg_t *pkg, double k, int n) {
  uint32_t vr_x = vr_of(pkg, "x"), vr_k = vr_of(pkg, "k");
  fmu_t *m = create(pkg);

  assert(fmu_set_real(m, &vr_k, 1, &k) == FMU_OK); /* parameter, before initialization */
  double back = 0.0;
  assert(fmu_get_real(m, &vr_k, 1, &back) == FMU_OK && back == k);
  assert(fmu_init(m, 0.0) == FMU_OK);

  double x = 0.0, prev = 0.0;
  assert(fmu_get_real(m, &vr_x, 1, &x) == FMU_OK);
  assert(x == 1.0);
  prev = x;
  for (int i = 0; i < n; i++) {
    fmu_result_t r = fmu_step(m, i * H, H);
    if (r != FMU_OK) {
      fprintf(stderr, "step %d: %s (%s)\n", i, fmu_result_str(r), fmu_last_message(m));
    }
    assert(r == FMU_OK);
    assert(fmu_get_real(m, &vr_x, 1, &x) == FMU_OK);
    assert(x <= prev && x > 0.0);
    prev = x;
  }
  fmu_destroy(m);
  return x;
}

static void test_dahlquist(void) {
  fmu_pkg_t pkg;
  open_pkg("Dahlquist", &pkg);
  double x1 = run_dahlquist(&pkg, 1.0, 1000); /* t = 1 s */
  double x2 = run_dahlquist(&pkg, 1.0, 1000);
  assert(x1 == x2); /* a fresh instance repeats the run exactly */
  fprintf(stderr, "dahlquist x(1) = %.9g (exp(-1) = %.9g)\n", x1, exp(-1.0));
  assert(fabs(x1 - exp(-1.0)) < 0.03); /* the FMU's own fixed-step solver: close, not exact */
  fmu_close(&pkg);
}

static void test_feedthrough(void) {
  fmu_pkg_t pkg;
  open_pkg("Feedthrough", &pkg);
  uint32_t vr_in = vr_of(&pkg, "Float64_continuous_input");
  uint32_t vr_out = vr_of(&pkg, "Float64_continuous_output");
  uint32_t vr_par = vr_of(&pkg, "Float64_tunable_parameter");
  fmu_t *m = create(&pkg);

  double par = 7.0, v = 0.0;
  assert(fmu_set_real(m, &vr_par, 1, &par) == FMU_OK);
  assert(fmu_init(m, 0.0) == FMU_OK);
  assert(fmu_get_real(m, &vr_par, 1, &v) == FMU_OK && v == 7.0);

  for (int i = 0; i < 5; i++) {
    double u = 3.5 + i;
    assert(fmu_set_real(m, &vr_in, 1, &u) == FMU_OK);
    assert(fmu_step(m, i * H, H) == FMU_OK);
    assert(fmu_get_real(m, &vr_out, 1, &v) == FMU_OK);
    assert(v == u); /* the output follows the input */
  }

  /* An unknown value reference is an error and the FMU says why. */
  uint32_t bad = 999999;
  double zero = 0.0;
  assert(fmu_set_real(m, &bad, 1, &zero) != FMU_OK);
  assert(fmu_last_message(m)[0] != '\0');
  fprintf(stderr, "feedthrough bad vr message: %s\n", fmu_last_message(m));

  /* Two values in one call. */
  uint32_t two_vr[2] = {vr_in, vr_par};
  double two_v[2] = {1.25, 9.0}, got[2] = {0, 0};
  assert(fmu_set_real(m, two_vr, 2, two_v) == FMU_OK);
  assert(fmu_get_real(m, two_vr, 2, got) == FMU_OK);
  assert(got[0] == 1.25 && got[1] == 9.0);

  fmu_destroy(m);
  fmu_close(&pkg);
}

static void test_step_before_init(void) {
  fmu_pkg_t pkg;
  open_pkg("Feedthrough", &pkg);
  fmu_t *m = create(&pkg);
  fmu_result_t r = fmu_step(m, 0.0, H); /* instance is not in step mode yet */
  fprintf(stderr, "step before init: %s (%s)\n", fmu_result_str(r), fmu_last_message(m));
  assert(r != FMU_OK);
  fmu_destroy(m); /* must not terminate an instance that never reached step mode */
  fmu_close(&pkg);
}

static void test_vanderpol(void) {
  fmu_pkg_t pkg;
  open_pkg("VanDerPol", &pkg);
  uint32_t vr[2] = {vr_of(&pkg, "x0"), vr_of(&pkg, "x1")};
  fmu_t *m = create(&pkg);
  assert(fmu_init(m, 0.0) == FMU_OK);
  double x[2];
  assert(fmu_get_real(m, vr, 2, x) == FMU_OK);
  assert(x[0] == 2.0 && x[1] == 0.0);
  for (int i = 0; i < 2000; i++) {
    assert(fmu_step(m, i * H, H) == FMU_OK);
  }
  assert(fmu_get_real(m, vr, 2, x) == FMU_OK);
  assert(isfinite(x[0]) && isfinite(x[1]) && fabs(x[0]) < 3.0);
  fmu_destroy(m);
  fmu_close(&pkg);
}

static void test_missing_token(void) {
  fmu_pkg_t pkg;
  open_pkg("Dahlquist", &pkg);
  pkg.md.instantiation_token[0] = '\0';
  char err[256] = "";
  assert(fmi3_create(&pkg, err, sizeof err) == NULL);
  assert(strstr(err, "instantiationToken") != NULL);
  fmu_close(&pkg);
}

int main(void) {
  char probe[512];
  snprintf(probe, sizeof probe, "%s/Dahlquist.fmu", KRUMPIR_FMU_DIR);
  if (access(probe, R_OK) != 0) {
    fprintf(stderr, "SKIP: %s not found, see runner/fmus/README.md\n", probe);
    return SKIP;
  }
  test_dahlquist();
  test_feedthrough();
  test_step_before_init();
  test_vanderpol();
  test_missing_token();
  puts("test_fmi3: ok");
  return 0;
}
