"""EcuPort against the T12 controller on its board, over Wi-Fi. Skipped unless KRUMPIR_ECU_A2L
names the A2L of the firmware that is running (controllers/README.md, XCP);
the address comes from the A2L, or from KRUMPIR_ECU_HOST.

    KRUMPIR_ECU_A2L=controllers/esp32-t12-controller/docs/t12.a2l python -m pytest tests/test_ecu_board.py
"""
import os
import time

import pytest

from krumpir import EcuPort

pytestmark = pytest.mark.skipif("KRUMPIR_ECU_A2L" not in os.environ,
                                reason="no board: set KRUMPIR_ECU_A2L")


@pytest.fixture
def ecu():
    with EcuPort(os.environ["KRUMPIR_ECU_A2L"], host=os.environ.get("KRUMPIR_ECU_HOST")) as e:
        yield e


def test_measure(ecu):
    first = ecu.read("s_loop.cycles")
    time.sleep(0.3)
    m = ecu.read("s_loop.cycles", "s_loop.temp", "s_loop.handle", "s_loop.duty")
    assert m["s_loop.cycles"] > first  # the control loop runs
    assert -20 < m["s_loop.temp"] < 500 and -20 < m["s_loop.handle"] < 100
    assert 0 <= m["s_loop.duty"] <= 1


def test_calibrate_setpoint(ecu):
    """A calibration write reaches the controller; the old value is put back."""
    old = ecu.read("t12_cal_page.setpoint")
    new = 300.0 if old != 300.0 else 320.0
    try:
        ecu.write({"t12_cal_page.setpoint": new})
        deadline = time.monotonic() + 1.0
        while ecu.read("s_loop.ctrl.setpoint") != new:
            assert time.monotonic() < deadline, "the controller did not take the new setpoint"
            time.sleep(0.02)
    finally:
        ecu.write({"t12_cal_page.setpoint": old})
    assert ecu.read("t12_cal_page.setpoint") == old
