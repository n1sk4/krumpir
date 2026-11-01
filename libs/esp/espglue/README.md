# espglue

The ESP-IDF + FreeRTOS **port of the OSAL contract** in [../../osal](../../osal): it implements
`tick_task.h`, `telemetry.h` and `console_util.h` (headers in `libs/osal/include`) and provides
`osal_port.h`, the critical section behind `osal.h`. Controllers include only the contract headers,
so another platform (Zephyr, a host port) can replace this library without touching them. It is not
built by the native tests; each controller pulls it in only for its `esp32dev` environments.

| Module | What it does |
|---|---|
| `tick_task.h` | A fixed-rate control task on one core, driven by a hardware timer: raises the timing pin around the body, feeds the task watchdog, and records period, execution time and missed ticks. |
| `telemetry.h` | CSV telemetry without blocking the control task: a lock-free ring (from `ctrlcore`) drained by a low-priority task, printed only while logging is on. |
| `console_util.h` | The serial console on UART0: REPL set-up, `help` and `log on|off`, command registration, a float argument parser and the "loop (since last status)" line. |

What stays in a controller is its control body, its parameters, its own console commands and
`status` text, and the format of one CSV row.

## Adding a controller

1. Create `controllers/<name>/` with `platformio.ini`, `CMakeLists.txt`, `sdkconfig.defaults` and
   `src/` (copy them from `esp32-t12-controller` or `esp32-motor-controller`). In
   `platformio.ini`:
   ```
   [env]
   lib_extra_dirs = ../../libs, ../../libs/plants, ../../libs/esp
   lib_deps = ctrlcore

   [env:esp32dev]
   platform = espressif32@6.9.0
   board = esp32dev
   framework = espidf
   monitor_speed = 115200
   lib_deps =
     ${env.lib_deps}
     espglue
   ```
   The native test environment must not list `espglue`.
2. The control body, run once per tick inside the timing pulse:
   ```c
   #include "telemetry.h"
   #include "tick_task.h"

   static void control_init(void *ctx) { /* runs on the task, before the first tick */ }

   static void control_tick(void *ctx, uint32_t tick)
   {
     /* read the sensor, run the controller, write the actuator */
     telem_sample_t s = {tick, {setpoint, measurement, duty}};
     telemetry_push(&s);
   }

   void control_task_start(void)
   {
     tick_cfg_t cfg = TICK_CFG_DEFAULT; /* 1 kHz, GPIO 23, core 1, 100-tick watchdog feed */
     tick_task_start(&cfg, control_init, control_tick, NULL);
   }
   ```
   The body must not block. Settings that the console changes should be copied into the task in a
   short critical section once per tick (`osal_crit_enter`/`osal_crit_exit` from `osal.h`), so a
   change never tears.
3. The console, with the controller's own commands:
   ```c
   #include "console_util.h"

   static int cmd_sp(int argc, char **argv)
   {
     float v;
     if (!console_arg_float(argc, argv, 0.0f, 100.0f, &v))
       return 1;
     /* hand v to the control task */
     return 0;
   }

   void console_start(void)
   {
     console_init("name> ");
     console_register("sp", "Set the setpoint", "<value>", cmd_sp);
     console_register("status", "Show state and loop timing", NULL, cmd_status);
     console_run();
   }
   ```
   `status` should end with `console_print_loop_stats()`.
4. `app_main` starts the three parts and gives telemetry its CSV header and row printer:
   ```c
   static void print_row(const telem_sample_t *s)
   {
     printf("%u,%d,%d\n", (unsigned)s->t_ms, telemetry_round(s->v[0]), telemetry_round(s->v[1]));
   }

   void app_main(void)
   {
     telemetry_start("t_ms,setpoint,measurement", print_row);
     control_task_start();
     console_start();
   }
   ```
5. `src/CMakeLists.txt` lists the project's own files and `REQUIRES driver esp_hw_support
   esp_system console`.

A telemetry sample is a timestamp and up to four floats (`TELEM_VALUES` in `ctrlcore`'s
`ring_buf.h`). Rows are printed from the drain task on core 0, so formatting there never delays the
control loop.

## Test aids

Build flags for timing checks, for example `PLATFORMIO_BUILD_FLAGS=-DCONTROL_BUSY_US=200`:

- `CONTROL_BUSY_US=200` busy-waits that many microseconds inside every tick.
- `CONTROL_HANG_AT_TICK=3000` spins forever at that tick, to check that the task watchdog resets
  the board within about a second.
