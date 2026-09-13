# BrakeBox — HX711 80SPS Hardware Modification

**This modification is required.** Without it, brake input lags by
100ms or more, which is very noticeable in use.

## Background

The HX711 can sample at either 10SPS (samples per second) or 80SPS,
selected by the voltage on its RATE pin (pin 15). Most breakout boards
expose a solder-jumper pad intended to select this, but on the boards
used for this build, that pad did not actually change the sample rate as
documented — it stayed at the slow 10SPS default regardless of the
jumper's state.

## The fix

Solder a **100Ω resistor from HX711 pin 15 (RATE) directly to VCC**,
bypassing the board's own rate-select pad entirely.

- Pin 15 (RATE) is usually a small pad or through-hole near the crystal
  on the HX711 module; check your specific board's silkscreen/datasheet
  to confirm which pad is RATE before soldering.
- VCC is the same 3.3V rail already feeding the rest of the module.

## Confirming it worked

After the mod, brake input should feel immediate rather than laggy. If
you want to confirm numerically rather than by feel, you can add a
temporary `Serial.println(millis())` around each `scale.get_units()`
call and check the time between readings — at 80SPS you should see a
new reading roughly every 12.5ms, versus roughly every 100ms at 10SPS.

## If your board's rate-select pad does work

Some HX711 boards do implement the documented pad correctly. If yours
does, you may not need this resistor — check for a visible change in
read frequency before assuming you need the mod. When in doubt, the
resistor mod is safe to apply regardless; it simply forces RATE high
directly rather than relying on the board's own (possibly
non-functional) jumper.
