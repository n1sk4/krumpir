#!/usr/bin/env python3
"""Setpoint steps on the running T12 controller over XCP, logged to CSV and plotted.

Writes t12_cal_page.setpoint for each step, polls the controller while it settles, and puts the
old setpoint back at the end (also on Ctrl-C). The controller must be running (console: start).

    python examples/setpoint_steps.py --steps 200:15,300:15,250:15 --out runs/steps

Writes <out>.csv, and <out>.png when matplotlib is installed.
"""
import argparse
import csv
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # the krumpir package next door
from krumpir import EcuPort, XcpError  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
DEFAULT_A2L = REPO / "controllers/esp32-t12-controller/docs/t12.a2l"
SIGNALS = ["s_loop.ctrl.sp_active", "s_loop.temp", "s_loop.handle", "s_loop.duty", "s_loop.amps"]


def parse_steps(text):
    steps = []
    for part in text.split(","):
        sp, secs = part.split(":")
        steps.append((float(sp), float(secs)))
    return steps


def plot(rows, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed: no plot (pip install matplotlib)")
        return
    t = [r["t"] for r in rows]
    fig, (top, bottom) = plt.subplots(2, 1, sharex=True, figsize=(10, 6))
    top.plot(t, [r["setpoint"] for r in rows], "--", label="setpoint")
    top.plot(t, [r["s_loop.ctrl.sp_active"] for r in rows], label="setpoint (ramped)")
    top.plot(t, [r["s_loop.temp"] for r in rows], label="tip")
    top.plot(t, [r["s_loop.handle"] for r in rows], label="handle")
    top.set_ylabel("°C")
    top.legend()
    top.grid(True)
    bottom.plot(t, [r["s_loop.duty"] for r in rows], label="duty")
    bottom.set_ylabel("duty")
    bottom.set_xlabel("time [s]")
    bottom.grid(True)
    fig.tight_layout()
    fig.savefig(path, dpi=120)
    print(f"plot: {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--a2l", default=str(DEFAULT_A2L), help="A2L of the running build")
    ap.add_argument("--host", help="board address (default: from the A2L)")
    ap.add_argument("--steps", default="200:15,300:15,250:15", help="setpoint:seconds,...")
    ap.add_argument("--period", type=float, default=0.1, help="poll period [s]")
    ap.add_argument("--out", default="setpoint_steps", help="output path without extension")
    args = ap.parse_args()

    steps = parse_steps(args.steps)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    with EcuPort(args.a2l, host=args.host) as ecu:
        if not ecu.read("s_loop.ctrl.run"):
            sys.exit("the controller is stopped: type 'start' on its console first")
        old = ecu.read("t12_cal_page.setpoint")
        t0 = time.monotonic()
        try:
            for sp, secs in steps:
                ecu.write({"t12_cal_page.setpoint": sp})
                end = time.monotonic() + secs
                while time.monotonic() < end:
                    m = ecu.read(*SIGNALS)
                    row = {"t": round(time.monotonic() - t0, 3), "setpoint": sp, **m}
                    rows.append(row)
                    print(f"{row['t']:7.2f}s  sp {sp:5.1f}  ramp {m['s_loop.ctrl.sp_active']:6.1f}"
                          f"  tip {m['s_loop.temp']:6.1f}  duty {m['s_loop.duty']:.2f}")
                    time.sleep(args.period)
        except KeyboardInterrupt:
            print("stopped")
        finally:
            ecu.write({"t12_cal_page.setpoint": old})
            print(f"setpoint back to {old:g}")

    with open(out.with_suffix(".csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["t", "setpoint", *SIGNALS])
        w.writeheader()
        w.writerows(rows)
    print(f"csv: {out.with_suffix('.csv')} ({len(rows)} rows)")
    plot(rows, out.with_suffix(".png"))


if __name__ == "__main__":
    try:
        main()
    except XcpError as e:
        sys.exit(f"xcp: {e}")
