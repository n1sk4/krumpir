#include "control_task.h"

#include <math.h>

#include "t12_io_sim.h"
#include "t12_loop.h"
#include "telemetry.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_cpu.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"

#define TICK_HZ 1000
#define TICK_DT_S (1.0f / TICK_HZ)
#define TIMING_GPIO 23
#define TIMING_MASK (1u << TIMING_GPIO)
#define WATCHDOG_FEED_TICKS 100
#define DEFAULT_AMBIENT_C 25.0

/* Simulation settings handed over from the console. */
typedef struct {
  float load;
  double ambient;
  t12_fault_t fault;
  t12_coeffs_t coeffs; /* valid once coeffs_gen is non-zero */
  uint32_t coeffs_gen;
} sim_cfg_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;       /* stats */
static portMUX_TYPE s_param_mux = portMUX_INITIALIZER_UNLOCKED; /* params and sim settings */
static loop_stats_t s_period, s_exec;
static uint32_t s_overruns;
static TaskHandle_t s_task;
static const t12_ctrl_cfg_t s_ctrl_default = T12_CTRL_CFG_DEFAULT;
static ctrl_params_t s_params = {350.0f, 0, 0, 0, 0, false};
static sim_cfg_t s_sim = {.ambient = DEFAULT_AMBIENT_C};
static volatile float s_temp, s_handle, s_amps, s_duty;
static volatile t12_trip_t s_trip;
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

  const t12_params_t plant_params = T12_PARAMS_DEFAULT;
  const t12_safety_cfg_t safety_cfg = T12_SAFETY_CFG_DEFAULT;
  const t12_cal_t cal = T12_CAL_DEFAULT;
  static t12_io_sim_t sim; /* large: keep it off the task stack */
  static t12_loop_t loop;
  t12_io_t io;
  t12_io_sim_init(&sim, &io, &plant_params, TICK_DT_S, DEFAULT_AMBIENT_C);
  t12_loop_init(&loop, &s_ctrl_default, &safety_cfg, &cal, TICK_DT_S);
  ctrl_params_t applied = {0};
  applied.sp = loop.ctrl.setpoint;
  uint32_t applied_coeffs_gen = 0;
  uint32_t tick = 0;

  uint32_t prev = 0;
  bool have_prev = false;
  for (;;) {
    uint32_t n = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t t0 = esp_cpu_get_cycle_count();
    GPIO.out_w1ts = TIMING_MASK;

    /* Take the settings for this tick in one short critical section (no tearing). */
    portENTER_CRITICAL(&s_param_mux);
    ctrl_params_t p = s_params;
    sim_cfg_t sim_cfg = s_sim;
    bool clear = s_clear_request;
    s_clear_request = false;
    portEXIT_CRITICAL(&s_param_mux);

    if (sim_cfg.coeffs_gen != applied_coeffs_gen) {
      t12_plant_set_coeffs(&sim.plant, &sim_cfg.coeffs);
      applied_coeffs_gen = sim_cfg.coeffs_gen;
    }
    sim.plant.fault = sim_cfg.fault;
    sim.ambient = sim_cfg.ambient;

    if (clear)
      t12_loop_clear(&loop);
    if (p.kp != applied.kp || p.ki != applied.ki || p.kd != applied.kd)
      t12_ctrl_set_gains(&loop.ctrl, p.kp, p.ki, p.kd);
    if (p.sp != applied.sp)
      t12_ctrl_set_setpoint(&loop.ctrl, p.sp);
    loop.ctrl.cfg.ramp = p.ramp;
    bool start_refused = false;
    if (p.run != applied.run) {
      if (p.run)
        start_refused = !t12_loop_start(&loop); /* refused while a trip is latched */
      else
        t12_loop_stop(&loop);
    }
    applied = p;

    bool fresh = t12_loop_tick(&loop, &io);
    t12_io_sim_step(&sim);

    /* A trip, or a refused start, drops the run flag again until the user starts the controller. */
    if (p.run && (start_refused || !t12_safety_ok(&loop.safety))) {
      portENTER_CRITICAL(&s_param_mux);
      s_params.run = false;
      portEXIT_CRITICAL(&s_param_mux);
    }
    s_trip = loop.safety.latched;
    if (fresh) {
      s_temp = loop.temp;
      s_handle = loop.handle;
      s_amps = loop.amps;
      s_duty = loop.duty;
      telem_sample_t s = {tick, {loop.ctrl.sp_active, loop.temp, loop.duty, loop.amps}};
      telemetry_push(&s);
    }
    if (tick % WATCHDOG_FEED_TICKS == 0)
      esp_task_wdt_reset();
    tick++;

    GPIO.out_w1tc = TIMING_MASK;
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

void control_task_start(void)
{
  s_params.kp = s_ctrl_default.kp;
  s_params.ki = s_ctrl_default.ki;
  s_params.kd = s_ctrl_default.kd;
  s_params.ramp = s_ctrl_default.ramp;
  loop_stats_reset(&s_period);
  loop_stats_reset(&s_exec);
  xTaskCreatePinnedToCore(control_task, "control", 6144, NULL, configMAX_PRIORITIES - 2, NULL, 1);
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

void control_task_set_load(float w_per_k)
{
  /* The coefficient math uses doubles, so do it here on the caller core, not in the loop. */
  t12_params_t params = T12_PARAMS_DEFAULT;
  params.g_load = w_per_k;
  t12_coeffs_t coeffs;
  t12_plant_coeffs(&params, TICK_DT_S, &coeffs);

  portENTER_CRITICAL(&s_param_mux);
  s_sim.load = w_per_k;
  s_sim.coeffs = coeffs;
  s_sim.coeffs_gen++;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_fault(t12_fault_t fault)
{
  portENTER_CRITICAL(&s_param_mux);
  s_sim.fault = fault;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_set_ambient(float celsius)
{
  portENTER_CRITICAL(&s_param_mux);
  s_sim.ambient = celsius;
  portEXIT_CRITICAL(&s_param_mux);
}

void control_task_get_sim(float *load, t12_fault_t *fault, float *ambient)
{
  portENTER_CRITICAL(&s_param_mux);
  *load = s_sim.load;
  *fault = s_sim.fault;
  *ambient = (float)s_sim.ambient;
  portEXIT_CRITICAL(&s_param_mux);
}

t12_trip_t control_task_get_trip(void) { return s_trip; }

void control_task_clear_trip(void) { s_clear_request = true; }

void control_task_get_state(control_state_t *s)
{
  s->temp = s_temp;
  s->handle = s_handle;
  s->amps = s_amps;
  s->duty = s_duty;
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
