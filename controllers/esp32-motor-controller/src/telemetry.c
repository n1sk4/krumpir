#include "telemetry.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DRAIN_PERIOD_MS 10

static ring_buf_t s_ring;
static volatile bool s_log;

static int rounded(float x)
{
    return (int)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

static void drain_task(void *arg)
{
    telem_sample_t s;
    for (;;) {
        while (ring_buf_pop(&s_ring, &s)) {
            if (s_log) {
                printf("%u,%d,%d,%d\n", (unsigned)s.t_ms, rounded(s.sp), rounded(s.rpm),
                       rounded(s.duty * 1000.0f));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DRAIN_PERIOD_MS));
    }
}

void telemetry_start(void)
{
    ring_buf_init(&s_ring);
    xTaskCreatePinnedToCore(drain_task, "telemetry", 3072, NULL, tskIDLE_PRIORITY + 1, NULL, 0);
}

void telemetry_push(const telem_sample_t *s)
{
    ring_buf_push(&s_ring, s);
}

void telemetry_set_log(bool on)
{
    if (on && !s_log) printf("t_ms,setpoint_rpm,speed_rpm,duty_permille\n");
    s_log = on;
}

bool telemetry_log(void)
{
    return s_log;
}

uint32_t telemetry_dropped(void)
{
    return ring_buf_dropped(&s_ring);
}
