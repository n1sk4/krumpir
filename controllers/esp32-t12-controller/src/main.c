#include <stdio.h>

#include "control_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define US(cycles) ((cycles) / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

void app_main(void)
{
  control_task_start();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    loop_stats_t period, exec;
    uint32_t overruns;
    control_task_get_stats(&period, &exec, &overruns);
    printf("loop period us min/mean/max %u/%u/%u  exec us max %u  overruns %u  n=%u\n",
           (unsigned)US(period.min), (unsigned)US(loop_stats_mean(&period)),
           (unsigned)US(period.max), (unsigned)US(exec.max), (unsigned)overruns,
           (unsigned)period.count);
  }
}
