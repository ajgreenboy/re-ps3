# Phase 2 — native text renderer and firmware trace

Updated 2026-10-25. The GL atlas foundation is implemented; pixel parity with PAF is still open.

## Implementation

`src/text_renderer.cpp` shapes text with QTextLayout, retains each run's actual QRawFont (including fallback
faces), and caches grayscale glyph coverage at the display's native pixel size. It uploads padded coverage
and a separate Gaussian halo to GL atlas pages. Consecutive glyphs on the same page are batched in shaped
order; the shader produces premultiplied coverage and halo after a separate whole-string shadow pass. Labels are drawn in the original scene
order through QPainter native-paint boundaries, so dialogs still dim the elements behind them.

Menu labels, captions, subtitles, dialog/information text and the OSK use the atlas. The existing Qt text path
remains a fallback if shader initialization fails. It is not a port of PS3 libfont: QRawFont still uses the
host Qt font engine. The temporary metrics used for elision and the OSK cursor also remain Qt metrics.

Native pixel size is rounded from the logical font size and viewport scale. Resizing invalidates atlas and
shaped-layout caches. Coverage is grayscale, with no added gamma curve. Vertical hinting is an explicit
current choice, not a recovered firmware setting. The measured halo uses sigma 2.2 and a ~6-pixel radius at
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

1. Recover hinting/rasterizer flags and style-code-to-face mappings. Match focused/unfocused/subtitle/dialog
   crops against the same reference state; readable native output alone does not prove pixel parity.
2. Recover runtime glow/shadow overrides and coordinate scaling; implement RCO gradients and spacing.
3. Reproduce max-coverage mask accumulation for overlapping glyphs without losing shaping/fallback faces.
4. Replace temporary Qt elision/cursor metrics once the firmware metric path is reproduced.

The user also identified material issues: clock inputs have since been traced and implemented (see
ICON-MATERIAL-CLOCK.md); item icons in the open column and a background that is too bright relative to RPCS3
remain open. Correct background sampling before tuning unfocused icons.
