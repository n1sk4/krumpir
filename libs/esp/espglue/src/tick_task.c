#include "tick_task.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_cpu.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/gpio_struct.h"

#define CYCLES_PER_US CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

#ifndef CONTROL_BUSY_US
#define CONTROL_BUSY_US 0
#endif

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED; /* stats */
static loop_stats_t s_period, s_exec;
static uint32_t s_overruns;
static TaskHandle_t s_task;
static tick_cfg_t s_cfg;
static tick_init_t s_init;
static tick_body_t s_body;
static void *s_ctx;

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *ev,
                               void *arg)
{
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(s_task, &woken);
  return woken == pdTRUE;
}

/* Runs on the configured core so the timer interrupt is allocated on that core as well. */
static void tick_task(void *arg)
{
  s_task = xTaskGetCurrentTaskHandle();
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  const uint32_t timing_mask = 1u << s_cfg.timing_gpio;
  gpio_config_t gpio_cfg = {.pin_bit_mask = timing_mask, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&gpio_cfg);

  if (s_init)
    s_init(s_ctx); /* before the timer starts, so the first tick is not a late one */

  gptimer_handle_t timer;
  gptimer_config_t cfg = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000,
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));
  gptimer_event_callbacks_t cbs = {.on_alarm = on_alarm};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, NULL));
  gptimer_alarm_config_t alarm = {
      .alarm_count = 1000000 / s_cfg.hz,
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
  ESP_ERROR_CHECK(gptimer_enable(timer));
  ESP_ERROR_CHECK(gptimer_start(timer));

  uint32_t tick = 0;
  uint32_t prev = 0;
  bool have_prev = false;
  for (;;) {
    uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t t0 = esp_cpu_get_cycle_count();
    GPIO.out_w1ts = timing_mask;

    s_body(s_ctx, tick);

    if (tick % s_cfg.wdt_feed_ticks == 0)
      esp_task_wdt_reset();
#ifdef CONTROL_HANG_AT_TICK
    if (tick == CONTROL_HANG_AT_TICK) {
      for (;;) {
      }
    }
#endif
    tick++;
#if CONTROL_BUSY_US > 0
    while (esp_cpu_get_cycle_count() - t0 < CONTROL_BUSY_US * CYCLES_PER_US) {
    }
#endif

    GPIO.out_w1tc = timing_mask;
    uint32_t exec = esp_cpu_get_cycle_count() - t0;

    portENTER_CRITICAL(&s_mux);
    if (n > 1)
      s_overruns += n - 1;
    if (have_prev)
      loop_stats_add(&s_period, t0 - prev);
    loop_stats_add(&s_exec, exec);
    portEXIT_CRITICAL(&s_mux);
    prev = t0;
    have_prev = true;
  }
}

void tick_task_start(const tick_cfg_t *cfg, tick_init_t init, tick_body_t body, void *ctx)
{
  s_cfg = *cfg;
  s_init = init;
  s_body = body;
  s_ctx = ctx;
  loop_stats_reset(&s_period);
  loop_stats_reset(&s_exec);
  xTaskCreatePinnedToCore(tick_task, "control", s_cfg.stack, NULL, configMAX_PRIORITIES - 2, NULL,
                          s_cfg.core);
}

void tick_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns)
{
  portENTER_CRITICAL(&s_mux);
  *period = s_period;
  *exec = s_exec;
  *overruns = s_overruns;
  loop_stats_reset(&s_period);
  loop_stats_reset(&s_exec);
  s_overruns = 0;
  portEXIT_CRITICAL(&s_mux);
}
