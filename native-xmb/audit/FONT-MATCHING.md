# Firmware font matching — continuation handoff

Updated 2026-10-25. Original face identity and glyph load settings are implemented. Exact PAF pixel parity remains open.

## Recovered firmware behavior

Read-only Ghidra analysis of local firmware 4.93 `libfontFT.prx`, SHA-256
`0d316a76c03a4b7f5cb9a5088785bd3f2e54a64f1f2780012366209bbd2afa52`:

- `cellFontFT_65E27CB3` at `0x6aa4` calls import stub `0x9760` with glyph load flags **10**.
  Its import NID `0xb229e6bf` resolves to `FT_Load_Glyph`; 10 is `FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP`.
  The additional-character path in the same function uses the same flags.
- It calls stub `0x9500`, NID `0x18c23bdf`, `FT_Outline_Get_Bitmap`, after configuring transforms and effects.
  The bridge therefore rasterizes grayscale outlines rather than a Windows font engine's hinted coverage.
- `cellFontFT_529AFC47` at `0x50b4` forwards character sizes and DPI to `FT_Set_Char_Size` at stub `0x9560`.
  PAF setup in `vsh.elf` uses 72 DPI; its position and metric arithmetic uses 1/64 units.
- PAF `FUN_0037a774` maps private codes F8FC→3007, F8FD→00D7, F8FE→25A2, F8FF→25A1.
  These mappings explain why the native UI's previous U+2715 cross required a Windows fallback.

Local evidence: `out/native-reference/fontft-bridge.c`, `fontft-imports.csv`, `font-exact-lowlevel.c` and
the earlier `text-string-refs.c`. The existing Ghidra scripts were used against a dedicated `fontmatch493`
project; existing VSH analysis was read-only. REA's previously unavailable PPC route was not retried.
The string `cell_FreeType2_2_1` is an ABI/module name, not proof of the upstream FreeType version.
The firmware's `FT_Library_Version` reads runtime fields; its actual version remains unresolved.

## Native implementation

`FontRaster` owns a FreeType library and memory-backed copies of the user's local SCE-PS3 font files.
Every shaped QRawFont is matched to a local font by **byte-identical head and cmap tables**. Names alone
never establish identity. A bounded 128-entry sized-face cache avoids hashing large CJK cmap tables for every
glyph; active size is cached per face. Files stay local and memory is released with the renderer.

The atlas now uses FreeType with flags 10, grayscale normal rendering and actual bitmap bearings for matched
faces. Qt shapes with no hinting and explicitly requests Regular for ordinary text. Qt coverage remains a
logged fallback for unmatched faces or failed glyph loads. `XMB_TEXT_QT=1` selects the earlier Qt coverage path
for diagnosis. The host FreeType version is logged (2.14.2 in this build); it is not identified as Sony's version.
`FT_Render_Glyph(..., FT_RENDER_MODE_NORMAL)` invokes the grayscale outline renderer; reproducing Sony's
outline translation/rounding around `FT_Outline_Get_Bitmap` is still required for exact pixel equivalence.

The PAF private symbol mappings and the native U+2715 cross alias are applied before shaping. `--font-report`
now reports actual style and source filename, and detects host fallback by table identity. This also avoids
misclassifying firmware families such as DFHeiW5-A and VAGRundschriftDLig because their names lack SCE-PS3.

The build uses system `Freetype::Freetype` if supplied, otherwise the installed Qt SDK's BundledFreetype and
FreetypePrivate header targets. No font or FreeType source is downloaded by the build.
FreeType API definitions: https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html

## Verification and limits

The isolated native build succeeded. `--font-report` exited zero for Rodin, Seurat and VAGR sample runs:
Latin/Latin2, accents and combining marks, PAF controls, Japanese, Korean and Chinese all resolved to local
firmware files. This establishes source identity, not the firmware's exact fallback/style ordering. In particular,
Qt currently chooses YG-B-KOR for the Korean sample even when Regular is requested; that selection remains to trace.

Isolated and default builds succeeded; the default executable was reopened and verified through Computer Use.
Root captures at 960x540 and a 1920x1080-requested window (actual client 1920x1061), plus a message dialog,
and the Qt comparison replay exited zero. No unmatched
coverage-face warning occurred in the captured English UI. The supplied `reference-rpcs3-game.png` was read
offline; no RPCS3 capture/input was used. At a conservative RGB>140 foreground threshold, its Game label is
27×8 pixels in the 900×506 client area (28.81×8.54 normalized to 960×540), versus 29×9 in the native capture.
This checks one label's extent, not a pixel-match score: background, phase and recording filtering differ.
Images, face report and logs stay under `out/native-reference/font-match/`, outside Git.

## Next phase

1. Match complete focused/unfocused/subtitle/clock/dialog strings at the same client resolution and pixel phase.
2. Recover the firmware's rasterizer version, outline transforms and subpixel rounding; the current renderer
   retains its earlier integer size selection, 2x UI resolve and whole-output-pixel glyph-origin snapping.
3. Replace Qt shaping/advances, line metrics, elision and cursor metrics only after recovering PAF's equivalents.
4. Implement the recovered maximum-coverage accumulation for overlapping glyphs. Current atlas blending still
   uses alpha-over for glyph coverage; separate shadow/foreground passes do not solve this difference.
5. Trace per-label style/weight/slant and font-set fallback ordering. Do not apply the known weight effects
   globally or assume every regular CJK request selects the correct firmware face.
6. Recover runtime shadow and halo settings; the current subtle shadow and breathing halo remain approximations.
