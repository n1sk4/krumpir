#!/usr/bin/env python3
"""kb: one entry point to build, test and check the repository (Windows, Linux, WSL, the Pi).

    python tools/kb.py info                   what this machine can build, and the build folder
    python tools/kb.py setup [--system]       what is missing: tools, the venv, the Reference FMUs
    python tools/kb.py build [--preset P] [--only PARTS]
    python tools/kb.py test  [--preset P] [-R REGEX]
    python tools/kb.py check                  codegen up to date, the tools' self-checks
    python tools/kb.py all   [--preset P]     setup, build, test, check
    python tools/kb.py fmus  [--both-archs]   the plant FMUs (both: x86_64 + aarch64, merged)
    python tools/kb.py fw    CTRL [ENV]       a firmware: pio run -e esp32dev[-ENV] (ENV: hil, xcp)
    python tools/kb.py flash CTRL [ENV] [--monitor]
    python tools/kb.py a2l   CTRL (--ip IP | --host --elf ELF [--port N])
    python tools/kb.py loop  KIND [--live]    a closed loop on this PC (tools/hil/pc_loop.sh)
    python tools/kb.py compare CTRL [--board DEV --console PATH]   SIL vs PC loop (or the board)
    python tools/kb.py clean [--preset P]

CTRL is a controller's name (motor, t12) or its folder. --dry-run prints the commands instead of
running them. It only runs the repository's own builds (CMake presets, PlatformIO, the scripts in
tools/), in the right order with the right options; what this machine cannot do, it skips and says
why. On Windows run it with `python` (python3 there is the Microsoft Store stub).

The build folder: $KRUMPIR_BUILD_ROOT/<preset> when that is set; on WSL with the repository on
/mnt/<drive>, ~/build/krumpir/<preset> (the Linux side builds much faster); else build/<preset>.
Exit status: 0 ok, 1 a step failed, 2 usage.
"""
import argparse
import os
import platform
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PY_MIN = (3, 13)  # also tools/krumpir/pyproject.toml, runner/tests/CMakeLists.txt, the CI
DRY = False


# --- the machine -----------------------------------------------------------------------------

def is_windows():
    return os.name == "nt"


def is_wsl():
    return platform.system() == "Linux" and "microsoft" in platform.release().lower()


def which(name):
    """A tool on PATH, or PlatformIO's own copy for pio."""
    found = shutil.which(name)
    if found or name != "pio":
        return found
    pio = Path.home() / ".platformio" / "penv" / ("Scripts" if is_windows() else "bin") / "pio"
    pio = pio.with_suffix(".exe") if is_windows() else pio
    return str(pio) if pio.exists() else None


def can():
    """What this machine can build or run: ({what: ok}, {what: why not})."""
    linux = platform.system() == "Linux"
    have = {
        "cmake": (bool(which("cmake")), "CMake 3.25+ is not on PATH"),
        "ninja": (bool(which("ninja")), "Ninja is not on PATH"),
        "c compiler": (bool(which("gcc") or which("cc")), "no gcc or cc on PATH"),
        "runner": (linux, "the runner is Linux only (POSIX, termios)"),
        "cross": (bool(which("aarch64-linux-gnu-gcc") and which("qemu-aarch64")),
                  "no aarch64 cross compiler and qemu (gcc-aarch64-linux-gnu, qemu-user)"),
        "socat": (bool(which("socat")), "no socat: the closed loops on this PC are skipped"),
        "pio": (bool(which("pio")), "no PlatformIO (pip install platformio)"),
        "xcpclient": (bool(which("xcpclient")), "no xcpclient (cargo install, see XCPlite)"),
        "venv": (venv_python().exists(), f"no venv at {venv_python().parent.parent}"),
    }
    return {k: v[0] for k, v in have.items()}, {k: v[1] for k, v in have.items()}


def default_preset():
    return "mingw" if is_windows() else "dev"


def build_dir(preset):
    root = os.environ.get("KRUMPIR_BUILD_ROOT")
    if root:
        return Path(root).expanduser() / preset
    if is_wsl() and str(ROOT).startswith("/mnt/"):
        return Path.home() / "build" / "krumpir" / preset
    return ROOT / "build" / preset


def venv_python():
    if is_windows():
        return ROOT / ".venv" / "Scripts" / "python.exe"
    venv = Path(os.environ.get("KRUMPIR_VENV", Path.home() / ".venvs" / "krumpir"))
    return venv / "bin" / "python"


def python():
    """The venv's Python when there is one (fmpy, matplotlib), else this one."""
    v = venv_python()
    return str(v) if v.exists() else sys.executable


def controllers():
    """{name: folder} of every controller (its controller.toml [controller] name)."""
    import tomllib
    out = {}
    for toml in sorted((ROOT / "controllers").glob("*/controller.toml")):
        with open(toml, "rb") as f:
            out[tomllib.load(f)["controller"]["name"]] = toml.parent
    return out


def controller(name):
    """(name, folder) of a controller given by its name or its folder."""
    for n, d in controllers().items():
        if name in (n, d.name):
            return n, d
    sys.exit(f"kb: no controller '{name}' (have: {', '.join(controllers())})")


# --- running ---------------------------------------------------------------------------------

def run(cmd, cwd=ROOT, env=None):
    """Runs a command (or prints it with --dry-run). Returns its exit status."""
    shown = " ".join(shlex.quote(str(c)) for c in cmd)
    print(f"+ {shown}" + (f"   (in {cwd})" if Path(cwd) != ROOT else ""), flush=True)
    if DRY:
        return 0
    return subprocess.run([str(c) for c in cmd], cwd=cwd,
                          env={**os.environ, **(env or {})}).returncode


def step(ok, what):
    if not ok:
        print(f"kb: {what} failed", file=sys.stderr)
        sys.exit(1)


def skip(what, why):
    print(f"kb: skipped {what}: {why}")


def need(tool):
    """The tool's path; exits with why when it is missing (a dry run just names it)."""
    path = which(tool)
    if not path and not DRY:
        sys.exit(f"kb: {can()[1][tool]}")
    return path or tool


# --- commands --------------------------------------------------------------------------------

def cmd_info(a):
    have, why = can()
    wsl = " (WSL)" if is_wsl() else ""
    print(f"repository  {ROOT}")
    print(f"platform    {platform.system()}{wsl}, Python {platform.python_version()}")
    print(f"preset      {default_preset()} -> {build_dir(default_preset())}")
    for k, ok in have.items():
        print(f"{k:12}{'yes' if ok else 'no: ' + why[k]}")
    print(f"controllers {', '.join(f'{n} ({d.name})' for n, d in controllers().items())}")
    return 0


def cmd_setup(a):
    have, why = can()
    if a.system:
        if is_windows() or not which("apt-get"):
            skip("system packages", "only on Debian, Ubuntu, WSL or Raspberry Pi OS (apt-get)")
        else:
            sudo = [] if os.geteuid() == 0 else ["sudo"]
            pkgs = ["build-essential", "cmake", "ninja-build", "libexpat1-dev",
                    "python3-venv", "python3-pip", "socat", "gcc-aarch64-linux-gnu", "qemu-user"]
            step(run(sudo + ["apt-get", "update"]) == 0, "apt-get update")
            step(run(sudo + ["apt-get", "install", "-y"] + pkgs) == 0, "apt-get install")
    for tool in ("cmake", "ninja", "c compiler"):
        if not have[tool]:
            print(f"kb: missing: {why[tool]}")
    venv = venv_python()
    # made before a distro upgrade: an older Python, or its packages (pip) for the old version
    old = venv.exists() and not DRY and subprocess.run(
        [venv, "-c", f"import sys, pip; sys.exit(sys.version_info < {PY_MIN})"],
        capture_output=True).returncode != 0
    if old or not venv.exists():
        step(run([sys.executable, "-m", "venv", "--clear", venv.parent.parent]) == 0,
             "creating the venv")
    step(run([venv, "-m", "pip", "install", "-q", "-r", ROOT / "tools/requirements.txt"]) == 0,
         "pip install")
    for sub in ("libs/vendor/Reference-FMUs", "libs/vendor/XCPlite"):
        if not (ROOT / sub / "src").exists():
            print(f"kb: the submodule {sub} is empty: git submodule update --init {sub}")
    if not (ROOT / "runner/fmus/Dahlquist.fmu").exists():
        if have["runner"] and have["cmake"]:
            step(run(["cmake", "-P", ROOT / "cmake/fetch_reference_fmus.cmake"]) == 0,
                 "the Reference FMUs")
        else:
            skip("the Reference FMUs", "they are for the runner (Linux, with CMake)")
    return 0


def cmd_build(a):
    need("cmake")
    need("ninja")
    bdir = build_dir(a.preset)
    # always set: a cached --only from an earlier build would otherwise stick
    cfg = ["cmake", "--preset", a.preset, "-B", bdir, f"-DKRUMPIR_ONLY={a.only or ''}"]
    step(run(cfg) == 0, "configure")
    step(run(["cmake", "--build", bdir]) == 0, "build")
    return 0


def cmd_test(a):
    bdir = build_dir(a.preset)
    if not DRY and not (bdir / "CTestTestfile.cmake").exists():
        sys.exit(f"kb: {bdir} is not built: python tools/kb.py build --preset {a.preset}")
    have, why = can()
    if have["runner"] and not have["socat"]:
        skip("the closed loops (hil_pc_*, hil_compare_*)", why["socat"])
    cmd = ["ctest", "--test-dir", bdir, "--output-on-failure"]
    if a.R:
        cmd += ["-R", a.R]
    step(run(cmd) == 0, "tests")
    return 0


def cmd_check(a):
    py = python()
    if not DRY and subprocess.run([py, "-c", "import jinja2, serial"],
                                  capture_output=True).returncode != 0:
        sys.exit(f"kb: {py} lacks the tools' packages (jinja2, pyserial): python tools/kb.py setup")
    step(run([py, ROOT / "tools/codegen/test_gen_controller.py"]) == 0, "the codegen self-test")
    for name, d in controllers().items():
        step(run([py, ROOT / "tools/codegen/gen_controller.py", d, "--check"]) == 0,
             f"codegen --check of {name}")
    for tool in ("tools/serial/capture.py", "tools/serial/timing_check.py",
                 "tools/hil/check_loop.py", "tools/hil/fix_host_a2l.py",
                 "libs/plants/tools/merge_fmus.py", "tools/check_links.py"):
        step(run([py, ROOT / tool, "--selftest"]) == 0, f"{tool} --selftest")
    step(run([py, ROOT / "tools/check_links.py"]) == 0, "the markdown links")
    if not DRY:  # kb itself (its test runs kb dry)
        step(run([py, ROOT / "tools/test_kb.py"]) == 0, "tools/test_kb.py")
    return 0


def cmd_all(a):
    for f in (cmd_setup, cmd_build, cmd_test, cmd_check):
        f(a)
    return 0


def cmd_fmus(a):
    if not can()[0]["runner"]:
        sys.exit("kb: the plant FMUs are built on Linux (WSL, the Pi)")
    if a.both_archs:
        return run(["bash", ROOT / "libs/plants/tools/build_fmus.sh", build_dir("plant-fmus")],
                   env={"KRUMPIR_PYTHON": python()})
    a.only = "plants"
    return cmd_build(a)


def pio_env(env):
    return "esp32dev" + (f"-{env}" if env else "")


def cmd_fw(a):
    return run([need("pio"), "run", "-e", pio_env(a.env)], cwd=controller(a.ctrl)[1])


def cmd_flash(a):
    pio, d = need("pio"), controller(a.ctrl)[1]
    rc = run([pio, "run", "-e", pio_env(a.env), "-t", "upload"], cwd=d)
    if rc == 0 and a.monitor:
        rc = run([pio, "device", "monitor"], cwd=d)
    return rc


def cmd_a2l(a):
    xcp = need("xcpclient")
    name, d = controller(a.ctrl)
    if a.host:
        if not a.elf:
            sys.exit("kb: --host needs --elf (the PC build made with -DKRUMPIR_XCP=ON)")
        port = a.port or {"motor": 5555, "t12": 5556}.get(name, 5555)
        a2l = d / "docs" / f"{name}_host.a2l"
        target = ["--udp", "--dest-addr", "127.0.0.1", "--port", port, "--elf", a.elf]
    else:
        if not a.ip:
            sys.exit("kb: the board's A2L needs --ip (the board prints it at boot)")
        a2l = d / "docs" / f"{name}.a2l"
        elf = a.elf or d / ".pio/build/esp32dev-xcp/firmware.elf"
        target = ["--udp", "--dest-addr", a.ip, "--elf", elf]
    rc = run([xcp, "--offline", *target, "--a2l", a2l, "--elf-unit-filter", "control_task",
              "--default-event", "tick", "--embed-aml"])
    if rc == 0 and a.host:
        rc = run([sys.executable, ROOT / "tools/hil/fix_host_a2l.py", a2l])
    return rc


def runner_build(preset):
    """The runner's folder in the root tree's build: its programs and the PC controllers."""
    return build_dir(preset) / "runner"


def cmd_loop(a):
    need("socat")
    b = runner_build(a.preset)
    ctrl = a.kind.split("-")[0]
    env = {"KRUMPIR_RUNNER": str(b / "krumpir"),
           "KRUMPIR_CONTROLLER": str(b / f"host-{ctrl}" / f"{ctrl}_host")}
    live = ["--live"] if a.live else []
    return run(["bash", ROOT / "tools/hil/pc_loop.sh", a.kind, "--check", *live], env=env)


def cmd_compare(a):
    cmd = [python(), ROOT / "tools/hil/compare.py", controller(a.ctrl)[0],
           "--build", runner_build(a.preset)]
    if a.board:
        cmd += ["--board", a.board, "--console", a.console]
    return run(cmd)


def cmd_clean(a):
    bdir = build_dir(a.preset)
    print(f"+ remove {bdir}")
    if not DRY and bdir.exists():
        shutil.rmtree(bdir)
    return 0


# --- arguments -------------------------------------------------------------------------------

def parser():
    ap = argparse.ArgumentParser(prog="kb", description=__doc__.split("\n\n")[0])
    ap.add_argument("--dry-run", action="store_true", help="print the commands, do not run them")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add(name, fn, help_, preset=False):
        p = sub.add_parser(name, help=help_)
        p.set_defaults(fn=fn, only=None, R=None, system=False)
        if preset:
            p.add_argument("--preset", default=default_preset())
        return p

    add("info", cmd_info, "what this machine can build")
    add("setup", cmd_setup, "tools, venv, Reference FMUs").add_argument(
        "--system", action="store_true", help="also apt-get the system packages (Debian, Ubuntu)")
    add("build", cmd_build, "configure and build the root tree", preset=True).add_argument(
        "--only", help="parts: libs;controllers;runner;plants;tui")
    add("test", cmd_test, "run the tests", preset=True).add_argument(
        "-R", metavar="REGEX", help="only the tests matching it")
    add("check", cmd_check, "codegen up to date, self-checks")
    add("all", cmd_all, "setup, build, test, check", preset=True)
    add("fmus", cmd_fmus, "the plant FMUs", preset=True).add_argument(
        "--both-archs", action="store_true", help="x86_64 and aarch64, merged")
    for name, fn in (("fw", cmd_fw), ("flash", cmd_flash)):
        p = add(name, fn, "build a firmware" if name == "fw" else "build and upload a firmware")
        p.add_argument("ctrl")
        p.add_argument("env", nargs="?", choices=["hil", "xcp"], help="omitted: esp32dev")
        if name == "flash":
            p.add_argument("--monitor", action="store_true")
    p = add("a2l", cmd_a2l, "an A2L from a build's ELF")
    p.add_argument("ctrl")
    p.add_argument("--ip", help="the board's IP address (the board's A2L)")
    p.add_argument("--host", action="store_true", help="the PC build's A2L")
    p.add_argument("--elf")
    p.add_argument("--port", type=int)
    p = add("loop", cmd_loop, "a closed loop on this PC", preset=True)
    p.add_argument("kind", help="motor, t12, motor-sensor, motor-link, t12-open, ...")
    p.add_argument("--live", action="store_true")
    p = add("compare", cmd_compare, "SIL vs PC loop, or vs the board", preset=True)
    p.add_argument("ctrl")
    p.add_argument("--board", metavar="DEV")
    p.add_argument("--console", metavar="PATH")
    add("clean", cmd_clean, "remove a build folder", preset=True)
    return ap


def main(argv=None):
    global DRY
    ap = parser()
    a = ap.parse_args(argv)
    DRY = a.dry_run
    if getattr(a, "board", None) and not getattr(a, "console", None):
        ap.error("--board needs --console")
    return a.fn(a) or 0


if __name__ == "__main__":
    if sys.version_info < PY_MIN:
        sys.exit(f"kb: needs Python {'.'.join(map(str, PY_MIN))}+, this is "
                 f"{platform.python_version()} (BUILDING.md, Setup)")
    sys.exit(main())
