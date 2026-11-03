#!/usr/bin/env python3
"""Check the control-loop timing of an ESP32 controller on the connected board: PASS or FAIL.

Measure the firmware already on the board (start the controller with --setup):
  python tools/serial/timing_check.py --port COM4 --setup "sp 350" "start" --teardown "stop"
Build and flash the project first (--flash), optionally with a busy-wait load in the loop:
  python tools/serial/timing_check.py --port COM4 --project controllers/esp32-t12-controller --flash
  python tools/serial/timing_check.py --port COM4 --project controllers/esp32-t12-controller --busy-us 200
Measure with CSV telemetry streaming (log on), to show that logging does not disturb the loop:
  python tools/serial/timing_check.py --port COM4 --log --rate-hz 10 --setup "sp 350" "start"
Self-check of the parser and the limits (no board needed):
  python tools/serial/timing_check.py --selftest

It reads the `loop (since last status)` line that `status` prints, so it works with any controller
built on libs/esp/espglue. Limits (flags in brackets): 0 overruns, period within +-50 us of the
nominal one (--max-jitter-us, --period-us), execution time under --max-exec-us (plus --busy-us),
at least 90 % of the expected cycles. With --log it also requires 0 dropped telemetry samples and
at least 90 % of the expected CSV rows (--rate-hz per second).

Exit code: 0 pass, 1 timing out of limits, 2 could not talk to the board.
Close the serial monitor first, the port can only be open once. Needs pyserial.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

LOOP_LINE = re.compile(
    r"period ns min/mean/max (\d+)/(\d+)/(\d+)\s+exec ns min/mean/max (\d+)/(\d+)/(\d+)"
    r"\s+overruns (\d+)\s+n=(\d+)"
)
DROPPED = re.compile(r"telemetry dropped (\d+)")
CSV_ROW = re.compile(r"^\d+(,-?\d+)+$", re.MULTILINE)
FIELDS = ("period_min", "period_mean", "period_max", "exec_min", "exec_mean", "exec_max",
          "overruns", "n")


def parse_loop_line(text):
    """Return the loop stats of a status reply as a dict (ns and counts), or None."""
    m = LOOP_LINE.search(text)
    if not m:
        return None
    s = dict(zip(FIELDS, (int(g) for g in m.groups())))
    d = DROPPED.search(text)
    if d:
        s["dropped"] = int(d.group(1))
    return s


def evaluate(s, duration, period_us, max_jitter_us, max_exec_us, rate_hz, log=False):
    """Return a list of failure messages; empty means PASS."""
    fails = []
    period_ns = period_us * 1000
    if s["overruns"] != 0:
        fails.append(f"overruns {s['overruns']} (must be 0)")
    jitter = max(period_ns - s["period_min"], s["period_max"] - period_ns) / 1000
    if jitter > max_jitter_us:
        fails.append(f"period jitter {jitter:.1f} us (limit {max_jitter_us} us)")
    if s["exec_max"] / 1000 > max_exec_us:
        fails.append(f"exec max {s['exec_max'] / 1000:.1f} us (limit {max_exec_us} us)")
    expected = duration * 1e6 / period_us
    if s["n"] < 0.9 * expected:
        fails.append(f"only {s['n']} cycles in {duration} s (expected about {expected:.0f})")
    if log:
        if s.get("dropped", 0) != 0:
            fails.append(f"telemetry dropped {s['dropped']} samples (must be 0)")
        if s.get("rows", 0) < 0.9 * duration * rate_hz:
            fails.append(f"only {s.get('rows', 0)} CSV rows received "
                         f"(expected about {int(duration * rate_hz)})")
    return fails


def find_pio():
    exe = shutil.which("pio")
    if exe:
        return exe
    home = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
    return home if os.path.exists(home) else None


def flash(port, project, busy_us):
    pio = find_pio()
    if not pio:
        sys.exit("pio not found: add ~/.platformio/penv/Scripts to PATH")
    env = dict(os.environ)
    env.pop("PLATFORMIO_BUILD_FLAGS", None)  # never inherit a stale load flag
    if busy_us:
        env["PLATFORMIO_BUILD_FLAGS"] = f"-DCONTROL_BUSY_US={busy_us}"
    cmd = [pio, "run", "-e", "esp32dev", "-t", "upload", "--upload-port", port]
    print("flashing:", " ".join(cmd), env.get("PLATFORMIO_BUILD_FLAGS", ""))
    if subprocess.run(cmd, cwd=project, env=env).returncode != 0:
        sys.exit(2)
    time.sleep(3)  # let the board boot


def read_for(port, seconds):
    """Read lines for a while (this also drains streaming telemetry) and return the text."""
    end = time.time() + seconds
    out = []
    while time.time() < end:
        out.append(port.readline().decode(errors="replace"))
    return "".join(out)


def measure(port, setup, teardown, duration, log):
    """Run the setup commands, then read the loop stats of a quiet measurement window."""
    def send(cmd, wait=0.2):
        port.write((cmd + "\n").encode())
        time.sleep(wait)

    port.reset_input_buffer()
    send("log off")
    for cmd in setup:
        send(cmd)
    time.sleep(1.0)
    send("status", wait=0.0)  # status resets the stats, so this discards the startup window
    read_for(port, 0.5)
    if log:
        send("log on", wait=0.0)
    streamed = read_for(port, duration)
    send("status", wait=0.0)
    reply = read_for(port, 1.5)
    send("log off")
    for cmd in teardown:
        send(cmd)
    s = parse_loop_line(reply)
    if s and log:
        s["rows"] = len(CSV_ROW.findall(streamed.replace(chr(13), "")))
    return s


class FakePort:
    """A console that answers `status` with a loop line, and streams rows after `log on`."""

    def __init__(self, status_reply):
        self.sent, self.queue, self.status_reply = [], [], status_reply

    def reset_input_buffer(self):
        self.queue.clear()

    def write(self, data):
        text = data.decode().strip()
        self.sent.append(text)
        if text == "status":
            self.queue += self.status_reply.splitlines()
        if text == "log on":
            self.queue += ["t_ms,a,b", "10,1,2", "20,1,3", "30,1,-4"]

    def readline(self):
        time.sleep(0.005)
        return (self.queue.pop(0) + "\n").encode() if self.queue else b""


def selftest():
    good = ("loop (since last status): period ns min/mean/max 999887/1000000/1000112  "
            "exec ns min/mean/max 2512/2525/2537  overruns 0  n=1000")
    s = parse_loop_line(good)
    assert s["period_min"] == 999887 and s["exec_max"] == 2537 and s["n"] == 1000, s
    lim = dict(duration=1, period_us=1000, max_jitter_us=50, max_exec_us=100, rate_hz=100)
    assert evaluate(s, **lim) == []
    assert any("overruns" in f for f in evaluate({**s, "overruns": 1}, **lim))
    assert any("jitter" in f for f in evaluate({**s, "period_max": 1_080_000}, **lim))
    assert any("jitter" in f for f in evaluate({**s, "period_min": 900_000}, **lim))
    assert any("exec" in f for f in evaluate({**s, "exec_max": 150_000}, **lim))
    assert any("cycles" in f for f in evaluate({**s, "n": 10}, **lim))
    assert evaluate({**s, "period_min": 1_900_000, "period_max": 2_100_000, "n": 500},
                    duration=1, period_us=2000, max_jitter_us=150, max_exec_us=100,
                    rate_hz=10) == []  # another loop rate
    assert parse_loop_line("motor> status") is None

    reply = ("running  sp 350 C  kp 0.05 ki 0.05 kd 0  ramp 0 K/s\n"
             "tip 350.0 C  duty 0.16  log on  telemetry dropped 3\n" + good)
    s = parse_loop_line(reply)
    assert s["dropped"] == 3
    logged = {**s, "dropped": 0, "rows": 100}
    assert evaluate(logged, log=True, **lim) == []
    assert any("dropped" in f for f in evaluate({**logged, "dropped": 2}, log=True, **lim))
    assert any("rows" in f for f in evaluate({**logged, "rows": 10}, log=True, **lim))
    assert len(CSV_ROW.findall("t_ms,a,b,c\n10,3000,0,1000\n20,3000,50,-990\nt12> ")) == 2

    port = FakePort(good)
    got = measure(port, ["sp 350", "start"], ["stop"], 0.2, True)
    assert port.sent == ["log off", "sp 350", "start", "status", "log on", "status", "log off",
                         "stop"], port.sent
    assert got["rows"] == 3 and got["n"] == 1000, got
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=5.0, help="measurement seconds")
    ap.add_argument("--setup", nargs="*", default=[], help="console commands that start the loop")
    ap.add_argument("--teardown", nargs="*", default=[], help="console commands sent at the end")
    ap.add_argument("--project", default=".", help="project folder for --flash")
    ap.add_argument("--flash", action="store_true", help="build and upload first")
    ap.add_argument("--busy-us", type=int, default=0, help="build with a busy-wait load (flashes)")
    ap.add_argument("--log", action="store_true", help="stream CSV telemetry while measuring")
    ap.add_argument("--rate-hz", type=float, default=100.0, help="telemetry rows per second")
    ap.add_argument("--period-us", type=float, default=1000.0, help="nominal loop period")
    ap.add_argument("--max-jitter-us", type=float, default=50.0)
    ap.add_argument("--max-exec-us", type=float, default=None, help="default 100 + busy-us")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.port:
        ap.error("--port is required")
    if args.max_exec_us is None:
        args.max_exec_us = 100.0 + args.busy_us
    if args.flash or args.busy_us:
        flash(args.port, args.project, args.busy_us)

    import serial

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"cannot open {args.port}: {e}")
        sys.exit(2)
    with port:
        s = measure(port, args.setup, args.teardown, args.duration, args.log)
    if not s:
        print("no loop stats received (check the port, baud and that the firmware is running)")
        sys.exit(2)
    print(f"period ns  min/mean/max {s['period_min']}/{s['period_mean']}/{s['period_max']}")
    print(f"exec   ns  min/mean/max {s['exec_min']}/{s['exec_mean']}/{s['exec_max']}")
    print(f"overruns {s['overruns']}  cycles {s['n']} "
          f"(window about {s['n'] * args.period_us / 1e6:.1f} s)")
    if args.log:
        print(f"telemetry rows {s.get('rows', 0)}  dropped {s.get('dropped', '?')}")
    fails = evaluate(s, args.duration, args.period_us, args.max_jitter_us, args.max_exec_us,
                     args.rate_hz, args.log)
    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else "FAIL")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
