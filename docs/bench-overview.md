# Krumpir bench overview

A Raspberry Pi 4B runs a plant model as an FMU in hard real time, a Pico 2 handles the
timing-critical I/O, and an ESP32 controller is tested against the simulated plant as if it were
the real one.

FMI 3.0 co-simulation, 1 kHz fixed step, PREEMPT_RT, isolated core, 12-bit analog I/O.

## Signal loop

```
ESP32 (DUT, PID) --PWM--> Pico 2 (PIO capture) --SPI--> Pi 4B (FMU, PREEMPT_RT)
       ^                                                        |
       |                                                        v
       +----------ADC <---- MCP4822 DAC <--SPI-- Pico 2 <--SPI--+
```

The signals between the ESP32 and the Pico look to the ESP32 as a real motor would present them.
The signals between the Pico and the Pi stay inside the bench.

## Step budget

Design targets for one 1 ms simulation step, not measurements. The bench's job is to measure the
real values and show the slack holds.

| Item | Budget |
| --- | --- |
| Pico samples PWM and ADC | 30 µs |
| SPI inputs to Pi | 50 µs |
| RT thread wake-up (worst case) | 70 µs |
| FMU step | ≤ 250 µs |
| SPI outputs to Pico | 50 µs |
| DAC update | 20 µs |
| Slack | ≈ 530 µs |

## Scope

Included:

- Load one FMI 3.0 co-simulation FMU at startup.
- Fixed 1 kHz step on an isolated core with `SCHED_FIFO`.
- One PWM input and one analog output closing a single loop.
- Per-step timestamps logged to CSV for jitter analysis.
- CLI to load the FMU, set parameters, start and stop.
- The same FMU run in SIL on a PC with FMPy for comparison.

Not included: GUI or model editor, multiple FMUs or a co-simulation master, Model Exchange,
variable-step solvers, non-real variables, CAN, fault injection, test automation, bipolar ±10 V
outputs and signal conditioning.

## Demo: DC motor speed control

1. **Export the plant.** A DC motor model (inertia, friction, back-EMF) exported from OpenModelica
   as an FMI 3.0 FMU with C sources, compiled for aarch64.
2. **Run it in SIL.** The FMU and a PID copy run on a PC with FMPy, producing the reference speed
   response to a 0 → 2000 rpm setpoint step.
3. **Run it in HIL.** The same FMU runs on the Pi. The ESP32's PID firmware drives it and reads
   the speed back.
4. **Compare and measure.** Overlay the SIL and HIL speed curves and show logic-analyzer captures
   of the 1 kHz tick and loop latency.

## Acceptance

| Metric | How it is measured | Target |
| --- | --- | --- |
| Step jitter, worst case | Pico GPIO toggle per tick, logic analyzer, 1 h run | < 50 µs |
| Missed deadlines | Overrun counter in the RT thread | 0 in 1 h |
| Loop latency | PWM edge in → DAC change out | ≤ 2 steps |
| SIL vs HIL match | RMS speed error, setpoint step response | < 2 % FS |
| FMU step time | `clock_gettime` around `fmi3DoStep` | < 250 µs |

The parts list is in [hil-bom.md](hil-bom.md); the software design is in
[architecture.md](architecture.md).
