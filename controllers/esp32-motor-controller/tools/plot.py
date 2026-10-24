"""Plot a step-response CSV (t_s, setpoint_rpm, speed_rpm, duty, error_rpm).

Usage: python tools/plot.py docs/step_response.csv docs/step_response.png
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

src, dst = sys.argv[1], sys.argv[2]
with open(src, newline="") as f:
    rows = list(csv.DictReader(f))
col = {k: [float(r[k]) for r in rows] for k in rows[0]}

fig, (a, b, c) = plt.subplots(3, 1, sharex=True, figsize=(8, 7))
a.plot(col["t_s"], col["setpoint_rpm"], "--", label="setpoint")
a.plot(col["t_s"], col["speed_rpm"], label="speed")
a.set_ylabel("rpm")
a.legend()
b.plot(col["t_s"], col["duty"])
b.set_ylabel("duty")
c.plot(col["t_s"], col["error_rpm"])
c.set_ylabel("error [rpm]")
c.set_xlabel("time [s]")
for ax in (a, b, c):
    ax.grid(True)
fig.tight_layout()
fig.savefig(dst, dpi=120)
