/* Host tool for the closed-loop scenarios.
 *   scenarios <cold|load|ambient|open|short|stuck|runaway>   CSV of one run, default gains
 *   scenarios summary                                        results of every scenario, markdown
 *   scenarios sweep                                          kp / ki sweep, markdown
 * The CSV columns match what plot.py reads. */
#include <stdio.h>
#include <string.h>

#include "t12_scenario.h"

static void print_sample(void *ctx, const t12_scn_sample_t *s)
{
  (void)ctx;
  printf("%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.4f,%.3f,%.3f,%.1f,%d\n", s->t_s, s->setpoint, s->temp,
         s->tip, s->sensor, s->handle, s->duty, s->amps, s->load, s->ambient, (int)s->trip);
}

static int summary(void)
{
  const t12_ctrl_cfg_t cfg = T12_CTRL_CFG_DEFAULT;
  printf("| scenario | overshoot %% | dip %% | settling s | final error K | trip | trip after s |\n"
         "|---|---|---|---|---|---|---|\n");
  for (int id = 0; id < T12_SCN_COUNT; id++) {
    t12_scn_result_t r = t12_scenario_run((t12_scenario_t)id, &cfg, NULL, NULL);
    const char *name = t12_scenario_name((t12_scenario_t)id);
    if (id == T12_SCN_COLD_START) { /* the rise from ambient is not a dip */
      printf("| %s | %.1f | - | %.2f | %.1f | - | - |\n", name, r.overshoot_pct, r.settling_s,
             r.final_error);
    } else if (id < T12_SCN_OPEN_TIP) { /* the response only means something without a trip */
      printf("| %s | %.1f | %.1f | %.2f | %.1f | - | - |\n", name, r.overshoot_pct,
             r.undershoot_pct, r.settling_s, r.final_error);
    } else {
      printf("| %s | - | - | - | - | %s | %.2f |\n", name, t12_trip_name(r.trip), r.trip_s);
    }
  }
  return 0;
}

static int sweep(void)
{
  const float kps[] = {0.02f, 0.05f, 0.1f};
  const float kis[] = {0.02f, 0.05f, 0.1f};
  printf("| kp | ki | cold: overshoot %% | cold: settling s | load: dip %% | load: settling s |\n"
         "|---|---|---|---|---|---|\n");
  for (unsigned i = 0; i < sizeof kps / sizeof kps[0]; i++) {
    for (unsigned j = 0; j < sizeof kis / sizeof kis[0]; j++) {
      t12_ctrl_cfg_t cfg = T12_CTRL_CFG_DEFAULT;
      cfg.kp = kps[i];
      cfg.ki = kis[j];
      t12_scn_result_t cold = t12_scenario_run(T12_SCN_COLD_START, &cfg, NULL, NULL);
      t12_scn_result_t load = t12_scenario_run(T12_SCN_LOAD_STEP, &cfg, NULL, NULL);
      printf("| %.2f | %.2f | %.1f | %.2f | %.1f | %.2f |\n", cfg.kp, cfg.ki, cold.overshoot_pct,
             cold.settling_s, load.undershoot_pct, load.settling_s);
    }
  }
  return 0;
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s <cold|load|ambient|open|short|stuck|runaway|summary|sweep>\n",
            argv[0]);
    return 2;
  }
  if (!strcmp(argv[1], "summary"))
    return summary();
  if (!strcmp(argv[1], "sweep"))
    return sweep();
  for (int id = 0; id < T12_SCN_COUNT; id++) {
    if (!strcmp(argv[1], t12_scenario_name((t12_scenario_t)id))) {
      const t12_ctrl_cfg_t cfg = T12_CTRL_CFG_DEFAULT;
      printf("t_s,setpoint_c,temp_c,tip_c,sensor_c,handle_c,duty,current_a,load_wk,ambient_c,trip\n");
      t12_scenario_run((t12_scenario_t)id, &cfg, print_sample, NULL);
      return 0;
    }
  }
  fprintf(stderr, "unknown scenario %s\n", argv[1]);
  return 2;
}
