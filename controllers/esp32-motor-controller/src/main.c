#include <stdio.h>

#include "console.h"
#include "control_task.h"
#include "gen/ctrl_gen.h"

#ifdef KRUMPIR_XCP
#include "xcp_glue.h"
#endif
#ifdef KRUMPIR_IO_HIL
#include "hil_uart.h"
#endif

void app_main(void)
{
  ctrl_gen_telemetry_start(); /* CSV header and rows from controller.toml */
#ifdef KRUMPIR_XCP
  xcp_glue_start("motor"); /* before the control task: its XCP events need XcpInit */
#endif
  control_task_start();
#ifdef KRUMPIR_IO_HIL
  hil_uart_start(); /* the plant on UART2; after the control task bound its slots */
#endif
  console_start();
}
