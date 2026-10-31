#include "telemetry.h"

#include <math.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DRAIN_PERIOD_MS 20

static ring_buf_t s_ring;
static volatile bool s_log;

static int rounded(float x) { return (int)(x < 0.0f ? x - 0.5f : x + 0.5f); }

static void drain_task(void *arg)
{
  telem_sample_t s;
  for (;;) {
    while (ring_buf_pop(&s_ring, &s)) {
      if (s_log) {
        /* An invalid tip reading prints as -1 so the row keeps its integer shape. */
        int tip = isfinite(s.v[1]) ? rounded(s.v[1]) : -1;
        printf("%u,%d,%d,%d,%d\n", (unsigned)s.t_ms, rounded(s.v[0]), tip,
               rounded(s.v[2] * 1000.0f), rounded(s.v[3] * 1000.0f));
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

void telemetry_push(const telem_sample_t *s) { ring_buf_push(&s_ring, s); }

void telemetry_set_log(bool on)
{
  if (on && !s_log)
    printf("t_ms,setpoint_c,temp_c,duty_permille,current_ma\n");
  s_log = on;
}

bool telemetry_log(void) { return s_log; }

uint32_t telemetry_dropped(void) { return ring_buf_dropped(&s_ring); }
