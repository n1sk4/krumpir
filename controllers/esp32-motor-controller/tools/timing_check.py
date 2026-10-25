"""Check the 1 kHz loop timing on the connected ESP32 and report PASS or FAIL.

Measure the firmware already on the board (default):
  python tools/timing_check.py --port COM4
Build and flash first (add a busy-wait load inside the loop with --busy-us):
  python tools/timing_check.py --port COM4 --flash
  python tools/timing_check.py --port COM4 --busy-us 200
Measure with CSV telemetry streaming (log on) to show logging does not disturb the loop:
  python tools/timing_check.py --port COM4 --log
Self-check of the parser and limits (no board needed):
  python tools/timing_check.py --selftest

Exit code: 0 pass, 1 timing out of limits, 2 could not talk to the board.
Close the serial monitor first, the port can only be open once.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

PERIOD_NS = 1_000_000
TELEMETRY_HZ = 100
LOOP_LINE = re.compile(
    r"period ns min/mean/max (\d+)/(\d+)/(\d+)\s+exec ns min/mean/max (\d+)/(\d+)/(\d+)"
    r"\s+overruns (\d+)\s+n=(\d+)"
)
DROPPED = re.compile(r"telemetry dropped (\d+)")
CSV_ROW = re.compile(r"^\d+,-?\d+,-?\d+,-?\d+$", re.MULTILINE)
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


def evaluate(s, duration, max_jitter_us, max_exec_us, log=False):
    """Return a list of failure messages; empty means PASS."""
    fails = []
    if s["overruns"] != 0:
        fails.append(f"overruns {s['overruns']} (must be 0)")
    jitter = max(PERIOD_NS - s["period_min"], s["period_max"] - PERIOD_NS) / 1000
    if jitter > max_jitter_us:
        fails.append(f"period jitter {jitter:.1f} us (limit {max_jitter_us} us)")
    if s["exec_max"] / 1000 > max_exec_us:
        fails.append(f"exec max {s['exec_max'] / 1000:.1f} us (limit {max_exec_us} us)")
    if s["n"] < 0.9 * duration * 1000:
        fails.append(f"only {s['n']} cycles in {duration} s (expected about {duration * 1000})")
    if log:
        if s.get("dropped", 0) != 0:
            fails.append(f"telemetry dropped {s['dropped']} samples (must be 0)")
        if s.get("rows", 0) < 0.9 * duration * TELEMETRY_HZ:
            fails.append(f"only {s.get('rows', 0)} CSV rows received "
                         f"(expected about {int(duration * TELEMETRY_HZ)})")
    return fails


def find_pio():
    exe = shutil.which("pio")
    if exe:
        return exe
    home = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
    return home if os.path.exists(home) else None


def flash(port, busy_us):
    pio = find_pio()
    if not pio:
        sys.exit("pio not found: add ~/.platformio/penv/Scripts to PATH")
    env = dict(os.environ)
    env.pop("PLATFORMIO_BUILD_FLAGS", None)  # never inherit a stale load flag
    if busy_us:
        env["PLATFORMIO_BUILD_FLAGS"] = f"-DCONTROL_BUSY_US={busy_us}"
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    cmd = [pio, "run", "-e", "esp32dev", "-t", "upload", "--upload-port", port]
    print("flashing:", " ".join(cmd), env.get("PLATFORMIO_BUILD_FLAGS", ""))
    if subprocess.run(cmd, cwd=root, env=env).returncode != 0:
        sys.exit(2)
    time.sleep(3)  # let the board boot


def read_for(port, seconds):
    """Read lines for a while (this also drains streaming telemetry) and return the text."""
    end = time.time() + seconds
    out = []
    while time.time() < end:
        out.append(port.readline().decode(errors="replace"))
    return "".join(out)


def measure(args):
    import serial

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"cannot open {args.port}: {e}")
        sys.exit(2)
    with port:
        def send(cmd):
            port.write((cmd + "\n").encode())

        port.reset_input_buffer()
        for cmd in ("log off", "sp 3000", "start"):
            send(cmd)
            time.sleep(0.2)
        time.sleep(1.0)
        send("status")  # status resets the stats, so this discards the startup window
        read_for(port, 0.5)
        if args.log:
            send("log on")
        streamed = read_for(port, args.duration)
        send("status")
        reply = read_for(port, 1.5)
        send("log off")
        send("stop")
    s = parse_loop_line(reply)
    if s and args.log:
        s["rows"] = len(CSV_ROW.findall(streamed.replace(chr(13), "")))
    return s


def selftest():
    good = ("loop (since last status): period ns min/mean/max 999887/1000000/1000112  "
            "exec ns min/mean/max 2512/2525/2537  overruns 0  n=1000")
    s = parse_loop_line(good)
    assert s["period_min"] == 999887 and s["exec_max"] == 2537 and s["n"] == 1000, s
    assert evaluate(s, 1, 50, 100) == []
    assert any("overruns" in f for f in evaluate({**s, "overruns": 1}, 1, 50, 100))
    assert any("jitter" in f for f in evaluate({**s, "period_max": 1_080_000}, 1, 50, 100))
    assert any("jitter" in f for f in evaluate({**s, "period_min": 900_000}, 1, 50, 100))
    assert any("exec" in f for f in evaluate({**s, "exec_max": 150_000}, 1, 50, 100))
    assert any("cycles" in f for f in evaluate({**s, "n": 10}, 1, 50, 100))
    assert parse_loop_line("motor> status") is None

    reply = ("running  sp 3000 rpm  kp 0.00036 ki 0.015 kd 0\n"
             "speed 2999.0 rpm  duty 0.536  log on  telemetry dropped 3\n" + good)
    s = parse_loop_line(reply)
    assert s["dropped"] == 3
    logged = {**s, "dropped": 0, "rows": 100}
    assert evaluate(logged, 1, 50, 100, log=True) == []
    assert any("dropped" in f for f in evaluate({**logged, "dropped": 2}, 1, 50, 100, log=True))
    assert any("rows" in f for f in evaluate({**logged, "rows": 10}, 1, 50, 100, log=True))
    assert len(CSV_ROW.findall("t_ms,a,b,c\n10,3000,0,1000\n20,3000,50,990\nmotor> ")) == 2
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=5.0, help="measurement seconds")
    ap.add_argument("--flash", action="store_true", help="build and upload first")
    ap.add_argument("--busy-us", type=int, default=0, help="build with a busy-wait load (flashes)")
    ap.add_argument("--log", action="store_true", help="stream CSV telemetry while measuring")
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
        flash(args.port, args.busy_us)

    s = measure(args)
    if not s:
        print("no loop stats received (check the port, baud and that the firmware is running)")
        sys.exit(2)
    print(f"period ns  min/mean/max {s['period_min']}/{s['period_mean']}/{s['period_max']}")
    print(f"exec   ns  min/mean/max {s['exec_min']}/{s['exec_mean']}/{s['exec_max']}")
    print(f"overruns {s['overruns']}  cycles {s['n']} (window about {s['n'] / 1000:.1f} s)")
    if args.log:
        print(f"telemetry rows {s.get('rows', 0)}  dropped {s.get('dropped', '?')}")
    fails = evaluate(s, args.duration, args.max_jitter_us, args.max_exec_us, args.log)
    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else "FAIL")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
