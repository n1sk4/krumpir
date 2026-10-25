# tools

Host-side scripts. Python ones need the venv from the main README (`pip install -r requirements.txt`).

| Script | Purpose |
|---|---|
| `timing_check.py` | measure the 1 kHz loop timing on the board, PASS or FAIL |
| `capture.py` | record the console CSV telemetry into a file |
| `plot.py` | plot a step-response CSV to PNG |
| `step_response.c` | PC simulation of PID + motor, prints CSV |

## timing_check.py

Needs the board. Close the serial monitor first (the port can only be open once). It starts the
controller at 3000 rpm, reads the loop stats from `status` and prints PASS or FAIL.

```
python tools/timing_check.py --port COM4               # measure the firmware already on the board
python tools/timing_check.py --port COM4 --flash       # build and flash first
python tools/timing_check.py --port COM4 --busy-us 200 # flash a build with a 200 us load
python tools/timing_check.py --port COM4 --log         # same, with CSV telemetry streaming
python tools/timing_check.py --selftest                # parser/limits check, no board
```

Limits (flags in brackets): 0 overruns, period within +-50 us of 1 ms (`--max-jitter-us`), exec
time under 100 us plus `--busy-us` (`--max-exec-us`), at least 90 % of the expected cycles.
With `--log` it also turns `log on` during the measurement and additionally requires 0 dropped
telemetry samples and at least 90 % of the expected CSV rows (100 per second). Measurement time is
`--duration` (default 5 s). Exit code 0 pass, 1 out of limits, 2 no board.

Reference numbers from the manual runs are in `docs/timing.md`.

## capture.py and plot.py

```
python tools/capture.py --port COM4 --duration 3 --cmd "start" "sp 3000" -o docs/capture.csv
python tools/plot.py docs/capture.csv docs/step_response_target.png
```

## step_response.c

Build and run commands are in the main README ("Step response").
