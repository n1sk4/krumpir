#!/usr/bin/env python3
"""Run a scenario with the runner and compare its deterministic columns with a golden CSV.

    check_golden.py KRUMPIR SCENARIO GOLDEN            # compare
    check_golden.py KRUMPIR SCENARIO GOLDEN --update   # rewrite GOLDEN from this run

Run it from the runner/ directory: paths in the scenario are relative to it. The timing columns
(wake_ns, step_ns, flags) change on every run and are dropped. The step index n must match exactly;
the other numbers are compared with a small tolerance, because the last bits of a floating-point
result can differ between CPUs (x86_64 and aarch64 contract multiply-adds differently).

Exit status: 0 match, 1 differs, 2 error, 77 skipped (FMU not installed).
Only the standard library is needed (Python 3.11+).
"""
import argparse
import csv
import math
import subprocess
import sys
import tomllib
from pathlib import Path

SKIP = 77
TIMING_COLUMNS = ["wake_ns", "step_ns", "flags"]


def read_deterministic(path):
    """Header and rows of the log without the timing columns."""
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    if not rows or rows[0][-len(TIMING_COLUMNS):] != TIMING_COLUMNS:
        raise ValueError(f"{path}: expected the columns to end with {','.join(TIMING_COLUMNS)}")
    return [r[: -len(TIMING_COLUMNS)] for r in rows]


def differences(got, want, tol, limit=5):
    """Up to `limit` human-readable differences between two tables (header row first)."""
    out = []
    if got[0] != want[0]:
        return [f"header: got {','.join(got[0])}, golden has {','.join(want[0])}"]
    if len(got) != len(want):
        out.append(f"{len(got) - 1} rows, golden has {len(want) - 1}")
    for i, (g, w) in enumerate(zip(got[1:], want[1:])):
        for col, (a, b) in enumerate(zip(g, w)):
            same = a == b if col == 0 else math.isclose(float(a), float(b), rel_tol=tol, abs_tol=tol)
            if not same:
                out.append(f"row {i}, {got[0][col]}: got {a}, golden {b}")
                break
        if len(out) >= limit:
            break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("krumpir")
    ap.add_argument("scenario")
    ap.add_argument("golden")
    ap.add_argument("--update", action="store_true", help="write the golden file from this run")
    ap.add_argument("--tol", type=float, default=1e-12, help="relative and absolute tolerance")
    args = ap.parse_args()

    with open(args.scenario, "rb") as f:
        sc = tomllib.load(f)
    fmu, log = Path(sc["model"]["fmu"]), Path(sc["log"]["file"])
    if not fmu.exists():
        print(f"SKIP: {fmu} not found, see runner/fmus/README.md", file=sys.stderr)
        return SKIP

    r = subprocess.run([args.krumpir, "run", args.scenario], capture_output=True, text=True)
    if r.returncode != 0:
        print(f"runner failed ({r.returncode}):\n{r.stdout}{r.stderr}", file=sys.stderr)
        return 2
    try:
        got = read_deterministic(log)
    except (OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 2

    golden = Path(args.golden)
    if args.update:
        golden.parent.mkdir(parents=True, exist_ok=True)
        with open(golden, "w", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows(got)
        print(f"wrote {golden} ({len(got) - 1} rows)")
        return 0

    try:
        with open(golden, newline="") as f:
            want = list(csv.reader(f))
    except OSError as e:
        print(f"cannot read golden file: {e} (create it with --update)", file=sys.stderr)
        return 2

    diffs = differences(got, want, args.tol)
    if diffs:
        print(f"{args.scenario} differs from {golden}:")
        for d in diffs:
            print(f"  - {d}")
        print("RESULT: FAIL")
        return 1
    print(f"{args.scenario}: {len(got) - 1} rows match {golden} (tolerance {args.tol:g})")
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
