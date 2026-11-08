# Runner

Real-time FMU runner, C11, Linux (the PC, WSL, the Pi). It loads an FMI 3.0 co-simulation FMU,
steps it at a fixed time step and exchanges signals with a device under test through a pluggable
I/O backend: `null` (the stimulus in the scenario) or `uart` (a controller on a serial line).
Design: [../docs/architecture.md](../docs/architecture.md). Building: [../BUILDING.md](../BUILDING.md)
(`python3 tools/kb.py build`; `<build>` below is the folder `kb.py info` prints).

## Run

From this folder:

```sh
<build>/runner/krumpir info fmus/Dahlquist.fmu                 # model description and variables
<build>/runner/krumpir init fmus/my.fmu > scenarios/my.toml    # starter scenario for an FMU
<build>/runner/krumpir run scenarios/dahlquist.toml            # run, write runs/dahlquist.csv
<build>/runner/krumpir serve                                   # drive runs over TCP
```

- `init` prints a scenario that runs as written: the FMU's default step and stop time, every
  Float64 parameter at its start value (with unit and description), every Float64 input and
  output, inputs with a non-zero start value held there, the `null` backend and
  `runs/<model>.csv` as the log. A scenario that no longer matches its FMU fails to load with the
  variable it trips on.
- Paths in a scenario are relative to the directory `krumpir` is started from. Ctrl-C stops a run
  cleanly after the current step. Exit status: 0 ok, 1 setup error, 2 fault during the run.
- Every scenario key: [docs/scenario-reference.md](docs/scenario-reference.md). The service
  protocol (JSON lines over TCP, one client at a time): [docs/service-protocol.md](docs/service-protocol.md).
- On the Pi, `[rt]` pins the run to an isolated core with SCHED_FIFO (`cpu`, `priority`,
  `wait = "busy"`), and `rt.timing_gpio` toggles a GPIO per step for a logic analyzer.
- With `backend = "uart"` a controller closes the loop: `scenarios/*_hil_pc.toml` run the
  controllers' own code built for this PC
  ([../controllers/README.md](../controllers/README.md#hil-on-the-pc-and-the-board)).

## Log columns

`n,t,<inputs>,<outputs>,wake_ns,step_ns,flags`. Row `n` is one step: `t = (n+1)*step` is the time
at the end of the step, the inputs are the values applied at its start, and the outputs are read
after it. `wake_ns` is how late the tick started (0 when `wait = "none"`), `step_ns` the time in
the FMU's step, and `flags` bit 0 marks an overrun.

## FMUs

`fmus/`, not in git:

| FMU | Inputs | Outputs | Parameters |
| --- | --- | --- | --- |
| Feedthrough | `Float64_continuous_input`, `Float64_discrete_input` | `Float64_continuous_output`, `Float64_discrete_output` | `Float64_fixed_parameter`, `Float64_tunable_parameter` |
| Dahlquist | none | `x` | `k` |
| VanDerPol | none | `x0`, `x1` | `mu` |
| dcmotor | `duty`, `load_torque`, `fault` | `speed`, `speed_rpm`, `current` | `R`, `L`, `Kt`, `Ke`, `J`, `b`, `Vbus` |
| t12tip | `heater`, `series_on`, `ambient_c`, `load_wk`, `fault` | `v_amp`, `i_heater`, `t_heater_c`, `t_tip_c`, `t_handle_c` | `c_h` ... `tau_sense` |

The first three come from the [Reference FMUs](https://github.com/modelica/Reference-FMUs)
release (unitless). The build downloads them when they are missing, checked against their SHA-256
(`../cmake/versions.cmake`); by hand: `cmake -P cmake/fetch_reference_fmus.cmake` from the
repository root. The plant FMUs `dcmotor` and `t12tip` (SI units) are built from `libs/plants` by
the same build ([../libs/README.md](../libs/README.md#plant-models)). Tests that need a missing
FMU report SKIPPED.

## Tests

`ctest` on the build: C unit and integration tests; `fmpy_*` run a scenario and simulate the same
FMU, parameters and inputs with FMPy, comparing every output (`tools/compare_fmpy.py`, tolerance
1e-9, `--plot out.png` for an overlay); `golden_*` replay `tests/golden/*.toml` against their CSVs
(tolerance 1e-12, the last bits can differ between CPUs); `client` runs the Python client's tests;
`hil_pc_*` and `hil_compare_*` the closed loops. After an intended change, regenerate a golden
file from this folder:

```sh
python3 tools/check_golden.py <build>/runner/krumpir tests/golden/dahlquist.toml \
  tests/golden/dahlquist.csv --update
```

## Vendored code

Copied unmodified from the pinned release, built without our warnings:

| Folder | Project | Version | License | Used by |
|---|---|---|---|---|
| `vendor/tomlc99/` | [cktan/tomlc99](https://github.com/cktan/tomlc99) | commit `29076dfd095bbbbd50a3c1b2760d29f4b83e74ac` | MIT | scenario parsing |
| `vendor/miniz/` | [richgel999/miniz](https://github.com/richgel999/miniz) | 3.1.2, amalgamated (`MINIZ_NO_ZLIB_APIS`) | MIT | unpacking FMUs |
| `vendor/cjson/` | [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | `v1.7.18` | MIT | the service protocol |
| `vendor/fmi3/` | [modelica/fmi-standard](https://github.com/modelica/fmi-standard) | `v3.0.2` (`headers/`) | BSD-2-Clause | the FMI 3.0 backend |

System dependency: `libexpat` (`libexpat1-dev`).

| File | sha256 |
|------|--------|
| `vendor/tomlc99/toml.c` | `1ed386df52a6d7a9ee8341c35af6689e15ecc78e053e246ede1a2eb937171e26` |
| `vendor/tomlc99/toml.h` | `54743ee323a09a99f53e409de0e14890db0bd5afcd5ac7f0b867e6e238eb594a` |
| `vendor/fmi3/fmi3FunctionTypes.h` | `d247d1c5fbf58e32379f1d38e7f73990709e0501fc4a9b3901ce3f9638503a0f` |
| `vendor/fmi3/fmi3Functions.h` | `038358fb0209c3547fe8c6f03b80582960c93ce0a7d342b0d9348c8ce6137784` |
| `vendor/fmi3/fmi3PlatformTypes.h` | `1aa81efddeb84b0d9425f9226969525bd034cb63848c086b6696655179500616` |
| `vendor/cjson/cJSON.c` | `75c51de8fa40ac9d7a99319c6330719bd692eb81c0a869265f3d4c682533f9b9` |
| `vendor/cjson/cJSON.h` | `0578cc29132912edbc88f83207a8fc76e5db3db0605497e909a9384ef3cc474b` |
| `vendor/miniz/miniz.c` | `e2c1aeb66eef9191d8c3feb164db2def2335a61d039bf04ed849f6b042433b30` |
| `vendor/miniz/miniz.h` | `b53b62ed122e559b8f679e3cb787a0b0035fe87a58f909da0e44931678f4e85f` |
