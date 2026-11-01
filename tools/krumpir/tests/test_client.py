"""The client against the service, with the Reference FMU Feedthrough (output = input)."""
import csv

import pytest

from krumpir import KrumpirError, Signal

IN, OUT = "Float64_continuous_input", "Float64_continuous_output"

SCENARIO = f"""
[model]
fmu = "fmus/Feedthrough.fmu"
step = 0.001
[rt]
wait = "sleep"
max_overruns = 1000000000
[io]
backend = "null"
inputs = ["{IN}"]
outputs = ["{OUT}"]
[log]
file = "runs/client_test.csv"
"""


def test_signal_builder():
    s = Signal().const(1, 0.5).ramp(2, 1.0).pulse(1, 0.1, 0.5, duration=1).idle().loops(1)
    assert s.stimulus() == {"loops": 1, "segments": [
        {"kind": "const", "value": 1, "duration": 0.5},
        {"kind": "ramp", "to": 2, "duration": 1.0},
        {"kind": "pulse", "high": 1, "period": 0.1, "duty": 0.5, "low": 0.0, "duration": 1},
        {"kind": "idle"},
    ]}
    assert Signal().ramp(1, 2, frm=-1).segments[0]["from"] == -1


def test_info(tb):
    assert tb.info()["protocol"] == 1


def test_model_access(ma):
    assert ma.load_toml(SCENARIO) == "INITIALIZED"
    info = ma.variables()
    assert info[IN]["causality"] == "input" and info["Float64_tunable_parameter"]["tunable"]
    assert IN in ma.variable_names()

    ma.write({IN: 2.5})
    ma.write(Float64_tunable_parameter=1.5)
    assert ma.read("Float64_tunable_parameter") == 1.5
    with pytest.raises(KrumpirError, match="fixed parameter"):
        ma.write(Float64_fixed_parameter=1.0)
    with pytest.raises(KrumpirError, match="not a variable"):
        ma.read("nope")

    assert ma.start() == "RUNNING"
    assert not ma.wait(timeout=0.05)
    assert ma.read(OUT, IN) == {OUT: 2.5, IN: 2.5}
    assert ma.stop() == "STOPPED"
    summary = ma.unload()
    assert summary["stopped_early"] and summary["steps"] > 0 and summary["dropped"] == 0


def test_signal_and_capture(ma, tmp_path):
    ma.load_toml(SCENARIO)
    ma.signal(IN, Signal().const(0, 0.02).ramp(1, 0.1).const(5))
    ramp = ma.capture([OUT], start=(OUT, ">", 0), stop=(OUT, ">=", 1))
    later = ma.capture(start=(OUT, ">=", 5), duration=0.03)
    ma.start()

    res = ramp.fetch(wait=True, timeout=5, page=7)  # several pages
    assert res.state == "FINISHED" and res.names == ["t", OUT]
    out = res[OUT]
    assert 0 < out[0] < 0.02 and out[-1] >= 1 and all(b >= a for a, b in zip(out, out[1:]))
    assert 95 <= len(res) <= 101  # 0.1 s of ramp at 1 ms

    res = later.fetch(wait=True, timeout=5)
    assert len(res) == 30 and set(res[IN]) == {5.0} and res.names == ["t", IN, OUT]
    res.to_csv(tmp_path / "c.csv")
    with open(tmp_path / "c.csv") as f:
        rows = list(csv.reader(f))
    assert rows[0] == ["t", IN, OUT] and len(rows) == 31

    stopped = ma.capture([IN])
    stopped.stop()
    assert stopped.state == "FINISHED"
    ma.stop()


def test_wait_for_the_end(ma):
    ma.load_toml(SCENARIO.replace("step = 0.001", "step = 0.001\nduration = 0.05"))
    ma.signal(IN, {"kind": "constant", "value": 3})
    ma.start()
    assert ma.wait(timeout=5) and ma.state == "STOPPED"
    assert ma.read(OUT) == 3.0
    assert not ma.unload()["stopped_early"]
