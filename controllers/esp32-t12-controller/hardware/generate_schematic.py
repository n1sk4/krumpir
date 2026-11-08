#!/usr/bin/env python3
"""Generate t12-controller.kicad_sch (KiCad 10) from the part and net tables below.

The circuit is the protected T12 / T15 controller described in docs/T12 Controller Design.md: a
24 V heater switched on the high side by two P-MOSFETs in series (the second one held on by a
heartbeat detector), high-side current sensing, a thermocouple amplifier and a buck converter for
the ESP32. It is an untested reference design: prototype it on a current-limited bench supply.

The schematic reads from the 24 V input at the top left, down the heater path, through the
thermocouple amplifier, with the current sense, the NTC and the buck converter beside it and the
ESP32 on the right. Wires connect the parts inside each block; net labels only link the blocks.

Usage (from any folder; the files go next to this script):
  python generate_schematic.py            # write t12-controller.kicad_sch and the library tables
  python generate_schematic.py --check    # also print each net with the pins on it
  python generate_schematic.py --out DIR  # write into DIR instead of this folder

The writer is shared with the other boards: tools/kicad/kicadgen.py (see its docstring for the
placement and wiring tables).
"""
import os
import sys
import uuid

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "..", "tools", "kicad"))
from kicadgen import Board, run  # noqa: E402

NAME = "t12-controller"
NS = uuid.UUID("7431322d-6374-726c-0000-000000000001")
POWER = {"GND", "+3V3"}

PMOS = "Transistor_FET:Q_PMOS_GDS"  # pins 1 G, 2 D, 3 S: the DPAK numbering
ZENER = "Device:D_Zener"  # pins 1 K, 2 A
R, C, CP = "Device:R", "Device:C", "Device:C_Polarized"
BAT54S = "Diode:BAT54S"  # pin 1 anode, 2 cathode, 3 the middle point
NPN = "Transistor_BJT:MMBT3904"  # pins 1 B, 2 E, 3 C
CONN2 = "Connector_Generic:Conn_01x02"
CONN4 = "Connector_Generic:Conn_01x04"
OPA = "Amplifier_Operational:OPA333xxD"
INA = "Amplifier_Current:INA240A2D"
BUCK = "Regulator_Switching:LMR36510ADDA"

PARTS, WIRES, CONNECTIONS, NAMES, POWER_AT = [], [], [], [], []


def part(ref, lib, value, x, y, rot=0, anchor=None):
    """Place a part; with an anchor pin, that pin lands exactly on (x, y)."""
    PARTS.append((ref, lib, value, x, y, rot) if anchor is None else
                 (ref, lib, value, x, y, rot, anchor))


def wire(*points):
    WIRES.append(list(points))


def conn(*triples):
    CONNECTIONS.extend(triples)


# --- Power input: connector, fuse, reverse-polarity MOSFET Q4 (off when wired backwards), TVS ----
# The +24V_SW rail runs along y = 38.1 to the buck converter on the right.
part("J1", CONN2, "24V in", 25.40, 38.10, 180, "2")  # pin 2 is +24 V, pin 1 GND
part("F1", "Device:Fuse", "T5A", 38.10, 38.10, 90, "1")
part("Q4", PMOS, "PMOS -60V", 55.88, 38.10, 90, "D")
part("D5", ZENER, "12V", 73.66, 38.10, 270, "K")
part("R9", R, "10k", 60.96, 50.80, 0, "1")
part("D2", ZENER, "SMBJ28A", 86.36, 38.10, 270, "K")
part("C2", CP, "470u/35V", 99.06, 38.10, 0, "1")
wire("J1.2", "F1.1")
wire("F1.2", "Q4.D")
wire("Q4.G", "R9.1")
wire("Q4.G", "D5.A")
wire("Q4.S", (312.42, 38.10), (312.42, 53.34), "U2.2")  # the rail, to the buck converter
conn(("J1", "1", "GND>"), ("R9", "2", "GND"), ("D2", "A", "GND"), ("C2", "2", "GND"))
NAMES.append(((121.92, 38.10), "up", "+24V_SW"))

# --- Series switch Q3: shunt, PMOS held on by the heartbeat detector through Q5 ------------------
part("R1", R, "10m", 116.84, 38.10, 0, "1")
part("Q3", PMOS, "PMOS -60V", 116.84, 53.34, 180, "S")
part("R7", R, "10k", 129.54, 50.80, 0, "1")
part("D7", ZENER, "12V", 142.24, 50.80, 270, "K")
part("R8", R, "10k", 185.42, 62.23, 0, "1")
part("Q5", NPN, "MMBT3904", 185.42, 73.66, 0, "C")
part("R12", R, "22k", 172.72, 78.74, 90, "2")
wire("R1.2", "Q3.S")
wire((116.84, 50.80), "R7.1", "D7.K")
wire("Q3.G", "R7.2", "D7.A", (185.42, 58.42), "R8.1")
wire("R8.2", "Q5.C")
wire("R12.2", "Q5.B")
conn(("Q5", "E", "GND"))
NAMES.append(((116.84, 46.99), "right", "HEAT_IN"))
NAMES.append(((146.05, 95.25), "right", "HEARTBEAT"))

# --- Heartbeat detector: GPIO26 toggles, the capacitor and BAT54S rectify it into R12 ----------
part("R13", R, "100k", 160.02, 78.74, 0, "1")
part("C6", C, "1u", 152.40, 78.74, 0, "1")
part("D4", BAT54S, "BAT54S", 144.78, 78.74, 0, "K")
part("C5", C, "100n", 137.16, 87.63, 0, "1")
wire("R12.1", "R13.1", "C6.1", "D4.K")
wire("D4.COM", "C5.1")
wire("C5.2", (146.05, 95.25))
conn(("R13", "2", "GND"), ("C6", "2", "GND"), ("D4", "A", "GND>"))

# --- Heater switch Q1, driven by the PWM pin through Q6; its drain is the tip -------------------
part("Q1", PMOS, "PMOS -60V", 116.84, 111.76, 180, "S")
part("R5", R, "10k", 129.54, 109.22, 0, "1")
part("D6", ZENER, "12V", 142.24, 109.22, 270, "K")
part("R6", R, "10k", 185.42, 120.65, 0, "1")
part("Q6", NPN, "MMBT3904", 185.42, 132.08, 0, "C")
part("R10", R, "4.7k", 172.72, 137.16, 90, "2")
part("R11", R, "100k", 175.26, 137.16, 0, "1")
wire("Q3.D", "Q1.S")  # SW_MID
wire((116.84, 109.22), "R5.1", "D6.K")
wire("Q1.G", "R5.2", "D6.A", (185.42, 116.84), "R6.1")
wire("R6.2", "Q6.C")
wire("R10.2", "Q6.B")
conn(("Q6", "E", "GND"), ("R11", "2", "GND"), ("R10", "1", "PWM"))

# --- Tip connector and the thermocouple front end -----------------------------------------------
part("J2", CONN2, "T12 tip", 104.14, 124.46, 180, "2")  # pin 2 is TIP+, pin 1 the shell (GND)
part("R4", R, "10k", 121.92, 170.18, 90, "1")
part("D1", BAT54S, "BAT54S", 149.86, 165.10, 0, "COM")
part("C1", C, "1n", 160.02, 170.18, 0, "1")
part("U3", OPA, "OPA333", 170.18, 170.18, 0, "3")
part("R3", R, "1k", 165.10, 198.12, 0, "1")
part("R2", R, "249k", 165.10, 193.04, 270, "2")
part("R14", R, "100", 195.58, 172.72, 90, "1")
part("C3", C, "10n", 210.82, 172.72, 0, "1")
part("C7", C, "100n", 200.66, 158.75, 0)
wire("Q1.D", (116.84, 170.18), "R4.1")  # TIP_PLUS
wire("J2.2", (116.84, 124.46))
wire("R4.2", "C1.1", "U3.3")  # SENSE_IN
wire("D1.COM", (149.86, 170.18))
wire("U3.2", (165.10, 175.26), (165.10, 193.04))  # TC_FB
wire((165.10, 193.04), "R3.1")
wire("R2.1", (190.50, 193.04), (190.50, 172.72))
wire("U3.6", "R14.1")
wire("R14.2", "C3.1", (215.90, 172.72))
conn(("J2", "1", "GND>"), ("D1", "A", "GND>"), ("D1", "K", "+3V3>"), ("C1", "2", "GND"),
     ("U3", "7", "+3V3"), ("U3", "4", "GND"), ("R3", "2", "GND"), ("C3", "2", "GND"),
     ("C7", "1", "+3V3"), ("C7", "2", "GND"))
NAMES.append(((215.90, 172.72), "right", "TC_ADC"))

# --- Heater current: INA240 across the shunt, 3 A * 10 mohm * 50 = 1.5 V, unidirectional -------
part("U4", INA, "INA240A2", 60.96, 215.90, 0)
part("R15", R, "100", 73.66, 215.90, 90, "1")
part("C4", C, "10n", 88.90, 215.90, 0, "1")
part("C8", C, "100n", 40.64, 205.74, 0)
wire("U4.2", (58.42, 228.60), (63.50, 228.60), "U4.7")
wire("U4.3", (60.96, 228.60))
wire("U4.5", "R15.1")
wire("R15.2", "C4.1", (96.52, 215.90))
POWER_AT.append(("GND", (60.96, 228.60)))
conn(("U4", "8", "+24V_SW"), ("U4", "1", "HEAT_IN"), ("U4", "6", "+3V3"),
     ("C8", "1", "+3V3"), ("C8", "2", "GND"), ("C4", "2", "GND"))
NAMES.append(((96.52, 215.90), "right", "CUR_ADC"))

# --- Handle NTC divider --------------------------------------------------------------------------
part("R16", R, "100k", 215.90, 205.74, 0, "1")
part("TH1", "Device:Thermistor_NTC", "NTC 100k", 215.90, 217.17, 0, "1")
part("J3", CONN2, "NTC handle", 228.60, 214.63, 0, "1")
wire("R16.2", "TH1.1")
wire((215.90, 214.63), "J3.1")
wire((215.90, 214.63), (210.82, 214.63))
conn(("R16", "1", "+3V3"), ("TH1", "2", "GND"), ("J3", "2", "GND>"))
NAMES.append(((210.82, 214.63), "left", "NTC_NODE"))

# --- 3.3 V buck converter, from the 24 V rail (values from the LMR36510 datasheet example) -------
part("U2", BUCK, "LMR36510", 330.20, 58.42)
part("C11", C, "2.2u/100V", 289.56, 38.10, 0, "1")
part("C12", C, "220n/100V", 304.80, 38.10, 0, "1")
part("C13", C, "100n", 345.44, 48.26, 90, "1")
part("C14", C, "1u", 314.96, 68.58, 0, "1")
part("L1", "Device:L", "22u", 360.68, 55.88, 90, "1")
part("C15", C, "22u", 373.38, 63.50, 0, "1")
part("C16", C, "22u", 383.54, 63.50, 0, "1")
part("C17", C, "22u", 393.70, 63.50, 0, "1")
part("R17", R, "100k", 345.44, 88.90, 0)
part("R18", R, "43.2k", 358.14, 88.90, 0)
part("D3", ZENER, "3.6V", 370.84, 88.90, 270)
part("C9", CP, "10u", 383.54, 88.90, 0)
part("C10", C, "100n", 396.24, 88.90, 0)
wire("U2.3", (312.42, 58.42), (312.42, 53.34))  # EN tied to VIN
wire("U2.8", (355.60, 55.88), "L1.1")  # SW_BUCK
wire("U2.7", (342.90, 53.34), (342.90, 48.26), "C13.1")  # BOOT
wire("C13.2", (355.60, 48.26), (355.60, 55.88))
wire("L1.2", (373.38, 55.88), (373.38, 63.50), "C15.1")
wire((373.38, 55.88), (373.38, 50.80))
wire("C15.1", "C16.1", "C17.1")
wire("U2.6", (314.96, 63.50), "C14.1")  # VCC
wire("U2.1", (330.20, 73.66), (332.74, 73.66), "U2.9")
POWER_AT.extend([("+3V3", (373.38, 50.80)), ("GND", (331.47, 73.66))])
conn(("C11", "2", "GND"), ("C12", "2", "GND"), ("C14", "2", "GND"), ("C15", "2", "GND"),
     ("C16", "2", "GND"), ("C17", "2", "GND"),
     ("U2", "5", "FB"), ("R17", "1", "+3V3"), ("R17", "2", "FB"), ("R18", "1", "FB"),
     ("R18", "2", "GND"), ("D3", "K", "+3V3"), ("D3", "A", "GND"),
     ("C9", "1", "+3V3"), ("C9", "2", "GND"), ("C10", "1", "+3V3"), ("C10", "2", "GND"))
# The two caps on the rail (tapped straight from it at y = 38.1) need no wires of their own.

# --- ESP32 DevKitC and its connectors ------------------------------------------------------------
part("U1", "Espressif:ESP32-DevKitC", "ESP32-DevKitC", 300.00, 215.00)
part("J4", CONN2, "Loop pin", 395.00, 162.00)
part("J5", CONN4, "Krumpir", 395.00, 178.00)
conn(("U1", "1", "+3V3"), ("U1", "14", "GND"), ("U1", "2", "EN"),
     ("U1", "9", "PWM"), ("U1", "10", "HEARTBEAT"),
     ("U1", "5", "TC_ADC"), ("U1", "6", "NTC_NODE"), ("U1", "3", "CUR_ADC"),
     ("U1", "35", "UART_TX"), ("U1", "34", "UART_RX"), ("U1", "37", "LOOP_TIMING"),
     ("J4", "1", "LOOP_TIMING"), ("J4", "2", "GND>"),
     ("J5", "1", "GND!"), ("J5", "2", "UART_TX"), ("J5", "3", "UART_RX"), ("J5", "4", "EN"))

# --- Power flags give ERC a driver for the supplied nets -----------------------------------------
part("#FLG01", "power:PWR_FLAG", "PWR_FLAG", 240.00, 268.00)
part("#FLG02", "power:PWR_FLAG", "PWR_FLAG", 255.00, 268.00)
part("#FLG03", "power:PWR_FLAG", "PWR_FLAG", 270.00, 268.00)
conn(("#FLG01", "1", "+24V_SW!"), ("#FLG02", "1", "GND!"), ("#FLG03", "1", "+3V3!"))

NO_CONNECT = [("U2", "4")]

HEADER = "Connector_PinHeader_2.54mm:PinHeader_1x%02d_P2.54mm_Vertical"
TERMINAL = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"
R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
C1210 = "Capacitor_SMD:C_1210_3225Metric"
DPAK = "Package_TO_SOT_SMD:TO-252-3_TabPin2"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOIC8 = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"
SOD323 = "Diode_SMD:D_SOD-323"
SMB = "Diode_SMD:D_SMB"
FOOTPRINTS = {
    "U1": "Espressif:ESP32-DevKitC",
    "U2": "Package_SO:Texas_HTSOP-8-1EP_3.9x4.9mm_P1.27mm_EP2.95x4.9mm_Mask2.4x3.1mm_ThermalVias",
    "U3": SOIC8, "U4": SOIC8,
    "Q1": DPAK, "Q3": DPAK, "Q4": DPAK, "Q5": SOT23, "Q6": SOT23,
    "D1": SOT23, "D4": SOT23, "D2": SMB, "D3": SMB,
    "D5": SOD323, "D6": SOD323, "D7": SOD323,
    "F1": "Fuse:Fuseholder_Cylinder-5x20mm_Schurter_0031_8201_Horizontal_Open",
    "R1": "Resistor_SMD:R_2512_6332Metric",
    "L1": "Inductor_SMD:L_Bourns-SRN8040_8x8.15mm",
    "TH1": R0805,
    "C2": "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
    "C9": "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm",
    "C11": C1210, "C12": C1206, "C15": C1206, "C16": C1206, "C17": C1206,
    "J1": TERMINAL, "J2": TERMINAL,
    "J3": HEADER % 2, "J4": HEADER % 2, "J5": HEADER % 4,
}
for ref in ["R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "R13", "R14",
            "R15", "R16", "R17", "R18"]:
    FOOTPRINTS[ref] = R0805
for ref in ["C1", "C3", "C4", "C5", "C6", "C7", "C8", "C10", "C13", "C14"]:
    FOOTPRINTS[ref] = C0805

NOTES = [
    (20.0, 14.0, "ESP32 T12 / T15 soldering tip controller, 24 V - protected reference design"),
    (20.0, 19.0, "See docs/T12 Controller Design.md. Untested: prototype on a current-limited bench supply."),
    (190.0, 45.0, "The buck output also feeds the DevKit 3V3 pin: do not power the board from USB and 24 V at once."),
    (190.0, 50.0, "LMR36510 values follow the datasheet 3.3 V example (22 uH, 3 x 22 uF out, 100k / 43.2k): check them."),
    (20.0, 268.0, "Everything on +24V_SW must be rated 60 V or more: the TVS can clamp as high as 45 V."),
    (20.0, 273.0, "Layout: one star ground at the negative terminal of C2. The heater return, the buck converter and the analog"),
    (20.0, 278.0, "ground each run to it separately; keep R4, D1, C1, U3 and R2/R3 off the heater return."),
    (20.0, 283.0, "R1 is a 2-pad 2512 shunt: route the INA240 inputs as a Kelvin connection at its pads."),
]

TEXT_AT = {"Q3": (-24.0, -1.27), "Q1": (-24.0, -1.27), "Q4": (-8.0, -10.0),
           "Q5": (6.0, -1.27), "Q6": (6.0, -1.27)}  # keep clear of the wires

BOARD = Board(NAME, NS, PARTS, CONNECTIONS, POWER, FOOTPRINTS, NO_CONNECT, NOTES,
              wires=WIRES, names=NAMES, power_at=POWER_AT, text_at=TEXT_AT)

if __name__ == "__main__":
    sys.exit(run(BOARD, here))
