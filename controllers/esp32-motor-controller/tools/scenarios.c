/* Host tool for the closed-loop scenarios.
 *   scenarios <step|load|saturation|inertia>   CSV of one run with the default gains
 *   scenarios sweep                            metrics for scaled gains, as a markdown table
 * The CSV columns match what plot.py reads. */
#include <stdio.h>
#include <string.h>

#include "motor_scenario.h"

static void print_sample(void *ctx, const motor_scn_sample_t *s)
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
  const motor_ctrl_cfg_t base = {3.6e-4f, 0.015f, 0.0f, -10000.0f, 10000.0f};

  printf("| gain scale | kp | ki | scenario | overshoot %% | undershoot %% | settling s | "
         "final error rpm | min rpm |\n|---|---|---|---|---|---|---|---|---|\n");
  for (unsigned i = 0; i < sizeof scales / sizeof scales[0]; i++) {
    motor_ctrl_cfg_t g = base;
    g.kp = base.kp * scales[i]; /* ki scales with kp, so the integral time stays fixed */
    g.ki = base.ki * scales[i];
    for (int id = 0; id < MOTOR_SCN_COUNT; id++) {
      motor_scn_result_t r = motor_scenario_run((motor_scenario_t)id, &g, NULL, NULL);
      printf("| %.1f | %.2e | %.4f | %s | %.1f | %.1f | %.3f | %.1f | %.0f |\n", scales[i], g.kp,
             g.ki, motor_scenario_name((motor_scenario_t)id), r.overshoot_pct, r.undershoot_pct,
             r.settling_s, r.final_error_rpm, r.min_rpm);
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
  for (int id = 0; id < MOTOR_SCN_COUNT; id++) {
    if (!strcmp(argv[1], motor_scenario_name((motor_scenario_t)id))) {
      const motor_ctrl_cfg_t gains = MOTOR_CTRL_CFG_DEFAULT;
      printf("t_s,setpoint_rpm,speed_rpm,duty,error_rpm,load_nm\n");
      motor_scenario_run((motor_scenario_t)id, &gains, print_sample, NULL);
      return 0;
    }
  }
  fprintf(stderr, "unknown scenario %s\n", argv[1]);
  return 2;
}
