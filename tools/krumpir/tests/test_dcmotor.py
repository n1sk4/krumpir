"""Bench tests of the DC motor plant (libs/plants/dcmotor) through the service.

The expected values come from the model's own parameters, read through the model access port:
steady speed w = (Kt V - R T_load) / (R b + Kt Ke), current i = (b w + T_load) / Kt, and a
mechanical time constant of about J R / (R b + Kt Ke).
"""
import time

import pytest

from krumpir import Signal

PARAMS = ["R", "L", "Kt", "Ke", "J", "b", "Vbus"]


def load(ma, scenario, **kw):
    ma.load_toml(scenario("dcmotor.fmu", ["duty", "load_torque"], ["speed", "current"], **kw))
    return ma.read(*PARAMS)


def steady(p, duty, load_torque=0.0):
    w = (p["Kt"] * duty * p["Vbus"] - p["R"] * load_torque) / (p["R"] * p["b"] + p["Kt"] * p["Ke"])
    return w, (p["b"] * w + load_torque) / p["Kt"]


@pytest.mark.parametrize("duty", [0.25, 0.5, -0.5, 1.0])
def test_steady_speed_and_current(ma, scenario, duty):
    p = load(ma, scenario, duration=0.4)
    ma.signal("duty", Signal().const(duty))
    cap = ma.capture(["speed", "current"])
    ma.start()
    assert ma.wait(timeout=10) and ma.state == "STOPPED"

    res = cap.fetch()
    w, i = steady(p, duty)
    assert res["speed"][-1] == pytest.approx(w, rel=5e-3)  # float32 plant
    assert res["current"][-1] == pytest.approx(i, rel=5e-3)


def test_step_response_time_constant(ma, scenario):
    p = load(ma, scenario, duration=0.3)
    ma.signal("duty", Signal().const(0.0, 0.05).const(0.5))
    cap = ma.capture(["speed"], start=("duty", ">=", 0.5), duration=0.2)
    ma.start()
    res = cap.fetch(wait=True, timeout=10)

    w, _ = steady(p, 0.5)
    t0 = res["t"][0] - 0.001  # the first row is the state after the first step at duty 0.5
    t63 = next(t for t, v in zip(res["t"], res["speed"]) if v >= 0.632 * w) - t0
    tau = p["J"] * p["R"] / (p["R"] * p["b"] + p["Kt"] * p["Ke"]) + p["L"] / p["R"]
    assert t63 == pytest.approx(tau, rel=0.05)
    assert max(res["speed"]) <= w * 1.001  # overdamped: no overshoot


def test_load_disturbance_while_running(ma, scenario):
    """Real-time run: the load is applied by the test while the motor turns."""
    p = load(ma, scenario, wait="sleep")
    ma.signal("duty", Signal().const(0.5))
    ma.start()
    time.sleep(0.3)  # > 10 time constants
    w0, _ = steady(p, 0.5)
    assert ma.read("speed") == pytest.approx(w0, rel=5e-3)

    cap = ma.capture(["speed", "current", "load_torque"], start=("load_torque", ">", 0),
                     duration=0.3)
    ma.write(load_torque=2e-4)
    res = cap.fetch(wait=True, timeout=5)
    ma.stop()

    w1, i1 = steady(p, 0.5, 2e-4)
    assert res["speed"][0] == pytest.approx(w0, rel=5e-3)  # the drop starts with the load
    assert res["speed"][-1] == pytest.approx(w1, rel=5e-3)
    assert res["current"][-1] == pytest.approx(i1, rel=5e-3)
    assert min(res["speed"]) >= w1 * 0.999  # settles from above, no undershoot
