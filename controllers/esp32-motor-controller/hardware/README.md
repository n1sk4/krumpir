# Hardware

KiCad 10 schematic of the intended wiring: ESP32 DevKit, DRV8833 motor driver, a 6 V N20 gearmotor
with a quadrature hall encoder, and the connections to Krumpir (Raspberry Pi).

**Status:** this documents the planned hardware. The firmware currently simulates the motor and
uses only GPIO 23 (loop timing) and UART0. The other pins below are proposed and not driven yet.

| File | Content |
|---|---|
| `motor-controller.pdf` / `.svg` | rendered schematic, viewable without KiCad |
| `motor-controller.kicad_sch` | schematic (open `motor-controller.kicad_pro` in KiCad 10) |
| `sym-lib-table`, `fp-lib-table` | point the project at the Espressif symbol and footprint libraries |
| `generate_schematic.py` | writes the files above from the part and net tables in the script |

The schematic files are generated: change the tables in `generate_schematic.py` and rerun. If you
edit the schematic in KiCad instead, do not regenerate over it.

**Libraries:** the ESP32 DevKit uses the symbol and footprint `ESP32-DevKitC` (38-pin board) from
the [Espressif KiCad libraries](https://github.com/espressif/kicad-libraries). Install them once
from KiCad's Plugin and Content Manager (Install from File with the add-on zip). The project's
`sym-lib-table` and `fp-lib-table` reference them through `${KICAD10_3RD_PARTY}`. Check that your
board's pin layout matches the DevKitC before ordering a PCB. All other parts use KiCad stock
symbols and footprints: DRV8833 in TSSOP-16, 0805 SMD capacitors and resistors, a 6.3 mm radial
electrolytic and 2.54 mm pin headers.

## Connections

ESP32 DevKit (U1) to the rest. Pins avoid the strapping pins (0, 2, 5, 12, 15); GPIO 34 and 35 are
input-only and have no internal pull-ups, hence R2 and R3. All unused DevKit pins carry no-connect
flags.

| ESP32 pin (DevKitC pad) | Net | Goes to |
|---|---|---|
| GPIO 18 (30) | `M_IN1` | DRV8833 AIN1 (PWM) |
| GPIO 19 (31) | `M_IN2` | DRV8833 AIN2 (PWM) |
| GPIO 27 (11) | `M_NSLEEP` | DRV8833 nSLEEP |
| GPIO 26 (10) | `M_NFAULT` | DRV8833 nFAULT, pulled up to 3V3 by R1 (10 k) |
| GPIO 34 (5) | `ENC_A` | encoder A (J2 pin 3), pulled up by R2 (10 k) |
| GPIO 35 (6) | `ENC_B` | encoder B (J2 pin 4), pulled up by R3 (10 k) |
| GPIO 23 (37) | `LOOP_TIMING` | test header J3 (loop-timing pulse) |
| GPIO 1, TX0 (35) | `UART_TX` | Krumpir header J4 pin 2 |
| GPIO 3, RX0 (34) | `UART_RX` | Krumpir header J4 pin 3 |
| EN, CHIP_PU (2) | `EN` | Krumpir header J4 pin 4 (reset) |
| 3V3 (1), GND (14, 32, 38) | `+3V3`, `GND` | encoder supply, pull-ups, grounds |

Connectors:

| Ref | Pins |
|---|---|
| J1, 6 V in | 1 `+6V`, 2 `GND` |
| J2, N20 motor + encoder | 1 motor +, 2 `GND`, 3 `ENC_A`, 4 `ENC_B`, 5 `+3V3`, 6 motor - |
| J3, GPIO 23 loop timing | 1 `LOOP_TIMING`, 2 `GND` |
| J4, Krumpir (Raspberry Pi) | 1 `GND`, 2 `UART_TX`, 3 `UART_RX`, 4 `EN` |

- J2 pin order is the common one for N20 hall-encoder motors (motor +, encoder GND, A, B, encoder
  3V3, motor -). Check yours before wiring.
- `UART_TX` is the ESP32 transmit line, so it connects to the Raspberry Pi's receive pin, and
  `UART_RX` to the Pi's transmit pin.
- UART0 is also the USB console. Use the USB cable or the Pi, not both driving GPIO 3.
- The ESP32 and the Raspberry Pi are 3.3 V logic. The 6 V supply must share GND with both.

DRV8833 (U2), bridge A drives the motor (`MOT_A` = AOUT1, `MOT_B` = AOUT2):

| Part | Value | Between |
|---|---|---|
| C1 | 100 uF | `+6V` and `GND` (bulk) |
| C2 | 100 nF | `+6V` and `GND` |
| C3 | 10 nF | `VCP` and `+6V` (charge pump) |
| C4 | 2.2 uF | `VINT` and `GND` |

Bridge B is unused: BIN1, BIN2 and BISEN are tied to GND and BOUT1/BOUT2 are left open (no-connect
flags). AISEN is tied to GND, so there is no current sensing.

## Regenerate and check

Needs KiCad 10 with the Espressif libraries and Python. The script reads KiCad's stock symbols from
the default Windows install path (set `KICAD_SYMBOL_DIR` otherwise) and the Espressif symbol from
`~\Documents\KiCad\10.0\3rdparty` (set `KICAD_3RD_PARTY_DIR` otherwise):

```
python generate_schematic.py --check     # writes the files, prints each net and its pins
kicad-cli sch erc --severity-all --format report -o erc.rpt motor-controller.kicad_sch
kicad-cli sch export pdf -o motor-controller.pdf motor-controller.kicad_sch
kicad-cli sch export svg -o . motor-controller.kicad_sch
```

`kicad-cli` is in `C:\Program Files\KiCad\10.0\bin`. The ERC report has no errors and two expected
warnings: AISEN and BISEN are tied to the `GND` net, which carries a power flag, and the pin types
conflict on paper. The connection is what the datasheet specifies for unused current sensing.

## Not included

PCB layout and footprints, current sensing, reverse-polarity protection, an emergency stop, and
firmware for the motor driver and encoder pins.
