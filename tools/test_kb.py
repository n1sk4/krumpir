#!/usr/bin/env python3
"""Self-check of kb.py: arguments, the build folder, the controllers, and the commands each
sub-command runs (with --dry-run, so nothing is built). Run: python tools/test_kb.py (exit 0 = ok)."""
import contextlib
import io
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kb  # noqa: E402


def dry(*argv):
    """The commands kb would run for argv, one per line (the '+ ' lines)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = kb.main(["--dry-run", *argv])
    assert rc == 0, (argv, rc)
    return [line[2:] for line in out.getvalue().splitlines() if line.startswith("+ ")]


def usage_error(*argv):
    with contextlib.redirect_stderr(io.StringIO()):
        try:
            kb.main(list(argv))
        except SystemExit as e:
            return e.code
    return 0


def main():
    names = kb.controllers()
    assert {"motor", "t12"} <= set(names), names
    assert kb.controller("t12")[1].name == "esp32-t12-controller"
    assert kb.controller("esp32-motor-controller")[0] == "motor"

    os.environ["KRUMPIR_BUILD_ROOT"] = str(Path("kbtest-root").resolve())
    bdir = kb.build_dir("dev")
    assert bdir == Path("kbtest-root").resolve() / "dev"

    build = dry("build", "--preset", "dev", "--only", "runner;tui")
    assert build[0].startswith("cmake --preset dev -B ") and "'-DKRUMPIR_ONLY=runner;tui'" in build[0]
    assert build[1].startswith("cmake --build "), build

    assert dry("test", "--preset", "dev", "-R", "hil_")[-1].endswith("--output-on-failure -R hil_")

    t12, motor = kb.controller("t12")[1], kb.controller("motor")[1]
    fw = dry("fw", "t12", "hil")
    assert fw[0].endswith(f"run -e esp32dev-hil   (in {t12})"), fw
    flash = dry("flash", "motor", "--monitor")
    assert "run -e esp32dev -t upload" in flash[0] and flash[1].endswith(f"device monitor   (in {motor})")

    if kb.which("xcpclient"):
        a2l = dry("a2l", "t12", "--host", "--elf", "t12_host")
        assert "--port 5556" in a2l[0] and a2l[0].endswith("--embed-aml") and "fix_host_a2l" in a2l[1]

    if kb.which("socat"):
        assert "pc_loop.sh t12-open --check" in dry("loop", "t12-open")[0]

    cmp_ = dry("compare", "motor", "--board", "/dev/ttyUSB0", "--console", "/dev/ttyUSB1")
    assert "compare.py" in cmp_[0] and " motor --build " in cmp_[0], cmp_
    assert cmp_[0].endswith("--board /dev/ttyUSB0 --console /dev/ttyUSB1"), cmp_

    assert any("gen_controller.py" in c and "--check" in c for c in dry("check"))
    assert dry("clean", "--preset", "dev") == [f"remove {bdir}"]

    assert usage_error("fw", "t12", "nope") == 2               # not an env
    assert usage_error("compare", "t12", "--board", "x") == 2  # --board without --console
    assert usage_error("frobnicate") == 2
    print("test_kb: ok")


if __name__ == "__main__":
    main()
