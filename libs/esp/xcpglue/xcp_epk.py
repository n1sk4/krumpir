"""PlatformIO extra script (pre:) for the XCP builds.

Writes $BUILD_DIR/xcp_epk/xcp_epk.h with an EPK string that is new for every build. xcpclient copies
it into the A2L file and the target reports it on connect, so an A2L file from another build is
refused instead of measuring or calibrating wrong addresses. Only xcp_glue.c includes the header,
so a build recompiles that one file.
"""
import time
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO)

out = Path(env.subst("$BUILD_DIR")) / "xcp_epk"  # noqa: F821
out.mkdir(parents=True, exist_ok=True)
# No spaces or colons: XcpInit replaces them, and the A2L file would then differ from the target
(out / "xcp_epk.h").write_text('#define XCP_EPK "build_%s"\n' % time.strftime("%Y%m%d_%H%M%S"))
env.Append(CPPPATH=[str(out)])  # noqa: F821
