# Shared libraries

Plain C, no framework, used by the controllers (on the chip and in the PC tests) and by the plant
FMUs. Each builds with PlatformIO (`library.json`), any CMake project (a target
`krumpir::<name>`) and ESP-IDF (the same `CMakeLists.txt` registers a component when
`ESP_PLATFORM` is set). Building: [../BUILDING.md](../BUILDING.md) (part `libs`; `pio test -e
native` here runs the same Unity tests).

| Library | Contents |
|---------|----------|
| `ctrlcore/` | PID, ring buffer, loop statistics, step-response figures, parameter parsing |
| `plants/` | the plant models and their FMUs ([below](#plant-models)) |
| `osal/` | the OS abstraction contract (`osal.h` critical section, `tick_task.h`, `telemetry.h`, `console_util.h`) and its platform-independent parts |
| `osal_host/` | the PC port: spinlock, a table-driven console, the HIL link on a tty (`hil_host`), XCP for the PC build |
| `rte/` | the controller runtime: parameter table with limits, run/stop/clear and trips, the hand-over to the tick, calibration page mapping, console commands |
| `hil/` | the HIL link: frames `A5 5A, type, seq, n, n x f32, CRC-16`, a resyncing parser, the HELLO name check, the newest plant frame passed lock-free to the tick |
| `esp/espglue/` | the ESP-IDF + FreeRTOS port of the OSAL: a fixed-rate task on a hardware timer (timing pin, watchdog, period and execution statistics), CSV telemetry drained by a low-priority task, the UART0 console, the HIL link on UART2 |
| `esp/xcpglue/` | XCP over Wi-Fi ([../controllers/README.md](../controllers/README.md#xcp)) |
| `esp/xcplite/` | PlatformIO wrapper around the XCPlite submodule ([below](#vendored-code)) |
| `vendor/` | third-party code |

## Plant models

The simulated hardware the controllers are tested against. Each plant is plain C (no OS, no
allocation) and runs in three places: compiled into the ESP32 firmware, which simulates its own
plant; in the PC tools and tests (`scenarios.c`, the Unity tests); and as an FMI co-simulation
FMU, for the runner on the PC and the Pi, FMPy and any other FMI tool.

| Plant | Library | FMU |
|---|---|---|
| DC motor (N20-class gearmotor on a PWM driver) | `plants/dcmotor/` (`plant_sim.h`) | `dcmotor` |
| T12 soldering tip (thermal network, heater, thermocouple front end) | `plants/t12/` (`t12_plant.h`) | `t12tip` |

The FMUs wrap the same C files in the Reference FMUs framework (submodule
`vendor/Reference-FMUs`). Each comes as FMI 3.0 and FMI 2.0 co-simulation, with binaries for
`x86_64-linux` and `aarch64-linux` and with the C sources (`buildDescription.xml`, `all.c`), so
it compiles for other targets too. The plant takes one exact step per 1 ms solver step; the
communication step is any multiple of 1 ms. Parameters are fixed (set before the start, physical
values only). All variables are Float64 in SI units, temperatures in degC. Building them for both
platforms: [../BUILDING.md](../BUILDING.md#plant-fmus).

**dcmotor**

| Variable | Causality | Unit | Meaning |
|---|---|---|---|
| `duty` | input | | PWM duty, -1..1 (clamped), sign = direction |
| `load_torque` | input | N.m | load on the shaft |
| `fault` | input | | 0 none, 1 encoder lost: `speed_rpm` (the encoder reading) is NaN, `speed` stays the shaft's |
| `speed`, `speed_rpm` | output | rad/s, rpm | shaft speed |
| `current` | output | A | armature current |
| `R`, `L`, `Kt`, `Ke`, `J`, `b` | parameter | | motor constants (`PLANT_PARAMS_N20`) |
| `Vbus` | parameter | V | supply behind the driver (6 V) |

**t12tip**

| Variable | Causality | Unit | Meaning |
|---|---|---|---|
| `heater` | input | | heater switch Q1, on when >= 0.5 |
| `series_on` | input | | series switch Q3 held by the heartbeat, on when >= 0.5 (default 1) |
| `ambient_c` | input | degC | room temperature, also the start temperature (default 25) |
| `load_wk` | input | W/K | heat drawn by touching a joint |
| `fault` | input | | 0 none, 1 open (no tip), 2 shorted tip, 3 heater switch stuck on |
| `v_amp` | output | V | thermocouple amplifier output: clamped while heating, settles after switch-off |
| `i_heater` | output | A | heater current of the last step |
| `t_heater_c`, `t_tip_c`, `t_handle_c` | output | degC | node temperatures; the handle NTC reads `t_handle_c` |
| `c_h` ... `tau_sense` | parameter | | the 15 values of `T12_PARAMS_DEFAULT` |

**Checks:** `<plant>/fmu/test_fmu.c` steps the FMU against the plant library directly
(bit-identical at every step, also on aarch64 under qemu; for the T12 every fault and the numpy
reference rows of `test/test_t12_plant/golden.h`; a 10 ms step equals ten 1 ms steps);
`plants/tools/check_fmu.py` has FMPy validate the FMU, run it and rebuild it from its sources
alone with the same result; the runner's `fmpy_<plant>` and `golden_<plant>` tests.

**Adding a plant:**

1. The plant as plain C in `plants/<name>/` (`include/`, `src/`, `library.json`,
   `CMakeLists.txt`), with tests in `test/`.
2. `plants/<name>/fmu/`: `config.h` (value references, model data, `FIXED_SOLVER_STEP`),
   `model.c` (start values, get/set, one plant step in `eventUpdate`), `FMI3.xml`, `FMI2.xml`,
   `buildDescription.xml` and `test_fmu.c`. The two existing plants are the templates.
3. One `add_plant_fmu(...)` call in `plants/CMakeLists.txt`.

## Vendored code

| Folder | Project | Version | License |
|---|---|---|---|
| `vendor/unity/` | [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity) | `v2.6.1` (`src/`), as PlatformIO's | MIT |
| `vendor/Reference-FMUs/` | submodule, [modelica/Reference-FMUs](https://github.com/modelica/Reference-FMUs) | v0.0.42 | BSD-2-Clause |
| `vendor/XCPlite/` | submodule, [n1sk4/XCPlite](https://github.com/n1sk4/XCPlite) branch `krumpir` | upstream `da32477` (V2.3.2) | MIT |

- XCPlite's fork has one change: `src/platform.h` pins the XCP server tasks to core
  `OPTION_FREERTOS_CORE` on ESP-IDF (marked `krumpir patch`). To move to a newer XCPlite, rebase
  the `krumpir` branch onto the upstream release and update the submodule. `esp/xcplite` builds
  only the 8 sources of a UDP server on FreeRTOS, with forwarding headers (a global include path
  to its `src/` would shadow ESP-IDF's `platform.h`) and `xcp_sections.py` for its linker
  sections; the projects set `lib_ignore = vendor`, so the submodule builds only through it.
- The runner's vendored code: [../runner/README.md](../runner/README.md#vendored-code). The TUI's
  libraries and the Reference FMUs release are downloaded, pinned in `../cmake/versions.cmake`.

| File | sha256 |
|------|--------|
| `vendor/unity/unity.c` | `b90e735a54cf3b3765ab6caa955d11a1488ee73d9c6152cdc98576c2d17cb871` |
| `vendor/unity/unity.h` | `9db174d3c2c6424fd35c0980c5941d124c5ebb0f48e8172f997a2aa9554b64ea` |
| `vendor/unity/unity_internals.h` | `fcd8b3f6b412ac0ab599547eb8a30b6d7f3f0af77aab31f7a1822a2a8fc9a2b2` |
