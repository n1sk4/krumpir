# Hardware

KiCad 10 schematic of the protected controller for a T12 / T15 soldering tip, as described in
[../../../docs/T12 Controller Design.md](../../../docs/T12%20Controller%20Design.md): a 24 V heater
switched on the high side by two P-MOSFETs in series, high-side current sensing, a thermocouple
amplifier and a 3.3 V buck converter that powers an ESP32 DevKit.

**Status:** an untested reference design. Prototype it on a current-limited bench supply. The
firmware currently simulates the tip and uses only GPIO 23 (loop timing) and UART0; the other pins
below are proposed and not driven yet. There is no PCB layout.

| File | Content |
|---|---|
| `t12-controller.pdf` / `.svg` | rendered schematic, viewable without KiCad |
| `t12-controller.kicad_sch` | schematic (open `t12-controller.kicad_pro` in KiCad 10) |
| `sym-lib-table`, `fp-lib-table` | point the project at the Espressif symbol and footprint libraries |
| `generate_schematic.py` | the part and net tables of this board; writes the files above through the shared writer in `../../../tools/kicad/kicadgen.py` |

**Libraries:** the ESP32 DevKit uses the symbol and footprint `ESP32-DevKitC` (38-pin board) from
the [Espressif KiCad libraries](https://github.com/espressif/kicad-libraries). Install them once
from KiCad's Plugin and Content Manager (Install from File with the add-on zip). Every other part
uses KiCad stock symbols and footprints.

## How the schematic is laid out

The 24 V input is at the top left. The `+24V_SW` rail runs along the top to the buck converter on
the right. The heater path runs down the page: shunt R1, series switch Q3, heater switch Q1, then
the tip connector. The thermocouple amplifier continues from the tip node. Wires connect the parts
inside each block; net labels link the blocks (`PWM`, `HEARTBEAT`, `TC_ADC`, `NTC_NODE`,
`CUR_ADC`, the UART lines) and power symbols are used for GND and +3V3.

| Block | Parts |
|---|---|
| Power input | J1 connector, F1 5 A fuse, Q4 reverse-polarity P-MOSFET (D5 12 V zener, R9 pull-down), D2 SMBJ28A TVS, C2 470 uF bulk |
| Series switch | R1 10 mohm shunt, Q3 P-MOSFET (R7, D7, R8), Q5 driver |
| Heartbeat detector | C5, D4 BAT54S, C6, R13, R12: GPIO 26 toggling holds Q5, and so Q3, on |
| Heater switch | Q1 P-MOSFET (R5, D6, R6), Q6 driver (R10, R11), J2 tip connector |
| Thermocouple | R4 10k and D1 BAT54S protection, C1, U3 OPA333 with R2 249k / R3 1k (gain 250), R14 / C3 output filter |
| Heater current | U4 INA240A2 across R1 (50 V/V, 3 A gives 1.5 V), R15 / C4 output filter |
| Handle NTC | R16 100k, TH1 NTC 100k, J3 for an NTC in the handle |
| Buck converter | U2 LMR36510 (4.2 to 65 V in, 3.3 V out), C11 / C12 in, L1 22 uH, C15 to C17 out, R17 / R18 feedback, D3 3.6 V clamp |
| ESP32 | U1 DevKitC, C9 / C10 decoupling, J4 loop-timing header, J5 Krumpir header |

## Connections

ESP32 DevKit (U1) to the rest. Pins avoid the strapping pins (0, 2, 5, 12, 15) and use only ADC1
(GPIO 34, 35, 36), since ADC2 is unusable while Wi-Fi runs. All unused DevKit pins carry no-connect
flags.

| ESP32 pin (DevKitC pad) | Net | Goes to |
|---|---|---|
| GPIO 25 (9) | `PWM` | R10 and Q6, which switch heater switch Q1 |
| GPIO 26 (10) | `HEARTBEAT` | C5, the heartbeat detector that holds Q3 on |
| GPIO 34 (5) | `TC_ADC` | thermocouple amplifier output (R14 / C3) |
| GPIO 35 (6) | `NTC_NODE` | handle NTC divider |
| GPIO 36 (3) | `CUR_ADC` | INA240 output (R15 / C4) |
| GPIO 23 (37) | `LOOP_TIMING` | test header J4 |
| GPIO 1, TX0 (35) | `UART_TX` | Krumpir header J5 pin 2 |
| GPIO 3, RX0 (34) | `UART_RX` | Krumpir header J5 pin 3 |
| EN, CHIP_PU (2) | `EN` | Krumpir header J5 pin 4 (reset) |
| 3V3 (1), GND (14, 32, 38) | `+3V3`, `GND` | the buck converter output and the ground |

Connectors:

| Ref | Pins |
|---|---|
| J1, 24 V in | 1 `GND`, 2 `+24V_IN` |
| J2, T12 tip | 1 shell (`GND`), 2 `TIP_PLUS` |
| J3, NTC in the handle | 1 `NTC_NODE`, 2 `GND` |
| J4, GPIO 23 loop timing | 1 `LOOP_TIMING`, 2 `GND` |
| J5, Krumpir (Raspberry Pi) | 1 `GND`, 2 `UART_TX`, 3 `UART_RX`, 4 `EN` |

- J1 and J2 are drawn rotated so their pins face the circuit, so pin 2 is the live pin.
- `UART_TX` is the ESP32 transmit line, so it connects to the Raspberry Pi's receive pin, and
  `UART_RX` to the Pi's transmit pin. UART0 is also the USB console: use the USB cable or the Pi.
- The buck converter feeds the DevKit's 3V3 pin. Do not power the board from USB and from the 24 V
  at once.
- The tip shell is ground, so the heater is switched on the high side. Everything on `+24V_SW`
  must be rated 60 V or more: the TVS can clamp as high as 45 V.

## Values to check before building

- **Buck converter:** the values follow the LMR36510 datasheet's 3.3 V example (22 uH, three
  22 uF output capacitors, 100k / 43.2k feedback divider, 2.2 uF + 220 nF input). EN is tied to
  VIN. Check them against the datasheet.
- **Part values the design document does not give:** the 22k base resistor of Q5, the 4.7k base
  resistor of Q6, the NTC divider (100k / 100k, with the firmware's beta of 3950) and the 100 ohm /
  10 nF output filters. Confirm them on the prototype.
- **Shunt:** R1 is a plain 2-pad 2512 resistor. Route the INA240 inputs as a Kelvin connection at
  its pads in the layout.
- **P-MOSFETs:** Q1, Q3 and Q4 use a generic symbol with the DPAK footprint. Pick a part with
  VDS of -60 V, RDS(on) under 30 mohm at -10 V and ID of 10 A or more, and check the pin order
  (G, D, S) against its datasheet.
- **Grounding in the layout:** one star ground at the negative terminal of C2. The heater return,
  the buck converter and the analog ground each run to it separately, and R4, D1, C1, U3 and
  R2 / R3 stay off the heater return.

## Editing and regenerating

Edit the schematic in KiCad, or change the tables in `generate_schematic.py` and rerun. Do not
regenerate over a schematic you edited in KiCad: the generator overwrites `t12-controller.kicad_sch`.
It only writes `t12-controller.kicad_pro` when it does not exist, so KiCad's project settings are
safe. The tables describe the schematic's parts and wires (see the docstring of
`../../../tools/kicad/kicadgen.py` for the format).

```
python generate_schematic.py --check          # writes the files, prints each net and its pins
python generate_schematic.py --out DIR        # writes into DIR instead, for comparing
kicad-cli sch erc --severity-all --format report -o erc.rpt t12-controller.kicad_sch
kicad-cli sch export pdf -o t12-controller.pdf t12-controller.kicad_sch
kicad-cli sch export svg -o . t12-controller.kicad_sch
```

Needs KiCad 10 with the Espressif libraries and Python. The script reads KiCad's stock symbols
from the default Windows install path (set `KICAD_SYMBOL_DIR` otherwise) and the Espressif symbol
from `~\Documents\KiCad\10.0\3rdparty` (set `KICAD_3RD_PARTY_DIR` otherwise). `kicad-cli` is in
`C:\Program Files\KiCad\10.0\bin`. The ERC report has no violations.

## Not included

PCB layout and footprint placement, thermal design of the heater switches, an emergency stop, a
mains-side supply, and the firmware for the real heater, sensor and heartbeat pins.
