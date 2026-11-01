# tools

Host-side scripts. Python ones need the venv from the main README (`pip install -r requirements.txt`).
The board tools are shared by all controllers and live in `../../tools/serial`.

| Script | Purpose |
|---|---|
| `../../tools/serial/timing_check.py` | measure the 1 kHz loop timing on the board, PASS or FAIL |
| `../../tools/serial/capture.py` | record the console CSV telemetry into a file |
| `plot.py` | plot a step-response CSV (or a capture) to PNG |
| `step_response.c` | PC simulation of PID + motor, prints CSV |
| `scenarios.c` | closed-loop scenarios (CSV per scenario) and the gain sweep table |

## timing_check.py

Needs the board. Close the serial monitor first (the port can only be open once). It runs the
setup commands, reads the loop stats from `status` and prints PASS or FAIL. For this controller
(commands are run from this project's folder):

```
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 3000" "start" --teardown "stop"
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 3000" "start" --project . --flash
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 3000" "start" --busy-us 200
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 3000" "start" --log
python ../../tools/serial/timing_check.py --selftest          # parser/limits check, no board
```

Limits (flags in brackets): 0 overruns, period within +-50 us of 1 ms (`--max-jitter-us`), exec
time under 100 us plus `--busy-us` (`--max-exec-us`), at least 90 % of the expected cycles. With
`--log` it also turns `log on` during the measurement and additionally requires 0 dropped
telemetry samples and at least 90 % of the expected CSV rows (100 per second, `--rate-hz`).
Measurement time is `--duration` (default 5 s). Exit code 0 pass, 1 out of limits, 2 no board.

Reference numbers from the manual runs are in `docs/timing.md`.

## capture.py and plot.py

```
python ../../tools/serial/capture.py --port COM4 --duration 3 --cmd "start" "sp 3000" -o docs/capture.csv
python tools/plot.py docs/capture.csv docs/step_response_target.png
```

The capture has the firmware's CSV columns with the time from zero in seconds
(`t_s,setpoint_rpm,speed_rpm,duty`); `plot.py` computes the error from setpoint and speed.

## step_response.c

Build and run commands are in the main README ("Step response").

## scenarios.c

Runs the closed-loop scenarios from `lib/motorctrl/src/motor_scenario.c` on the PC. Usage and the build
command are in `docs/tuning.md`:

```
.pio/scenarios.exe sweep                       # markdown table for several gain scales
.pio/scenarios.exe step|load|saturation|inertia > docs/scenario_x.csv
```
