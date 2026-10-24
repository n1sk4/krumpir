#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    for (;;) {
        printf("esp32-motor-controller: hello\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
