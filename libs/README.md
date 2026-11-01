# Shared libraries

Plain C, no framework, used by the controllers (on the chip and in host tests) and by the plant
FMUs.

| Library | Contents |
|---------|----------|
| `ctrlcore/` | PID, ring buffer, loop statistics, step-response figures, parameter parsing |
| `plants/dcmotor/`, `plants/t12/` | plant models; also packaged as FMUs (`plants/README.md`) |
| `esp/` | ESP32 glue (tick task, console, telemetry, XCP); ESP-IDF only |
| `vendor/` | third-party code (see below) |

## Building

Each portable library (`ctrlcore`, `plants/*`, and the controllers' `lib/<name>`) builds three ways
from the same sources:

- **PlatformIO**, from its `library.json` (the controllers' `platformio.ini`).
- **Any CMake project**: `CMakeLists.txt` defines a static library target of the same name.
  `cmake/krumpir.cmake` adds `ctrlcore`, `dcmotor`, `t12` and `unity` with `krumpir_add_libs()`.
- **ESP-IDF component**: the same `CMakeLists.txt` calls `idf_component_register()` when
  `ESP_PLATFORM` is set (list the folder in `EXTRA_COMPONENT_DIRS`).

The Unity tests run without PlatformIO too:

```sh
cmake -S libs -B build/libs && cmake --build build/libs && ctest --test-dir build/libs
cmake -S controllers/esp32-t12-controller/test -B build/t12 && cmake --build build/t12 && ctest --test-dir build/t12
```

`pio test -e native` runs the same tests.

## Vendored code

| Directory | Project | Version | License |
|-----------|---------|---------|---------|
| `vendor/unity/` | [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity) | tag `v2.6.1` (`src/`), the version PlatformIO uses | MIT (`vendor/unity/LICENSE.txt`) |
| `vendor/XCPlite/` | submodule, see `esp/xcplite/README.md` | | MIT |
| `vendor/Reference-FMUs/` | submodule, see `plants/README.md` | | BSD-2-Clause |

| File | sha256 |
|------|--------|
| `vendor/unity/unity.c` | `b90e735a54cf3b3765ab6caa955d11a1488ee73d9c6152cdc98576c2d17cb871` |
| `vendor/unity/unity.h` | `9db174d3c2c6424fd35c0980c5941d124c5ebb0f48e8172f997a2aa9554b64ea` |
| `vendor/unity/unity_internals.h` | `fcd8b3f6b412ac0ab599547eb8a30b6d7f3f0af77aab31f7a1822a2a8fc9a2b2` |
