# esp32-t12-controller

Temperature controller for a T12/T15 soldering tip on an ESP32 (ESP32-WROOM-32 DevKit). The tip,
its thermocouple front end and the protected 24 V power stage are described in
[../../docs/T12 Controller Design.md](../../docs/T12%20Controller%20Design.md).

**Status:** the controller runs against a simulated tip inside the firmware. No heater, thermocouple
or power stage is connected yet; the pins for them are not driven.

- The control logic lives in `lib/t12ctrl/` as portable C (no ESP-IDF or FreeRTOS includes), so it
  is unit tested on the PC with Unity and reused unchanged on the target. The shared building blocks
  come from `../../libs` (`ctrlcore`: PID, ring buffer, loop statistics; `plants/t12`: the tip model).
- A small port (`t12_io_t`: heater, thermocouple, handle NTC, heater current, heartbeat) lets the same
  controller run against the simulated tip or, later, the real board. All readings are raw ADC counts.
- One heater cycle lasts 100 ms: heat, wait for the switching artifact to settle, average the
  thermocouple while the heater is off, then update the PID.
- A serial console sets the setpoint and gains and injects tip faults; CSV telemetry streams at 10 Hz.
- Safety: latched trips for over temperature, a stuck heater switch, a short, a missing tip, a wrong
  heater resistance, invalid readings and thermal runaway. A trip also stops the heartbeat that
  keeps the series switch on, and a task watchdog resets the board if the loop hangs.

| Item | Value |
|---|---|
| Control loop | 1 kHz, period 1 ms, timer-driven task on core 1 |
| Heater cycle | 100 ms, up to 93 ms of heating |
| Loop timing pin | GPIO 23, high while the loop body runs |
| Console | UART0 through the USB-serial bridge, 115200 baud, prompt `t12> ` |

## Console
| Command | Effect |
|---|---|
| `sp <degC>` | setpoint, 100 to 450 |
| `kp`, `ki`, `kd <value>` | PID gains |
| `ramp <K/s>` | setpoint ramp rate, 0 = off |
| `start`, `stop` | run the controller (`start` is refused while a trip is latched) |
| `clear` | clear a latched trip |
| `status` | state, readings, trip and loop timing |
| `log on` / `log off` | CSV telemetry: `t_ms,setpoint_c,temp_c,duty_permille,current_ma` |
| `load <W/K>` | simulation only: heat drawn by touching a joint |
| `ambient <degC>` | simulation only: room temperature |
| `fault none/open/short/stuck` | simulation only: inject a tip fault |

The firmware boots stopped, setpoint 350 degC, with the tuned gains. Settings are lost on reset.

## Build and test
```
pio test -e native            # unit tests on the PC (needs gcc)
pio run -e esp32dev           # build the firmware
pio run -e esp32dev -t upload
pio device monitor
```
The shared libraries have their own tests: `pio test -e native` in `../../libs`.

Scenario runs (cold start, joint load, ambient change, each fault), plots and the gain sweep are in
[docs/tuning.md](docs/tuning.md). The commands that regenerate them are in that file.
