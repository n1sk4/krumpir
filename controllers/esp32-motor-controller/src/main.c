#include <stdio.h>

#include "control_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NS(cycles) ((uint64_t)(cycles) * 1000 / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

/* Define -DSPAM_CONSOLE to print as fast as possible and show the effect on core 1. */
#ifdef SPAM_CONSOLE
#define REPORT_PERIOD_MS 5
#else
#define REPORT_PERIOD_MS 1000
#endif

void app_main(void)
{
    control_task_start();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
        loop_stats_t period, exec;
        uint32_t overruns;
        control_task_get_stats(&period, &exec, &overruns);
        if (!period.count) continue;
        float rpm, duty;
        control_task_get_state(&rpm, &duty);
        printf("period ns min/mean/max %llu/%llu/%llu  exec ns min/mean/max %llu/%llu/%llu  "
               "overruns %u  n=%u  rpm %d duty_permille %d\n",
               NS(period.min), NS(loop_stats_mean(&period)), NS(period.max), NS(exec.min),
               NS(loop_stats_mean(&exec)), NS(exec.max), (unsigned)overruns,
               (unsigned)period.count, (int)rpm, (int)(duty * 1000));
    }
}
