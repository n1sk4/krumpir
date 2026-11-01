"""Fixtures: a running service and a model-access port on it.

By default the tests start `krumpir serve` on a free local port, in runner/ (where fmus/ is), from
KRUMPIR_EXE (default ~/build/krumpir/krumpir). With KRUMPIR_SERVICE=host:port they use a service
that is already running instead (e.g. on the Pi, started in its runner/ directory).
"""
import os
import subprocess
from pathlib import Path

import pytest

from krumpir import Testbench

RUNNER = Path(__file__).resolve().parents[3] / "runner"


@pytest.fixture(scope="session")
def service():
    if "KRUMPIR_SERVICE" in os.environ:
        host, port = os.environ["KRUMPIR_SERVICE"].rsplit(":", 1)
        yield host, int(port)
        return
    exe = Path(os.environ.get("KRUMPIR_EXE", "~/build/krumpir/krumpir")).expanduser()
    if not exe.exists():
        pytest.skip(f"{exe} not found: build the runner or set KRUMPIR_EXE")
    if not (RUNNER / "fmus/Feedthrough.fmu").exists():
        pytest.skip("runner/fmus/Feedthrough.fmu not found (runner/tools/fetch_fmus.sh)")
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
def ma(tb):
    """A model-access port; whatever the test loaded is unloaded afterwards."""
    port = tb.ma_port()
    yield port
    if port.state != "IDLE":
        port.unload()
