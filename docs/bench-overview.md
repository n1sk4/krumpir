# Krumpir bench overview

A Raspberry Pi 4B runs a plant model as an FMU in hard real time, and an ESP32 controller is tested
against the simulated plant: its own firmware and its own 1 kHz loop, with the plant's values in and
its commands out over a serial link.

FMI 3.0 co-simulation, 1 kHz fixed step, PREEMPT_RT, isolated core, UART link.

## Signal loop

```
ESP32 (DUT, PID, 1 kHz) --UART2: commands (CMD)----------> Pi 4B (runner: FMU, PREEMPT_RT, core 3)
        ^                                                        |
        +--------------- UART2: plant values (PLANT) <-----------+
```

Each step the runner sends the plant's outputs and applies the controller's newest commands
(frames, timing and the late-frame rules: `runner/docs/scenario-reference.md`, "The uart backend").
The controller reads the plant values where its on-chip simulation would be (`io_hil`), so its
firmware above the I/O layer is the one for the real hardware. The same loop runs on a PC with the
controller's code built for Linux, over a pty pair (`tools/hil/pc_loop.sh`).

## Step budget

Design targets for one 1 ms simulation step, and what the Pi measured so far.

| Item | Budget | Measured (Pi 4B) |
| --- | --- | --- |
| RT thread wake-up, worst case | 70 µs | 29 µs (cyclictest, 1 h under load) |
| FMU step | ≤ 250 µs | 0.25 µs mean, 5.5 µs max |
| PLANT frame on the line (921600 baud) | ≤ 500 µs | T12: 24 bytes, 0.26 ms; motor: 16 bytes, 0.17 ms |
| CMD frame back | ≤ 500 µs | T12: 16 bytes, 0.17 ms; motor: 12 bytes, 0.13 ms |

The frames travel while the next step runs: the controller's command answers the plant frame of the
step before, so the loop latency is two steps by design.

## Scope

Included:

- Load one FMI 3.0 co-simulation FMU at startup.
- Fixed 1 kHz step on an isolated core with `SCHED_FIFO`.
- The controller's real firmware in the loop over a UART: plant values in, commands out.
- Per-step timestamps logged to CSV for jitter analysis, and a GPIO line high while each step runs
  (`rt.timing_gpio`) for a logic analyzer.
- CLI, a TCP service and a terminal UI to load the FMU, set parameters, start and stop.
- The same FMU run in SIL on a PC with FMPy, and against the controller's on-chip simulation
  (`tools/hil/compare.py`).

Not included: the controller's own analog and PWM I/O path (the bench drives it over the UART
only), a GUI model editor, multiple FMUs or a co-simulation master,
Model Exchange, variable-step solvers, non-real variables, CAN, bipolar ±10 V outputs and signal
conditioning.

## Demo: DC motor speed control

1. **The plant.** A DC motor model (inertia, friction, back-EMF) as an FMI 3.0 FMU with C sources,
   built for aarch64 (`libs/plants/dcmotor`).
2. **Run it in SIL.** The FMU on a PC against FMPy, and the controller on its on-chip plant.
3. **Run it in HIL.** The same FMU runs on the Pi; the ESP32's PID firmware (`esp32dev-hil`) drives
   it over the UART and reads the speed back.
4. **Compare and measure.** `python3 tools/kb.py compare motor --board ...` overlays SIL and HIL and reports
   the error and the loop latency; logic-analyzer captures of the Pi's timing pin, the ESP32's
   (GPIO 23) and the UART lines show the tick and the latency.

## Acceptance

| Metric | How it is measured | Target |
| --- | --- | --- |
| Step jitter, worst case | `rt.timing_gpio` on the Pi, logic analyzer, 1 h run | < 50 µs |
| Missed deadlines | Overrun counter in the RT thread, with the controller in the loop | 0 in 1 h |
| Loop latency | a plant frame out (UART TX) to the command it caused back (UART RX); `compare.py --board` | ≤ 2 steps |
| SIL vs HIL match | RMS speed error, setpoint step response (`compare.py --board`) | < 2 % FS |
| FMU step time | `clock_gettime` around `fmi3DoStep` | < 250 µs |

The parts list is in [hil-bom.md](hil-bom.md); the software design is in
[architecture.md](architecture.md).
