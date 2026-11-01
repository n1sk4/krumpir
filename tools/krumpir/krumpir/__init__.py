"""Python client of the Krumpir model-access service (runner/docs/service-protocol.md).

The classes follow ASAM XIL: Testbench (the connection), MAPort (model access), Signal (the
signal generator's segments) and Capture. Method names are Python style; each docstring names
the XIL operation it stands for.

    from krumpir import Testbench, Signal

    with Testbench("127.0.0.1", 7700) as tb:
        ma = tb.ma_port()
        ma.load("scenarios/dcmotor.toml")
        ma.signal("duty", Signal().const(0.2, 1.0).ramp(0.8, 0.5))
        cap = ma.capture(["speed"], start=("speed", ">", 10), duration=0.5)
        ma.start()
        print(cap.fetch(wait=True)["speed"][-1])
        ma.stop()
        ma.unload()
"""
import csv
import json
import socket
import time

__all__ = ["KrumpirError", "Testbench", "MAPort", "Signal", "Capture", "CaptureResult"]


class KrumpirError(RuntimeError):
    """The service refused a request; the message is its reason."""


class Testbench:
    """A connection to `krumpir serve` (XIL: Testbench). One request at a time."""

    def __init__(self, host="127.0.0.1", port=7700, timeout=30.0):
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._file = self._sock.makefile("rwb")

    def request(self, op, **args):
        """Sends one request and returns the reply without "ok"; raises KrumpirError."""
        self._file.write(json.dumps({"op": op, **args}).encode() + b"\n")
        self._file.flush()
        line = self._file.readline()
        if not line:
            raise ConnectionError("the service closed the connection")
        reply = json.loads(line)
        if not reply.pop("ok"):
            raise KrumpirError(f"{op}: {reply['error']}")
        return reply

    def info(self):
        return self.request("info")

    def ma_port(self):
        """XIL: MAPortFactory.CreateMAPort."""
        return MAPort(self)

    def close(self):
        self._file.close()
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class MAPort:
    """Model access: one scenario loaded on the service (XIL: MAPort)."""

    def __init__(self, tb):
        self._tb = tb

    def load(self, path):
        """Loads a scenario file that is on the server (XIL: LoadConfiguration + Configure)."""
        return self._tb.request("load", scenario=str(path))["state"]

    def load_toml(self, text):
        """Loads a scenario given as TOML text, e.g. a file read on this machine."""
        return self._tb.request("load", toml=text)["state"]

    def variables(self):
        """{name: {vr, type, causality, tunable}} (XIL: GetVariableInfo)."""
        return {v.pop("name"): v for v in self._tb.request("variables")["variables"]}

    def variable_names(self):
        """XIL: VariableNames."""
        return list(self.variables())

    def start(self):
        """XIL: StartSimulation."""
        return self._tb.request("start")["state"]

    def stop(self):
        """XIL: StopSimulation."""
        return self._tb.request("stop")["state"]

    @property
    def state(self):
        """IDLE, LOADED, INITIALIZED, RUNNING, STOPPED or FAULT (XIL: State)."""
        return self._tb.request("state")["state"]

    @property
    def fault(self):
        """Why the run went to FAULT ("" otherwise)."""
        return self._tb.request("state")["fault"]

    def wait(self, timeout=None):
        """Waits for the run to end (duration, stop or fault). Returns False on timeout (s)."""
        end = None if timeout is None else time.monotonic() + timeout
        while True:  # in short requests, so the socket timeout never fires
            left = 1.0 if end is None else min(1.0, end - time.monotonic())
            if self._tb.request("wait", timeout_ms=max(0, left * 1000))["done"]:
                return True
            if end is not None and time.monotonic() >= end:
                return False

    def unload(self):
        """Frees the run; returns its summary (steps, overruns, dropped, rows, timing)."""
        return self._tb.request("unload")["summary"]

    def read(self, *names):
        """One name: its value. Several: {name: value} (XIL: Read)."""
        values = self._tb.request("read", names=list(names))["values"]
        return values[names[0]] if len(names) == 1 else values

    def write(self, values=None, /, **kw):
        """Writes inputs and tunable parameters, as a dict or keywords (XIL: Write). While
        running, returns once the values are applied, at a step boundary."""
        self._tb.request("write", values={**(values or {}), **kw})

    def signal(self, input, signal):
        """Plays `signal` (a Signal, or a dict of io.stimulus keys) on `input` from now on
        (XIL: SignalGenerator.LoadSignalDescriptionSet + Start)."""
        stim = signal.stimulus() if isinstance(signal, Signal) else dict(signal)
        self._tb.request("signal", stimulus={"input": input, **stim})

    def capture(self, vars=(), *, start=None, stop=None, duration=None, max_rows=None):
        """Arms a capture of scenario inputs and outputs (all if `vars` is empty). Triggers are
        (var, op, value) with op one of >, >=, <, <= (XIL: CreateCapture, SetStartTrigger,
        SetStopTrigger with condition or duration watchers, Start)."""
        args = {"vars": list(vars)}
        for key, cond in (("start", start), ("stop", stop)):
            if cond is not None:
                var, op, value = cond
                args[key] = {"var": var, "op": op, "value": value}
        if duration is not None:
            args["duration"] = duration
        if max_rows is not None:
            args["max_rows"] = max_rows
        return Capture(self._tb, self._tb.request("capture_start", **args)["id"])


class Signal:
    """A signal as segments played in order (XIL: SignalDescription with its segment types).
    A segment without a duration runs to the end of the run (the last one only)."""

    def __init__(self):
        self.segments = []
        self._loops = 1

    def _add(self, kind, duration, **keys):
        seg = {"kind": kind, **{k: v for k, v in keys.items() if v is not None}}
        if duration is not None:
            seg["duration"] = duration
        self.segments.append(seg)
        return self

    def const(self, value, duration=None):
        return self._add("const", duration, value=value)

    def ramp(self, to, duration, frm=None):
        """From `frm` (default: where the previous segment ended) to `to`."""
        return self._add("ramp", duration, to=to, **{"from": frm})

    def sine(self, amplitude, freq_hz, duration=None, offset=0.0, phase=0.0):
        return self._add("sine", duration, amplitude=amplitude, freq_hz=freq_hz, offset=offset,
                         phase=phase)

    def pulse(self, high, period, duty, duration=None, low=0.0):
        return self._add("pulse", duration, high=high, period=period, duty=duty, low=low)

    def idle(self, duration=None):
        """Holds the previous segment's end value."""
        return self._add("idle", duration)

    def data(self, file, duration=None, interpolation="linear"):
        """time,value rows from a CSV file on the server."""
        return self._add("data", duration, file=str(file), interpolation=interpolation)

    def loops(self, n):
        """Plays the segments n times (0: forever)."""
        self._loops = n
        return self

    def stimulus(self):
        """The io.stimulus keys of this signal (without the input)."""
        return {"segments": self.segments, "loops": self._loops}


class Capture:
    """A capture on the service (XIL: Capture)."""

    def __init__(self, tb, id):
        self._tb = tb
        self.id = id

    @property
    def state(self):
        """ARMED, RUNNING or FINISHED (XIL: Capture.State)."""
        return self._tb.request("capture_fetch", id=self.id, max=0)["capture_state"]

    def stop(self):
        """Ends the capture now; it keeps its rows (XIL: Capture.Stop)."""
        self._tb.request("capture_stop", id=self.id)

    def fetch(self, wait=False, timeout=None, page=20000):
        """The rows recorded so far; with wait=True, once the capture has finished
        (XIL: Capture.Fetch). Raises TimeoutError after `timeout` s."""
        end = None if timeout is None else time.monotonic() + timeout
        while wait and self.state != "FINISHED":
            if end is not None and time.monotonic() >= end:
                raise TimeoutError(f"capture {self.id} has not finished")
            time.sleep(0.02)
        first = self._tb.request("capture_fetch", id=self.id, max=page)
        rows, total = first["rows"], first["n_rows"]
        while len(rows) < total:
            rows += self._tb.request("capture_fetch", id=self.id, max=page,
                                     **{"from": len(rows)})["rows"]
        return CaptureResult(first["names"], rows[:total], first["capture_state"])


class CaptureResult:
    """Captured rows: res["speed"] is a column, res.rows the rows ([t, v1, ...])."""

    def __init__(self, names, rows, state):
        self.names, self.rows, self.state = names, rows, state

    def __getitem__(self, name):
        i = self.names.index(name)
        return [r[i] for r in self.rows]

    def __len__(self):
        return len(self.rows)

    def columns(self):
        return {n: self[n] for n in self.names}

    def to_csv(self, path):
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(self.names)
            w.writerows(self.rows)
