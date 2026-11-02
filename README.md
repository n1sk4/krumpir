# Krumpir

X-in-the-loop test bench for embedded controllers.

| | |
|---|---|
| **Controllers** | [![esp32-motor-controller](https://github.com/n1sk4/krumpir/actions/workflows/esp32-motor-controller.yml/badge.svg)](https://github.com/n1sk4/krumpir/actions/workflows/esp32-motor-controller.yml) [![esp32-t12-controller](https://github.com/n1sk4/krumpir/actions/workflows/esp32-t12-controller.yml/badge.svg)](https://github.com/n1sk4/krumpir/actions/workflows/esp32-t12-controller.yml) |
| **Plants** | [![plants](https://github.com/n1sk4/krumpir/actions/workflows/plants.yml/badge.svg)](https://github.com/n1sk4/krumpir/actions/workflows/plants.yml) |
| **Runner** | [![runner](https://github.com/n1sk4/krumpir/actions/workflows/runner.yml/badge.svg)](https://github.com/n1sk4/krumpir/actions/workflows/runner.yml) |
| **Tech** | ![C](https://img.shields.io/badge/C-A8B9CC?logo=c&logoColor=black) ![ESP32](https://img.shields.io/badge/ESP32-E7352C?logo=espressif&logoColor=white) ![PlatformIO](https://img.shields.io/badge/PlatformIO-F5822A?logo=platformio&logoColor=white) ![CMake](https://img.shields.io/badge/CMake-064F8C?logo=cmake&logoColor=white) ![FMI](https://img.shields.io/badge/FMI-3.0-4C9A2A) ![Python](https://img.shields.io/badge/Python-3776AB?logo=python&logoColor=white) ![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-A22846?logo=raspberrypi&logoColor=white) ![KiCad](https://img.shields.io/badge/KiCad-314CB0?logo=kicad&logoColor=white) ![GitHub Actions](https://img.shields.io/badge/GitHub%20Actions-2088FF?logo=githubactions&logoColor=white) |

![The Krumpir bench](docs/img/bench-iso.svg)

## What it is

Krumpir is a low-cost bench for testing an embedded controller against a simulated plant instead of
the real hardware. The plant runs as an FMI 3.0 co-simulation FMU, stepped by a real-time runner.
An I/O stage connects the runner to the device under test, which sees the simulated plant as if it were the real one.
The same controller code also runs against the same plant on the PC, so a test can climb from software-in-the-loop to
hardware-in-the-loop without changing the controller.

The repository contains:

- **Controllers**: the firmware under test ([controllers/](controllers/README.md)).
- **Shared libraries**: portable C for the controllers and the plant models, which are also packaged as FMUs ([libs/](libs/README.md)).
- **Runner**: the real-time FMU runner and its model-access service ([runner/](runner/README.md)).
- **Tools**: a Python client for scripting tests, controller code generation, serial capture and
  timing scripts, and schematic generation ([tools/](tools/)).
- **Docs**: ([docs/](docs/)).