#include "console.h"
#include "control_task.h"
#include "telemetry.h"

void app_main(void)
{
  telemetry_start();
  control_task_start();
  console_start();
}
