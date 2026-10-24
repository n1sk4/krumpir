#include "control_task.h"

#include "driver/gptimer.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#define TICK_HZ 1000
#define TIMING_GPIO 23
#define TIMING_MASK (1u << TIMING_GPIO)
#define CYCLES_PER_US CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

/* Test load inside the loop body; override with -DCONTROL_BUSY_US=200. */
#ifndef CONTROL_BUSY_US
#define CONTROL_BUSY_US 0
#endif

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static loop_stats_t s_period, s_exec;
static uint32_t s_overruns;
static TaskHandle_t s_task;

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *ev,
                               void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    return woken == pdTRUE;
}

/* Runs on core 1 so the timer interrupt is allocated on core 1 as well. */
static void control_task(void *arg)
{
    s_task = xTaskGetCurrentTaskHandle();

    gpio_config_t io = {.pin_bit_mask = TIMING_MASK, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);

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
        .alarm_count = 1000000 / TICK_HZ,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
    ESP_ERROR_CHECK(gptimer_enable(timer));
    ESP_ERROR_CHECK(gptimer_start(timer));

    uint32_t prev = 0;
    bool have_prev = false;
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t t0 = esp_cpu_get_cycle_count();
        GPIO.out_w1ts = TIMING_MASK;

        /* control body goes here (Phase 3+) */
#if CONTROL_BUSY_US > 0
        while (esp_cpu_get_cycle_count() - t0 < CONTROL_BUSY_US * CYCLES_PER_US) {
        }
#endif

        GPIO.out_w1tc = TIMING_MASK;
        uint32_t exec = esp_cpu_get_cycle_count() - t0;

        portENTER_CRITICAL(&s_mux);
        if (n > 1) s_overruns += n - 1;
        if (have_prev) loop_stats_add(&s_period, t0 - prev);
        loop_stats_add(&s_exec, exec);
        portEXIT_CRITICAL(&s_mux);
        prev = t0;
        have_prev = true;
    }
}

void control_task_start(void)
{
    loop_stats_reset(&s_period);
    loop_stats_reset(&s_exec);
    xTaskCreatePinnedToCore(control_task, "control", 4096, NULL, configMAX_PRIORITIES - 2, NULL,
                            1);
}

void control_task_get_stats(loop_stats_t *period, loop_stats_t *exec, uint32_t *overruns)
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
