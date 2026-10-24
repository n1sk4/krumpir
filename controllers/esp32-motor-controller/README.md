# esp32-motor-controller

A PID speed controller for a DC motor, running on an ESP32 (ESP32-WROOM-32 DevKit) in a
deterministic 1 kHz loop. It is the device under test for the Krumpir HIL bench.

- Control logic lives in `lib/control/` as portable C (no ESP-IDF or FreeRTOS includes), so it is
  unit tested on the PC with Unity and reused unchanged on the target.
- A small I/O abstraction lets the same controller run against a simulated plant, a real motor,
  or Krumpir.
- Planned: runtime config and CSV telemetry over the USB console, measured loop timing, and
  step-response results.

**Status:** Phase 0 (project foundation). The firmware only prints a hello line; the control
library, plant model and console come in later phases.

## Layout

```
platformio.ini     envs: esp32dev (firmware), native (PC tests)
CMakeLists.txt     ESP-IDF top level
sdkconfig.defaults tick 1000 Hz, console 115200 baud, task watchdog on
src/               ESP32-specific code (main.c)
lib/control/       portable C library (to be filled in Phase 1)
test/              Unity tests
tools/  docs/      host scripts and results (empty for now)
```

## Initial setup (Windows)

1. **PlatformIO**: install the VS Code "PlatformIO IDE" extension, or `pip install platformio`.
   The CLI is `~\.platformio\penv\Scripts\pio.exe`; add that folder to your PATH to use `pio`.
2. **Host C compiler** (needed only for `native` tests): install MinGW-w64 gcc, e.g.
   `winget install BrechtSanders.WinLibs.POSIX.UCRT`, and make sure `gcc --version` works in a
   new terminal.
3. **USB driver** for the DevKit's serial chip (CP210x or CH340, depending on the board) if the
   board does not show up as a COM port.

## Build, test, flash

Run from this folder:

```
pio test -e native                    # unit tests on the PC
pio run -e esp32dev                   # build firmware
pio run -e esp32dev -t upload         # flash the board
pio device monitor                    # serial monitor, 115200 baud
```

Expected output on the monitor: `esp32-motor-controller: hello` once per second.

## Pinned versions

- `espressif32` platform 6.9.0 (bundles ESP-IDF 5.3.1)
- Unity 2.6.1 (fetched by PlatformIO for the `native` env)

## Notes

- Without a first git commit, the ESP-IDF build fails looking up the git revision, which is why
  `CMakeLists.txt` sets `PROJECT_VER`.
- Keep `lib/control/` free of ESP-IDF includes.
