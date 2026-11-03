#!/usr/bin/env python3
"""Checks a closed-loop run on the PC (pc_loop.sh) against the controller's acceptance figures
(docs/tuning.md of each controller), from the controller's telemetry CSV and the runner's log.

    check_loop.py motor|t12|t12-open <controller.csv> <runner.csv>

Exit 0 when every figure is met; prints one line per figure."""
import csv
import sys


def rows(path):
    with open(path, newline="") as f:
        return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]


def step_figures(t, x, sp, t0, t1, band):
    """Overshoot %, settling time s (last time outside sp +- band, from t0) and the mean error
    over the last 10 % of [t0, t1)."""
    seg = [(a, b) for a, b in zip(t, x) if t0 <= a < t1]
    over = max(0.0, (max(b for _, b in seg) - sp) / sp * 100)
    outside = [a for a, b in seg if abs(b - sp) > band]
    settle = (outside[-1] - t0) if outside else 0.0
    tail = [b for a, b in seg if a >= t1 - 0.1 * (t1 - t0)]
    return over, settle, abs(sum(tail) / len(tail) - sp)


def main():
    kind, ctrl, run = sys.argv[1], rows(sys.argv[2]), rows(sys.argv[3])
    checks = []
    if kind == "motor":
        # step 0 -> 3000 rpm: overshoot under 10 %, settling under 0.3 s, error under 0.5 %
        t = [r["t"] for r in run]
        x = [r["speed_rpm"] for r in run]
        over, settle, err = step_figures(t, x, 3000, 0, 0.5, 60)
        checks += [("overshoot %", over, 10), ("settling s", settle, 0.3),
                   ("steady-state error %", err / 30, 0.5)]
        # load step at 0.5 s: back within 2 % in under 0.3 s, no steady-state error
        over, settle, err = step_figures(t, x, 3000, 0.5, 1.0, 60)
        checks += [("load: settling s", settle, 0.3), ("load: error %", err / 30, 0.5)]
    elif kind == "t12":
        # cold start to 350 degC: overshoot under 2 %, settling under 8 s, final error under 1 K
        t = [r["t_ms"] / 1000 for r in ctrl]
        x = [r["temp_c"] for r in ctrl]
        over, settle, err = step_figures(t, x, 350, 0, 10, 7)
        checks += [("overshoot %", over, 2), ("settling s", settle, 8), ("final error K", err, 1)]
        over, settle, err = step_figures(t, x, 350, 10, 14, 7)
        checks += [("load: final error K", err, 1)]
    elif kind == "t12-open":
        # the tip goes open at 3 s: the controller trips, Q1 and the heartbeat (Q3) drop
        heated = max(r["heater"] for r in run if r["t"] < 3)
        last = run[-1]
        checks += [("heater never on before the fault", 1 - heated, 0),
                   ("heater at the end", last["heater"], 0),
                   ("series_on at the end", last["series_on"], 0)]
    else:
        sys.exit(f"unknown kind {kind}")
    ok = True
    for name, value, limit in checks:
        good = value <= limit
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} {name}: {value:.3g} (limit {limit:g})")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
