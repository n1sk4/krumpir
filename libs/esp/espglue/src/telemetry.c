#include "telemetry.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DRAIN_PERIOD_MS 10

static ring_buf_t s_ring;
static volatile bool s_log;
static const char *s_header;
static telemetry_row_t s_print_row;

int telemetry_round(float x) { return (int)(x < 0.0f ? x - 0.5f : x + 0.5f); }

static void drain_task(void *arg)
{
  telem_sample_t s;
  for (;;) {
    while (ring_buf_pop(&s_ring, &s)) {
      if (s_log)
        s_print_row(&s);
    }
    vTaskDelay(pdMS_TO_TICKS(DRAIN_PERIOD_MS));
  }
}

void telemetry_start(const char *csv_header, telemetry_row_t print_row)
{
  s_header = csv_header;
  s_print_row = print_row;
  ring_buf_init(&s_ring);
  xTaskCreatePinnedToCore(drain_task, "telemetry", 3072, NULL, tskIDLE_PRIORITY + 1, NULL, 0);
}

void telemetry_push(const telem_sample_t *s) { ring_buf_push(&s_ring, s); }

void telemetry_set_log(bool on)
{
  if (on && !s_log)
    printf("%s\n", s_header);
  s_log = on;
}

bool telemetry_log(void) { return s_log; }

uint32_t telemetry_dropped(void) { return ring_buf_dropped(&s_ring); }
