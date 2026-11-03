#!/usr/bin/env python3
"""Merge the plant FMUs of several builds (one per platform) into FMUs that carry all binaries.

    python merge_fmus.py OUT_DIR BUILD_DIR [BUILD_DIR ...]
    python merge_fmus.py --selftest

Every <BUILD_DIR>/fmus/fmi{2,3}/<name>.fmu is merged into OUT_DIR/fmi{2,3}/<name>.fmu: the
binaries/<platform>/ folders of all builds are combined. Everything else (modelDescription.xml,
sources/) must be identical in all builds, otherwise the merge fails. Standard library only.
Exit status: 0 ok, 1 the merge failed, 2 usage.
"""
import argparse
import sys
import tempfile
import zipfile
from pathlib import Path


def merge(out, fmus):
    files = {}
    for fmu in fmus:
        with zipfile.ZipFile(fmu) as zf:
            for info in zf.infolist():
                if info.is_dir():
                    continue
                data = zf.read(info)
                if info.filename in files and not info.filename.startswith("binaries/"):
                    if files[info.filename] != data:
                        raise SystemExit(f"{fmu}: {info.filename} differs from the other builds")
                if info.filename.startswith("binaries/") and info.filename in files:
                    raise SystemExit(f"{fmu}: {info.filename} is in two builds")
                files[info.filename] = data
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in sorted(files):
            zf.writestr(name, files[name])
    platforms = sorted({n.split("/")[1] for n in files if n.startswith("binaries/")})
    print(f"{out}: {', '.join(platforms)}")


def selftest():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        def fmu(name, files):
            with zipfile.ZipFile(tmp / name, "w") as zf:
                for k, v in files.items():
                    zf.writestr(k, v)
            return tmp / name

        a = fmu("a.fmu", {"modelDescription.xml": "md", "binaries/x86_64-linux/p.so": "x"})
        b = fmu("b.fmu", {"modelDescription.xml": "md", "binaries/aarch64-linux/p.so": "y"})
        merge(tmp / "out.fmu", [a, b])
        with zipfile.ZipFile(tmp / "out.fmu") as zf:
            assert sorted(zf.namelist()) == ["binaries/aarch64-linux/p.so",
                                             "binaries/x86_64-linux/p.so", "modelDescription.xml"]
        c = fmu("c.fmu", {"modelDescription.xml": "other"})
        try:
            merge(tmp / "bad.fmu", [a, c])
            raise AssertionError("a differing modelDescription.xml was merged")
        except SystemExit as e:
            assert "differs" in str(e)
    print("merge_fmus: selftest ok")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("out_dir", nargs="?", type=Path)
    p.add_argument("builds", nargs="*", type=Path, metavar="BUILD_DIR")
    p.add_argument("--selftest", action="store_true", help="check the merge and exit")
    a = p.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not a.out_dir or not a.builds:
        p.error("OUT_DIR and at least one BUILD_DIR are needed")
    first = a.builds[0] / "fmus"
    fmus = sorted(first.glob("fmi*/*.fmu"))
    if not fmus:
        sys.exit(f"no FMUs in {first}")
    for fmu in fmus:
        rel = fmu.relative_to(first)
        parts = [b / "fmus" / rel for b in a.builds]
        missing = [str(p) for p in parts if not p.exists()]
        if missing:
            sys.exit(f"missing: {', '.join(missing)}")
        merge(a.out_dir / rel, parts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
