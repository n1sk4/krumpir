# Krumpir bench bill of materials

Low-cost hardware-in-the-loop bench: a Raspberry Pi 4B runs the plant model (FMU) under PREEMPT_RT, and an ESP32 runs the controller under test (DUT) with its own firmware, the two joined by a serial link.

> Prices are approximate (EUR, late 2026) and vary by supplier. All logic is 3.3 V — no level shifting needed.

## Architecture

```
ESP32 (DUT, PID, 1 kHz) --UART2: commands (CMD)----------> Pi 4B (runner: FMU, PREEMPT_RT, core 3)
        ^                                                        |
        +--------------- UART2: plant values (PLANT) <-----------+
```

The same link also runs from a PC (the runner on Linux or WSL) through a USB-serial adapter
([bench-overview.md](bench-overview.md)).

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

Wiring (UART2, 921600 baud, 8N1; on the controllers' schematics the Krumpir header J4 / J5):

| ESP32 | Header pin | Adapter (runner on a PC) | Pi 4B (runner on the Pi) |
|-------|------------|--------------------------|--------------------------|
| GPIO17 (TX), `HIL_TX` | 2 | RX | the UART's RX |
| GPIO16 (RX), `HIL_RX` | 3 | TX | the UART's TX |
| GND | 1 | GND | GND |

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

## Notes

- **Headline measurements:** step jitter (worst case), loop latency (a plant frame out to the command it caused back, at most 2 steps), and SIL (FMPy on PC) vs HIL output match ([bench-overview.md](bench-overview.md), Acceptance).
- **Grounding:** connect all boards to a common ground; power the ESP32 from USB or the Pi's 3.3 V/5 V rails, not a separate floating supply.
- **Not covered:** the controller's own analog and PWM I/O (the bench drives the controller over the UART only).
