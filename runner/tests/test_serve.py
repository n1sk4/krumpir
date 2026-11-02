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
        starter = c.call("init", fmu=str(fmu))["toml"]
        assert f'"{IN}",' in starter and f'"{OUT}",' in starter, starter
        starter = starter.replace('"runs/Feedthrough.csv"', f'"{log}"')
        assert c.call("load", toml=starter)["state"] == "INITIALIZED"
        c.call("unload")

        assert c.call("load", toml=scenario)["state"] == "INITIALIZED"
        names = {v["name"]: v for v in c.call("variables")["variables"]}
        assert names[IN]["causality"] == "input" and names["Float64_tunable_parameter"]["tunable"]

        c.call("signal", stimulus={"input": IN, "segments": [{"kind": "const", "value": 4}]})
        c.fails("signal", "unknown key 'bogus'", stimulus={"input": IN, "kind": "constant",
                                                          "value": 1, "bogus": 2})
        cap = c.call("capture_start", vars=[OUT], duration=0.05,
                     start={"var": OUT, "op": ">=", "value": 4})["id"]
        c.fails("capture_start", "op must be", start={"var": OUT, "op": "=", "value": 1})
        assert c.call("start")["state"] == "RUNNING"
        c.close()  # the run goes on without a client

        c = Client(port)
        assert c.call("state")["state"] == "RUNNING"
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
        s = c.call("unload")["summary"]
        assert s["state"] == "STOPPED" and s["stopped_early"] and s["steps"] == s["rows"] > 100
        assert c.call("state")["state"] == "IDLE"
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
