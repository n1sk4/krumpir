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
~/build/krumpir/krumpir run scenarios/dahlquist.toml  # run a scenario, write runs/dahlquist.csv
```

Paths in a scenario are relative to the directory `krumpir` is started from. Ctrl-C stops a run
cleanly after the current step. Exit status: 0 ok, 1 setup error, 2 fault during the run.

### Log columns

`n,t,<inputs>,<outputs>,wake_ns,step_ns,flags`. Row `n` is one step: `t = (n+1)*step` is the time
at the end of the step, the inputs are the values applied at its start (`n*step`), and the outputs
are read after it. `wake_ns` is how late the tick started (0 when `wait = "none"`), `step_ns` is the
time spent in the FMU's step, and `flags` bit 0 marks an overrun.

The full list of scenario keys is in [docs/scenario-reference.md](docs/scenario-reference.md).

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

All values are plain model units (the Reference FMUs are unitless).

## Layout

```
src/         app, core, fmi, io, telemetry
vendor/      vendored FMI headers, miniz, tomlc99
fmus/        Reference FMUs (not in git, see fmus/README.md)
scenarios/   scenario.toml files
tests/       unit tests and golden CSVs
tools/       setup and comparison scripts
```
