# XMB render pipeline: firmware source of each layer

Goal: output identical to the PS3 XMB, implemented in C++. Every layer has to come from the firmware's own programs,
parameters and assets. This file records where each layer comes from in the 4.93 firmware and how far the port has got.

Local inventory (`out/native-reference/qrc-inventory.txt`, not committed) lists every `vsh/resource/qgl/*.qrc`.

## Layers, back to front

| Layer | Firmware source | Native status |
|---|---|---|
| Background gradient | `lines.qrc` moyou/background, back_colours0 + month_bg textures, BACKGROUND.mnu; custom_render_plugin clock mapping | **Ported**: firmware Cg translated RSX→GLSL, inputs traced |
| Ribbon (wave sheet) | `lines.qrc` lines1 / ffd_shader1 / LinesController, LINE1.mnu; FFD wave field from the SPU spline.elf | **Ported**: shaders firmware; geometry fitted to reference (spline.elf placement not traced) |
| Particles | `lines.qrc` particles_quads / particles_second, PARTICLES*.mnu; simulation runs on SPU particles.elf | **Partial**: shaders firmware; CPU simulation reconstructs the SPU job |
| HDR tone map + glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, HDR.mnu | **Ported** |
| Category icons | `icons.qrc` lib/icons/quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo; ICONS.mnu with day/night/yoake/higure/boot overrides; widgets/other/texenv.dds (env map), amb.bmp; `icontex.qrc` 1n–70n normal maps + dif | **Not ported**: drawn by Qt from the normal maps with a fixed light. Next step |
| Item icons, labels, dialogs, info bar | explore_plugin / system_plugin via paf (RCO layouts and textures) | **Reconstructed**: RCO textures are the firmware's; positions, sizes and fades measured from the 60 fps reference |
| Text | paf font renderer with the RD-R/B (SCE-PS3 Rodin) faces from dev_flash/data/font | **Reconstructed**: Qt rasterises the firmware fonts; glyph rasterisation, hinting and glow differ |

`raf.qrc` (ubber shaders, ConeFilter), `rhm.qrc`, `canyon.qrc`/`earth.qrc` (visualiser themes) and `store.qrc` are
outside the main XMB.

## Category icon renderer (next)

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
