# Krumpir bench bill of materials

Low-cost hardware-in-the-loop bench: a Raspberry Pi 4B runs the plant model (FMU) under PREEMPT_RT, a Raspberry Pi Pico 2 handles timing-critical I/O, and an ESP32 acts as the device under test (DUT).

> Prices are approximate (EUR, late 2026) and vary by supplier. All logic is 3.3 V — no level shifting needed.

## Architecture

```
ESP32 (DUT, PID) --PWM--> Pico 2 (PIO capture) --SPI--> Pi 4B (FMU, PREEMPT_RT)
       ^                                                        |
       |                                                        v
       +----------ADC <---- MCP4822 DAC <--SPI-- Pico 2 <--SPI--+
```

## Core

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 1 | Raspberry Pi 4 Model B, 4 GB | 1 | Runs FMU on isolated core (PREEMPT_RT) | €55–65 |
| 2 | Official Raspberry Pi USB-C PSU (5.1 V / 3 A) | 1 | Stable power (undervoltage causes throttling) | €8–10 |
| 3 | microSD 32 GB (A2) or USB 3 SSD | 1 | OS and logs | €8–15 |
| 4 | Case with active fan / heatsink | 1 | Prevents thermal throttling during timing tests | €8–15 |
| 5 | Raspberry Pi Pico 2 (RP2350) | 1 | I/O coprocessor: hardware-timed sampling, PWM capture via PIO | €5–7 |

## I/O

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 6 | MCP4822 dual 12-bit SPI DAC (DIP-8) | 1 | Plant outputs (e.g. simulated speed) → DUT ADC | €3–5 |
| 7 | MCP3208 8-ch 12-bit SPI ADC (DIP-16) | 1 | *Optional* — upgrade if Pico's internal ADC is too noisy | €4–5 |
| 8 | 330 Ω resistors | 10+ | Series protection on inter-board signals | €1 |
| 9 | 100 nF ceramic capacitors | 5+ | Decoupling for DAC/ADC | €1 |

## Device under test

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 10 | ESP32 DevKit (classic ESP32-WROOM-32) | 1 | Runs PID controller; PWM out, ADC in | €6–10 |

## Measurement & prototyping

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 11 | USB logic analyzer, 8 ch / 24 MHz (sigrok/PulseView compatible) | 1 | Measure step jitter and loop latency | €10–15 |
| 12 | Breadboard, full size | 1–2 | Prototyping | €4–8 |
| 13 | Jumper wire kit (M-M, M-F) | 1 | Wiring | €4–6 |
| 14 | USB cables (micro/USB-C as needed) | 2–3 | Flashing Pico / ESP32 | €5 |
| 15 | Proto HAT for Pi | 1 | clean permanent build | €5–10 |

## Optional: CAN extension

| # | Item | Qty | Purpose | Approx. price |
|---|------|-----|---------|---------------|
| 16 | SN65HVD230 CAN transceiver module (3.3 V) | 2 | ESP32 TWAI ↔ Pico (can2040 PIO library) | €4–6 |
| 17 | 120 Ω resistors | 2 | Bus termination (if not on modules) | €0.5 |

## Totals (approx.)

| Configuration | Price |
|---------------|-------|
| Base bench (items 1–6, 8–14) | ~€120–170 |
| Base bench without Pi (already owned, items 5–6, 8–14) | ~€40–65 |
| + optional ADC, proto HAT, CAN | +€15–25 |

## Notes

- **Closed-loop timing:** exchange one sample per step (e.g. 1 kHz) between Pi and Pico rather than buffering blocks — buffering adds latency that destabilizes the loop.
- **Headline measurements for the portfolio:** step jitter (worst case), Pi↔Pico round-trip latency, and SIL (FMPy on PC) vs HIL output match.
- **Grounding:** connect all boards to a common ground; power the Pico and ESP32 from USB or the Pi's 3.3 V/5 V rails, not separate floating supplies.
