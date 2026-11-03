# krumpir-tui

A terminal UI for the runner and the controllers: connect to a `krumpir serve`, pick a scenario or
an FMU on the runner, load it, run it and watch it; or connect to a controller over XCP, tune it,
start and stop it and plot its signals. Runs on Windows, Linux (also WSL) and the Raspberry Pi;
the runner can be on the same machine or another one.

```
 krumpir-tui   runner 127.0.0.1:7700  * connected  | scenarios/dcmotor.toml  STOPPED
───────┬─────────────────────────────────────────────────────────────────────────────
F1 Load│F2 Monitor
───────┴─────────────────────────────────────────────────────────────────────────────
 STOPPED   t 1.000 / 1 s  steps 1000  overruns 0  step max 13.5 us   Start  Stop  Unload
╭ Variables ────────────────────────────╮╭ Plot: duty ───────────────────────────────╮
│> duty            input          0.5   ││0.5     ⢸⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉⠉│
│  load_torque     input       0.0002   ││        ⢸                                  │
│  speed_rpm       output     2748.33   ││⣀⣀⣀⣀⣀⣀⣀⣀⣸                                  │
│  current         output   0.0487813   ││0                       t 0.001 .. 1.000 s│
╰───────────────────────────────────────╯╰───────────────────────────────────────────╯
```

## Build

CMake 3.25+ and a C++17 compiler. FTXUI, Asio, nlohmann/json and doctest are fetched by CMake
at pinned versions.

```sh
cmake -S clients/tui -B ~/build/tui -G Ninja -DCMAKE_BUILD_TYPE=Release   # Linux, WSL, Pi
cmake --build ~/build/tui && ctest --test-dir ~/build/tui
```

On Windows the same commands work with MinGW (WinLibs) or Visual Studio. With MinGW the
`.exe` is linked statically and needs no DLLs. Use Windows Terminal.

## Run

```sh
cd runner && ~/build/krumpir/krumpir serve            # the runner, in the directory with scenarios/ and fmus/
~/build/tui/krumpir-tui                               # connects to 127.0.0.1:7700
krumpir-tui 192.168.1.20:7700                         # a runner on the Pi (serve 0.0.0.0:7700 there)
krumpir-tui --repo ~/krumpir                          # where controllers/ is (default: found upwards
                                                      # from the current folder, then the program's)
```

| Key | |
|-----|-|
| F1 / F2 | Load / Monitor tab (or click the tab) |
| up / down | on the cards: the runner or a controller (the same on both tabs) |
| Tab, arrows | move between the cards, fields and lists |
| Enter | on a file: load it (as edited); in the path: open that file |
| up / down | (Load, the form) the next or previous field; space ticks a box, Enter opens a list |
| v | (Load) the form or the scenario text, on the file list or the text |
| s / x / u | (Monitor) start / stop / unload; `s` on a stopped run resets it and starts again |
| r | (Monitor) reset a stopped or faulted run: the same scenario again from t = 0 |
| up / down | (Monitor) the variable to plot |
| w, Enter | (Monitor) write a value to the selected input or tunable parameter |
| g | (Monitor) play a signal on the selected input |
| Enter | (Load, a controller) in the address: connect; in the grid: write the changed values |
| s / x / c | (Monitor, a controller) start / stop / clear a latched trip |
| space, Enter | (Monitor, a controller) on a signal: plot it or not |
| m | (Monitor) one graph each / all in one |
| v | (Monitor) hide or show the variables (signals) pane; the plot takes the room |
| [ / ] | (Monitor) make the variables pane narrower / wider by 4 columns (shows it if hidden) |
| Esc | close a dialog |
| F10, Ctrl+C | quit |

Both tabs have a card per target on the left: the runner, then each controller of the repo. A
card's name is green while its link is up. Each controller has its own link, so several can be
connected at once.

**Load** (runner) lists `scenarios/*.toml` and `fmus/*.fmu` on the runner and shows the selected
file as a form to edit before loading: a scenario as written, an FMU as the starter scenario
`krumpir init` makes for it. One row per value, in the scenario's sections: name, field, unit, and
the FMU's description or what is wrong with the value.

- Numbers and text are typed, choices (FMU, `wait`, a stimulus kind) are lists, switches are
  boxes: run until stopped, pin to a cpu, SCHED_FIFO, and each FMU input and output to use. A
  value that depends on a switch shows while the switch is on (duration, cpu, priority, an
  input's stimulus and its numbers).
- Every Float64 parameter of the FMU is listed; one the scenario does not set shows the FMU's
  start value, dimmed, and is written only once changed. Other types are shown, not editable.
- Each field is checked as you type (a number, its range, a whole number of steps, at most 16
  parameters, inputs and outputs); a changed value is bold, a wrong one red. Segment stimuli
  are kept as written; pick another kind to replace one.
- **Check** asks the runner to check the form against the FMU as loading would, without loading;
  what it says shows above the form (a field it names turns red). **Load** loads the form (the
  file as it is when unchanged); a run that is still loaded is unloaded first. **Save** writes
  the form over the scenario file, **Save as** to another `scenarios/<name>.toml` on the runner;
  the file is written from the form, so its comments and layout are not kept. **Revert** puts
  the values back, **Text** shows the scenario as text (as edited).
- Opening another file with unsaved edits asks first.

**Monitor** (runner) shows the run's state, time, overruns and worst step and wake-up times, the Float64
inputs, outputs and tunable parameters with their values, a plot of the selected one, and the
events. Start arms a capture of the scenario's inputs and outputs, so the plot holds every step,
also for unpaced runs that end at once. Other variables are sampled at 10 Hz while it runs.

The variables list (runner) and the signals list (controllers) have a filter box above them: type
to narrow the list to the names that contain the text, ignoring case. Checked or watched ones stay
on the plot while the filter hides them. Down from the box moves to the list.

On every Monitor page the variables (signals) pane and the plot share the room right of the cards:
drag the divider with the mouse or press `[` and `]` to move it, `v` to hide the pane. The cards, the
header and the events stay as they are, and the setting is the same for all targets. It is not saved
between runs.

A written value takes effect at the next step; on an input it replaces the input's signal. Signal
presets (the dialog keeps the last one picked; numbers default to the input's value):

| Preset | Numbers | Plays |
|--------|---------|-------|
| constant | value | the value |
| step | from, after s, to | `from`, then `to` once `after` seconds have passed |
| ramp | from, to, over s | a ramp from `from` to `to`, then holds `to` |
| sine | offset, amplitude, freq Hz | `offset + amplitude sin(2 pi f t)` |
| pulse | low, high, period s, duty | `high` for `duty` of each period, else `low` |

Both work once a run is loaded (from the start of the run) and while it runs (from the next
step); outputs and fixed parameters are refused.

**Controllers** are found by their `controllers/*/src/gen/ctrl_gen.json` (written by
`tools/codegen`). The TUI talks XCP on UDP to them, with the A2L of their build
(`controllers/<name>/docs/<name>.a2l`; build with `-e esp32dev-xcp` and generate it as the
controller's README and [xcpglue](../../libs/esp/xcpglue/README.md) describe). On Load, the
address comes from the A2L and can be changed before Connect. Connecting checks the A2L's EPK
against the firmware: an A2L from another build is refused.

- Load: the calibration grid and the link.
- Monitor: the status line shows whether the control loop runs and the latched trip by name (red).
  Start, Stop and Clear go through the calibration page's request field and act at the next tick;
  a start with a latched trip is ignored by the controller until it is cleared.
- The calibration grid shows every parameter with its unit, limits and the value read back from
  the controller. A changed field turns yellow, a value outside the limits red; Write (or Enter)
  sends the changed ones and refuses those outside the limits. Revert puts the read values back.
  Parameters without a calibration value (e.g. `ramp`) are set on the serial console only.
- Monitor: signals are the A2L's scalar measurements; the controller's telemetry values are
  checked at connect, and plotted like the runner's. A checked signal with a DAQ event in the A2L
  (all of the controller's, on `tick`) is measured by XCP DAQ: the controller sends it on every
  event, 1 kHz on `tick`, with its own timestamps, so the plot holds every tick. Each event gets
  a DAQ list; checking or unchecking a signal sets them up again. Signals without an event are
  polled every 50 ms, one round trip each. The header shows how many are measured each way, the
  rate per signal, and DAQ packets lost (counter gaps; yellow when any). If the controller
  refuses DAQ, everything is polled and the events say why.

A controller with a HIL link also gets a card for its PC build ("t12 pc"), with the A2L
`docs/<name>_host.a2l` of that build (controllers/README.md, "XCP on the PC build").

The controller takes one XCP master at a time: while the TUI is connected, `xcpclient` and the
Python `EcuPort` can't connect, and the other way round.

## Design

```
 UI thread (FTXUI)                    worker thread                     krumpir serve
 ┌──────────────────────┐  commands  ┌──────────────────────────┐ TCP  ┌──────────────┐
 │ Root: status, tabs,  │──────────▶ │ Model                    │◀────▶│ one session  │
 │ key bar              │            │  runs commands in order, │ JSON │              │
 │ LoadTab  MonitorTab  │ ◀───────── │  polls every 100 ms      │ lines│              │
 └──────────────────────┘  snapshot  │ Service (blocking client)│      └──────────────┘
                           + redraw  └──────────────────────────┘
```

- `src/service.*`: the protocol client. Blocking, one request at a time, every call bounded by a
  timeout; a refusal is a `ServiceError`, a broken or slow link a `LinkError` (it closes).
  No UI code: tests and later tools use it as it is.
- `src/model.*`: owns the connection on a worker thread. The UI posts commands (connect, load,
  start, ...) and reads a `Snapshot` under a lock; the worker tells it to redraw. The UI never
  waits on the network.
- `src/a2l.*`, `src/xcp.*`, `src/ecu_model.*`: the same for a controller: the A2L subset the
  tools use, a blocking XCP-on-UDP master, and `EcuModel` with its own worker and snapshot.
- `src/ui/`: one component per tab plus the frame, and the plot and events panes they share
  (`plot.cpp`). A new tab is a component that reads a snapshot and posts commands, and one line
  in `root.cpp`.

The service takes one client at a time: while the TUI is connected, scripts using
`tools/krumpir` wait.
