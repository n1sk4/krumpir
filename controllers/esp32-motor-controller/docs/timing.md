# Loop timing

Board: ESP32-WROOM-32 DevKit (CH340 USB-serial, COM4), CPU 160 MHz (ESP-IDF default, 6.25 ns per cycle).

## Method
- GPTimer at 1 kHz; its ISR only notifies the control task (core 1, priority `configMAX_PRIORITIES - 2`).
- Period = CPU cycle-counter delta between consecutive wakeups; exec = wakeup to end of body.
- The body is the closed loop: PID + simulated motor, parameter copy and telemetry push.
- `tools/timing_check.py` starts the controller at 3000 rpm and reads the loop stats from the
  `status` command over about 5.6 s (the first status call discards the startup window).
  Commands and pass limits: [tools/README.md](../tools/README.md#timing_checkpy).

## Results

| Case | Command | Period min / max | Exec min / max | Overruns |
|---|---|---|---|---|
| Idle | `--flash` | 995,718 / 1,004,237 ns | 3,081 / 3,993 ns | 0 |
| 200 us load | `--busy-us 200` | 995,750 / 1,004,331 ns | 200,087 / 200,162 ns | 0 |
| Telemetry on | `--log` | 995,725 / 1,003,575 ns | 4,656 / 5,456 ns | 0 |

- All three runs passed the limits (period within +-50 us, exec under 100 us plus the load).
- **Idle:** jitter about +-4.3 us (0.43 %); exec about 3.1 us, 0.3 % of the 1 ms period.
- **200 us load:** same jitter; exec is the 200 us wait plus about 0.1 us.
- **Telemetry on:** 499 CSV rows received in 5 s (100 Hz expected), 0 samples dropped, 0 overruns.
  Jitter is unchanged (about +-4.3 us); exec rises to about 4.7 us, the cost of telemetry traffic.

## Caveats
- Measured with the CPU cycle counter, which shares a PLL with the timer; a logic-analyzer
  capture of GPIO 23 would be the independent check (not done yet).
- Short runs (seconds). A longer soak test would catch rarer excursions.
