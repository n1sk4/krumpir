# Plant models

The simulated hardware the controllers are tested against. Each plant is plain C (no OS, no
allocation) and is used in three places:

- **PiL:** compiled into the ESP32 firmware, which simulates its own plant (PlatformIO library).
- **SiL:** in the PC tools and tests (`scenarios.c`, `pio test -e native`).
- **FMU:** packaged as an FMI co-simulation FMU, for the runner on the PC and the Raspberry Pi,
  FMPy and any other FMI tool.

| Plant | Library | FMU |
|---|---|---|
| DC motor (N20-class gearmotor on a PWM driver) | `dcmotor/` (`plant_sim.h`) | `dcmotor` |
| T12 soldering tip (thermal network, heater, thermocouple front end) | `t12/` (`t12_plant.h`) | `t12tip` |

## FMUs

The FMUs are built from the same C files, with the
[Reference FMUs](https://github.com/modelica/Reference-FMUs) framework (submodule
`libs/vendor/Reference-FMUs`, v0.0.42) providing the FMI API around each plant's `fmu/model.c`.
Every FMU comes as FMI 3.0 and FMI 2.0 co-simulation, with binaries for `x86_64-linux` and
`aarch64-linux` and with the C sources (`buildDescription.xml` or `all.c`), so it can also be
compiled for other targets.

The plant takes one exact step per 1 ms solver step: the inputs set before a communication step
are held over it, and the outputs read after it are the state at its end. The communication step
can be any multiple of 1 ms. Parameters are fixed: they can be set only before the simulation
starts, and only to physical values. All variables are Float64 with SI units (temperatures in
degC).

### dcmotor

| Variable | Causality | Unit | Meaning |
|---|---|---|---|
| `duty` | input | | PWM duty, -1..1 (clamped), sign = direction |
| `load_torque` | input | N.m | load on the shaft |
| `fault` | input | | 0 none, 1 encoder lost: `speed_rpm` (the encoder reading) is NaN, `speed` stays the shaft's |
| `speed`, `speed_rpm` | output | rad/s, rpm | shaft speed |
| `current` | output | A | armature current |
| `R`, `L`, `Kt`, `Ke`, `J`, `b` | parameter | | motor constants (`PLANT_PARAMS_N20`) |
| `Vbus` | parameter | V | supply behind the driver (6 V) |

### t12tip

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

## Building

On Linux or WSL (cmake, ninja, gcc; for the Pi binaries also `gcc-aarch64-linux-gnu` and
`qemu-user`; for the FMPy checks Python with `fmpy`, see `runner/tools/requirements.txt`):

```sh
git submodule update --init libs/vendor/Reference-FMUs
bash libs/plants/tools/build_fmus.sh            # both platforms, tested and merged
ls ~/build/plant-fmus/fmus/fmi3 ~/build/plant-fmus/fmus/fmi2
```

Or one platform only:

```sh
cmake -S libs/plants -B ~/build/plants -G Ninja && cmake --build ~/build/plants
ctest --test-dir ~/build/plants
```

The runner's CMake build also builds them and copies the FMI 3.0 FMUs to `runner/fmus/`, where
the scenarios `runner/scenarios/dcmotor.toml` and `t12tip.toml` load them.

## Checks

| Test | What it shows |
|---|---|
| `<plant>/fmu/test_fmu.c` (`dcmotor_test`, `t12tip_test`) | the FMU against the plant library stepped directly: bit-identical outputs at every step, also on aarch64 (under qemu); for the T12 also every fault and the numpy reference rows of `libs/test/test_t12_plant/golden.h`; parameter rules; a 10 ms step equals ten 1 ms steps |
| `tools/check_fmu.py` (`fmpy_<plant>_fmi3`, `_fmi2`) | FMPy validates the FMU, runs it, and rebuilds it from its sources alone with the same result |
| runner `fmpy_<plant>`, `golden_<plant>` | the runner and FMPy give the same result; the runner matches its golden file |

CI: `.github/workflows/plants.yml` builds, tests and merges both platforms and uploads the FMUs.

## Adding a plant

1. The plant as plain C in `libs/plants/<name>/` (`include/`, `src/`, `library.json`), with tests
   in `libs/test/`.
2. `libs/plants/<name>/fmu/`: `config.h` (value references, model data, `FIXED_SOLVER_STEP`),
   `model.c` (start values, get/set, one plant step in `eventUpdate`), `FMI3.xml`, `FMI2.xml`,
   `buildDescription.xml` and `test_fmu.c`. The two existing plants are the templates.
3. One `add_plant_fmu(...)` call in `libs/plants/CMakeLists.txt`.
