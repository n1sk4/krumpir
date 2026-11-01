#include <stdio.h>

#include "console.h"
#include "control_task.h"
#include "telemetry.h"

#ifdef KRUMPIR_XCP
#include "xcp_glue.h"
#endif

static void print_row(const telem_sample_t *s)
{
  printf("%u,%d,%d,%d\n", (unsigned)s->t_ms, telemetry_round(s->v[0]), telemetry_round(s->v[1]),
         telemetry_round(s->v[2] * 1000.0f));
}

void app_main(void)
{
  telemetry_start("t_ms,setpoint_rpm,speed_rpm,duty_permille", print_row);
#ifdef KRUMPIR_XCP
  xcp_glue_start("motor"); /* before the control task: its XCP events need XcpInit */
#endif
  control_task_start();
  console_start();
}
