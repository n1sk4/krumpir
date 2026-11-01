/* The t12tip FMU against t12_plant stepped directly, and against the independent numpy reference
 * (libs/test/test_t12_plant/golden.h): the same inputs must give bit-identical outputs at every
 * step, so the FMU adds nothing to the plant model. Also checks the faults, the series switch, the
 * start state, the parameter rules and that one 10 ms communication step equals ten 1 ms steps.
 * Links the FMU's sources statically (FMI 3.0, no function prefix). */
#include <math.h>
#include <stdio.h>

#include "fmi3Functions.h"
#include "golden.h"
#include "t12_plant.h"

#define TOKEN "{0d3c7e91-5a24-4b6f-8e1d-7c9a2f4b6e02}"
#define DT 0.001
#define AMB 25.0
enum {
  VR_HEATER = 1,
  VR_SERIES = 2,
  VR_AMBIENT = 3,
  VR_LOAD = 4,
  VR_FAULT = 5,
  VR_V_AMP = 6,
  VR_I = 7,
  VR_T_H = 8,
  VR_T_T = 9,
  VR_T_C = 10,
  VR_R_COIL = 19,
  VR_TAU = 25
};

static int failures;
#define CHECK(cond, ...)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                  \
      printf(__VA_ARGS__);                                                                         \
      printf("\n");                                                                                \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

static void logger(fmi3InstanceEnvironment env, fmi3Status status, fmi3String category,
                   fmi3String message)
{
  (void)env;
  (void)status;
  (void)category;
  (void)message; /* expected errors in the parameter tests; keep the output quiet */
}

static fmi3Instance start(void)
{
  fmi3Instance fmu = fmi3InstantiateCoSimulation("t12tip", TOKEN, NULL, fmi3False, fmi3False,
                                                 fmi3False, fmi3False, NULL, 0, NULL, logger,
                                                 NULL);
  CHECK(fmu != NULL, "instantiate");
  return fmu;
}

static void init(fmi3Instance fmu)
{
  CHECK(fmi3EnterInitializationMode(fmu, fmi3False, 0, 0.0, fmi3False, 0) == fmi3OK, "enter init");
  CHECK(fmi3ExitInitializationMode(fmu) == fmi3OK, "exit init");
}

static double get(fmi3Instance fmu, fmi3ValueReference vr)
{
  double v = NAN;
  fmi3GetFloat64(fmu, &vr, 1, &v, 1);
  return v;
}

static fmi3Status set(fmi3Instance fmu, fmi3ValueReference vr, double v)
{
  return fmi3SetFloat64(fmu, &vr, 1, &v, 1);
}

static fmi3Status step(fmi3Instance fmu, double t, double h)
{
  fmi3Boolean event, terminate, early;
  double last;
  return fmi3DoStep(fmu, t, h, fmi3True, &event, &terminate, &early, &last);
}

/* Every output of the FMU equals the plant's state. */
static int same(fmi3Instance fmu, const t12_plant_t *ref, int n)
{
  int before = failures;
  CHECK(get(fmu, VR_T_H) == ref->t_h, "step %d t_h %.17g, plant %.17g", n, get(fmu, VR_T_H),
        ref->t_h);
  CHECK(get(fmu, VR_T_T) == ref->t_t, "step %d t_t", n);
  CHECK(get(fmu, VR_T_C) == ref->t_c, "step %d t_c", n);
  CHECK(get(fmu, VR_V_AMP) == ref->v_amp, "step %d v_amp %.17g, plant %.17g", n,
        get(fmu, VR_V_AMP), ref->v_amp);
  CHECK(get(fmu, VR_I) == ref->i_heater, "step %d i_heater", n);
  return failures == before;
}

/* The golden scenario: 10 Hz heating cycles (90 ms on, 10 ms off) for 20 s, a 0.3 W/K joint from
 * 10 s. Bit-identical to t12_plant and within the plant's own tolerances of the numpy reference. */
static void test_golden_scenario(void)
{
  const t12_params_t p = T12_PARAMS_DEFAULT;
  t12_plant_t ref;
  t12_plant_init(&ref, &p, DT, AMB);
  fmi3Instance fmu = start();
  init(fmu);
  CHECK(same(fmu, &ref, -1), "start state");

  int next = 0;
  for (int k = 0; next < GOLDEN_N; k++) {
    if (k == GOLDEN_LOAD_STEP) {
      t12_params_t touching = p;
      touching.g_load = GOLDEN_LOAD_G;
      t12_plant_set_params(&ref, &touching, DT);
      set(fmu, VR_LOAD, GOLDEN_LOAD_G);
    }
    bool on = k % 100 < 90;
    set(fmu, VR_HEATER, on ? 1.0 : 0.0);
    CHECK(step(fmu, k * DT, DT) == fmi3OK, "step %d", k);
    t12_plant_step(&ref, on, AMB);
    if (!same(fmu, &ref, k))
      break;
    if (k + 1 == GOLDEN[next].step) {
      CHECK(fabs(get(fmu, VR_T_H) - GOLDEN[next].t_h) <= 1e-6, "golden row %d t_h", next);
      CHECK(fabs(get(fmu, VR_T_T) - GOLDEN[next].t_t) <= 1e-6, "golden row %d t_t", next);
      CHECK(fabs(get(fmu, VR_T_C) - GOLDEN[next].t_c) <= 1e-6, "golden row %d t_c", next);
      CHECK(fabs(get(fmu, VR_V_AMP) - GOLDEN[next].v_amp) <= 0.003, "golden row %d v_amp", next);
      next++;
    }
  }
  /* The joint from 10 s draws the tip down from above 300 to about 236 degC by 20 s */
  CHECK(get(fmu, VR_T_T) > 200.0, "the tip heated (%g degC)", get(fmu, VR_T_T));
  fmi3FreeInstance(fmu);
}

/* Faults and the series switch, each injected on a hot tip: the FMU follows the plant exactly. */
static void test_faults(void)
{
  for (int fault = T12_FAULT_NONE; fault <= T12_FAULT_STUCK_ON; fault++) {
    for (int series = 0; series <= 1; series++) {
      const t12_params_t p = T12_PARAMS_DEFAULT;
      t12_plant_t ref;
      t12_plant_init(&ref, &p, DT, AMB);
      fmi3Instance fmu = start();
      init(fmu);
      for (int k = 0; k < 3000; k++) {
        bool on = k % 100 < 50;
        if (k == 2000) {
          set(fmu, VR_FAULT, fault);
          set(fmu, VR_SERIES, series);
          ref.fault = (t12_fault_t)fault;
          ref.series_on = series;
        }
        set(fmu, VR_HEATER, on ? 1.0 : 0.0);
        step(fmu, k * DT, DT);
        t12_plant_step(&ref, on, AMB);
        if (!same(fmu, &ref, k))
          break;
      }
      if (!series)
        CHECK(get(fmu, VR_I) == 0.0, "series switch off blocks the heater (fault %d)", fault);
      fmi3FreeInstance(fmu);
    }
  }
}

/* The start state follows the ambient set before the simulation starts. */
static void test_start_state(void)
{
  fmi3Instance fmu = start();
  CHECK(set(fmu, VR_AMBIENT, 30.0) == fmi3OK, "ambient");
  init(fmu);
  CHECK(get(fmu, VR_T_T) == 30.0 && get(fmu, VR_T_C) == 30.0, "all nodes at ambient");
  fmi3FreeInstance(fmu);
}

/* Parameters before the start only and with physical values; fault codes 0..3 only. */
static void test_parameters(void)
{
  fmi3Instance fmu = start();
  CHECK(set(fmu, VR_TAU, 0.0) == fmi3Error, "tau_sense = 0 refused");
  fmi3FreeInstance(fmu);

  fmu = start();
  CHECK(set(fmu, VR_R_COIL, 6.0) == fmi3OK, "r_coil before init");
  init(fmu);
  set(fmu, VR_HEATER, 1.0);
  step(fmu, 0.0, DT);
  CHECK(get(fmu, VR_I) == 4.0, "24 V / 6 ohm = 4 A (%g)", get(fmu, VR_I));
  CHECK(set(fmu, VR_R_COIL, 8.0) == fmi3Error, "r_coil refused after init");
  CHECK(set(fmu, VR_FAULT, 1.5) == fmi3Error, "fault 1.5 refused");
  CHECK(set(fmu, VR_FAULT, 4.0) == fmi3Error, "fault 4 refused");
  CHECK(set(fmu, VR_LOAD, -0.1) == fmi3Error, "negative load refused");
  fmi3FreeInstance(fmu);
}

/* A 10 ms communication step runs ten plant steps. */
static void test_long_step(void)
{
  fmi3Instance a = start(), b = start();
  init(a);
  init(b);
  set(a, VR_HEATER, 1.0);
  set(b, VR_HEATER, 1.0);
  for (int n = 0; n < 100; n++) {
    CHECK(step(a, n * 0.01, 0.01) == fmi3OK, "long step %d", n);
    for (int k = 0; k < 10; k++)
      step(b, (n * 10 + k) * DT, DT);
  }
  CHECK(get(a, VR_T_T) == get(b, VR_T_T), "10 ms step %g, ten 1 ms steps %g", get(a, VR_T_T),
        get(b, VR_T_T));
  fmi3FreeInstance(a);
  fmi3FreeInstance(b);
}

int main(void)
{
  test_golden_scenario();
  test_faults();
  test_start_state();
  test_parameters();
  test_long_step();
  printf("%s\n", failures ? "FAIL" : "OK");
  return failures ? 1 : 0;
}
