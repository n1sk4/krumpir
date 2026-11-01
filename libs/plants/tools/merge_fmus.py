"""Merge the plant FMUs of several builds (one per platform) into FMUs that carry all binaries.

Usage: python merge_fmus.py OUT_DIR BUILD_DIR [BUILD_DIR ...]

Every <BUILD_DIR>/fmus/fmi{2,3}/<name>.fmu is merged into OUT_DIR/fmi{2,3}/<name>.fmu: the
binaries/<platform>/ folders of all builds are combined. Everything else (modelDescription.xml,
sources/) must be identical in all builds, otherwise the merge fails. Standard library only.
"""
import sys
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


def main(args):
    if len(args) < 2:
        sys.exit(__doc__)
    out_dir, builds = Path(args[0]), [Path(b) for b in args[1:]]
    first = builds[0] / "fmus"
    fmus = sorted(first.glob("fmi*/*.fmu"))
    if not fmus:
        sys.exit(f"no FMUs in {first}")
    for fmu in fmus:
        rel = fmu.relative_to(first)
        parts = [b / "fmus" / rel for b in builds]
        missing = [str(p) for p in parts if not p.exists()]
        if missing:
            sys.exit(f"missing: {', '.join(missing)}")
        merge(out_dir / rel, parts)


if __name__ == "__main__":
    main(sys.argv[1:])
