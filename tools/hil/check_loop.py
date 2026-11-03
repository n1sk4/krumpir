#!/usr/bin/env python3
"""Checks a closed-loop run on the PC (pc_loop.sh) against the controller's acceptance figures
(docs/tuning.md of each controller) or its expected trip, from the controller's telemetry CSV,
its stderr (<controller.csv minus .csv>.err: "trip: <name> at tick <n>") and the runner's log.

    check_loop.py KIND <controller.csv> <runner.csv> [--rc N]

    check_loop.py --selftest

KIND as pc_loop.sh; --rc is the runner's exit status (2: the run faulted). Prints one line per
figure. Exit status: 0 every figure met, 1 one missed, 2 usage."""
import argparse
import csv
import sys
from pathlib import Path

# Fault kinds: the trip the controller must latch, the plant time of the fault and how long the
# trip may take (the controllers' safety configs: invalid readings, heater cycles, runaway window)
TRIPS = {
    "t12-open": ("no tip", 3.0, 0.5),
    "t12-short": ("heater short", 3.0, 0.5),
    "t12-stuck": ("heater stuck on", 3.0, 0.5),
    "t12-runaway": ("thermal runaway", 6.0, 25.0),
    "motor-sensor": ("sensor timeout", 0.5, 0.1),
}


def first_trip(err_path):
    """(name, plant time s) of the first trip the controller reported, or (None, None)."""
    for line in Path(err_path).read_text().splitlines():
        if line.startswith("trip: ") and " at tick " in line:
            name, tick = line[6:].rsplit(" at tick ", 1)
            if name != "cleared":
                return name, int(tick) / 1000
    return None, None


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


def selftest():
    # 0 -> 100 at t = 0: up to 110 at 0.2 s, inside +-5 from 0.3 s, then 101
    t = [i / 10 for i in range(10)]
    x = [0, 50, 110, 104, 98, 101, 101, 101, 101, 101]
    over, settle, err = step_figures(t, x, 100, 0, 1.0, 5)
    assert abs(over - 10) < 1e-9 and abs(settle - 0.2) < 1e-9 and abs(err - 1) < 1e-9
    print("check_loop: selftest ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("kind", nargs="?", help="as pc_loop.sh: motor, t12, t12-open, ...")
    p.add_argument("ctrl_csv", nargs="?", help="the controller's telemetry")
    p.add_argument("run_csv", nargs="?", help="the runner's log")
    p.add_argument("--rc", type=int, default=0, help="the runner's exit status")
    p.add_argument("--selftest", action="store_true", help="check the step figures and exit")
    a = p.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not a.run_csv:
        p.error("KIND, the controller's CSV and the runner's CSV are needed")
    kind, ctrl_path, rc = a.kind, a.ctrl_csv, a.rc
    ctrl, run = rows(ctrl_path), rows(a.run_csv)
    want_rc = 2 if kind == "motor-link" else 0
    print(f"runner exit status {rc} (want {want_rc})")
    checks = [("runner exit status not as wanted", int(rc != want_rc), 0)]
    if kind in TRIPS:
        want, t_fault, limit = TRIPS[kind]
        name, t = first_trip(Path(ctrl_path).with_suffix(".err"))
        print(f"trip: {name} at {t} s (want {want} within {limit:g} s of {t_fault:g} s)")
        checks += [("the expected trip", 0 if name == want else 1, 0),
                   ("trip after the fault s", (t - t_fault) if t is not None else 1e9, limit),
                   ("tripped before the fault", 1 if t is not None and t < t_fault else 0, 0)]
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
    elif kind.startswith("t12-"):
        # the controller trips: Q1 and the heartbeat (Q3) drop
        heated = max(r["heater"] for r in run if r["t"] < TRIPS[kind][1])
        last = run[-1]
        checks += [("heater never on before the fault", 1 - heated, 0),
                   ("heater at the end", last["heater"], 0),
                   ("series_on at the end", last["series_on"], 0)]
    elif kind == "motor-sensor":
        checks += [("duty at the end", abs(run[-1]["duty"]), 0)]
    elif kind == "motor-link":
        # muted at 0.5 s: the runner holds the last duty while late; the 10th late answer
        # (max_late) faults the run before its step, so 9 steps run on the held duty
        held = [r["duty"] for r in run if r["t"] > 0.5]
        checks += [("steps after the link broke, off by", abs(len(held) - 9), 0),
                   ("duty not held", max(held) - min(held) if held else 1, 0)]
    else:
        p.error(f"unknown kind {kind}")
    ok = True
    for name, value, limit in checks:
        good = value <= limit
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} {name}: {value:.3g} (limit {limit:g})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
