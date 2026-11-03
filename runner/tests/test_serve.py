#!/usr/bin/env python3
"""The model-access service end to end: starts `krumpir serve` on a free port and drives the
Reference FMU Feedthrough over TCP. Exit 77 (skipped) if the FMU is missing.

    test_serve.py <build>/krumpir      (run from runner/)
"""
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

IN, OUT = "Float64_continuous_input", "Float64_continuous_output"


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.file = self.sock.makefile("rwb")

    def raw(self, line):
        self.file.write(line.encode() + b"\n")
        self.file.flush()
        return json.loads(self.file.readline())

    def call(self, op, **args):
        reply = self.raw(json.dumps({"op": op, **args}))
        assert reply["ok"], f"{op}: {reply}"
        return reply

    def fails(self, op, why, **args):
        reply = self.raw(json.dumps({"op": op, **args}))
        assert not reply["ok"] and why in reply["error"], f"{op}: {reply}"

    def close(self):
        self.file.close()
        self.sock.close()


def main():
    exe = Path(sys.argv[1]).resolve()
    fmu = Path("fmus/Feedthrough.fmu").resolve()
    if not fmu.exists():
        print(f"SKIP: {fmu} not found")
        return 77
    log = Path(tempfile.gettempdir()) / f"krumpir_test_serve_{os.getpid()}.csv"
    scenario = f"""
[model]
fmu = "{fmu}"
step = 0.001
[rt]
wait = "sleep"
max_overruns = 1000000000
[io]
backend = "null"
inputs = ["{IN}"]
outputs = ["{OUT}"]
stimulus = [{{ input = "{IN}", kind = "constant", value = 1.0 }}]
[log]
file = "{log}"
"""
    srv = subprocess.Popen([str(exe), "serve", "127.0.0.1:0"], stdout=subprocess.PIPE, text=True)
    try:
        first = srv.stdout.readline().split()
        assert first[:2] == ["listening", "on"], first
        port = int(first[2].rsplit(":", 1)[1])

        c = Client(port)
        r = c.call("info", tag=7)
        assert r["tag"] == 7 and r["protocol"] == 1 and r["state"] == "IDLE"
        assert not c.raw("not json")["ok"]
        c.fails("nope", "unknown op")
        c.fails("variables", "no scenario loaded")
        c.fails("load", "scenario:", toml="[model]\n")
        c.fails("init", "takes \"fmu\"")
        c.fails("init", "cannot open", fmu="/nonexistent.fmu")
        files = c.call("files")  # the test runs from runner/
        assert "fmus/Feedthrough.fmu" in files["fmus"], files
        assert "scenarios/feedthrough.toml" in files["scenarios"], files
        assert "[model]" in c.call("text", path="scenarios/feedthrough.toml")["text"]
        c.fails("text", "of a .toml file", path="fmus/Feedthrough.fmu")
        c.fails("text", "cannot open", path="nope.toml")
        assert "t" not in c.call("state")  # progress only once loaded
        starter = c.call("init", fmu=str(fmu))["toml"]
        assert f'"{IN}",' in starter and f'"{OUT}",' in starter, starter
        starter = starter.replace('"runs/Feedthrough.csv"', f'"{log}"')
        assert c.call("load", toml=starter)["state"] == "INITIALIZED"
        c.call("unload")

        # describe: text, the scenario as JSON with defaults, the FMU's variables
        d = c.call("describe", scenario="scenarios/feedthrough.toml")
        assert "[model]" in d["text"] and "problem" not in d, d
        sc = d["scenario"]
        assert sc["rt"]["cpu"] is None and sc["rt"]["priority"] is None, sc
        assert sc["io"]["backend"] == "null" and isinstance(sc["model"]["params"], dict), sc
        v = {x["name"]: x for x in d["variables"]}
        assert {"unit", "start", "description"} <= v[IN].keys(), v[IN]
        d = c.call("describe", fmu=str(fmu))
        assert d["scenario"]["model"]["fmu"] == str(fmu) and d["variables"], d
        d = c.call("describe", toml="[model]\n")
        assert "missing" in d["problem"] and "scenario" not in d and d["text"] == "[model]\n", d
        d = c.call("describe", toml=scenario.replace(str(fmu), "/nonexistent.fmu"))
        assert "cannot open" in d["problem"] and "scenario" in d and "variables" not in d, d
        seg = scenario.replace('kind = "constant", value = 1.0',
                               'loops = 2, segments = [{ kind = "ramp", to = 2, duration = 0.5 }, '
                               '{ kind = "const", value = 1, duration = 1 }]')
        st = c.call("describe", toml=seg)["scenario"]["io"]["stimulus"][0]
        assert st == {"input": IN, "loops": 2, "segments": [
            {"kind": "ramp", "duration": 0.5, "to": 2}, {"kind": "const", "duration": 1, "value": 1}]}, st
        c.fails("describe", "describe takes", toml="x", fmu="y")
        c.fails("describe", "cannot open", scenario="nope.toml")
        assert c.call("state")["state"] == "IDLE"  # nothing was loaded

        # check: as load would, without loading
        c.call("check", toml=scenario)
        c.fails("check", "scenario: missing", toml="[model]\n")
        c.fails("check", "cannot open", toml=scenario.replace(str(fmu), "/nonexistent.fmu"))
        c.fails("check", "'nope' is not a variable", toml=scenario.replace(f'outputs = ["{OUT}"]',
                                                                           'outputs = ["nope"]'))
        c.fails("check", "cannot open data file", toml=scenario.replace(
            'kind = "constant", value = 1.0', 'segments = [{ kind = "data", file = "/nope.csv" }]'))

        # save: scenarios/<name>.toml only, checked first, no silent overwrite
        name = f"scenarios/test_save_{os.getpid()}.toml"
        try:
            c.call("save", path=name, toml=scenario)
            assert Path(name).read_text() == scenario and not Path(name + ".tmp").exists()
            c.fails("save", "exists", path=name, toml=scenario)
            c.call("save", path=name, toml=seg, overwrite=True)
            assert Path(name).read_text() == seg
            for bad in ["x.toml", "../x.toml", "scenarios/../x.toml", "scenarios/a/b.toml",
                        "scenarios/x.txt", "/tmp/x.toml", "scenarios/.toml", "scenarios/.x.toml"]:
                c.fails("save", "save takes", path=bad, toml=scenario)
            c.fails("save", "scenario:", path=name, toml="[model]\n", overwrite=True)
            assert Path(name).read_text() == seg  # a refused save leaves the file alone
        finally:
            Path(name).unlink(missing_ok=True)

        assert c.call("load", toml=scenario)["state"] == "INITIALIZED"
        names = {v["name"]: v for v in c.call("variables")["variables"]}
        assert names[IN]["causality"] == "input" and names["Float64_tunable_parameter"]["tunable"]
        assert "unit" in names[IN] and "start" in names[IN] and "description" in names[IN]

        c.call("signal", stimulus={"input": IN, "segments": [{"kind": "const", "value": 4}]})
        c.fails("signal", "unknown key 'bogus'", stimulus={"input": IN, "kind": "constant",
                                                          "value": 1, "bogus": 2})
        cap = c.call("capture_start", vars=[OUT], duration=0.05,
                     start={"var": OUT, "op": ">=", "value": 4})["id"]
        c.fails("capture_start", "op must be", start={"var": OUT, "op": "=", "value": 1})
        assert c.call("start")["state"] == "RUNNING"
        c.close()  # the run goes on without a client

        c = Client(port)
        for _ in range(200):  # a sanitizer build takes a while to its first step
            st = c.call("state")
            if st["steps"] > 0:
                break
            time.sleep(0.01)
        assert st["state"] == "RUNNING" and st["step"] == 0.001 and "duration" not in st
        assert st["steps"] > 0 and abs(st["t"] - st["steps"] * 0.001) < 1e-9 and st["fmu"] == str(fmu)
        assert st["max_step_ns"] > 0 and "overruns" in st and "max_wake_ns" in st
        assert not c.call("wait", timeout_ms=100)["done"]
        assert c.call("read", names=[OUT])["values"][OUT] == 4.0
        c.call("write", values={IN: 6.0})
        assert c.call("read", names=[OUT, IN])["values"] == {OUT: 6.0, IN: 6.0}
        c.fails("write", "fixed parameter", values={"Float64_fixed_parameter": 1})

        r = c.call("capture_fetch", id=cap)
        assert r["capture_state"] == "FINISHED" and r["n_rows"] == 50 and r["names"] == ["t", OUT]
        assert len(r["rows"]) == 50 and r["rows"][0][1] == 4.0
        page = c.call("capture_fetch", id=cap, **{"from": 10, "max": 5})
        assert page["from"] == 10 and page["rows"] == r["rows"][10:15]
        c.fails("capture_fetch", "no capture", id=5)

        assert c.call("stop")["state"] == "STOPPED"
        r = c.call("reset")  # STOPPED -> INITIALIZED: the same scenario again from t = 0
        s = r["summary"]
        assert r["state"] == "INITIALIZED", r
        assert s["state"] == "STOPPED" and s["stopped_early"] and s["steps"] == s["rows"] > 100
        c.fails("reset", "cannot reset in state INITIALIZED")
        assert c.call("read", names=[OUT])["values"][OUT] == 0.0  # a fresh FMU
        assert c.call("start")["state"] == "RUNNING"
        c.call("read", names=[OUT])  # answered at a step boundary: the loop is stepping
        assert c.call("stop")["state"] == "STOPPED"
        assert c.call("unload")["summary"]["steps"] > 0
        assert c.call("state")["state"] == "IDLE"
        c.fails("reset", "cannot reset in state IDLE")
        c.close()

        srv.send_signal(signal.SIGTERM)
        assert srv.wait(timeout=10) == 0
    finally:
        if srv.poll() is None:
            srv.kill()
        log.unlink(missing_ok=True)
    print("test_serve: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
