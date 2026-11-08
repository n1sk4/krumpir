# Krumpir bench bill of materials

Low-cost hardware-in-the-loop bench: a Raspberry Pi 4B runs the plant model (FMU) under PREEMPT_RT, and an ESP32 runs the controller under test (DUT) with its own firmware, the two joined by a serial link. A Pico 2 and analog I/O are a later stage ([below](#later-stage-pico-2-and-analog-io)).

> Prices are approximate (EUR, late 2026) and vary by supplier. All logic is 3.3 V — no level shifting needed.

## Architecture

```
ESP32 (DUT, PID, 1 kHz) --UART2: commands (CMD)----------> Pi 4B (runner: FMU, PREEMPT_RT, core 3)
        ^                                                        |
        +--------------- UART2: plant values (PLANT) <-----------+
```

The same link runs from a PC (the runner on Linux or WSL) through a USB-serial adapter, before the Pi
is in the loop ([bench-overview.md](bench-overview.md)).

## Core

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 1 | Raspberry Pi 4 Model B, 4 GB | 1 | Runs FMU on isolated core (PREEMPT_RT) | €55–65 |
| 2 | Official Raspberry Pi USB-C PSU (5.1 V / 3 A) | 1 | Stable power (undervoltage causes throttling) | €8–10 |
| 3 | microSD 32 GB (A2) or USB 3 SSD | 1 | OS and logs | €8–15 |
| 4 | Case with active fan / heatsink | 1 | Prevents thermal throttling during timing tests | €8–15 |

## Device under test

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 5 | ESP32 DevKit (classic ESP32-WROOM-32) | 1 | Runs the controller firmware (`esp32dev-hil`); the HIL link on UART2, the console on UART0 (USB) | €6–10 |

## Link

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 6 | USB-serial adapter, 3.3 V (FT232R or CP2102) | 1 | The ESP32's HIL UART to the runner on a PC (`/dev/ttyUSB0`); with an FTDI chip set its latency timer to 1 ms | €3–8 |
| 7 | 330 Ω resistors | 10+ | Series protection on the UART lines | €1 |

Wiring (UART2, 921600 baud, 8N1):

| ESP32 | Adapter (runner on a PC) | Pi 4B (runner on the Pi) |
|-------|--------------------------|--------------------------|
| GPIO17 (TX) | RX | the UART's RX |
| GPIO16 (RX) | TX | the UART's TX |
| GND | GND | GND |

The runner's `device` is `/dev/ttyUSB0` for the adapter and `/dev/ttyAMA0` on the Pi; the pins and the
bring-up checklist are in each controller's `docs/krumpir-interface.md` ("HIL link").

## Measurement & prototyping

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 8 | USB logic analyzer, 8 ch / 24 MHz (sigrok/PulseView compatible) | 1 | Step jitter and loop latency: the Pi's timing pin (`rt.timing_gpio`), the ESP32's GPIO 23, the UART lines | €10–15 |
| 9 | Breadboard, full size | 1–2 | Prototyping | €4–8 |
| 10 | Jumper wire kit (M-M, M-F) | 1 | Wiring | €4–6 |
| 11 | USB cables (micro/USB-C as needed) | 2–3 | The ESP32 (flashing, console) and the adapter | €5 |
| 12 | Proto HAT for Pi | 1 | *Optional* — clean permanent build | €5–10 |

## Totals (approx.)

| Configuration | Price |
|---------------|-------|
| Bench (items 1–11) | ~€112–158 |
| Bench without Pi (already owned, items 5–11) | ~€33–53 |
| + optional proto HAT (item 12) | +€5–10 |
| + later stage, core (items 13–15) | +€9–13 |
| + later stage, optional ADC and CAN (items 16–18) | +€8.5–11.5 |

## Later stage: Pico 2 and analog I/O

The controller's own analog and PWM I/O path, per controller: the plant's values as voltages into the
ESP32's ADC and its PWM measured, instead of the UART link. Not part of the bench above.

```
ESP32 (DUT, PID) --PWM--> Pico 2 (PIO capture) --SPI--> Pi 4B (FMU, PREEMPT_RT)
       ^                                                        |
       |                                                        v
       +----------ADC <---- MCP4822 DAC <--SPI-- Pico 2 <--SPI--+
```

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 13 | Raspberry Pi Pico 2 (RP2350) | 1 | I/O coprocessor: hardware-timed sampling, PWM capture via PIO | €5–7 |
| 14 | MCP4822 dual 12-bit SPI DAC (DIP-8) | 1 | Plant outputs (e.g. simulated speed) → DUT ADC | €3–5 |
| 15 | 100 nF ceramic capacitors | 5+ | Decoupling for DAC/ADC | €1 |
| 16 | MCP3208 8-ch 12-bit SPI ADC (DIP-16) | 1 | *Optional* — upgrade if Pico's internal ADC is too noisy | €4–5 |

Optional CAN extension:

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 17 | SN65HVD230 CAN transceiver module (3.3 V) | 2 | ESP32 TWAI ↔ Pico (can2040 PIO library) | €4–6 |
| 18 | 120 Ω resistors | 2 | Bus termination (if not on modules) | €0.5 |

Closed-loop timing on this path: exchange one sample per step (e.g. 1 kHz) between the Pi and the Pico
rather than buffering blocks — buffering adds latency that destabilizes the loop.

## Notes

- **Headline measurements:** step jitter (worst case), loop latency (a plant frame out to the command it caused back, at most 2 steps), and SIL (FMPy on PC) vs HIL output match ([bench-overview.md](bench-overview.md), Acceptance).
- **Grounding:** connect all boards to a common ground; power the ESP32 (and later the Pico) from USB or the Pi's 3.3 V/5 V rails, not separate floating supplies.
