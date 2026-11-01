#!/usr/bin/env python3
"""Compare a runner log against an FMPy reference run of the same scenario.

    compare_fmpy.py scenarios/dahlquist.toml --run build/krumpir
    compare_fmpy.py scenarios/dahlquist.toml            # use the existing log

The scenario is simulated by FMPy (co-simulation, same step, same parameters, same input
signals), and every logged output of the runner is compared with FMPy's value at the same time.
Runner log row n has t = (n+1)*step: its outputs are the state after step n.

Exit status: 0 PASS, 1 FAIL (mismatch), 2 error, 77 skipped (FMU not installed).
"""
import argparse
import contextlib
import io
import math
import subprocess
import sys
import tomllib
from pathlib import Path

import numpy as np

SKIP = 77


def read_data(path):
    """time,value rows; blank lines, # comments and a header line are skipped."""
    rows = []
    for i, line in enumerate(Path(path).read_text().splitlines()):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            rows.append(tuple(float(x) for x in line.split(",")))
        except ValueError:
            if i > 0 or rows:
                raise
    return rows


def stimulus_signal(s):
    """The stimulus as a function of time, evaluated as the runner does (src/core/siggen.c)."""
    kind = s.get("kind")
    loops = 1
    if kind == "constant":
        segs = [{"kind": "const", "value": s["value"]}]
    elif kind == "step":
        first = [{"kind": "const", "value": s["from"], "duration": s["t"]}] if s["t"] > 0 else []
        segs = first + [{"kind": "const", "value": s["to"]}]
    elif kind == "sine":
        segs = [{"kind": "sine", "offset": s.get("offset", 0.0), "amplitude": s["amplitude"],
                 "freq_hz": s["freq_hz"]}]
    else:
        segs, loops = s["segments"], s.get("loops", 1)

    def seg_value(g, u):
        k = g["kind"]
        if k == "const":
            return float(g["value"])
        if k == "ramp":
            return g["from"] + (g["to"] - g["from"]) * u / g["dur"]
        if k == "sine":
            return g.get("offset", 0.0) + g["amplitude"] * math.sin(
                2 * math.pi * g["freq_hz"] * u + g.get("phase", 0.0))
        if k == "pulse":
            on = math.fmod(u, g["period"]) < g["duty"] * g["period"]
            return float(g["high"] if on else g.get("low", 0.0))
        if k == "idle":
            return g["v0"]
        rows = g["rows"]  # data
        if u <= rows[0][0]:
            return rows[0][1]
        if u >= rows[-1][0]:
            return rows[-1][1]
        hi = next(i for i, r in enumerate(rows) if r[0] > u)
        (t0, v0), (t1, v1) = rows[hi - 1], rows[hi]
        return v0 if g.get("interpolation") == "step" else v0 + (v1 - v0) * (u - t0) / (t1 - t0)

    resolved, start, prev = [], 0.0, 0.0
    for seg in segs:
        g = {**seg, "start": start, "v0": prev}
        if g["kind"] == "ramp":
            g.setdefault("from", prev)
        if g["kind"] == "data":
            g["rows"] = read_data(g["file"])
        g["dur"] = float(g.get("duration", g["rows"][-1][0] if g["kind"] == "data" else math.inf))
        g["v1"] = (float(g["to"]) if g["kind"] == "ramp"
                   else math.nan if math.isinf(g["dur"]) else seg_value(g, g["dur"]))
        prev, start = g["v1"], start + g["dur"]
        resolved.append(g)
    total, last = start, resolved[-1]

    def value(t):
        if loops != 1:
            if loops > 0 and t >= total * loops:
                return last["v1"]
            t = math.fmod(t, total)
        elif t >= total:
            return last["v1"]
        for g in resolved[:-1]:
            if t < g["start"] + g["dur"]:
                return seg_value(g, t - g["start"])
        return seg_value(last, t - last["start"])

    return value


def load_scenario(path):
    with open(path, "rb") as f:
        sc = tomllib.load(f)
    model, io = sc["model"], sc.get("io", {})
    step, duration = float(model["step"]), float(model["duration"])
    return {
        "fmu": Path(model["fmu"]),
        "step": step,
        "duration": duration,
        "n_steps": round(duration / step),
        "params": {k: float(v) for k, v in model.get("params", {}).items()},
        "inputs": list(io.get("inputs", [])),
        "outputs": list(io.get("outputs", [])),
        "signals": {s["input"]: stimulus_signal(s) for s in io.get("stimulus", [])},
        "log": Path(sc["log"]["file"]),
    }


def read_log(path):
    with open(path) as f:
        header = f.readline().strip().split(",")
    data = np.loadtxt(path, delimiter=",", skiprows=1, ndmin=2)
    return {name: data[:, i] for i, name in enumerate(header)}


def input_signal(sc, times):
    """Input samples at the step start times t_n = n*step, as FMPy expects them."""
    if not sc["inputs"]:
        return None
    dtype = [("time", np.float64)] + [(name, np.float64) for name in sc["inputs"]]
    sig = np.zeros(len(times), dtype=dtype)
    sig["time"] = times
    for name in sc["inputs"]:
        f = sc["signals"].get(name)
        sig[name] = [f(t) if f else 0.0 for t in times]
    return sig


def fmpy_reference(sc, times):
    from fmpy import simulate_fmu

    # FMPy prints a warning for every input it is not given. The runner only drives the Float64
    # inputs named in the scenario and leaves the rest at their start values, same as here.
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        res = simulate_fmu(
            str(sc["fmu"]),
            start_time=0.0,
            stop_time=sc["duration"],
            step_size=sc["step"],
            output_interval=sc["step"],
            fmi_type="CoSimulation",
            start_values=sc["params"],
            input=input_signal(sc, times),
            output=sc["outputs"],
            validate=False,
        )
    for line in captured.getvalue().splitlines():
        if not line.startswith("Warning: missing input"):
            print(line)
    return {name: res[name] for name in res.dtype.names}


def plot(path, sc, log, ref):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(len(sc["outputs"]), 1, sharex=True, squeeze=False, figsize=(8, 3 * len(sc["outputs"])))
    for ax, name in zip(axes[:, 0], sc["outputs"]):
        ax.plot(ref["time"][1:], ref[name][1:], label="FMPy", linewidth=2)
        ax.plot(log["t"], log[name], "--", label="runner")
        ax.set_ylabel(name)
        ax.grid(True)
        ax.legend()
    axes[-1, 0].set_xlabel("t [s]")
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    print(f"plot: {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario")
    ap.add_argument("--run", metavar="KRUMPIR", help="run this krumpir binary on the scenario first")
    ap.add_argument("--tol", type=float, default=1e-9, help="max absolute error (default 1e-9)")
    ap.add_argument("--plot", metavar="PNG", help="write an overlay plot")
    args = ap.parse_args()

    sc = load_scenario(args.scenario)
    if not sc["fmu"].exists():
        print(f"SKIP: {sc['fmu']} not found, see runner/fmus/README.md", file=sys.stderr)
        return SKIP

    if args.run:
        r = subprocess.run([args.run, "run", args.scenario], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"runner failed ({r.returncode}):\n{r.stdout}{r.stderr}", file=sys.stderr)
            return 2

    try:
        log = read_log(sc["log"])
    except OSError as e:
        print(f"cannot read log: {e}", file=sys.stderr)
        return 2

    n = sc["n_steps"]
    step = sc["step"]
    times = np.arange(n + 1) * step  # t_n = n*step, exactly as the runner computes it

    ref = fmpy_reference(sc, times)
    ok = True
    problems = []

    if len(log["t"]) != n:
        problems.append(f"log has {len(log['t'])} rows, expected {n}")
        ok = False
    if len(ref["time"]) != n + 1:
        problems.append(f"FMPy returned {len(ref['time'])} points, expected {n + 1}")
        ok = False
    if not ok:
        print("\n".join(problems), file=sys.stderr)
        print("RESULT: FAIL")
        return 1

    # Row n of the log is the state after step n, at t = (n+1)*step.
    t_err = np.max(np.abs(log["t"] - times[1:]))
    t_err_ref = np.max(np.abs(ref["time"] - times))
    if t_err > 1e-12 or t_err_ref > 1e-9:
        problems.append(f"time axes differ (log {t_err:.3g}, FMPy {t_err_ref:.3g})")

    for name in sc["inputs"]:  # the logged inputs are the ones that were applied
        f = sc["signals"].get(name)
        expected = np.array([f(t) if f else 0.0 for t in times[:-1]])
        in_err = np.max(np.abs(log[name] - expected))
        if in_err > 1e-12:
            problems.append(f"logged input '{name}' differs from the stimulus by {in_err:.3g}")

    print(f"scenario: {args.scenario}")
    print(f"fmu:      {sc['fmu']}   steps: {n}   step: {step:g} s   tolerance: {args.tol:g}")
    print(f"{'output':<28} {'max |err|':>12} {'rms err':>12} {'at t [s]':>10}")
    for name in sc["outputs"]:
        err = np.abs(log[name] - ref[name][1:])
        worst = int(np.argmax(err))
        rms = float(np.sqrt(np.mean(err**2)))
        print(f"{name:<28} {err[worst]:>12.3e} {rms:>12.3e} {log['t'][worst]:>10.4f}")
        if not err[worst] <= args.tol:  # also catches NaN
            problems.append(f"output '{name}': max error {err[worst]:.3e} > {args.tol:g}")

    if args.plot:
        plot(args.plot, sc, log, ref)

    for p in problems:
        print(f"  - {p}")
    print("RESULT: " + ("FAIL" if problems else "PASS"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
