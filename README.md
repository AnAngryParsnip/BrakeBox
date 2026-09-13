# BrakeBox

A DIY, standalone brake pedal controller for sim racing. BrakeBox replaces
a stock potentiometer pedal (e.g. a Logitech G29's) with a load cell, and
gives you full control over the response curve, deadzones, and bite point
— all configured on-device via a small OLED screen and three rotary
encoders, with no PC app required for normal use.

![BrakeBox](docs/images/hero.jpg)
<!-- swap in a real photo of the finished unit -->

## Features

- Load-cell input via HX711, read at 80SPS (see hardware mod below)
- Three curve modes: Curve (single), Sym-S, Asym-S
- Configurable deadzone: low-end reshape (DZN L) and soft output cap (DZN H)
- 500 profile slots across 25 banks, with 20 curated factory presets
  (GT3, F1, Rally, Drift, and more)
- USB HID joystick output (no drivers or companion app needed)
- Optional VR-mirror mode: streams the OLED display to a PC viewer app
  for use in VR

## Hardware

- Waveshare RP2040-Zero
- HX711 load cell amplifier
- 2.42" SSD1309 OLED display
- 3× KY-040 rotary encoders
- Load cell (see wiring notes in `docs/hardware/`)

**Required modification:** the HX711 board's factory rate-select pads did
not work as documented on the units used in this build. A 100Ω resistor
from HX711 pin 15 (RATE) to VCC is required to get 80SPS operation;
without it, brake input lags by 100ms or more. Full details in
`docs/hardware/hx711-80sps-mod.md`.

## Repository structure

```
firmware/         Arduino sketch (BrakeBox.ino)
docs/
  manual/         User manual (.docx) and screen mockups/photos
  hardware/       Wiring notes, BOM, hardware modifications
  images/         Photos and diagrams used in this README and the manual
LICENSE.md        Project license (CC BY-NC-SA 4.0)
README.md         This file
```

## Building one

1. Flash `firmware/BrakeBox.ino` to a Waveshare RP2040-Zero using the
   Arduino IDE (Adafruit TinyUSB stack, "2MB (Sketch: 1920KB, FS: 128KB)"
   flash partition scheme).
2. Wire the hardware as described in `docs/hardware/`.
3. Apply the HX711 80SPS modification above.
4. Power on, follow the on-screen calibration steps.

See the full [user manual](docs/manual/BrakeBox_User_Manual.docx) for
day-to-day use once it is built.

## License

This project is licensed under **CC BY-NC-SA 4.0** — see
[`LICENSE.md`](LICENSE.md) for details. In short: you are welcome to build
one for yourself, modify it, and share your modifications under the same
license, but selling assembled units or kits based on this project is
reserved to the original author. Get in touch if you are interested in a
commercial arrangement.

## Status

Actively developed as a hobby project. Issues and pull requests
(documentation fixes, hardware notes, firmware improvements) are welcome —
see `LICENSE.md` for the terms any contribution is shared under.
