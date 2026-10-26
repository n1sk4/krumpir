"""Generate motor-controller.kicad_sch (KiCad 10) from the part and net tables below.

Usage (from this folder):
  python generate_schematic.py            # write motor-controller.kicad_sch and .kicad_pro
  python generate_schematic.py --check    # also print each net with the pins on it

Symbols are copied from the KiCad stock libraries (set KICAD_SYMBOL_DIR if KiCad is not in the
default Windows location) and from the Espressif KiCad libraries (installed through KiCad's
Plugin and Content Manager; set KICAD_3RD_PARTY_DIR if it is not under Documents). Signals are
connected with short wire stubs and net labels, power with power symbols. Edit the tables, rerun,
then export with kicad-cli (see README.md).
"""
import os
import re
import sys
import uuid

NAME = "motor-controller"
SYMBOL_DIR = os.environ.get("KICAD_SYMBOL_DIR", r"C:\Program Files\KiCad\10.0\share\kicad\symbols")
THIRD_PARTY = os.environ.get(
    "KICAD_3RD_PARTY_DIR", os.path.expanduser(r"~\Documents\KiCad\10.0\3rdparty"))
ESPRESSIF = "com_github_espressif_kicad-libraries"
SYMBOL_FILES = {"Espressif": os.path.join(THIRD_PARTY, "symbols", ESPRESSIF, "Espressif.kicad_sym")}
NS = uuid.UUID("6d6f746f-722d-4374-726c-000000000001")
STUB = 5.08
POWER = {"GND", "+3V3", "+6V"}

# ref, library:symbol, value, x, y
PARTS = [
    ("U1", "Espressif:ESP32-DevKitC", "ESP32-DevKitC", 139.7, 114.3),
    ("U2", "Driver_Motor:DRV8833PW", "DRV8833", 254.0, 116.84),
    ("J1", "Connector_Generic:Conn_01x02", "6V in", 40.0, 220.0),
    ("J2", "Connector_Generic:Conn_01x06", "N20 motor + encoder", 350.52, 116.84),
    ("J3", "Connector_Generic:Conn_01x02", "GPIO23 loop timing", 50.8, 66.0),
    ("J4", "Connector_Generic:Conn_01x04", "Krumpir (Raspberry Pi)", 50.8, 101.6),
    ("C1", "Device:C_Polarized", "100u", 80.0, 220.0),
    ("C2", "Device:C", "100n", 100.0, 220.0),
    ("C3", "Device:C", "10n", 120.0, 220.0),
    ("C4", "Device:C", "2.2u", 140.0, 220.0),
    ("R1", "Device:R", "10k", 170.0, 220.0),
    ("R2", "Device:R", "10k", 190.0, 220.0),
    ("R3", "Device:R", "10k", 210.0, 220.0),
    ("#FLG01", "power:PWR_FLAG", "PWR_FLAG", 240.0, 220.0),
    ("#FLG02", "power:PWR_FLAG", "PWR_FLAG", 260.0, 220.0),
    ("#FLG03", "power:PWR_FLAG", "PWR_FLAG", 280.0, 220.0),
    ("#FLG04", "power:PWR_FLAG", "PWR_FLAG", 300.0, 220.0),
]

# Footprints from the KiCad stock libraries (hand-solderable SMD passives, 2.54 mm headers).
HEADER = "Connector_PinHeader_2.54mm:PinHeader_1x%02d_P2.54mm_Vertical"
FOOTPRINTS = {
    "U1": "Espressif:ESP32-DevKitC",
    "U2": "Package_SO:TSSOP-16_4.4x5mm_P0.65mm",
    "J1": HEADER % 2, "J2": HEADER % 6, "J3": HEADER % 2, "J4": HEADER % 4,
    "C1": "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm",
    "C2": "Capacitor_SMD:C_0805_2012Metric", "C3": "Capacitor_SMD:C_0805_2012Metric",
    "C4": "Capacitor_SMD:C_0805_2012Metric",
    "R1": "Resistor_SMD:R_0805_2012Metric", "R2": "Resistor_SMD:R_0805_2012Metric",
    "R3": "Resistor_SMD:R_0805_2012Metric",
}

# (ref, pin name or number, net). A pin name is matched without its ~{ } overbar markup.
# Nets in POWER become power symbols; a trailing "!" forces a plain label instead (used on
# crowded pins and for the power flags).
CONNECTIONS = [
    # ESP32 DevKitC (pin numbers are the DevKitC header pads; 14, 32 and 38 are all GND)
    ("U1", "1", "+3V3"), ("U1", "14", "GND"), ("U1", "2", "EN"),
    ("U1", "35", "UART_TX"), ("U1", "34", "UART_RX"),
    ("U1", "30", "M_IN1"), ("U1", "31", "M_IN2"), ("U1", "11", "M_NSLEEP"),
    ("U1", "10", "M_NFAULT"), ("U1", "5", "ENC_A"), ("U1", "6", "ENC_B"),
    ("U1", "37", "LOOP_TIMING"),
    # DRV8833: bridge A drives the motor, bridge B is unused
    ("U2", "AIN1", "M_IN1"), ("U2", "AIN2", "M_IN2"), ("U2", "SLEEP", "M_NSLEEP"),
    ("U2", "FAULT", "M_NFAULT"), ("U2", "AOUT1", "MOT_A"), ("U2", "AOUT2", "MOT_B"),
    ("U2", "VM", "+6V"), ("U2", "GND", "GND"), ("U2", "VCP", "VCP"), ("U2", "VINT", "VINT"),
    ("U2", "AISEN", "GND!"), ("U2", "BISEN", "GND!"), ("U2", "BIN1", "GND!"),
    ("U2", "BIN2", "GND!"),
    # Connectors
    ("J1", "1", "+6V"), ("J1", "2", "GND"),
    ("J2", "1", "MOT_A"), ("J2", "2", "GND!"), ("J2", "3", "ENC_A"), ("J2", "4", "ENC_B"),
    ("J2", "5", "+3V3"), ("J2", "6", "MOT_B"),
    ("J3", "1", "LOOP_TIMING"), ("J3", "2", "GND"),
    ("J4", "1", "GND"), ("J4", "2", "UART_TX"), ("J4", "3", "UART_RX"), ("J4", "4", "EN"),
    # Passives
    ("C1", "1", "+6V"), ("C1", "2", "GND"),
    ("C2", "1", "+6V"), ("C2", "2", "GND"),
    ("C3", "1", "+6V"), ("C3", "2", "VCP"),
    ("C4", "1", "VINT"), ("C4", "2", "GND"),
    ("R1", "1", "+3V3"), ("R1", "2", "M_NFAULT"),
    ("R2", "1", "+3V3"), ("R2", "2", "ENC_A"),
    ("R3", "1", "+3V3"), ("R3", "2", "ENC_B"),
    # Power flags give ERC a driver for the externally supplied nets
    ("#FLG01", "1", "+6V!"), ("#FLG02", "1", "GND!"), ("#FLG03", "1", "VINT!"),
    ("#FLG04", "1", "+3V3!"),
]
NO_CONNECT = [("U2", "BOUT1"), ("U2", "BOUT2")]

NOTES = [
    (20.0, 25.0, "ESP32 DC motor speed controller - intended wiring (6 V N20 gearmotor)"),
    (20.0, 31.0, "Pins are proposed. The firmware currently simulates the motor and uses only"),
    (20.0, 36.0, "GPIO23 (loop timing) and UART0; the other pins are not driven yet."),
    (20.0, 262.0, "J2 pin order is typical for N20 hall-encoder motors: check yours before wiring."),
    (20.0, 267.0, "UART0 is also the USB console: do not drive GPIO3 from the Pi while USB is attached."),
    (20.0, 272.0, "ESP32 and Raspberry Pi are 3.3 V logic. The 6 V supply must share GND with both."),
]


def uid(key):
    return str(uuid.uuid5(NS, key))


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


# Direction a pin leads away from its body, in schematic coordinates (y grows downward),
# keyed by the pin angle in the symbol library.
OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}


def snap(v):
    """Put part origins on the 2.54 mm grid so pins land on the connection grid."""
    return round(round(v / 2.54) * 2.54, 2)


PARTS = [(ref, lib_id, value, snap(x), snap(y)) for ref, lib_id, value, x, y in PARTS]
PLACED = {ref: (lib_id, x, y) for ref, lib_id, _, x, y in PARTS}


class Sheet:
    def __init__(self):
        self.libs = {}
        self.items = []
        self.pwr = 0
        self.seq = 0
        self.nets = {}
        self.sheet_uuid = uid("sheet")

    def lib(self, lib_id):
        if lib_id not in self.libs:
            block = load_symbol(lib_id)
            self.libs[lib_id] = (block, parse_pins(block))
        return self.libs[lib_id]

    def next_id(self, kind):
        self.seq += 1
        return uid(f"{kind}{self.seq}")

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
                 ("Footprint", FOOTPRINTS.get(ref, ""), " (hide yes)", (x, y), ""),
                 ("Datasheet", "", " (hide yes)", (x, y), ""),
                 ("Description", "", " (hide yes)", (x, y), "")]
        body = "".join(f'\n\t\t(property "{k}" "{v}" (at {round(at[0], 2)} {round(at[1], 2)} 0) '
                       f'{effects(extra=hide + j)})' for k, v, hide, at, j in props)
        pin_ids = "".join(f'\n\t\t(pin "{n}" (uuid "{self.next_id("pin")}"))' for n in pins)
        self.items.append(
            f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {x} {y} 0)\n\t\t(unit 1)\n\t\t(body_style 1)\n'
            f'\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
            f'\t\t(uuid "{self.next_id("sym")}"){body}{pin_ids}\n'
            f'\t\t(instances (project "{NAME}" (path "/{self.sheet_uuid}" '
            f'(reference "{ref}") (unit 1))))\n\t)')

    def pin_point(self, ref, pin):
        lib_id, x, y = PLACED[ref]
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
        if net in POWER and not label_only and d[0] == 0:  # power symbols only on vertical stubs
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
        return (f'(kicad_sch\n\t(version 20260101)\n\t(generator "generate_schematic.py")\n'
                f'\t(generator_version "10.0")\n\t(uuid "{self.sheet_uuid}")\n\t(paper "A3")\n'
                f'\t(lib_symbols\n{libs}\n\t)\n' + "\n".join(self.items) + "\n)\n")


def main():
    s = Sheet()
    for ref, lib_id, value, x, y in PARTS:
        s.symbol(ref, lib_id, value, x, y, hidden=ref.startswith("#"))
    for ref, pin, net in CONNECTIONS:
        s.connect(ref, pin, net)
    for ref, pin in NO_CONNECT:
        s.no_connect(ref, pin)
    # Every DevKit pin not wired above gets a no-connect flag (pins sharing a point with a wired
    # pin, such as the extra GND pads, are already connected).
    wired = {s.pin_point("U1", pin)[1:3] for ref, pin, _ in CONNECTIONS if ref == "U1"}
    for num in s.lib("Espressif:ESP32-DevKitC")[1]:
        if s.pin_point("U1", num)[1:3] not in wired:
            s.no_connect("U1", num)
    for x, y, t in NOTES:
        s.text(x, y, t)
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, NAME + ".kicad_sch"), "w", encoding="utf-8", newline="\n") as f:
        f.write(s.render())
    # Project library tables for the Espressif libraries (installed through the KiCad PCM).
    third = "${KICAD10_3RD_PARTY}"
    with open(os.path.join(here, "sym-lib-table"), "w", encoding="utf-8", newline="\n") as f:
        f.write('(sym_lib_table\n  (version 7)\n  (lib (name "Espressif")(type "KiCad")'
                f'(uri "{third}/symbols/{ESPRESSIF}/Espressif.kicad_sym")(options "")'
                '(descr "Espressif modules and DevKits"))\n)\n')
    with open(os.path.join(here, "fp-lib-table"), "w", encoding="utf-8", newline="\n") as f:
        f.write('(fp_lib_table\n  (version 7)\n  (lib (name "Espressif")(type "KiCad")'
                f'(uri "{third}/footprints/{ESPRESSIF}/Espressif.pretty")(options "")'
                '(descr "Espressif modules and DevKits"))\n)\n')
    with open(os.path.join(here, NAME + ".kicad_pro"), "w", encoding="utf-8", newline="\n") as f:
        f.write('{\n  "meta": {\n    "filename": "%s.kicad_pro",\n    "version": 3\n  }\n}\n' % NAME)
    if "--check" in sys.argv:
        for net, pins in sorted(s.nets.items()):
            print(f"{net:12s} {' '.join(pins)}")


if __name__ == "__main__":
    main()
