# BrakeBox — Bill of Materials

Quantities are per unit. Links are omitted deliberately — sources and
prices change constantly and vary by region; search the part name/number
on your preferred supplier (AliExpress, Amazon, Mouser, etc.).

| # | Part | Qty | Notes |
|---|------|-----|-------|
| 1 | Waveshare RP2040-Zero | 1 | Main controller board |
| 2 | HX711 load cell amplifier module | 1 | Requires the 80SPS hardware mod — see `hx711-80sps-mod.md` |
| 3 | Load cell | 1 | Bar/button-style load cell rated for expected pedal force (50 kg+ recommended) |
| 4 | 2.42" OLED display, SSD1309 driver, I2C | 1 | 128×64, white or blue/green depending on preference |
| 5 | KY-040 rotary encoder module | 3 | One per ENC (left/middle/right) |
| 6 | WS2812/NeoPixel LED (single) | 1 | Status LED |
| 7 | 100Ω resistor | 1 | HX711 80SPS mod (pin 15 RATE → VCC) |
| 8 | 1kΩ resistor | 2 | Load cell bridge wiring (E+→A-, E-→A-) |
| 9 | Pedal/pushplate mechanism | 1 | Whatever housing/lever arrangement you are building into (varies by build — not specified here) |
| 10 | Hookup wire, various gauges | — | |
| 11 | USB-C cable | 1 | Data-capable, for connecting to PC |
| 12 | Enclosure | 1 | 3D-printed, laser-cut, or off-the-shelf project box |
| 13 | M3 screws/standoffs (or similar) | — | For mounting the RP2040-Zero, OLED, and encoders in the enclosure |

## Optional / nice-to-have

| Part | Notes |
|------|-------|
| Perfboard or small custom PCB | Cleaner wiring than point-to-point; a custom PCB is on the project roadmap |
| Rubber feet | If mounting the unit as a free-standing box near your pedals |

## Not included / build-specific

This BOM covers the electronics common to every build. It does not cover
the pedal mechanism itself (lever, spring/elastomer, mounting bracket),
since that depends heavily on what you're integrating BrakeBox into (e.g.
retrofitting a Logitech G29 pedal vs. a fully custom rig). See your own
build notes for that part.
