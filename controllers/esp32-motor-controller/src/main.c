#include <stdio.h>

#include "console.h"
#include "control_task.h"
#include "gen/ctrl_gen.h"

#ifdef KRUMPIR_XCP
#include "xcp_glue.h"
#endif

void app_main(void)
{
  ctrl_gen_telemetry_start(); /* CSV header and rows from controller.toml */
#ifdef KRUMPIR_XCP
  xcp_glue_start("motor"); /* before the control task: its XCP events need XcpInit */
#endif
  control_task_start();
  console_start();
}
