# Controllers

Firmware for the devices under test of the Krumpir HIL bench. Each controller is an ESP32
(ESP32-WROOM-32 DevKit) running a deterministic 1 kHz control loop against a simulated plant.

| Controller | What it controls |
|---|---|
| [esp32-motor-controller](esp32-motor-controller/README.md) | PID speed control of a DC motor |
| [esp32-t12-controller](esp32-t12-controller/README.md) | temperature control of a T12 / T15 soldering tip |

## Shared design

- The control logic is portable C with no ESP-IDF or FreeRTOS includes (`lib/` in each controller,
  building blocks in [../libs/ctrlcore](../libs/ctrlcore), plant models in `../libs/plants`). It is
  unit tested on the PC with Unity and reused unchanged on the target.
- The ESP-IDF glue (tick task, telemetry, console helpers) is shared in
  [../libs/esp/espglue](../libs/esp/espglue/README.md).
- An I/O abstraction lets the same controller run against a simulated plant, a real one, or Krumpir.
  The firmware currently ships with the simulated plant; the PC build below runs against Krumpir.
- A task watchdog resets the board if the control loop hangs; safety trips are latched until `clear`.

## Initial setup (Windows)

1. **PlatformIO**: install the VS Code "PlatformIO IDE" extension, or `pip install platformio`.
   The CLI is `~\.platformio\penv\Scripts\pio.exe`; add that folder to your PATH to use `pio`.
2. **Host C compiler** (for the `native` tests and the PC simulations): install MinGW-w64 gcc,
   e.g. `winget install BrechtSanders.WinLibs.POSIX.UCRT`, and check `gcc --version` in a new terminal.
3. **USB driver** for the DevKit's serial chip (CP210x or CH340) if the board does not show up as a COM port.
4. **Python 3.13 venv**, one per controller, created in the controller folder (packages in its
   `requirements.txt`: matplotlib for plots, pyserial for the board tools):
   ```
   python -m venv .venv
   .venv\Scripts\activate           # bash: source .venv/Scripts/activate
   pip install -r requirements.txt
   ```

## Build, test, flash

Run from the controller folder:
```
pio test -e native                    # unit tests on the PC
pio run -e esp32dev                   # build firmware
pio run -e esp32dev -t upload         # flash the board
pio device monitor -p COM4            # serial console, 115200 baud
```
The shared libraries have their own tests: `pio test -e native` in `../libs`.

## Serial console

The firmware boots stopped, with the tuned gains; settings are lost on reset. These commands exist
in every controller (the rest are listed in its README):

| Command | Effect |
|---|---|
| `sp <value>` | setpoint |
| `kp` / `ki` / `kd <value>` | gains, finite and >= 0 |
| `start` / `stop` | run the controller / output 0 and PID reset |
| `clear` | clear a latched trip; `start` is refused until then |
| `status` | state, trip, measurements, loop timing since the last `status` |
| `log on` / `log off` | CSV telemetry |

The port can only be open once: close the monitor before running the board scripts in
`../tools/serial` (`capture.py` records the CSV into a file, `timing_check.py` measures the loop
timing, PASS or FAIL). Their commands are in each controller's `tools/README.md`.

## On the PC, against the runner (closed loop)

`host/` builds each controller for Linux or WSL: the same control task, console and tables as the
board, with the plant behind the HIL link (`../libs/hil`) instead of the on-chip simulation. The
runner steps the plant FMU and talks to it over a serial line, here a pty pair from socat:

```sh
cmake -S controllers/esp32-t12-controller/host -B ~/build/host-t12 && cmake --build ~/build/host-t12
tools/hil/pc_loop.sh t12 --check      # or motor, t12-open; --live: own clocks instead of lockstep
```

- The values on the link are `[hil]` in `controller.toml` (in frame order): the plant values the
  runner sends and the commands the controller answers with. The runner's scenario names the same
  (its `outputs`, and its `inputs` without a stimulus); the two check each other at connect.
- `host/io_hil.c` is the controller's I/O on the link. The T12's turns the FMU's physical values
  into ADC counts with the same front end as its on-chip simulation (`t12_frontend.c`), and keeps
  Q3 on the controller's side, driven by its heartbeat.
- **Lockstep** (default): one tick per plant step, deterministic, as fast as it goes; the runner's
  `ctest` runs `pc_loop.sh --check` for both controllers. **Free-running** (`--hz 1000`): the
  controller ticks on its own clock from the first plant frame, as the board will.
- A link that stops answering reads as a lost sensor: after 10 ticks without a plant frame the
  controller gets invalid readings and trips `sensor`. The runner's `BYE` (unload, or a fault)
  stops it.
- `<name>_host --device DEV [--lockstep | --hz N] [--log] [-c "sp 300"]...`: console on stdin, CSV
  telemetry on stdout (`--log` or `log on`).

### SIL vs PC loop

`tools/hil/compare.py motor|t12` runs every closed-loop scenario of `tools/scenarios.c` twice: on
the controller's on-chip plant (SIL), and as the PC loop against the plant FMU in the runner, with
the same setpoints (`--at` on the PC controller) and plant events (runner stimuli). It compares
the signals (RMS error in % of full scale), the step figures and the trips, and writes
`docs/sil_vs_pc.md` with an overlay plot per case (matplotlib, e.g. the venv of
`runner/tools/setup_wsl.sh`). A case passes with an RMS error under 2 %, figures within 10 % and
the same trip; the runner's CTest runs it as `hil_compare_motor` and `hil_compare_t12`
(`--check`, which leaves the report alone).

Both loops compute the command for plant step k from the plant before it: the runner sends the
model's initial outputs before step 0, so tick k of the PC controller is step k, as on the chip.
The remaining differences are the float motor plant on the chip and the integer telemetry columns.

### XCP on the PC build

With `-DKRUMPIR_XCP=ON` the PC build serves XCP on UDP (XCPlite's Linux configuration, the same
calibration page and events as the board), and the TUI has a card for it ("t12 pc", "motor pc").
Every build has a new EPK, so make the A2L after each build:

```sh
cmake -S controllers/esp32-t12-controller/host -B ~/build/host-t12-xcp -DKRUMPIR_XCP=ON
cmake --build ~/build/host-t12-xcp
xcpclient --offline --udp --dest-addr 127.0.0.1 --port 5556 --elf <the t12_host program> \
  --a2l controllers/esp32-t12-controller/docs/t12_host.a2l --elf-unit-filter control_task \
  --default-event tick --embed-aml
python3 tools/hil/fix_host_a2l.py controllers/esp32-t12-controller/docs/t12_host.a2l
CONTROLLER=~/build/host-t12-xcp/t12_host XCP_PORT=5556 tools/hil/pc_loop.sh t12 --live
```

- Give each controller its own port (`--xcp`, and the same in the A2L): motor 5555, t12 5556.
- `fix_host_a2l.py`: on Linux XCPlite keeps the EPK in calibration segment 0, which an offline A2L
  does not know. `xcpclient --create-a2l` connected to the running program gets it right too.
- On Windows, xcpclient and the TUI reach a controller in WSL only with WSL's mirrored networking
  (`networkingMode=mirrored` in `%UserProfile%\.wslconfig`); WSL's default NAT drops the UDP.
  Otherwise run the TUI in WSL.
- `KT_ECU=<a2l> test_core` (clients/tui) checks a running controller: EPK, a calibration write and
  DAQ at 1 kHz.

## XCP measurement and calibration

The env `esp32dev-xcp` builds the same firmware with XCP on Ethernet over Wi-Fi: every variable of
the control loop can be measured at 1 kHz, and setpoint and gains calibrated through a calibration
page. Writing the page's `request` field starts, stops or clears the controller like the console
does (`<name>_cal_page.request`: low byte 1 start, 2 stop, 3 clear, upper bytes a sequence number
to count up; see [../tools/codegen](../tools/codegen/README.md)). Setup, the `xcpclient` commands
and the measured cost on the loop are in
[../libs/esp/xcpglue/README.md](../libs/esp/xcpglue/README.md).
