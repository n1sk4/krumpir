# Controller code generation

Each controller is described by `controllers/<name>/controller.toml`; `gen_controller.py` turns
the description into the glue around the hand-written component (in AUTOSAR terms: the runtime
tables and the build configuration).

```sh
pip install -r tools/codegen/requirements.txt          # jinja2
python tools/codegen/gen_controller.py controllers/esp32-motor-controller          # write
python tools/codegen/gen_controller.py controllers/esp32-motor-controller --check  # CI: up to date?
python tools/codegen/test_gen_controller.py            # self-check of the generator
```

| Description | Generated |
|-------------|-----------|
| `[controller]` name, plant, lib | console prompt, calibration page name, `platformio.ini`, CMake files, `sdkconfig.defaults`, `.github/workflows/<dir>.yml` |
| `[[param]]` name, help, hint, min, max, default, unit, cal | `src/gen/ctrl_gen.{h,c}`: `PARAM_*` ids and the `rte_param_t` table (console commands, limits, boot values); `src/gen/ctrl_gen_task.inc`: the XCP calibration page for the params with `cal` |
| `[[command]]` name, help, hint, handler, after_run | console registration in help order; the handlers stay in `src/console.c` |
| `[[telemetry.column]]` name, value, scale, nan | CSV header, row format, and `CTRL_GEN_TELEM_VALUES` for the control task |
| `[[ci.step]]` name, run | the controller's own CI steps, after the common ones |

Hand-written and never touched: `lib/<lib>` (the component), the hooks and plumbing in
`src/control_task.c`, the command handlers in `src/console.c`, `src/main.c`.

Generated files carry a "do not edit" banner and are committed, so a build needs no generator.
The controller's CI regenerates and fails when a committed file differs from its description.
`ctrl_gen_task.inc` is included by `control_task.c` (not compiled on its own): the calibration page
must be in the control task's compilation unit, which `xcpclient` reads the A2L from.

The description is checked before anything is written: unknown keys, missing keys, ranges
(min < max, default inside), C identifiers, duplicate names, the number of parameters (16) and of
telemetry columns (`TELEM_VALUES`).
