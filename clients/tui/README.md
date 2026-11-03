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
| Enter | on a file or the path: load it |
| s / x / u | (Monitor) start / stop / unload; `s` on a stopped run resets it and starts again |
| r | (Monitor) reset a stopped or faulted run: the same scenario again from t = 0 |
| up / down | (Monitor) the variable to plot |
| w, Enter | (Monitor) write a value to the selected input or tunable parameter |
| g | (Monitor) play a signal on the selected input |
| Enter | (Load, a controller) in the address: connect; in the grid: write the changed values |
| s / x / c | (Monitor, a controller) start / stop / clear a latched trip |
| space, Enter | (Monitor, a controller) on a signal: plot it or not |
| m | (Monitor) one graph each / all in one |
| Esc | close a dialog |
| F10, Ctrl+C | quit |

Both tabs have a card per target on the left: the runner, then each controller of the repo. A
card's name is green while its link is up. Each controller has its own link, so several can be
connected at once.

**Load** (runner) lists `scenarios/*.toml` and `fmus/*.fmu` on the runner and shows the selected file:
a scenario as written, an FMU as the starter scenario `krumpir init` makes for it. Loading an
FMU loads that starter. A run that is still loaded is unloaded first.

**Monitor** (runner) shows the run's state, time, overruns and worst step and wake-up times, the Float64
inputs, outputs and tunable parameters with their values, a plot of the selected one, and the
events. Start arms a capture of the scenario's inputs and outputs, so the plot holds every step,
also for unpaced runs that end at once. Other variables are sampled at 10 Hz while it runs.

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
  checked at connect. Checked signals are polled every 50 ms, one round trip each (about 10
  signals at 20 Hz over Wi-Fi), and plotted like the runner's.

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
