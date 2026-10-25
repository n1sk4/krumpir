"""Capture CSV telemetry from the ESP32 console into a file plot.py can read.

Usage:
  python tools/capture.py --port COM4 --duration 3 --cmd "start" "sp 3000" -o docs/capture.csv

Sends `log on`, then each --cmd, records for --duration seconds, then sends `log off`.
Firmware lines look like: t_ms,setpoint_rpm,speed_rpm,duty_permille (integers).
"""
import argparse
import re
import sys
import time

import serial

LINE = re.compile(r"^(\d+),(-?\d+),(-?\d+),(-?\d+)$")
HEADER = "t_s,setpoint_rpm,speed_rpm,duty,error_rpm"


def parse_line(line):
    """Return (t_ms, setpoint_rpm, speed_rpm, duty) or None for any non-telemetry line."""
    m = LINE.match(line.strip())
    if not m:
        return None
    t_ms, sp, rpm, permille = (int(g) for g in m.groups())
    return t_ms, sp, rpm, permille / 1000.0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--duration", type=float, default=3.0, help="seconds to record")
    ap.add_argument("--cmd", nargs="*", default=[], help="console commands sent after log on")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        sys.exit(f"cannot open {args.port}: {e} (is the serial monitor still open?)")

    def send(text):
        port.write((text + "\n").encode())
        time.sleep(0.2)

    rows = []
    with port:
        port.reset_input_buffer()
        send("log on")
        for c in args.cmd:
            send(c)
        end = time.time() + args.duration
        while time.time() < end:
            row = parse_line(port.readline().decode(errors="replace"))
            if row:
                rows.append(row)
        send("log off")

    if not rows:
        sys.exit("no telemetry received (check port, baud and that the firmware is running)")

    t0 = rows[0][0]
    with open(args.output, "w", newline="") as f:
        f.write(HEADER + "\n")
        for t_ms, sp, rpm, duty in rows:
            f.write(f"{(t_ms - t0) / 1000:.3f},{sp},{rpm},{duty:.3f},{sp - rpm}\n")
    print(f"{len(rows)} samples -> {args.output}")


if __name__ == "__main__":
    main()
