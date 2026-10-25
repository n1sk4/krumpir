#include "control_task.h"

#include "io_sim.h"
#include "pid.h"
#include "telemetry.h"

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
#define TELEMETRY_DECIMATION 10 /* 1 kHz -> 100 Hz */

/* Test load inside the loop body; override with -DCONTROL_BUSY_US=200. */
#ifndef CONTROL_BUSY_US
#define CONTROL_BUSY_US 0
#endif

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;       /* stats */
static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED; /* params */
static loop_stats_t s_period, s_exec;
static uint32_t s_overruns;
static TaskHandle_t s_task;
static ctrl_params_t s_params = CTRL_PARAMS_DEFAULT;
static volatile float s_rpm, s_duty;

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

    gpio_config_t gpio_cfg = {.pin_bit_mask = TIMING_MASK, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&gpio_cfg);

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

    plant_params_t params = PLANT_PARAMS_N20;
    io_sim_t sim;
    io_t io;
    pid_ctrl_t pid;
    io_sim_init(&sim, &io, &params, 1.0f / TICK_HZ);
    ctrl_params_t applied = CTRL_PARAMS_DEFAULT;
    pid_init(&pid, applied.kp, applied.ki, applied.kd, -1.0f, 1.0f, 1.0f / TICK_HZ, 0.0f);
    uint32_t tick = 0;

    uint32_t prev = 0;
    bool have_prev = false;
    for (;;) {
        uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t t0 = esp_cpu_get_cycle_count();
        GPIO.out_w1ts = TIMING_MASK;

        /* Take the settings for this cycle in one short critical section (no tearing). */
        portENTER_CRITICAL(&s_param_mux);
        ctrl_params_t p = s_params;
        portEXIT_CRITICAL(&s_param_mux);

        if (p.kp != applied.kp || p.ki != applied.ki || p.kd != applied.kd) {
            pid_set_gains(&pid, p.kp, p.ki, p.kd);
        }
        if (p.run != applied.run) pid_reset(&pid); /* start and stop both begin from zero */
        applied = p;

        float rpm = io.sensor_read(io.ctx);
        float duty = 0.0f;
        if (p.run) {
            duty = pid_update(&pid, p.sp, rpm);
            io.actuator_write(io.ctx, duty);
        } else {
            io.safe_state(io.ctx); /* motor coasts down in the simulation */
        }
        io_sim_step(&sim);
        s_rpm = rpm;
        s_duty = duty;

        if (tick % TELEMETRY_DECIMATION == 0) {
            telem_sample_t s = {tick, p.sp, rpm, duty};
            telemetry_push(&s);
        }
        tick++;
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

void control_task_set_params(const ctrl_params_t *p)
{
    portENTER_CRITICAL(&s_param_mux);
    s_params = *p;
    portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_params(ctrl_params_t *p)
{
    portENTER_CRITICAL(&s_param_mux);
    *p = s_params;
    portEXIT_CRITICAL(&s_param_mux);
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

void control_task_get_state(float *rpm, float *duty)
{
    *rpm = s_rpm;
    *duty = s_duty;
}
