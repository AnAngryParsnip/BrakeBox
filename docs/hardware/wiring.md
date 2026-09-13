# BrakeBox — Wiring

All pin numbers below are GPIO numbers on the Waveshare RP2040-Zero, and
match the pin map in `firmware/BrakeBox.ino` (Section 2 — Pin map). If you
change any pin assignment in the firmware, update this document to match.

## Pin map

| Function | GPIO |
|---|---|
| Load cell (HX711 DOUT) | 4 |
| Load cell (HX711 SCK) | 5 |
| OLED SDA (Wire1) | 2 |
| OLED SCL (Wire1) | 3 |
| Status LED (NeoPixel data) | 16 |
| ENC 1 (left) — CLK | 6 |
| ENC 1 (left) — DT | 7 |
| ENC 1 (left) — SW (click) | 8 |
| ENC 2 (middle) — CLK | 14 |
| ENC 2 (middle) — DT | 15 |
| ENC 2 (middle) — SW (click) | 26 |
| ENC 3 (right) — CLK | 27 |
| ENC 3 (right) — DT | 28 |
| ENC 3 (right) — SW (click) | 29 |

All three encoder switch pins are configured as `INPUT_PULLUP` in
firmware — wire each encoder's SW pin directly to its GPIO with no
external pull-up resistor needed. Each encoder also needs its GND and
(if present) + pins connected to the RP2040-Zero's GND and 3.3V.

## Load cell → HX711

A standard 4-wire load cell has two excitation wires (usually red and
black) and two signal wires. Wire it as follows:

| Load cell wire | HX711 pin |
|---|---|
| Red (E+) | E+ |
| Black (E-) | E- |
| Signal | **A+** (not A-) |

In addition, bridge two 1kΩ resistors as follows:

- One 1kΩ resistor from **E+ to A-**
- One 1kΩ resistor from **E- to A-**

This bridges the unused differential input (A-) to a stable reference
point, which is required for a clean, low-noise reading from a
single-ended-style wiring like this. Without these two resistors the
reading will be noisy or unusable.

> If your load cell uses different wire colors, check its datasheet —
> "E+/E-" are the excitation (power) pair and "A+/A-" (or "S+/S-") are
> the signal (output) pair. Do not assume red = E+ on every load cell.

## HX711 → RP2040-Zero

| HX711 pin | RP2040-Zero GPIO |
|---|---|
| DOUT | 4 |
| SCK | 5 |
| VCC | 3.3V |
| GND | GND |

Also see `hx711-80sps-mod.md` — a required hardware modification for
usable input latency.

## OLED display

The OLED is wired to **Wire1** (the RP2040's second I2C bus), not the
default Wire bus:

| OLED pin | RP2040-Zero GPIO |
|---|---|
| SDA | 2 |
| SCL | 3 |
| VCC | 3.3V (check your display's rated voltage — most SSD1309 modules are 3.3V-tolerant, some need 5V; check before powering) |
| GND | GND |

## Status LED (NeoPixel)

Single WS2812/NeoPixel LED, data line on GPIO 16. Note the color order is
**GRB**, matching most common NeoPixel/WS2812 parts — if your specific LED
uses a different order, update `NEO_GRB` in the firmware to match, or
colors will appear wrong.

## Power

The whole unit is powered over USB (5V from the PC or a USB power
adapter). No separate power supply is required for the electronics
described here.
