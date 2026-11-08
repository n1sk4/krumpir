# krumpir-tui

A terminal UI (C++17) for the runner and the controllers: connect to a `krumpir serve`, pick a
scenario or an FMU on the runner, load it, run it and watch it; or connect to a controller over
XCP, tune it, start and stop it and plot its signals. Windows (Windows Terminal), Linux, WSL and
the Pi; the runner can be on another machine. Building: [../../BUILDING.md](../../BUILDING.md)
(part `tui`).

```
 krumpir-tui   runner 127.0.0.1:7700  * connected  | scenarios/dcmotor.toml  STOPPED
───────┬─────────────────────────────────────────────────────────────────────────────
F1 Load│F2 Monitor
───────┴─────────────────────────────────────────────────────────────────────────────
 STOPPED   t 1.000 / 1 s  steps 1000  overruns 0  step max 13.5 us   Start  Stop  Unload
╭ Variables ────────────────────────────╮╭ Plot: duty ───────────────────────────────╮
│> duty            input          0.5   ││0.5        ⢸⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉ ⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉│
│  load_torque     input       0.0002   ││           ⢸                               │
│  speed_rpm       output     2748.33   ││⣀⣀⣀⣀⣀⣀⣀⣀⣸                               │
│  current         output   0.0487813   ││0                        t 0.001 .. 1.000 s│
╰───────────────────────────────────────╯╰───────────────────────────────────────────╯
```

## Run

```sh
cd runner && <build>/runner/krumpir serve     # in the directory with scenarios/ and fmus/
<build>/clients/tui/krumpir-tui               # connects to 127.0.0.1:7700
krumpir-tui 192.168.1.20:7700                 # a runner on the Pi (serve 0.0.0.0:7700 there)
krumpir-tui --repo ~/krumpir                  # where controllers/ is (default: found upwards)
```

| Key | |
|-----|-|
| F1 / F2 | Load / Monitor tab (or click the tab) |
| up / down | on the cards: the runner or a controller (the same on both tabs) |
| Tab, arrows | move between the cards, fields and lists |
| Enter | on a file: load it (as edited); in the path: open that file |
| up / down | (Load, the form) the next or previous field; space ticks a box, Enter opens a list |
| v | (Load) the form or the scenario text |
| s / x / u | (Monitor) start / stop / unload; `s` on a stopped run resets it and starts again |
| r | (Monitor) reset a stopped or faulted run: the same scenario again from t = 0 |
| up / down | (Monitor) the variable to plot |
| w, Enter | (Monitor) write a value to the selected input or tunable parameter |
| g | (Monitor) play a signal on the selected input |
| w | (Monitor, a controller) write a calibration parameter, inside its limits |
| Enter | (Load, a controller) in the address: connect; in the grid: write the changed values |
| s / x / c | (Monitor, a controller) start / stop / clear a latched trip |
| space, Enter | (Monitor, a controller) on a signal: plot it or not |
| m | (Monitor) one graph each / all in one |
| v | (Monitor) hide or show the variables pane; the plot takes the room |
| [ / ] | (Monitor) the variables pane narrower / wider by 4 columns |
| Esc | close a dialog |
| F10, Ctrl+C | quit |

Both tabs have a card per target on the left: the runner, then each controller of the repository
(green while its link is up; several can be connected at once).

- **Load** (runner) lists `scenarios/*.toml` and `fmus/*.fmu` on the runner and shows the
  selected file as a form: a scenario as written, an FMU as the starter scenario `krumpir init`
  makes. One row per value, with unit and the FMU's description or what is wrong. Fields are
  checked as you type (a changed value bold, a wrong one red). **Check** asks the runner to check
  the form as loading would; **Load** loads it (unloading a run first); **Save** / **Save as**
  write the form to `scenarios/<name>.toml` (comments and layout are not kept); **Revert**,
  **Text**. Opening another file with unsaved edits asks first.
- **Monitor** (runner): the run's state, time, overruns, worst step and wake-up times, the Float64
  inputs, outputs and tunable parameters, a plot of the selected one, and the events. Start arms a
  capture of the scenario's inputs and outputs, so the plot holds every step; other variables are
  sampled at 10 Hz. A filter box above the list narrows it by name. The pane and the plot share
  the room: drag the divider, or `[`, `]`, `v`.
- A written value takes effect at the next step, and on an input replaces its signal. Signal
  presets: constant (value), step (from, after s, to), ramp (from, to, over s), sine (offset,
  amplitude, freq Hz), pulse (low, high, period s, duty). Outputs and fixed parameters are refused.
- **Controllers** are found by their `src/gen/ctrl_gen.json` and talk XCP on UDP with the A2L of
  their build ([../../controllers/README.md](../../controllers/README.md#xcp)); connecting checks
  the A2L's EPK against the firmware. Load: the calibration grid (unit, limits, the value read
  back; changed yellow, out of limits red) and the address. Monitor: whether the loop runs and the
  latched trip; start, stop and clear through the calibration page's request field. Signals with a
  DAQ event (all on `tick`) are measured by XCP DAQ at 1 kHz with the controller's timestamps;
  others polled every 50 ms. A controller with a HIL link also gets a card for its PC build
  ([XCP on the PC build](../../controllers/README.md#xcp-on-the-pc-build)).

The controller takes one XCP master at a time, and the service one client: while the TUI is
connected, `xcpclient`, `EcuPort` and scripts using `tools/krumpir` wait.

## Design

The UI thread (FTXUI) posts commands and reads a snapshot; a worker thread (`src/model.*`) owns
the connection, runs the commands in order and polls every 100 ms, so the UI never waits on the
network. `src/service.*` is the blocking protocol client (a refusal is a `ServiceError`, a broken
link a `LinkError`); `src/a2l.*`, `src/xcp.*`, `src/ecu_model.*` the same for a controller;
`src/ui/` one component per tab. `KT_ECU=<a2l> test_core` also checks a running controller (EPK,
a calibration write, DAQ at 1 kHz).
