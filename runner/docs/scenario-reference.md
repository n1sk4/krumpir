# Scenario reference

A run is one TOML file, started with `krumpir run <scenario.toml>`. Paths in it are relative to the
directory `krumpir` is started from. Unknown keys are errors, so a typo cannot silently change a
run. Examples: `scenarios/` (demos) and `tests/golden/` (short regression runs).

```toml
[model]
fmu      = "fmus/Dahlquist.fmu"
step     = 0.001          # s, communication step
duration = 10             # s, a whole number of steps
params   = { k = 1.0 }    # set before initialization

[rt]
wait         = "sleep"    # none | sleep | busy
max_overruns = 0

[io]
backend  = "null"
inputs   = ["u"]
outputs  = ["x"]
stimulus = [{ input = "u", kind = "step", t = 1.0, from = 0, to = 1 }]

[log]
file = "runs/dahlquist.csv"
```

## `[model]` (required)

| Key | Type | Meaning |
|-----|------|---------|
| `fmu` | string | Path to an FMI 3.0 co-simulation FMU. The shared library for this platform (`x86_64-linux` or `aarch64-linux`) must be inside. |
| `step` | number | Communication step in seconds, at least `1e-9`. |
| `duration` | number | Simulated time in seconds. Must be a whole number of steps (`duration / step`). Optional: without it the run goes on until it is stopped (Ctrl-C, or `stop` through the service). |
| `params` | table | `name = number`, up to 16. Each must be a Float64 variable with causality `parameter`. |

## `[rt]` (optional)

| Key | Default | Meaning |
|-----|---------|---------|
| `wait` | `"none"` | `none`: no pacing, as fast as possible, and never drops a log row. `sleep`: wait for each tick with `clock_nanosleep`. `busy`: spin until each tick. |
| `max_overruns` | `0` | Paced runs only. A step that finishes after its deadline is an overrun; the run faults when the count exceeds this. |
| `cpu` | unset | Pins the loop thread to this CPU (the isolated core on the Pi). |
| `priority` | unset | Runs the loop thread `SCHED_FIFO` at this priority (1 to 99) and locks the process's memory (`mlockall`). Needs root, or `rtprio` and `memlock` limits for the user; otherwise the run does not start and says why. |
| `timing_gpio` | unset | A GPIO line (on `/dev/gpiochip0`: the Pi 4's GPIO numbers) that is high from each step's wake-up to the end of its work, for a logic analyzer: the period is the tick, the rising edges show the jitter, the width the step's work. One ioctl per edge, about 1 to 2 µs. Loading fails if the line cannot be had (no such chip, or the line is in use). |

Paced ticks are absolute (`start + n * step`), so a late step does not push later ones back.

## `[io]` (required)

| Key | Type | Meaning |
|-----|------|---------|
| `backend` | string | `"null"`: no device; inputs read 0, outputs are discarded. `"uart"`: a controller on a serial line (below). |
| `inputs` | list of strings | Model inputs to drive, up to 16, unique. Float64 variables with causality `input`. |
| `outputs` | list of strings | Model outputs to read and log, up to 16, unique. Float64 variables with causality `output`. |
| `stimulus` | list of tables | One per input at most. A stimulus replaces the backend's value for its input; an input without one gets the backend's value. Also written as `[[io.stimulus]]` tables. |
| `device` | string | `uart` only, required: the serial device (`/dev/ttyAMA0`, a USB adapter, or a pty). |
| `baud` | integer | `uart`: default `921600`; a standard rate. Ignored on a pty. |
| `timeout_ms` | number | `uart`: default `100`. How long a step waits for the controller's answer. `0`: never wait, take the newest answer that has arrived (a controller on its own clock). |
| `late` | string | `uart`: `"fault"` (default) or `"hold"`. A step without its answer keeps the last commands and counts as late; after `max_late` late steps in a row, `fault` faults the run. |
| `max_late` | integer | `uart`: default `10`. |

### The `uart` backend

The runner and the controller exchange `libs/hil` frames: before step k the runner sends the
`outputs`, in order, as PLANT(k) (before step 0: the outputs of the initialized model); the
controller answers CMD(k) with one value per input without a stimulus, in order, which feed step
k. So each step's command comes from the plant before it, as on a board that samples, then acts. Inputs with a stimulus stay the runner's (a load
torque, a fault). On load the runner sends HELLO with a hash of both name lists and waits up to
2 s for the controller's; a controller built for other values is refused, with both lists in the
message. Unloading or a fault sends BYE: the controller goes to its safe state. `late` in `state`
and the run's summary counts the late steps.

### Stimulus kinds

All take `input` (a name from `inputs`) and either `kind` or `segments`. The value is evaluated at
the start of each step, at `t = n * step`.

| `kind` | Keys | Value |
|--------|------|-------|
| `constant` | `value` | `value` |
| `step` | `t`, `from`, `to` | `from` before time `t`, `to` from `t` on |
| `sine` | `offset` (default 0), `amplitude`, `freq_hz` | `offset + amplitude * sin(2 pi freq_hz t)` |

### Segments

A signal of up to 8 segments played in order, like the segments of an ASAM XIL signal description.
`loops` plays it that many times (default 1, `0` forever; needs a `duration` on every segment). After
the end the signal holds its last value.

```toml
[[io.stimulus]]
input = "duty"
loops = 2
segments = [
  { kind = "const", value = 0.3, duration = 0.5 },
  { kind = "ramp", to = 0.8, duration = 1 },
  { kind = "pulse", high = 0.6, period = 0.05, duty = 0.3, duration = 1 },
  { kind = "data", file = "signals/drive.csv" },
]
```

Each segment takes `kind` and `duration` (s, > 0). Only the last segment may leave `duration` out,
and then runs to the end of the run; a `data` segment without one lasts as long as its file. `u` is
the time since the segment started.

| `kind` | Keys | Value |
|--------|------|-------|
| `const` | `value` | `value` |
| `ramp` | `to`, `from` (default: the previous segment's end value), `duration` required | `from + (to - from) * u / duration` |
| `sine` | `amplitude`, `freq_hz`, `offset` (default 0), `phase` (rad, default 0) | `offset + amplitude * sin(2 pi freq_hz u + phase)` |
| `pulse` | `high`, `period`, `duty` (0..1), `low` (default 0) | `high` while `u mod period < duty * period`, else `low` |
| `idle` | | holds the previous segment's end value (0 for the first) |
| `data` | `file`, `interpolation` (`"linear"` default, or `"step"`) | rows of `time,value` from the file, time from 0 and rising; holds the first and last value outside them |

A data file is plain CSV: one header line, blank lines and `#` comments are skipped. Its path is
relative to the working directory, like `model.fmu`. Files are read when the scenario is loaded.

## `[log]` (required)

| Key | Meaning |
|-----|---------|
| `file` | CSV written during the run. A single missing parent directory (such as `runs/`) is created. |

Columns: `n,t,<inputs>,<outputs>,wake_ns,step_ns,flags`. Row `n` is one step: `t = (n+1) * step`,
the inputs are the values applied at its start, the outputs are read after it. `wake_ns` is how late
the tick began, `step_ns` the time spent in the FMU's step, `flags` bit 0 an overrun.

## Exit status

`0` run completed or was stopped with Ctrl-C, `1` setup error (bad scenario, FMU, mapping or log
file; nothing ran), `2` fault during the run (FMU error or too many overruns).
