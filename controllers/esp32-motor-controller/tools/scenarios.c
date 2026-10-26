/* Host tool for the closed-loop scenarios.
 *   scenarios <step|load|saturation|inertia>   CSV of one run with the default gains
 *   scenarios sweep                            metrics for scaled gains, as a markdown table
 * The CSV columns match what plot.py reads. */
#include <stdio.h>
#include <string.h>

#include "scenario.h"

static void print_sample(void *ctx, const scn_sample_t *s)
{
  (void)ctx;
  printf("%.3f,%.1f,%.1f,%.4f,%.1f,%.4f\n", s->t_s, s->setpoint_rpm, s->speed_rpm, s->duty,
         s->setpoint_rpm - s->speed_rpm, s->load_nm);
}

static int sweep(void)
{
  const float scales[] = {0.5f, 1.0f, 2.0f, 3.0f};
  /* Reference gains the sweep scales from. ki/kp = 41.7 1/s cancels the slow motor pole
   * (24 ms time constant), so every scale keeps the same integral time. */
  const ctrl_params_t base = {0.0f, 3.6e-4f, 0.015f, 0.0f, false};

  printf("| gain scale | kp | ki | scenario | overshoot %% | undershoot %% | settling s | "
         "final error rpm | min rpm |\n|---|---|---|---|---|---|---|---|---|\n");
  for (unsigned i = 0; i < sizeof scales / sizeof scales[0]; i++) {
    ctrl_params_t g = base;
    g.kp = base.kp * scales[i]; /* ki scales with kp, so the integral time stays fixed */
    g.ki = base.ki * scales[i];
    for (int id = 0; id < SCN_COUNT; id++) {
      scn_result_t r = scenario_run((scenario_t)id, &g, NULL, NULL);
      printf("| %.1f | %.2e | %.4f | %s | %.1f | %.1f | %.3f | %.1f | %.0f |\n", scales[i], g.kp,
             g.ki, scenario_name((scenario_t)id), r.overshoot_pct, r.undershoot_pct, r.settling_s,
             r.final_error_rpm, r.min_rpm);
    }
  }
  return 0;
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s <step|load|saturation|inertia|sweep>\n", argv[0]);
    return 2;
  }
  if (!strcmp(argv[1], "sweep"))
    return sweep();
  for (int id = 0; id < SCN_COUNT; id++) {
    if (!strcmp(argv[1], scenario_name((scenario_t)id))) {
      const ctrl_params_t gains = CTRL_PARAMS_DEFAULT;
      printf("t_s,setpoint_rpm,speed_rpm,duty,error_rpm,load_nm\n");
      scenario_run((scenario_t)id, &gains, print_sample, NULL);
      return 0;
    }
  }
  fprintf(stderr, "unknown scenario %s\n", argv[1]);
  return 2;
}
