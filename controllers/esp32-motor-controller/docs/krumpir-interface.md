# Krumpir interface

How a test bench (Krumpir or any host with a serial port) drives and observes the controller.

## Summary

| Item | Value |
|---|---|
| Control link | UART0 through the USB-serial bridge, 115200 baud, 8N1, no flow control |
| Commands | text lines at the `motor> ` prompt |
| Telemetry | CSV rows at 100 Hz, enabled with `log on` |
| Loop timing pin | GPIO 23, high while the control loop body runs |
| Control loop | 1 kHz, period 1 ms |
| Motor | simulated inside the firmware (no physical motor pins yet) |

The firmware boots stopped, setpoint 0, no fault, with the tuned gains. Every setting is lost on
reset; nothing is stored in flash.

## Serial protocol

Send one command per line, ending in `\n`. The device answers with lines ending in `\r\n`
(the console may also echo the typed characters), then prints the prompt `motor> `. A command that fails prints a line starting with
`error:` or `usage:` and changes nothing.

| Command | Argument | Range | Effect |
|---|---|---|---|
| `sp` | rpm | -10000 to 10000 | speed setpoint |
| `kp`, `ki`, `kd` | value | 0 to 1e6 | PID gains |
| `start` | | | run the controller; refused while a fault is latched |
| `stop` | | | duty 0 and PID reset |
| `clear` | | | clear a latched fault |
| `status` | | | print state (below); also restarts the loop-timing window |
| `log` | `on` or `off` | | CSV telemetry on or off |
| `load` | N*m | -0.1 to 0.1 | simulation only: load torque on the shaft |
| `inertia` | scale | 0.1 to 10 | simulation only: rotor inertia scale (1 = nominal) |

Rejected input examples:

```
error: safa is not a number in [-10000, 10000]
error: fault (overspeed) is latched, use clear first
usage: log on|off
```

### `status` output

```
running  sp 3000 rpm  kp 0.00072 ki 0.03 kd 0
fault none  sim load 0 N*m  inertia x1
speed 3000.0 rpm  duty 0.537  log off  telemetry dropped 0
loop (since last status): period ns min/mean/max 995718/1000000/1004237  exec ns min/mean/max 3081/3100/3993  overruns 0  n=5000
```

- State is `running` or `stopped`. Fault is `none`, `overspeed` or `sensor timeout`.
- `duty` is signed, -1 to +1.
- The `loop` line appears once the control loop has run since the previous `status`. `n` is the
  number of cycles in that window.
- `telemetry dropped` counts samples lost because the host did not drain the stream fast enough.

## Telemetry

`log on` prints the header once, then one row every 10 ms:

```
t_ms,setpoint_rpm,speed_rpm,duty_permille
0,3000,0,1000
10,3000,412,1000
```

| Column | Unit | Meaning |
|---|---|---|
| `t_ms` | ms | control-loop tick count (1 tick = 1 ms) |
| `setpoint_rpm` | rpm | setpoint in use, rounded to an integer |
| `speed_rpm` | rpm | measured speed, rounded to an integer |
| `duty_permille` | 1/1000 | actuator command, -1000 to +1000 |

Command replies and the prompt share the same serial stream, so a parser should keep only lines
that match `^\d+,-?\d+,-?\d+,-?\d+$` (see `tools/capture.py`). Up to 128 samples are buffered; the
`telemetry dropped` count in `status` shows if the host fell behind.

## Timing signal

GPIO 23 goes high at the start of each control cycle and low when the cycle's work is done, so:

- the rising-edge period is the loop period (1 ms nominal);
- the pulse width is the execution time (about 3 to 5 us idle, see [timing.md](timing.md)).

Measured period jitter is about +-4 us with a 0 overrun count. The timing is measured with the CPU
cycle counter; an external capture of this pin is an independent check.

## Safety behavior the bench can expect

| Condition | Result |
|---|---|
| `stop` | duty 0, PID reset |
| overspeed: speed beyond 7000 rpm in either direction | fault `overspeed` latched, controller stops, duty 0 |
| sensor timeout: 50 consecutive non-finite speed readings | fault `sensor timeout` latched, controller stops, duty 0 |
| control loop stops running for 1 s | task watchdog resets the board; it comes back stopped with defaults |

A latched fault stays until `clear`; `start` is refused in the meantime. A single bad speed reading
is ignored and the PID holds its last output. The sensor timeout cannot occur with the simulated
motor.

## Typical test sequence

1. Open the port and send `log off`, `stop`, `clear`, then `status` and check `fault none`.
2. Send `sp 3000`, `start`, then wait about 0.3 s: speed should read 3000 rpm +-0.5 %.
3. Disturbance: `load 0.001`; speed dips under 5 % and recovers within 0.3 s, duty rises.
4. Fault: `load -0.06`; `status` shows `fault overspeed`, `stopped`, duty 0. `start` is refused.
5. Recover: `load 0`, `clear`, `start`.
6. Timing: `python tools/timing_check.py --port COM4` gives PASS or FAIL against the loop limits.

## Motor I/O

The speed measurement and the duty output go through the `io_t` interface in
`lib/control/include/io.h`:

| Function | Meaning |
|---|---|
| `sensor_read` | speed in rpm |
| `actuator_write` | duty from -1 to +1 |
| `safe_state` | outputs to a safe state (duty 0) |

The firmware currently uses the simulated motor (`io_sim`). Physical pins for a real motor driver,
a speed sensor or Krumpir-injected signals are not defined yet; adding them means writing another
`io_t` backend, with the controller and the tests unchanged.
