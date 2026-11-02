# Controllers

Firmware for the devices under test of the Krumpir HIL bench. Each controller is an ESP32
(ESP32-WROOM-32 DevKit) running a deterministic 1 kHz control loop against a simulated plant.

| Controller | What it controls |
|---|---|
| [esp32-motor-controller](esp32-motor-controller/README.md) | PID speed control of a DC motor |
| [esp32-t12-controller](esp32-t12-controller/README.md) | temperature control of a T12 / T15 soldering tip |

## Shared design

- The control logic is portable C with no ESP-IDF or FreeRTOS includes (`lib/` in each controller,
  building blocks in [../libs/ctrlcore](../libs/ctrlcore), plant models in `../libs/plants`). It is
  unit tested on the PC with Unity and reused unchanged on the target.
- The ESP-IDF glue (tick task, telemetry, console helpers) is shared in
  [../libs/esp/espglue](../libs/esp/espglue/README.md).
- An I/O abstraction lets the same controller run against a simulated plant, a real one, or Krumpir.
  The firmware currently ships with the simulated plant.
- A task watchdog resets the board if the control loop hangs; safety trips are latched until `clear`.

## Initial setup (Windows)

1. **PlatformIO**: install the VS Code "PlatformIO IDE" extension, or `pip install platformio`.
   The CLI is `~\.platformio\penv\Scripts\pio.exe`; add that folder to your PATH to use `pio`.
2. **Host C compiler** (for the `native` tests and the PC simulations): install MinGW-w64 gcc,
   e.g. `winget install BrechtSanders.WinLibs.POSIX.UCRT`, and check `gcc --version` in a new terminal.
3. **USB driver** for the DevKit's serial chip (CP210x or CH340) if the board does not show up as a COM port.
4. **Python 3.13 venv**, one per controller, created in the controller folder (packages in its
   `requirements.txt`: matplotlib for plots, pyserial for the board tools):
   ```
   python -m venv .venv
   .venv\Scripts\activate           # bash: source .venv/Scripts/activate
   pip install -r requirements.txt
   ```

## Build, test, flash

Run from the controller folder:
```
pio test -e native                    # unit tests on the PC
pio run -e esp32dev                   # build firmware
pio run -e esp32dev -t upload         # flash the board
pio device monitor -p COM4            # serial console, 115200 baud
```
The shared libraries have their own tests: `pio test -e native` in `../libs`.

## Serial console

The firmware boots stopped, with the tuned gains; settings are lost on reset. These commands exist
in every controller (the rest are listed in its README):

| Command | Effect |
|---|---|
| `sp <value>` | setpoint |
| `kp` / `ki` / `kd <value>` | gains, finite and >= 0 |
| `start` / `stop` | run the controller / output 0 and PID reset |
| `clear` | clear a latched trip; `start` is refused until then |
| `status` | state, trip, measurements, loop timing since the last `status` |
| `log on` / `log off` | CSV telemetry |

The port can only be open once: close the monitor before running the board scripts in
`../tools/serial` (`capture.py` records the CSV into a file, `timing_check.py` measures the loop
timing, PASS or FAIL). Their commands are in each controller's `tools/README.md`.

## XCP measurement and calibration

The env `esp32dev-xcp` builds the same firmware with XCP on Ethernet over Wi-Fi: every variable of
the control loop can be measured at 1 kHz, and setpoint and gains calibrated through a calibration
page. Setup, the `xcpclient` commands and the measured cost on the loop are in
[../libs/esp/xcpglue/README.md](../libs/esp/xcpglue/README.md).
