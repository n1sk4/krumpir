# Krumpir runner

Real-time FMU runner. Loads an FMI 3.0 co-simulation FMU, steps it at a fixed time step and
exchanges signals with a device under test through a pluggable I/O backend. Design:
[../docs/architecture.md](../docs/architecture.md).

## Setup (Debian/Ubuntu, WSL or Raspberry Pi OS)

```sh
bash runner/tools/setup_wsl.sh     # build tools, Python venv with FMPy (in ~/.venvs/krumpir)
bash runner/tools/fetch_fmus.sh    # Reference FMUs, see fmus/README.md
```

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
