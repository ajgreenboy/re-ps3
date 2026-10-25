# XMB render pipeline: firmware source of each layer

Goal: output identical to the PS3 XMB, implemented in C++. Every layer has to come from the firmware's own programs,
parameters and assets. This file records where each layer comes from in the 4.93 firmware and how far the port has got.

Local inventory (`out/native-reference/qrc-inventory.txt`, not committed) lists every `vsh/resource/qgl/*.qrc`.

## Layers, back to front

| Layer | Firmware source | Native status |
|---|---|---|
| Background gradient | `lines.qrc` moyou/background, back_colours0 + month_bg textures, BACKGROUND.mnu; custom_render_plugin clock mapping | **Ported**: firmware Cg translated RSX→GLSL, inputs traced |
| Ribbon (wave sheet) | `lines.qrc` lines1 / ffd_shader1 / LinesController, LINE1.mnu; geometry from the SPU spurs/moyou/spline/spline.elf | **Shaders ported; geometry wrong**: ours is a flat displaced 128x24 sheet, the firmware's is a rolled 128x128 sheet (see "Ribbon geometry") |
| Particles | `lines.qrc` particles_quads / particles_second, PARTICLES*.mnu; simulation runs on SPU particles.elf | **Partial**: shaders firmware; CPU simulation reconstructs the SPU job |
| HDR tone map + glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, HDR.mnu | **Ported** |
| Category icons | `icons.qrc` lib/icons/quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo; ICONS.mnu with day/night/yoake/higure/boot overrides; widgets/other/texenv.dds (env map), amb.bmp; `icontex.qrc` 1n–70n normal maps + dif | **Ported** (`icons.cpp`): firmware quad.vpo + icons_no_ss_no_shad.fpo, the variant the firmware runs at 1080p; uniforms from ICONS.mnu by the packing read from an RPCS3 capture; matches RPCS3's frame side by side |
| Item icons, labels, dialogs, info bar | explore_plugin / system_plugin via paf (RCO layouts and textures) | **Reconstructed**: RCO textures are the firmware's; positions, sizes and fades measured from the 60 fps reference |
| Text | paf font renderer with the RD-R/B (SCE-PS3 Rodin) faces from dev_flash/data/font | **Partial**: native-resolution GL glyph atlas, Qt shaping/rasterization; PAF coverage/style trace in TEXT-RENDERER.md; hinting and glow parity open |

`raf.qrc` (ubber shaders, ConeFilter), `rhm.qrc`, `canyon.qrc`/`earth.qrc` (visualiser themes) and `store.qrc` are
outside the main XMB.

## Ribbon geometry (captured 2026-10-25)

The ribbon geometry was captured from RPCS3 with `XMB_VTX_DUMP` (patch: `rpcs3-rsx-capture.patch` at the repo root;
the draw is identified by lines1.fpo's 336-byte ucode). Each frame is a single indexed draw:

- 35,306 big-endian u32 indices form triangle strips with primitive restart (1016 strips of 32–34 indices), over
  16,384 vertices: a 128x128 grid where vertex = row*128 + column, and strips pair row r with row r+1.
- Array 0 holds float4 positions (stride 32) that are already in clip space: lines1.vpo passes `in_0` straight to
  `gl_Position`, with w ≈ 8. Array 9 holds float4 normals (stride 32), array 8 float2 coordinates (stride 8).
- The camera and projection are applied by the SPU; there is no matrix in the vertex constants.
- The shape: rows run the full width of the screen, while each column closes into an ellipse. The sheet is rolled
  into a travelling scroll, which produces the strands crossing through each other. Our flat FFD-displaced 128x24
  sheet cannot produce this, which is why the wave looks wrong.

To match it exactly, the geometry has to come from spline.elf. It is 22 KB of SPU code (.text 0x3060..0x8728,
.rodata 5 KB, .bss 19.5 KB) with no symbols, and Ghidra has no SPU module. Plan: write an SPU disassembler, trace the
SPURS job's inputs (the parameters custom_render_plugin passes, time, LINE1.mnu) and outputs (DMA of positions and
normals), then either port the simulation to C++ or run the original code in a small embedded SPU interpreter.
Verify against frames captured from RPCS3 at the same time.

## Category icon renderer (ported 2026-10-25)

How it was resolved: the plugin's VMX packing code decompiles too poorly to read, so the values were taken from a real
run instead. The local RPCS3 build gained `XMB_FP_DUMP` (each fragment program as drawn, constants patched in, plus
vertex constants and texture units), and `re-ps3-native --decode-fp` turns a capture back into named uniforms.
Findings, now in `icons.h`:

- At 1920x1080 the firmware runs `icons_no_ss_no_shad` (SHADOW STREN is 0).
- Camera distance is 2700; ndc = (2.8125 X, 5 Y) / d, so world units are 1080p pixels.
- Quads are 117.13 px (unfocused) and 167.33 px (focused), with icons 200 px apart plus 10 px either side of focus.
  The focus column is at x 565, y 290 (280 focused).
- Item icons (captured by driving RPCS3: Settings, then a page; keyboard pad as Player 1 for the session). icontex
  items use the same shader at brightness 1. In the open category column they are 77.616 units (focused 184.8) on the
  same rows as the layout: screen y = 540 + `_Eye.y`, with focus at 505. Settings bullets inside a page are 35.721
  units (focused 85.05). When a page opens, the parent column's unfocused rows move back to camera distance 2857.2 at
  brightness 0.247, so they shrink toward the screen centre and the gap below focus closes by 15. Its focused row
  stays at 2700 with brightness 0.698 and, with the category icon, steps to x 303.
- `_Ambiant.w` is 0.698 for unfocused icons and 1 for focused ones. The program's alpha (envmap alpha × `_Ambiant.w`)
  blends the icon over the frame.
- Light directions are (sin Z cos X, sin Z sin X, cos Z) of the LITn angles. `_EnvRot` turns about 30°/s.
- tex3 is a 128×128 px CookieCutter cut of the frame around each icon, refracted per channel.

Translator fixes found by diffing against RPCS3's own GLSL for this program (`Log shader programs`):
- LIF (0x3C) is (1, y, y>0 ? 2^w : 0, 1), not full LIT. This was why the icons came out saturated white.
- DIVSQ uses sqrt(|b|) and keeps a when a is 0; NRM leaves zero-length vectors alone.
- Source and destination precision clamps (fixed12 ±2, fixed9 ±1, saturate), with NaN read as 0.
- H registers are rounded to fp16. H and R are not aliased (RPCS3 keeps them apart).

Clock inputs updated: `_ChangingColor` now samples amb.bmp and `_LuminanceParams` derives from GLASS; see
`ICON-MATERIAL-CLOCK.md`. Daytime visual parity still needs a matched capture. Item
icons in the open column still go through Qt. The background is brighter than RPCS3's at the same hour, which also
lightens the refracted unfocused icons. Diagnosis: `XMB_ICON_UCODE=<capture>` runs on the firmware's captured
constants; `XMB_ICON_DEBUG=0..4` shows a single input texture or the result.

### Original notes

The firmware draws category icons as lit glass. Each icon is a quad (quad.vpo, `_ModelviewProjection`,
`_CookieCutterCenter/Ratio`). The fragment programs read these:

- `_NormalMap` (tex0): icontex `Nn`. Its alpha channel is the coverage, and a second fetch at a shadow offset gives the
  shadow.
- `_DiffuseMap` (tex1) and `_EnvMap` (tex2): `texenv.dds`, rotated by `_EnvRot`.
- `_BackgroundMap` (tex3): the frame behind the icon, refracted per channel (`REFR R/G/B` −0.032/−0.038/−0.048,
  `REFR SCL`, `REFR BLUR`).
- Two specular lights: `_PackedLightParams[4]` from `LIT1/LIT2 ANG Z/X`, `ATTN *`, `SPEC1/2 POW`.
- `_Ambiant` from `AMB R/G/B`; `_LuminanceParams[4]`, `EXPOSE`; `_ChangingColor` (theme colour, `TIME COLOR MIX`,
  `GLASS`); `_ParallaxAndGlow` (`PAR SCL/BIAS`); `_ShadowParams` (`SHADOW STREN/X/Y`).
- `_SuperSampling` (`MULTI SAMP` 0.5, `MULTI ROT` 30°): the icon is antialiased inside the shader. `SS METHOD`
  chooses full, cheap or none.

custom_render_plugin builds the ICONS menu object at FUN_00047xxx (MNU reads around line 27540 of the local
decompile) and binds the uniforms in FUN_000428b8…FUN_00042b2c (offsets: +0x00 PackedLightParams, +0x60 ChangingColor,
+0x90 Ambiant, +0xC0 ParallaxAndGlow, +0xF0 Eye, +0x120 EnvRot, +0x150 ShadowParams, +0x180 LuminanceParams,
+0x1E0 RefractParams, +0x210 SuperSampling). The function that packs the MNU values into those uniforms still has to
be traced, as was done for back_colours0. `--dump-cg` now also writes the icons programs.
