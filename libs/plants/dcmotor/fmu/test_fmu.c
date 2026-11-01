/* The dcmotor FMU against plant_sim stepped directly: the same inputs must give bit-identical
 * outputs at every step, so the FMU adds nothing to the plant model. Also checks the parameter
 * rules and that one 10 ms communication step equals ten 1 ms steps. Links the FMU's sources
 * statically (FMI 3.0, no function prefix). */
#include <math.h>
#include <stdio.h>

#include "fmi3Functions.h"
#include "plant_sim.h"

#define TOKEN "{6b8f1a52-3c1e-4d7a-9f0b-2a5e7c4d1e01}"
#define DT 0.001
#define PI 3.14159265358979323846
enum { VR_DUTY = 1, VR_LOAD = 2, VR_SPEED = 3, VR_RPM = 4, VR_CURRENT = 5, VR_L = 7, VR_VBUS = 12 };

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
  fmi3Instance fmu = fmi3InstantiateCoSimulation("dcmotor", TOKEN, NULL, fmi3False, fmi3False,
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

/* Duty step, load step and an out-of-range duty (clamped): 1 s, against plant_sim. */
static void test_matches_plant(void)
{
  const plant_params_t p = PLANT_PARAMS_N20;
  plant_sim_t ref;
  plant_sim_init(&ref, &p, (float)DT);

  fmi3Instance fmu = start();
  init(fmu);
  for (int n = 0; n < 1000; n++) {
    double duty = n < 100 ? 0.0 : n < 800 ? 0.5 : 1.5;
    double load = n < 500 ? 0.0 : 2e-4;
    set(fmu, VR_DUTY, duty);
    set(fmu, VR_LOAD, load);
    CHECK(step(fmu, n * DT, DT) == fmi3OK, "step %d", n);

    float d = duty > 1.0 ? 1.0f : (float)duty;
    plant_sim_step(&ref, d * p.Vbus, (float)load);
    CHECK(get(fmu, VR_SPEED) == (double)ref.w, "step %d speed %g, plant %g", n, get(fmu, VR_SPEED),
          (double)ref.w);
    CHECK(get(fmu, VR_CURRENT) == (double)ref.i, "step %d current %g, plant %g", n,
          get(fmu, VR_CURRENT), (double)ref.i);
    if (failures)
      break;
  }
  CHECK(fabs(get(fmu, VR_RPM) - (double)ref.w * 60.0 / (2.0 * PI)) < 1e-9, "rpm");
  CHECK(get(fmu, VR_SPEED) > 100.0, "the motor turned (%g rad/s)", get(fmu, VR_SPEED));
  fmi3FreeInstance(fmu);
}

/* A 10 ms communication step runs ten plant steps. */
static void test_long_step(void)
{
  fmi3Instance a = start(), b = start();
  init(a);
  init(b);
  set(a, VR_DUTY, 0.7);
  set(b, VR_DUTY, 0.7);
  for (int n = 0; n < 50; n++) {
    CHECK(step(a, n * 0.01, 0.01) == fmi3OK, "long step %d", n);
    for (int k = 0; k < 10; k++)
      step(b, (n * 10 + k) * DT, DT);
  }
  CHECK(get(a, VR_SPEED) == get(b, VR_SPEED), "10 ms step %g, ten 1 ms steps %g",
        get(a, VR_SPEED), get(b, VR_SPEED));
  fmi3FreeInstance(a);
  fmi3FreeInstance(b);
}

/* Parameters: set before initialization only, and only physically possible values. */
static void test_parameters(void)
{
  fmi3Instance fmu = start();
  CHECK(set(fmu, VR_L, 0.0) == fmi3Error, "L = 0 refused");
  fmi3FreeInstance(fmu);

  fmu = start();
  CHECK(set(fmu, VR_VBUS, 12.0) == fmi3OK, "Vbus before init");
  init(fmu);
  CHECK(get(fmu, VR_VBUS) == 12.0, "Vbus kept");
  CHECK(set(fmu, VR_VBUS, 5.0) == fmi3Error, "Vbus refused after init");
  fmi3FreeInstance(fmu);
}

int main(void)
{
  test_matches_plant();
  test_long_step();
  test_parameters();
  printf("%s\n", failures ? "FAIL" : "OK");
  return failures ? 1 : 0;
}
