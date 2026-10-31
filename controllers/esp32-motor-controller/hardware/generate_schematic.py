"""Generate motor-controller.kicad_sch (KiCad 10) from the part and net tables below.

Usage (from this folder):
  python generate_schematic.py            # write motor-controller.kicad_sch and the library tables
  python generate_schematic.py --check    # also print each net with the pins on it
  python generate_schematic.py --out DIR  # write into DIR instead of this folder

The writer itself is shared with the other boards: tools/kicad/kicadgen.py (see its docstring for
the KiCad library locations). Edit the tables, rerun, then export with kicad-cli (see README.md).
"""
import os
import sys
import uuid

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "..", "tools", "kicad"))
from kicadgen import Board, run  # noqa: E402

NAME = "motor-controller"
NS = uuid.UUID("6d6f746f-722d-4374-726c-000000000001")
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

BOARD = Board(NAME, NS, PARTS, CONNECTIONS, POWER, FOOTPRINTS, NO_CONNECT, NOTES)

if __name__ == "__main__":
    run(BOARD, here)
