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
the visible UI clock remains the real PC clock. Fixed colours select a background palette only; icon material
uses the actual/diagnostic calendar. The previous implementation incorrectly substituted the background month
into both layers. Fixed-colour rendering now has an opaque native path; see CATEGORY-ICON-FIX.md.
Updated 2026-10-26: every built-in colour, including Original, now uses that opaque path; the glass program is
diagnostic only (`src/icons.cpp:97-109,164-166`).
Set `XMB_ICON_LOG_MATERIAL=1` with `XMB_LOG=<local log>` to record the first derived material values.

Remaining user-reported requirements:

Status note, 2026-10-25: the brightness concern below is historical. HANDOFF-PHASES.md now records that the large
night/day mismatch came from comparing different console hours. The remaining fitted day curve is being refined
by another session; use the current phase handoff for that status, rather than applying an extra darkness gain.

- The background is brighter than the RPCS3 reference at the same hour. Scene encoding, exposure, day curve,
  presets, palette orientation and tone-map composition need matched-state evidence. No arbitrary darkening
  multiplier was introduced. This also affects the icon refraction input.
- Updated 2026-10-26: item icons in the open column no longer use Qt when they are icontex items - they queue to
  `IconRenderer` with the opaque material (`src/view.cpp:949-952`); baked RCO, artwork and theme images still use
  QPainter. Their shader/asset bindings must be identified before passing them to the glass shader.
- Native rasterizer/glow parity, RCO runtime layout and later phases remain open in HANDOFF-PHASES.md.

## Runtime ICONS.mnu reads (P1) - 2026-10-27

`src/icons.{h,cpp}` no longer parses MNU. The private `readMnu()` (the 12-line split(':') parser) and the four
`QHash<QString,float>` preset tables are gone; `IconRenderer::init` now keeps four `const Materials::Mnu*` from the
scene's shared reader:

```cpp
day_=&sceneMaterials(assets).icons("day");      // override/day/ICONS.mnu
night_=&sceneMaterials(assets).icons("night");
yoake_=&sceneMaterials(assets).icons("yoake");
higure_=&sceneMaterials(assets).icons("higure");
```

so the app has one MNU parser, one cache and one fallback rule (`number(key, fallback)`); `value()` is unchanged
in behaviour: night -> day -> the four-preset keyframe blend, `XMB_ICON_PRESET_LINEAR` still forces the old
day/night cross-fade. If `override/night/ICONS.mnu` is unreadable the renderer stays unavailable (logged), exactly
as before; if only yoake/higure are missing the day/night pair covers those keys.

**The values are identical, measured.** With `XMB_ICON_LOG_MATERIAL=1` at the canonical pinned clock
(`--background-clock 2026-10-25T10:00:00`), the pre-change and post-change builds log the same derived material:

```
icons: derived material "2026-10-25T10:00:00" ChangingColor QVector3D(0.904533, 0.852625, 0.646848)
       TIME COLOR MIX 0.503418 GLASS 0.138197 luminance coefficients -0.376329 0.231954
```

`TIME COLOR MIX`, `GLASS` and the derived luminance coefficients all come from ICONS.mnu keys, so this is a
byte-level check that `Materials::icons()` returns what the old parser returned, not just a lookalike. The
category-icon layer moved +0.03 dB / +0.0024 SSIM in the canonical capture only because the frame behind the glass
icons changed (the dust, `PARTICLES.md` 5), not because the material changed.

### Constants that stay in `icons.cpp/h`

`// captured:` (firmware bytes, not through an MNU key):

| constant | value | source |
|---|---|---|
| `kDistance`, `kScaleX`, `kScaleY` | 2700, 2.8125, 5 | steady-state camera of the captures (the `_Eye` matrix) |
| `kLuminanceX0/X1/X2`, `kLuminanceY0/Y1` | .20443, .544441, 1, -.736614, -.259981 | FUN_0003c584's three-point Lagrange control points |
| `kEnvStartDegrees`, `kEnvTurnPerSecond` | -147, 30 | `_EnvRot` turn in the captures |
| `vc[258]` z offset | 19.95 | the icon quad's firmware matrix |
| `vc[466]` CookieCutterRatio | 15, 8.4375 | the 128x128 px window as a screen fraction |
| `Icon::size/brightness/distance` defaults | 117.13 / .698 / 2700 | RPCS3 captures (focused 167.33, 1 / 2857.2 behind a page) |

`// fit:` (no ICONS.mnu key):

- the opaque material's ambient/diffuse/bevel (`0.58 + 0.26 * diffuse + bevel`, bevel `0.10`) and its
  `mix(0.70, 1.0, brightness)` factor - the glass terms (refraction, env map, tone curve, EXPOSE, LIT2
  specular) have no opaque counterpart, so the stand-in stays until every icon is drawn through the glass path;
- `tintStrength` 0.12 in `setFixedTint` - the tint weight for that stand-in;
- the `0.25` scale in the `XMB_ICON_DEBUG=4` diagnostic and the `128x128` cut are diagnostics/captures
  respectively, not material values.

`MATERIAL` note: `LIT1/LIT2 ANG Z/X`, `ATTN *`, `AMB *`, `REFR *`, `EXPOSE`, `GLASS`, `TIME COLOR MIX`, `PAR BIAS/
SCL`, `SPEC1/2 POW` and every other key `value()` reads now come straight out of the four MNUs; none is typed in
this file any more.

### Reproduce

```
XMB_ICON_LOG_MATERIAL=1 XMB_LOG=<local log> re-ps3-native --dev-flash <dev_flash> \
    --background-clock 2026-10-25T10:00:00 --render-clock 30 --script full,wait,quit
```

still logs the first derived material; `--background-clock` fixes both the preset blend and `amb.bmp`'s time.
The only remaining hunk on this surface is View's: `icons_.setClock(backgroundOverride_.isValid() ? ... :
QDateTime::currentDateTime())` already passes the right clock, so nothing else changes.
