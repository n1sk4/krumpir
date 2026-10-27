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


def stimulus_value(s, t):
    """Same definition as the runner's null backend (src/io/null.c)."""
    kind = s["kind"]
    if kind == "constant":
        return float(s["value"])
    if kind == "step":
        return float(s["from"]) if t < s["t"] else float(s["to"])
    if kind == "sine":
        return s.get("offset", 0.0) + s["amplitude"] * math.sin(2 * math.pi * s["freq_hz"] * t)
    raise ValueError(f"unknown stimulus kind '{kind}'")


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
        "stimuli": {s["input"]: s for s in io.get("stimulus", [])},
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
        s = sc["stimuli"].get(name)
        sig[name] = [stimulus_value(s, t) if s else 0.0 for t in times]
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
        s = sc["stimuli"].get(name)
        expected = np.array([stimulus_value(s, t) if s else 0.0 for t in times[:-1]])
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
