#!/usr/bin/env python3
"""Capture the CSV telemetry of an ESP32 controller console into a file.

Usage:
  python tools/serial/capture.py --port COM4 --duration 10 --cmd "sp 350" "start" -o docs/capture.csv
Self-check (no board needed):
  python tools/serial/capture.py --selftest

Sends `log off` and `log on` (the firmware then prints its CSV header), runs each --cmd, records
for --duration seconds, then sends `log off`. Any controller works: the columns come from the
header the firmware prints. The first column (t_ms) becomes t_s from zero; a `_permille` column is
divided by 1000 and loses the suffix; a `_ma` column becomes amps (`_a`). Close the serial monitor
first, the port can only be open once. Needs pyserial.
"""
import argparse
import re
import sys
import time

HEADER_LINE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(,[A-Za-z_][A-Za-z0-9_]*)+$")
ROW_LINE = re.compile(r"^-?\d+(,-?\d+)+$")


def convert_header(names):
    """Output column names: t_ms -> t_s, *_permille -> *, *_ma -> *_a."""
    out = []
    for n in names:
        if n == "t_ms":
            out.append("t_s")
        elif n.endswith("_permille"):
            out.append(n[: -len("_permille")])
        elif n.endswith("_ma"):
            out.append(n[: -len("_ma")] + "_a")
        else:
            out.append(n)
    return out


def convert_row(names, values, t0):
    """Output values for one row; t0 is the first timestamp in ms."""
    out = []
    for n, v in zip(names, values):
        if n == "t_ms":
            out.append(f"{(v - t0) / 1000:.3f}")
        elif n.endswith("_permille") or n.endswith("_ma"):
            out.append(f"{v / 1000:.3f}")
        else:
            out.append(str(v))
    return out


def read_lines(port, seconds):
    end = time.time() + seconds
    while time.time() < end:
        line = port.readline().decode(errors="replace").strip()
        if line:
            yield line


def capture(port, cmds, duration):
    """Run the commands and return (header names, rows of ints) from the telemetry stream."""
    names, rows = None, []

    def send(text, wait=0.2):
        port.write((text + "\n").encode())
        time.sleep(wait)

    port.reset_input_buffer()
    send("log off")
    send("log on")
    for c in cmds:
        send(c)
    for line in read_lines(port, duration):
        # the console may echo what was typed in front of the first row: keep the last token
        line = line.split()[-1] if line.split() else line
        if names is None and HEADER_LINE.match(line):
            names = line.split(",")
        elif names and ROW_LINE.match(line):
            values = [int(x) for x in line.split(",")]
            if len(values) == len(names):
                rows.append(values)
    send("log off", wait=0.0)
    return names, rows


def write_csv(path, names, rows):
    t0 = rows[0][0]
    with open(path, "w", newline="") as f:
        f.write(",".join(convert_header(names)) + "\n")
        for values in rows:
            f.write(",".join(convert_row(names, values, t0)) + "\n")


class FakePort:
    """A console that answers `log on` with a header and a few rows, for the self-test."""

    def __init__(self):
        self.sent, self.queue = [], []

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False

    def reset_input_buffer(self):
        self.queue.clear()

    def write(self, data):
        text = data.decode().strip()
        self.sent.append(text)
        if text == "log on":
            self.queue += ["t_ms,setpoint_c,temp_c,duty_permille,current_ma",
                           "t12>  1000,350,25,1000,2999", "1099,350,31,1000,2999",
                           "1201,350,38,920,-1", "not a row", "1300,350,45"]  # last row is short

    def readline(self):
        time.sleep(0.005)
        return (self.queue.pop(0) + "\n").encode() if self.queue else b""


def selftest():
    port = FakePort()
    names, rows = capture(port, ["sp 350", "start"], 0.3)
    assert port.sent == ["log off", "log on", "sp 350", "start", "log off"], port.sent
    assert names == ["t_ms", "setpoint_c", "temp_c", "duty_permille", "current_ma"], names
    assert len(rows) == 3, rows  # the short row and the text are ignored
    assert convert_header(names) == ["t_s", "setpoint_c", "temp_c", "duty", "current_a"]
    assert convert_row(names, rows[1], rows[0][0]) == ["0.099", "350", "31", "1.000", "2.999"]
    assert convert_row(names, rows[2], rows[0][0])[4] == "-0.001"
    assert convert_header(["t_ms", "speed_rpm", "duty_permille"]) == ["t_s", "speed_rpm", "duty"]
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=5.0, help="seconds to record")
    ap.add_argument("--cmd", nargs="*", default=[], help="console commands sent after log on")
    ap.add_argument("-o", "--output")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.port or not args.output:
        ap.error("--port and -o/--output are required")

    import serial

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        sys.exit(f"cannot open {args.port}: {e} (is the serial monitor still open?)")
    with port:
        names, rows = capture(port, args.cmd, args.duration)
    if not names or not rows:
        sys.exit("no telemetry received (check the port, the baud rate and that the firmware runs)")
    write_csv(args.output, names, rows)
    print(f"{len(rows)} samples -> {args.output}")


if __name__ == "__main__":
    main()
