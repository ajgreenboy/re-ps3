# XMB render pipeline: firmware source of each layer

Goal: output identical to the PS3 XMB, implemented in C++. Every layer has to come from the firmware's own programs,
parameters and assets. This file records where each layer comes from in the 4.93 firmware and how far the port has got.

Local inventory (`out/native-reference/qrc-inventory.txt`, not committed) lists every `vsh/resource/qgl/*.qrc`.

## Layers, back to front

Current category-material routing (updated 2026-10-26): every built-in colour, including Original, uses the
opaque native normal-map material (`src/icons.cpp:97-109`, selected at `:164-166`); the firmware glass
program is diagnostic only (`XMB_ICON_FIRMWARE` / `XMB_ICON_UCODE` / `XMB_ICON_DEBUG`). Original's tint
still follows the original monthly RGB textures at a fitted 0.45 strength (`src/view.cpp:294-308`). The
earlier routing (Original glass, fixed colours opaque) was current before 2026-10-26; see
CATEGORY-ICON-FIX.md for the evidence and fidelity limits.

| Layer | Firmware source | Native status |
|---|---|---|
| Background gradient | `lines.qrc` moyou/background, back_colours0 + month_bg textures, BACKGROUND.mnu; custom_render_plugin clock mapping | **Ported**: firmware Cg translated RSX???GLSL, inputs traced |
| Ribbon (wave sheet) | `lines.qrc` lines1 / ffd_shader1 / LinesController, LINE1.mnu; cloth in custom_render_plugin; geometry from the SPU spurs/moyou/spline/spline.elf | **Ported** (`ribbon.cpp`): PPU cloth, ffd_shader1 deformation lattice, spline.elf FFD + camera + bicubic sheet, each stage checked against RPCS3 captures (see "Ribbon port"); LINE1 presets follow the plugin's clock keyframes and boot schedule (see "Ribbon presets") |
| Particles | `lines.qrc` particles_quads / particles_second, PARTICLES*.mnu; simulation runs on SPU particles.elf | **Ported (2026-10-26)**: firmware-shaped pool in `src/particles.cpp` (1536 cap, 48-byte stride, fixed 0.0088883 s step, PARTICLES.mnu physics, Park-Miller seed), one indexed draw into the HDR scene before the tone map and a second draw into the glare source (`src/view.cpp:370-376`). Superseded reading (accurate before 2026-10-26): this layer was linkev/PlayStation-3-XMB's additive point sprites, not a firmware port. Still open: the real `particles_second` program (the glare draw reuses the same geometry with a fitted 0.045 halo), `proc_iridescent.tga`, and the `_Color`/`_Gamma`/spot packing (`audit/PARTICLES.md`) |
| HDR tone map + glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, HDR.mnu | **Ported** |
| Category icons | `icons.qrc` lib/icons/quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo; ICONS.mnu with day/night/yoake/higure/boot overrides; widgets/other/texenv.dds (env map), amb.bmp; `icontex.qrc` 1n???70n normal maps + dif | **Geometry ported; material is a stand-in** (`icons.cpp`): firmware quad.vpo + icons_no_ss_no_shad.fpo (the variant the firmware runs at 1080p) and ICONS.mnu uniforms from an RPCS3 capture. The visible surface is the opaque native normal-map material for every built-in colour (`src/icons.cpp:110-130`); the firmware glass program runs only behind the diagnostics. Side-by-side frame parity is not claimed. |
| Item icons, labels, dialogs, info bar | explore_plugin / system_plugin via paf (RCO layouts and textures) | **Reconstructed** (item icons ported, labels moved to the GL atlas): `icontex` item icons and settings bullets draw through `IconRenderer` (firmware vertex program plus the opaque normal-map material, `src/icons.cpp:110-130,164-166`), with captured sizes 77.616/184.800 (items) and 35.721/85.050 (bullets), row pitch 80, brightness 1.0 in the open column and 0.247/0.698 behind an open page, all confirmed in `ITEM-ICONS.md`. Labels, dialogs, the info bar, the OSK, the options panel, the notification pop-up, subtitle cues and the volume HUD draw through the GL glyph atlas (`src/text_renderer.cpp`, called from `View::paintGL`); RCO, artwork and theme icons still use QPainter. Three corner/overlay composites were added on that path on 2026-10-26/27 - the notification plaque (`View::drawNineSlice` / `View::drawNotification`, audit/NOTIFICATIONS.md), the volume HUD (`View::drawVolumeHud`, audit/HTPC-IDEAS.md section 6) and the subtitle band (`View::drawSubtitle`, audit/SUBTITLES.md); none of them is a firmware layer, so they have no row here. |
| Text | paf font renderer with the RD-R/B (SCE-PS3 Rodin) faces from dev_flash/data/font | **Partial**: native-resolution GL glyph atlas with FreeType coverage (firmware load flags 10) and Qt shaping only; PAF coverage/style trace in TEXT-RENDERER.md; PAF metric authority, per-style weight/slant and glow parity open. Per-phase coverage landed 2026-10-26 as a fit (TEXT-RASTER.md 2.4) |
| Cold-boot intro | custom_render_plugin boot case + anim_coldboot2 events; raf.sprx playing vsh/resource/coldboot.raf (coldboot.jsx) with raf.qrc vert0/sce01; coldboot_stereo.ac3 | **Ported** (`coldboot.cpp`): RAF scene, script timeline, sound, event times, background boot presets; matches the RPCS3 capture (see "Cold boot") |

`raf.qrc` (ubber shaders, ConeFilter), `rhm.qrc`, `canyon.qrc`/`earth.qrc` (visualiser themes) and `store.qrc` are
outside the main XMB.

## Ribbon geometry (captured 2026-10-25)

The ribbon geometry was captured from RPCS3 with `XMB_VTX_DUMP` (patch: `rpcs3-rsx-capture.patch` at the repo root;
the draw is identified by lines1.fpo's 336-byte ucode). Each frame is a single indexed draw:

- 35,306 big-endian u32 indices form triangle strips with primitive restart (1016 strips of 32???34 indices), over
  16,384 vertices: a 128x128 grid where vertex = row*128 + column, and strips pair row r with row r+1.
- Array 0 holds float4 positions (stride 32) that are already in clip space: lines1.vpo passes `in_0` straight to
  `gl_Position`, with w ??? 8. Array 9 holds float4 normals (stride 32), array 8 float2 coordinates (stride 8).
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
music_1 run during first-time setup and music playback. Updated 2026-10-26: the native app now launches games
(`Shell::startTracked`) but still does not schedule gameboot1-5; it has no
first-time-setup or background-music flow, so welcome_1/2 and music_1 are not scheduled either.

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
  particles skip coldboot2 (the native sparkle layer's opacity follows it);
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

## Menu motion (traced 2026-10-25)

The cross bar is the paf widget class `PhXmBar`, inside vsh.elf's paf (around 0x3ab000???0x3b1000), not
explore_plugin. Its focus moves (`FUN_003ad3bc` next, `FUN_003ad728` previous, on input event 0x11 keys 4/3) step
the focus index (+0x2a8) and start widget animations of **200 ms** (DAT_0068f054). The XmBar animate calls pass
acceleration mode **5**, and so does the exported wrapper paf_E8149213 used by explore_plugin. explore_plugin's own
200 ms calls (65 of them) carry slot ids 0x10000NN; the mode is a separate argument.

paf's animation setup (`FUN_003981ec`) converts the duration to n = int(ms ?? 0.001 ?? 60) frames and the rate to
1/(n/60 ?? 0.999999). Its evaluator (`FUN_00398748`, mode in byte +0x23) is:

| Mode | Curve |
|---|---|
| 1 | ???t (ease-out) |
| 2 | t?? (ease-in) |
| 3 | (sin(??(t ??? 0.5)) + 1)/2 |
| 4 | per frame v = ((1 ??? v)k + v) ?? 1.0001, k from a 14-entry table (0.995 ??? 0.281), then 1/(3.775 + 0.217(n ??? 15)) |
| 5 | per frame v = ((1 ??? v)k + v) ?? 1.000001, k = 0.9615, 0.78125, 0.637, 0.529 for n = 1???4, then 1/(2.2 + (n ??? 5)/3) |
| other | t clamped to [0, 1] |

Modes 4 and 5 step once per elapsed frame (paf's frames-per-tick global DAT_006fb1f4); a fraction above 0.05 blends
one step into the state. When the snapped duration ends, the value is set to the target.

For 200 ms, n = 12 and k = 0.2206: each frame leaves 77.9% of the remaining distance (0.779, 0.607, 0.474, 0.369,
0.288, 0.224, 0.175, 0.136, 0.106, 0.083, 0.065), then the last 6.5% snaps on frame 12. This replaces the measured
"25% per frame" exponential. The video's per-frame item steps had ratios 0.77, 0.73, 0.73, 0.73, 0.69, 0.64, which
fit mode 5 at 200 ms better than mode 4 (0.68) or a constant 0.75.

`View::PafEase` implements mode 5 exactly and drives the category, item, page/depth and column cross-fade moves.
A new target restarts from the current value, as the reference shows. `XMB_MOTION_LOG=1` logs the focused
column's position per frame; a native DOWN press follows the curve and ends at 200 ms.

### Measured on the real XMB (RPCS3, 2026-10-25)

Recorded from the RPCS3 window with CopyFromScreen at about 118 captures a second (PrintWindow returns stale Vulkan
frames), with a keyboard pad and the key driven by `keybd_event`. Analysis scripts: `work/motion-track.ts` (scroll
of the bright UI content per frame) and `work/region-luma.ts` (mean luma of a region per frame).

- **Held-key repeat.** DOWN held for 3 s in the Settings column: the first move starts 34 ms after the key goes down
  (input latency), the second 300 ms (18 frames) after the first, then one move every 66.7 ms (4 frames): 1344, 1411,
  1479 ??? 2144 ms. There is no acceleration over 14 repeats. Each repeat restarts the ease from the current position,
  so the column trails the selection while the key is held. `View::pollController` repeats on this timing, counting
  from the previous deadline so that its 16 ms poll does not stretch the interval. Arrow keys go through the same path
  (OS auto-repeats are dropped). Native check: the first repeat at +302 ms, then a mean of 67.8 ms, landing on the
  same item after 15 moves.
- **Help text.** There is no delay. The description belongs to the selected row: it is drawn fully opaque from the
  frame the move starts, including on every repeat, even while the selected row is still sliding toward the focus
  slot (16 ms after a repeat, Chat Settings is below the slot with its description and Music Settings is still in
  it). The native view used to wait for the eased focus to pass 0.95; it now draws the description on
  `column.selection`.
- **Category move.** A single RIGHT tap: motion starts 42 ms after the key, and the remaining distance falls by
  0.765 per frame for the first 8 frames. That fits mode 5 with n = 11???12 (0.762 at 183 ms, 0.779 at 200 ms), so the
  200 ms assumption stands. A small tail of motion afterwards comes from the icons' scale change, which the tracker
  mixes in.
- **Page move** (into a column's page and back; Settings ??? Game Settings, then back): the toolbox icon travels
  261 px. Entering starts about 530 ms after the press, while the plugin loads the page; leaving starts after 34 ms.
  Both directions follow the same curve. The remaining distance falls by 0.760 per frame, steadily, for 17 to 18
  frames, and there is no snap at frame 12. So this is not the 200 ms mode-5 ease (0.779, with a 5% snap on frame 12).
  This capture method reads about 0.014 below the model on moves whose timing is known (the category move: 0.765
  against 0.779). The page move therefore fits mode 4 at 300 ms: n = 18, k = 1/(3.775 + 0.217 ?? 3) = 0.226, so
  0.774 per frame, and the 1% left snaps on frame 18. Updated 2026-10-26: explore_plugin's five 300.0f constants are
  file-delete dialog, `user.info` dialog and list-label durations, not the page move (`audit/PAGE-MOVE.md:62-81`), and
  the only captured 300 ms bar call passes mode 5 (`:99-101`). The page move lives in vsh.elf paf PhXmBar, so 300 ms at
  mode 4 remains a fit, not a trace.
  `View::PafEase` gained mode 4, and the depth ease uses it at 300 ms. Native check: `XMB_MOTION_LOG` shows the
  depth position settling in 18 frames in each direction.
- Capture notes: RPCS3's exclusive fullscreen leaves CopyFromScreen showing a frozen frame, so the scratch config
  sets "Exclusive Fullscreen Mode: Disable". Letter keys did not reach RPCS3's keyboard pad when sent with
  `keybd_event` (arrows did), so Cross and Circle were mapped to Page Down and Page Up for the test.

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
- `_Ambiant.w` is 0.698 for unfocused icons and 1 for focused ones. The program's alpha (envmap alpha ?? `_Ambiant.w`)
  blends the icon over the frame.
- Light directions are (sin Z cos X, sin Z sin X, cos Z) of the LITn angles. `_EnvRot` turns about 30??/s.
- tex3 is a 128??128 px CookieCutter cut of the frame around each icon, refracted per channel.

Translator fixes found by diffing against RPCS3's own GLSL for this program (`Log shader programs`):
- LIF (0x3C) is (1, y, y>0 ? 2^w : 0, 1), not full LIT. This was why the icons came out saturated white.
- DIVSQ uses sqrt(|b|) and keeps a when a is 0; NRM leaves zero-length vectors alone.
- Source and destination precision clamps (fixed12 ??2, fixed9 ??1, saturate), with NaN read as 0.
- H registers are rounded to fp16. H and R are not aliased (RPCS3 keeps them apart).

Clock inputs updated: `_ChangingColor` now samples amb.bmp and `_LuminanceParams` derives from GLASS; see
`ICON-MATERIAL-CLOCK.md`. Daytime visual parity still needs a matched capture. Updated 2026-10-26: item
icons in the open column no longer go through Qt. **Superseded by ITEM-ICONS.md**: icontex item icons and settings
bullets queue to `IconRenderer` (the item-icon queue in `View::paintGL`) and only RCO, artwork and theme icons stay on the QPainter path. The background is brighter
than RPCS3's at the same hour, which also
lightens the refracted unfocused icons. Diagnosis: `XMB_ICON_UCODE=<capture>` runs on the firmware's captured
constants; `XMB_ICON_DEBUG=0..4` shows a single input texture or the result.

### Original notes

The firmware draws category icons as lit glass. Each icon is a quad (quad.vpo, `_ModelviewProjection`,
`_CookieCutterCenter/Ratio`). The fragment programs read these:

- `_NormalMap` (tex0): icontex `Nn`. Its alpha channel is the coverage, and a second fetch at a shadow offset gives the
  shadow.
- `_DiffuseMap` (tex1) and `_EnvMap` (tex2): `texenv.dds`, rotated by `_EnvRot`.
- `_BackgroundMap` (tex3): the frame behind the icon, refracted per channel (`REFR R/G/B` ???0.032/???0.038/???0.048,
  `REFR SCL`, `REFR BLUR`).
- Two specular lights: `_PackedLightParams[4]` from `LIT1/LIT2 ANG Z/X`, `ATTN *`, `SPEC1/2 POW`.
- `_Ambiant` from `AMB R/G/B`; `_LuminanceParams[4]`, `EXPOSE`; `_ChangingColor` (theme colour, `TIME COLOR MIX`,
  `GLASS`); `_ParallaxAndGlow` (`PAR SCL/BIAS`); `_ShadowParams` (`SHADOW STREN/X/Y`).
- `_SuperSampling` (`MULTI SAMP` 0.5, `MULTI ROT` 30??): the icon is antialiased inside the shader. `SS METHOD`
  chooses full, cheap or none.

custom_render_plugin builds the ICONS menu object at FUN_00047xxx (MNU reads around line 27540 of the local
decompile) and binds the uniforms in FUN_000428b8???FUN_00042b2c (offsets: +0x00 PackedLightParams, +0x60 ChangingColor,
+0x90 Ambiant, +0xC0 ParallaxAndGlow, +0xF0 Eye, +0x120 EnvRot, +0x150 ShadowParams, +0x180 LuminanceParams,
+0x1E0 RefractParams, +0x210 SuperSampling). The function that packs the MNU values into those uniforms still has to
be traced, as was done for back_colours0. `--dump-cg` now also writes the icons programs.

## Runtime reads in the background layers (P1) - 2026-10-27

The ribbon, HDR and icon layers no longer carry private MNU parsers or duplicated preset numbers. All three (and
particles) read through one process-wide reader pair, `RcoObjects` + `Materials`, built on first use:

```cpp
RcoObjects& sceneObjects(Assets&);     // declared in src/particles.h, defined in src/particles.cpp
Materials&  sceneMaterials(Assets&);   // one cache, one missing-key rule
```

`RcoObjects` performs no I/O until a container is asked for; the scene path asks for none (its values live in the
QGL `.mnu` files, not the RCO object trees), so `--rco-tree` remains the only RCO consumer. The measured startup
line, emitted once by the last scene layer that reads materials (`Particles::init`):

```
scene reader: particles : 0 RCO containers, 0 B, 0.00 ms; 20 MNU files, 0.56-0.99 ms
(lazy: no scene layer reads an RCO object tree; the scene path's values live in the QGL MNU files)
```

20 files = PARTICLES base + 4 theme overrides, LINE1 day/night/yoake/higure/coldboot1/coldboot2, BACKGROUND base,
HDR base/day/night/coldboot1, ICONS day/night/yoake/higure. A missing archive logs each failed file and every
layer keeps its compiled fallback, so the app still starts with a `dev_flash` that has no `vsh/resource/qgl`.

### Ribbon (`src/ribbon.{h,cpp}`)

- **BACKGROUND.mnu keys 3/4** (`3 RED/GREEN/BLUE`, `4 RED/GREEN/BLUE`) are read once in `init()` into
  `colour3_`/`colour4_` and uploaded to `LinesController.fpo`'s `colour3`/`colour4` uniforms. The base file is
  the running value: keys 3/4 are identical in every preset the plugin schedules for this layer (day, night,
  yoake, higure, coldboot1, coldboot2); black/music_1/welcome change them and this build never schedules those as
  a background.
- **LINE1.mnu** presets are read with `Materials::line1("day"/"night"/"yoake"/"higure"/"coldboot1"/"coldboot2")`;
  the private `readMnu`/`readPreset` pair is deleted. A missing override now inherits the day preset instead of
  the second hardcoded `nightDefaults` table that used to sit here (its spacing/brightness/mipmap-bias/fresnel/
  perturbation/angle/scale2/ffd-param values were LINE1 override/night's own numbers). The `Preset` initializers
  remain as the day-file fallback and are labelled as such.
- Values are identical, and the canonical capture shows it: the clock-text and info-bar layers are bit-identical
  before and after, and the only changed pixels are dust (see `PARTICLES.md` 5). The MNU text is parsed by the
  same `Materials` parser the icons and particles use, so a key present in one layer is spelled the same in all.

Constants that stay in `ribbon.cpp`:

- `thinness = 0` - `// fit:`; the presets say `THINNESS 1`, which the plugin remaps (not traced); 0 reproduces
  the stripe period of the captures.
- `cameraFovy 53, near .1, far 1000` - `// captured:` from the spline task's matrix (0x20401dc0, decomposed).
  BACKGROUND.mnu's `FOVY 71.846` is the background shader's camera, not this one.
- `stripeWidth 512` - `// fit:`; the plugin's width is a runtime global and 512 resolves the profile.
- Captured, not MNU: the stripe profile (FUN_0004bad0: 1.01, 4x^2, 20(k/3)^3, k/6+0.5), the 128x1 UVW grid and
  the 4096-entry `_Encode` ramp, the 128x128 spline sheet and its 16-column strip layout, the cloth step
  (TIMESTEP x 1e-4, DAMPING x TIMESTEP, 0.02 pin push, sin(11t)/cos(15t) driver, 10x tension bending) and the
  boot schedule (0.99-per-frame filter, 1.4e-5 n^2 to n=112 then a Hermite to n=168, 229/60 stage two).

### HDR (`src/hdr.{h,cpp}`)

- `HDR.mnu` is read in `init()`: base file for `TEX SIZE`/`TEX MAX MIP` (8/8), `override/day`, `override/night`
  and `override/coldboot1` for `EXPOSURE`, `WHITE LEVEL`, `GLARE LEVEL`, `GLARE THRESH`, `GAUSSIAN RAD R/G/B`
  and `GLARE SUM POW`. `finish()` blends the three by the boot weights as before.
- Values are identical: the preset table in the old `hdr.cpp` was exactly override/night, override/day and
  override/coldboot1 (checked value by value against the files). The compile-time `Preset` values remain as the
  fallback for a tree without lines.qrc and are labelled `// fallback:`.
- Captured, not MNU: the 15-tap Gaussian (`k = -7..7`, first 8 weights), the per-level `SUM POW^level`
  normalisation, the empty/gaussian VAOs and the RGBA16F targets.

### `view.h` / `view.cpp` hunks this task did not edit (other agents' fence)

Three BACKGROUND.mnu values are still typed in View. None is a contradiction today (`RCO-OBJECT-MODEL.md` 9.5);
each is a one-line read once View can ask the reader - `Ribbon` already calls `sceneMaterials(assets_)` in `init()`,
so a two-line accessor on `Ribbon` (`double backgroundNumber(preset,key)`) is all it takes, e.g.
`ribbon_.backgroundNumber("day", "NIGHT BLEND")`:

| site | value | firmware read |
|---|---:|---|
| `view.h:194` `static constexpr double kNightBlend=.499947;` | 0.499947 | `BACKGROUND.mnu` `NIGHT BLEND` (day/night/yoake/higure/coldboot2) |
| `view.cpp:493` `b.nightBrightness=.486059f;` | 0.486059 | `override/day/BACKGROUND.mnu` `NIGHT WHIT BIAS` |
| `view.cpp:411` `constexpr float coldboot1NightBias=.513834f;` | 0.513834 | `override/coldboot1/BACKGROUND.mnu` `NIGHT WHIT BIAS` |

The two smoothstep windows (`0/5.16611/24`, `18.498/20.3312/24`) in `backgroundTime()` are captures from the
reference day-cycle video, not MNU keys, and stay.

The fourth coupling is the particle clock: `View::renderBackground` computes `when` (the `--background-clock`
override or the live clock) and passes only `when.time()` to `ribbon_.render(...)`. `Particles::render` takes no
time of day, so `particles.cpp` re-reads the `--background-clock` argument itself. The hunk that removes the
duplication is `particles_.setBackgroundClock(when)` next to `ribbon_.render(...)` in `view.cpp:416`.

### Determinism and gate

Every `--render-clock 30` capture of the final sources (mandated build, an annotation-only rebuild, a second
out-of-repo build of the same sources, and runs with `XMB_PARTICLE_LOG=1`) hash the same:

```
sha256 F68732A7F9A135D55D097710EE4B45B2779594BD1AD886E3FF2BB084CC97BF18   (7 of 7 identical)
```

Canonical gate: PASS 6/6 (values in `PARTICLES.md` 5). All 122 RCO containers still parse with the shared
reader's unchanged code: `--rco-tree "*@file"` -> `122 containers, 8070 objects, 165 animations, 20039542 bytes
read, read+parse 47.69 ms` (exit 0, 8,358 dump lines), the same counts as the P1 audit.

### Fallback run (no firmware QGL files) and named-state movement

With a copy of `dev_flash` that has no `vsh/resource/qgl`, the app still starts and renders:
`--script full,wait,wait,wait,grab=...,quit` exits 0, writes the frame, and logs one line per missing file
(`mnu: lines PARTICLES.mnu Cannot open local QRC`, `particles: PARTICLES.mnu unavailable ... using the compiled
fallbacks`, `icons: firmware programs/palette unavailable`), with the startup-cost line reporting
`5 MNU files, 0.40 ms` (the attempted reads). The literal `--dev-flash` absent path is unchanged and is main.cpp's
usage error (`exit 2`), outside this task's fence.

Named states moved with the dust, as expected:

| state | stored build (`native-phase3`) | current worktree before this change | this change |
|---|---:|---:|---:|
| steam-game-root | inf / 1.0000 PASS | 48.82 / 0.9996 FAIL | 42.23 / 0.9927 FAIL (item column 41.08 / 0.9897) |
| xmb-notification | - | 48.82 / 0.9996 FAIL | 42.32 / 0.9929 FAIL |
| xmb-volume-hud | - | 48.82 / 0.9996 FAIL | 42.31 / 0.9929 FAIL |

The 55 dB self-baseline is hypersensitive by design (a native self-reference, not firmware fidelity), and the
worktree was already 6.2 dB below it before this change because of other agents' view/shell edits; the particle
change adds the dust deltas measured in `PARTICLES.md` 5 (6,000 pixels, 0.29% of the frame, all specks). The
harness was not edited. `--states` cannot complete in this environment at all: discovery fails on
`steam-not-installed: could not resolve path stage 1: no folder dump`, and the same failure reproduces with the
stored `native-phase3` exe, so it is a fixture gap, not a regression.

## Exposure and ribbon phase: the residual measured, 2026-10-28

The layout pass closed with a fitted per-channel gain of 0.785 (root), 0.762 (`page-settings-system`) and 0.772
(`theme-list`) lifting the whole frame from 17.86 to 20.12 dB, 18.50 to 21.51 and 18.92 to 22.18, and called the
remainder an "exposure and ribbon-phase" difference (`GEOMETRY-ROOT.md` 12.6). This pass localised it on the matched
pair. The answer changes the framing: the residual is **not a gain at all**. It is (a) a uniform **additive
pedestal** in the background layer's own output, equal in all three channels, and (b) a **ribbon whose drawn sheet
sits about 140 px higher and reaches about 220 px less far to the right** than the console's, with 86 % of its
energy. Neither is in `hdr.cpp` or `ribbon.cpp`'s arithmetic in a form that can be corrected there without faking a
number, and no source file was edited by this pass - the temporary diagnostic described in 3 was removed and a
rebuild of the reverted tree reproduces the canonical frame with **0 differing pixels** and PASS 6/6.

Method: the native frame captured with the harness's own pins (`--render-clock 30`, `--background-clock`
2026-10-25T10:00, `--ui-clock` 2026-10-25T12:32, `--focus-time 2000`) against
`out/parity-references/root__original__day__1080p.png`, plus the settings-page and theme-list references. All
numbers below are Pillow/numpy statistics on those PNGs; the scripts are scratch files outside the repo
(`out/bgprobe/`), so nothing derived from the firmware enters Git.

### 1. The per-band table

Means over 12 horizontal bands of 90 px, 8-bit, native minus console. The 10-28 column re-runs the same binary
with `--background-clock 2026-10-28T10:00:00`, the date the console's own clock reads on the reference
(`root__original__day__1080p.state.json`), to separate the month-palette term from the rest.

| band | y | R nat / con / d | G nat / con / d | B nat / con / d | luma d (10-28) |
|---|---|---|---|---|---|
| 0 | 0-90 | 117.5 / 99.9 / **+17.6** | 96.1 / 78.9 / **+17.2** | 56.2 / 39.7 / **+16.4** | +17.2 |
| 1 | 90-180 | 116.0 / 98.3 / **+17.8** | 94.1 / 76.8 / **+17.4** | 55.8 / 39.2 / **+16.6** | +17.4 |
| 2 | 180-270 | 111.2 / 93.4 / **+17.8** | 88.8 / 71.4 / **+17.4** | 50.7 / 34.0 / **+16.7** | +17.4 |
| 3 | 270-360 | 109.7 / 90.6 / **+19.1** | 88.9 / 70.6 / **+18.3** | 52.8 / 35.2 / **+17.6** | +18.4 |
| 4 | 360-450 | 109.5 / 79.5 / +30.1 | 85.8 / 56.6 / +29.2 | 50.2 / 22.1 / +28.1 | +29.3 |
| 5 | 450-540 | 127.0 / 92.3 / +34.7 | 104.2 / 70.4 / +33.8 | 72.2 / 38.8 / +33.4 | +34.0 |
| 6 | 540-630 | 111.0 / 84.7 / +26.3 | 86.6 / 60.7 / +25.9 | 54.1 / 28.5 / +25.6 | +26.0 |
| 7 | 630-720 | 88.8 / 97.0 / **-8.2** | 63.8 / 72.1 / **-8.3** | 32.5 / 40.8 / **-8.3** | -8.3 |
| 8 | 720-810 | 91.8 / 96.5 / -4.7 | 66.4 / 71.0 / -4.7 | 35.8 / 40.3 / -4.5 | -4.7 |
| 9 | 810-900 | 94.7 / 85.0 / +9.7 | 68.4 / 59.2 / +9.2 | 38.3 / 29.5 / +8.8 | +9.3 |
| 10 | 900-990 | 99.0 / 82.6 / **+16.4** | 72.1 / 56.5 / **+15.7** | 42.4 / 27.3 / **+15.1** | +15.8 |
| 11 | 990-1080 | 100.7 / 84.2 / **+16.5** | 73.8 / 58.5 / **+15.3** | 44.7 / 30.7 / **+14.0** | +15.5 |

With the harness's pinned date (10-25) the four ribbon-free bands read +19.6 .. +21.4 and the bottom two
+16.4 .. +17.1; the 10-28 date removes about 3 levels and leaves 17. The "per-channel gain of 0.785" is the
least-squares shadow of this additive term: the console's B is the darkest channel, so a constant offset inflates
its **ratio** (1.50 in band 0) far more than R (1.21), and a single fitted gain lands in between. Fitted per channel
the gains would have to be 0.826 (R), 0.787 (G) and 0.663 (B) - three different numbers, which is the signature of a
constant, not a scale.

Within one band the difference is flat to +-0.6/255 across the full width. Horizontal profile of the top 70 rows
(no ribbon, no clock plate): native 80.2 -> 102.2 -> 74.9, console 63.8 -> 84.7 -> 57.7 over x = 0 .. 1760, and
`native - console` = 16.4, 16.4, 16.5, 16.6, 17.0, 17.5, 17.8, 18.0, 17.9, 17.8, 17.5, 17.2. Modelling the pair as
`con = a * nat + b` per channel over 63,980 background pixels gives `b` = -0.0135 / -0.0426 / -0.0504 with
`a` = 0.868 / 0.925 / 0.923 and, restricted to the top band, a pure additive model fits to rms **0.0028** (0.7/255)
against 0.024 for a pure multiplicative one. The difference is additive, and it is smaller in B only because the
console's B is smaller.

### 2. Which layer owns it: five independent exclusions

1. **Not the tone map and not the glare.** `XMB_BG_SCENE` dumps the scene target (background + ribbon +
   procedural pass) *before* `Hdr::finish`. In that linear dump the native is already +16.2 .. +17.5 (8-bit scene
   units) above the console's implied scene (`ToneMap` inverted with the day preset: EXPOSURE 1.05, WHITE LEVEL
   0.899181). A tone-map or exposure error cannot be present before the tone map, and an additive display-space
   offset cannot be present in the scene. The glare's own contribution in the same region is
   `final - toneMap(scene)` = 0.0146 / 0.0136 / 0.0118 (3.7/255), i.e. 22 % of the pedestal at most, and it is
   confined to the ribbon's halo.
2. **Not the particles.** The dump is taken before `particles_.render` (view.cpp 438 vs 458), so the dust is not in
   the numbers that carry the pedestal.
3. **Not the ribbon.** A temporary `XMB_RIBBON_SKIP` early return in `Ribbon::render` (added for this measurement,
   removed again) changes **0 pixels** outside y 240-700 and leaves the top four bands bit-identical
   (16.5 / 16.4 / 16.4 / 16.4). The ribbon is a *local* contributor (mean |delta| up to 31/255 in its band), never
   the flat term.
4. **Not the UI and not a display transfer.** In the `osk` pair the panel interior (which covers the background)
   reads +1.0/255 while the background around it reads +5.3/255, and the pedestal scales with the scrim that dims
   the background (the same background pixel is 63 native in `root` and 19.7 in `osk`, and the pedestal falls with
   it). A display-level offset would be constant over both. The text, icons and panels are not offset.
5. **Not the clock.** Captures at `--render-clock` 30 and with `--no-intro` (a different boot path, different boot
   weights) give the same 111.9 / 90.5 / 50.5 to 0.0.

That leaves one owner: `renderOriginalBackground`'s `back_colours0.fpo` draw, i.e. the firmware's own translated
moyou shader. Every input that `view.cpp` feeds it was re-checked against the firmware files this pass:
`_DayTime` 1168.77 at 10:00 (the RPCS3 FP dump value), `_NightTime` 2200, `_NightDayBlend` 1, `_NightBrightness`
0.486059 = `override/day/BACKGROUND.mnu`'s NIGHT WHIT BIAS, `_Alpha` 1, the four `month_bg/{rgb,night}/10|11.dds`
palettes decoded and dumped (October = 226/163/12 at the top row, November = 152/115/45). All match. A sweep of the
month term over four dates (10-01, 10-15, 10-28, 11-05) moves the top band from 162.8/147.5/31.6 to
111.9/90.9/51.4 and no date reaches the console's 95.1/74.0/34.6, so the month blend is a ~3-level term inside the
pedestal, not its cause.

The remaining candidate class is therefore a **runtime constant the plugin sets that the native never sets**, or a
constant-slot assignment in the ucode translation (`src/rsx_program.cpp`; the shader body is the firmware's). This
is checkable with one experiment that this environment cannot run: an RPCS3 `XMB_FP_DUMP` of `back_colours0`'s
uniform block at the same clock, compared field by field with what `renderOriginalBackground` passes. Until then a
constant subtraction in `hdr.cpp` would be a symptom fix with a fitted magnitude, which is exactly what the gain
already was.

Note on the number: the pedestal is 0.057 in scene units and 17.0/255 in display units, i.e. **1/17.5**, close to
but not equal to the 1/16 of `ToneApplyDisplay`'s `- noise / 8` (the pre-exposure dither the native deliberately
omits, "the same sum without the dither"). That term is ruled out as the cause by its own texture: `Noise.tga` is
64x64 random with mean 127.6 and corner texels 102/119/124/192, so sampling it the way the shader does
(`texture(tex13, gl_FragCoord)`, clamped, i.e. a constant) can only give 12.75, 14.9, 15.5 or 24 levels, and if the
sampler is REPEAT the console would carry a 30-px dither pattern that its frame does not have (its local
neighbour-difference energy is 0.33/255 against the native's 0.16).

### 3. The ribbon: separated, and two earlier readings corrected

Rendering with and without the ribbon separates its contribution exactly (`native_ribbon = with - without`,
`console_ribbon = console - (without - 16.4)`, 8-bit luma):

| y | native ribbon | console ribbon |
|---|---:|---:|
| 360-480 | 17.0 | -0.2 |
| 480-600 | 30.5 | 15.9 |
| 600-720 | 2.3 | 22.6 |
| 720-840 | 0.0 | 19.2 |
| whole frame (sum) | 11.55 M | 13.35 M (**0.86x**) |

So the native's sheet is drawn in y 360-660, the console's in y 480-840: the native's sits about **140 px higher**,
not lower, and it is **narrower** (its energy is gone by y 720 where the console still has 19), reaching x ~1700
against the console's 1920. `GEOMETRY-ROOT.md` 12.6's "lower and wider" is a misreading of per-cell ratios: cells
above the native's band are brighter because the native's ribbon is there, which is the opposite of its position.
Crest centroids: native 0.42 (x 960) -> 0.50 (x 0), console 0.45 (x 960) -> 0.60 (x 0) - the same slope direction,
the console's steeper and lower, which is a **phase of the swell**, and the total energy within 14 % of the
console's agrees with a phase, not with a brightness error.

The phase and the extent are both set by the cloth's state, and the cloth's state here is the documented
approximation: without `--vsh-modules` the sheet settles from the plugin's generic flat start (FUN_0004ec90) for
14,400 steps, while the firmware starts from the preset-1 tables (FUN_0004e624). The pinned `--render-clock 30`
freezes whatever phase the settle produced after one extra step, and the reference still froze an arbitrary phase of
a 22 s cycle. Nudging POS/ANG or FFD SCALE2 to move the band would be fitting a live animation state to one still -
it would move every other state's ribbon out of place.

**The pinned clock freezes the cloth, measured.** Captures at `--render-clock` 30 and 45 differ by 77,793 pixels
whose largest channel delta is 1 (the map of the difference is 0.0 +- 0.2 everywhere), i.e. the pinned clock does not
advance the cloth: `dt = seconds - lastSeconds_` is 0 after the first frame, so each parity still shows the settle's
phase plus one 1/60 s step. The reference still, captured from a live console, holds an arbitrary phase of the same
22 s swell. This is worth stating plainly because it bounds what any ribbon work can buy on this scorecard: the
ribbon's 82 % share of the residual (4) is mostly a **pinning artifact** - two different instants of one animation
compared as if they were the same instant - not a material or brightness error, and the 0.86 total-energy ratio
says the material is close. Scoring the ribbon needs a cloth-phase pin (a second, advancing clock that the frame
grab does not use), which is harness work, not `ribbon.cpp`.

### 4. Effect on the scores, and what remains

With the tree unchanged (and the canonical frame byte-identical, 0 differing pixels against the pre-pass capture)
the three states read exactly the stored values: `root` frame 17.86 / 0.8226 (top background **22.24 / 0.9710,
which already passes 20.00 / 0.900**), `theme-list` 18.98 / 0.8602, `page-settings-system` 18.50 / 0.8533 with page
rows 16.26 / 0.7397. Canonical gate PASS 6/6 with the text-presence line (1,677 bright pixels): frame 18.31,
top background 20.25, category icon row 21.20 / 0.9029, item label row 11.95, clock text 11.39, info bar 14.98 -
the same six numbers as `GEOMETRY-ROOT.md` 12.5.

Two consequences worth recording before the next pass spends time on exposure:

- A **perfect** pedestal fix is worth 1.46 dB on `root`, not the gain's 2.26. Measured by subtracting k/255 from
  every pixel of the captured frame and rescoring: 17.86 dB as captured, **19.31-19.32 dB for k = 15..20** (the
  optimum is broad and flat, as an additive term that is only part of the error must be). The fitted 0.785 gain
  reaches 20.12 dB only because it also darkens the **too-bright ribbon band and the label rows**, i.e. part of its
  effect is a compression of the content mismatches (the reference's Game column holds PlayStation Store rows where
  the native holds Steam Store rows), not an exposure correction.
- The largest single region failure is not exposure at all: `item label row` 11.03 / 0.3957 is a content mismatch
  (different strings on the same rows), and `info bar` 14.54 / 0.7669 and `clock text` 11.57 / 0.5376 are the
  info-bar/text layers. Fixing the pedestal cannot move the root frame past about 19.2 dB while those stand.

Where the error sits after the pedestal is removed (same frame, 17/255 subtracted, MSE by band):

| band | y | MSE | share of pixels | share of error energy | PSNR |
|---|---|---:|---:|---:|---:|
| top | 0-360 | 163.7 | 33.3 % | 7.2 % | 25.99 |
| ribbon | 360-480 | 1124.3 | 11.1 % | 16.4 % | 17.62 |
| ribbon | 480-600 | 2632.8 | 11.1 % | **38.5 %** | 13.93 |
| ribbon | 600-720 | 864.3 | 11.1 % | 12.6 % | 18.76 |
| ribbon | 720-840 | 996.6 | 11.1 % | 14.6 % | 18.15 |
| bottom | 840-1080 | 365.0 | 22.2 % | 10.7 % | 22.51 |

**82 % of what is left lives in y 360-840, the ribbon's own band**, and y 480-600 alone carries 38.5 % at 13.93 dB;
the top and bottom of the frame are at 26.0 and 22.5 dB. That is the ordering the next pass should work in: first
the ribbon's sheet state (the 140 px / 220 px geometry gap in 3), then the pedestal, then the text layers - and not
exposure first, which is what the fitted gain suggested.

Nothing in this pass edited `src/`; `hdr.cpp`, `hdr.h`, `ribbon.cpp` and `ribbon.h` are unchanged, no budget,
reference or harness file was touched, and the scratch scripts and dumps under `out/bgprobe/` are local only.

## The pedestal, tested against the console's own constant block, 2026-10-28 (second pass)

This pass was asked to test the cheapest source hypotheses for the pre-tone-map residual: a uniform the plugin
sets that the native never sets, a constant-slot error in the ucode translation, a texture term, and a missing
blend/clear term. It closed all four with local artifacts and found that the residual is **not a constant
pedestal**: it is present at the day state (+0.065 linear scene) and absent at the night state (+0.002), so a
constant subtraction would be a one-state fit that over-corrects the other state by more than 20x. No source file
was changed by this pass; the build under test is `C:\dev\out\native-pedestal` from `view.cpp` 07:13:01 (another
pass's text/label work included), `hdr.cpp` unchanged.

### 1. Hypothesis 1, a uniform we never set: false, from the console's own ucode

`out/native-reference/fp-dump2` and `fpdump7` hold RPCS3 fragment-program captures of `moyou/back_colours0.fpo`
(97 and 468 ucodes of the program's 8608-byte microcode). `--decode-fp lines:lib/moyou/back_colours0.fpo@<dir>`
patches each capture back into the shipped program and reads every named parameter's slot value. The decoded
block, at the pinned 10-25 10:00 clock, against what `renderOriginalBackground` passes:

| parameter | console (decoded capture) | native (`view.cpp`) |
|---|---|---|
| `_DayTime` | 1168.77 | 1168.8 (cosine fit through the same point) |
| `_NightTime` | 2200 | 2200 |
| `_MonthTime` | 23.2258 | 23.629 (see 6) |
| `_NightBrightness` | 0.486059 (settled) / 0.513834 (coldboot1 boot) | 0.486059 at settle |
| `_NightDayBlend` | 1 | 1 |
| `_Alpha` | 0, 1.48e-05, ... smoothstep of the boot fade | 1 at settle |
| `PI` | 3.14159 | 3.14159 (the ucode's own literal) |

The program's parameter table has exactly these seven constants plus the four month samplers, `uv` and `col`; there
is no eighth additive slot. The captured microcode differs from the shipped `.fpo`'s in **17 bytes of 8608, in 5
instruction words**, and those five words are the patched literals (`_NightTime` at slot 0 = 2200 is the first) -
i.e. the console runs the same 395-instruction program this build translates, with the six values above patched in.
There is no uniform for the native to be missing.

### 2. Hypothesis 2, a constant-slot assignment error: the mapping is right

Because the console's patched literals land in exactly the slots our decoder names (the `--decode-fp` values above
are read through `CgProgram::parameters[name].constantSlots`), the parameter-to-slot map is confirmed against the
console, not just against our own dump. `renderOriginalBackground` starts from `backColours_.constants`, i.e. the
ucode's own 143 literal slots, so unused slots hold the firmware's defaults rather than stale values; there is no
"zero the unused slots" case here, and all six runtime values are written to every slot their parameter names.

### 3. Hypothesis 3, a texture term: the month palettes decode byte-exactly

`--month-sheet` (the app's own decode of `textures/month_bg/rgb/10.dds`) was compared with an independent decode of
the same file out of `lines.qrc` (A8R8G8B8, 64x32, five mips, masks 0x00FF0000/0x0000FF00/0x000000FF): mean delta
0.05/0.19/0.57 of 255 over the tile (the 0.57 in B is the contact sheet's "10 day" label drawn over one texel), and
the sampled texels match exactly (0,0 = 226/177/15; 0,32 = 225/162/14; 31,0 = 216/197/0). The shader's sample
coordinate is the firmware's own quantisation - `r[0].xy = floor(uv*64+0.5, uv*32+0.5) * (1/64, 1/32)` - so both
engines read the same texels with the same 50/50 texel-boundary filtering. No SRGB or mip bias is involved: the
textures are uncompressed, the native binds them with a Linear filter at LOD 0, and the derivative of the quantised
coordinate is 0 between steps.

### 4. Hypothesis 4, a clear colour or a double draw: false

`hdr.cpp` `begin()` clears the scene target to (0,0,0,1) every frame; `renderOriginalBackground` disables blending
and draws one fullscreen triangle, so the background *replaces* the clear and a clear colour cannot survive under it.
The only other contributor to the scene before the `XMB_BG_SCENE` dump is the procedural `background_` pass, which
with `overlay = original` sets `c = vec3(0.)` and adds only `grain/650` (+-0.2/255, mean 0). The ribbon is excluded
by the earlier pass's skip test; the particles are drawn after the dump.

### 5. What the residue actually is: a day-state term, not a pedestal

Splitting the residual through the shipped `ToneMap` curve (`glutils/ToneMap.fpo`, day preset from
`override/day/HDR.mnu`: EXPOSURE 1.05, WHITE LEVEL 0.899181), with the native's pre-tone-map scene from
`XMB_BG_SCENE` and the console's scene recovered by inverting the reference's displayed background (day: model check
toneMap(scene) vs native display rms 1.8-2.6/255):

| state | window | native - console, linear scene (R/G/B) | display delta (R/G/B) |
|---|---|---|---|
| day, 10-25 10:00 | y<70, x<1400 | +0.065 / +0.067 / +0.061 | +20.6 / +21.3 / +19.4 |
| night, 10-27 21:53 | y<70, x<1400 | +0.0023 / +0.0019 / +0.0003 | +1.50 / +1.38 / +0.82 |

The scene column is the reference's display run backwards through the shipped tone map, so it does not by itself say
whether the term enters before or after the tone map: a post-tone-map term of the same size lands in that column too.
What it does establish is magnitude and state dependence, and it rules out a tone-map *parameter* difference (below).

At the day state the difference is a tight plateau: 97% of the top band's pixels fall in +0.05..+0.08, and the
display profile across 160 px columns is +19.6..+21.3 (mean +20.3). At night the same measurement is 1/30 of that.
A constant added anywhere in the background chain cannot do that - the console's night background is 0.0194 in scene
units, so a +0.065 pedestal would make the native four times brighter than the console there, and it is not.

It is not a tone-map parameter change either. Solving the shipped tone-map form for the per-pixel
`k = 1/white^2` that maps the native's scene onto the console's display, for every EXPOSURE in 0.50..1.60, leaves no
interior solution: the spread of `k` falls monotonically to the grid edge (E 0.51, relative sd 4.6%), and the shipped
day preset leaves an rms of 20.76/255. A single (EXPOSURE, WHITE) pair cannot explain the pair of frames.

The shape that fits the day state is a term of roughly +0.06 plus about 8% of the level. It cannot be unconditional
arithmetic in the day path either: that path is still mixed at 0.5 weight at night (the decoded `_NightDayBlend` is
0.5 there, and the output really is a 50/50 palette mix), so a day-path term would still show at half strength, about
15x the +0.002 measured. The term therefore follows the *state* - the day preset or the day-side inputs - and every
input that reaches the background is now excluded (values, slots, program bytes, textures), which is why this pass
did not spend a fifth guess on it.

### 6. Two side findings worth their own line

- **`_MonthTime` has no time-of-day term in the console.** The decoded value at 10-25 10:00 is 23.2258, which is
  `(day-1)/daysInMonth*30` exactly; `view.cpp`'s `(date.day()-1+t)/date.daysInMonth()*30` adds +0.403 at 10:00. The
  shader uses the month as a phase (`fc[95] = -15`, then 0.25 per unit), so the effect is about 1.3% of the phase
  and well under 1/255 of the frame. This is a conformance hunk for `backgroundTime()`, not applied here because
  `view.cpp` is another pass's file.
- **The firmware's final composite in the captured frames is `ToneApplyDisplay`, not `ApplyGlare`.** The RSX call
  list (`out/native-reference/fp-dump2/calls.txt`, 306 draws, every draw's microcode length) draws the 96-byte
  `ToneApplyDisplay` five times, once per captured frame, and never draws the 128-byte `ApplyGlare` this build uses
  in `Hdr::finish`. That pass subtracts `texture(tex13, gl_FragCoord)/8`, but the console's own frame carries no such
  term: projected through the tone curve the scene delta accounts for the display delta to within 1-2/255, i.e. the
  post-tone-map part of the difference is an order of magnitude below the 1/16 = 16/255 that dither would subtract.
  So swapping the pass is not a pedestal fix. Recorded as a follow-up for whoever owns the HDR chain, because the
  program choice is a real divergence from the firmware.

### 7. What artifact closes this

The one measurement that decides between "our translation computes a different value" and "the console's chain
post-processes the background" is the console's **pre-tone-map scene for the settled day frame**: the contents of
the 64x32 colour surface the `back_colours0` draw renders into (`ucode_length 8608 draw N surface 64x32` in the
capture records - note the draw is at 64x32, not 1920x1080), at the pinned clock. The existing capture patch dumps
the program, its runtime constants, the bound textures and the final frame, but not intermediate surfaces, so the
console's own scene value at a pixel cannot be read today. One RPCS3 run at `--render-clock 30`,
`--background-clock 2026-10-25T10:00`, with the surface dumped after that draw, compared with the native's
`XMB_BG_SCENE` at the same pixel, decides it in a single comparison. Absent that, the next local step is a
CPU evaluation of the translated fragment program over the console's decoded constants and the two month palettes:
it would reproduce either the native's plateau or the console's value and localise the instruction.

### 8. Verification numbers for this build (no change kept)

- `work/ab-layers.ts --capture --exe C:\dev\out\native-pedestal\re-ps3-native.exe`: **PASS 6/6**, text presence
  1677 bright pixels (minimum 900) PASS; frame 18.48 (base 18.29), top background 20.25, category icon row 21.20,
  item label row 11.92, clock text 11.39, info bar 14.98.
- `work/parity.ts --capture --score --state root`: Tier A PASS (category game, depth 0, 11 rows), Tier B fail on the
  baseline budget: frame 17.97 / 0.8225, top background 22.24 / 0.9710 PASS, category icon row 20.52 / 0.9087 PASS,
  item label row 11.08 / 0.4003 (content), clock text 11.57 / 0.5376, info bar 14.54 / 0.7669.
- Per-band native - console luma, 12 bands of 90 px, canonical pins: +21.4, +20.2, +19.6, +20.0, +30.8, +35.8,
  +27.3, -6.0, -4.3, +10.5, +17.4, +16.6. Bands 0-3 (y 0-360) are ribbon-free and read +21.4..+19.6 in display
  units; the ribbon-free top window (y<70) reads +0.065..+0.067 in linear scene units; bands 10-11 (ribbon-free,
  y 900-1080) read +17.4 / +16.6 in display units.
- Determinism: two captures at `--render-clock 20` with the other pins equal are SHA256-identical
  (`72F01CEB1A07B320...EDD513A8`).
- No change was kept, so there is no before/after delta: the numbers above are the current tree's numbers, and the
  pedestal did not shrink. Scratch scripts and dumps live under `out/bgprobe/` and are local only; no budget,
  reference or harness file was touched.

## The display chain against the captured call list, and where the day term actually lives, 2026-10-28 (third pass)

This pass was asked to test the hypothesis that the state-dependent day excess (+19.6..+21.4/255 at day against
+0.8..+1.5/255 at night) is an energy term in the post-background chain: the glare/bloom contribution, a tone-curve
shoulder, or the firmware's `ToneApplyDisplay` composite. It rebuilt the console's display chain from the captures,
then measured the native frame against the native pre-tone-map scene on the same build. The result is the opposite of
the hypothesis and is worth stating first: **the day excess is already in the scene before `Hdr::finish`, and inside
that window the frame equals `ToneMap(scene)` to 0.0/255, so no display-chain term - glare, tone curve, composite
program or dither - can own it.** No source file was changed by this pass; the canonical frame is byte-identical
(SHA256 `FB3882E5...0F35412` from two independent captures) and the gate is PASS 6/6.

### 1. The console's chain, from the captured call list

`out/native-reference/fp-dump2/calls.txt` holds 306 draws with the microcode length of every draw; `fpdump7/calls.txt`
holds 2348. Program names come from the plugin's own table (`out/native-reference/lines/cg-programs.txt`, which gives
`ucode N bytes` per `.fpo`). Length histogram of the 306-draw window, with the only names of that size:

| ucode | count | program(s) of that microcode length |
|---:|---:|---|
| 8608 | 5 | `lib/moyou/back_colours0.fpo` (the 395-instruction background) |
| 400 | 4 | `lib/glutils/GlareSourcePre.fpo` (unique at this size) |
| 640 | 54 | `lib/glutils/Gaussian.fpo` (also `moyou/ffd_shader2.fpo`) |
| 96 | 5 | `lib/glutils/ToneApplyDisplay.fpo`, `moyou/ffd_alpha_blend.fpo`, `glutils/BlurAcc.fpo` (three share 96) |
| 48 | 27 | `glutils/AccGlare.fpo`, `ApplyDisplay.fpo`, `CopyVarAlpha.fpo`, `moyou/background.fpo` |
| 128 | 0 | `lib/glutils/ApplyGlare.fpo` (unique at this size) |
| 112 | 0 | `lib/glutils/GlareSourceNoTone.fpo` (unique at this size) |
| 272 | 0 | `lib/glutils/ToneMap.fpo` (unique at this size) |
| 384 | 0 | `lib/glutils/ToneApplyDisplayMRT.fpo` (unique at this size) |

Limits of that evidence, stated because the previous pass leaned on it: the window is a sparse sample (the recorded
frame indices run 24..478 and only five of them carry a full set of draws), and it contains **no** 272-byte `ToneMap`
and **no** 112-byte `GlareSourceNoTone`, i.e. it does not hold a complete HDR chain. So "the firmware never draws
`ApplyGlare`" is true of these 306 draws and not proven for every frame; and "96 bytes = `ToneApplyDisplay`" is an
inference from the size and from the once-per-captured-frame cadence, not an identification (two other programs share
96 bytes). What the window does establish is enough to read the two programs' arithmetic:

`ToneApplyDisplay.fpo` (6 instructions, translated in `out/native-reference/lines/glsl/ToneApplyDisplay.fpo.glsl`):

```
r0 = texture(_Glare, uv)                      // TEXUNIT1
r1 = r0 * _GlareWeight                        // slot 0
r0 = texture(preexpose_Noise, gl_FragCoord)   // TEXUNIT13
r0 = -r0 * 0.125 + r1                         // fc[1] = 0.125
out = r0
```

It never samples the screen: the tone-mapped screen is already in the destination and this pass adds `glare x weight`
and subtracts `noise/8`. `ApplyGlare.fpo` (128 bytes, ours) instead samples both and replaces the target:
`screen*_Weight1.x + glare*_Weight1.y`, and `Hdr::finish` sets `_Weight1 = (1, GLARE LEVEL)`. The two are the same sum
apart from the dither, which is what `VIDEO-REFERENCES.md` already records.

The background draw itself is `surface 64x32` with four 64x32 month palettes in `tex0..tex3` (format 0x85) - the
console renders the background at 64x32 and scales it up, where this build evaluates the same program at 1920x1080.

### 2. The measurement that takes the display chain out of the running

`XMB_BG_SCENE` dumps the float scene after the background draw, the ribbon and the procedural pass, and before
particles and `Hdr::finish`. Comparing that dump with the captured frame gives the chain's own contribution pixel by
pixel; comparing both with the reference gives the console's. Canonical pins (`--render-clock 30`,
`--background-clock 2026-10-25T10:00`, `--ui-clock 2026-10-25T12:32`, `--focus-time 2000`), top window y<70,
x<1400, where the previous pass measured the day term:

| day, y<70 x<1400 | R | G | B |
|---|---:|---:|---:|
| native display | 125.3 | 104.8 | 62.5 |
| console display | 103.9 | 82.7 | 42.9 |
| native scene (dump) | 0.4357 | 0.3673 | 0.2233 |
| console scene (reference inverted through the day preset) | 0.3642 | 0.2926 | 0.1549 |
| **native frame minus `ToneMap(scene)`** | **+0.00** | **+0.06** | **+0.09** |
| native minus console, linear scene | +0.0715 | +0.0747 | +0.0684 |
| native minus console, display | +21.4 | +22.1 | +19.6 |

The fifth row is the whole argument. In the exact window where the day term lives, this build's frame is
`ToneMap(scene)` to a tenth of a level: the glare contributes **0.0/255** there, and the tone map is applied exactly
as configured. A chain applied to the same scene can therefore not produce the console's value; the console's scene
must be below ours. Repeating it per band (x<1400) gives `native - ToneMap(scene)` of +0.00, -0.04, +2.08, +8.96,
+0.33, +1.87, +0.74, +1.47, +2.97, +2.12, +1.94, +2.10 for R - the glare is real but local (its maximum is at the
ribbon's upper edge, band 3), and it is zero in the ribbon-free top and bottom bands.

Two more host-side tests, both negative:

- **A tone-curve change cannot reconcile the pair.** Solving the shipped curve for the per-pixel `k = 1/white^2`
  over EXPOSURE 0.70..1.45 on the ribbon-free bands (y 0-240 and 900-1080, x<1400) leaves no interior solution: the
  best grid point is EXPOSURE 0.792 with a per-channel `white` of 0.739 / 0.791 / 1.712 (relative sd 194%) and an
  rms of 13.0/255, against 21.95/255 for the shipped 1.05 / 0.899181. A single (EXPOSURE, WHITE) pair maps our scene
  onto the console's display nowhere.
- **The day term is not an additive pedestal either.** Fitting `console = a * native + b` inside the top window gives
  a = 0.983, b = -18.1, rms 1.00/255, but across the ribbon-free bands the difference falls from +21.0 (band 0) to
  +16.4 (band 10) as the scene falls from 0.44 to 0.34, and per channel the scene deltas are equal in absolute terms
  while the channels' levels differ by 2x. A constant subtraction fits one band and misses the rest; the previous
  pass's refusal to ship one was right.

What the term looks like instead: a smooth field, flat within a band (+-0.6/255), +21 at the top and +16.5 at the
bottom, equal across channels in linear scene units. That is a property of the background program's output, not of
the display chain - which is where the previous pass's five exclusions put it, now with the chain itself measured
rather than assumed.

### 3. The glare term, and the one chain divergence this pass did find

The only glare-source program the captured window draws is `GlareSourcePre` (400 bytes, 4 draws), and it does not
read a linear scene: it inverts `ToneMap` on the tone-mapped display, which has been clipped at 1. The inverse
saturates, so the recovered scene at display white is `WHITE LEVEL / EXPOSURE` = 0.899181 / 1.05 = **0.8564** at
day, and the source can exceed `GLARE THRESH` (0.738857) by at most **0.1175** for any pixel at or above white.
This build feeds `GlareSourceNoTone` with the raw float scene and a threshold of `GLARE THRESH / EXPOSURE`, so its
source is unbounded above (a scene of 2.0 gives an excess of 1.30, eleven times the console's cap). That is a real
divergence in the chain and it is on the list for the next pass. It cannot be the day term: the glare is 0.0/255 in
the window where the term lives, and the cap only bites on the clipped specular of the ribbon and the clock plate.

The composite divergence is real too (`ToneApplyDisplay` + dither against `ApplyGlare`), and it is not ported here
because three inputs are untraced and each candidate interpretation is contradicted by a measurement:

1. **The dither.** `-noise/8` is state-independent. If it were the ~16-21/255 pedestal, the night background would
   be clipped to zero (the console's night top window is 6.28 / 6.17 / 5.80) and this build's would read +8, not the
   +1.50 measured. It also requires the noise sampler's address mode and the bound texel: the captured `tex13` of
   the background draw is 32x32 format 0xe5, not the 64x64 `textures/TGA/Noise.tga` the second pass names, and the
   captured display draw has no parameter dump.
2. **The weight.** `_GlareWeight` is a runtime constant whose setter the decompile leaves behind a vtable
   (`custom_render_all.c`); the AccGlare per-level `_Weight` is a reconstruction (`GLARE SUM POW^level`
   normalised), so the console's per-level weights are not known either.
3. **The destination and blend.** `ToneApplyDisplay` does not write the screen, so its result depends on what is
   already in the target and on the blend factors, neither of which the capture records. Swapping it in without them
   would subtract a fitted constant from one state and clip the other.

### 4. The night state, measured properly

The second pass's night numbers are the top window at the reference's own clock; this pass reproduces them, and also
finds that the night state is not globally right: its lower half is a second, separate gap. Reference
`root__original__night__1080p` (console 10/27 9:53 PM) against a capture at `--background-clock
2026-10-27T21:53:00`:

| night | band 0 (y 0-90) | band 5 | band 8 | band 11 |
|---|---:|---:|---:|---:|
| native scene R/G/B | 0.023 / 0.022 / 0.019 | 0.212 / 0.189 / 0.157 | 0.121 / 0.080 / 0.030 | 0.172 / 0.114 / 0.049 |
| console scene, inverted | 0.015 / 0.014 / 0.014 | 0.135 / 0.115 / 0.104 | 0.062 / 0.020 / 0.001 | 0.119 / 0.059 / 0.001 |
| delta | +0.008 / +0.007 / +0.005 | +0.076 / +0.074 / +0.053 | +0.059 / +0.060 / +0.029 | +0.053 / +0.055 / +0.047 |

In the top window (y<70, x<1400) the night reads +1.50 / +1.37 / +0.83 display, matching the second pass; the lower
half carries +0.05..+0.095 in scene units, which is larger than the console's own level in blue (0.049 against 0.001)
and about 40% in red. That is a background-level, spatially structured
gap (a night palette / day-night-blend difference), not a pedestal and not the display chain. Every fragment-program
capture in the repository is at 10:00, so `_NightDayBlend`, `_DayTime` and `_NightTime` have never been decoded at a
night clock; that capture is the cheapest next artifact for this half of the problem.

### 5. Tables, scores and gate (no change kept, so before equals after)

12-band native-minus-console luma, 90 px bands, full width, canonical pins. Day (frame SHA256 `FB3882E5...0F35412`):

| band | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| day | +21.4 | +20.2 | +19.6 | +20.0 | +30.8 | +35.3 | +27.3 | -7.1 | -3.4 | +10.6 | +17.1 | +16.8 |
| night (21:53) | +3.8 | +6.4 | +14.1 | +20.5 | +15.9 | +31.7 | +27.0 | +16.8 | +23.8 | +22.8 | +20.6 | +21.7 |

Bands 0-3 and 10-11 are ribbon-free (the ribbon sits at y 360-840); the night lower bands include the reference's
different Game-column rows, which is why the top window above is the state comparison rather than the band means.
The day table is the current build's; bands 0-4, 6 and 9-11 agree with the second pass within 0.5/255 and bands 5, 7
and 8 differ by up to 1.1/255 (this capture is from `out/native-xmb`, the second pass's from `out/native-pedestal`).

- Canonical gate, `work/ab-layers.ts --capture --exe C:\dev\xmb\out\native-xmb\re-ps3-native.exe`: **PASS 6/6**
  with the text-presence line (1,677 bright pixels, minimum 900). Frame 18.31 / 0.8356 (base 18.29 / 0.8342), top
  background 20.25 / 0.9363, category icon row 21.20 / 0.9029 (base 20.93 / 0.8990), item label row 11.95 / 0.3924,
  clock text 11.39 / 0.4500, info bar 14.98 / 0.7639. No re-baselining.
- `work/parity.ts --capture --score --state root,page-settings-system`: Tier A PASS on both cells. Root frame
  17.86 / 0.8226, top background 22.24 / 0.9710 PASS, category icon row 20.52 / 0.9087 PASS, item label row
  11.03 / 0.3957 (content), clock text 11.57 / 0.5376, info bar 14.54 / 0.7669. `page-settings-system` frame
  18.50 / 0.8533 PASS, page rows 16.26 / 0.7397 (content). Same numbers as the second pass.
- Determinism: two independent `--capture` runs of root day (different run directories) are SHA256-identical,
  `FB3882E50EEF90956CBA9DB40E683DA9A744415323C9782F22B73E71C0F35412`.
- Build identity: every number above is `out/native-xmb/re-ps3-native.exe` (built 07:08:40), the build the stored
  capture baselines were measured on. Concurrent passes have since moved `view.cpp` (07:59) and `work/parity.ts`
  (08:03) and committed them; no HDR or particle file changed, and this pass rebuilt nothing, so the canonical frame
  above is the one that owns the current baselines rather than a fresh build of a tree other passes are still
  editing.

### 6. What closes it, and what this pass did not do

No source file was edited (no `hdr.cpp`, `hdr.h` or `particles.cpp` change was shipped), because every candidate
display-chain correction is either impossible in magnitude (the glare is zero where the term lives), contradicted by
one of the two states (the dither), or blocked on an untraced input (the `_GlareWeight`, the AccGlare weights, the
composite's destination/blend). The closing artifacts, in the order that settles the most per unit of work:

1. **The console's 64x32 pre-tone-map surface** after the `back_colours0` draw at the pinned clock - the previous
   pass's item 7. It decides between "our translation computes a different value" and "the console's draw differs"
   with one comparison against `XMB_BG_SCENE`.
2. **A CPU evaluation of the translated 395-instruction program** on the console's seven decoded constants and the
   two month palettes, which localises the instruction if (1) says translation.
3. **A night-clock fragment capture** (the existing ones are all 10:00) to read `_NightDayBlend` / `_DayTime` /
   `_NightTime` at 21:53, for the night lower-half gap in section 4.

The glare-source cap in section 3 is the one chain change worth doing and it should be done as a proper port of
`ToneMap -> GlareSourcePre -> threshold -> Gaussian -> AccGlare`, with its own before/after, not as a clamp bolted
onto the current `GlareSourceNoTone` feed.

Scratch scripts and dumps for this pass live under `out/hdrprobe/` (`analyze.py`, `fit.py`, the two scene dumps, the
21:53 capture) and are local only; no budget, reference or harness file was touched.
## The program evaluated on the CPU, and the day term located past it, 2026-10-28 (fourth pass)

This pass ran the decisive experiment the third pass asked for: `moyou/back_colours0.fpo` evaluated on a CPU over
the console's own microcode, its own constants and its own month palettes, then compared with this build's
`XMB_BG_SCENE` dump and with the console's implied scene. The answer is that **the translation is exact and the
64x32 draw stage is not the cause**: the console's own 395-instruction program, fed the console's own numbers,
reproduces *this build's* value to 0.75/255, not the console's. The day term is therefore applied to the background
layer's **output** - the 64x32 surface the draw leaves behind holds a value the program did not compute. No source
file was edited (the fence holds); helpers are under `work/` and local.

### 1. The instrument, and why it can be trusted

Three independent implementations now exist for the same program and they agree:

1. `work/fp-interp.py` decodes the microcode directly (RPCS3 `FragmentProgramDecompiler` rules: opcode =
   `((dst>>24)&0x3F)|((s1w>>31)<<6)`, 16-byte literal slot after any instruction that reads a constant, H/R split,
   fp16 destination rounding, fixed12/fixed9/saturate clamps, per-source precision modifiers) and executes it with
   numpy. It reports **395 instructions, 143 literal slots**, and the four TEX reads on units 0..3.
2. `work/fp-glsl-eval.py` parses the translated GLSL from `out/native-reference/lines/glsl/back_colours0.fpo.glsl`
   (the translator emits exactly one statement per non-sync instruction, so it can be stepped in lockstep) and
   evaluates it with the same vec4 semantics.
3. The GPU's own output: `out/hdrprobe/scene-root-day.png`, the `XMB_BG_SCENE` dump at the canonical pins.

Instruction-stepped comparison of (1) against (2) at (960,10): **identical to 0.0 over all 380 data instructions and
all 96 H registers** - the only divergences this pass found were two bugs in the *new* helper (a scalar op must read
lane x of the swizzled source, `EX2 r6.z, -r6` is `exp2(-r6.x)` and not `exp2(-r6.z)`; and the patched constant
block must be written back into the literal slots), after which the two agree bit for bit. (1) against (3) over the
full top window (1400 x 70 px): **mean |delta| 0.24 / 0.32 / 0.42 of 255, maximum 0.75** - the CPU evaluation
reproduces the GPU render. That is also an independent confirmation of `src/rsx_program.cpp`'s decoder for this
program: no instruction, swizzle, mask, clamp or literal is mistranslated.

Inputs recovered for the CPU run, in the order the task asked for:

- The console's **microcode**: `out/native-reference/fp-dump2/*.ucode` holds 97 captures of 8608 bytes each. Against
  the shipped `.fpo` they differ in **5 words of 2152** (`0x10`, `0x3a0`, `0x13e0`, `0x1680`, `0x2190`), and all
  five are constant-slot payloads rather than instruction words (the last, `0x2190`, is the 16 bytes at
  `base+16` of the final instruction, i.e. slot 142 = `_NightDayBlend`); three of them also change between
  captures. So the console runs this program with its constants patched into the literal slots, exactly as the
  second pass described, and the shipped bytes are the same program with those slots left at zero. The interpreter reads the literals straight out of the capture, so it
  starts from the console's real constant block and then overwrites the seven named slots with the values under test.
- The **palettes** are decoded here from `lines.qrc` (`C:\dev\rpcs3-cap\dev_flash\vsh\resource\qgl\lines.qrc`), not
  from the build: `textures/month_bg/{rgb,night}/{10,11}.dds`, A8R8G8B8 64x32 with five mips, masks
  0x00FF0000/0x0000FF00/0x000000FF. Row 0 texel 0 of `rgb/10` is 226/177/15, (0,32) is 225/162/14, (31,0) is
  216/197/0, matching the values the second pass printed, so the decode is the same one the app does.
- The **sample coordinate rule**: the program builds it itself, and it is richer than the dossier's summary. In
  ucode terms `r0.xy = floor(uv*64 + 0.5, uv*32 + 0.5) * (1/64, 1/32)` is only the first half; the sub-texel
  remainder `r5.zw = frac(...)` is then run through `x^3*(6x^2 - 15x + 10)` (smootherstep) and added, so the sample
  coordinate is `(floor(uv*64+0.5) - 0.5 + smootherstep(frac(uv*64+0.5))) / 64`. At the console's own 64x32 draw
  size `frac` is exactly 0 and the coordinate is the exact texel centre of texel `floor(...)`; at 1920x1080 it
  sweeps a texel over each 30 px block. This is why the console reads texels and this build reads smooth blends of
  them - and it is *not* the day term (section 3).
- **uv** is this build's fullscreen triangle: `v_tc[0] = (p.x, p.y, 0, 1)`, `p = gl_Position*0.5 + 0.5`, so at image
  row y (top down) `uv = ((x+0.5)/1920, 1-(y+0.5)/1080)`. The interpreter is given the same uv, so the comparison is
  per pixel.

### 2. The sample points

All values are linear scene units in the window y<70, x<1400 unless stated; display values are those of the shipped
day preset (EXPOSURE 1.05, WHITE LEVEL 0.899181). "Console" is the reference frame's display run backwards through
that curve (the third pass's method; reproduced here to 0.0001 and re-checked by the round trip).

| point | CPU, app MonthTime 23.629 | our GL scene (XMB_BG_SCENE) | CPU, console MonthTime 26.129 | console-implied | CPU(23.629) - GL | CPU(26.129) - console |
|---|---|---|---|---|---|---|
| (60,10) | 0.3862 / 0.3218 / 0.1761 | 0.3843 / 0.3216 / 0.1765 | 0.3748 / 0.3049 / 0.1690 | 0.3174 / 0.2458 / 0.1094 | +0.0019 / +0.0002 / -0.0004 | +0.0575 / +0.0591 / +0.0597 |
| (400,10) | 0.4307 / 0.3640 / 0.2169 | 0.4314 / 0.3647 / 0.2157 | 0.4177 / 0.3466 / 0.2085 | 0.3612 / 0.2902 / 0.1519 | -0.0006 / -0.0007 / +0.0012 | +0.0565 / +0.0564 / +0.0566 |
| (960,10) | 0.4662 / 0.3977 / 0.2521 | 0.4667 / 0.3961 / 0.2510 | 0.4506 / 0.3796 / 0.2417 | 0.3913 / 0.3207 / 0.1800 | -0.0004 / +0.0016 / +0.0011 | +0.0593 / +0.0589 / +0.0617 |
| (1380,10) | 0.4162 / 0.3474 / 0.2031 | 0.4157 / 0.3490 / 0.2039 | 0.4023 / 0.3314 / 0.1939 | 0.3410 / 0.2697 / 0.1343 | +0.0005 / -0.0016 / -0.0008 | +0.0613 / +0.0617 / +0.0596 |
| (60,40) | 0.3846 / 0.3185 / 0.1752 | 0.3843 / 0.3176 / 0.1765 | 0.3750 / 0.3034 / 0.1674 | 0.3174 / 0.2458 / 0.1094 | +0.0003 / +0.0008 / -0.0013 | +0.0576 / +0.0576 / +0.0581 |
| (700,40) | 0.4630 / 0.3932 / 0.2494 | 0.4627 / 0.3922 / 0.2471 | 0.4499 / 0.3772 / 0.2391 | 0.3913 / 0.3207 / 0.1800 | +0.0002 / +0.0010 / +0.0023 | +0.0586 / +0.0564 / +0.0591 |
| (1300,40) | 0.4252 / 0.3548 / 0.2123 | 0.4275 / 0.3569 / 0.2118 | 0.4132 / 0.3405 / 0.2024 | 0.3511 / 0.2800 / 0.1413 | -0.0022 / -0.0021 / +0.0006 | +0.0621 / +0.0605 / +0.0611 |
| (60,68) | 0.3816 / 0.3158 / 0.1756 | 0.3804 / 0.3176 / 0.1765 | 0.3733 / 0.3024 / 0.1676 | 0.3174 / 0.2458 / 0.1094 | +0.0012 / -0.0019 / -0.0009 | +0.0560 / +0.0567 / +0.0582 |
| (960,68) | 0.4583 / 0.3888 / 0.2485 | 0.4588 / 0.3882 / 0.2471 | 0.4471 / 0.3750 / 0.2380 | 0.3846 / 0.3140 / 0.1765 | -0.0005 / +0.0005 / +0.0014 | +0.0625 / +0.0611 / +0.0615 |
| **window mean** | **0.4358 / 0.3674 / 0.2234** | **0.4354 / 0.3674 / 0.2232** | **0.4235 / 0.3516 / 0.2140** | **0.3641 / 0.2926 / 0.1549** | **+0.0004 / +0.0000 / +0.0002** | **+0.0594 / +0.0590 / +0.0591** |

The decisive column is the last one. **The console's program, on the console's constants and the console's palettes,
does not produce the console's value; it produces ours.** Against the GPU's own dump the per-pixel agreement is
0.24-0.42 of a level (max 0.75) over 98,000 pixels, so this is not an artefact of the CPU implementation. Feeding the
app's own `_MonthTime` (23.629 = `(25-1+0.4167)/31*30`) instead of the 10-25 capture's 23.2258 makes the agreement
even tighter: 0.4358 / 0.3674 / 0.2234 against the dump's 0.4354 / 0.3674 / 0.2232.

### 3. The draw stage, tested rather than assumed

The task's second candidate was the 64x32 draw and sampling stage. Four tests, all negative:

1. **Draw size.** Evaluating the same program on the console's own grid (`uv = ((i+0.5)/64, 1-(j+0.5)/32)`, 2048
   fragments) instead of 1920x1080 changes the whole-frame mean by 0.06 / 0.28 / 0.07 of 255 and the top window by
   under 3.5/255 (the 70-row window is 2.2 surface rows, so that is the whole difference). The +0.059 survives at
   both sizes. Resolution, fragment density and the smootherstep sub-texel sweep are excluded.
2. **Texture assignment.** Every month pair (`m/m+1` for m = 1..12) and phase 0..30 in 1.5 steps was evaluated
   (`work/fp-broad.py`). The best *raw* match to the console's field is 13.9/255 (pair 10/11 at phase 30, mean
   0.4233 / 0.3479 / 0.2036); no assignment or phase reaches the console's level (0.3641 / 0.2926 / 0.1549), and the
   console's R/B ratio of 2.35 is not on the program's phase curve at that level at all. Swapping the two groups (night
   palettes on tex0/tex1) gives 0.0521 / 0.0510 / 0.0469, i.e. rms 60.5/255 against the console - 4x worse, not better.
   Filtering is not in play either: the palettes are uncompressed, bound Linear/ClampToEdge at LOD 0 and magnified
   30x, and at the console's own 1:1 draw the program's own coordinate lands on texel centres.
3. **Month phase (a real confound in the earlier comparisons).** The day reference frame is console 10/28 10:00
   (`root__original__day__1080p.state.json`), so the console's `_MonthTime` there is 26.129
   (`(28-1)/31*30`), not the 23.2258 captured in a 10-25 session. Evaluating at 26.129 lowers the program's output
   to 0.4235 / 0.3516 / 0.2140, i.e. **0.012 / 0.016 / 0.010 of the 0.079 / 0.082 / 0.070 residue is the date pin**
   (3.7/255 display, the "about 3 levels" the second pass measured). What is left is a clean constant.
4. **Geometry.** Neither a vertical shift (-200..+200 px) nor a vertical scale (0.6..1.2) of the field improves the
   match: the unshifted field is already the best at 15.15/255 and the best shifted one is 15.10/255, with everything
   else worse, so the residue is not a misregistration of the background.

After (3) the residual in the window is **+0.0594 / +0.0590 / +0.0591** in linear scene units: equal across
channels to 0.0004, and the best single-parameter description of the whole 98,000-pixel window is "subtract
0.0593": **rms 0.653/255, maximum 1.81** - i.e. at the 8-bit quantisation floor of the reference, flat in x (rms
0.63 / 0.61 / 0.49 / 0.84 per x-quartile) and in y (0.65 / 0.66 / 0.65 per tercile). A single gain fits six times
worse (0.833 gives rms 3.98/255), a day/night blend leak fits 6.6x worse (best single b = 0.816, rms 4.30/255, and
its per-channel optimum splits 0.85 / 0.81 / 0.68), and a per-channel gain fits 1.27/255 only by spending three
parameters on a range in which gain and offset are nearly degenerate.

In display units that constant is **+17.8 / +17.4 / +16.9 of 255** at the console's own month phase, and +21.4 /
+22.1 / +19.6 at the 10-25 pin - which is where the earlier passes' numbers came from.

### 4. What this pass concludes, and what it does not

- **The translation is not the cause, and that is now a measurement rather than an exclusion.** The console's own
  microcode at the console's own constants reproduces this build's scene to 0.75/255 over the window, and the
  translated GLSL is instruction-identical to that microcode. There is no instruction, expression, swizzle or
  constant slot in `src/rsx_program.cpp` to fix for this defect; the file was not touched.
- **The 64x32 draw stage is not the cause either**, in the four senses the task listed that a CPU can test:
  sampling, filtering, texture assignment and draw resolution all reproduce our value when exercised on the console's
  own terms. What remains of that list is the *surface format*, and the experiment above reframes it: the question is
  no longer how the surface is sampled but **what the surface contains**, because the value the console's chain acts
  on is 0.059 lower than the value the console's own program computes.
- The residue therefore behaves like a term applied to the background layer's output: an achromatic -0.059 in linear
  scene units at the day state, +0.004 at the night state, present before `Hdr::finish` (third pass) and after the
  program (this pass). Two families survive: the 64x32 surface holds a value the program did not write (a blend or
  format behaviour of that draw on real hardware), or a scene-space offset sits between that surface and the tone
  map. This pass cannot separate them locally, and it did not fit either one into `hdr.cpp`; a subtraction there
  would be the same fitted constant the first pass already refused.
- **The one artifact that closes it** is unchanged from the second pass's item 7, and it is now a one-line
  comparison: dump the 64x32 surface after the 8608-byte draw on the console at the pinned clock and compare against
  this pass's evaluation of the same texels. The interpreter can emit that 64x32 surface exactly (it is
  `work/fp-grid-day.npy`), so the dump either reads 0.4235 (the draw is right and the offset is later in the chain)
  or 0.3641 (the draw writes less than the program computes) with no ambiguity.
- The `_MonthTime` conformance hunk stands as before and is worth **0.012 / 0.016 / 0.010 in scene units, 3.7/255
  display** - now measured on the console's own date rather than fitted. It is in `view.cpp`, which is outside this
  pass's fence, so it is returned as a hunk: `backgroundTime()` should pass `(day-1)/daysInMonth*30` with no
  time-of-day term.

Helpers, all local and firmware-derived, under `work/`: `fp-interp.py` (microcode interpreter), `fp-disasm.py`
(listing), `fp-glsl-eval.py` (GLSL evaluator), `fp-batch.py` (vectorised interpreter), `fp-eval.py`,
`fp-diff.py` (instruction-stepped comparison), `fp-vs-gl.py`, `fp-console-scene.py`, `fp-fit.py`, `fp-broad.py`,
`fp-align.py`, `fp-matrix.py`, `fp-blend.py`, `fp-final.py`, `fp-ucompare.py`, `fp-tex-pairs.py`,
`fp-res-test.py`, `fp-artifacts.py`, plus `fp-day-scene.npy` (1920x1080 at the app month phase), `fp-cpu28.npy`
(1920x1080 at the console month phase), `fp-grid-day.npy` (the same program on the console's 64x32 grid) and
`fp-console-scene.npy` (the reference frame's inverted scene). No budget, reference or harness file was touched.

## Ribbon extent: the sheet measured against the console's own vertex buffer, 2026-10-28 (fifth pass)

The exposure pass above localised 82 % of the remaining error in the ribbon's band and read it as "the sheet sits
about 140 px higher and reaches about 220 px less far to the right". This pass measures that claim against a better
instrument than either frame: the console's own captured ribbon vertices. `out/native-reference/vtxdump7` holds 720
consecutive ribbon draws, each the 128x128 clip-space position array of `lines1.fpo` (array 0, stride 32, `w` about
9). The clip positions are the sheet itself, so converting them with `x = (ndc.x + 1) x 960`, `y = (1 - ndc.y) x 540`
gives the console's silhouette with no background model, no pedestal and no text in the way. The same array is
available natively: `Ribbon::splineSheet` writes exactly those 16,384 clip positions into `vertices_`, and a
temporary dump (removed before the final build, see 5 below) wrote it in the same layout. Nothing else was added to
`src/`.

Scripts are scratch files under `out/ribbonprobe/` (not committed): `vtx.py`, `shape.py`, `sheetcmp.py`,
`motion.py` (console/native clip arrays), `pair.py`, `hp.py`, `final-tables.py` (frames), `capture-root.ps1` (a
capture that reproduces `work/parity.ts`'s invocation byte for byte: its frame is identical to the harness's, 0
differing pixels).

### 1. The sheet in clip space (authoritative)

Console: mean of `draw_0003..0008` (the settled draws of the burst; `draw_0001/0002` are the boot state).

| quantity | console | native, shipped path | native, `--vsh-modules` (preset-1 tables) |
|---|---|---|---|
| sheet bbox x (px) | -592 .. 2399 | -680 .. 2195 | -575 .. 2383 |
| sheet bbox y (px) | 413 .. 769 | 353 .. 674 | 419 .. 768 |
| ndc x span | 3.115 | 2.994 | 3.082 |
| ndc y span | 0.659 | 0.593 | 0.645 |
| w (camera depth) | 6.59 .. 11.05 | 6.38 .. 12.05 | 6.64 .. 11.08 |
| crest at the right end | 672 (c0, x 2137) | 440 (c0, x 1813) | 668 (c0, x 2126) |
| crest at the left end | 543 (c120, x -248) | 511 (c120, x -334) | 543 (c120, x -240) |

Per-column crest (row mean of the 128 vertices, every 8th column, c0 = the sheet's right end):

| c | console x/y | shipped x/y | tables x/y |
|---|---|---|---|
| 0 | 2137 / 672 | 1813 / 440 | 2126 / 668 |
| 16 | 1871 / 697 | 1405 / 455 | 1864 / 694 |
| 32 | 1429 / 693 | 1018 / 499 | 1424 / 694 |
| 48 | 1001 / 648 | 642 / 543 | 999 / 651 |
| 64 | 645 / 592 | 331 / 571 | 646 / 595 |
| 80 | 336 / 555 | 80 / 589 | 338 / 557 |
| 96 | 73 / 540 | -165 / 584 | 78 / 541 |
| 112 | -151 / 537 | -327 / 535 | -144 / 537 |

So the console's sheet is a single sagging band: crest 672 px at its right end, 543 px at its left end. The shipped
path draws the *opposite* slope (440 px at the right end, 511 px at the left), 204 px short at the right edge and
60-95 px high over the whole sheet. With the plugin's own preset-1 rest state the native reproduces the console's
sheet to **4 px in y and 11 px in x on every sampled column** (the residual is the console's live swell phase: the
burst's own frame-to-frame drift is 0.4-1.2 px, `motion.py`).

The console's burst also shows the sheet is not static: over `draw_0003..0120` its right end walks 2387 -> 2705 and
its bottom edge 768 -> 804, i.e. the resting ribbon is a slow travelling wave, and the reference still is one
instant of it.

### 2. Frame-space silhouette (what the scorecard sees)

Native, exact: `with - without` from two captures of the same build and pins (the ribbon-free one from a temporary
`XMB_RIBBON_SKIP` return, removed with the other instrumentation). Threshold 8 of 255 luma. Console: reference
minus (native ribbon-free + the fitted background), with the local lower envelope of the residual removed because
the console's background program differs from ours (the pedestal of the pass above); it is an estimate and is
labelled as one. Rows are restricted to 200-960; the "clean strips" avoid the item-label and value columns, whose
strings differ between the builds.

| state | frame | bbox y | crest (x 0-520) | crest (x 1400-1920) | right edge per 60 px band, y 300 -> 840 |
|---|---|---|---|---|---|
| root/day | native | 354 .. 635 | 558 | 430 | 1847, 1919, 1919, 1919, 1216, 716, -, -, - |
| root/day | console | ~480 .. 840 | 653 | 657 | 1782, 1909, 1909, 1919, 1919, 1919, 1919, 1919, 1852, 1337 |
| page-settings-system/day | native | 354 .. 635 | 560 | 430 | 1847, 1919, 1919, 1919, 1216, 716, -, -, - |
| page-settings-system/day | console | ~460 .. 900 | 567 | 587 | 359, 1061, 1528, 1814, 1917, 1919, 1919, 1517, 841 |
| root/night | native | 354 .. 635 | 558 | 430 | 1847, 1919, 1919, 1919, 1216, 718, -, -, - |
| root/night | console | ~430 .. 800 | 554 | 530 | 1555, 1604, 1919, 1919, 1919, 1919, 1915, 1816, 1638 |

Three readings from this table:

1. The native's band is the same in all three states (y 354..635, crest 558/430). The settings page does not move
   it and neither does the night preset. The frame-space error is therefore not a layout or a preset effect.
2. The "220 px shorter on the right" is the *vertical* offset seen from the right: the native's sheet leaves the
   bottom-right corner (right edge 1919 only down to y 540, 716 by y 600, nothing below 660), while the console's is
   still at x 1850-1919 down to y 840. The sheet's own right end is only 204 px shorter (2195 vs 2399), i.e. the
   horizontal extent is very nearly right; the corner is empty because the band is high.
3. Night is measured with the pin caveat: both the native and the console night references were captured at a pinned
   clock, the ribbon's *brightness* differs (the map's energy is 1.216x the day map, uniformly: correlation 0.9998
   over 258,456 overlapping pixels) but its *geometry* is identical, because at the pinned render clock the LINE1
   boot weights leave the preset at coldboot1 for the whole capture (`Ribbon::advancePresets`, `ribbon.cpp:469-473`,
   with `bootWeights(1/60)`: toClock = 1.4e-5). That is the same class of pinning artifact as the cloth phase and
   is *not* touched here.

### 3. The four candidates, each with its code line and its measurement

| candidate | code line | expected signature if wrong | measured |
|---|---|---|---|
| camera fovy/near/far | `ribbon.cpp:20` (`cameraFovy=53`, near .1, far 1000) | a uniform ndc scale about the screen centre; w unaffected | w 6.64..11.08 with the tables against the console's 6.59..11.05, and the per-column crest matches to 4 px. The projection is right to 0.5 % |
| sheet world placement (`POS`, `(0,0,-2)`, `FFD SCALE2`, `FFD OFFSET`) | `ribbon.cpp:405` (`eye=p.pos+(0,0,-2)`), `:414` (`(n+sum)*p.scale2+eye`), `:394` (`field*scale1+offset`) | a translation/scale of the whole sheet, w range scaled too | placement is reproduced by the same control run; POS/SCALE1/SCALE2/OFFSET come from `override/day|night/LINE1.mnu` (`POS X -8.2`, `SCALE2 3.2 / 1.27579 / 2.88782`) and the compiled fallbacks in `Preset` are bit-identical to that file, so a missing MNU cannot change them |
| projection aspect / viewport | `ribbon.cpp:403-404`, `:415`, and `aspect=pixelWidth/pixelHeight` at `:488` | ndc x span off by the aspect ratio while ndc y is right | ndc x span 3.082 vs 3.115 (1.1 %) and ndc y 0.645 vs 0.659 (2.1 %) in the control; a wrong aspect would tilt one only |
| lattice end conditions (`END Y`/`END Z`) and the sheet's segment count | `ribbon.cpp:114-117` (pin), `:230` (END keys), `:418` (`segment=k/8`, `t=(k%8)*16/127`) | the sheet's cross-section and its travelling wave wrong, i.e. crest shape wrong even with the right state | the control run reproduces the console's crest *shape* (values 672/697/693/648/592/555/540/537 against 668/694/694/651/595/557/541/537) and the swell's phase behaviour; END Y/Z read as 0.4/0.2 from `LINE1.mnu`, and the pin's own constants (11, 15, 0.5, 1.0, 0.1, 0.02, 10, -2) were re-read from the plugin's `.data` at 0x92d58..0x92d7c and match `Cloth::step` |

The conclusion is that none of the four is the error. The error is the *cloth's initial state*: 14,400 steps from
the invented fallback start end in a different instant of the same slow swell than the console's preset-1 table
(which carries positions *and* velocities: mean |v| 2.07 over the 361 points). Every sweep that changes only the
state moves the sheet by the observed amounts and can never land on the console's:

| settle length (steps) | sheet screen y | sheet screen x |
|---|---|---|
| 14,400 (shipped) | 353 .. 674 | -680 .. 2195 |
| 21,600 | 405 .. 798 | -811 .. 2401 |
| 28,800 | 261 .. 724 | -689 .. 2400 |
| 43,200 | 354 .. 743 | -875 .. 2425 |
| 72,000 | 461 .. 841 | -821 .. 2597 |
| plugin preset-1 table | 419 .. 768 | -575 .. 2383 |

Every settle length is inside the swell's own range of shapes (the console's burst moves the same edges by tens of
pixels at 0.5 px/frame), so picking one that matches the reference is fitting a live animation state to one still -
which the earlier pass already refused to do.

### 4. Fix decision

No traced value is wrong, so no geometry was changed. Two source comments were corrected instead, because both
claimed a provenance the artifacts contradict:

* `ribbon.cpp:131-145` said the fallback start is "the fallback start the plugin uses when it has no preset-1 tables
  (FUN_0004ec90)". It is not: `FUN_0004ec90` (decompiled at `out/native-reference/lines/custom_render_all.c:31916`)
  builds a flat plane, `x = (u - .5) x 4`, `y = (v - .5) x 4`, `z = -3`, `w = 1` from the `.data` floats 4.0 / .5 /
  -3 / 1.0 at 0x92dc8 / 0x92dc4 / 0x92dcc / 0x92dd0. The loop in the source is a **fit** to the preset-1 table's
  y/z extent, and it is the better of the two: settling from the traced plane draws the sheet 1031 px short at the
  right edge (x max 1368 against the console's 2399). The comment now says so.
* `ribbon.cpp:147-161` (the 14,400-step warm-up) now records that the settle is a warm-up whose end state is one
  phase of the swell, with the deltas above, so the next pass does not tune the step count or the camera to a
  reference still.

The artifact that would settle it: **the plugin's preset-1 rest tables** (positions at `custom_render_plugin.prx`
0x97c18, velocities at 0x98d04, 361 float3 each). `Ribbon::init` already consumes them (`ribbon.cpp:279-287`) when
the app is given `--vsh-modules <decrypted modules>`, and that is the only path that reproduces the console's sheet;
but the copy inside dev_flash is `vsh/module/custom_render_plugin.sprx` (315,225 B, signed/encrypted) against the
915,704 B decrypted module, so the shipped fallback cannot read it and the parity capture does not pass the flag.
Either the capture path passes `--vsh-modules` (harness work, out of this fence) or the tables arrive as a
documented, licensed local artifact. Nothing in `ribbon.cpp` can reconstruct the table's velocities, which is what
makes its state the console's and the settle's not.

### 5. Verification of this build (`C:\dev\out\native-ribbon2`)

* Before/after are the same drawing: the ribbon map's silhouette is identical (left-strip crest 552.5 in both, band
  y 367..795 in both, per-160 px crest 553/564/548/540 against 553/565/548/540) and five consecutive captures of the
  final build are byte-identical (`8B59B17DEBE28EFC...`). A frame is reproducible within a session but not across
  sessions: the same source built twice and captured 20 minutes apart differs by a smooth background term (whole
  frame 17.97 against 18.23 dB, while the *top background* region, where the ribbon never reaches, moves 22.24 ->
  23.76). That term is the background-layer residue of the passes above and is outside this fence; it is also the
  whole of the shipped-phase score movement between the two captures, since the ribbon's own silhouette is stable to
  0.1 px across them.
* `work/parity.ts --capture --score --phase-sweep 0,12,24,36 --state root` (shipped 18.2297 / 0.8263, ribbon band
  16.1436; live+0 s 18.0703 / 0.8297 / 15.9745; live+12 s 18.5450 / 0.8361 / 16.5553; live+24 s 18.3710 / 0.8371 /
  16.4806; **live+36 s 18.8899 / 0.8441 / 17.0507**, the best). The swept phases move the sheet's crest between 445
  and 555 px in the left strip and 482 and 663 px in the right one, i.e. the extent error is phase-dominated as
  claimed, but no swept phase reaches the console's band: the phase is not the whole 140 px.
* `work/ab-layers.ts --capture --exe C:\dev\out\native-ribbon2\re-ps3-native.exe`: **PASS 6/6**, text presence
  1,668 bright pixels (minimum 900) PASS. Against the stored baselines: frame 18.74 / 0.8361 (18.76 / 0.8360),
  top background 21.06 / 0.9434 (21.06 / 0.9434), category icon row 21.25 / 0.9073 (21.62 / 0.9062), item label row
  11.99 / 0.3895 (11.99 / 0.3895), clock text 11.41 / 0.4528 (11.41 / 0.4528), info bar 15.05 / 0.7667 (15.05 /
  0.7667). Nothing was re-baselined.
* Determinism: two `--render-clock 20` captures hash-equal
  (`93785964BEF5030E...`, 1920x1080, Tier A pass), and five `--render-clock 30` captures hash-equal
  (`8B59B17DEBE28EFC...`).

`ribbon.cpp` and `ribbon.h` are the only source files touched (comments only); `view.cpp`, `hdr.cpp`,
`particles.cpp`, `shell.*`, `main.cpp`, the harness, the parity tooling, the budgets and the references were not
edited.
