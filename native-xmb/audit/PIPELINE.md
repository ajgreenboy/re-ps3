# XMB render pipeline: firmware source of each layer

Goal: output identical to the PS3 XMB, implemented in C++. Every layer has to come from the firmware's own programs,
parameters and assets. This file records where each layer comes from in the 4.93 firmware and how far the port has got.

Local inventory (`out/native-reference/qrc-inventory.txt`, not committed) lists every `vsh/resource/qgl/*.qrc`.

## Layers, back to front

| Layer | Firmware source | Native status |
|---|---|---|
| Background gradient | `lines.qrc` moyou/background, back_colours0 + month_bg textures, BACKGROUND.mnu; custom_render_plugin clock mapping | **Ported**: firmware Cg translated RSX→GLSL, inputs traced |
| Ribbon (wave sheet) | `lines.qrc` lines1 / ffd_shader1 / LinesController, LINE1.mnu; cloth in custom_render_plugin; geometry from the SPU spurs/moyou/spline/spline.elf | **Ported** (`ribbon.cpp`): PPU cloth, ffd_shader1 deformation lattice, spline.elf FFD + camera + bicubic sheet, each stage checked against RPCS3 captures (see "Ribbon port"); LINE1 presets follow the plugin's clock keyframes and boot schedule (see "Ribbon presets") |
| Particles | `lines.qrc` particles_quads / particles_second, PARTICLES*.mnu; simulation runs on SPU particles.elf | **Partial**: shaders firmware; CPU simulation reconstructs the SPU job |
| HDR tone map + glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, HDR.mnu | **Ported** |
| Category icons | `icons.qrc` lib/icons/quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo; ICONS.mnu with day/night/yoake/higure/boot overrides; widgets/other/texenv.dds (env map), amb.bmp; `icontex.qrc` 1n–70n normal maps + dif | **Ported** (`icons.cpp`): firmware quad.vpo + icons_no_ss_no_shad.fpo, the variant the firmware runs at 1080p; uniforms from ICONS.mnu by the packing read from an RPCS3 capture; matches RPCS3's frame side by side |
| Item icons, labels, dialogs, info bar | explore_plugin / system_plugin via paf (RCO layouts and textures) | **Reconstructed**: RCO textures are the firmware's; positions, sizes and fades measured from the 60 fps reference |
| Text | paf font renderer with the RD-R/B (SCE-PS3 Rodin) faces from dev_flash/data/font | **Partial**: native-resolution GL glyph atlas, Qt shaping/rasterization; PAF coverage/style trace in TEXT-RENDERER.md; hinting and glow parity open |
| Cold-boot intro | custom_render_plugin boot case + anim_coldboot2 events; raf.sprx playing vsh/resource/coldboot.raf (coldboot.jsx) with raf.qrc vert0/sce01; coldboot_stereo.ac3 | **Ported** (`coldboot.cpp`): RAF scene, script timeline, sound, event times, background boot presets; matches the RPCS3 capture (see "Cold boot") |

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

**Who produces it (traced 2026-10-25).** The vertex buffer is in main memory at 0x20980000 (positions and normals
interleaved, 32 bytes per vertex; texcoords sit in RSX local memory). It is not rendered by the GPU: no draw targets
that address, and its bytes appear nowhere in RSX local memory. To find the writer, the buffer was made read-only
after the first ribbon draw (`XMB_VTX_PROTECT`). The fault came from SPU thread `SceQglLinesCellSpursKernel0` at SPU
pc 0x05824, a `wrch ch21` list-DMA site in spline.elf. (RPCS3 copies list elements on a fast path that skips
`do_dma_transfer`, which is why the DMA log showed no puts.)

Per frame the spline task gets:
- 5776 bytes from 0x20401f00: a **19x19 control lattice** of float4 (x, y, z, 1), 19 per row with x falling from
  about 5 to 0;
- 64 bytes from 0x20401dc0: a **4x4 matrix**, the camera to clip space;
- 8704 bytes from 0x20417680: 539 float4 of (0, -0.47, 0, 1) followed by the count 539;
- 256 bytes from 0x200dda00: SPURS task data.

The lattice and matrix are written by custom_render_plugin on the PPU; the task creation is FUN_00054c7c,
"moyou/spline", with 0x3d400 bytes of local store.

The code (`work/spu/disasm-elf.ts`, a disassembler generated from RPCS3's opcode table by
`work/spu/gen-disasm.ts`) is an unoptimised vector-math library plus two compute functions:
- func_45c0 loads a 4x4 patch of lattice rows (stride 304 bytes = 19 x 16), transposes it (func_44d0) and
  accumulates `fma` against per-sample weight tables, which is bicubic patch evaluation;
- func_3c68 does the per-vertex math through the vector helpers at 0x3ab0..0x3bf0.

The large FP-free functions are the SPURS/DMA runtime.

## Ribbon port (2026-10-25)

The whole chain is ported in `ribbon.cpp`. Each stage was checked against RPCS3 data: local-store snapshots taken at
the spline task's output list puts (`XMB_SPU_WATCH`, `out/native-reference/lslist1`) and the per-frame DMA log of
its inputs (5000 frames from boot). The scripts are in `work/spu/`.

**spline.elf (SPU), per frame.** The task data block (0x200dda00) carries FFD SCALE1, SCALE2 and OFFSET from
LINE1.mnu (stored to LS 0xd590/0xd5a0/0xd5b0). For every lattice point, func_3c68 computes:
- n = (p - OFFSET) / SCALE1 (func_3440: frest + fi);
- the cell from clamp(n, 0, .999) x (8, 4, 4), with cubic B-spline weights (basis matrix at LS 0xd5c0);
- the sum over the 4x4x4 neighbourhood of the 11x7x7 deformation lattice, index x + 11y + 77z;
- result (n + sum) x SCALE2. The unclamped n is kept, and only the cell lookup is clamped.

Then func_3bf0 applies the camera as a row-vector product. func_45c0 evaluates 16x16 uniform bicubic patches at
t = k x 16/127 (k = 0..7) in each direction, giving 128x128 clip positions. The normal is dP/dv x dP/du with
w = 0, using derivative weights from LS 0xde00. `check-ffd.ts` matches the transformed lattice to 4.8e-4 (the
frest/fi reciprocal), and `check-eval.ts` / `check-normal.ts` match the output tiles to about 1e-5.

**The camera matrix** decomposes exactly into these parts:
- view = R(ANG X/Y/Z axis, ANG ROT degrees) p + POS + (0, 0, -2);
- a GL perspective with fovy 53, the viewport aspect, near 0.1 and far 1000. The matrix's translation column
  divided by the projection scale gives POS X/Y exactly.

**The deformation lattice (PPU).** FUN_000493c4 builds the `_UVW` texture: 128 texels holding the 8x4x4 node grid
(x/8, y/4, z/4, 0) at texel z*32 + y*8 + x. FUN_0004a220 then:
- renders ffd_shader1 into a 128x1 float target with _Time = 10 x the cloth's wave clock;
- reads it back and scales it, field x SCALE1 + OFFSET (vmaddfp).

FUN_00047b1c pads the result to 11x7x7 by index clamp(x-1, 0, 7), clamp(y-1, 0, 3), clamp(z-1, 0, 3). The first
frame is constant (OFFSET), because the field has not been rendered yet. `check-ffd-sweep.ts` matches the native
lattice (`XMB_RIBBON_FFD_SWEEP`) to every captured one within 5e-5..1.9e-4. The fitted _Time advances 1.0 per
500 frames, as TIMESTEP 2 predicts.

**The cloth (PPU custom_render_plugin).** It is a 19x19 mass-spring sheet in four buffers (current, previous,
velocity, drawn), 60 fixed steps per second (FUN_0004f814). LINE1.mnu DAMPING, LENGTH, TENSION, TIMESTEP,
PERTURBATION and END Y/Z drive it. Each step (FUN_0004bed0) runs:
- current is copied to previous, and the wave clock advances by TIMESTEP x 1e-4 (wrapping to 0 past 10);
- each point gets spring impulses (k = TENSION, rest LENGTH) to its left and upper neighbours, and bending impulses
  (10 TENSION, rest 2 LENGTH) to the second neighbours. These go straight into the velocities, as does a random
  kick of PERTURBATION x three draws of the hash noise FUN_0004ab04;
- explicit Euler with dt = TIMESTEP x 1e-4, then vel -= vel x DAMPING x TIMESTEP;
- FUN_0004bce0 eases the phase toward the clock by 0.1. Each row r's first point is pushed +x by 0.02 TIMESTEP, and
  its last point is pinned with velocity zeroed at (0, (sin(11t)/2 + 1/2) END Y, (cos(15t) + 1)/2 END Z),
  t = r/19 + phase.

The drawn lattice blends previous and current by the leftover step fraction. Its y and z are scaled by
1.3 - cos(d pi/2)(1 - smoothstep(d/5)), d = x - FFD PARAM 1. The import 0x87b50 is sin for mode 0 and cos for
mode 1, fitted from the boot frame.

The boot ribbon starts from preset 1 (FUN_0004e624: positions at 0x97c18, velocities at 0x98d04). That matches the
first captured lattice to 1.6e-7, with the scale curve fitted at FFD PARAM 1 = -2 (coldboot1). `cloth-sim.ts`
follows the boot frames to about 1e-4 in x per frame. The remaining gap is the boot preset blend (TIMESTEP fits
3.69, between LINE1.mnu's 4 and coldboot1's 2), which is not ported.

The native app reads the rest tables from a local decrypted plugin (`--vsh-modules`, never committed). Without
one, it settles the plugin's generic flat start (FUN_0004ec90) for a minute of simulated time. The vertex texcoords
(in_8, from local memory 0xc2bb4100) are (row/127, edge fade), the fade being FUN_0004d190's.

## Ribbon presets and boot transition (2026-10-25)

The plugin registers every override set (FUN_000388b8) and blends to one with FUN_00039728(duration, layer,
"override/<name>"). That call snapshots the current values and sets the target; the blending itself runs in the
imported `qgl_base` animation player (qglbase.prx; the import stubs are resolved by `work/spu/prx-imports.ts`). The
preset clock (+0xa20) advances by the same frame delta as the cloth (FUN_0003a06c, dt from import FUN_000865e0).

**Clock keyframes (FUN_00011600, table at 0x9c98c).** Fraction of the day, preset:
0 night, 1/24 night, 5/24 yoake, 7/24 yoake, 11/24 day, 13/24 day, 17/24 higure, 19/24 higure, 23/24 night,
1 night. The blend between neighbours is linear, so the presets hold for two hours around 06:00, 12:00 and 18:00 and
cross-fade over four hours in between.

**Boot (plugin 0x11100, outside Ghidra's function list):**
- coldboot1 with duration 0 (DAT_00091e24);
- coldboot2 over 3 s (DAT_00091e1c);
- the clock preset via FUN_00010900 with 7.5 s (DAT_00091e20).

The curves were measured from the RPCS3 cold-boot capture (`lslist1`). The task data block carries FFD SCALE2 every
frame, and the cloth's wave clock (fitted from the FFD lattice) shows exactly one cloth step per task run, so the
samples are 1/60 s apart in the plugin's time:
- frames 0 to 229 (3.82 s): value += (coldboot2 - value) x 0.01 per frame, exactly (1 - p = 0.99^n);
- frames 229 to 397 (2.8 s): from the values reached to the clock preset, p = 1.4e-5 n^2 up to n = 112, then
  steeper and flat at p = 1 by n = 168.

The capture settles exactly on yoake's SCALE2 (3.2, 1.27473, 2.88939). It was taken at 05:42, inside the yoake
hold, which confirms the keyframe table. `work/spu/check-presets.ts` replays ribbon.cpp's schedule against every
frame: stage 1 is within 1e-4, and the worst error is 2.2e-3 at the stage-2 knee (0.6% of the range). The measured
durations (3.82 s, 2.8 s) differ from the call arguments (3 s, 7.5 s). How qgl_base's player maps one to the other
is not traced.

gameboot1-5 run when a game launches (pages page_gameboot / anim_gameboot, 0xe950..0xf71c), and welcome_1/2 and
music_1 run during first-time setup and music playback. The native app has none of those flows, so they are not
scheduled.

## Cold boot (ported 2026-10-25)

The 4.93 intro is a RAF scene: the PS symbol, "PS3" and a "PlayStation 3" footer, lit by a moving point light. The
RCO's tex_ps3logo / tex_scelogo ("PLAYSTATION 3", "Sony Interactive Entertainment") are not what shows: anim_coldboot2
holds scelogo at alpha 0. `src/coldboot.cpp` plays the scene natively, `View` gates the UI and drives the background's
boot presets from its clock, and any key or pad button skips it. `--no-intro` (and `--script`, `--background-clock`,
`--measure-reference`) start at the XMB.

**Plugin side.** custom_render_plugin's boot case (0x15a00, the routine at 0x11100's caller) opens page_coldboot,
runs anim_coldboot2 on scelogo, loads raf.sprx and `vsh/resource/coldboot.raf` (FUN_000180c8), starts it on
page_coldboot2's coldboot_ps3logo plane (FUN_00017fc8) and plays coldboot_stereo.ac3 (coldboot_multi.ac3 for a
multichannel output), all in one call. The AC3 is 313 frames at 640 kbps (frmsizecod 36), 10.02 s.

anim_coldboot2's FireEvent operands are offsets into the RCO event table (0xf84) whose `native:/` names the plugin
resolves through the table at 0x9cbc0 (name, function descriptor):

| Time | Event | Handler | Effect |
|---|---|---|---|
| 0 s | anim_coldboot_BootBG2 | 0x110dc | hides the GUI; LINE/BACKGROUND/HDR layers to override/coldboot1 (duration 0), then coldboot2 over 3 s (0x91e1c), then FUN_00010900 with 7.5 s (0x91e20) |
| 4 s | anim_coldboot_NormalBG2 | 0x11b20 | clears the boot overrides on four layers and blends to the clock keyframes (FUN_00011600) over 7.5 s (0x91e34) |
| 5.5 s | anim_coldboot_ShowGUI | 0x4110 | shows the GUI (in RPCS3 the photosensitive-epilepsy notice comes first, then the XMB) |
| 10 s | anim_coldboot_Finished | 0x3c10 | closes page_coldboot |

ICONS.mnu coldboot1-5 are only registered (FUN_000388b8); nothing schedules them in this flow.

**coldboot.raf.** `_RAF`, u32 BE size, zlib. Inflated `RAFO`: u32 version, six (offset, size) sections: element
tree, object names (u32 element offset + NUL string), keys, (empty), floats, file data. Elements are RCO-like
(name key, attribute count, parent, previous, next, first/last child) with 16-byte attributes (key, type, a, b): 1 int,
2 float, 3 key string, 5 float vector (index, count), 6 data (offset, size), 7 object id, 8 reference.
`work/coldboot-raf-dump.ts` prints the tree and extracts the files:
- six actors on one plane (logo, footer, blur, and SD copies), position (1.85, 0, 0), rotation (pi/2, 0, 0), scale
  (1.28, .64, .64), uv scale (1, -1), effect sce01;
- camera at (0, 0, 4) looking down -z, yfov 0.927 rad; a point light (attenuation (0, 1, 4)) and an ambient light (black);
- plane.edge (Edge geometry: 16 vertices, a 4x4 grid over [-1, 1]^2, uv = ((x+1)/2, (1-z)/2); the compressed index
  stream is not decoded, the grid is triangulated by uv), plane.skel (ES01, one joint);
- six GTF textures, B8 linear (format 0xa1), 700x350 HD / 240x120 SD, ten mips at a constant pitch;
- coldboot.jsx, VSMX bytecode (opcodes as RCOMage's vsmx.c numbers them; `work/coldboot-vsmx-decompile.ts`).

The script: logo alpha 0 -> 1 over first_wait (1.5 s, linear); at 1.5 s the light moves from logo + (-3, .5, 0) to
logo + (0, .5, 0), attenuation to (.1, .1, .1), colour to white over appear_duration (1.3 s); at 2.55 s blur alpha
to 1 over 0.5 s, Bezier (.1, 0, .1, 1); at 3.3 s blur back to 0 (same curve) and footer to 1 (linear) over 0.5 s;
at 4.3 s footer and logo to 0, light + (2.25, 1.5, 0), black, attenuation (0, 1, 4) over 1.2 s, Bezier (.8, 0, .8, 1).
The scene ends at 5.5 s, the ShowGUI time. `System.resolution == 720` (the SD output width) selects the SD actors;
the native port always uses HD. The timing constants are read from the VSMX at load.

**Shaders and uniforms.** raf.qrc vert0.vpo + sce01.fpo, translated like the other programs. sce01: rgb = vertex
colour, alpha = colour.a x tex x _Material[3].x x clamp(lit / 3), lit = (tex x diffuse x (warp(halfLambert^2) x light
colour x kd + ambient) + specular) / (a0 + a1 d + a2 d^2). An RPCS3 capture of all 1902 sce01 draws of a cold boot
(`XMB_FP_SERIES`, below) gives the packing: _SpotLight[i] = (light position i, attenuation i, -, specular 1),
_LightingMisc[i] = (ambient i, -, light colour i, 1), _Material[0..2] = (1, 1, 0, .12), (1, .7, 6, 3), (1, 40, 0, 0),
_Material[3].x = the actor's alpha; vc[256..259] MVP rows, vc[260..263] model rows, vc[466] uv (offset, scale), vc[467]
eye. _DiffuseWarp is a 64x16 ramp (round(255 i / 63)) and _SpecularMap 32x32 white, both generated by raf.sprx.

**Timing check.** `work/coldboot-series-timing.ts` inverts each captured track to its start time: light 1.478 s, blur
2.562 s, blur-out and footer 3.311 s, fade 4.312 s, last logo draw 5.444 s, against the script's 1.5 / 2.55 / 3.3 /
4.3 / 5.5 s. The ~20-40 ms spread is RPCS3's boot stalls (raf.sprx caps its time step, so the script clock falls behind
host time while frames stall). The fade-out curve fits cubic-bezier(.8, 0, .8, 1) to 0.003 RMS. Natively, frames saved
with `XMB_INTRO_GRAB` at the same script times match the RPCS3 frames (`work/coldboot-grab-burst.ps1`) in layout and
sequence (`work/coldboot-region-means.ps1`).

**Background layers during the boot** (captures of back_colours0 and particles_quads through a cold boot):
- back_colours0 `_Alpha` = smoothstep(t / 7.5 s) from BootBG2 (0.00025 RMS). The program renders the 64x32
  background texture and its alpha is applied there, so the native port premultiplies the output by it;
- `_NightBrightness` goes from coldboot1 (.513834) to coldboot2 on the same 0.99-per-frame filter as LINE1, and the
  native port blends it with `Ribbon::bootWeights`; HDR.mnu coldboot1 (EXPOSURE 1.64, GLARE 1.74, THRESH 0.2) uses the
  same weights (not captured);
- PARTICLES `_Transparency.y` (GLOBAL ALPHA) stays 0 until NormalBG2 and then follows the LINE1 stage-two curve to 1:
  particles skip coldboot2;
- LINE1 stage two now starts at NormalBG2 (4 s) when the intro drives the ribbon; RPCS3's 229 cloth steps at that point
  are boot stalls.

Open: RPCS3 composites the frame with LinesController and its pre-exposure LUTs (no ToneMap pass runs during the boot),
so its background stays near black until ~2.2 s and ends darker (display level 67 vs 83 native in the same region).
The native ToneMap path is a separate fidelity gap. Native draws the logo directly onto the frame; the firmware draws it
on the coldboot_ps3logo plane. The epilepsy notice after ShowGUI is not ported.

**Capture tooling.** `rpcs3-rsx-capture.patch` adds `XMB_FP_SERIES=<file>` / `XMB_SERIES_FP_LENGTH=<ucode bytes>`
(0 lists lengths): one record per draw of that program with the patched ucode, vertex constants and texture units, and
the bound textures once (`<file>.texN`). `work/coldboot-series-split.ts` splits a capture for
`re-ps3-native --decode-fp`. `--dump-cg` prints each program's ucode size (sce01 1344, back_colours0 8608,
LinesController 368, particles_quads 1968). `build-rpcs3-cap.bat` builds the patched RPCS3 into `C:\dev\rpcs3-cap`
next to the main build; copy dev_flash, dev_hdd0 and config there and run it with a scratch `--config`.

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
