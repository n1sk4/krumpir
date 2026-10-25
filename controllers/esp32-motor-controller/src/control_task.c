#include "control_task.h"

#include "io_sim.h"
#include "pid.h"
#include "telemetry.h"

#include "driver/gptimer.h"
#include "esp_cpu.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#define TICK_HZ 1000
#define TICK_DT_S (1.0f / TICK_HZ)
#define TIMING_GPIO 23
#define TIMING_MASK (1u << TIMING_GPIO)
#define CYCLES_PER_US CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#define TELEMETRY_DECIMATION 10 /* 1 kHz -> 100 Hz */
#define WATCHDOG_FEED_TICKS 100

/* Test load inside the loop body; override with -DCONTROL_BUSY_US=200. */
#ifndef CONTROL_BUSY_US
#define CONTROL_BUSY_US 0
#endif

/* Test aid: -DCONTROL_HANG_AT_TICK=3000 makes the loop spin forever at that tick, to check
 * that the task watchdog resets the board. */

/* Simulation settings handed over from the console. */
typedef struct {
    float load_nm;
    float inertia_scale;
    plant_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
    uint32_t coeffs_gen;
} sim_cfg_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;       /* stats */
static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED; /* params and sim settings */
static loop_stats_t s_period, s_exec;
static uint32_t s_overruns;
static TaskHandle_t s_task;
static ctrl_params_t s_params = CTRL_PARAMS_DEFAULT;
static sim_cfg_t s_sim = {.inertia_scale = 1.0f};
static volatile float s_rpm, s_duty;
static volatile fault_t s_fault;
static volatile bool s_clear_request;

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
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

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
    io_sim_init(&sim, &io, &params, TICK_DT_S);
    ctrl_params_t applied = CTRL_PARAMS_DEFAULT;
    pid_init(&pid, applied.kp, applied.ki, applied.kd, -1.0f, 1.0f, TICK_DT_S, 0.0f);
    const safety_cfg_t safety_cfg = SAFETY_CFG_DEFAULT;
    safety_t safety;
    safety_init(&safety, &safety_cfg);
    uint32_t applied_coeffs_gen = 0;
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
        sim_cfg_t sim_cfg = s_sim;
        portEXIT_CRITICAL(&s_param_mux);

        if (sim_cfg.coeffs_gen != applied_coeffs_gen) {
            plant_sim_set_coeffs(&sim.plant, &sim_cfg.coeffs);
            applied_coeffs_gen = sim_cfg.coeffs_gen;
        }
        io_sim_set_load(&sim, sim_cfg.load_nm);

        if (s_clear_request) {
            s_clear_request = false;
            safety_clear(&safety);
        }
        float rpm = io.sensor_read(io.ctx);
        fault_t fault = safety_update(&safety, rpm);
        s_fault = fault;
        if (fault != FAULT_NONE && p.run) {
            p.run = false; /* a fault stops the controller until the user starts it again */
            portENTER_CRITICAL(&s_param_mux);
            s_params.run = false;
            portEXIT_CRITICAL(&s_param_mux);
        }

        if (p.kp != applied.kp || p.ki != applied.ki || p.kd != applied.kd) {
            pid_set_gains(&pid, p.kp, p.ki, p.kd);
        }
        if (p.run != applied.run) pid_reset(&pid); /* start and stop both begin from zero */
        applied = p;

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
        if (tick % WATCHDOG_FEED_TICKS == 0) esp_task_wdt_reset();
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

void control_task_set_load(float torque_nm)
{
    portENTER_CRITICAL(&s_param_mux);
    s_sim.load_nm = torque_nm;
    portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_inertia_scale(float scale)
{
    /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
    plant_params_t params = PLANT_PARAMS_N20;
    params.J *= scale;
    plant_coeffs_t coeffs;
    plant_sim_coeffs(&params, TICK_DT_S, &coeffs);

    portENTER_CRITICAL(&s_param_mux);
    s_sim.inertia_scale = scale;
    s_sim.coeffs = coeffs;
    s_sim.coeffs_gen++;
    portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_sim(float *load_nm, float *inertia_scale)
{
    portENTER_CRITICAL(&s_param_mux);
    *load_nm = s_sim.load_nm;
    *inertia_scale = s_sim.inertia_scale;
    portEXIT_CRITICAL(&s_param_mux);
}

fault_t control_task_get_fault(void)
{
    return s_fault;
}

void control_task_clear_fault(void)
{
    s_clear_request = true;
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
