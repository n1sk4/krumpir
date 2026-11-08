"""Shared writer that generates a KiCad 10 schematic from part and net tables.

A board script (for example controllers/esp32-motor-controller/hardware/generate_schematic.py)
holds the tables, builds a Board and calls run(board, folder):

  python generate_schematic.py                  # write the board files into the script's folder
  python generate_schematic.py --check          # also print each net with the pins on it
  python generate_schematic.py --out some/dir   # write somewhere else (to compare, say)

Symbols are copied from the KiCad stock libraries (set KICAD_SYMBOL_DIR if KiCad is not in the
default Windows location) and from the Espressif KiCad libraries (installed through KiCad's
Plugin and Content Manager; set KICAD_3RD_PARTY_DIR if it is not under Documents).

Two ways to connect pins, which can be mixed:
  connections  (ref, pin, net): a short wire stub ending in a net label or a power symbol. A
               trailing "!" forces a plain label; a trailing ">" lets a power symbol hang below
               (GND) or above (other nets) a sideways stub.
  wires        polylines of pins ("REF.PIN") and points (x, y); between two points that are not in
               line the wire bends horizontally first. Junction dots are added where three or more
               wire ends meet.
  names        (("REF.PIN"), net): a net label right at a pin of a wired net.

Parts are (ref, "Library:Symbol", value, x, y[, rotation[, anchor pin]]). The rotation is 0, 90,
180 or 270 degrees counterclockwise. Without an anchor pin x and y are the part origin, snapped to
the 2.54 mm grid; with one, the part is placed so that this pin lands exactly on (x, y).

The .kicad_pro file is only written when it does not exist, so the project settings KiCad stores
in it are never overwritten. UUIDs are derived from the board's namespace, so the output is
reproducible. --check prints the nets as the drawn geometry connects them, as KiCad sees them.
"""
import argparse
import os
import re
import sys
import uuid
from dataclasses import dataclass, field

SYMBOL_DIR = os.environ.get("KICAD_SYMBOL_DIR", r"C:\Program Files\KiCad\10.0\share\kicad\symbols")
THIRD_PARTY = os.environ.get(
    "KICAD_3RD_PARTY_DIR", os.path.expanduser(r"~\Documents\KiCad\10.0\3rdparty"))
ESPRESSIF = "com_github_espressif_kicad-libraries"
SYMBOL_FILES = {"Espressif": os.path.join(THIRD_PARTY, "symbols", ESPRESSIF, "Espressif.kicad_sym")}
STUB = 5.08

# Direction a pin leads away from its body, in schematic coordinates (y grows downward),
# keyed by the pin angle in the symbol library.
OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}


@dataclass
class Board:
    name: str  # file name stem and KiCad project name
    ns: uuid.UUID  # namespace for the reproducible UUIDs
    parts: list  # (ref, "Library:Symbol", value, x, y[, rotation[, anchor pin]])
    connections: list  # (ref, pin name or number, net); "!" and ">" suffixes, see above
    power: set  # nets drawn as power symbols
    footprints: dict = field(default_factory=dict)  # ref -> "Library:Footprint"
    no_connect: list = field(default_factory=list)  # (ref, pin)
    notes: list = field(default_factory=list)  # (x, y, text)
    devkit: str = "U1"  # ref of the ESP32 DevKit: every pin not wired gets a no-connect flag
    generator: str = "generate_schematic.py"
    wires: list = field(default_factory=list)  # polylines of "REF.PIN" and (x, y)
    names: list = field(default_factory=list)  # ("REF.PIN", net) or ((x, y), dir, net)
    power_at: list = field(default_factory=list)  # (net, (x, y)): a power symbol on a wire
    text_at: dict = field(default_factory=dict)  # ref -> (dx, dy) of the reference text


def balanced(text, start):
    """Return the end index (exclusive) of the S-expression starting at text[start] == '('."""
    depth, i, in_str = 0, start, False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced")


def raw_symbol(lib, name):
    """Return the text of one symbol as it is stored in its library file."""
    path = SYMBOL_FILES.get(lib, os.path.join(SYMBOL_DIR, lib + ".kicad_sym"))
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r'^\t\(symbol "%s"\n' % re.escape(name), text, re.M)
    if not m:
        raise SystemExit(f"symbol {lib}:{name} not found")
    start = m.start() + 1
    return text[start:balanced(text, start)]


def property_blocks(block):
    """Map property name -> its full (property ...) text inside a symbol block."""
    return {m.group(1): block[m.start():balanced(block, m.start())]
            for m in re.finditer(r'\(property "([^"]+)"', block)}


def load_symbol(lib_id):
    """Return the lib_symbols entry text for a stock 'Lib:Name' symbol.

    A derived symbol ("extends") is flattened the way KiCad embeds it in a schematic: the parent's
    drawing and pins under the child's name, with the child's own property values."""
    lib, name = lib_id.split(":")
    block = raw_symbol(lib, name)
    ext = re.search(r'\(extends "([^"]+)"\)', block)
    if ext:
        parent = ext.group(1)
        merged = raw_symbol(lib, parent)
        assert "(extends" not in merged, f"{lib_id}: more than one level of extends"
        merged = merged.replace(f'(symbol "{parent}_', f'(symbol "{name}_')
        merged = merged.replace(f'(symbol "{parent}"', f'(symbol "{name}"', 1)
        for key, text in property_blocks(block).items():  # the child's own properties win
            have = property_blocks(merged)
            if key in have:
                merged = merged.replace(have[key], text, 1)
            else:
                last = list(have.values())[-1]
                merged = merged.replace(last, last + "\n\t\t" + text, 1)
        block = merged
    return block.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)


def effects(size=1.27, extra=""):
    return f"(effects (font (size {size} {size})){extra})"


def parse_pins(block):
    """Map pin number -> (name, x, y, angle) from a symbol block."""
    pins = {}
    for chunk in block.split("(pin ")[1:]:
        m = re.search(r"\(at\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(\d+)\)", chunk)
        name = re.search(r'\(name\s+"((?:[^"\\]|\\.)*)"', chunk).group(1)
        num = re.search(r'\(number\s+"([^"]+)"', chunk).group(1)
        pins[num] = (name, float(m.group(1)), float(m.group(2)), int(m.group(3)))
    return pins


def snap(v):
    """Put part origins on the 2.54 mm grid so pins land on the connection grid."""
    return round(round(v / 2.54) * 2.54, 2)


def rotate(x, y, rot):
    """Rotate a point of the symbol library (y up) counterclockwise by rot degrees."""
    return {0: (x, y), 90: (-y, x), 180: (-x, -y), 270: (y, -x)}[rot % 360]


def key(pt):
    return (round(pt[0], 2), round(pt[1], 2))


def on_segment(p, a, b):
    """True when p lies on the axis-aligned segment a-b (ends included)."""
    if abs(a[0] - b[0]) < 0.01 and abs(p[0] - a[0]) < 0.01:
        return min(a[1], b[1]) - 0.01 <= p[1] <= max(a[1], b[1]) + 0.01
    if abs(a[1] - b[1]) < 0.01 and abs(p[1] - a[1]) < 0.01:
        return min(a[0], b[0]) - 0.01 <= p[0] <= max(a[0], b[0]) + 0.01
    return False


class Sheet:
    def __init__(self, board):
        self.board = board
        self.libs = {}
        self.items = []
        self.pwr = 0
        self.seq = 0
        self.segs = []  # every wire, stubs included
        self.drawn = []  # wires of the polylines only
        self.net_points = []  # (point, net name) for labels and power symbols
        self.extra_pins = set()  # pins of power symbols placed on a wire
        self.sheet_uuid = self.uid("sheet")
        self.parts = []
        self.placed = {}
        for ref, lib_id, value, x, y, *rest in board.parts:
            rot = rest[0] if rest else 0
            if len(rest) > 1:  # anchored: this pin lands on (x, y)
                pins = self.lib(lib_id)[1]
                _, px, py, _ = pins[self.resolve_pin(pins, rest[1])]
                dx, dy = rotate(px, py, rot)
                x, y = round(x - dx, 2), round(y + dy, 2)
            else:
                x, y = snap(x), snap(y)
            self.parts.append((ref, lib_id, value, x, y, rot))
            self.placed[ref] = (lib_id, x, y, rot)

    def uid(self, key):
        return str(uuid.uuid5(self.board.ns, key))

    def lib(self, lib_id):
        if lib_id not in self.libs:
            block = load_symbol(lib_id)
            self.libs[lib_id] = (block, parse_pins(block))
        return self.libs[lib_id]

    def next_id(self, kind):
        self.seq += 1
        return self.uid(f"{kind}{self.seq}")

    @staticmethod
    def resolve_pin(pins, pin):
        if pin in pins:
            return pin
        want = pin.strip("~{}")  # match by name, ignoring ~{ } markup
        hits = [n for n, p in pins.items() if p[0].strip("~{}") == want]
        if len(hits) != 1:
            raise SystemExit(f"pin {pin!r} not found or ambiguous")
        return hits[0]

    def symbol(self, ref, lib_id, value, x, y, rot=0, hidden=False):
        _, pins = self.lib(lib_id)
        h = " (hide yes)" if hidden else ""
        turned = [rotate(p[1], p[2], rot) for p in pins.values()]
        xs = [p[0] for p in turned]
        ys = [p[1] for p in turned]
        if len(set(xs)) == 1:  # vertical two-pin part: text to the right
            ref_at, val_at = (x + 2.54, y - 1.27), (x + 2.54, y + 1.27)
        else:  # text stacked above the part
            left = x + min(xs)
            ref_at, val_at = (left, y - max(ys) - 6.35), (left, y - max(ys) - 8.89)
        if ref in self.board.text_at:  # explicit text position: reference here, value below it
            tx, ty = self.board.text_at[ref]
            ref_at, val_at = (x + tx, y + ty), (x + tx, y + ty + 2.54)
        just = " (justify left)"
        props = [("Reference", ref, h, ref_at, just), ("Value", value, h, val_at, just),
                 ("Footprint", self.board.footprints.get(ref, ""), " (hide yes)", (x, y), ""),
                 ("Datasheet", "", " (hide yes)", (x, y), ""),
                 ("Description", "", " (hide yes)", (x, y), "")]
        text_angle = {0: 0, 90: 270, 180: 0, 270: 90}[rot % 360]  # KiCad adds the part rotation
        body = "".join(f'\n\t\t(property "{k}" "{v}" (at {round(at[0], 2)} {round(at[1], 2)} '
                       f'{text_angle}) {effects(extra=hide + j)})' for k, v, hide, at, j in props)
        pin_ids = "".join(f'\n\t\t(pin "{n}" (uuid "{self.next_id("pin")}"))' for n in pins)
        self.items.append(
            f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {x} {y} {rot})\n\t\t(unit 1)\n'
            f'\t\t(body_style 1)\n'
            f'\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
            f'\t\t(uuid "{self.next_id("sym")}"){body}{pin_ids}\n'
            f'\t\t(instances (project "{self.board.name}" (path "/{self.sheet_uuid}" '
            f'(reference "{ref}") (unit 1))))\n\t)')

    def pin_point(self, ref, pin):
        lib_id, x, y, rot = self.placed[ref]
        _, pins = self.lib(lib_id)
        pin = self.resolve_pin(pins, pin)
        _, px, py, ang = pins[pin]
        dx, dy = rotate(px, py, rot)
        return pin, round(x + dx, 2), round(y - dy, 2), OUTWARD[(ang + rot) % 360]

    def wire(self, a, b):
        self.segs.append((a, b))
        self.items.append(
            f'\t(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) '
            f'(uuid "{self.next_id("wire")}"))')

    def label(self, net, pt, d):
        ang, just = {(-1, 0): (180, "right"), (1, 0): (0, "left"),
                     (0, -1): (90, "left"), (0, 1): (270, "right")}[d]
        self.net_points.append((pt, net))
        self.items.append(
            f'\t(label "{net}" (at {pt[0]} {pt[1]} {ang}) (effects (font (size 1.27 1.27)) '
            f'(justify {just} bottom)) (uuid "{self.next_id("label")}"))')

    def connect(self, ref, pin, net):
        pin, px, py, d = self.pin_point(ref, pin)
        end = (round(px + d[0] * STUB, 2), round(py + d[1] * STUB, 2))
        self.wire((px, py), end)
        label_only = net.endswith("!")
        turn = net.endswith(">")
        net = net.rstrip("!>")
        if net in self.board.power and not label_only and (d[0] == 0 or turn):
            if d[0] != 0:  # sideways stub: the power symbol hangs from its end
                corner = end
                end = (corner[0], round(corner[1] + (STUB if net == "GND" else -STUB), 2))
                self.wire(corner, end)
            self.pwr += 1
            lib_id = f"power:{net}"
            self.net_points.append((end, net))
            self.symbol(f"#PWR{self.pwr:02d}", lib_id, net, end[0], end[1], hidden=True)
        else:
            self.label(net, end, d)

    def point(self, e):
        """A polyline element: "REF.PIN" or an (x, y) point."""
        if isinstance(e, str):
            ref, pin = e.rsplit(".", 1)
            _, px, py, _ = self.pin_point(ref, pin)
            return (px, py)
        return (round(float(e[0]), 2), round(float(e[1]), 2))

    def draw(self, elements):
        pts = [self.point(e) for e in elements]
        path = [pts[0]]
        for p in pts[1:]:
            q = path[-1]
            if abs(p[0] - q[0]) > 0.005 and abs(p[1] - q[1]) > 0.005:
                path.append((p[0], q[1]))  # bend: horizontal first
            path.append(p)
        for a, b in zip(path, path[1:]):
            if key(a) != key(b):
                self.wire(a, b)
                self.drawn.append((a, b))

    def name_pin(self, where, *rest):
        """A label at a pin: name_pin("REF.PIN", net), or at a point: name_pin((x, y), dir, net)
        with dir one of "left", "right", "up", "down" (the way the text runs)."""
        if isinstance(where, str):
            ref, num = where.rsplit(".", 1)
            _, px, py, d = self.pin_point(ref, num)
            self.label(rest[0], (px, py), d)
        else:
            direction, net = rest
            d = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1)}[direction]
            self.label(net, (round(where[0], 2), round(where[1], 2)), d)

    def power_at(self, net, pt):
        """A power symbol whose pin sits on pt."""
        pt = (round(pt[0], 2), round(pt[1], 2))
        self.pwr += 1
        self.net_points.append((pt, net))
        self.extra_pins.add(key(pt))
        self.symbol(f"#PWR{self.pwr:02d}", f"power:{net}", net, pt[0], pt[1], hidden=True)

    def junctions(self):
        pins = {key(self.pin_point(ref, p)[1:3]) for ref in self.placed
                for p in self.lib(self.placed[ref][0])[1]} | self.extra_pins
        ends = {}
        for a, b in self.drawn:
            for p in (key(a), key(b)):
                ends[p] = ends.get(p, 0) + 1
        for p in sorted(set(ends) | pins):
            n = ends.get(p, 0)
            through = sum(1 for a, b in self.drawn
                          if on_segment(p, a, b) and p not in (key(a), key(b)))
            if n + (1 if p in pins else 0) + 2 * through >= 3:
                self.items.append(
                    f'\t(junction (at {p[0]} {p[1]}) (diameter 0) (color 0 0 0 0) '
                    f'(uuid "{self.next_id("junction")}"))')

    def touched(self, pt):
        """True when a wire or label connects at pt."""
        pt = key(pt)
        if any(key(p) == pt for p, _ in self.net_points):
            return True
        return any(on_segment(pt, a, b) for a, b in self.segs)

    def no_connect(self, ref, pin):
        _, px, py, _ = self.pin_point(ref, pin)
        self.items.append(f'\t(no_connect (at {px} {py}) (uuid "{self.next_id("nc")}"))')

    def text(self, x, y, s):
        self.items.append(
            f'\t(text "{s}" (exclude_from_sim no) (at {x} {y} 0) (effects (font (size 1.27 1.27)) '
            f'(justify left)) (uuid "{self.next_id("text")}"))')

    def netlist(self):
        """Nets as the drawn geometry connects them: name -> sorted pins (power symbols left out)."""
        parent = {}

        def find(a):
            parent.setdefault(a, a)
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        def union(a, b):
            parent[find(a)] = find(b)

        pin_at = {}
        for ref in self.placed:
            for p in self.lib(self.placed[ref][0])[1]:
                pin_at.setdefault(key(self.pin_point(ref, p)[1:3]), []).append(f"{ref}.{p}")
        nodes = set(pin_at) | {key(p) for p, _ in self.net_points}
        for a, b in self.segs:
            nodes |= {key(a), key(b)}
            union(key(a), key(b))
        for n in nodes:
            for a, b in self.segs:
                if on_segment(n, a, b):
                    union(n, key(a))
        by_name = {}
        for p, net in self.net_points:
            union(key(p), by_name.setdefault(net, key(p)))
        groups = {}
        for n in nodes:
            groups.setdefault(find(n), set())
        for p, pins in pin_at.items():
            groups[find(p)].update(x for x in pins if not x.startswith("#PWR"))
        names = {find(key(p)): net for p, net in self.net_points}
        nets = {}
        for root, pins in groups.items():
            if root in names:
                nets[names[root]] = sorted(pins, key=lambda s: (s.split(".")[0], int(s.split(".")[1])
                                                                 if s.split(".")[1].isdigit() else 0,
                                                                 s))
            elif len(pins) > 1:
                ordered = sorted(pins)
                nets[f"Net-({ordered[0]})"] = ordered
        return nets

    def render(self):
        libs = "\n".join(block for block, _ in self.libs.values())
        return (f'(kicad_sch\n\t(version 20260101)\n\t(generator "{self.board.generator}")\n'
                f'\t(generator_version "10.0")\n\t(uuid "{self.sheet_uuid}")\n\t(paper "A3")\n'
                f'\t(lib_symbols\n{libs}\n\t)\n' + "\n".join(self.items) + "\n)\n")


def generate(board, out_dir, check=False):
    """Write <name>.kicad_sch and the Espressif library tables into out_dir."""
    s = Sheet(board)
    for ref, lib_id, value, x, y, rot in s.parts:
        s.symbol(ref, lib_id, value, x, y, rot, hidden=ref.startswith("#"))
    for ref, pin, net in board.connections:
        s.connect(ref, pin, net)
    for poly in board.wires:
        s.draw(poly)
    for entry in board.names:
        s.name_pin(*entry)
    for net, pt in board.power_at:
        s.power_at(net, pt)
    s.junctions()
    for ref, pin in board.no_connect:
        s.no_connect(ref, pin)
    # Every DevKit pin nothing connects to gets a no-connect flag (pins sharing a point with a
    # wired pin, such as the extra GND pads, are already connected).
    dev = board.devkit
    for num in s.lib(s.placed[dev][0])[1]:
        if not s.touched(s.pin_point(dev, num)[1:3]):
            s.no_connect(dev, num)
    for x, y, t in board.notes:
        s.text(x, y, t)

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, board.name + ".kicad_sch"), "w", encoding="utf-8",
              newline="\n") as f:
        f.write(s.render())
    # Project library tables for the Espressif libraries (installed through the KiCad PCM).
    third = "${KICAD10_3RD_PARTY}"
    with open(os.path.join(out_dir, "sym-lib-table"), "w", encoding="utf-8", newline="\n") as f:
        f.write('(sym_lib_table\n  (version 7)\n  (lib (name "Espressif")(type "KiCad")'
                f'(uri "{third}/symbols/{ESPRESSIF}/Espressif.kicad_sym")(options "")'
                '(descr "Espressif modules and DevKits"))\n)\n')
    with open(os.path.join(out_dir, "fp-lib-table"), "w", encoding="utf-8", newline="\n") as f:
        f.write('(fp_lib_table\n  (version 7)\n  (lib (name "Espressif")(type "KiCad")'
                f'(uri "{third}/footprints/{ESPRESSIF}/Espressif.pretty")(options "")'
                '(descr "Espressif modules and DevKits"))\n)\n')
    pro = os.path.join(out_dir, board.name + ".kicad_pro")
    if not os.path.exists(pro):  # KiCad keeps its own settings in here: never overwrite them
        with open(pro, "w", encoding="utf-8", newline="\n") as f:
            f.write('{\n  "meta": {\n    "filename": "%s.kicad_pro",\n    "version": 3\n  }\n}\n'
                    % board.name)
    if check:
        for net, pins in sorted(s.netlist().items()):
            print(f"{net:12s} {' '.join(pins)}")


def run(board, here, argv=None):
    """Entry point for a board script: --check, --out DIR (default `here`), and --help with the
    script's docstring. Exit status: 0 ok, 1 a KiCad library or the folder is missing, 2 usage."""
    import __main__
    ap = argparse.ArgumentParser(description=__main__.__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="also print each net with its pins")
    ap.add_argument("--out", metavar="DIR", default=here, help="write into DIR (default: here)")
    a = ap.parse_args(argv)
    try:
        generate(board, a.out, check=a.check)
    except FileNotFoundError as e:
        sys.exit(f"kicadgen: {e.filename} not found (KiCad's libraries: KICAD_SYMBOL_DIR, "
                 "KICAD_3RD_PARTY_DIR, tools/kicad/kicadgen.py)")
    return 0
