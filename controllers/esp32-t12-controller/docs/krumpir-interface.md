# Krumpir interface

How a test bench (Krumpir or any host with a serial port) drives and observes the T12 controller.

## Summary

| Item | Value |
|---|---|
| Control link | UART0 through the USB-serial bridge, 115200 baud, 8N1, no flow control |
| Commands | text lines at the `t12> ` prompt |
| Telemetry | CSV rows every heater cycle (about 10 Hz), enabled with `log on` |
| Loop timing pin | GPIO 23, high while the control loop body runs |
| Control loop | 1 kHz, period 1 ms; one heater cycle is 100 ticks |
| Tip | simulated inside the firmware (no physical heater or sensor pins yet) |

The firmware boots stopped, setpoint 350 C, gains `kp 0.05 ki 0.05 kd 0`, ramp off, simulated
load 0, no injected fault, ambient 25 C, no trip. Every setting is lost on reset; nothing is stored
in flash.

## Serial protocol

Send one command per line, ending in `\n`. Several commands on one line are not supported: the
first word is the command and the rest are its arguments. The device answers with lines ending in
`\r\n` (the console may also echo the typed characters), then prints the prompt `t12> `. A command
that fails prints a line starting with `error:` or `usage:` and changes nothing; the console then
also prints `Command returned non-zero error code: 0x1 (ERROR)`.

| Command | Argument | Range | Effect |
|---|---|---|---|
| `sp` | C | 100 to 450 | temperature setpoint |
| `kp`, `ki`, `kd` | value | 0 to 1000 | PID gains (duty per K, per K*s, per K/s) |
| `ramp` | K/s | 0 to 100 | rate limit for setpoint changes; 0 = jump to the new setpoint |
| `start` | | | run the controller; refused while a trip is latched |
| `stop` | | | duty 0 and PID reset |
| `clear` | | | clear a latched trip |
| `status` | | | print state (below); also restarts the loop-timing window |
| `log` | `on` or `off` | | CSV telemetry on or off |
| `load` | W/K | 0 to 1 | simulation only: heat drawn by touching a joint (0.1 is about 32 W at 350 C) |
| `ambient` | C | -20 to 60 | simulation only: room temperature |
| `fault` | `none`, `open`, `short` or `stuck` | | simulation only: inject a tip fault |

Rejected input examples:

```
error: hot is not a number in [100, 450]
error: trip (no tip) is latched, use clear first
usage: fault none|open|short|stuck
usage: log on|off
```

### `status` output

```
running  sp 350 C  kp 0.05 ki 0.05 kd 0  ramp 0 K/s
trip none  sim load 0 W/K  fault none  ambient 25 C
tip 350.3 C  handle 31.0 C  duty 0.161  heater 3.00 A  log off  telemetry dropped 0
loop (since last status): period ns min/mean/max 993200/1000000/1009637  exec ns min/mean/max 19562/22156/75787  overruns 0  n=99100
```

- State is `running` or `stopped`. The run flag drops by itself when a trip stops the controller.
- Trip is `none` or one of the names in the safety table below.
- `tip` is the temperature at the thermocouple junction, which sits on the heater: under load the
  tip itself is cooler by several kelvin. `handle` is the handle NTC. Both read 0.0 until the first
  measurement (about 0.1 s after boot) and `nan` when a reading is invalid.
- `duty` is the heating time of the last cycle as a fraction of the longest allowed heating phase,
  0 to 1: the heater is on for at most 93 of the 100 ticks, so 1.000 means 93 ms of heating.
- `heater` is the heater current seen while heating in the last cycle, 0 when the cycle did not
  heat or heated for fewer than two ticks.
- The `loop` line appears once the control loop has run since the previous `status`. `n` is the
  number of ticks in that window.
- `telemetry dropped` counts samples lost because the host did not drain the stream fast enough.

## Telemetry

`log on` prints the header once, then one row per heater cycle:

```
t_ms,setpoint_c,temp_c,duty_permille,current_ma
43322,350,350,172,2999
43421,350,350,161,2999
```

| Column | Unit | Meaning |
|---|---|---|
| `t_ms` | ms | control-loop tick count at the end of the measurement (1 tick = 1 ms) |
| `setpoint_c` | C | setpoint in use after the ramp, rounded to an integer |
| `temp_c` | C | measured tip temperature, rounded; -1 when the reading is invalid |
| `duty_permille` | 1/1000 | heating fraction of the cycle, 0 to 1000 |
| `current_ma` | mA | heater current while heating, 0 when not heating |

The rows are one heater cycle apart, but the timestamp moves with the heating time: the
measurement happens right after the heater switches off, so consecutive rows are typically 99 and
101 ms apart when the duty toggles by one tick, and further apart while the duty changes quickly.
Command replies and the prompt
share the same serial stream, so a parser should keep only lines that match
`^\d+,\d+,-?\d+,\d+,\d+$`. Up to 128 samples are buffered; the `telemetry dropped` count in `status`
shows if the host fell behind.

## Timing signal

GPIO 23 goes high at the start of each control tick and low when the tick's work is done, so:

- the rising-edge period is the loop period (1 ms nominal);
- the pulse width is the execution time (about 20 to 25 us on average, 75 to 100 us at worst,
  including the simulated tip, which runs in double precision).

Measured on the board over the console: period 1000 us, +-15 us, 0 overruns. The timing is
measured with the CPU cycle counter; an external capture of this pin is an independent check.

## Safety behavior the bench can expect

A trip stops the controller, sets the duty to 0 and stops the heartbeat that holds the series
switch on, so on the real board the heater loses power within 100 ms even if the first switch
fails. It stays latched until `clear`, and `start` is refused in the meantime. Trips are checked
once per heater cycle, so they appear within about 0.2 s of the cause.

| Trip | Condition |
|---|---|
| `over temperature` | a reading above 480 C |
| `heater stuck on` | more than 0.5 A while the heater is commanded off |
| `heater short` | 24 V divided by the heating current below 6 ohm |
| `no tip` | less than 0.3 A while heating |
| `heater resistance` | 24 V divided by the heating current above 10 ohm |
| `sensor timeout` | 5 invalid temperature readings in a row; a single one is ignored |
| `thermal runaway` | 20 s at 90 % duty or more without a 50 K rise |

Other behavior:

| Condition | Result |
|---|---|
| `stop` | duty 0, PID reset; the simulated tip cools |
| missing tip while stopped | not detected; the check needs current, so it trips within one cycle after `start` |
| control loop stops running for 1 s | task watchdog resets the board; it comes back stopped with the defaults |

## Typical test sequence

1. Open the port and send `log off`, `stop`, `clear`, `load 0`, `fault none`, then `status` and
   check `trip none` and a tip reading near room temperature.
2. Send `sp 350`, `start`, wait about 6 s: the tip reads 350 C +-1, duty about 0.16 and the heater
   current 3.00 A.
3. Disturbance: `load 0.1`; the tip dips by about 5 C at most and recovers within about 2 s, duty
   rises to about 0.6.
4. Fault: `load 0`, `fault open`; within 0.2 s `status` shows `trip no tip`, `stopped`, heater 0.00 A.
   `start` is refused.
5. Recover: `fault none`, `clear`, `start`.
6. Stuck switch: `fault stuck` trips `heater stuck on`; `fault none`, `clear` to recover.
7. Runaway: `load 0.5`, `start`; after about a minute `status` shows `trip thermal runaway`.
   Then `load 0`, `clear`.

## Board I/O

The measurements and the heater command go through the `t12_io_t` port in
`lib/t12ctrl/include/t12_io.h`. All readings are raw 12-bit ADC counts:

| Function | Meaning |
|---|---|
| `heater_set` | heater switch Q1 on or off |
| `tc_read` | thermocouple amplifier output |
| `ntc_read` | handle NTC divider |
| `current_read` | heater current sense (INA240) |
| `heartbeat_toggle` | keeps the series switch Q3 on |
| `safe_state` | heater off |

The firmware currently uses the simulated tip (`t12_io_sim`). Physical pins for the heater driver,
the sensors and the heartbeat are not implemented yet. The intended circuit is described in
[../../../docs/T12 Controller Design.md](../../../docs/T12%20Controller%20Design.md); using it means
writing another `t12_io_t` backend, with the controller and the tests unchanged.
