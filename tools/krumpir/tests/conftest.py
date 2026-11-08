"""Fixtures: a running service and a model-access port on it.

By default the tests start `krumpir serve` on a free local port, in runner/ (where fmus/ is), from
KRUMPIR_EXE (default: kb.py's dev build, e.g. ~/build/krumpir/dev/runner/krumpir on WSL). With
KRUMPIR_SERVICE=host:port they use a service that is already running instead (e.g. on the Pi,
started in its runner/ directory).
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

from krumpir import Testbench

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from kb import build_dir  # noqa: E402  the build folder, as kb.py makes it

RUNNER = Path(__file__).resolve().parents[3] / "runner"


@pytest.fixture(scope="session")
def service():
    if "KRUMPIR_SERVICE" in os.environ:
        host, port = os.environ["KRUMPIR_SERVICE"].rsplit(":", 1)
        yield host, int(port)
        return
    exe = Path(os.environ.get("KRUMPIR_EXE", build_dir("dev") / "runner/krumpir")).expanduser()
    if not exe.exists():
        pytest.skip(f"{exe} not found: build the runner or set KRUMPIR_EXE")
    if not (RUNNER / "fmus/Feedthrough.fmu").exists():
        pytest.skip("runner/fmus/Feedthrough.fmu not found (configure the runner)")
    srv = subprocess.Popen([str(exe), "serve", "127.0.0.1:0"], cwd=RUNNER,
                           stdout=subprocess.PIPE, text=True)
    line = srv.stdout.readline().split()  # listening on host:port
    try:
        yield "127.0.0.1", int(line[2].rsplit(":", 1)[1])
    finally:
        srv.terminate()
        srv.wait(timeout=10)


@pytest.fixture
def tb(service):
    with Testbench(*service) as tb:
        yield tb


@pytest.fixture
def scenario(request):
    """Builds the TOML text of a scenario for an FMU in runner/fmus. wait="none" runs as fast as
    possible (set signals before the start); "sleep" runs in real time (drive it while it runs)."""

    def build(fmu, inputs, outputs, *, duration=None, wait="none", params=None):
        if "KRUMPIR_SERVICE" not in os.environ and not (RUNNER / "fmus" / fmu).exists():
            pytest.skip(f"runner/fmus/{fmu} not found (build the runner)")
        name = re.sub(r"\W", "_", request.node.name)
        lines = ["[model]", f'fmu = "fmus/{fmu}"', "step = 0.001"]
        if duration is not None:
            lines.append(f"duration = {duration}")
        if params:
            lines.append("params = { " + ", ".join(f"{k} = {v!r}" for k, v in params.items()) + " }")
        lines += ["[rt]", f'wait = "{wait}"', "max_overruns = 1000000000",
                  "[io]", 'backend = "null"', f"inputs = {json.dumps(inputs)}",
                  f"outputs = {json.dumps(outputs)}", "[log]", f'file = "runs/pytest_{name}.csv"']
        return "\n".join(lines) + "\n"

    return build


@pytest.fixture
def ma(tb):
    """A model-access port; whatever the test loaded is unloaded afterwards."""
    port = tb.ma_port()
    yield port
    if port.state != "IDLE":
        port.unload()
