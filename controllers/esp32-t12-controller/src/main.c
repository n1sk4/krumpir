#include <math.h>
#include <stdio.h>

#include "console.h"
#include "control_task.h"
#include "telemetry.h"
#ifdef KRUMPIR_XCP
#include "xcp_glue.h"
#endif

/* One CSV row: an invalid tip reading prints as -1 so the row keeps its integer shape. */
static void print_row(const telem_sample_t *s)
{
  int tip = isfinite(s->v[1]) ? telemetry_round(s->v[1]) : -1;
  printf("%u,%d,%d,%d,%d\n", (unsigned)s->t_ms, telemetry_round(s->v[0]), tip,
         telemetry_round(s->v[2] * 1000.0f), telemetry_round(s->v[3] * 1000.0f));
}

void app_main(void)
{
  telemetry_start("t_ms,setpoint_c,temp_c,duty_permille,current_ma", print_row);
#ifdef KRUMPIR_XCP
  xcp_glue_start("t12"); /* before the control task: its XCP events need XcpInit */
#endif
  control_task_start();
  console_start();
}
