#!/usr/bin/env python3
"""Check a plant FMU with FMPy.

    python check_fmu.py <file.fmu>

1. Validates the model description (and buildDescription.xml for FMI 3.0).
2. Runs the default experiment with the FMU's own binary.
3. Rebuilds the binary from the FMU's sources/ alone (FMPy's CMake build) and runs it again: the
   result must be the same, which shows the FMU also works as a source FMU on other platforms.

Exit status: 0 all of it passes, 1 a check failed, 2 usage.
"""
import argparse
import math
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

import numpy as np
from fmpy import read_model_description, simulate_fmu
from fmpy.build import build_platform_binary
from fmpy.validation import validate_fmu


def finite(result):
    for name in result.dtype.names:
        if not all(math.isfinite(float(x)) for x in result[name]):
            print("not finite:", name)
            return False
    return True


def check(path):
    problems = validate_fmu(path)
    for p in problems:
        print("problem:", p)
    if problems:
        return 1

    md = read_model_description(path)
    print("%s: FMI %s, %d variables" % (md.modelName, md.fmiVersion, len(md.modelVariables)))
    result = simulate_fmu(path, fmi_type="CoSimulation")
    if not finite(result):
        return 1
    for name in result.dtype.names:
        if name != "time":
            print("  %-24s first %-12g last %g" % (name, result[name][0], result[name][-1]))
    print("%d rows, t = %g .. %g" % (len(result), result["time"][0], result["time"][-1]))

    with tempfile.TemporaryDirectory() as tmp:
        unzipdir = Path(tmp) / "fmu"
        with zipfile.ZipFile(path) as zf:
            zf.extractall(unzipdir)
        shutil.rmtree(unzipdir / "binaries")
        # FMI 3.0 source FMUs prefix their function names with the model identifier unless told
        # otherwise (for static linking). FMPy loads the library dynamically and needs the plain
        # names, so this copy is built with DISABLE_PREFIX, as such an importer would.
        bd = unzipdir / "sources" / "buildDescription.xml"
        if bd.exists():
            text = bd.read_text()
            define = '<PreprocessorDefinition name="DISABLE_PREFIX"/>'
            bd.write_text(text.replace("</SourceFileSet>", define + "</SourceFileSet>", 1))
        build_dir = Path(tmp) / "build"
        build_dir.mkdir()
        build_platform_binary(unzipdir, build_dir=build_dir)
        rebuilt = simulate_fmu(str(unzipdir), fmi_type="CoSimulation")
    for name in result.dtype.names:
        if not np.allclose(result[name], rebuilt[name], rtol=0, atol=1e-12):
            print("source build differs:", name)
            return 1
    print("source build: same result")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("fmu", help="the FMU to check")
    return check(p.parse_args().fmu)


if __name__ == "__main__":
    sys.exit(main())
