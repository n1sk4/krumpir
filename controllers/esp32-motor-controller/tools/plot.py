#!/usr/bin/env python3
"""Plot a step-response CSV (t_s, setpoint_rpm, speed_rpm, duty and optionally error_rpm).

    python tools/plot.py docs/step_response.csv docs/step_response.png

A capture from tools/serial/capture.py has no error column: it is computed from setpoint and speed.
Exit status: 0 ok, 2 usage.
"""
import argparse
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("csv", help="the step response or capture")
    p.add_argument("png", help="the plot to write")
    a = p.parse_args()
    with open(a.csv, newline="") as f:
        rows = list(csv.DictReader(f))
    col = {k: [float(r[k]) for r in rows] for k in rows[0]}

    fig, (ax, bx, cx) = plt.subplots(3, 1, sharex=True, figsize=(8, 7))
    ax.plot(col["t_s"], col["setpoint_rpm"], "--", label="setpoint")
    ax.plot(col["t_s"], col["speed_rpm"], label="speed")
    ax.set_ylabel("rpm")
    ax.legend()
    bx.plot(col["t_s"], col["duty"])
    bx.set_ylabel("duty")
    error = col.get("error_rpm") or [s - v for s, v in zip(col["setpoint_rpm"], col["speed_rpm"])]
    cx.plot(col["t_s"], error)
    cx.set_ylabel("error [rpm]")
    cx.set_xlabel("time [s]")
    for x in (ax, bx, cx):
        x.grid(True)
    fig.tight_layout()
    fig.savefig(a.png, dpi=120)
    return 0


if __name__ == "__main__":
    sys.exit(main())
