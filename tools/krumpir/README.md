# krumpir (Python client)

Client of the model-access service (`krumpir serve`, protocol in
[runner/docs/service-protocol.md](../../runner/docs/service-protocol.md)). The classes follow ASAM
XIL: `Testbench`, `MAPort`, `Signal` (signal generator segments) and `Capture`. Standard library
only.

```sh
pip install -e tools/krumpir
```

```python
from krumpir import Testbench, Signal

with Testbench("127.0.0.1", 7700) as tb:
    ma = tb.ma_port()
    ma.load("scenarios/dcmotor.toml")                  # a path on the server
    ma.signal("duty", Signal().const(0.2, 1.0).ramp(0.8, 0.5).const(0.8))
    cap = ma.capture(["speed", "duty"], start=("duty", ">=", 0.5), duration=0.5)
    ma.start()
    res = cap.fetch(wait=True)                         # once the capture has finished
    print(res["speed"][-1])
    res.to_csv("speed.csv")
    ma.stop()
    print(ma.unload())                                 # steps, overruns, timing
```

| Class | Methods | XIL |
|-------|---------|-----|
| `Testbench(host, port)` | `info()`, `scenario_for(fmu)` (starter scenario text, as `krumpir init`), `ma_port()`, `close()` | Testbench |
| `MAPort` | `load(path)`, `load_toml(text)`, `variables()`, `variable_names()`, `start()`, `stop()`, `state`, `fault`, `wait(timeout)`, `reset()`, `unload()`, `read(*names)`, `write(dict, **values)`, `signal(input, Signal)`, `capture(vars, start, stop, duration, max_rows)` | MAPort |
| `Signal` | `const`, `ramp`, `sine`, `pulse`, `idle`, `data`, `loops` | SignalGenerator segments |
| `Capture` | `state`, `fetch(wait, timeout)`, `stop()` | Capture |
| `CaptureResult` | `res["name"]` (a column), `rows`, `names`, `columns()`, `to_csv(path)` | CaptureResult |

A refused request raises `KrumpirError` with the service's reason.

## EcuPort: the controller over XCP

`EcuPort` measures and calibrates a controller over XCP on UDP (XIL: ECUMPort and ECUCPort),
straight to the board, with the addresses and types from the A2L that `xcpclient` generates for the
running build (`controllers/esp32-t12-controller/README.md`, XCP). On connect it checks the EPK, so
an A2L of another build is refused.

```python
from krumpir import EcuPort

with EcuPort("controllers/esp32-t12-controller/docs/t12.a2l") as ecu:   # address from the A2L
    print(ecu.read("s_loop.temp", "s_loop.duty"))                      # measurement
    ecu.write({"t12_cal_page.setpoint": 300})                          # calibration
```

Names are the A2L's, struct members joined by dots; arrays read as lists. Reads and writes are
polled (about 5 ms each over Wi-Fi). Synchronous recording (DAQ) is not implemented: use
`xcpclient --mea ... --csv` for that. Errors raise `XcpError` (no answer, a refused command, EPK
mismatch, unknown name).

## Tests

```sh
cd tools/krumpir && python -m pytest
```

| File | What it shows |
|------|---------------|
| `test_client.py` | the client itself, against the Reference FMU Feedthrough |
| `test_dcmotor.py` | steady speed and current, step-response time constant, a load applied while the motor runs; expected values from the model's own parameters |
| `test_xcp.py` | the A2L parser and the XCP master, against a fake ECU (no hardware) |
| `test_ecu_board.py` | measurement and a calibration write on the T12 board; skipped unless `KRUMPIR_ECU_A2L` names the running build's A2L |
| `test_t12.py` | energy balance while heating, the thermocouple reading after switch-off, the four fault modes, and a two-point thermostat run by the test over the service in real time, with a load disturbance |

The `scenario` fixture builds a scenario for an FMU in `runner/fmus`: `wait="none"` runs as fast as
possible (set signals and captures before `start`), `wait="sleep"` runs in real time for tests
that act while the model runs.

The fixtures start `krumpir serve` from `KRUMPIR_EXE` (default `~/build/krumpir/krumpir`) in
`runner/`, or use a running service with `KRUMPIR_SERVICE=host:port`. The runner's CTest runs
them as the `client` test when pytest is installed.
