# Tools

| Folder | What | More |
|---|---|---|
| `kb.py` | the one entry point to build, test and check everything | [../BUILDING.md](../BUILDING.md) |
| `krumpir/` | the Python client of the runner's service, and `EcuPort` for XCP | below |
| `codegen/` | controller code generation from `controller.toml` | [../controllers/README.md](../controllers/README.md#code-generation) |
| `serial/` | board scripts: `capture.py`, `timing_check.py` | [../controllers/README.md](../controllers/README.md#board-tools) |
| `kicad/` | the schematic writer behind each `generate_schematic.py` | [../controllers/README.md](../controllers/README.md#schematics) |
| `hil/` | the closed loop on the PC (`pc_loop.sh`), its checks, SIL vs PC / board (`compare.py`) | [../controllers/README.md](../controllers/README.md#hil-on-the-pc-and-the-board) |
| `check_links.py` | every relative link and anchor in the markdown resolves (`kb.py check`) | |

The Python packages for all of them: `requirements.txt` (installed by `kb.py setup`).

## Python client

`krumpir/`: a client of `krumpir serve` (protocol in
[../runner/docs/service-protocol.md](../runner/docs/service-protocol.md)), standard library only,
its classes after ASAM XIL (`Testbench`, `MAPort`, `Signal` segments, `Capture`); `EcuPort`
measures and calibrates a controller over XCP. `pip install -e tools/krumpir`.

```python
from krumpir import EcuPort, Signal, Testbench

with Testbench("127.0.0.1", 7700) as tb:
    ma = tb.ma_port()
    ma.load("scenarios/dcmotor.toml")                  # a path on the server
    ma.signal("duty", Signal().const(0.2, 1.0).ramp(0.8, 0.5).const(0.8))
    cap = ma.capture(["speed", "duty"], start=("duty", ">=", 0.5), duration=0.5)
    ma.start()
    res = cap.fetch(wait=True)                         # once the capture has finished
    res.to_csv("speed.csv")
    print(ma.unload())                                 # steps, overruns, timing

with EcuPort("controllers/esp32-t12-controller/docs/t12.a2l") as ecu:   # address from the A2L
    print(ecu.read("s_loop.temp", "s_loop.duty"))
    ecu.write({"t12_cal_page.setpoint": 300})
```

| Class | Methods | XIL |
|-------|---------|-----|
| `Testbench(host, port)` | `info()`, `scenario_for(fmu)` (as `krumpir init`), `ma_port()`, `close()` | Testbench |
| `MAPort` | `load(path)`, `load_toml(text)`, `variables()`, `variable_names()`, `start()`, `stop()`, `state`, `fault`, `wait(timeout)`, `reset()`, `unload()`, `read(*names)`, `write(dict, **values)`, `signal(input, Signal)`, `capture(vars, start, stop, duration, max_rows)` | MAPort |
| `Signal` | `const`, `ramp`, `sine`, `pulse`, `idle`, `data`, `loops` | SignalGenerator segments |
| `Capture` / `CaptureResult` | `state`, `fetch(wait, timeout)`, `stop()` / `res["name"]`, `rows`, `names`, `columns()`, `to_csv(path)` | Capture |
| `EcuPort(a2l)` | `read(*names)`, `write(dict)`: names as in the A2L, struct members joined by dots | ECUMPort, ECUCPort |

A refused request raises `KrumpirError`, an XCP problem `XcpError` (no answer, a refused command,
an EPK mismatch, an unknown name). `EcuPort` polls (about 5 ms per read over Wi-Fi); for recording
use `xcpclient --mea ... --csv`.

Tests: `cd tools/krumpir && python -m pytest` (the runner's CTest runs them as `client`).
`test_client.py` against the Reference FMU Feedthrough; `test_dcmotor.py` and `test_t12.py` the
plant models over the service (the T12 also a thermostat run by the test in real time);
`test_xcp.py` the A2L parser and XCP master against a fake ECU; `test_ecu_board.py` the T12 board,
only when `KRUMPIR_ECU_A2L` names the running build's A2L. The fixtures start `krumpir serve` from
`KRUMPIR_EXE` in `runner/`, or use a running service from `KRUMPIR_SERVICE=host:port`.
