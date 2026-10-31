"""Shared writer that generates a KiCad 10 schematic from part and net tables.

A board script (for example controllers/esp32-motor-controller/hardware/generate_schematic.py)
holds the tables, builds a Board and calls run(board, folder):

  python generate_schematic.py                  # write the board files into the script's folder
  python generate_schematic.py --check          # also print each net with the pins on it
  python generate_schematic.py --out some/dir   # write somewhere else (to compare, say)

Symbols are copied from the KiCad stock libraries (set KICAD_SYMBOL_DIR if KiCad is not in the
default Windows location) and from the Espressif KiCad libraries (installed through KiCad's
Plugin and Content Manager; set KICAD_3RD_PARTY_DIR if it is not under Documents). Signals are
connected with short wire stubs and net labels, power with power symbols.

The .kicad_pro file is only written when it does not exist, so the project settings KiCad stores
in it are never overwritten. UUIDs are derived from the board's namespace, so the output is
reproducible.
"""
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
    parts: list  # (ref, "Library:Symbol", value, x, y)
    connections: list  # (ref, pin name or number, net); a trailing "!" forces a plain label
    power: set  # nets drawn as power symbols
    footprints: dict = field(default_factory=dict)  # ref -> "Library:Footprint"
    no_connect: list = field(default_factory=list)  # (ref, pin)
    notes: list = field(default_factory=list)  # (x, y, text)
    devkit: str = "U1"  # ref of the ESP32 DevKit: every pin not wired gets a no-connect flag
    generator: str = "generate_schematic.py"


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


def load_symbol(lib_id):
    """Return the lib_symbols entry text for a stock 'Lib:Name' symbol."""
    lib, name = lib_id.split(":")
    path = SYMBOL_FILES.get(lib, os.path.join(SYMBOL_DIR, lib + ".kicad_sym"))
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r'^\t\(symbol "%s"\n' % re.escape(name), text, re.M)
    if not m:
        raise SystemExit(f"symbol {lib_id} not found")
    start = m.start() + 1
    block = text[start:balanced(text, start)]
    assert "(extends" not in block, f"{lib_id} extends another symbol"
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


class Sheet:
    def __init__(self, board):
        self.board = board
        self.parts = [(ref, lib_id, value, snap(x), snap(y))
                      for ref, lib_id, value, x, y in board.parts]
        self.placed = {ref: (lib_id, x, y) for ref, lib_id, _, x, y in self.parts}
        self.libs = {}
        self.items = []
        self.pwr = 0
        self.seq = 0
        self.nets = {}
        self.sheet_uuid = self.uid("sheet")

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

    def symbol(self, ref, lib_id, value, x, y, hidden=False):
        _, pins = self.lib(lib_id)
        h = " (hide yes)" if hidden else ""
        xs = [p[1] for p in pins.values()]
        ys = [p[2] for p in pins.values()]
        if len(set(xs)) == 1:  # vertical two-pin part: text to the right
            ref_at, val_at = (x + 2.54, y - 1.27), (x + 2.54, y + 1.27)
        else:  # text stacked above the part
            left = x + min(xs)
            ref_at, val_at = (left, y - max(ys) - 6.35), (left, y - max(ys) - 8.89)
        just = " (justify left)"
        props = [("Reference", ref, h, ref_at, just), ("Value", value, h, val_at, just),
                 ("Footprint", self.board.footprints.get(ref, ""), " (hide yes)", (x, y), ""),
                 ("Datasheet", "", " (hide yes)", (x, y), ""),
                 ("Description", "", " (hide yes)", (x, y), "")]
        body = "".join(f'\n\t\t(property "{k}" "{v}" (at {round(at[0], 2)} {round(at[1], 2)} 0) '
                       f'{effects(extra=hide + j)})' for k, v, hide, at, j in props)
        pin_ids = "".join(f'\n\t\t(pin "{n}" (uuid "{self.next_id("pin")}"))' for n in pins)
        self.items.append(
            f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {x} {y} 0)\n\t\t(unit 1)\n\t\t(body_style 1)\n'
            f'\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
            f'\t\t(uuid "{self.next_id("sym")}"){body}{pin_ids}\n'
            f'\t\t(instances (project "{self.board.name}" (path "/{self.sheet_uuid}" '
            f'(reference "{ref}") (unit 1))))\n\t)')

    def pin_point(self, ref, pin):
        lib_id, x, y = self.placed[ref]
        _, pins = self.lib(lib_id)
        if pin not in pins:  # match by name, ignoring ~{ } markup
            want = pin.strip("~{}")
            hits = [n for n, p in pins.items() if p[0].strip("~{}") == want]
            if len(hits) != 1:
                raise SystemExit(f"{ref}: pin {pin!r} not found or ambiguous")
            pin = hits[0]
        _, px, py, ang = pins[pin]
        return pin, round(x + px, 2), round(y - py, 2), OUTWARD[ang]

    def wire(self, a, b):
        self.items.append(
            f'\t(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) '
            f'(uuid "{self.next_id("wire")}"))')

    def label(self, net, pt, d):
        ang, just = {(-1, 0): (180, "right"), (1, 0): (0, "left"),
                     (0, -1): (90, "left"), (0, 1): (270, "right")}[d]
        self.items.append(
            f'\t(label "{net}" (at {pt[0]} {pt[1]} {ang}) (effects (font (size 1.27 1.27)) '
            f'(justify {just} bottom)) (uuid "{self.next_id("label")}"))')

    def connect(self, ref, pin, net):
        pin, px, py, d = self.pin_point(ref, pin)
        end = (round(px + d[0] * STUB, 2), round(py + d[1] * STUB, 2))
        self.wire((px, py), end)
        label_only = net.endswith("!")
        net = net.rstrip("!")
        if net in self.board.power and not label_only and d[0] == 0:  # power symbols: vertical stubs
            self.pwr += 1
            lib_id = f"power:{net}"
            self.symbol(f"#PWR{self.pwr:02d}", lib_id, net, end[0], end[1], hidden=True)
        else:
            self.label(net, end, d)
        self.nets.setdefault(net, []).append(f"{ref}.{pin}")

    def no_connect(self, ref, pin):
        _, px, py, _ = self.pin_point(ref, pin)
        self.items.append(f'\t(no_connect (at {px} {py}) (uuid "{self.next_id("nc")}"))')

    def text(self, x, y, s):
        self.items.append(
            f'\t(text "{s}" (exclude_from_sim no) (at {x} {y} 0) (effects (font (size 1.27 1.27)) '
            f'(justify left)) (uuid "{self.next_id("text")}"))')

    def render(self):
        libs = "\n".join(block for block, _ in self.libs.values())
        return (f'(kicad_sch\n\t(version 20260101)\n\t(generator "{self.board.generator}")\n'
                f'\t(generator_version "10.0")\n\t(uuid "{self.sheet_uuid}")\n\t(paper "A3")\n'
                f'\t(lib_symbols\n{libs}\n\t)\n' + "\n".join(self.items) + "\n)\n")


def generate(board, out_dir, check=False):
    """Write <name>.kicad_sch and the Espressif library tables into out_dir."""
    s = Sheet(board)
    for ref, lib_id, value, x, y in s.parts:
        s.symbol(ref, lib_id, value, x, y, hidden=ref.startswith("#"))
    for ref, pin, net in board.connections:
        s.connect(ref, pin, net)
    for ref, pin in board.no_connect:
        s.no_connect(ref, pin)
    # Every DevKit pin not wired above gets a no-connect flag (pins sharing a point with a wired
    # pin, such as the extra GND pads, are already connected).
    dev = board.devkit
    wired = {s.pin_point(dev, pin)[1:3] for ref, pin, _ in board.connections if ref == dev}
    for num in s.lib(s.placed[dev][0])[1]:
        if s.pin_point(dev, num)[1:3] not in wired:
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
        for net, pins in sorted(s.nets.items()):
            print(f"{net:12s} {' '.join(pins)}")


def run(board, here, argv=None):
    """Entry point for a board script: handles --check and --out DIR, defaulting to `here`."""
    argv = sys.argv[1:] if argv is None else argv
    out = here
    if "--out" in argv:
        out = argv[argv.index("--out") + 1]
    generate(board, out, check="--check" in argv)
