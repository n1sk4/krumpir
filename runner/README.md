# Krumpir runner

Real-time FMU runner. Loads an FMI 3.0 co-simulation FMU, steps it at a fixed time step and
exchanges signals with a device under test through a pluggable I/O backend. Design:
[../docs/architecture.md](../docs/architecture.md).

## Setup (Debian/Ubuntu, WSL or Raspberry Pi OS)

```sh
bash runner/tools/setup_wsl.sh     # build tools, Python venv with FMPy (in ~/.venvs/krumpir)
bash runner/tools/fetch_fmus.sh    # Reference FMUs, see fmus/README.md
```

## Build and test

```sh
cmake -S runner -B ~/build/krumpir -G Ninja
cmake --build ~/build/krumpir
ctest --test-dir ~/build/krumpir --output-on-failure
```

Add `-DKRUMPIR_SANITIZE=ON` for an AddressSanitizer/UndefinedBehaviorSanitizer build. Tests that
need the Reference FMUs report SKIPPED until `tools/fetch_fmus.sh` has been run. The `fmpy_*` tests
need the Python venv from `tools/setup_wsl.sh`.

## Run

```sh
cd runner
~/build/krumpir/krumpir info fmus/Dahlquist.fmu       # model description and variables
~/build/krumpir/krumpir init fmus/my.fmu > scenarios/my.toml  # starter scenario for an FMU
~/build/krumpir/krumpir run scenarios/dahlquist.toml  # run a scenario, write runs/dahlquist.csv
~/build/krumpir/krumpir serve                         # drive runs over TCP (docs/service-protocol.md)
```

`init` reads the FMU and prints a scenario that runs as written: the FMU's default step and stop
time, every Float64 parameter at its start value (with unit and description), every Float64 input
and output, inputs with a non-zero start value held there, the `null` backend and
`runs/<model>.csv` as the log. Edit the parameters and the stimulus from there; a scenario that no
longer matches its FMU fails to load with the variable it trips on.

Paths in a scenario are relative to the directory `krumpir` is started from. Ctrl-C stops a run
cleanly after the current step. Exit status: 0 ok, 1 setup error, 2 fault during the run.

### Log columns

`n,t,<inputs>,<outputs>,wake_ns,step_ns,flags`. Row `n` is one step: `t = (n+1)*step` is the time
at the end of the step, the inputs are the values applied at its start (`n*step`), and the outputs
are read after it. `wake_ns` is how late the tick started (0 when `wait = "none"`), `step_ns` is the
time spent in the FMU's step, and `flags` bit 0 marks an overrun.

The full list of scenario keys is in [docs/scenario-reference.md](docs/scenario-reference.md).

### A controller in the loop

With `backend = "uart"` the inputs come from a controller on a serial line instead of a stimulus
(`libs/hil` frames; keys in the scenario reference). `scenarios/*_hil_pc.toml` run the
controllers' own code built for this PC over a pty pair: `tools/hil/pc_loop.sh motor|t12|t12-open`
starts both ([controllers/README.md](../controllers/README.md)). This build also builds those
controllers (`-DKRUMPIR_HIL_PC=OFF` leaves them out), and CTest checks each closed loop against
the controller's acceptance figures (`hil_pc_*`; skipped without socat).

## Golden files

`tests/golden/` has three short scenarios and the CSVs they must reproduce (the `n`, `t`, input and
output columns; the timing columns are dropped). CTest runs them as `golden_*` and compares with a
tolerance of 1e-12, because the last bits of a floating-point result can differ between CPUs. After
an intended change, regenerate a file from the `runner/` directory:

```sh
python3 tools/check_golden.py ~/build/krumpir/krumpir tests/golden/dahlquist.toml \
  tests/golden/dahlquist.csv --update
```

## Continuous integration

`.github/workflows/runner.yml` (manual dispatch) builds the runner and runs all tests twice, plain
and with AddressSanitizer/UndefinedBehaviorSanitizer, and uploads the FMPy overlay plots.

## Compare with FMPy

```sh
~/.venvs/krumpir/bin/python tools/compare_fmpy.py scenarios/dahlquist.toml --run ~/build/krumpir/krumpir
```

Runs the scenario, simulates the same FMU, parameters and inputs with FMPy, and compares every
logged output (default tolerance 1e-9, `--tol` to change it, `--plot out.png` for an overlay).
Exit status: 0 pass, 1 mismatch, 2 error, 77 FMU not installed.

## Signals in the first scenarios

| FMU | Inputs | Outputs | Parameters |
| --- | --- | --- | --- |
| Feedthrough | `Float64_continuous_input`, `Float64_discrete_input` | `Float64_continuous_output`, `Float64_discrete_output` | `Float64_fixed_parameter`, `Float64_tunable_parameter` |
| Dahlquist | none | `x` | `k` |
| VanDerPol | none | `x0`, `x1` | `mu` |
| dcmotor | `duty`, `load_torque` | `speed`, `speed_rpm`, `current` | `R`, `L`, `Kt`, `Ke`, `J`, `b`, `Vbus` |
| t12tip | `heater`, `series_on`, `ambient_c`, `load_wk`, `fault` | `v_amp`, `i_heater`, `t_heater_c`, `t_tip_c`, `t_handle_c` | `c_h` ... `tau_sense` |

The Reference FMUs are unitless; the plant FMUs (`dcmotor`, `t12tip`, built from `libs/plants`,
see [../libs/plants/README.md](../libs/plants/README.md)) use SI units.

## Layout

```
src/         app, core, fmi, io, telemetry
vendor/      vendored FMI headers, miniz, tomlc99
fmus/        Reference FMUs (not in git, see fmus/README.md)
scenarios/   scenario.toml files
tests/       unit tests and golden CSVs
tools/       setup and comparison scripts
```
