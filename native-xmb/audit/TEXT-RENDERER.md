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
