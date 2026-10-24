# esp32-motor-controller

A PID speed controller for a DC motor, running on an ESP32 (ESP32-WROOM-32 DevKit) in a
deterministic 1 kHz loop. It is the device under test for the Krumpir HIL bench.

- Control logic lives in `lib/control/` as portable C (no ESP-IDF or FreeRTOS includes), so it is
  unit tested on the PC with Unity and reused unchanged on the target.
- A small I/O abstraction lets the same controller run against a simulated plant, a real motor,
  or Krumpir.
- Planned: runtime config and CSV telemetry over the USB console, measured loop timing, and
  step-response results.

**Status:** Phase 3 done: portable PID, simulated DC motor and I/O backend, unit tested on the PC.
The firmware runs the closed loop on the ESP32 against the simulated motor (scripted 0 -> 3000 rpm
step). On-target timing measurements (Phase 2) and the console/telemetry (Phase 4) are still open.

## Layout

```
platformio.ini     envs: esp32dev (firmware), native (PC tests)
CMakeLists.txt     ESP-IDF top level
sdkconfig.defaults tick 1000 Hz, console 115200 baud, task watchdog on
src/               ESP32-specific code (main.c)
lib/control/       portable C library: pid, plant_sim, io_sim, loop_stats, io.h
test/              Unity tests
tools/             host tools: step_response.c (CSV), plot.py (plot)
docs/              results (step response CSV and plot)
requirements.txt   Python packages for tools/
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

## Python (host tools)

Python 3.13 (developed with 3.13.5). Packages are listed in `requirements.txt` (currently only
`matplotlib==3.10.3`). Set up a virtual environment in this folder:

```
python -m venv .venv
.venv\Scripts\activate           # Windows (bash: source .venv/Scripts/activate)
pip install -r requirements.txt
```

`.venv/` is git-ignored. Python is not needed for the firmware or the unit tests.

## Step response

Simulates PID + motor on the PC and plots it (needs gcc and the venv above):

```
gcc -Ilib/control/include tools/step_response.c lib/control/src/pid.c lib/control/src/plant_sim.c lib/control/src/io_sim.c -lm -o .pio/step_response.exe
.pio/step_response.exe > docs/step_response.csv
python tools/plot.py docs/step_response.csv docs/step_response.png
```

## Pinned versions

- `espressif32` platform 6.9.0 (bundles ESP-IDF 5.3.1)
- Unity 2.6.1 (fetched by PlatformIO for the `native` env)

## Notes

- Without a first git commit, the ESP-IDF build fails looking up the git revision, which is why
  `CMakeLists.txt` sets `PROJECT_VER`.
- Keep `lib/control/` free of ESP-IDF includes.
