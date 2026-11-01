"""Bench tests of the T12 soldering tip plant (libs/plants/t12) through the service: energy
balance, the thermocouple reading, the fault modes, and a thermostat closed over the service
in real time (the test is the controller)."""
import time

import pytest

from krumpir import Signal

FMU = "t12tip.fmu"
AMBIENT = 25.0  # the FMU's default ambient_c


def test_energy_balance_while_heating(ma, scenario):
    """1 s at full power: heat stored in the three nodes plus heat lost equals V^2/R * 1 s."""
    ma.load_toml(scenario(FMU, ["heater"], ["t_heater_c", "t_tip_c", "t_handle_c"],
                          duration=1.0))
    p = ma.read("c_h", "c_t", "c_c", "r_ta", "r_ca", "v_supply", "r_coil")
    ma.signal("heater", Signal().const(1.0))
    cap = ma.capture(["t_heater_c", "t_tip_c", "t_handle_c"])
    ma.start()
    assert ma.wait(timeout=10)
    res = cap.fetch()

    rise = {k: res[k][-1] - AMBIENT for k in ("t_heater_c", "t_tip_c", "t_handle_c")}
    stored = (p["c_h"] * rise["t_heater_c"] + p["c_t"] * rise["t_tip_c"]
              + p["c_c"] * rise["t_handle_c"])
    lost = sum(0.001 * ((tt - AMBIENT) / p["r_ta"] + (tc - AMBIENT) / p["r_ca"])
               for tt, tc in zip(res["t_tip_c"], res["t_handle_c"]))
    energy_in = p["v_supply"] ** 2 / p["r_coil"] * 1.0
    assert stored + lost == pytest.approx(energy_in, rel=5e-3)
    assert res["t_heater_c"][-1] > res["t_tip_c"][-1] > res["t_handle_c"][-1] > AMBIENT


def test_thermocouple_reads_after_switch_off(ma, scenario):
    """The amplifier is clamped while heating and settles to the thermocouple voltage after."""
    ma.load_toml(scenario(FMU, ["heater"], ["v_amp", "t_heater_c", "t_handle_c"], duration=1.2))
    p = ma.read("seebeck", "amp_gain", "amp_offset", "v_clamp", "tau_sense")
    ma.signal("heater", Signal().const(1.0, 1.0).const(0.0))
    heating = ma.capture(["v_amp"], stop=("heater", "<", 0.5))
    after = ma.capture(["v_amp", "t_heater_c", "t_handle_c"], start=("heater", "<", 0.5),
                       duration=0.05)
    ma.start()
    assert ma.wait(timeout=10)

    assert set(heating.fetch()["v_amp"][:-1]) == {p["v_clamp"]}
    res = after.fetch()
    k = int(20 * p["tau_sense"] / 0.001)  # settled
    expect = p["amp_gain"] * (p["seebeck"] * (res["t_heater_c"][k] - res["t_handle_c"][k])
                              + p["amp_offset"])
    assert 0 < expect < p["v_clamp"]
    assert res["v_amp"][k] == pytest.approx(expect, rel=1e-3)


@pytest.mark.parametrize("fault, heater, current, heats", [
    (0, 1.0, lambda p: p["v_supply"] / p["r_coil"], True),
    (1, 1.0, lambda p: 0.0, False),                          # open: no tip
    (2, 1.0, lambda p: p["v_supply"] / p["r_short"], False),  # shorted tip: current, no heat
    (3, 0.0, lambda p: p["v_supply"] / p["r_coil"], True),    # heater switch stuck on
], ids=["none", "open", "short", "stuck_on"])
def test_fault_modes(ma, scenario, fault, heater, current, heats):
    ma.load_toml(scenario(FMU, ["heater", "fault"], ["i_heater", "t_tip_c"], duration=0.5))
    p = ma.read("v_supply", "r_coil", "r_short")
    ma.signal("heater", Signal().const(heater))
    ma.signal("fault", Signal().const(fault))
    ma.start()
    assert ma.wait(timeout=10)
    assert ma.read("i_heater") == pytest.approx(current(p))
    assert (ma.read("t_tip_c") > AMBIENT + 1) == heats


def run_thermostat(ma, setpoint, seconds, heater, band=1.0):
    """Two-point control from the test: read the tip, switch the heater, every ~5 ms."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        t = ma.read("t_tip_c")
        want = 1.0 if t < setpoint - band else 0.0 if t > setpoint + band else heater
        if want != heater:
            ma.write(heater=want)
            heater = want
        time.sleep(0.005)
    return heater


def test_thermostat_holds_setpoint_under_load(ma, scenario):
    setpoint = 150.0
    ma.load_toml(scenario(FMU, ["heater", "load_wk"], ["t_tip_c"], wait="sleep"))
    ma.start()

    heater, deadline = 1.0, time.monotonic() + 15
    ma.write(heater=heater)
    while ma.read("t_tip_c") < setpoint:  # heat up at full power
        assert time.monotonic() < deadline, "the tip did not reach the setpoint"
        time.sleep(0.01)

    free = ma.capture(["t_tip_c"], duration=1.0)
    heater = run_thermostat(ma, setpoint, 1.1, heater)
    ma.write(load_wk=0.1)  # touch a joint: 0.1 W/K more loss
    loaded = ma.capture(["t_tip_c", "heater"], duration=1.0)
    run_thermostat(ma, setpoint, 1.1, heater)
    ma.stop()

    for res in (free.fetch(wait=True, timeout=5), loaded.fetch(wait=True, timeout=5)):
        tip = res["t_tip_c"]
        assert len(tip) == 1000
        assert abs(sum(tip) / len(tip) - setpoint) < 8
        assert setpoint - 10 < min(tip) and max(tip) < setpoint + 20
    duty = sum(loaded.fetch()["heater"]) / 1000
    assert 0.1 < duty < 0.9  # the controller works harder but keeps up
