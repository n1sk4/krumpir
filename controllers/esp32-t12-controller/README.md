# esp32-t12-controller

A temperature controller for a T12 / T15 soldering tip, running on an ESP32 (ESP32-WROOM-32 DevKit)
in a deterministic 1 kHz control loop with a 100 ms heater cycle. Like the motor controller, it is
meant as a device under test for the Krumpir HIL bench. The tip, its thermocouple front end and the
protected 24 V power stage are described in
[../../docs/T12 Controller Design.md](../../docs/T12%20Controller%20Design.md).

**Status:** the controller runs against a simulated tip inside the firmware. No heater, thermocouple
or power stage is connected yet; the pins for them are not driven. The intended circuit is the KiCad
schematic in [hardware/](hardware/README.md).

- The control logic lives in `lib/t12ctrl/` and the shared building blocks (PID, ring buffer, loop
  statistics, parameter parsing, step metrics) in `libs/ctrlcore/`, as portable C (no ESP-IDF or
  FreeRTOS includes), so they are unit tested on the PC with Unity and reused unchanged on the
  target. The tip model is `libs/plants/t12`; the ESP-IDF glue (tick task, telemetry, console
  helpers) is shared with the other controllers in `libs/esp/espglue`.
- A small I/O abstraction (`t12_io_t`: heater, thermocouple, handle NTC, heater current, heartbeat)
  lets the same controller run against the simulated tip or, later, the real board. All readings
  are raw ADC counts. The firmware currently ships with the simulated tip.
- One heater cycle lasts 100 ms: heat, wait for the switching artifact to settle, average the
  thermocouple while the heater is off, then update the PID.
- A serial console sets the setpoint and gains, starts/stops the controller and injects tip
  faults. CSV telemetry streams at 10 Hz.
- Safety: latched trips for over temperature, a stuck heater switch, a short, a missing tip, a
  wrong heater resistance, invalid readings and thermal runaway. A trip also stops the heartbeat
  that keeps the series switch on, and a task watchdog resets the board if the loop hangs.

## Initial setup (Windows)

1. **PlatformIO**: install the VS Code "PlatformIO IDE" extension, or `pip install platformio`.
   The CLI is `~\.platformio\penv\Scripts\pio.exe`; add that folder to your PATH to use `pio`.
2. **Host C compiler** (needed for the `native` tests and the simulation tools): install MinGW-w64
   gcc, e.g. `winget install BrechtSanders.WinLibs.POSIX.UCRT`, and make sure `gcc --version`
   works in a new terminal.
3. **USB driver** for the DevKit's serial chip (CP210x or CH340, depending on the board) if the
   board does not show up as a COM port.

### Python (host tools)

Python 3.13. Packages are listed in `requirements.txt`. Set up a virtual environment in this folder:

```
cd krumpir\controllers\esp32-t12-controller
python -m venv .venv
.venv\Scripts\activate           # Windows (bash: source .venv/Scripts/activate)
pip install -r requirements.txt
```

## Build, test, flash

Run from this folder:

```
pio test -e native                    # unit tests on the PC
pio run -e esp32dev                   # build firmware
pio run -e esp32dev -t upload         # flash the board
pio device monitor                    # serial console, 115200 baud
```

The shared libraries have their own tests: `pio test -e native` in `../../libs`.

## Using the controller

Open the serial monitor (`pio device monitor -p COM4`) and type commands at the `t12>` prompt.
The firmware boots stopped, setpoint 350 C, with the tuned gains. Settings are lost on reset.

| Command | Effect |
|---|---|
| `sp <degC>` | setpoint, 100 to 450 |
| `kp` / `ki` / `kd <value>` | gains, finite and >= 0 |
| `ramp <K/s>` | setpoint ramp rate, 0 = off |
| `start` / `stop` | run the controller / duty 0 and PID reset |
| `load <W/K>` | simulation only: heat drawn by touching a joint, 0 to 1 |
| `ambient <degC>` | simulation only: room temperature, -20 to 60 |
| `fault none/open/short/stuck` | simulation only: inject a tip fault |
| `clear` | clear a latched trip; `start` is refused until then |
| `status` | state, trip, tip and handle temperature, duty, heater current, loop timing since the last `status` |
| `log on` / `log off` | CSV telemetry at 10 Hz: `t_ms,setpoint_c,temp_c,duty_permille,current_ma` |

Example: `sp 350`, `start`, then `status` shows the tip settling at 350 C after about 5 s.

### Check the loop timing

Close the monitor first (the port can only be open once). The check starts the controller, reads
the loop stats from `status` and prints PASS or FAIL:

```
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 350" "start" --teardown "stop" --rate-hz 10 --max-exec-us 200 --log
```

### Capture and plot a run

```
python ../../tools/serial/capture.py --port COM4 --duration 10 --cmd "sp 350" "start" -o docs/capture.csv
python tools/plot.py docs/capture.csv docs/cold_start_target.png
```

### Simulate on the PC

The same controller, tip model and safety supervisor run on the PC (needs gcc and the venv above):

```
gcc -Ilib/t12ctrl/include -I../../libs/ctrlcore/include -I../../libs/plants/t12/include tools/scenarios.c lib/t12ctrl/src/t12_scenario.c lib/t12ctrl/src/t12_loop.c lib/t12ctrl/src/t12_ctrl.c lib/t12ctrl/src/t12_cycle.c lib/t12ctrl/src/t12_convert.c lib/t12ctrl/src/t12_safety.c lib/t12ctrl/src/t12_io_sim.c ../../libs/ctrlcore/src/pid.c ../../libs/ctrlcore/src/step_metrics.c ../../libs/plants/t12/src/t12_plant.c -lm -o .pio/scenarios.exe
.pio/scenarios.exe cold > docs/scenario_cold.csv
python tools/plot.py docs/scenario_cold.csv docs/scenario_cold.png
```

Closed-loop scenarios (cold start, joint load, ambient change, each fault) and the gain sweep are
described in [docs/tuning.md](docs/tuning.md).

## Results

Cold start to 350 C (the board runs the simulated tip, so this compares the firmware on the ESP32
with the same code on the PC):

| On the board | Simulated on the PC |
|:---:|:---:|
| ![Cold start on the board](docs/cold_start_target.png) | ![Cold start in simulation](docs/scenario_cold.png) |

On the board the tip reaches 350 C about 5.7 s after heating starts (6.0 s in the simulation) and
then holds 350 to 351 C with a duty of about 0.16 and a heater current of 3.00 A.

Closed-loop scenarios (simulation):

| Joint load | Missing tip | Thermal runaway |
|:---:|:---:|:---:|
| ![Joint load](docs/scenario_load.png) | ![Missing tip](docs/scenario_open.png) | ![Thermal runaway](docs/scenario_runaway.png) |

## Hardware

Intended circuit (the firmware currently simulates the tip; details, pin table and the values to
check in [hardware/README.md](hardware/README.md)):

![Schematic](hardware/t12-controller.svg)

## Documentation

- [docs/tuning.md](docs/tuning.md): tip model, gain selection, scenarios and targets, trips.
- [docs/krumpir-interface.md](docs/krumpir-interface.md): what a test bench needs to drive and observe the controller.
- [hardware/README.md](hardware/README.md): KiCad schematic and pin table for the protected 24 V power stage.
- [tools/README.md](tools/README.md): host scripts, including the timing check.
- [../../docs/T12 Controller Design.md](../../docs/T12%20Controller%20Design.md): the circuit design and its fault table.
- [../../libs/esp/espglue/README.md](../../libs/esp/espglue/README.md): the ESP-IDF glue shared by all controllers.
