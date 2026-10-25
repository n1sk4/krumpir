# esp32-motor-controller

A PID speed controller for a DC motor, running on an ESP32 (ESP32-WROOM-32 DevKit) in a
deterministic 1 kHz loop. It is the device under test for the Krumpir HIL bench.

- Control logic lives in `lib/control/` as portable C (no ESP-IDF or FreeRTOS includes), so it is
  unit tested on the PC with Unity and reused unchanged on the target.
- A small I/O abstraction lets the same controller run against a simulated plant, a real motor,
  or Krumpir.

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

Python 3.13. Packages are listed in `requirements.txt`. Set up a virtual environment in this folder:

```
cd krumpir\controllers\esp32-motor-controller
python -m venv .venv
.venv\Scripts\activate           # Windows (bash: source .venv/Scripts/activate)
pip install -r requirements.txt
```

## Step response

Simulates PID + motor on the PC and plots it (needs gcc and the venv above):

```
gcc -Ilib/control/include tools/step_response.c lib/control/src/pid.c lib/control/src/plant_sim.c lib/control/src/io_sim.c -lm -o .pio/step_response.exe
.pio/step_response.exe > docs/step_response.csv
python tools/plot.py docs/step_response.csv docs/step_response.png
```

## Console and telemetry

Open the serial monitor (`pio device monitor -p COM4`) and type commands at the `motor>` prompt:

| Command | Effect |
|---|---|
| `sp <rpm>` | setpoint, within +-10000 |
| `kp` / `ki` / `kd <value>` | gains, finite and >= 0 |
| `start` / `stop` | run the controller / duty 0 and PID reset |
| `load <Nm>` | simulation only: load torque on the shaft, within +-0.1 |
| `inertia <scale>` | simulation only: rotor inertia scale, 0.1 to 10 (1 = nominal) |
| `clear` | clear a latched fault (overspeed, sensor timeout); `start` is refused until then |
| `status` | state, fault, gains, speed, duty, loop timing since the last `status` |
| `log on` / `log off` | CSV telemetry at 100 Hz: `t_ms,setpoint_rpm,speed_rpm,duty_permille` |

The firmware boots stopped. Capture a step response from the PC (close the monitor first, the port
can only be open once) and plot it:

```
python tools/capture.py --port COM4 --duration 3 --cmd "start" "sp 3000" -o docs/capture.csv
python tools/plot.py docs/capture.csv docs/step_response_target.png
```

Gain selection, scenario targets and fault behavior are described in `docs/tuning.md`.
