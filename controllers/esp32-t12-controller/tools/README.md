# tools

Host-side tools. The Python ones need the venv from [../../README.md](../../README.md#initial-setup-windows)
(matplotlib for plots, pyserial for the board tools).

| Tool | Purpose |
|---|---|
| `scenarios.c` | closed-loop scenarios on the simulated tip (CSV per scenario, a summary table, the gain sweep) |
| `plot.py` | plot a scenario CSV, or a capture of the board's telemetry, to PNG |
| `../../../tools/serial/timing_check.py` | measure the control-loop timing on the board, PASS or FAIL (shared by all controllers) |
| `../../../tools/serial/capture.py` | record the console's CSV telemetry into a file (shared by all controllers) |

## timing_check.py

Needs the board. Close the serial monitor first (the port can only be open once). It runs the
setup commands, reads the loop stats from `status` and prints PASS or FAIL. For this controller:

```
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 350" "start" --teardown "stop" \
    --rate-hz 10 --max-exec-us 200
python ../../tools/serial/timing_check.py --port COM4 --setup "sp 350" "start" --teardown "stop" \
    --rate-hz 10 --max-exec-us 200 --log        # with the CSV telemetry streaming
python ../../tools/serial/timing_check.py --port COM4 --project . --flash   # build and flash first
python ../../tools/serial/timing_check.py --selftest                       # no board needed
```

The tip is simulated in double precision inside the 1 kHz loop, so the execution-time limit is
200 us here (measured: about 23 us on average, about 100 us at worst). The other limits are the
defaults: 0 overruns, period within +-50 us of 1 ms, at least 90 % of the expected cycles; with
`--log` also 0 dropped telemetry samples and at least 90 % of the expected rows (10 per second).

## capture.py and plot.py

```
python ../../tools/serial/capture.py --port COM4 --duration 10 --cmd "sp 350" "start" -o docs/capture.csv
python tools/plot.py docs/capture.csv docs/capture.png
```

The capture has the columns of the firmware's CSV header with the time from zero in seconds
(`t_s,setpoint_c,temp_c,duty,current_a`). `plot.py` draws it the same way as a scenario, so a run
on the board can be compared with the simulation in `docs/scenario_*.png`.

## scenarios.c

Runs the closed-loop scenarios from `lib/t12ctrl/src/t12_scenario.c` on the PC. The build command
is in `docs/tuning.md`:

```
.pio/scenarios.exe summary                    # results of every scenario, markdown
.pio/scenarios.exe sweep                      # kp / ki sweep, markdown
.pio/scenarios.exe cold|load|ambient|open|short|stuck|runaway > docs/scenario_x.csv
```
