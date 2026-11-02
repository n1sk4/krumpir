# krumpir-tui

A terminal UI for the runner: connect to a `krumpir serve`, pick a scenario or an FMU on the
runner, load it, run it and watch it. Runs on Windows, Linux (also WSL) and the Raspberry Pi; the
runner can be on the same machine or another one.

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
```

| Key | |
|-----|-|
| F1 / F2 | Load / Monitor tab (or click the tab) |
| Tab, arrows | move between fields and lists |
| Enter | on a file or the path: load it |
| s / x / u | (Monitor) start / stop / unload |
| up / down | (Monitor) the variable to plot |
| w, Enter | (Monitor) write a value to the selected input or tunable parameter |
| g | (Monitor) play a signal on the selected input |
| Esc | close a dialog |
| F10, Ctrl+C | quit |

**Load** lists `scenarios/*.toml` and `fmus/*.fmu` on the runner and shows the selected file:
a scenario as written, an FMU as the starter scenario `krumpir init` makes for it. Loading an
FMU loads that starter. A run that is still loaded is unloaded first.

**Monitor** shows the run's state, time, overruns and worst step and wake-up times, the Float64
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
- `src/ui/`: one component per tab plus the frame. A new tab is a component that reads the
  snapshot and posts commands, and one line in `root.cpp`.

The service takes one client at a time: while the TUI is connected, scripts using
`tools/krumpir` wait.
