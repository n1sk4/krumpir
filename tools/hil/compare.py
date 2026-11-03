#!/usr/bin/env python3
"""SIL vs PC loop: each closed-loop scenario of a controller, run on its on-chip plant
(<name>_scenarios, the controller's tools/scenarios.c) and as the PC loop (the same controller code
against the plant FMU in the runner over the HIL link, pc_loop.sh), compared signal by signal.

    compare.py motor|t12 [--build DIR] [--check] [--no-plots] [--case NAME]...

Writes controllers/<dir>/docs/sil_vs_pc.md and, with matplotlib, one overlay plot per case
(sil_vs_pc_<case>.png). --check only checks (CTest): exit 1 when a case fails, 77 without socat.
--build is the runner build that also built the controllers for the PC (default ~/build/krumpir).

A case passes when the RMS error of each compared signal is under 2 % of its full scale, the step
figures agree within 10 % (or a small absolute floor), and both runs trip the same way.

Timing: both loops compute the command for plant step k from the plant before it (the runner
sends the initial outputs before step 0), and tick k belongs to step k, so a setpoint change at
on-chip time t is the PC controller's --at t.
"""
import argparse
import csv
import math
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RMS_LIMIT_PCT = 2.0
FIG_REL = 0.10

# The plant FMU and its scenario frame per controller (the HIL values of controller.toml)
PLANTS = {
    "motor": {
        "dir": "esp32-motor-controller",
        "fmu": "fmus/dcmotor.fmu",
        "params": {"Vbus": 6.0},
        "inputs": ["duty", "load_torque"],
        "cmd": ["duty"],
        "outputs": ["speed_rpm", "current"],
        "dt": 0.001,
    },
    "t12": {
        "dir": "esp32-t12-controller",
        "fmu": "fmus/t12tip.fmu",
        "params": {},
        "inputs": ["heater", "series_on", "load_wk", "ambient_c", "fault"],
        "cmd": ["heater", "series_on"],
        "outputs": ["v_amp", "i_heater", "t_handle_c", "t_tip_c"],
        "dt": 0.001,
    },
}
T12_HOLD = {"load_wk": 0, "ambient_c": 25, "fault": 0}  # the on-chip plant's defaults
T12_CYCLE_S = 0.1  # the heater window: trip times may differ by one

# The on-chip scenarios (motor_scenario.c), as runner stimuli and timed setpoints
CASES = {
    "motor": {
        "step": {"duration": 1.1, "sp": 0, "at": [(0.1, "sp 3000")], "target": 3000,
                 "window": (0.1, 1.1)},
        "load": {"duration": 1.6, "sp": 3000, "target": 3000, "window": (0.8, 1.6),
                 "stim": {"load_torque": (0.8, 0.0, 0.001)}},
        "saturation": {"duration": 1.6, "sp": 10000, "at": [(0.6, "sp 3000")], "target": 3000,
                       "window": (0.6, 1.6)},
        "inertia": {"duration": 1.1, "sp": 0, "at": [(0.1, "sp 3000")], "target": 3000,
                    "window": (0.1, 1.1), "params": {"J": 2e-6}},
    },
    # t12_scenario.c: 350 degC, 25 degC ambient; the event at `event` s
    "t12": {
        "cold": {"duration": 30, "event": 0},
        "load": {"duration": 60, "event": 30, "stim": {"load_wk": (30, 0, 0.1)}},
        "ambient": {"duration": 60, "event": 30, "stim": {"ambient_c": (30, 25, 10)}},
        "open": {"duration": 20, "event": 10, "stim": {"fault": (10, 0, 1)}},
        "short": {"duration": 20, "event": 10, "stim": {"fault": (10, 0, 2)}},
        "stuck": {"duration": 20, "event": 10, "stim": {"fault": (10, 0, 3)}},
        "runaway": {"duration": 80, "event": 10, "stim": {"load_wk": (10, 0, 0.5)}},
    },
}
for _c in CASES["t12"].values():
    _c.update({"sp": 350, "target": 350, "hold": T12_HOLD})


def toml_scenario(ctrl, name, case, board=None):
    """The case as a runner scenario: lockstep on the PC loop's pty, or (board: the device) paced
    and free-running, as the board ticks on its own clock."""
    p = PLANTS[ctrl]
    params = {**p["params"], **case.get("params", {})}
    stim = case.get("stim", {})
    timing = (['wait = "none"'] if board is None else
              ['wait = "sleep"', "max_overruns = 1000000"])
    link = (['device = "/tmp/krumpir-hil/runner"', "timeout_ms = 500"] if board is None else
            [f'device = "{board}"', "baud = 921600", "timeout_ms = 0", 'late = "hold"'])
    log = f"runs/compare/{ctrl}_{name}{'' if board is None else '_board'}.csv"
    lines = [f"# compare.py {ctrl} {name}: generated, see tools/hil/compare.py",
             "[model]", f'fmu = "{p["fmu"]}"', f"step = {p['dt']}",
             f"duration = {case['duration']}",
             "params = { " + ", ".join(f"{k} = {v}" for k, v in params.items()) + " }",
             "", "[rt]", *timing, "", "[io]", 'backend = "uart"', *link,
             "inputs = [" + ", ".join(f'"{i}"' for i in p["inputs"]) + "]",
             "outputs = [" + ", ".join(f'"{o}"' for o in p["outputs"]) + "]",
             "stimulus = ["]
    for inp, (t, a, b) in stim.items():
        lines.append(f'  {{ input = "{inp}", kind = "step", t = {t}, from = {a}, to = {b} }},')
    # inputs the controller does not command are the runner's: held unless stimulated
    for inp in p["inputs"]:
        if inp not in p["cmd"] and inp not in stim:
            value = case.get("hold", {}).get(inp, 0)
            lines.append(f'  {{ input = "{inp}", kind = "constant", value = {value} }},')
    lines += ["]", "", "[log]", f'file = "{log}"', ""]
    return "\n".join(lines)


def read_csv(path):
    with open(path, newline="") as f:
        return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]


def run_sil(build, ctrl, name):
    exe = Path(build) / f"host-{ctrl}" / f"{ctrl}_scenarios"
    out = subprocess.run([str(exe), name], check=True, capture_output=True, text=True).stdout
    return [{k: float(v) for k, v in r.items()} for r in csv.DictReader(out.splitlines())]


def run_pc(build, ctrl, name, case):
    """Returns the runner's log and the controller's telemetry."""
    p = PLANTS[ctrl]
    scn = ROOT / "runner" / "runs" / "compare" / f"{ctrl}_{name}.toml"
    scn.parent.mkdir(parents=True, exist_ok=True)
    scn.write_text(toml_scenario(ctrl, name, case))
    args = ["-c", f"sp {case['sp']}"]
    for t, line in case.get("at", []):
        args += ["--at", f"{t:g}", line]
    env = {**os.environ, "KRUMPIR": str(Path(build) / "krumpir"),
           "CONTROLLER": str(Path(build) / f"host-{ctrl}" / f"{ctrl}_host")}
    r = subprocess.run(["bash", str(ROOT / "tools/hil/pc_loop.sh"), ctrl, "--scenario", str(scn),
                        "--", *args], capture_output=True, text=True, env=env)
    if r.returncode == 77:
        print(r.stdout.strip())
        sys.exit(77)  # no socat: skipped
    if r.returncode != 0:
        sys.exit(f"compare: the PC loop failed for {ctrl} {name}:\n{r.stdout}{r.stderr}")
    # the controller's first trip, as main_host reports it: "trip: <name> at tick <n>"
    trip = ("none", None)
    for line in r.stderr.splitlines():
        if line.startswith("trip: ") and " at tick " in line and trip[0] == "none":
            what, tick = line[6:].rsplit(" at tick ", 1)
            if what != "cleared":
                trip = (what, int(tick) * p["dt"])
    return (read_csv(ROOT / "runner" / "runs" / "compare" / f"{ctrl}_{name}.csv"),
            read_csv(ROOT / "runner" / "runs" / f"{ctrl}_{name}_ctrl.csv"), trip)


class Console:
    """The board's console (UART0): a tty, set to 115200 raw, or any writable path (a fifo into a
    PC controller standing in for the board). Kept open: a fifo's reader would see an EOF."""

    def __init__(self, path):
        self.f = open(path, "w", buffering=1)
        if self.f.isatty():
            import termios
            a = termios.tcgetattr(self.f)
            a[0] = a[1] = a[3] = 0  # raw: no input, output or local processing
            a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            a[4] = a[5] = termios.B115200
            termios.tcsetattr(self.f, termios.TCSANOW, a)

    def send(self, *lines):
        for line in lines:
            self.f.write(line + "\n")
            time.sleep(0.05)  # one command at a time for the board's line editor


def run_board(build, ctrl, name, case, device, console):
    """The case against the controller on the board: its console gets the setpoint and a start
    (after a stop and a clear, so every case starts the same), the runner drives the plant."""
    scn = ROOT / "runner" / "runs" / "compare" / f"{ctrl}_{name}_board.toml"
    scn.parent.mkdir(parents=True, exist_ok=True)
    scn.write_text(toml_scenario(ctrl, name, case, board=device))
    console.send("stop", "clear", f"sp {case['sp']}", "start")
    time.sleep(0.3)
    r = subprocess.run([str(Path(build) / "krumpir"), "run", str(scn)], capture_output=True,
                       text=True, cwd=ROOT / "runner")
    if r.returncode != 0:
        sys.exit(f"compare: the board run failed for {ctrl} {name}:\n{r.stdout}{r.stderr}")
    late = next((int(x.split()[1]) for x in r.stdout.splitlines() if x.startswith("late:")), -1)
    return read_csv(ROOT / "runner" / "runs" / "compare" / f"{ctrl}_{name}_board.csv"), late


MAX_LATENCY_STEPS = 3
# On the board the commands (free-running: a change lands up to a step either way of the on-chip
# run) are held to 5 %; the plant outputs keep RMS_LIMIT_PCT.
BOARD_LIMITS = {"duty": 5.0}


def aligned_motor(sil, run, case):
    """The motor against the board: free-running, its tick falls anywhere in the runner's step, so
    the response trails by up to a step or two (the loop latency). Compared at the delay that fits
    best (0..MAX_LATENCY_STEPS), which is reported as the latency."""
    dt = PLANTS["motor"]["dt"]
    best = None
    for shift in range(MAX_LATENCY_STEPS + 1):
        moved = [{**r, "t": r["t"] - shift * dt} for r in run if r["t"] - shift * dt > dt / 2]
        res = compare_motor(sil, (moved, None, None), case)
        if best is None or res["rms"]["speed_rpm"] < best["rms"]["speed_rpm"]:
            best, best["latency"] = res, shift
    best["figures"]["latency steps"] = (0, best["latency"], 2)  # MVP: at most 2 steps
    best["limits"] = BOARD_LIMITS
    return best


def compare_t12_board(sil, run, case):
    """The T12 against the board, at the delay that fits the duty best (as aligned_motor)."""
    dt = PLANTS["t12"]["dt"]
    best = None
    for shift in range(MAX_LATENCY_STEPS + 1):
        res = t12_from_log(sil, run, case, shift)
        if best is None or res["rms"]["duty"] < best["rms"]["duty"]:
            best, best["latency"] = res, shift
    best["figures"]["latency steps"] = (0, best["latency"], 2)  # MVP: at most 2 steps
    return best


def t12_from_log(sil, run, case, shift):
    """The board from the runner's log only (its telemetry has its own clock), `shift` steps
    earlier: the tip temperature at the on-chip samples, the duty as the heater's mean over the
    cycle after each, and a trip as series_on dropping after it was on (Q3 opens
    T12_HEARTBEAT_TIMEOUT_S = 0.1 s after the trip)."""
    dt = PLANTS["t12"]["dt"]
    tip, heater = {0: 25.0}, {}
    for r in run:
        k = round(r["t"] / dt) - shift  # row n = k - 1: the heater during step n, the plant after
        tip[k] = r["t_tip_c"]
        heater[k - 1] = r["heater"]
    cycle = round(T12_CYCLE_S / dt)
    t, tips, duty = [], ([], []), ([], [])
    for s in sil:
        k = round(s["t_s"] / dt)
        window = [heater[j] for j in range(k, k + cycle) if j in heater]
        if k not in tip or len(window) < cycle:
            continue
        t.append(s["t_s"])
        tips[0].append(s["tip_c"])
        tips[1].append(tip[k])
        duty[0].append(s["duty"])
        duty[1].append(sum(window) / cycle)
    sil_trip = next((s["t_s"] for s in sil if s["trip"]), None)
    on, drop = False, None
    for r in run:
        on = on or r["series_on"] >= 0.5
        if on and r["t"] >= case["event"] and r["series_on"] < 0.5:
            drop = r["t"] - shift * dt
            break
    board_trip = drop - 0.1 if drop is not None else None
    res = {"t": t, "signals": {"tip_c": tips, "duty": duty},
           "rms": {"tip_c": rms_pct(*tips, case["target"]), "duty": rms_pct(*duty, 1.0)},
           "figures": {}, "limits": BOARD_LIMITS,
           "trip": ("trip" if sil_trip is not None else "none",
                    "trip" if board_trip is not None else "none")}
    if sil_trip is not None or board_trip is not None:
        a = sil_trip - case["event"] if sil_trip is not None else math.nan
        b = board_trip - case["event"] if board_trip is not None else math.nan
        res["figures"]["trip after s"] = (a, b, 2 * T12_CYCLE_S)
    return res


def step_figures(t, x, sp, t0, t1, band):
    """Overshoot % (rising steps), settling s from t0 (last time outside sp +- band), final
    error (mean over the last 10 % of the window), and the minimum (a load dip)."""
    seg = [(a, b) for a, b in zip(t, x) if t0 <= a < t1]
    rising = sp >= seg[0][1]
    over = max(0.0, (max(b for _, b in seg) - sp) / sp * 100) if rising else 0.0
    outside = [a for a, b in seg if abs(b - sp) > band]
    settle = (outside[-1] - t0) if outside else 0.0
    tail = [b for a, b in seg if a >= t1 - 0.1 * (t1 - t0)]
    return {"overshoot %": over, "settling s": settle,
            "final error": abs(sum(tail) / len(tail) - sp), "min": min(b for _, b in seg)}


def rms_pct(a, b, fs):
    return 100 * math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a)) / fs


def compare_motor(sil, pc, case):
    """Both on the on-chip grid: the speed at time t (the plant after t/dt steps) and the duty
    applied in the step that starts at t."""
    run, _, _ = pc
    dt = PLANTS["motor"]["dt"]
    speed_pc, duty_pc = {0: 0.0}, {}
    for r in run:
        k = round(r["t"] / dt)  # row n: t = (n+1) dt, outputs after step n, duty during step n
        speed_pc[k] = r["speed_rpm"]
        duty_pc[k - 1] = r["duty"]
    t = [r["t_s"] for r in sil]
    ks = [round(x / dt) for x in t]
    end = speed_pc[max(speed_pc)]  # held at the end (aligned_motor shifts the last rows out)
    s_sil, s_pc = [r["speed_rpm"] for r in sil], [speed_pc.get(k, end) for k in ks]
    d_sil, d_pc = [r["duty"] for r in sil], [duty_pc.get(k, 0.0) for k in ks]
    fs = max(abs(x) for x in s_sil) or 1
    t0, t1 = case["window"]
    band = 0.02 * case["target"]
    f_sil = step_figures(t, s_sil, case["target"], t0, t1, band)
    f_pc = step_figures(t, s_pc, case["target"], t0, t1, band)
    floors = {"overshoot %": 0.5, "settling s": 0.01, "final error": 0.005 * case["target"],
              "min": 0.01 * case["target"]}
    return {"t": t, "signals": {"speed_rpm": (s_sil, s_pc), "duty": (d_sil, d_pc)},
            "rms": {"speed_rpm": rms_pct(s_sil, s_pc, fs), "duty": rms_pct(d_sil, d_pc, 1.0)},
            "figures": {k: (f_sil[k], f_pc[k], floors[k]) for k in f_sil},
            "trip": ("none", "none")}


T12_TRIPS = ["none", "over temperature", "heater stuck on", "heater short", "no tip",
             "heater resistance", "sensor timeout", "thermal runaway"]  # t12_trip_name()


def compare_t12(sil, pc, case):
    """On the measurement cycles (the samples of both sides): the controller's temperature and
    duty, and the tip temperature of the plant at that tick. Invalid readings (NaN on chip, -1 in
    the telemetry) are left out; the fault cases compare the trip and its time."""
    run, ctrl, (pc_trip, pc_trip_s) = pc
    dt = PLANTS["t12"]["dt"]
    tip_pc = {0: 25.0}
    for r in run:
        tip_pc[round(r["t"] / dt)] = r["t_tip_c"]  # row n: the plant after step n
    by_tick = {round(r["t_ms"]): r for r in ctrl}
    t, temp, duty, tip = [], ([], []), ([], []), ([], [])
    for s in sil:
        k = round(s["t_s"] / dt)
        c = by_tick.get(k)
        if c is None:
            continue
        t.append(s["t_s"])
        ok = math.isfinite(s["temp_c"]) and c["temp_c"] >= 0
        temp[0].append(s["temp_c"] if ok else math.nan)
        temp[1].append(c["temp_c"] if ok else math.nan)
        duty[0].append(s["duty"])
        duty[1].append(c["duty_permille"] / 1000)
        tip[0].append(s["tip_c"])
        tip[1].append(tip_pc.get(k, math.nan))
    sil_trip = next(((T12_TRIPS[int(s["trip"])], s["t_s"]) for s in sil if s["trip"]),
                    ("none", None))

    def rms(pair, fs):
        a, b = zip(*[(x, y) for x, y in zip(*pair) if math.isfinite(x) and math.isfinite(y)])
        return rms_pct(a, b, fs)

    res = {"t": t, "signals": {"temp_c": temp, "tip_c": tip, "duty": duty},
           "rms": {"temp_c": rms(temp, case["target"]), "tip_c": rms(tip, case["target"]),
                   "duty": rms(duty, 1.0)},
           "figures": {}, "trip": (sil_trip[0], pc_trip)}
    if sil_trip[0] != "none" or pc_trip != "none":  # fault cases: the trip time instead
        a = sil_trip[1] - case["event"] if sil_trip[1] is not None else math.nan
        b = pc_trip_s - case["event"] if pc_trip_s is not None else math.nan
        res["figures"]["trip after s"] = (a, b, T12_CYCLE_S)
        return res
    t0, t1 = case["event"], case["duration"]
    f = [step_figures([x for x, y in zip(t, v) if math.isfinite(y)], [y for y in v if
                                                                      math.isfinite(y)],
                      350, t0, t1, 7) for v in temp]
    floors = {"overshoot %": 0.5, "settling s": 2 * T12_CYCLE_S, "final error": 1.0, "min": 2.0}
    res["figures"] = {k: (f[0][k], f[1][k], floors[k]) for k in f[0]}
    return res


def verdict(res):
    limits = res.get("limits", {})
    bad = [f"{s} RMS {v:.2f} %" for s, v in res["rms"].items()
           if v >= limits.get(s, RMS_LIMIT_PCT)]
    for k, (a, b, floor) in res["figures"].items():
        if not abs(a - b) <= max(FIG_REL * abs(a), floor):  # NaN (one side never got there) fails
            bad.append(f"{k} {a:.3g} vs {b:.3g}")
    if res["trip"][0] != res["trip"][1]:
        bad.append(f"trip {res['trip'][0]} vs {res['trip'][1]}")
    return bad


def plot(path, name, res, other="PC loop (FMU over the link)"):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    sigs = res["signals"]
    fig, axes = plt.subplots(len(sigs), 1, figsize=(9, 2.6 * len(sigs)), sharex=True)
    for ax, (sig, (a, b)) in zip(axes, sigs.items()):
        ax.plot(res["t"], a, label="SIL (on-chip plant)", lw=1.6)
        ax.plot(res["t"], b, label=other, lw=1.0, ls="--")
        ax.set_ylabel(sig)
        ax.grid(alpha=0.3)
    axes[0].set_title(f"{name}: SIL vs {other.split(' (')[0]}")
    axes[0].legend(loc="best", fontsize=8)
    axes[-1].set_xlabel("t [s]")
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


COMPARE = {"motor": compare_motor, "t12": compare_t12}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("ctrl", choices=sorted(CASES))
    ap.add_argument("--build", default=str(Path.home() / "build/krumpir"))
    ap.add_argument("--check", action="store_true", help="exit 1 when a case fails")
    ap.add_argument("--no-plots", action="store_true")
    ap.add_argument("--case", action="append", help="only these cases (no report)")
    ap.add_argument("--board", metavar="DEVICE",
                    help="the controller on the board instead of the PC loop: its HIL UART "
                         "(e.g. /dev/ttyUSB0); paced, free-running")
    ap.add_argument("--console", metavar="PATH",
                    help="with --board: the board's console (UART0, e.g. /dev/ttyUSB1)")
    args = ap.parse_args()
    if args.board and not args.console:
        ap.error("--board needs --console (the setpoint and start go there)")
    console = Console(args.console) if args.board else None

    ctrl = args.ctrl
    docs = ROOT / "controllers" / PLANTS[ctrl]["dir"] / "docs"
    kind, other = ("board", "board (FMU over the UART)") if args.board else ("pc", "PC loop")
    plots = not args.no_plots
    if plots:
        try:
            import matplotlib  # noqa: F401
        except ImportError:
            plots = False
            print("compare: no matplotlib, numbers only")
    rows, failed, names = [], 0, []
    for name, case in CASES[ctrl].items():
        if args.case and name not in args.case:
            continue
        if args.board and case.get("at"):
            print(f"{name}: skipped on the board (a timed setpoint needs the PC controller's --at)")
            continue
        names.append(name)
        sil, late = run_sil(args.build, ctrl, name), None
        if args.board:
            run, late = run_board(args.build, ctrl, name, case, args.board, console)
            res = (aligned_motor(sil, run, case) if ctrl == "motor" else
                   compare_t12_board(sil, run, case))
        else:
            res = COMPARE[ctrl](sil, run_pc(args.build, ctrl, name, case), case)
        bad = verdict(res)
        failed += bool(bad)
        rms = ", ".join(f"{s} {v:.2f} %" for s, v in res["rms"].items())
        figs = ", ".join(f"{k} {a:.3g} / {b:.3g}" for k, (a, b, _) in res["figures"].items())
        trip = f"{res['trip'][0]} / {res['trip'][1]}"
        result = "PASS" if not bad else "FAIL: " + "; ".join(bad)
        rows.append(f"| {name} | {result} | {rms} | {figs} | {trip} |" +
                    (f" {late} |" if args.board else ""))
        print(f"{name}: {result} ({rms}{f', late {late}' if args.board else ''})")
        if plots:
            plot(docs / f"sil_vs_{kind}_{name}.png", name, res, other)
    if not args.case and not args.check:  # --check (CTest) leaves the committed report alone
        what = ("the controller on the ESP32 against the plant FMU in the runner over its HIL "
                "UART, paced and free-running (compared from the runner's log: the tip temperature "
                "and the heater's duty per cycle; a trip is series_on dropping; cases with a timed "
                "setpoint are left out)" if args.board else
                "the PC loop (the same controller code against the plant FMU in the runner over "
                "the HIL link)")
        cmd = f"tools/hil/compare.py {ctrl}" + (" --board DEVICE --console PATH" if args.board
                                                else "")
        head = "| case | result | RMS error | figures (SIL / other) | trip (SIL / other) |"
        report = [f"# {ctrl}: SIL vs {other.split(' (')[0]}", "",
                  f"Generated by `{cmd}`: each closed-loop scenario on the controller's on-chip "
                  f"plant (SIL) and as {what}. RMS error in % of full scale; figures and trips SIL "
                  f"/ other. Pass: RMS under {RMS_LIMIT_PCT:g} %, figures within {FIG_REL:.0%} (or "
                  "a small floor), the same trip.", "",
                  head + (" late steps |" if args.board else ""),
                  "|---|---|---|---|---|" + ("---|" if args.board else ""), *rows, ""]
        if plots:
            report += [f"![{n}](sil_vs_{kind}_{n}.png)" for n in names] + [""]
        out = docs / f"sil_vs_{kind}.md"
        out.write_text("\n".join(report), encoding="utf-8")
        print(f"compare: wrote {out}")
    sys.exit(1 if args.check and failed else 0)


if __name__ == "__main__":
    main()
