#!/usr/bin/env python3
"""Self-check of gen_controller.py: float literals, the description checks, and the rendering of a
small description. Run: python tools/codegen/test_gen_controller.py (exit 0 = ok)."""
import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_controller as g  # noqa: E402

GOOD = """
[controller]
name = "demo"
plant = "dcmotor"
lib = "democtrl"

[[param]]
name = "sp"
help = "Set the setpoint"
hint = "<x>"
min = -10
max = 10
default = 1.5
unit = "rpm"
cal = "setpoint"

[[param]]
name = "kp"
help = "Set kp"
hint = "<value>"
min = 0
max = 1e6
default = 7.2e-4
cal = true

[[command]]
name = "status"
help = "Show state"
handler = "cmd_status"
after_run = true

[[telemetry.column]]
name = "speed_rpm"
value = "s_loop.speed"

[[telemetry.column]]
name = "temp_c"
value = "s_loop.temp"
scale = 1000
nan = -1
"""


def model(text):
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp) / "demo-controller"
        (d / "lib" / "democtrl").mkdir(parents=True)
        (d / "controller.toml").write_text(text)
        return g.load(d)


def expect_error(text, fragment):
    try:
        model(text)
    except g.DescriptionError as e:
        assert fragment in str(e), f"expected '{fragment}' in '{e}'"
        return
    raise AssertionError(f"no error, expected '{fragment}'")


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def main():
    # float literals parse to the same float32 as the description's number
    for v in (0, 7.2e-4, 0.03, -10000, 1e6, 350, 0.05):
        lit = g.c_float(v)
        assert lit.endswith("f") and f32(float(lit[:-1])) == f32(v), lit
    assert g.c_string('a "b"') == '"a \\"b\\""' and g.c_string(None) == "NULL"

    m = model(GOOD)
    assert [p["name"] for p in m["params"]] == ["sp", "kp"]
    assert [p["cal"] for p in m["cal"]] == ["setpoint", "kp"]
    assert m["telemetry"][1]["row"] == "isfinite(s->v[1]) ? telemetry_round(s->v[1] * 1000.0f) : -1"
    files = {p.name: t for p, t in g.render(m).items()}
    assert "PARAM_SP," in files["ctrl_gen.h"] and "PARAM_COUNT" in files["ctrl_gen.h"]
    assert 'console_init("demo> ")' in files["ctrl_gen.c"]
    assert files["ctrl_gen.c"].index("rte_cmd_register_run") < files["ctrl_gen.c"].index('"status"')
    assert "struct demo_cal_page {" in files["ctrl_gen_task.inc"]
    assert 'XCP_UNIT(demo_cal_page__setpoint, "rpm");' in files["ctrl_gen_task.inc"]
    assert "  uint32_t request;" in files["ctrl_gen_task.inc"]
    assert "= {1.5f, 0.00072f, 0};" in files["ctrl_gen_task.inc"]
    assert "rte_calib_request(&s_calib, r, req);" in files["ctrl_gen_task.inc"]
    j = json.loads(files["ctrl_gen.json"])
    assert j["a2l"] == "docs/demo.a2l" and j["request"]["name"] == "demo_cal_page.request"
    assert j["params"][0]["cal"] == "demo_cal_page.setpoint" and j["params"][0]["unit"] == "rpm"
    assert j["status"] == {"running": "s_rte.run_applied", "trip": "s_trip"}
    assert j["monitor"] == ["s_loop.speed", "s_loop.temp"]
    j = json.loads({p.name: t for p, t in g.render(model(
        GOOD + '[xcp]\ntrip = "s_fault"\nmonitor = ["a.b"]\n')).items()}["ctrl_gen.json"])
    assert j["status"]["trip"] == "s_fault" and j["monitor"] == ["a.b"]
    expect_error(GOOD + "[xcp]\nmonitor = 1\n", "monitor must be a list")
    assert "lib_deps = ctrlcore, dcmotor" in files["platformio.ini"]
    assert "gen_controller.py . --check" in files["demo-controller.yml"]

    expect_error(GOOD.replace('lib = "democtrl"', 'lib = "democtrl"\nboard = "x"'),
                 "unknown key(s) board")
    expect_error(GOOD.replace("min = -10", "min = 20"), "min < max")
    expect_error(GOOD.replace("default = 1.5", "default = 11"), "min <= default <= max")
    expect_error(GOOD.replace('name = "kp"', 'name = "sp"'), "duplicate name 'sp'")
    expect_error(GOOD.replace('name = "status"', 'name = "Status"'), "lowercase C identifier")
    expect_error(GOOD.replace('plant = "dcmotor"', 'plant = "nope"'), "plant 'nope'")
    expect_error(GOOD.replace('lib = "democtrl"', 'lib = "missing"'), "lib 'missing'")
    expect_error(GOOD + '\n[[telemetry.column]]\nname = "a"\nvalue = "x"\n' * 3, "TELEM_VALUES")
    expect_error(GOOD.replace('cal = true', 'cal = "setpoint"'), "duplicate cal 'setpoint'")
    print("test_gen_controller: ok")


if __name__ == "__main__":
    main()
