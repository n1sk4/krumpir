#!/usr/bin/env python3
"""Generates a controller's runtime tables and build files from its controller.toml.

    python tools/codegen/gen_controller.py controllers/esp32-motor-controller          # write
    python tools/codegen/gen_controller.py controllers/esp32-motor-controller --check  # CI

The description holds the data: parameters (console commands, limits, defaults, calibration page),
the controller's own console commands, telemetry columns and the controller-specific CI steps. The
generator writes the glue around the hand-written component (AUTOSAR terms: the RTE tables and the
build configuration); hand-written code is never touched. Generated files are committed; --check
fails when they differ from what the description gives.
"""
import argparse
import difflib
import json
import re
import sys
import tomllib
from pathlib import Path

import jinja2

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = Path(__file__).resolve().parent / "templates"
RTE_MAX_PARAMS = 16

# template -> output path ({root}: repository, otherwise relative to the controller), comment style
OUTPUTS = [
    ("ctrl_gen.h.j2", "src/gen/ctrl_gen.h", "c"),
    ("ctrl_gen.c.j2", "src/gen/ctrl_gen.c", "c"),
    ("ctrl_gen_task.inc.j2", "src/gen/ctrl_gen_task.inc", "c"),
    ("platformio.ini.j2", "platformio.ini", ";"),
    ("CMakeLists.txt.j2", "CMakeLists.txt", "#"),
    ("src_CMakeLists.txt.j2", "src/CMakeLists.txt", "#"),
    ("test_CMakeLists.txt.j2", "test/CMakeLists.txt", "#"),
    ("sdkconfig.defaults.j2", "sdkconfig.defaults", "#"),
    ("workflow.yml.j2", "{root}/.github/workflows/{dir}.yml", "#"),
    ("host_CMakeLists.txt.j2", "host/CMakeLists.txt", "#"),  # only with [hil]
]
HIL_MAX_VALUES = 16  # libs/hil/include/hil.h

IDENT = re.compile(r"[a-z_][a-z0-9_]*$")
A2L_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$")


class DescriptionError(Exception):
    pass


def _fail(where, msg):
    raise DescriptionError(f"{where}: {msg}")


def _table(t, where, required, optional=()):
    if not isinstance(t, dict):
        _fail(where, "must be a table")
    unknown = set(t) - set(required) - set(optional)
    if unknown:
        _fail(where, f"unknown key(s) {', '.join(sorted(unknown))}")
    missing = [k for k in required if k not in t]
    if missing:
        _fail(where, f"missing key(s) {', '.join(missing)}")


def _str(t, key, where, ident=False):
    v = t[key]
    if not isinstance(v, str) or not v:
        _fail(where, f"{key} must be a non-empty string")
    if ident and not IDENT.match(v):
        _fail(where, f"{key} '{v}' must be a lowercase C identifier")
    return v


def _num(t, key, where, default=None):
    v = t.get(key, default)
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        _fail(where, f"{key} must be a number")
    return float(v)


def _unique(items, key, where):
    seen = set()
    for it in items:
        if it[key] in seen:
            _fail(where, f"duplicate {key} '{it[key]}'")
        seen.add(it[key])


def telem_capacity():
    text = (ROOT / "libs/ctrlcore/include/ring_buf.h").read_text()
    return int(re.search(r"#define TELEM_VALUES (\d+)", text).group(1))


def load(ctrl_dir: Path) -> dict:
    """Reads and checks controller.toml; returns the model the templates render."""
    path = ctrl_dir / "controller.toml"
    try:
        with open(path, "rb") as f:
            d = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise DescriptionError(f"{path}: {e}") from None
    _table(d, "controller.toml", ["controller", "param", "telemetry"],
           ["command", "ci", "xcp", "hil"])

    c = d["controller"]
    _table(c, "[controller]", ["name", "plant", "lib"])
    name = _str(c, "name", "[controller]", ident=True)
    plant = _str(c, "plant", "[controller]")
    lib = _str(c, "lib", "[controller]")
    if not (ctrl_dir / "lib" / lib).is_dir():
        _fail("[controller]", f"lib '{lib}' is not a folder in {ctrl_dir / 'lib'}")
    if not (ROOT / "libs/plants" / plant).is_dir():
        _fail("[controller]", f"plant '{plant}' is not a folder in libs/plants")

    params = []
    for i, p in enumerate(d["param"]):
        w = f"[[param]] #{i + 1}"
        _table(p, w, ["name", "help", "hint", "min", "max", "default"], ["unit", "cal"])
        q = {k: _str(p, k, w, ident=(k == "name")) for k in ("name", "help", "hint")}
        q.update({k: _num(p, k, w) for k in ("min", "max", "default")})
        if not q["min"] < q["max"] or not q["min"] <= q["default"] <= q["max"]:
            _fail(w, "needs min < max and min <= default <= max")
        q["unit"] = _str(p, "unit", w) if "unit" in p else None
        cal = p.get("cal", False)
        if not isinstance(cal, (bool, str)) or (isinstance(cal, str) and not IDENT.match(cal)):
            _fail(w, "cal must be true/false or the calibration field name")
        q["cal"] = (q["name"] if cal is True else cal) or None
        params.append(q)
    if not params or len(params) > RTE_MAX_PARAMS:
        _fail("[[param]]", f"needs 1 to {RTE_MAX_PARAMS} parameters")
    _unique(params, "name", "[[param]]")
    _unique([p for p in params if p["cal"]], "cal", "[[param]] cal")

    commands = []
    for i, k in enumerate(d.get("command", [])):
        w = f"[[command]] #{i + 1}"
        _table(k, w, ["name", "help", "handler"], ["hint", "after_run"])
        q = {k2: _str(k, k2, w, ident=(k2 != "help")) for k2 in ("name", "help", "handler")}
        q["hint"] = _str(k, "hint", w) if "hint" in k else None
        q["after_run"] = k.get("after_run", False)
        if not isinstance(q["after_run"], bool):
            _fail(w, "after_run must be true or false")
        commands.append(q)
    _unique(params + commands, "name", "[[param]] and [[command]]")

    t = d["telemetry"]
    _table(t, "[telemetry]", ["column"])
    columns = []
    for i, col in enumerate(t["column"]):
        w = f"[[telemetry.column]] #{i + 1}"
        _table(col, w, ["name", "value"], ["scale", "nan"])
        q = {"name": _str(col, "name", w), "value": _str(col, "value", w)}
        q["scale"] = _num(col, "scale", w, 1)
        q["nan"] = col.get("nan")
        if q["nan"] is not None and (isinstance(q["nan"], bool) or not isinstance(q["nan"], int)):
            _fail(w, "nan must be an integer (the CSV value printed for NaN)")
        columns.append(q)
    if not columns or len(columns) > telem_capacity():
        _fail("[[telemetry.column]]", f"needs 1 to {telem_capacity()} columns (TELEM_VALUES)")
    for i, q in enumerate(columns):  # the C expression that prints column i of a sample
        v = f"s->v[{i}]" + ("" if q["scale"] == 1 else f" * {c_float(q['scale'])}")
        r = f"telemetry_round({v})"
        q["row"] = r if q["nan"] is None else f"isfinite(s->v[{i}]) ? {r} : {q['nan']}"

    steps = []
    for i, s in enumerate(d.get("ci", {}).get("step", [])):
        w = f"[[ci.step]] #{i + 1}"
        _table(s, w, ["name", "run"])
        steps.append({"name": _str(s, "name", w), "run": _str(s, "run", w).rstrip("\n")})
    if "ci" in d:
        _table(d["ci"], "[ci]", [], ["step"])

    # What a tool shows as the controller's state, as A2L names in the control task's unit
    x = d.get("xcp", {})
    _table(x, "[xcp]", [], ["running", "trip", "monitor"])
    xcp = {"running": _str(x, "running", "[xcp]") if "running" in x else "s_rte.run_applied",
           "trip": _str(x, "trip", "[xcp]") if "trip" in x else "s_trip"}
    if "monitor" in x:
        if not isinstance(x["monitor"], list) or not all(isinstance(s, str) and s
                                                         for s in x["monitor"]):
            _fail("[xcp]", "monitor must be a list of names")
        xcp["monitor"] = x["monitor"]
    else:  # the telemetry columns that are plain variables
        xcp["monitor"] = [q["value"] for q in columns if A2L_NAME.match(q["value"])]

    # The HIL link's values: the plant values the runner sends, the commands it reads back
    hil = None
    if "hil" in d:
        h = d["hil"]
        _table(h, "[hil]", ["plant", "cmd"])
        for k in ("plant", "cmd"):
            v = h[k]
            if (not isinstance(v, list) or not 1 <= len(v) <= HIL_MAX_VALUES
                    or not all(isinstance(s, str) and s for s in v) or len(set(v)) != len(v)):
                _fail("[hil]", f"{k} must be 1 to {HIL_MAX_VALUES} distinct names")
        hil = {"plant": h["plant"], "cmd": h["cmd"]}

    return {
        "name": name,
        "dir": ctrl_dir.name,
        "hil": hil,
        "plant": plant,
        "lib": lib,
        "params": params,
        "cal": [p for p in params if p["cal"]],
        "commands": commands,
        "telemetry": columns,
        "ci_steps": steps,
        "xcp": xcp,
    }


def c_float(v):
    """A C float literal for v, written so it parses to the same float as the description."""
    s = repr(float(v))
    return s + "f" if ("." in s or "e" in s) else s + ".0f"


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"' if s is not None else "NULL"


def banner(style, ctrl_dir):
    text = (f"Generated by tools/codegen/gen_controller.py from {ctrl_dir}/controller.toml. "
            "Do not edit; change the description and regenerate.")
    if style == "c":
        return f"/* {text} */"
    return f"{style} {text}"


def render(model: dict) -> dict:
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(str(TEMPLATES)),
                             undefined=jinja2.StrictUndefined, trim_blocks=True,
                             lstrip_blocks=True, keep_trailing_newline=True)
    env.filters["c_float"] = c_float
    env.filters["c_string"] = c_string
    ctrl_rel = f"controllers/{model['dir']}"
    out = {}
    for template, target, style in OUTPUTS:
        if template.startswith("host_") and not model["hil"]:
            continue
        text = env.get_template(template).render(m=model, banner=banner(style, ctrl_rel))
        rel = target.format(root="", dir=model["dir"])
        path = ROOT / rel.lstrip("/") if target.startswith("{root}") else ROOT / ctrl_rel / rel
        out[path] = text
    out[ROOT / ctrl_rel / "src/gen/ctrl_gen.json"] = describe(model, ctrl_rel)
    return out


def describe(model, ctrl_rel):
    """The controller for tools (the TUI): what the A2L alone does not say, as JSON."""
    page = f"{model['name']}_cal_page"
    d = {
        "generated": banner("", ctrl_rel).strip(),
        "name": model["name"],
        "a2l": f"docs/{model['name']}.a2l",
        "params": [{"name": p["name"], "help": p["help"], "unit": p["unit"], "min": p["min"],
                    "max": p["max"], "default": p["default"],
                    "cal": f"{page}.{p['cal']}" if p["cal"] else None} for p in model["params"]],
        "request": ({"name": f"{page}.request", "start": 1, "stop": 2, "clear": 3}
                    if model["cal"] else None),
        "status": {"running": model["xcp"]["running"], "trip": model["xcp"]["trip"]},
        "monitor": model["xcp"]["monitor"],
        "hil": model["hil"],
        # the PC build's A2L (host/, -DKRUMPIR_XCP=ON), when the controller has a HIL link
        "a2l_host": f"docs/{model['name']}_host.a2l" if model["hil"] else None,
    }
    return json.dumps(d, indent=2) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("controller", help="controller folder (holds controller.toml)")
    ap.add_argument("--check", action="store_true", help="fail if a generated file is outdated")
    args = ap.parse_args()

    ctrl_dir = Path(args.controller).resolve()
    try:
        files = render(load(ctrl_dir))
    except DescriptionError as e:
        sys.exit(f"gen_controller: {e}")

    stale = []
    for path, text in files.items():
        old = path.read_text(encoding="utf-8") if path.exists() else None
        if old == text:
            continue
        if args.check:
            stale.append(path)
            diff = difflib.unified_diff((old or "").splitlines(True), text.splitlines(True),
                                        str(path.relative_to(ROOT)), "generated", n=1)
            sys.stdout.writelines(list(diff)[:40])
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT)}")
    if stale:
        sys.exit(f"gen_controller: {len(stale)} generated file(s) out of date; run "
                 f"python tools/codegen/gen_controller.py {args.controller}")
    if args.check:
        print(f"gen_controller: {ctrl_dir.name} up to date ({len(files)} files)")


if __name__ == "__main__":
    main()
