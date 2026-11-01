"""Plot a scenario CSV written by tools/scenarios.c, or a capture of the board's telemetry.

Usage: python tools/plot.py docs/scenario_cold.csv docs/scenario_cold.png
       python tools/plot.py docs/capture.csv docs/capture.png

A capture (tools/serial/capture.py) has fewer columns than a scenario: the tip, handle, load and
trip columns are plotted only when they are there.
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

TRIPS = ["none", "over temperature", "heater stuck on", "heater short", "no tip",
         "heater resistance", "sensor timeout", "thermal runaway"]

src, dst = sys.argv[1], sys.argv[2]
with open(src, newline="") as f:
    rows = list(csv.DictReader(f))
col = {k: [float(r[k]) for r in rows] for k in rows[0]}
t = col["t_s"]

has_load = "load_wk" in col
fig, axes = plt.subplots(3 if has_load else 2, 1, sharex=True, figsize=(8, 7 if has_load else 5))
a, b = axes[0], axes[1]
a.plot(t, col["setpoint_c"], "--", label="setpoint")
a.plot(t, col["temp_c"], label="measured")
if "tip_c" in col:
    a.plot(t, col["tip_c"], label="tip (model)")
if "handle_c" in col:
    a.plot(t, col["handle_c"], label="handle")
a.set_ylabel("temperature [C]")
a.legend(loc="center right")
b.plot(t, col["duty"], label="duty")
if "current_a" in col:
    b.plot(t, [x / 3 for x in col["current_a"]], label="heater current / 3 A", alpha=0.6)
b.set_ylabel("duty / current")
b.legend(loc="upper right")
if has_load:
    c = axes[2]
    c.step(t, col["load_wk"], where="post", label="load [W/K]")
    c.set_ylabel("load [W/K]")
axes[-1].set_xlabel("time [s]")
if "trip" in col and max(col["trip"]) > 0:
    trip = max(col["trip"])
    t_trip = next(x for x, code in zip(t, col["trip"]) if code > 0)
    for ax in axes:
        ax.axvline(t_trip, color="red", linestyle=":")
    a.set_title("trip: %s at %.1f s" % (TRIPS[int(trip)], t_trip))
for ax in axes:
    ax.grid(True)
fig.tight_layout()
fig.savefig(dst, dpi=120)
