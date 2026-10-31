"""Plot a scenario CSV written by tools/scenarios.c.

Usage: python tools/plot.py docs/scenario_cold.csv docs/scenario_cold.png
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

fig, (a, b, c) = plt.subplots(3, 1, sharex=True, figsize=(8, 7))
a.plot(t, col["setpoint_c"], "--", label="setpoint")
a.plot(t, col["temp_c"], label="measured")
a.plot(t, col["tip_c"], label="tip (model)")
a.plot(t, col["handle_c"], label="handle")
a.set_ylabel("temperature [C]")
a.legend(loc="center right")
b.plot(t, col["duty"], label="duty")
b.plot(t, [x / 3 for x in col["current_a"]], label="heater current / 3 A", alpha=0.6)
b.set_ylabel("duty / current")
b.legend(loc="upper right")
c.step(t, col["load_wk"], where="post", label="load [W/K]")
c.set_ylabel("load [W/K]")
c.set_xlabel("time [s]")
trip = max(col["trip"])
if trip > 0:
    t_trip = next(x for x, code in zip(t, col["trip"]) if code > 0)
    for ax in (a, b, c):
        ax.axvline(t_trip, color="red", linestyle=":")
    a.set_title("trip: %s at %.1f s" % (TRIPS[int(trip)], t_trip))
for ax in (a, b, c):
    ax.grid(True)
fig.tight_layout()
fig.savefig(dst, dpi=120)
