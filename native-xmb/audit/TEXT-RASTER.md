# Phase 2 - firmware text rasteriser: hinting, gamma, overlap, glow and elision

Updated 2026-10-25. Companion to `TEXT-RENDERER.md` and `FONT-MATCHING.md`; this
document closes the remaining Phase 2 text questions with the evidence that exists
locally and explicitly marks the ones that no local evidence closes.

Ground rules used while writing this: no value below is invented. Anything read from
firmware is cited by module, address (virtual address in the module's own image base)
and the local export that shows it. Anything that could not be shown is written as
UNKNOWN. Firmware binaries and decompiled output stay local and out of Git; only
addresses, offsets and derived constants appear here.

Local inputs, all read-only:

| Input | What it supplies |
|---|---|
| `fw493/sys_external/libfontFT.prx` | Sony's FreeType bridge module. SHA-256 `0d316a76c03a4b7f5cb9a5088785bd3f2e54a64f1f2780012366209bbd2afa52` (also recorded in `FONT-MATCHING.md`) |
| `out/native-reference/fontft-imports.csv`, `fontft-functions.csv` | libfontFT import table and function ranges |
| `out/native-reference/fontft-bridge.c` | decompiled libfontFT glyph path (local, not committed) |
| `out/native-reference/font-exact-lowlevel.c`, `text-font-lowlevel.c`, `text-font-wrappers.c`, `text-string-refs.c` | decompiled PAF (vsh.elf) text path |
| `out/native-reference/paf-range.c` | wider vsh.elf/PAF decompile range used for the metric and animation functions |
| `out/native-reference/explore_plugin_all.c` | decompiled explore_plugin (label/ellipsis logic) |
| `out/native-reference/lines/cg-programs.txt` | Cg program inventory of the custom renderer |
| `out/native-reference/rco-behavior/resources.json` | object/animation dump of all 122 dev_flash RCOs |
| `out/native-reference/objattribdef-ps3.ini` | RCO object field schema |
| `C:\dev\rpcs3\bin\dev_flash\vsh\resource\*.rco` | local decrypted RCOs for the object dumps |
| `native-xmb/src/text_renderer.cpp`, `font_raster.cpp`, `view.cpp` | native implementation |
| `work/inspect-vsh-text.ts`, `work/inspect-explore-constants.ts` | small read-only probes written for this audit (constants and data table dumps) |

No RPCS3 capture, input or network access was used for this document.

## 1. The firmware rasteriser module

Sony ships two layers:

- `libfont.prx` (the `cellFont` API) and `libfontFT.prx` (the `cellFontFT` API).
  `libfontFT.prx` is the FreeType bridge: it imports the `cell_FreeType2` module and
  forwards `cellFontFT_*` calls into `FT_*` calls. This is the "libfont-style module"
  the earlier audit item refers to.
- `vsh.elf` contains PAF (`PhFont`, `PhText`, `PhLabel*`) which selects a font face,
  binds a renderer, measures strings and finally calls the glyph draw path.

The glyph path is `cellFontFT_65E27CB3 @ 0x6aa4` (range `0x6aa4-0x761f`). Evidence
from `fontft-bridge.c` (line numbers are in that file):

- `0x2232` `FT_Get_Char_Index` (import stub `0x95a0`, NID `0x4F36CF3F`) maps the code
  unit to a glyph index.
- `0x2236` `FT_Load_Glyph` (stub `0x9760`, NID `0xB229E6BF`) with flags **10**.
  The additional-character loop repeats the same call at `0x2363`.
- `0x2351`/`0x2492` `FT_Outline_Get_Bitmap` (stub `0x9500`, NID `0x18C23BDF`) renders
  the outline into an 8-bit bitmap.
- `0x2193`/`0x2222` `FT_Set_Transform` (stub `0x9620`, NID `0x7EA2C657`) with a
  4-field matrix and, on one branch, a 2-field vector; see section 1.3.
- `0x2309`/`0x2438` `FT_Outline_Embolden` (stub `0x9580`, NID `0x4B4CBF7A`) for the
  numeric weight effect; see section 1.6.

Size and resolution setup is separate:

- `cellFontFT_529AFC47 @ 0x50b4` forwards character sizes and DPI to `FT_Set_Char_Size`
  (stub `0x9560`, NID `0x46D8826F`) and stores the DPI into the face wrapper, with
  `0x48` (72) when the caller passes 0.
- `cellFontFT_F7715A5B @ 0x5054` uses `FT_Set_Pixel_Sizes` (stub `0x9780`,
  NID `0xD12673A0`) and also stores `0x48` (72) DPI.
- PAF's own constants confirm this: 72.0 at VA `0x68e0c4` and `0x68e0f8`, and the
  7.02 fallback point size at VA `0x68e0f4` (used by `FUN_004064b4`, called from
  `FUN_00379ba8` in `text-string-refs.c`).
- 26.6 fixed point is the working unit: `1/64 = 0.015625` at VA `0x68e104`, used by
  PAF's glyph draw `FUN_0037a774` to convert positions (`text-string-refs.c:44`).

The import table (`fontft-imports.csv`, 30 entries) is itself evidence: it contains
no `FT_Render_Glyph`, no `FT_Set_Renderer`, no `FT_Property_Set`, no
`FT_Library_SetLcdFilter` and no `FT_GlyphSlot_Embolden`. The module therefore has
exactly one rasterisation route (outline -> `FT_Outline_Get_Bitmap`), one outline
loader (`FT_Load_Glyph`) and no way to change the hinting engine or enable LCD
filtering from this module.

### 1.1 Hinting mode: none (proven)

`FT_LOAD_NO_HINTING = 0x2`, `FT_LOAD_NO_BITMAP = 0x8`; flags 10 is their sum. Both
glyph-load sites in the draw function (primary `0x2236`, additional character
`0x2363`) use 10. Consequences, all direct:

- no bytecode/blue-zone grid fitting and no autohinter (no stem snapping);
- no vertical-only ("slight") grid fitting either - the outline is scaled to the
  26.6 size and left alone;
- no embedded bitmap strike (`FT_LOAD_NO_BITMAP`);
- grayscale coverage only, because `FT_Outline_Get_Bitmap` writes the standard 8-bit
  anti-aliased mask and no LCD filter is imported.

This matches what `FONT-MATCHING.md` already recorded and what the native FreeType
path already does (`font_raster.cpp:229` passes the flag word, `text_renderer.cpp:106`
passes 10). It is *not* a Qt hinting preference or a FreeType build option: it is a
per-call flag.

### 1.2 Stem snapping and grid fitting

None, for the reason in 1.1. There is no separate "snap stems" path in the module.
The only snapping anywhere near the glyph path is the rounding of the destination
rectangle from 26.6 to whole pixels (section 1.3), which is a blit-origin rounding,
not stem alignment.

### 1.3 Subpixel positioning: phases exist, outline translation unproven

What is proven:

- PAF passes a 26.6 pen position with a fractional part. `cellFontFT_65E27CB3` takes
  two 26.6 phase arguments and masks them with `0x3f`: `(param_6 & 0x3f)` and
  `(param_7 & 0x3f)` (`fontft-bridge.c:2529-2539`). The sibling function
  `cellFontFT_40DCBC8D @ 0x4c24` does the same at `fontft-bridge.c:1014-1038`.
- The destination rectangle is rounded from that phase with a `+0x3f` bias, i.e.
  `((phase + pen) + 63) >> 6`. So the blit origin is a whole pixel derived from a
  fractional position.
- `FT_Set_Transform` is invoked before `FT_Load_Glyph` (`0x2193`/`0x2222`), with a
  matrix in both branches and a delta vector in one branch. That is the standard
  FreeType mechanism for a fractional outline translation.

What is not proven:

- The contents of that matrix and delta. In the saved export both are stack locals
  that are never written in the visible decompile (`local_98`, `local_a0`), and the
  32-byte metrics struct passed by the callers is likewise uninitialised in the
  export (`cellFontFT_400B7E88 @ 0x7620`, `cellFontFT_5EAA5C26 @ 0x7690`). The
  matrix is the only place a synthetic *slant* could be applied (there is no
  dedicated FreeType slant call), and the delta is the only subpixel channel, so
  both roles are plausible - but neither is proven by the export.
- Whether the coverage itself is rendered at the fractional phase (true subpixel
  positioning) or only the blit origin is rounded. The code re-renders the outline
  per call rather than fetching a cached bitmap, so per-phase rendering is possible,
  but the operand evidence is missing.

Verdict for the audit item: subpixel pen phases of 1/64 pixel exist and affect
placement rounding; per-phase outline translation is LIKELY BUT UNPROVEN. Do not
implement it as firmware fact yet.

### 1.4 Gamma and coverage-to-alpha: identity at this stage, later stage UNKNOWN

- The rasteriser output is FreeType's ordinary 8-bit anti-aliased coverage. Nothing
  in libfontFT converts it through a lookup table: the module imports no table, no
  gamma API and no shader hook, and the bitmap is passed to the caller unchanged.
- PAF's two coverage copy routines also apply no curve:
  - `FUN_004056d8 @ 0x4056d8` (`text-font-lowlevel.c:126-175`) compares the
    destination byte with the incoming coverage byte and, only when the incoming
    value is larger, writes a fixed colour plus that coverage byte:
    `*puVar10 = param_2 << 24 | (param_3 & 0xff) << 16 | (param_4 & 0xff) << 8 | *pbVar5`.
  - `FUN_004057a0 @ 0x4057a0` (`text-font-lowlevel.c:178-225`) is the mask-only
    version: `if (*pbVar7 < *pbVar6) *pbVar7 = *pbVar6;`.
- Neither routine clamps, squares, adds or tables the value. So at the
  coverage-to-alpha step the curve is **alpha = coverage byte** (identity), i.e.
  linear in the rasteriser's coverage.
- What happens after that (the GCM/RSX blend that puts the mask on screen, plus any
  tone/glare pass) is NOT covered by local evidence. There is no text shader or
  fragment program in the text modules, and the screen blend state for PAF text was
  not traced. Any display gamma is therefore UNKNOWN.
- Also unresolved (do not claim): `FUN_004056d8` packs coverage into the low byte of
  a 32-bit word whose other three bytes are the fixed colour, and compares against
  the same byte of the destination. Which channel that byte is depends on the
  destination texture format, which was not established. What is unambiguous is the
  *algorithm*: compare-and-keep-larger, not add and not alpha-over.

### 1.5 Max-coverage / overlap rule: compare-and-keep-larger (proven copy rule; scope UNKNOWN)

`FUN_004057a0 @ 0x4057a0` is literally a bytewise maximum over a destination buffer:
a new coverage value replaces the stored one only if it is larger. `FUN_004056d8`
uses the same comparison for the coloured variant. So when PAF writes 8-bit glyph
coverage into a destination image it does so as a **union that saturates**: never an
add and never an alpha-over. A second write to the same byte can only increase it.

What is proven is that copy rule. What is NOT established locally is the scope of the
destination:

- `FUN_0037a774` draws one glyph per call, and `FUN_00406898`
  (`text-font-wrappers.c:199-222`) converts one coverage surface at a time into a
  descriptor passed in by the caller.
- libfontFT zeroes the FreeType bitmap it is about to fill (`cellFontFT_65E27CB3`,
  `fontft-bridge.c:2204-2221`), which is the rasteriser's scratch bitmap and not
  necessarily a shared string mask.

So "overlapping glyphs accumulate by max" is consistent with the copy semantics but
is not proven to be how glyph-to-glyph overlap is resolved; the alternative is that
each glyph is converted separately and on-screen overlap is decided by the RSX blend,
which was not traced (1.4). Treat the max rule as the firmware's coverage-copy rule
until a shared mask is demonstrated.

### 1.6 Weight and slant effects (context for parity)

Per-style numeric effects are real data, not guesses. `PhFont::SetStyle`
(`FUN_00378e54`, `text-font-wrappers.c:41-165`) selects a weight and a slant from
constants at VA `0x68e0a8`-`0x68e0c4`:

| VA | Value | Role |
|---|---:|---|
| `0x68e0a8` | 1.04 | weight for styles 7, 8, 0x69, 0x6a |
| `0x68e0ac` | 1.00 | weight for styles 2, 4 and default |
| `0x68e0b0` | 1.025 | weight for styles 5, 6, 0x68 |
| `0x68e0b4` | 0.93 | weight for style 0x65 |
| `0x68e0b8` | 0.96 | weight for style 0x66 |
| `0x68e0bc` | 0.60 | slant for styles 2, 4, 6, 7, 8, 0x69, 0x6a |
| `0x68e0c0` | 0.00 | slant for styles 5, 0x65, 0x66, 0x68, default |

The draw function `FUN_0037a774` re-selects the same values from
`0x68e10c`-`0x68e128` (`text-string-refs.c:160-225`), so the style table is applied
at glyph draw time as well. Weight is finally applied inside libfontFT as
`FT_Outline_Embolden` with a 26.6 strength; its clamp is
`clamp(weight - 1, -0.07, 0.04)`, proven by `FUN_000588f0` and the constants
`0x6eebe4 = 1.0`, `0x6eebec = 0.04`, `0x6eebf0 = -0.07` (see also `FUN_0005897c`
for the slant clamp). Slant has no dedicated FreeType call, so it has to travel
through the transform matrix of section 1.3 - the coupling is plausible but the
matrix contents are unproven.

## 2. Native renderer today, and what Qt can and cannot express

Native references below are to `native-xmb/src/text_renderer.cpp`,
`native-xmb/src/font_raster.cpp` and `native-xmb/src/view.cpp`.

### 2.1 Hinting

- Native today: coverage comes from FreeType with the same flag word 10
  (`font_raster.cpp:229`, `text_renderer.cpp:106`), so the *rasterised coverage* path
  already matches the firmware. Shaping and glyph positions still come from
  `QTextLayout`/`QGlyphRun` (`text_renderer.cpp:154-170`).
- Qt limit: `QFont::HintingPreference` only expresses a preference, and Qt documents
  that its effect is engine dependent. On Windows with DirectWrite,
  `PreferNoHinting` still produces vertical hinting; only Qt's FreeType engine maps
  it to no hinting (Qt 6 `QFont::HintingPreference` table:
  https://doc.qt.io/qt-6/qfont.html#HintingPreference-enum). So `draw()` cannot ask
  the platform engine for unhinted metrics or positions (`text_renderer.cpp:183`).
- Closest achievable mapping now: keep FreeType as the metric authority for both
  coverage and advances - replace `QGlyphRun::positions()` with advances obtained
  from FreeType (`FT_Get_Advance` / linear advance in 26.6) for matched faces; keep
  Qt shaping only for glyph selection, kerning lookups and fallback-face choice.
  Alternative: build Qt against its bundled FreeType font engine (the SDK ships it),
  which makes `PreferNoHinting` effective. Neither is implemented.

### 2.2 Gamma

- Native today: none. The fragment shader uses coverage directly and premultiplies:
  `fill = coverage.r * tint.a`, `result.rgb = tint.rgb * fill` (`text_renderer.cpp:56-59`).
- Qt limit: no public API renders glyph coverage through a caller-supplied curve;
  `QRawFont::alphaMapForGlyph` and QPainter both hand back linear coverage.
- Achievable now: apply the curve in the existing shader - a 256-entry 1D LUT texture
  or a `pow()` on `coverage.r`/`coverage.g` before compositing. This needs no change
  to Qt or to the atlas layout. Because the firmware curve is UNKNOWN (1.4), the
  exponent can only be fitted against reference crops; it must not be described as a
  firmware value.

### 2.3 Max-coverage for overlap

- Native today: one textured quad per glyph, drawn with
  `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` (`text_renderer.cpp:195`). Overlapping
  coverage composes as alpha-over, so the native path does not follow the firmware's
  compare-and-keep-larger copy rule. Whether that rule governs glyph-to-glyph overlap
  is unproven (1.5), but the copy semantics are the closest evidence available.
  This is the same gap `TEXT-RENDERER.md` and `FONT-MATCHING.md` already list.
- Achievable now: draw the string into an offscreen coverage mask with
  `glBlendEquation(GL_MAX)` (valid in GL 3.3 core for both RGB and alpha) and
  composite that mask once; that matches the firmware's copy rule under either scope
  interpretation. On the QPainter fallback path, `QPainter::CompositionMode_Lighten`
  is a per-channel max and is the closest built-in equivalent.
- Needs work: an FBO/mask target, a second pass per string, and a decision on
  whether the halo and shadow are inside or outside the mask (the native renderer
  currently bakes the halo into each glyph tile, which is incompatible with a
  single-string mask).

### 2.4 Subpixel phases and origin snapping

- Native today: glyphs are rasterised at the layer scale and each glyph origin is
  snapped to whole output pixels (`text_renderer.cpp:181`, `:218`). That is a
  deliberate, defensible substitute for the firmware's rounded 26.6 placement, and it
  fixed the soft-text defect recorded in `TEXT-RENDERER.md`.
- Firmware: the blit origin is also rounded to a whole pixel, but from a fractional
  26.6 position (`+0x3f` bias). Whether the outline is additionally shifted by the
  phase is unproven (1.3).
- Achievable now: if subpixel phases are later confirmed, feed
  `FT_Set_Transform` a per-glyph delta in 26.6 before `FT_Load_Glyph` in
  `FontRaster::rasterize`; the atlas tile identity would then need the phase in its
  key. Do not do this on the current evidence.

## 3. Open audit item: glow overrides

### 3.1 What the local firmware evidence shows

- The RCO `Text` schema has no glow/halo field at all. Its only optical fields are
  `shadowX`, `shadowY`, `shadowPerspective`, `shadowRed`, `shadowGreen`,
  `shadowBlue`, `shadowAlpha` (`objattribdef-ps3.ini`, `[Text]`; offsets already
  listed in `TEXT-RENDERER.md`).
- There is no glow-named texture for the UI plugins. `explore_plugin_full`'s image
  list contains no glow/halo/bloom texture (`out/native-reference/rco-images.txt`);
  the only `Glow*` image names in dev_flash belong to eula/regcam/store dialogs.
- Labels are not animated by RCO animation data: `explore_plugin_full.rco` and
  `sysconf_plugin.rco` both contain 0 animation entries. Animations do exist in other
  resources (`custom_render_plugin.rco` 51 entries, `premo_plugin.rco` 30,
  `system_plugin.rco` 17, and a few others; see
  `out/native-reference/rco-behavior/resources.json`), but the dump associates none of
  them with a Text object. So the label focus effect is not evidenced as an RCO
  keyframe animation.
- PAF does have a dedicated shadow surface: the text subsystem allocates a VM block
  named "Text(shadow)" of `0x2c000` bytes (`paf-range.c:6039`, `:6044`; freed at
  `:5906`). That proves the shadow is rendered into its own buffer, but both copy
  routines are hard-edged byte copies with no blur (1.4), so the shadow surface cannot
  produce a soft halo by itself.
- Runtime property animation IS possible from code: PAF exposes an animation
  constructor that takes a duration, a property-path string and callbacks
  (`FUN_0039af2c @ 0x39af2c`, `paf-range.c:16110-16130`; the path string
  `"> subscript"` and the descriptors `0x6df400`/`0x6df6a8` are passed to
  `FUN_00399e08`). The label code calls a thin wrapper, `FUN_0003384c`, at
  `explore_plugin_all.c:91971` and `:92016`; that wrapper forwards to the PAF export
  with NID `0xDE113DC2`, which `vsh_exports.csv` maps to `FUN_0039af2c`. The exported
  prototypes there are inconsistent (the wrapper is called with two arguments but
  typed with five), so WHICH text property is animated is NOT established - only that
  a code-driven property animation channel exists.
- The custom renderer plugin does contain bloom/blur Cg programs
  (`lib/glutils/BloomCone.fpo`, `BlurAcc.fpo`, `BlurOutput.fpo`,
  `GaussBloomAndBlur.fpo`, `out/native-reference/lines/cg-programs.txt`), and one of
  them samples the screen (`BlurOutputFG.fpo` reads `_Screen`). But that module's
  strings contain no font or text-rendering reference (its only "text" strings are
  image names such as `icon_chatroom_text`), so a screen-space bloom over text is
  possible in principle and unproven for labels.

### 3.2 Verdict

UNKNOWN. No local evidence identifies a firmware glow or halo on text, and no
evidence identifies the runtime override values that would produce one. The 3.4 s
breathing and the faint-to-peak ratio come from the MaXMB author's console
observations recorded in `TEXT-RENDERER.md`, not from firmware. The RCO shadow
values (4, -5, RGB 0.25, alpha 1) remain template coordinates with an unresolved
runtime conversion. Nothing here should be described as a firmware glow default.

### 3.3 Native today (for contrast)

- Halo: a Gaussian of sigma `2.2 * rasterScale` and radius 6 is baked into each
  glyph tile as a second channel (`text_renderer.cpp:17`, `:110-115`), then drawn
  with `glowStrength = glow * 1.5` and tint RGB `(0.922, 0.961, 1.0)`
  (`text_renderer.cpp:56-57`, `:199`).
- Breathing: `0.5 - 0.5*cos(2*pi*t/3400)` quantised to 24 steps, applied from 0.35
  of the peak (`view.cpp:582-584`, `:890-894`).
- Shadow: one scene pixel right/down, black at 20% alpha
  (`text_renderer.cpp:202-205`).
- QPainter fallback (renderer not ready): draw the text in a ring of offsets
  (`view.cpp:716-723`) and a 1 px black shadow at alpha 38 (`view.cpp:713`).

These are measured approximations, not recovered firmware values. Achievable now:
none of the numbers; the honest step is to keep them labelled approximate and to
stop treating the RCO shadow as the source of the halo.

## 4. Open audit item: elision and cursor metrics

### 4.1 PAF text metrics (proven shape)

- Glyph draw for a whole string is `_opd_FUN_00397f9c @ 0x397f9c`
  (`paf-range.c:14231-14275`). It walks a character range, gets each character's
  cell with `_opd_FUN_0037b08c`, adds kerning with `FUN_00397ca8 @ 0x397ca8`, and
  advances a 26.6 pen position.
- Per-line drawing is `FUN_0039399c @ 0x39399c` (`paf-range.c:12057-12106`). It
  draws only characters `param_7 .. param_8-1` of the line
  (`paf-range.c:12084-12087`), so characters outside the visible range are simply not
  drawn. There is no font-layer ellipsis substitution.
- String width is measured by `_opd_FUN_00397e5c @ 0x397e5c`
  (`paf-range.c:14190-14228`): it sums every character's advance
  (`local_6c[0]`) plus the kerning returned by `FUN_00397ca8`
  (`paf-range.c:14215-14223`). The sum is an integer count of whole advances; the
  layout code converts it to pixels with a `+0x3f >> 6` round
  (`paf-range.c:12222-12223`), which is consistent with 26.6 units.

### 4.2 Elision is a UI-layer text object (proven structure)

explore_plugin looks label text objects up by name. The names
`label2_text`, `label2_text_ex` and `label2_text_ellipsis` are adjacent in the
plugin's string data (`explore_plugin.prx` file offsets `0x265248`-`0x265274`) and are
used throughout `explore_plugin_all.c:91896-92346`:

- `FUN_0017d400` (`explore_plugin_all.c:91979-91995`) compares the label's measured
  extent against its limit and reports whether it is over.
- On overflow, `FUN_0017d838` (`:92000-92039`) calls `FUN_0017d1d4`
  (`:91929-91976`), which measures the label text object, computes the position of
  the last visible character and then positions/applies the separate
  `label2_text_ellipsis` text object at that point (`:91949-91971`), followed by a
  property update on both objects.
- `FUN_0017daf4` (`:92066-92100`) resolves the same two names and calls a
  one-argument object method on `label2_text` (vtable slot `+0x10`); the exact
  operation (show, hide or reset) is not established.

So the firmware ellipsis is a **second Text object placed after the truncated
string**, managed by the plugin that owns the label, not by the font or text-drawing
layer. This is the answer to the audit item: there is no PAF elision algorithm to
port; the equivalent is a separate ellipsis draw call positioned from the metric
path.

What stays UNKNOWN:

- The exact placement formula. The exported call prototypes around
  `FUN_0017d1d4` do not agree with the callees' signatures (for example the calls at
  `:91951-91971` pass argument counts the callee decompiles do not read), so the
  arithmetic (`x = textX + f(charIndex)`, `y = textY - (lineHeight + 3) * lineIndex`)
  is indicative only. Do not copy it.
- Whether the label's own text is clipped (by a plane/scissor) or pre-truncated by
  the plugin. Both are consistent with the exported code.
- The ellipsis glyph source. `FUN_0039399c` also draws two fixed 2-code-unit suffixes
  from constants `0x68e74c` and `0x68e750`, guarded by flag bits 0 and 1 of the text
  object (`paf-range.c:12088-12095`). The bytes at those addresses fall inside a
  float-constant block, and read as UTF-16 they are `U+25C1`/`U+21E9`, which is not an
  ellipsis. The operands are therefore not trustworthy in the saved export; do not
  implement a suffix from them.

### 4.3 Cursor metrics

- No cursor object exists in the OSK RCOs. The complete object lists of
  `oskpanel_plugin.rco` (135 objects), `oskfullkeypanel_plugin.rco` (132) and
  `osk_plugin.rco` (31), dumped in `rco-behavior/resources.json`, contain no cursor
  or caret object. The text-field caret must be drawn by the OSK plugin code
  (`osk_plugin.prx` / `oskpanel_plugin.prx`), for which no local decompile exists.
- Consequently the blink period, caret width, colour and vertical extent are
  UNKNOWN. What is proven is only the metric primitive that a caret position would
  use: PAF exposes a per-character advance accessor (`0xC7630260 -> FUN_0038f900 @
  0x38f900`, `paf-range.c:9521-9535`) that indexes a per-character record and scales
  a 26.6 advance by a float constant.
- Native today: the OSK caret x is
  `QFontMetricsF(fieldFont).horizontalAdvance(osk_.text.left(osk_.cursor))` with a
  26 px field font, a 2 x 36 px rectangle and a 500 ms on/off toggle
  (`view.cpp:1215-1217`).
- Achievable now: replace that Qt metric call with the same FreeType 26.6 advance sum
  used for glyph placement, so the caret and the glyphs share one metric authority
  (the firmware's). Do not change only the caret while glyph positions still come
  from `QTextLayout`, or the two will disagree.
- Needs work: the OSK caret's visual parameters remain approximations until the OSK
  plugin is traced or measured.

### 4.4 Native label elision today

`QFontMetrics(labelFont).elidedText(entry.title, Qt::ElideRight, ...)` at a 27 px
label font, with 1050 px or 640 px limits (`view.cpp:888`). This substitutes
`U+2026` inside the string using the platform font engine's metrics (on Windows that
is DirectWrite, which applies vertical hinting per Qt's own table), which is a
different mechanism from the firmware's separate ellipsis object and a different
metric source.
Achievable now: draw the label text up to the last whole glyph that fits (using the
FreeType advance sum), then draw a separate ellipsis draw call at that pen position -
structurally the firmware approach. Needs work: the truncation rule (whole glyph vs
cluster) and the ellipsis object's own font/size/colour must come from the RCO/UI
data, which has not been recovered.

## 5. Summary

| Item | Firmware behaviour + evidence | Native today | Achievable now | Needs work |
|---|---|---|---|---|
| Hinting mode | None. `FT_Load_Glyph` flags 10 = `NO_HINTING` + `NO_BITMAP` at `cellFontFT_65E27CB3 @ 0x6aa4` (`fontft-bridge.c:2236`, `:2363`); no hinting/LCD/renderer imports in `fontft-imports.csv` | Coverage already FreeType flags 10 (`font_raster.cpp:229`); shaping/metrics from `QTextLayout` | Yes: keep FreeType coverage; take advances/positions from FreeType in 26.6 for matched faces | Move shaping metrics off Qt's platform engine; or build Qt with its FreeType engine |
| Stem snapping / grid fit | None (follows from flags 10) | None in the FreeType path; Qt fallback uses vertical hinting | Yes (already) | Nothing |
| Subpixel positioning | 26.6 pen phases exist (`& 0x3f`, `+0x3f` rounding, `fontft-bridge.c:2529-2539`, `:1014-1038`); `FT_Set_Transform` called with matrix and optional delta (`:2193`, `:2222`). Phase source and outline translation UNKNOWN (operands uninitialised in export) | Glyph origins snapped to whole output pixels (`text_renderer.cpp:218`); rasterised per layer, no phase | Origin rounding is equivalent; per-glyph delta can be added if phases are confirmed | Prove the transform operands from disassembly before implementing subpixel rendering |
| Gamma / coverage to alpha | Identity: rasteriser coverage copied unchanged; `FUN_004056d8 @ 0x4056d8` and `FUN_004057a0 @ 0x4057a0` apply no curve (`text-font-lowlevel.c:126-225`) | No curve; shader uses coverage directly, premultiplied (`text_renderer.cpp:56-59`) | Yes: 1D LUT or `pow()` in the fragment shader; no Qt dependency | Curve value UNKNOWN; fit only from reference crops, never call it firmware |
| Overlap / max coverage | Compare-and-keep-larger copy rule: destination byte replaced only when the new coverage is larger (`FUN_004057a0`, `text-font-lowlevel.c:216-218`); no add, no over. Whether glyphs share one mask is UNPROVEN (1.5) | Per-glyph quads with `GL_ONE / ONE_MINUS_SRC_ALPHA` alpha-over (`text_renderer.cpp:195`) | Yes: render string to a mask with `GL_MAX`, composite once; `CompositionMode_Lighten` on the QPainter fallback | FBO/mask pass; decide halo/shadow membership in the mask |
| Weight / slant effects | Per-style weights 1.04/1.025/1.0/0.96/0.93 and slants 0.6/0.0 from VA `0x68e0a8-0x68e0c4` and `0x68e10c-0x68e128`; weight = `clamp(w-1,-0.07,0.04)` via `FT_Outline_Embolden`, clamp constants `0x6eebe4`, `0x6eebec`, `0x6eebf0` | Not applied per style; Qt bold/italic only | Weight via `FT_Outline_Embolden` could be added over the outline path | Slant channel unproven (transform matrix contents); do not apply one weight universally |
| Glow / halo | No `Text` glow field, no glow texture for explore, no RCO animation on text; `Text(shadow)` surface exists (`paf-range.c:6039-6044`) but the copiers are hard-edged; code-driven PAF property animation exists (`FUN_0039af2c @ 0x39af2c`). Mechanism and override values UNKNOWN | Gaussian halo sigma 2.2, radius 6, breathing 3.4 s from 0.35, shadow 1 px black 20% | Nothing firmware-faithful | Firmware trace or a console measurement; both required before any value is claimed |
| Elision | Plugin-managed separate `label2_text_ellipsis` Text object, positioned from the metric path (`explore_plugin_all.c:91929-92360`); PAF itself only draws a character range (`paf-range.c:12084-12087`) | `QFontMetrics::elidedText` with Qt hinted metrics (`view.cpp:888`) | Yes: draw the fitting glyphs with FreeType advances, then a separate ellipsis draw at that pen x | Truncation rule, ellipsis font/size/colour and exact placement formula UNKNOWN |
| Text metrics | Integer 26.6 advance sums plus kerning (`_opd_FUN_00397e5c @ 0x397e5c`, `paf-range.c:14215-14223`) | Qt `QTextLayout` positions (`text_renderer.cpp:168-171`) | Yes: FreeType advances summed in 26.6, matching PAF's shape | One metric authority for glyphs, elision and caret |
| Cursor | No cursor object in any OSK RCO (135/132/31 objects dumped); caret is code-drawn in the OSK plugin, not decompiled. Blink/width/colour UNKNOWN; per-char advance accessor exists (`FUN_0038f900 @ 0x38f900`) | `QFontMetricsF::horizontalAdvance` of the prefix, 2x36 px, 500 ms toggle (`view.cpp:1215-1217`) | Yes: caret x from the FreeType advance sum | Blink period, extent and colour; must come from OSK tracing or measurement |

## 6. What would close the remaining unknowns

1. Re-decompile `cellFontFT_65E27CB3 @ 0x6aa4` with the actual instruction stream
   (not the current export) to read the stores that fill the `FT_Set_Transform`
   matrix and delta. That single item decides subpixel positioning and the slant
   channel.
2. Trace the GCM/RSX blend used for PAF text (or capture the final framebuffer of a
   single known string) to get the display stage of the coverage-to-alpha curve.
3. Find the code path that writes the text object's shadow fields at runtime
   (a PAF property path or setter called from explore_plugin) to decide whether the
   RCO template shadow is ever overridden.
4. Decompile `osk_plugin.prx` (or measure the caret on a console capture) for the
   caret geometry and blink period.
5. Re-export the PAF calls around `FUN_0039af2c`, `FUN_00397f9c` and
   `FUN_0039399c` with correct prototypes so the ellipsis arithmetic and the two
   fixed suffix draws can be read reliably.

## 7. Limits of this audit

- The decompiled exports are Ghidra output from local read-only projects. Two known
  artifacts are called out in place: uninitialised stack locals in the libfontFT
  transform path, and call/callee prototype mismatches around the PAF ellipsis math.
  Neither was worked around by guessing.
- No firmware was fetched, no RPCS3 run was performed, and no firmware payload is
  included in this document or committed anywhere.
- Where this document says UNKNOWN, the honest state is that no local evidence
  distinguishes the possibilities. Marking those items done would require the work in
  section 6.
## Applied: hinting disabled in the native renderer

The three Qt font sites that had `PreferVerticalHinting` (`src/text_renderer.cpp:183` and the two
label helpers in `src/view.cpp`) now set `QFont::PreferNoHinting`, matching the traced
`FT_LOAD_NO_HINTING`. Measured effect on the canonical capture: none - `work/ab-layers.ts --capture`
reports the clock text at 13.51 dB / 0.6131 SSIM and the info bar at 17.12 / 0.7046 both before and
after, so the unhinted setting is behaviourally neutral at the 2x UI layer while matching the firmware.