# Icon material clock inputs — 2026-10-25

The category shader no longer pins `_ChangingColor` RGB and luminance coefficients to a captured night frame.
This corrects two known inputs; it does not establish full daytime parity or fix background brightness.

## ChangingColor

`IconRenderer` reads `icons.qrc` → `widgets/other/amb.bmp` through the existing local asset service. The palette
is a 16×12 indexed BMP. Qt decodes it to logical top-down RGB; do not flip it again for CPU sampling.

Firmware inputs are seconds since midnight and seasonal month position. Sample repeat-wrapped bilinearly:

```text
u = secondsOfDay / 86400
season = month - 1 + (day - 1) / monthDays
v = (season + 0.5) / 12
x = 16*u - 0.5
y = 12*v - 0.5
ChangingColor = (sample(amb, x, y).rgb / 255, TIME_COLOR_MIX)
```

The firmware's February calendar uses 28 days and repeats February 28 for leap day. Other months use their
calendar length. Both palette axes wrap; there is no added gamma transform.

Evidence: local `custom_render_all.c` lines 4839–4842, 4873–4896, 4962 (clock/date); 23083–23233 (bilinear);
20128–20150 (repeat wrap); 23040–23077 (channel conversion). Raw custom-render instructions 0x449cc–0x44a00
add the half-row offset omitted by the decompiler. At 0x44fdc the BMP is loaded; 0x44fec calls
`qgl_base_96B9D42E`. Its qglbase code at 0x3c160 swaps rows (indexed8 stride at 0x3c2f8, swap loop
0x3c214–0x3c288). No firmware/code extracts are committed.

Independent corroboration: at the existing capture's creation time, October 24, 23:40:10, the formula gives
`(.8093611638, .6995572423, .3530676787)`, matching the captured `(.809361, .699557, .353068)` to its printed
precision. The opposite row orientation does not match. Capture creation time alone is not a general guarantee
of emulated console time; here the agreement corroborates the static trace.

## LuminanceParams

The firmware constructs a three-point Lagrange curve (`FUN_0003c584`):

```text
x0=.20443, x1=.544441, x2=1
y0=-.736614*GLASS*b, y1=-.259981*GLASS*b, y2=0
Luminance[0] = (x1,x2,y0/((x0-x1)*(x0-x2)),0)
Luminance[1] = (x0,x2,y1/((x1-x0)*(x1-x2)),0)
Luminance[2] = (x0,x1,0,0)
```

`b` is the scalar envelope at renderer offset 0x320, initialized to one and updated each frame as a separate
animation. Native currently uses its steady state, one. It is not `TIME_COLOR_MIX` or a second day/night blend.

| Preset | GLASS | Coefficient 0 | Coefficient 1 |
|---|---:|---:|---:|
| Night | .0624733 | -.1701230 | .1048572 |
| Day | .152523 | -.4153402 | .2560000 |
| Dawn/dusk source presets | .0952192 | -.2592944 | .1598190 |

The night row agrees with the existing capture. Native currently interpolates day/night GLASS through its
existing preset weight; selecting independent dawn/dusk/boot overrides and reproducing the envelope remain open.

## Reproduce and next work

`--background-clock 2026-10-24T23:40:10` fixes background palette/preset and icon-material time for comparison;
the visible UI clock remains the real PC clock. The same selected fixed-colour month is passed to both layers.
Set `XMB_ICON_LOG_MATERIAL=1` with `XMB_LOG=<local log>` to record the first derived material values.

Remaining user-reported requirements:

Status note, 2026-10-25: the brightness concern below is historical. HANDOFF-PHASES.md now records that the large
night/day mismatch came from comparing different console hours. The remaining fitted day curve is being refined
by another session; use the current phase handoff for that status, rather than applying an extra darkness gain.

- The background is brighter than the RPCS3 reference at the same hour. Scene encoding, exposure, day curve,
  presets, palette orientation and tone-map composition need matched-state evidence. No arbitrary darkening
  multiplier was introduced. This also affects the icon refraction input.
- Item icons in the open column still use Qt. Some are icontex normal maps and others are baked RCO images;
  their actual shader/asset bindings must be identified before passing them to the glass shader.
- Native rasterizer/glow parity, RCO runtime layout and later phases remain open in HANDOFF-PHASES.md.
