# Phase 2 — native text renderer and firmware trace

Updated 2026-10-25. The GL atlas and recovered FreeType glyph-load path are implemented; pixel parity with PAF is still open.
See `FONT-MATCHING.md` for the latest bridge evidence, verification and next phase.

## Implementation

`src/text_renderer.cpp` shapes text with QTextLayout, retains each run's actual QRawFont (including fallback
faces), and caches grayscale glyph coverage at the display's native pixel size. It uploads padded coverage
and a separate Gaussian halo to GL atlas pages. Consecutive glyphs on the same page are batched in shaped
order; the shader produces premultiplied coverage and halo after a separate whole-string shadow pass. Labels are drawn in the original scene
order through QPainter native-paint boundaries, so dialogs still dim the elements behind them.

Menu labels, captions, subtitles, dialog/information text and the OSK use the atlas. Original font faces now
use FreeType outline coverage with recovered flags 10; head+cmap table identity selects the source file.
Qt shaping and the temporary elision/cursor metrics remain; this is not a complete port of PS3 libfont.
Qt coverage is a logged fallback for unmatched faces, and Qt text drawing remains a shader-failure fallback.

Native pixel size is rounded from the logical font size and viewport scale. Resizing invalidates atlas and
shaped-layout caches. Coverage is grayscale, with no added gamma curve. No hinting and no embedded bitmap
are recovered firmware load settings, now used by FreeType. The measured halo uses sigma 2.2 and a ~6-pixel radius at
1080p, separately from the glyph edge. Shadow uses a provisional one-scene-pixel right/down offset, black at
20% alpha. The earlier direct use of RCO template values (X=4, Y=-5, grey RGB=.25, full alpha) produced a visible
dark duplicate. Their runtime conversion remains unresolved. None of these values should be described as firmware defaults.

Bounds: sixteen 1024-square RGBA pages (64 MiB maximum), 512 shaped layouts, clearing at a frame boundary
when full. Space glyphs retain their shaped advance without allocating a tile. Uploads reset alignment,
row length and skipped rows/pixels: Qt leaves sub-image unpack state behind, which otherwise corrupts
later glyphs. Atlas/program/buffer objects are released while the View context is current.

## Evidence from firmware 4.93

Read-only analysis of the existing local `vsh.elf` Ghidra project. SHA-256:
`54f6216dd498da243132377b830fa17c74cbe564ec8b8ebd40c0a2b071a525fa`.
Derived decompiles remain local under `out/native-reference`; none are committed.

- PAF glyph drawing is `FUN_0037a774`. Glyph x/y use 26.6 fixed point, converted by 1/64.
- Font setup uses 72 DPI; a zero point size falls back to 7.02. This does not establish the final screen size.
- `FUN_004056d8` copies 8-bit coverage to white RGBA, retaining the maximum alpha where masks overlap.
  `FUN_004057a0` does the equivalent for an alpha-only surface. These copies apply no gamma conversion;
  rasterizer and later-compositor gamma remain unknown.
- PAF applies numeric weight/slant effects in addition to selecting font faces. The stored weight effect is
  `clamp(weight - 1, -0.07, 0.04)`. The style enum names are unresolved:

| PhFont style field | Weight | Slant |
|---|---:|---:|
| default | 1 | 0 |
| 2, 4 | 1 | 0.6 |
| 5, 0x68 | 1.025 | 0 |
| 6 | 1.025 | 0.6 |
| 7, 0x69, 0x6A | 1.04 | 0 |
| 8 | 1.04 | 0.6 |
| 0x65 | 0.93 | 0 |
| 0x66 | 0.96 | 0 |

Do not apply one weight effect universally. Current per-glyph alpha-over compositing also differs from
the firmware's max accumulation for overlapping coverage; this matters for combining/overlapping glyphs.

## RCO Text attributes for Phase 3

Offsets below are relative to a Text object's extra payload, following its 40-byte entry header. Source:
local `objattribdef-ps3.ini` Text section; independently checked against compressed original RCO bytes.

| Offset | Field |
|---|---|
| 0x060 | Font reference, eight bytes |
| 0x070 | Size |
| 0x074–0x088 | Top RGB, bottom RGB |
| 0x08C | Horizontal spacing |
| 0x09C | Vertical spacing |
| 0x0A0, 0x0A4 | Shadow X, Y |
| 0x0A8 | Shadow perspective |
| 0x0AC–0x0B8 | Shadow RGBA |

Across 1305 Text templates, 1304 use shadow X=4/Y=-5; all perspective values are zero. 1197 use shadow RGB=.25,
107 use .9 and one disables shadow. All horizontal spacing values are zero. In `explore_plugin_full.rco`,
`text_info_msg` at decompressed-table offset 227072 has shadow `(4,-5,0,.25,.25,.25,1)` and template size 10.
These are template coordinates, not proven output-pixel offsets. Runtime setters can override them.

Local evidence exports: `text-string-refs.c`, `text-font-wrappers.c`, `text-font-lowlevel.c`,
`text-family-map.c`, `text-ui-refs.c`. The REA MCP PPC route was unavailable; the installed Ghidra CLI
provided the existing project's exports with `-readOnly -noanalysis`. No RPCS3 capture/input was used.

## Rasterisation scale fix (2026-10-25)

The UI layer is drawn at twice the window size and resolved with a 2x2 box filter. Glyphs, however, were rasterised
for the window's own scale, so every glyph was magnified 2x by bilinear sampling before the resolve, and all text
looked soft. They are now rasterised at the layer's scale, one texel per layer pixel. Each glyph's origin also snaps
to whole window pixels (two layer pixels), so stems do not straddle two output pixels. On the clock and the "Game"
label, peak horizontal edge contrast in a 1080p grab rose from 127 to 173. Atlas pages fill about four times faster;
the 16 pages of 1024² still hold a full menu.

## Label timing observed by MaXMB (2026-10-25)

MaXMB (https://github.com/MaxiBono/MaXMB, MIT) draws its text with browser CSS: a bundled Rodin TTF with pinned
vertical metrics, plus text-shadow glows. Neither its rasterisation nor its metrics apply here, because this renderer
uses the firmware fonts. Its label rules record two behaviours its author observed on a console, and both are now
implemented in `view.cpp`:

- **Breathing glow.** The focused item's label glow cycles every 3.4 s (ease-in-out). It runs from a faint halo
  (about 0.35 of the peak) to the full bloom, starting faint when the row takes focus. The peak stays the static
  halo measured from the reference crop.
- **Idle fade.** After 4 s on one row of Photo/Music/Video/Game/Network/PSN (top level), the other rows' titles
  fade out over 0.8 s. They return when the focus moves.

Neither is confirmed on our own references yet: the 19:35 recording and the 40-48 s timing clip never rest on a
row. Native grabs at about 0.3 s, 1.8 s and 5.4 s after a move show the faint and full glow phases and the faded
rows (the row below the focus reads 68.6 against a background of 69.0). The UI layer redraws when the glow phase
(24 steps) or the fade (16 steps) changes. A capture resting on one row for 8 s or more would settle both.

## Build and capture workflow

Use a separate output directory while Phase 1 work shares the checkout:

```powershell
& ./native-xmb/build.ps1 -OutputDirectory ./out/native-phase2
```

The normal output directory stays the default. `--script` now supports `size=1920x1080` and `size=960x540`
in addition to keys, waits and grabs. Use an isolated `--data-dir`; keep captured images/logs outside Git.
Local observations and remaining visual checks are recorded in HANDOFF-PHASES.md.

The visible scripted run exited zero with a 1920x1080 root capture and 960x540 category/dialog/OSK/resize captures,
zero missing images, and five-second frame reports of 46.7, 61.3 and 61.5 fps during that mixed-resolution run.
This is not a 1080p performance or reference-parity result. A capture-helper bug then found was that `full`
did not update the shell's fullscreen state, so navigation immediately restored the window; the token now
uses the shell setting, and `size=` restores windowed state through the same setting.
The follow-up run confirmed all four root/network/dialog/OSK grabs at 1920x1080 and exited zero; its five-second
reports were 48.7 and 56.1 fps. Sustained 1080p frame pacing remains below the project's completion requirement.

## Remaining work

### Visible shadow correction (2026-10-25)

Computer Use reproduced heavy, offset dark text behind the clock and labels in the user's Green theme.
The renderer had treated RCO template coordinates as final scene distances without evidence for that conversion.
It also composited each glyph's shadow and foreground together, so a later glyph's shadow could overpaint an
earlier glyph's foreground. The correction draws all shadow batches before all foreground batches, including
strings spanning atlas pages, and uses the restrained provisional shadow described above. Firmware font
selection, shaping, coverage rasterisation, hinting and focus halo are unchanged.

Both the isolated and default native builds succeeded. Existing scripted replay produced before/after root
captures at 1920x1080-requested and 960x540 window sizes, followed by a dialog and Friends column. The dark
duplicate is absent in the after captures; the clock, long focused label, dim labels and dialog remain readable.
Diagnostic artifacts are local under `out/native-reference/text-fix/`; captures retain the live UI clock, so
before/after timestamps differ. This verifies the reported visual defect, not PS3 rasterizer pixel parity.

1. Recover rasterizer version, subpixel transforms and style-code-to-face mappings. Match focused/unfocused/subtitle/dialog
   crops against the same reference state; readable native output alone does not prove pixel parity.
2. Recover runtime glow/shadow overrides and coordinate scaling; implement RCO gradients and spacing.
3. Reproduce max-coverage mask accumulation for overlapping glyphs without losing shaping/fallback faces.
4. Replace temporary Qt elision/cursor metrics once the firmware metric path is reproduced.

The user also identified material issues: clock inputs have since been traced and implemented (see
ICON-MATERIAL-CLOCK.md); item icons in the open column and a background that is too bright relative to RPCS3
remain open. Correct background sampling before tuning unfocused icons.

## Rasteriser-level work: pen phase, coverage union, glow product, elision rule (2026-10-26)

Bounded to `src/text_renderer.cpp`, `src/text_renderer.h`, `src/font_raster.cpp`,
`src/font_raster.h` and this section. `view.cpp` and `shell.cpp` were read, never edited.

Build and score, exactly:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-text
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-text/re-ps3-native.exe
```

Every number below is from that command on this machine, one capture per configuration. Baseline
reproduced bit-exactly before any edit (frame 18.02/0.8301, category icon row 21.05/0.8950, item label
row 11.94/0.3910, clock text 13.51/0.6131, info bar 17.12/0.7046).

### 1. Pen phase: quantised before this change; implemented; measured an improvement on both clock layers

What the native path did: `view.cpp`'s `text()` hands the renderer a scene rect, `draw()` mapped each
glyph's tile origin into layer pixels and snapped it to `pixelGrid_` (2 layer pixels = 1 output pixel).
With the UI layer at twice the window, that quantises every pen position to whole output pixels, so the
fraction of PAF's 26.6 pen never reached the rasteriser even though `text_renderer.cpp` already had the
sub-pixel phase in hand after `QTextLayout` shaping.

Implementation, all in the two owned source pairs:

- `FontRaster::rasterize(font, glyph, flags, QPoint delta26_6)` installs `FT_Set_Transform` with an
  identity matrix and that delta before `FT_Load_Glyph`. This is the mechanism `cellFontFT_65E27CB3 @
  0x6aa4` uses: both pen phases are masked with `0x3f` and the transform is set at `0x2193`/`0x2222`
  (`TEXT-RASTER.md` 1.3). The delta is re-installed only when it changes, and every rasterisation states
  its own, so call order cannot leak a previous phase. `advance()` clears it for the same reason.
- Because FreeType renders the translated outline, the returned `bearing` is the shifted
  `bitmap_left`/`bitmap_top`; the tile origin is `floor(pen) + bearing` and the fraction lives in the
  coverage. No separate UV offset is needed and the shadow sample stays consistent.
- Cache: a glyph's identity is `(index << 4) | (phaseY << 2) | phaseX` with `phaseSteps = 2` per
  layer pixel on each axis (phase 0 and 0.5 layer px, i.e. four pen positions per output pixel at the
  2x layer). Worst case is 4x the glyph tiles; the key is a pure function of the pen. When the 16
  atlas pages fill, `glyph()` falls back to the phase-0 tile rather than dropping the glyph from the
  string, and logs one warning.
- Byte-identical across two runs of the canonical capture (`SHA-256 43C230D089B7E37E...` both times),
  so the path is deterministic for `--render-clock` captures. No atlas fallback warning occurred in
  the canonical state, so the 16 pages still hold the menu at 4x tiles.

Measured on the canonical capture, same executable, one run per configuration. The first attempt
phased x only and kept y snapped, which measured neutral-to-negative; the vertical quantisation
dominated. The final configuration phases both axes:

| layer | snapping (XMB_TEXT_PHASE=0) | x-phase, y snapped | two-axis phases (default) |
|---|---|---|---|
| frame | 18.01 dB / 0.8301 | 18.01 / 0.8301 | 18.01 / 0.8301 |
| top background | 20.24 / 0.9344 | 20.24 / 0.9344 | 20.24 / 0.9344 |
| category icon row | 20.91 / 0.8947 | 20.91 / 0.8947 | 20.91 / 0.8947 |
| item label row | 11.94 / 0.3910 | 11.94 / 0.3913 | 11.94 / 0.3913 |
| clock text | 13.51 / 0.6131 | 13.50 / 0.6119 | **13.66 / 0.6198** |
| info bar | 17.12 / 0.7046 | 17.11 / 0.7037 | **17.26 / 0.7098** |

Verdict: two-axis phases are a measured improvement on exactly the two layers that contain the clock
string (+0.15 dB / +0.0067 SSIM on clock text, +0.13 dB / +0.0052 on the info bar), with no layer
worse. They are the default; `XMB_TEXT_PHASE=0` restores the previous whole-output-pixel snapping and
reproduces 13.51/0.6131, 17.12/0.7046 and 11.94/0.3910 bit-for-bit on the text layers.

This is worth noting against `TEXT-RASTER.md` 2.4, which said not to implement sub-pixel phases on
the evidence then available. The reference now matches the per-phase outline translation better than
the rounded-origin-only model, so the LIKELY reading of section 1.3 is supported by measurement as
well. It is still not proven by the transform operands, so the switch stays as the way back.

Note on the category icon row: it reads 20.91 rather than the stored 21.05 because `icons.cpp` and
`view.cpp` in this shared checkout were modified after the stored feature build was made (source
mtimes 20:59 and 21:00 against exe mtime 20:20); every text layer is pixel-identical to the
pre-change build, and the differing pixels are confined to the icon band x 112..1212, y 246..325.
Enabling phases changes every native render, so the named-state self-baselines need
`... --states --save-reference` before those gates pass again.

### 2. Coverage union: today it is alpha-over, not keep-larger; helper and evidence in place; the draw path needs a shared mask

Verified: `draw()` still emits one textured quad per glyph with
`glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` and `GL_FUNC_ADD`, so two glyphs whose coverage overlaps
compose by alpha-over. The firmware's copy rules overwrite only when the new byte is larger
(`FUN_004057a0 @ 0x4057a0`, `FUN_004056d8 @ 0x4056d8`), which is a saturating union. So the native
composition does not follow it.

What was added: `unionCoverage()` in `text_renderer.cpp` is the literal keep-larger copy, and
`XMB_TEXT_CHECK=1` runs deterministic assertions over it. Output from the current build:

```
text check: PASS union-rule overlapping coverage 90 px, keep-larger below alpha-over at 41 px
text check: accent pair e+U+0301 overlap 0 px at one pen
text check: PASS metric-authority PAF advance 147.19 px vs Qt shaped 147.13 px at 21 px font
```

Two findings from that. First, on overlapping coverage the two rules genuinely differ (41 of 90 shared
pixels are darker under keep-larger), so this is a real behavioural difference, not a wording one.
Second, `e` plus U+0301 at one pen share zero coverage pixels, so a combining accent in Rodin does not
touch its base: for ordinary accented pairs the union rule is inert, which is why nothing in the
canonical capture can measure it (no overlapping coverage pair occurs in the English UI).

Not wired into composition, deliberately. Doing it inside the renderer needs a shared mask, and the
contract is now exact:

1. Add an R8 `GL_TEXTURE_2D` mask target of `target_` size plus an FBO, created in `beginFrame` when
   `target_` changes and released in `release()`.
2. In `draw()`, when a string's glyph boxes actually intersect, scissor to the string's layer-space box,
   clear it to 0, and draw every glyph quad of the string into the mask with `glBlendEquation(GL_MAX)`
   (valid for a single-channel target) using the existing vertex layout and a coverage-only fragment
   output.
3. Composite the mask once over the UI layer with the existing tint and `GL_ONE/GL_ONE_MINUS_SRC_ALPHA`,
   and let the shadow pass sample the same mask at the offset instead of per-tile `shadowBounds`.
4. Decide halo membership explicitly: the halo channel is a linear blur, so a max-union of per-glyph
   halos is not the same image as one blur of the unioned coverage. The firmware gives no evidence
   either way (`TEXT-RASTER.md` 1.5 leaves the scope of the destination mask unproven), so either keep
   the halo per tile or accept the change and measure it.
5. On the QPainter fallback path, `QPainter::CompositionMode_Lighten` is the closest built-in per-channel
   max.

No `view.cpp` wiring is involved in any of that; it is renderer-internal state.

### 3. Glow: already a text-renderer product; what is left is view-level timing

The halo is already generated from the same glyph at raster time: `tileFor()` writes coverage into the
tile's red channel and a Gaussian of that coverage into green, with sigma `2.2 * rasterScale` truncated
at 3 sigma, inside a pad of `ceil(6 * rasterScale) + 2` layer pixels. The fragment shader reads it as
`halo = clamp(coverage.g * glow * 1.5, 0, 1) * tint.a` and composites `tint.rgb * fill + (0.922,
0.961, 1.0) * halo * (1 - fill)`. So the shape, the falloff and the tint are renderer products; nothing
about them needs `view.cpp` beyond the strength value.

The contract `view.cpp` must keep (and which the renderer relies on):

- Focused label = two calls in this order, as today at `view.cpp:773` (the `glow` lambda), `:861` and
  `:957`: `draw(rect, text, font, white, flags, strength, /*shadow*/false, /*ink*/false)` for the halo,
  then `draw(rect, text, font, labelColour, flags, 0, /*shadow*/true, /*ink*/true)` for the ink. Both
  calls hit the same atlas tiles, so the second pass costs no extra rasterisation or atlas space.
- `strength` is the peak fraction in [0, 1] and the renderer multiplies it by 1.5; the breathing curve
  (`0.35 + 0.65 * (0.5 - 0.5 * cos(2*pi*t/3400))`, `view.cpp:956-957`) and the idle label fade
  (`view.cpp:943-944`) are view timing and stay where they are. No firmware value exists for either
  (`TEXT-RASTER.md` 3.2), so the renderer must not bake them in.
- A single combined glow+ink call would need a mask to keep a later glyph's halo off an earlier glyph's
  ink, i.e. the same shared-mask work as section 2. Until then the two-call order is the contract.

### 4. Elision: firmware shape implemented as a pure renderer helper; `view.cpp` wiring listed, not applied

Firmware evidence is unchanged (`TEXT-RASTER.md` 4.2): PAF draws a character range only, and
explore_plugin positions a separate `label2_text_ellipsis` Text object after the last visible character.
So the rule is metric plus a second draw, not a substituted U+2026.

Added `TextRenderer::elideToWidth(font, text, maxWidth)` and `measure(font, text)`. Both shape exactly as
`draw()` does (layer-scale pixel size, `PreferNoHinting`, same fallback faces, `mapSymbols` applied) and
return scene-unit widths, so the prefix width and the drawn glyph positions cannot disagree. The helper
reserves the ellipsis advance, takes the widest whole-glyph prefix that fits, and refuses to split a
surrogate pair or detach a combining mark from its base. It touches no GL state, no atlas page and no
cache. Deterministic assertion (`XMB_TEXT_CHECK=1`, same run as section 2):

```
text check: PASS elision-fits prefix=Corrupted Data width=206.53 elided=0
text check: PASS elision-width prefix="Corr" width=57.38 + ellipsis 27.00 <= 100
text check: PASS elision-empty empty=0
text check: all assertions passed
```

The second line is the unit check of the width rule: at a 100 px limit the prefix is "Corr" (57.38 px),
the ellipsis is 27.00 px in Rodin at 21 px, the sum stays inside the limit, and the next character would
exceed it. Note the ellipsis is a full em in this face, which any future elision budget must account for.

Wiring `view.cpp` needs, at `:951`:

```cpp
const QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(27);
const auto elided=textRenderer_.elideToWidth(labelFont,entry.title,value.isEmpty()?1050.:640.);
const auto label=elided.elided?elided.prefix:entry.title;
...
text(labelRect,label,27,QColor(255,255,255,int(255*alpha)));
if(elided.elided)
    text(QRectF(labelRect.left()+elided.ellipsisX,labelRect.top(),40,labelRect.height()),
         QString(QChar(0x2026)),27,QColor(255,255,255,int(255*alpha)));
```

and keep `QFontMetrics::elidedText` only as the QPainter fallback branch. This was not applied because
`view.cpp` is outside the write fence. It also cannot change the canonical capture: no label at 1050 px
or 640 px overflows in that state, so the prefix equals the full string and nothing is elided.

### 5. The clock layer's gap is string content, not the text pipeline

`elideToWidth` and the phase work both pointed at the clock text, so the metric source was measured
instead of assumed. With both sides at the same pixel size, the firmware's unhinted 26.6 advance sum and
Qt's shaped width agree:

```
text check: PASS metric-authority PAF advance 147.19 px vs Qt shaped 147.13 px at 21 px font
```

So there is no metric-source gap at this size, and the clock layer's 13.51 dB is not an advance-width
error. What it is, measured on the two frames:

| quantity | reference | native |
|---|---|---|
| clock glyph bbox width | 169 px | 185 px |
| gap between "25/10" and "12:32" | 14 px | 24 px |

`Shell::clockText()` builds `date + "  " + time` with two spaces (`settings_menu.cpp:185`) while the
reference shows one. One space is about 13.4 px at 27 px, which accounts for almost all of the 16 px
width difference and for the 10 px word gap. This is a `settings_menu.cpp`/`view.cpp` change (one
character in the format string), outside this task's write fence, and it is the largest single text
difference left in the clock layer.

Updated 2026-10-26: the change above landed. `Shell::clockText()` is now one date-time gap and the
clock defaults to MM/DD with a 12-hour time (`src/settings_menu.cpp:19-21`, `Shell::clockText`),
so the "native" column in the table is the earlier DD/MM + 24-hour, two-space render and no longer
describes the default. Against the DD/MM reference frame the remaining clock-band difference is string
content (date order and the 12-hour marker), not the text pipeline; setting DD/MM and 24-hour back in
preferences reproduces the reference string (INFO-BAR.md:16-25, :68-71).

### 6. Summary

| item | state | measured effect | owner if not closed |
|---|---|---|---|
| Pen phase | Per-axis phase tiles rasterised at the pen phase, keyed and bounded; default on | clock 13.51 -> 13.66 dB, 0.6131 -> 0.6198 SSIM; info bar 17.12 -> 17.26 dB; no layer worse | closed; `XMB_TEXT_PHASE=0` is the bit-exact way back, named-state baselines need refreshing |
| Coverage union | Verified native is alpha-over; keep-larger helper plus assertions in place; composition needs a shared mask | not measurable in the canonical state (no overlapping coverage pair); e+U+0301 overlap is 0 px | renderer-internal shared-mask pass, contract in section 2 |
| Glow | Already a renderer product (second tile channel, Gaussian, same glyph) | unchanged numbers by design | view-level breathing/fade timing only |
| Elision | Pure helper plus deterministic assertions; not wired | none (no label overflows in the capture) | the label loop in `View::paintGL` as listed in section 4 |
| Clock metric | Metric authority agrees with PAF at equal size | n/a | one-space clock format done 2026-10-26 (`Shell::clockText`) |

Open, in order of expected value for the clock and item label layers: the shared-mask union if a state with
accented or overlapping glyphs is ever gated. The clock string's second space and the date-time gap were
fixed on 2026-10-26 (section 5). The
per-phase tiles change every native render, so refresh the named-state self-baselines with
`bun work/ab-layers.ts --states --save-reference` (a deliberate step for a rendering change, not a
fidelity claim). Their 55 dB self-baseline still catches accidental drift afterwards.

## Coverage union implemented: per-string R8 mask with GL_MAX (2026-10-27)

Supersedes the "not wired into composition" status in section 2. The firmware copy rule is unchanged
(`FUN_004057a0 @ 0x4057a0`, `FUN_004056d8 @ 0x4056d8`; `TEXT-RASTER.md` 1.4-1.5). This section records the
implementation, the mode rule, the assertion that proves the union, the per-layer numbers and the
frame-time comparison.

Files touched: `src/text_renderer.h`, `src/text_renderer.cpp` and this section. `src/view.cpp` and
`src/view.h` were read and did not need to change: `View::paintGL` already hands the renderer the UI
layer size (`textRenderer_.beginFrame(*this, pixels, ...)`), and the renderer already receives the
painter transform in `draw()`, so the mask is renderer-internal state exactly as the section 2 contract
foresaw. The public `text(...)`/`glow(...)` API is unchanged.

### 1. Design

- Target: one `GL_R8` texture plus FBO at `target_` size, i.e. the UI layer's own size. Created on the
  first masked string and recreated only when `target_` changes (2.07 MB at the 1920x1080 layer,
  8.29 MB at 3840x2160), `GL_NEAREST`, `CLAMP_TO_EDGE`, no mipmaps, deleted in `release()`.
- Per string: scissor to the string's ink box grown by the shadow reach, `glClear` to 0 inside that
  scissor only, then draw every glyph quad already built for the string with a coverage-only fragment
  shader (atlas red channel) and `glBlendEquation(GL_MAX)`, `glBlendFunc(GL_ONE, GL_ONE)`. That is a
  bytewise compare-and-keep-larger on the R8 channel: the firmware copy rule, applied once per
  string instead of per glyph.
- One composite: a single quad over the box, appended after the glyph vertices, sampled 1:1 with
  `GL_NEAREST` (`uv = layer/target`, v flipped because the mask is rendered with the same top-down
  vertex transform). The shadow samples the same mask at `-reach/-reach` (`reach =
  round(rasterScale_)`, so one scene pixel as before), and ink is `tint.rgb * coverage * opacity`,
  premultiplied and blended with the layer's existing `GL_ONE/GL_ONE_MINUS_SRC_ALPHA`. One FBO
  bind/restore per masked string.
- Halo membership is decided explicitly: the halo is NOT unioned. The atlas green channel stays a
  per-glyph Gaussian and the halo pass is unchanged, because max-unioning per-glyph halos is not the
  same image as one blur of the unioned coverage and the firmware gives no evidence for either
  (`TEXT-RASTER.md` 1.5, 3.2). A glow-only call (`ink=false, shadow=false`) therefore never masks; the
  two-call glow/ink contract in section 3 still holds.
- Bounded: the mask target is allocated on first use and on layer resize only; the vertex, batch and
  box vectors are members that grow to the widest string and are reused, so a steady frame does not
  allocate for the mask path. Draw order is unchanged, so the mask is a pure function of the string.
- Deterministic: the canonical capture is byte-identical across runs with the mask path active
  (SHA-256 `9C2A6CE9...32A0`, three runs, including the forced mode below).

### 2. When the mask is used

The per-glyph ink boxes are tight (nonzero coverage only, halo padding excluded) and stored with each
atlas tile, so the decision is "can two glyph inks share a layer pixel":

| mode | when | env |
|---|---|---|
| auto (default) | an ink/shadow string whose ink boxes intersect | unset |
| fast path | no box intersection, glow-only calls, strings with no inked glyph, mask unavailable | auto |
| forced | every ink/shadow string (A/B and timing) | `XMB_TEXT_MASK=1` |
| off | never, the per-glyph alpha-over result | `XMB_TEXT_MASK=0` |

The box test is conservative: it can select the mask for a pair whose boxes meet while the coverage
does not. That is safe (the mask then reproduces alpha-over) and it is what the canonical capture
exercises: the first masked string is the item label "Game Data Utility" (235x30 layer px, 15 inked
glyphs). `XMB_TEXT_CHECK=1` also votes the rule over five menu strings and reports 1 of 5 taking the
mask, which is the same label.

### 3. Per-layer numbers

Canonical capture, same command as section 1:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-mask
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-mask/re-ps3-native.exe
```

| layer | before this change (build `native-mask-before`) | after, auto (default) | delta |
|---|---|---|---|
| frame | 18.29 / 0.8295 | 18.28 / 0.8285 | -0.01 / -0.0010 |
| top background | 20.24 / 0.9344 | 20.22 / 0.9336 | -0.02 / -0.0008 |
| category icon row | 20.91 / 0.8947 | 20.89 / 0.8939 | -0.02 / -0.0008 |
| item label row | 11.94 / 0.3913 | 11.94 / 0.3909 | +0.00 / -0.0004 |
| clock text | 11.39 / 0.4105 | 11.39 / 0.4105 | +0.00 / +0.0000 |
| info bar | 14.78 / 0.6117 | 14.78 / 0.6117 | +0.00 / +0.0000 |

All six gates pass (budget 0.5 dB / 0.004). The small whole-frame and background deltas are not this
change: the shared checkout's particle sources (`particles.cpp/.h`, `p3t.cpp/.h`) were modified by
another task between the two builds, and the mask path is text-only. The clean attribution is the
same-binary A/B below, where the only variable is the mask mode.

Same executable, one capture per mode:

| mode | item label row | clock text | info bar | PNG SHA-256 |
|---|---|---|---|---|
| `XMB_TEXT_MASK=0` (fast) | 11.94 / 0.3909 | 11.39 / 0.4105 | 14.78 / 0.6117 | `9C2A6CE9...32A0` |
| auto (default) | 11.94 / 0.3909 | 11.39 / 0.4105 | 14.78 / 0.6117 | `9C2A6CE9...32A0` |
| `XMB_TEXT_MASK=1` (forced) | 11.94 / 0.3909 | 11.39 / 0.4105 | 14.78 / 0.6117 | `9C2A6CE9...32A0` |

Forced mode renders every ink/shadow string through the mask, including the shadow sampled from the
mask, and still produces the exact same 1920x1080 frame as the fast path. That is the strongest
available form of "the mask path does not regress the canonical capture": the union is inert in this
English UI state (no shared covered pixel), as `TEXT-RASTER.md` 1.5 and the earlier `e` + U+0301
measurement predicted, and the path is nevertheless exercised on a real label.

No layer improves here because nothing in the canonical state has overlapping coverage; an
improvement is only expected on labels with overlapping or accented glyphs, and none is present
(the accent pair `e` + U+0301 still measures 0 shared ink pixels in Rodin).

### 4. The union assertion

`XMB_TEXT_CHECK=1` now runs the real mask pass, not just the helper: two deliberately overlapping
copies of `e` are drawn one layer pixel apart into the real R8 target with `GL_MAX`, read back with
`glReadPixels`, and compared pixel for pixel with `unionCoverage()`. The same probe counts where
alpha-over would have differed, so the assertion cannot pass vacuously. Output of the final build:

```
text check: PASS union-rule overlapping coverage 90 px, keep-larger below alpha-over at 41 px
text check: accent pair e+U+0301 overlap 0 px at one pen
text check: PASS union-detect overlapping ink -> mask; touching or gapped ink -> alpha-over fast path
text mask: R8 coverage target QSize(1920, 1080)
text check: PASS union-mask GPU GL_MAX mask == keep-larger helper on 169 px; alpha-over differs at 57 px
text check: union auto rule takes the mask on 1 of 5 menu strings tested
text check: all assertions passed
```

`union-mask` is the required proof: 169 covered pixels match the helper exactly (0 mismatches), and
57 of them would be darker under alpha-over. The earlier elision and metric assertions still pass.
Running the same check at the 3840x2160 layer also passes (`600 px`, alpha-over differs at `116 px`).

One bug was found and fixed by this work: the first composite attempt left the vertex array unbound
after the mask pass, so an ink pass drew nothing and forced mode lost the text (halo only). Forced
mode against the fast path now being byte-identical is the regression check that catches that class of
error.

### 5. Frame-time comparison

The app's own 5 s `native frames:` line, one 30 s scripted session per mode on the final build plus the
pre-change build under the same conditions (`100` wait tokens, no render-clock), warm lines only (the
first 5 s line covers start-up):

| session | fps lines | warm mean |
|---|---|---|
| before (build `native-mask-before`) | 239.8 240.0 240.0 240.0 240.0 240.0 | 239.9 |
| after, `XMB_TEXT_MASK=0` | 240.0 (six lines) | 240.0 |
| after, auto (default) | 240.0 (six lines) | 240.0 |
| after, `XMB_TEXT_MASK=1` | 240.0 (six lines) | 240.0 |

The render loop is capped at 240 fps, so the 5 s line cannot resolve sub-frame costs. The three
after-mode sessions were therefore also run with `XMB_FRAME_TRACE` (the existing per-paint CPU timer,
which is not capped) and the `rebuilt=1` paint lines - the frames that redraw the UI layer and run the
text path - were compared:

| session | paints | rebuilt | rebuilt mean | rebuilt p95 | all-paint mean |
|---|---|---|---|---|---|
| `XMB_TEXT_MASK=0` | 8155 | 481 | 3.315 ms | 3.216 ms | 2.111 ms |
| auto (default) | 8159 | 481 | 3.288 ms | 3.195 ms | 2.060 ms |
| `XMB_TEXT_MASK=1` | 8159 | 484 | 3.437 ms | 3.240 ms | 2.041 ms |

Auto is inside run noise of the fast path (-0.8% on rebuilt frames). Forced mode, which masks every
string including the shadow pass, adds about 0.12 ms on a UI-rebuild frame (+3.7%). UI rebuilds are
about 6% of frames, so the amortised effect is ~0.007 ms/frame (~0.3%), which is why the fps line
cannot see it. That is consistent with the draw counts: the fast path issues a shadow and an ink pass
over N glyph quads, the mask path issues one N-quad mask pass plus two composite quads, so it trades a
glyph pass for a scissored clear and one FBO switch. The mask target is created twice on the canonical
run, both at start-up as the window enters fullscreen; no frame after that reallocates it.

Two earlier sessions were discarded: a stale `re-ps3-native` instance from a previous session (script
finished, no `quit`, one core busy) was still running and produced 205-238 fps spreads. It was closed
before the sessions above; that is the only reason the distributions are this tight.

### 6. Limits

- The overlap test is a box test, so auto mode can mask a string whose coverage does not actually
  overlap. The union is then a no-op (proven by the forced-mode hash), but the cost is paid; off and
  forced remain available.
- The halo is not unioned (section 1); if a later trace shows the firmware blurs one string mask, that
  is a second pass over the same target, not a change to this design.
- The shadow is still the approximate one-scene-pixel offset of `TEXT-RASTER.md` 3.3, now taken from
  the union mask; its runtime value is still unrecovered.
- `XMB_TEXT_MASK` and `XMB_TEXT_CHECK` are diagnostic switches only; neither is set by the capture
  harness, so the default path is the one measured above.

## Phase 2.4/2.5, 2026-10-27: elision wired, metric authority at every size, caret, and the UI glare measurement

Files touched: `src/view.cpp`, `src/text_renderer.cpp` and this section. `src/text_renderer.h` and
`src/font_raster.*` did not need to change: `elideToWidth`, `measure` and `selfCheck` already had the contracts
section 4 and section 2 documented, and the label loop is in `view.cpp`.

Canonical gate after this change, `work/ab-layers.ts --capture --exe C:/dev/out/native-render2/re-ps3-native.exe`:
frame 18.28 / 0.8288, top background 20.22 / 0.9336, category icon row 20.89 / 0.8966, item label row
11.94 / 0.3909, clock text 11.39 / 0.4105, info bar 14.78 / 0.6117 (PASS 6/6). Every text layer is identical to
the run before the change; the icon row differs because the icon material changed in the same build
(ITEM-ICONS.md, phase 2.1/2.2). Two `--render-clock 30` captures of the final build hash equal
(`SHA-256 CFD9644F...B1BAC`), so the elision and caret changes are deterministic.

### 1. Elision is wired into the label loop (2.4)

The hunk section 4 listed is landed at the focused/`labelRect` loop, with one difference: it runs only when
the atlas renderer is ready, because that is also the condition under which the prefix's advances and the drawn
glyph positions come from the same FreeType sum. The QPainter fallback keeps `QFontMetrics::elidedText`, since
that path also draws with Qt.

```cpp
const qreal labelLimit=value.isEmpty()?1050.:640.;
QString label=entry.title;
TextRenderer::Elision elided;
if(textRenderer_.ready()) {
    elided=textRenderer_.elideToWidth(labelFont,entry.title,labelLimit);
    if(elided.elided)label=elided.prefix;
} else label=QFontMetrics(labelFont).elidedText(entry.title,Qt::ElideRight,int(labelLimit));
...
if(elided.elided)
    text(QRectF(labelRect.left()+elided.ellipsisX,labelRect.top(),40,labelRect.height()),
         QString(QChar(0x2026)),27,QColor(255,255,255,int(255*alpha)));
```

The sign-in field's value (21 px, 440 px limit) uses the same helper, with the ellipsis appended to the prefix
because that field is a single left-aligned string rather than a label plus a positioned `label2_text_ellipsis`
object. Both call sites are inert in the canonical state: no label at 1050/640/440 px overflows, so the prefix
equals the full string and nothing is elided - which is why the text layers are byte-identical. The change
matters where the firmware's rule matters: a long title now loses whole clusters from one metric authority and
shows the ellipsis at the pen the firmware puts the separate Text object at, instead of substituting U+2026 with
a second, platform metric engine.

Proof that the wired path runs, not just the helper: `XMB_LABEL_LIMIT=<px>` overrides the label limit for a
scripted capture (diagnostic only; unset = 1050/640). At 120 px the focused row's "Steam Store" renders as
"Steam" plus a separate ellipsis draw at the prefix pen (`out/render2-work/grabs/elide.png`, crop 620,460); the
same frame without the override shows "Steam Store". The gap before the dots is the em-wide U+2026 advance
(section 4 measured it at 27.00 px), which is what the firmware reserves and why the helper sizes the prefix
against `maxWidth - ellipsis`.

### 2. Metric authority checked at every label size, not only the clock (2.4)

`XMB_TEXT_CHECK=1` now reports one line per size the UI draws (the label loop 27, the sign-in field 26, panels
23/22/21/20, page heading 19, subtitle cue 17, option note 16, PS-menu badge 14). Each compares the PAF 26.6
advance sum for a representative string with the width the renderer shapes at that size:

```
text check: PASS metric-size-27 "Game Data Utility" PAF 234.95 px vs shaped 234.88 px (delta 0.06)
text check: PASS metric-size-26 "PS3XMBpassword" PAF 235.84 px vs shaped 235.79 px (delta 0.05)
text check: PASS metric-size-23 "Display All" PAF 119.53 px vs shaped 119.48 px (delta 0.05)
text check: PASS metric-size-22 "System Information" PAF 211.46 px vs shaped 211.36 px (delta 0.10)
text check: PASS metric-size-21 "Photo Settings" PAF 154.77 px vs shaped 154.72 px (delta 0.05)
text check: PASS metric-size-20 "Charge Level" PAF 130.41 px vs shaped 130.38 px (delta 0.03)
text check: PASS metric-size-19 "Sort By" PAF 69.73 px vs shaped 69.70 px (delta 0.04)
text check: PASS metric-size-17 "A subtitle cue line" PAF 152.38 px vs shaped 152.28 px (delta 0.10)
text check: PASS metric-size-16 "This build cannot play it" PAF 191.20 px vs shaped 191.09 px (delta 0.11)
text check: PASS metric-size-14 "Triangle" PAF 55.75 px vs shaped 55.72 px (delta 0.03)
```

Delta 0.03-0.11 px over 7-26 glyphs at every size, so there is no size-dependent metric gap: the clock's
agreement is not a property of 27 px. The remaining text-layer gap is string content and rasterisation, as
section 5 concluded. The earlier `metric-authority` line stays (it compares the clock at the shaped face's own
pixel size).

### 3. Caret metrics decided (2.4)

The OSK caret used `QFontMetricsF::horizontalAdvance` while the field's glyphs were placed by the renderer's
FreeType advances. It now takes the same sum (`TextRenderer::measure`) and falls back to Qt metrics only when
the atlas renderer is unavailable, i.e. when Qt also draws the glyphs. Assertion at the field's 26 px:

```
text check: PASS caret-metric prefix "PS3XMBp" advance 129.12 px vs Qt metrics 129.00 px
```

The caret's bar itself stays a fitted 2x36 scene px. No firmware caret/metrics trace exists (TEXT-RASTER.md
leaves the pen-width trace open), so this is stated as a fit, not a port: the position is now metric-authoritative,
the size is not.

### 4. UI-layer glare: measured, decided against (2.5)

Question: the firmware blooms its UI through the same glare chain; the native composites the UI layer after the
tone map. A switch `XMB_UI_GLARE=<scale>` adds the resolved UI layer to the glare source inside `Hdr::finish`'s
`extraGlare` callback (the same hook `particles_second` uses), so the text and panels go through the threshold,
Gaussian and AccGlare passes with the scene. It samples the previous frame's resolve (the UI is drawn after the
background in `paintGL`), which is stable in every scripted state; it is off by default.

Canonical capture, one run per scale:

| layer | no UI glare (shipped) | scale 0.15 | scale 0.35 | scale 1.0 |
|---|---|---|---|---|
| frame | 18.28 / 0.8288 | 18.23 / 0.8284 | 18.15 / 0.8275 | 17.83 / 0.8228 |
| top background | 20.22 / 0.9336 | 20.22 / 0.9335 | 20.22 / 0.9335 | 20.20 / 0.9334 |
| category icon row | 20.89 / 0.8966 | 20.85 / 0.8960 | 20.77 / 0.8946 | 20.43 / 0.8879 |
| item label row | 11.94 / 0.3909 | 11.92 / 0.3916 | 11.88 / 0.3918 | 11.66 / 0.3879 |
| clock text | 11.39 / 0.4105 | 11.38 / 0.4015 **FAIL** | 11.34 / 0.3910 **FAIL** | 11.07 / 0.3451 **FAIL** |
| info bar | 14.78 / 0.6117 | 14.76 / 0.6078 | 14.70 / 0.6006 **FAIL** | 14.40 / 0.5767 **FAIL** |

Decision: keep the UI out of the glare source. The text layers lose more than the label glow gains at every
strength, and the loss is structural, not a level offset: clock SSIM falls 0.4105 -> 0.4015 at an eighth
strength and 0.3451 at full strength, i.e. the halos blur the glyph edges the reference shows sharp. The item
label row's small SSIM gain (+0.0007 at 0.15, +0.0009 at 0.35) is the focused label's own glow bloom and does
not offset two failing gates. So the native's post-tone-map composite is closer to the reference than any feed
of the UI into the glare chain, and the switch stays a documented measurement tool, off by default
(`XMB_UI_GLARE` unset). The one thing the measurement cannot separate is a firmware UI drawn into the scene
*before* the threshold with its own exposure; that would be a different pipeline, not this switch.

Reproduction:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-render2
$env:XMB_UI_GLARE="0.35"   # or 0.15 / 1.0; unset = shipped path
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-render2/re-ps3-native.exe
$env:XMB_TEXT_CHECK="1"   # deterministic renderer assertions, including metric-size-* and caret-metric
$env:XMB_LABEL_LIMIT="120" # elision probe: "Steam Store" renders as "Steam" + the separate ellipsis draw
```

## Text shadow from the firmware material (P3), 2026-10-27

Scope: `src/text_renderer.cpp`, `src/text_renderer.h` and this section. `view.cpp` is another pass's
fence, so the call-site wiring is returned as hunks in section 10, not applied. Build:
`& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-text`.

The slice replaces the renderer's invented shadow (offset 0, black, alpha 0.2) with the shadow a
firmware `Text` object carries, and adds the surface trace that lets the next pass check the material
it drew with.

### 1. What the renderer drew before

- Per-tile path: `shadowOffset` one output pixel, `shadowColor` black, `shadowAlpha` 0.2
  (`text_renderer.cpp`, the old `program_` uniforms).
- Union-mask path: the same black 0.2 shadow, offset one layer pixel (`reach = rasterScale_`).
- Measured on the reference (section 4): that shadow never leaves the glyph's own antialiased edge.
  The reference's shadow lands 4-5 px away and is roughly twice as dark, so the native frame had no
  shadow where the reference has one.

### 2. The material, and where it comes from

`Materials::textMaterial(container, object)` reads the Text object's own fields: `size`,
`topRed..bottomBlue`, `spacingHorizontal/Vertical`, `shadowX/shadowY/shadowPerspective`,
`shadowRed/Green/Blue/Alpha` and the `font` reference (audit/RCO-OBJECT-MODEL.md 5.3, 9.4).
`TextRenderer` now takes that material three ways:

- `draw(..., const Materials::TextMaterial&, const char* labelClass, ...)` - a caller-supplied material.
- `draw(..., Materials&, container, object, const char* labelClass, ...)` - resolves and caches
  `textMaterial(container, object)` for the caller; this is the label path's one-call entry point.
- `draw(..., labelClass)` - no material. The renderer applies `defaultMaterial()`.

`defaultMaterial()` is the firmware's own shared block, not a renderer constant: 1304 of the 1305 Text
templates in 4.93 carry `shadowX=4 shadowY=-5`, 1197 carry shadow RGB .25 (107 carry .9, one disables
the shadow), all carry `shadowAlpha=1` and `shadowPerspective=0` (audit/TEXT-RENDERER.md section
"RCO Text attributes for Phase 3", audit/RCO-OBJECT-MODEL.md 9.4). The trace names its provenance as
`firmware/text-template`, and `XMB_TEXT_MATERIAL=1` proves that block against a real object on every
run (section 7): the renderer derives the firmware root from the font directory, reads
`xmb_plugin_normal/clock` through `Materials::textMaterial`, and compares.

Why the item labels use the default rather than an object: **no RCO in the local 4.93 tree holds a
label Text object**. A tree dump of every `vsh/resource` container (`--rco-tree "*"`: 122 containers,
8070 objects, 8358 lines) contains no `label2_text`/`label2_text_ellipsis` object; those names survive
only as strings in `explore_plugin.prx`, and TEXT-RASTER.md 4.2 shows paf creating/positioning them at
run time from XMBML. So for the item-label class the shared template block is the only firmware
authority, and `view.cpp` can only name the class (hunk B).

### 3. The mapping: traced vs fit

Traced (read from the object, no interpretation):

| field | value | source |
|---|---|---|
| offset | `shadowX=4`, `shadowY=-5` | `xmb_plugin_normal/clock`, 1304/1305 templates |
| screen direction | `(shadowX, -shadowY)` = (+4, +5) px right/down | PAF object space is y-up (the notification label set steps +0.5, -0.375, -0.5 from the top line down), so -5 is down |
| unit | 1 template unit = 1 scene px at 1920x1080 | reference measurement, section 4: 4/-5 lands as 5/4 (+-1) px; scaling by the object's size (10) would be 10.8/13.5 px and is not what the frame shows |
| colour | shadow RGB 0.25/0.25/0.25, multiply | field names, 1197 templates |
| alpha | shadowAlpha 1.0 | every template |
| coverage | shadow coverage = mask coverage x shadowAlpha x text colour alpha | composite choice, below |
| size / spacing / font | reported by the trace, NOT applied | this slice must not move geometry; the objects store `size=0`/`font=null`, the plugin supplies both |

Fit (labelled, because the firmware's shadow blend state is not recovered):

- The shadow **multiplies** the layer by the material's shadow RGB instead of painting a flat grey.
  The reference measures a multiplicative darkening (section 4); the literal reading (a 0.25 grey
  premultiplied at alpha 1) is a *fixed* 64 levels whatever is underneath, which is invisible over
  the info bar's 66-level band and 30-40 levels too light where the reference shows its shadow.
- Because the UI layer is display-referred while PAF composites in linear light, the factor actually
  multiplied is `pow(shadowRGB, 1/2.2)`, i.e. 0.533 for 0.25. The exponent is a fit. In premultiplied
  terms the shadow is a black shutter: `dst *= mix(1, 0.533, coverage * shadowAlpha * textAlpha)`.
- `shadowAlpha` still scales coverage (alpha 1.0 = no attenuation), and the text colour's own alpha
  scales it again, so a shadow is never stronger than the glyph that casts it. This is the coverage /
  alpha product TEXT-RASTER.md 3.3 left unrecovered; the factor above is the only free parameter.
- `XMB_TEXT_SHADOW=premultiplied` restores the literal reading for the A/B (section 5). Diagnostic
  switch only; the capture harness leaves it unset.

Offset in layer pixels is `(shadowX, -shadowY) * rasterScale`; the mask composite samples the union
mask at `p - offset` (its v is flipped with the layer, so the uniform is `(+dx, -dy)`), and the
per-tile path samples the atlas tile at `p - offset` in atlas texels.

### 4. Reference measurement

Reference frame `out/native-reference/ab-rpcs3-root.png` (RPCS3 1080p), clock region
`1560,70..1840,140`. Ink = luma > 200; the ink mask is eroded (3x3) so a shifted sample lands on a
solid stroke interior; a pixel counts as shadow when it is more than 6 levels darker than a 31 px
median background.

- Offset: shifting the eroded ink by (dx, dy), the fraction of shifted pixels that land on shadow
  peaks at **(+5, +4) with 0.579**; (+5,+5) 0.575, (+6,+4) 0.575, (+4,+6) 0.558. The firmware's
  (+4, +5) is inside that peak's 1 px spread. The same test on the old native frame finds **zero**
  shadow pixels more than 2 px from ink, i.e. no shadow at all.
- Strength: at (+4, +5) the median level under the shadow is **31.8 against a 66-level band**, i.e. a
  **0.48** display factor. Candidates over that band: premultiplied 25% grey = 64 (invisible), black at
  25% alpha = 48, display multiply by 0.25 = 16. The measured 0.48 is a multiply, close to the fit's
  0.533 (the frame is video-compressed and the shadow is blurred by the scene glare, so +-0.1 is the
  measurement's own spread).
- The focused label's own shadow is not measurable this way: its glow bloom is brighter than the
  shadow is dark, and the reference string ("Corrupted Data") differs from the native one anyway.

### 5. A/B: the two composites

Same executable, one capture per mode, every clock pinned:

| mode | frame | item label row | clock text | info bar |
|---|---|---|---|---|
| before this change (no material) | 18.30 / 0.8323 | 11.95 / 0.3927 | 11.39 / 0.4105 | 14.78 / 0.6117 |
| material, premultiplied colour/alpha (`XMB_TEXT_SHADOW=premultiplied`) | 18.28 / 0.8315 | 11.91 / 0.3918 | 11.35 / 0.4102 | 14.73 / 0.6100 |
| material, multiplicative (shipped) | 18.28 / 0.8322 | 11.95 / 0.3925 | 11.39 / 0.4128 | 14.77 / 0.6113 |

The literal premultiplied reading loses a little on every text layer (-0.03..-0.05 dB), which is what
"shadowAlpha 1.0 makes the shadow a fixed 0.25 grey" predicts: it darkens where the reference has no
shadow and leaves the band untouched where it does. The multiplicative reading keeps the label and
the info bar neutral and **gains +0.0023 SSIM on the clock text**, which is the one text layer whose
pixels sit where the reference's own shadow is. The shipped default is therefore the multiplicative
mapping; the premultiplied one stays available as the documented alternative.

Note on what these numbers can arbitrate: the canonical clock string is MM/DD 12-hour ("10/25 12:32
PM") while the reference frame is DD/MM 24-hour ("25/10 12:32"), by design
(`ab-layers.thresholds.json` capture note), and the item label is "Steam Store" against "Corrupted
Data". So the layer scores bound the change and compare its *style*, not its glyph alignment; the
mapping itself was chosen from the reference's measured shadow (section 4), and the A/B only decided
which composite expresses it.

### 6. Per-layer numbers (canonical capture, shipped build)

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-text
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-text/re-ps3-native.exe
```

| layer | before | after | delta | gate |
|---|---|---|---|---|
| frame | 18.30 / 0.8323 | 18.28 / 0.8322 | -0.02 / -0.0001 | PASS |
| top background | 20.25 / 0.9363 | 20.25 / 0.9363 | +0.00 / +0.0000 | PASS |
| category icon row | 20.92 / 0.8990 | 20.93 / 0.8990 | +0.01 / +0.0000 | PASS |
| item label row | 11.95 / 0.3927 | 11.95 / 0.3925 | +0.00 / -0.0002 | PASS |
| clock text | 11.39 / 0.4105 | 11.39 / 0.4128 | +0.00 / **+0.0023** | PASS |
| info bar | 14.78 / 0.6117 | 14.77 / 0.6113 | -0.01 / -0.0004 | PASS |

PASS 6/6 (budget 0.5 dB / 0.004). Only the text layers moved, which is the attribution: no geometry,
no string and no other material was touched. Grabs: `out/native-text/before-frame.png`,
`after-frame.png`, plus `label-before/after.png` (crop 400,425,800,100) and `clock-before/after.png`
(crop 1570,80,260,30). The after crops show the shadow the firmware material asks for: right and
down, darker than the band, on the clock digits and on the item label.

`XMB_TEXT_MASK=0`, auto and `=1` produce the same 1920x1080 frame on this build (SHA-256
`9E430CAFAA5A4B31C1D451BE7F6312C27248D292ED7FED655CF07D1D4F48654C` all three): the union path's
single-quad shadow and the per-tile per-glyph shadow agree on every string in the canonical state.
The shadow is drawn identically under both, so the mask path's shadow is not a second unresolved
variable.

### 7. The trace (`XMB_TEXT_MATERIAL=1`)

Diagnostic only; the capture harness never sets it. Output of the shipped build on the canonical
state (one line per surface, plus the per-surface assertion):

```
text material: trace on; shadow from the firmware material, mapping offset=(shadowX,-shadowY) scene px, colour=shadowRGB multiplies the layer as pow(rgb,1/2.2), coverage scaled by shadowAlpha*textAlpha (the composite is a fit; the firmware shadow blend state is unrecovered)
text material: default = firmware Text template (4.93: 1304/1305 objects shadowX=4 shadowY=-5 RGB=0.25,0.25,0.25 alpha=1.00) -> screen (+4.00,+5.00) scene px; no RCO Text object holds an item label (paf draws XMBML text at run time)
text material: RCO read xmb_plugin_normal/clock present=1 size=10 shadow=(4,-5) RGB=(0.250,0.250,0.250) alpha=1.00 spacing=(0,0) font=<null> container=read
text material: PASS default == xmb_plugin_normal/clock shadow block (shadow 4/-5 vs 4/-5; colour 0.250,0.250,0.250 vs 0.250,0.250,0.250; alpha 1.00 vs 1.00)
text material: class=unattributed drawn=21px source=firmware/text-template size=0 spacing=(0,0) font=<null> shadow=(4,-5) screen=(4.00,5.00) scene px colour=(0.250,0.250,0.250) alpha=1.00 applied=shadow
text material: PASS class=unattributed shadowX=4 shadowY=-5 shadowAlpha=1.00 offset-within-tile-pad=yes
text material: class=unattributed drawn=27px source=firmware/text-template size=0 spacing=(0,0) font=<null> shadow=(4,-5) screen=(4.00,5.00) scene px colour=(0.250,0.250,0.250) alpha=1.00 applied=off (shadow=false)
text material: PASS class=unattributed shadowX=4 shadowY=-5 shadowAlpha=1.00 offset-within-tile-pad=yes
text material: class=unattributed drawn=27px source=firmware/text-template size=0 spacing=(0,0) font=<null> shadow=(4,-5) screen=(4.00,5.00) scene px colour=(0.250,0.250,0.250) alpha=1.00 applied=shadow
text material: PASS class=unattributed shadowX=4 shadowY=-5 shadowAlpha=1.00 offset-within-tile-pad=yes
text material: class=unattributed drawn=17px source=firmware/text-template size=0 spacing=(0,0) font=<null> shadow=(4,-5) screen=(4.00,5.00) scene px colour=(0.250,0.250,0.250) alpha=1.00 applied=shadow
text material: PASS class=unattributed shadowX=4 shadowY=-5 shadowAlpha=1.00 offset-within-tile-pad=yes
text material: class=unattributed drawn=18px source=firmware/text-template size=0 spacing=(0,0) font=<null> shadow=(4,-5) screen=(4.00,5.00) scene px colour=(0.250,0.250,0.250) alpha=1.00 applied=shadow
text material: PASS class=unattributed shadowX=4 shadowY=-5 shadowAlpha=1.00 offset-within-tile-pad=yes
```

The 27 px class appears twice because the focused row draws a glow pass (`shadow=false`) and then the
ink pass (`applied=shadow`); the trace keys on (class, size, shadow applied) so the glow pass cannot
hide the shadow the ink pass uses. Classes read `unattributed` until the `view.cpp` hunks land; after
that the same lines read `class=item-label`, `class=clock` (source
`xmb_plugin_normal/clock`), `class=notification` (source `system_plugin/notification_label0_b`, ...).

Two things this trace is for, per the slice's goal:

1. **Authority, not agreement.** The `RCO read xmb_plugin_normal/clock` line is a real
   `Materials::textMaterial` read of the firmware's container during the same run, and the next line
   asserts the default block equals it. The material is not hardcoded *and* assumed: the code holds
   one default for the surfaces that have no object, and the trace proves that default is a real
   object's block.
2. **Per-surface.** Once the hunks are applied, a class with the wrong material (or a failed
   `textMaterial` returning `present=0`) prints `source=<absent>` and `FAIL`, instead of drawing
   silently with the default.

### 8. Assertions (`XMB_TEXT_CHECK=1`)

Two deterministic assertions were added; every existing assertion still passes:

```
text check: PASS shadow-material firmware default shadow (4,-5) .25 grey alpha 1 -> screen (4.00,5.00) scene px, colour alpha 1.00
text check: PASS shadow-tile-pad offset 4.0x5.0 layer px <= 8.0 px glyph-tile padding
...
text check: all assertions passed
```

`shadow-material` pins the mapping (screen sign included, so a y-up/y-down regression fails).
`shadow-tile-pad` pins the per-tile path's physical limit: the tile's halo padding is
`ceil(haloRadius*rasterScale)+2` (14 layer px at the 2x layer, 8 at the 1x layer the check first runs
on), and a material whose shadow is larger would clip it per glyph. The firmware's 8x10 layer px fits
both; a material that does not fits is traced as `offset-within-tile-pad=no` and warned once.

### 9. Determinism

- Two `--render-clock 20` captures of the shipped build: SHA-256
  `66B737CDF5ADE40D10367E2125CA978765DE0F82F67BC19C7035F723A902A9D0`, identical.
- The canonical `--render-clock 30` frame hashes
  `9E430CAFAA5A4B31C1D451BE7F6312C27248D292ED7FED655CF07D1D4F48654C` with and without
  `XMB_TEXT_MATERIAL=1`, so the trace does not alter the frame. Before the change the same capture
  hashed `F68732A7F9A135D55D097710EE4B45B2779594BD1AD886E3FF2BB084CC97BF18` - the delta is the shadow.
- The per-tile shadow samples the atlas at a fixed material offset and the mask shadow samples the
  R8 mask at the same offset; both are integer layer-pixel displacements, so no new fractional
  sampling was introduced.

### 10. `view.cpp` hunks (returned, not applied)

A. The shared `text()` helper (around line 1124) gains the class name; existing calls are unaffected.

```cpp
auto text=[&](const QRectF& r,const QString& value,int size,QColor colour,int flags=Qt::AlignLeft|Qt::AlignVCenter,
              const char* labelClass=nullptr){
    if(textRenderer_.ready()) {QFont f(shell_.fontFamily());f.setPixelSize(size);
        textRenderer_.draw(*this,p,r,value,f,colour,flags,0,true,true,labelClass);return;}
    QFont f(shell_.fontFamily());f.setPixelSize(size);f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);p.setFont(f);p.setPen(QColor(0,0,0,38));
    p.drawText(r.translated(1,1),flags,value);p.setPen(colour);p.drawText(r,flags,value);
};
```

B. The item-label draws (around lines 1417-1420). The class name is all this surface can supply: no
RCO holds its Text object, so the renderer's firmware default applies.

```cpp
text(labelRect,label,27,QColor(255,255,255,int(255*alpha)),Qt::AlignLeft|Qt::AlignVCenter,"item-label");
if(elided.elided)
    text(QRectF(labelRect.left()+elided.ellipsisX,labelRect.top(),40,labelRect.height()),
         QString(QChar(0x2026)),27,QColor(255,255,255,int(255*alpha)),
         Qt::AlignLeft|Qt::AlignVCenter,"item-label");
```

C. The clock (line 1471) is the one text surface with a Text object of its own
(`xmb_plugin_normal/clock`, size 10, shadow 4/-5 .25 alpha 1). Read the material and let the renderer
apply its shadow; the object's size/gradient stay unapplied so the label column does not move.

```cpp
if(textRenderer_.ready()) {
    QFont clockFont(shell_.fontFamily());clockFont.setPixelSize(27);
    textRenderer_.draw(*this,p,{1240,82,525,45},shell_.clockText(),clockFont,Qt::white,
                       Qt::AlignRight|Qt::AlignVCenter,sceneMaterials(assets_),
                       QStringLiteral("xmb_plugin_normal"),QStringLiteral("clock"),"clock");
} else text({1240,82,525,45},shell_.clockText(),27,Qt::white,Qt::AlignRight|Qt::AlignVCenter);
```

D. The notification (`drawNotification`, lines 1036-1050). Its three label objects are
`system_plugin/notification_label0_b`, `_1_b`, `_2_b`, and the one-line form is
`notification_message_a`; pass the object per call so the trace can print the real source.

```cpp
const auto label=[&](const QRectF& rect,const QString& value,int size,QColor colour,const char* object) {
    if(textRenderer_.ready()) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        textRenderer_.draw(*this,p,rect,value,font,colour,Qt::AlignLeft|Qt::AlignVCenter,
                           sceneMaterials(assets_),QStringLiteral("system_plugin"),
                           QString::fromLatin1(object),object);
        return;
    }
    QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
    p.setPen(QColor(0,0,0,38));p.drawText(rect.translated(1,1),Qt::AlignLeft|Qt::AlignVCenter,value);
    p.setPen(colour);p.drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,value);
};
if(twoLines) {
    label(QRectF(textLeft,plaque.top()+10.,textRight-textLeft,titleSize+6.),title,titleSize,
          QColor(245,245,245),"notification_label0_b");
    label(QRectF(textLeft,plaque.top()+10.+titleSize+4.,textRight-textLeft,detailSize+6.),
          notificationShown_.detail,detailSize,QColor(206,206,206),"notification_label1_b");
} else label(QRectF(textLeft,plaque.top()+8.,textRight-textLeft,plaque.height()-16.),title,titleSize,
             QColor(245,245,245),"notification_message_a");
```

Every other `text()` call site can pass a class name alone (`"options"`, `"panel-heading"`,
`"osk-field"`, ...) to appear in the trace; no material is needed where the surface has no object.
`info bar`'s status text has the system_plugin `infotext1/infotext2` objects available if that surface
is wanted next.

### 11. Limits

- The 12 named-state self-baselines (`work/ab-layers.ts --states`) carry text surfaces whose shadow is
  now the firmware one (notification, options panel, subtitle cue, sign-in, download page). They are
  expected to move and need a deliberate `--states --save-reference` refresh after review; they are
  recorded native renders, not firmware comparisons, so this is a recording step rather than a
  fidelity change. The canonical per-layer gate above is the firmware-facing one and it passes.
- The shadow is a hard copy of the coverage. The reference's is soft: the scene glare blurs it and
  the capture compresses it. No blur was added - it would be a new pass, and 4.93 gives no evidence
  for one (TEXT-RASTER.md 1.5, 3.2). This is the largest remaining visual difference.
- The per-tile path can only paint a shadow that lands inside the glyph tile's halo padding (14 layer
  px at 2x). The firmware's 8x10 fits; a material with a larger offset would clip there and is warned
  about once and traced (`offset-within-tile-pad=no`). The mask path has no such limit.
- The union mask and the per-tile path produce the same canonical frame, so the case the union would
  really change - two glyph shadows overlapping where their inks do not - is not present in the
  canonical state. A state with tight/overlapping text would decide whether the auto rule should
  union the *grown* ink boxes; it does not today.
- The layer scores compare against reference strings that differ from the native ones by design
  (clock format, item titles), so they bound the change but cannot certify glyph-level shadow
  placement. The placement rests on the reference measurement in section 4.
- `size`, `top`/`bottom` and `spacing*` are reported, not applied. The objects store `size=0` and
  `font=null` on the label surfaces, and applying the gradient would be a colour change beyond this
  slice.
- The trace's per-surface lines stay `unattributed` until the `view.cpp` hunks land; the material
  path itself is proven by the `RCO read` line, which runs every time the trace is on.

### 12. Reproduction

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-text
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-text/re-ps3-native.exe

$env:XMB_TEXT_MATERIAL="1"   # per-surface material trace + the RCO-read comparison (does not change the frame)
$env:XMB_TEXT_CHECK="1"      # assertions, including shadow-material and shadow-tile-pad
$env:XMB_TEXT_SHADOW="premultiplied"  # A/B: the literal colour/alpha shadow instead of the shipped multiply
```

## The text-metric residual measured: label em, unfocused ink level, depth-1 pen (2026-10-28)

This pass measured the residual `GEOMETRY-ROOT 12.6.2` recorded ("the native label 'System Name' inks 202 px
against the console's 218 px; the unfocused row ink peaks at 202-207 luma against 148-175"). Measured first,
then changed: the two defects live at the **call site**, not in the renderer, so `src/view.cpp` was read and
never edited and the fixes are returned as hunks in section 5. They were verified in out-of-tree copies of
the tree (`C:\dev\out\text2\hunk`, `hunk-pen`), built and captured against the same references; the repo's
own renderer sources and this file are the only things this pass touched. Build artifacts stay in
`C:\dev\out\text2`.

Build and measure, exactly:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-text2
& "$env:USERPROFILE\.bun\bin\bun.exe" work\parity.ts --capture --score --state root,page-settings-system,page-settings-theme,theme-list --exe C:/dev/out/native-text2/re-ps3-native.exe --out C:/dev/out/text2/parity-before
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-text2/re-ps3-native.exe
```

### 1. Method

Every number below is read off the matched pair: the reference
`out/parity-references/<state>__original__day__1080p.png` against the native frame captured for the same
state with the harness pins (`--render-clock 30`, background clock 2026-10-25T10:00, UI clock
2026-10-25T12:32, focus age 2000 ms). Ink thresholds are stated per measurement; where two frames have
different ink peaks the threshold is **50 % of that frame's own peak**, which is what makes a width
comparison fair across a compressed reference and an uncompressed native frame. Tools: the repo's
`work/ab-compare.ts` for region and crop scores, `C:\dev\work\m.py` (`inkcol`, `bbox`, `mean`, `crop`) for
extents, and three small read-only helpers written for this pass (`C:\dev\out\text2\measure.py`, `rowstat.py`,
`shadowstat.py`). Luma is Rec.709 throughout.

The label measured is `page-settings-system` row 0, "System Name" - the one item-label string the two frames
share, which is why this row is the pass's primary instrument. "White fraction" is
`(p99.5 ink - median background) / (255 - median background)`: the fraction of the way from the local
background to the display's white point that the glyph core reaches.

### 2. Width: the label em is 27 px against the console's 32 px (diagnosis b, glyph scale)

| quantity | console (matched ref) | native, 27 px | native, 32 px + pen |
|---|---|---|---|
| ink width, "System Name" | **214 px** (x 627-840) | **180-182 px** (x 624-803) | **214 px** (x 627-840) |
| `S` cap height | **26 px** (y 667-692) | **22-23 px** (y 669-690) | **26 px** (y 667-692) |
| glyph starts, 10 ink bands | 627 648 666 683 695 715 755 778 798 825 | 624 645 663 680 692 712 752 775 795 822 | 627 648 666 683 695 715 755 778 798 825 |
| per-glyph ink width (`S`..`e`) | 18 16 15 10 16 23 19 17 23 16 | 16 14 13 9 14 21 16 15 21 15 | 17 15 14 9 16 23 18 16 23 16 |

The 27 px column is 16 % narrow in ink and 4 px short in cap; the 32 px column is the console's own extents
within 1 px on every glyph. What the four candidate causes are, in turn:

- **(a) tracking / advance authority: not the cause.** Glyph-start pitch first-to-last is 167 px native
  against 198 px console, a ratio of 1.186 - and 198/167 is the same ratio as the ink widths and the cap
  heights. So the shortfall scales *every* metric by 1.186; there is no per-glyph tracking term to fix, and the
  renderer's own advance authority already agrees with PAF to 0.03-0.16 px at every size
  (`metric-size-*`, section "Phase 2.4/2.5"). The metrics are right; the size they are applied at is not.
- **(b) glyph scale: the cause.** 27 px x 1.186 = 32.0 px, and PIL/FreeType on the firmware's
  `SCE-PS3-RD-R-LATIN.TTF` gives cap 23 px and "System Name" ink 184 px at 27 px, cap 26-27 px and ink
  211-218 px at 31-32 px. The console's measured 26 px cap / 214 px ink sits at 31.5-32 px; 32 is the value
  used for the hunks below, and it reproduces all ten glyph starts exactly once the pen is corrected.
- **(c) hinting / rasterisation: not the cause.** The renderer draws FreeType unhinted coverage (flags 10),
  which is the firmware's own load path; and the *category title* proves the path is faithful when the size
  is right: the root title "Game" at 21 px inks 58 px with an 18 px cap in the console and 58 px with an 18 px
  cap in the native frame - identical. The clock at 27 px also matches (digit ink height 22 px console, 23 px
  native). So this is not a global scale error and not a rasteriser error: 21 px and 27 px are both right for
  their surfaces, and the item label alone is 5 px too small.
- **(d) wrong face: ruled out.** At 27 px every Latin face in `data/font` has cap 22-23 px
  (`RD-R`, `RD-B`, `RD-I`, `RD-L`, `MT-*` 22, `SR-R` 23, `VR-R` 18, `DH-R` 22, `NR-R-JPN` 23); none has 26.
  A face change cannot move the cap by 4 px, only a size change can. The face is also visibly the same
  typeface in both frames (same `S`, `y`, `m` shapes and the same 1.186 pitch ratio).

Where the 27 came from: `GEOMETRY-ROOT 4.2 #35` recorded "label ink height 492..519 (27 px row band)", i.e.
a *row band*, not a cap. `11.2` later measured the cap itself at 26 px and `12.2` recorded the native ink as
"495-515" (21 px) while calling it "same 26-27 px cap" - the two numbers were never compared directly. They
are here: 26 against 21-23. The em that produces a 26 px Rodin cap is 32 px.

### 3. Brightness: the unfocused ink level is 0.446 of white on the console, 0.72 in the code (diagnosis: the brightness factor)

Unfocused item rows, white fraction per row band (pitch 80, x 620-1150; the focused row is excluded where a
state has one):

| state | console | native (27 px, `.72+.28*focus`) |
|---|---|---|
| page-settings-system, rows 0-4 | 0.447 0.446 0.446 0.447 0.448 | 0.716 0.718 0.717 0.720 0.721 |
| page-settings-theme, rows 1-2 | 0.446 0.442 | 0.718 0.713 |
| column-settings, rows 0-4 | 0.447 0.446 0.445 0.447 0.449 | 0.716 0.718 0.717 0.716 0.721 |

The native value is exactly the code's `.72`; the console's is 0.442-0.449 across 12 rows in three states
(mean 0.446, spread 0.007). At the *focused* end both frames saturate at 255 (root's focused label, and
`page-settings-theme` row 0), so `focus = 1` -> 1.0 is right and only the unfocused base is wrong.

The other three candidates, checked and excluded:

- **Shadow: not the cause** (it also draws nothing - section 4). If a 0.53 multiply shadow were present it
  would darken the band, not brighten the glyph core, and the measured deficit is *inside* the glyph.
- **Antialiasing gamma / coverage accumulation: not the cause.** The renderer applies no gamma curve and the
  ink core is fully covered in both frames (2-3 px stems at these sizes, p99.5 sampled inside the stroke), so
  the coverage path cannot move a core from 0.72 to 0.45. A 0.72 -> 0.446 change is a factor change, not an
  edge change.
- **The background exposure is a *separate* error, and must not be folded into this one.** The console's page
  background reads 50-64 where the native's reads 68-82 in the same rows (`GEOMETRY-ROOT 12.6.1`), so the
  absolute peaks differ for two reasons at once. The white-fraction measure removes the background term, and
  the console's white point is 255 in the same frames (root title "Game" peak 255 in both), so the residual is
  the label's own alpha, not a global exposure difference. Correcting the exposure would move these peaks
  again; it is a background problem with its own owner.

### 4. The material shadow: the shipped multiply is inert (corrects section "Text shadow from the firmware material" 6)

Measured, and it changes what the P3 A/B meant:

- In the item-label band of `page-settings-system` (x 620-850, y 660-706) **no pixel is more than 1.2 levels
  below its own row's background** (per-row background from a text-free strip; min delta -1.2 over 250 x 55
  px). The console's band is the same (0 px below bg-4). On the root pair the console's clock band has pixels
  down to 30.9 against a 66.4 background - that is its shadow - while the native's clock band bottoms out at
  83.6 against 85.6, i.e. none.
- Replacing the multiply's blend state (`GL_DST_COLOR, GL_ZERO`) with the arithmetically identical
  `GL_ZERO, GL_SRC_COLOR` form produced a **byte-identical frame** (SHA-256
  `73AF46A7848F06A8C7F4E3880968C41F7D560A4BE87081A125780F3EDAF7DED6`, `page-settings-system`, render clock
  30, two runs). So the attenuate branch contributes no pixels at all, in either the per-tile or the union-mask
  path (the two paths render identical canonical frames, section 6 above).
- The branch that *does* change pixels is the premultiplied one: `XMB_TEXT_SHADOW=premultiplied` on the same
  state shifts the frame by up to 190 levels (`|d| > 2` on 15901 px), which is what the P3 A/B measured. Its
  +0.0023 SSIM on the clock layer was therefore the *removal of the old 1 px black 20 % shadow* plus the
  premultiplied alternative, not the shipped multiply landing where the reference's shadow is.
- What the references want is per surface, not global: the console's clock carries a 0.47 multiply shadow, and
  its item labels carry **none** (five unfocused rows in three states, plus a focused row - `page-settings-theme`
  row 0, min 48.1 against a 49.4 background). A correct fix therefore has to be scoped, and the item-label
  surface has no attributable RCO Text object, so the call site must pass the clock's material (the P3 section
  10 hunk C) before the renderer can tell the two apart. Left as it is in this pass: the multiply is a no-op, so
  nothing regressed, and making it draw would add a shadow to the labels that the reference does not have.

### 5. The hunks (`src/view.cpp`, returned not applied)

All three are inside the item-label loop (`View::paintGL`, the row loop around line 1706). Line numbers are
the tree's at the time of writing.

```cpp
// A. label em 27 -> 32 (section 2). The same labelFont feeds elideToWidth, so the ellipsis pen moves with it.
QFont labelFont(shell_.fontFamily());labelFont.setPixelSize(32);           // was 27
    if(focus>.5)glow(labelRect,label,32,(focus-.5)*2*breath*(entry.disabled?.45:1.));   // was 27
    text(labelRect,label,32,QColor(255,255,255,int(255*alpha)));                        // was 27
        QString(QChar(0x2026)),32,QColor(255,255,255,int(255*alpha)));                  // was 27

// B. unfocused ink level .72 -> .455 (section 3). The console's measured mean is 0.446; .455 is the value the
// verified build used and leaves the ink ~1.5 luma high, so .446+.554 is the closer pair if it is being tuned.
const double alpha=(.455+.545*focus)*(entry.disabled?.55:1.);              // was (.72+.28*focus)

// C. depth-1 label pen 57 -> 60 (section 2): at the corrected em the console's ten glyph starts are the native
// starts + 3 px, a constant, so it is the level>0 constant and not the advances. Level 0 keeps 113.
const double labelX=kColumnX+(level==0?(tile?173:113):60)+ahead*250+offset; // was (level==0?...:57)
```

Hunk C's evidence is the constant offset: at 32 px with the old 57 every one of the ten ink-band starts was
3 px left of the console's (624 645 663 680 692 712 752 775 795 822 against 627 648 666 683 695 715 755 778
798 825); with 60 they are identical. Nothing else in the row moves: the vertical centre is unchanged (the
cap lands on y 667-692 in both), because the label rect is vertically centred and the ink centre is the same.
Scope note: the constant is shared with the faint parent-column labels that a page draws behind itself, so
those move 3 px too. The page-row regions that contain them gained with the hunk (+0.34 dB on
`page-settings-system`, +0.26 dB on `page-settings-theme`), so the net is measured, but a settled frame with a
readable parent column would be the cleaner test of that sub-surface.

### 6. Numbers

Matched-string crop, `page-settings-system` row 0 (`620,655,250,55` - the one row both frames spell the same
way), and the five-row band (`615,650,545,370`):

| build | "System Name" ink | ink level | row crop PSNR / SSIM | 5-row band PSNR / SSIM |
|---|---|---|---|---|
| shipped (27 px, .72) | 180-182 px | 0.716 | 14.85 / 0.4813 | 17.85 / 0.7840 |
| + A, B | 214 px | 0.454 | 16.64 / 0.5226 | 19.01 / 0.7779 |
| + A, B, C | 214 px | 0.454 | **22.81 / 0.9457** | **19.51 / 0.8090** |

Matched regions (`work/parity.ts --capture --score`, against `out/parity-references`). "before" is
`C:\dev\out\native-text2` (the current tree); the two after columns are the out-of-tree builds.

| state / region | before | + A, B | + A, B, C |
|---|---|---|---|
| root frame | 17.86 / 0.8226 | 17.97 / 0.8225 | 17.97 / 0.8225 |
| root item label row | 11.03 / 0.3957 | 11.08 / 0.4003 | 11.08 / 0.4003 |
| root clock text | 11.57 / 0.5376 | 11.57 / 0.5376 | 11.57 / 0.5376 |
| root info bar | 14.54 / 0.7669 | 14.54 / 0.7669 | 14.54 / 0.7669 |
| page-settings-system frame | 18.50 / 0.8533 | 18.64 / 0.8531 | 18.69 / 0.8560 |
| page-settings-system page rows | 16.26 / 0.7397 | 16.51 / 0.7392 | 16.60 / 0.7494 |
| page-settings-theme frame | 17.88 / 0.8356 | 17.96 / 0.8352 | 18.04 / 0.8400 |
| page-settings-theme page rows | 15.27 / 0.7071 | 15.39 / 0.7039 | 15.53 / 0.7212 |
| theme-list frame | 18.98 / 0.8602 | 18.97 / 0.8603 | 18.98 / 0.8611 |
| theme-list band rows | 16.97 / 0.7413 | 16.97 / 0.7413 | 16.96 / 0.7410 |

Reading the table: the frame and page-row regions gain where the labels are, `theme-list` does not move
because its rows are drawn by `PanelBand`, whose text measures right at its own size (focused "Original" inks
91 px with a 25 px cap on the console against 88 px with a 24 px cap native; "Air Paint" 97 against 98), and
the root item label row can only move ~0.05 dB because its reference label is a different string.

The `+A,B` SSIM dip on the two page-row regions (-0.0005, -0.0032) and its recovery under `+A,B,C` is the
attribution for hunk C: a correct em with a 3 px pen offset overlaps the console's glyphs worse than the small
em did, and the offset is what has to go with it. On the one row whose string matches, `+A,B` alone already
gained +1.79 dB, so the em is not the regression; the pen was.

Canonical gate on the `+A,B,C` build (`work/ab-layers.ts --capture`): **PASS 6/6** with
`text presence: 1677 bright pixels in the clock crop (minimum 900) PASS`; frame 18.48 (base 18.29, +0.19) /
0.8327, top background 20.25 / 0.9363, category icon row 21.20 / 0.9029, item label row 11.92 (-0.03) /
0.3891 (-0.0034), clock text 11.39 / 0.4500, info bar 14.98 / 0.7639. The item label row is the one layer that
pays, and it is inside the 0.5 dB / 0.004 budget: the canonical reference's focused label is "Corrupted Data"
against the native's "Steam Store", so a correctly sized native label covers more of the reference's glyph
pixels than a small one did. The 12 named-state self-baselines need the usual deliberate
`bun work/ab-layers.ts --states --save-reference` refresh after the hunks land.

Determinism: two `--render-clock 20` captures of the current tree's build (`native-text2`, built 06:47 from
the source at that time) are byte-identical, SHA-256
`A2A68EC3ECD1BDA1A014FD140C3BCFD553DF17B1944A3CA78FDB1C05D542EB9E`. Two runs of the `+A,B` build on the
settings state are also byte-identical (`73AF46A7...ED6`, the hash quoted in section 4), and the `+A,B,C` build
reused the same capture path, so a pen constant cannot introduce nondeterminism. Note for a rebuild: another
pass wrote `src/ribbon.cpp` and `src/settings_menu.cpp` at 06:59-07:07 during this one, after that build, so a
fresh binary will differ in the ribbon; every "before" number here belongs to the 06:47 build.

### 7. Remaining differences, and who owns them

1. **The background exposure is worth more than the text.** In the page rows the console reads 50-64 where the
   native reads 68-82; the fitted per-channel gains of `12.6.1` (0.76-0.79) are the same statement. In the
   gated regions (1000 x 620 for `page rows`, 800 x 100 for `item label row`) that offset is most of the PSNR
   budget, which is why a text fix that gains 7.96 dB on the matched string moves the region by 0.34 dB.
   Owner: the background/ribbon pass, not the text stack.
2. **Region scores are mostly content.** `page-settings-system` shares one row string out of five
   ("System Name"), `root`'s focused label is a different string entirely, and `theme-list`'s rows match.
   Any future claim about text parity should quote the string-matched crop as well as the region, or it will
   understate a correct fix by an order of magnitude.
3. **The clock's shadow is still missing.** The console's clock band has shadow pixels at 0.47 of its
   background; the native's has none. The multiply branch is inert (section 4) and the label surface must stay
   shadowless, so the fix is (i) make the multiply work, (ii) pass `xmb_plugin_normal/clock`'s material at the
   clock call site (P3 section 10 hunk C) so the shadow is attributable, and (iii) leave the unattributed
   default without one - which is a view.cpp + renderer pair, outside this pass.
4. **The focused label's glow is unmeasured here.** The console's focused label carries a visible white bloom
   (`root`: the label sits inside a halo several pixels wide); the native's comes from the view's breathing
   curve times the renderer's Gaussian. The item-label-row region contains the focused row, so part of its
   residual is glow shape, not glyph shape.
5. **The level-0 pen is bounded, not settled.** The two frames' focused labels are different strings, so the
   console's first glyph is `P` (ink left 684) and the native's is `S` (680); a 1-2 px level-0 residual cannot
   be excluded from these frames. `12.1` row 1's 678 stands unrefuted.
6. **The subtitle (17 px) and help line (18 px) sizes are unmeasured.** Every captured state has different
   strings on those lines ("*User" against "Browse and install"), so the cap-height comparison that settled the
   label needs a state that shares them; a later pass with such a pair should check them the same way.
7. **The last per-glyph difference is 1 px of ink width** on several glyphs (18 against 17, 19 against 18) at
   identical starts and cap. That is the reference's video compression plus its own rasteriser, not a metric
   this renderer can reach.

No source file in the write fence changed in this pass; this section and the out-of-tree builds are the
deliverable, and the three `view.cpp` hunks in section 5 are the change to apply.
