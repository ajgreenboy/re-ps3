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
| Background gradient | `lines.qrc` moyou/background, back_colours0 + month_bg textures, BACKGROUND.mnu; custom_render_plugin clock mapping | **Ported**: firmware Cg translated RSX→GLSL, inputs traced |
| Ribbon (wave sheet) | `lines.qrc` lines1 / ffd_shader1 / LinesController, LINE1.mnu; cloth in custom_render_plugin; geometry from the SPU spurs/moyou/spline/spline.elf | **Ported** (`ribbon.cpp`): PPU cloth, ffd_shader1 deformation lattice, spline.elf FFD + camera + bicubic sheet, each stage checked against RPCS3 captures (see "Ribbon port"); LINE1 presets follow the plugin's clock keyframes and boot schedule (see "Ribbon presets") |
| Particles | `lines.qrc` particles_quads / particles_second, PARTICLES*.mnu; simulation runs on SPU particles.elf | **Ported (2026-10-26)**: firmware-shaped pool in `src/particles.cpp` (1536 cap, 48-byte stride, fixed 0.0088883 s step, PARTICLES.mnu physics, Park-Miller seed), one indexed draw into the HDR scene before the tone map and a second draw into the glare source (`src/view.cpp:370-376`). Superseded reading (accurate before 2026-10-26): this layer was linkev/PlayStation-3-XMB's additive point sprites, not a firmware port. Still open: the real `particles_second` program (the glare draw reuses the same geometry with a fitted 0.045 halo), `proc_iridescent.tga`, and the `_Color`/`_Gamma`/spot packing (`audit/PARTICLES.md`) |
| HDR tone map + glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, HDR.mnu | **Ported** |
| Category icons | `icons.qrc` lib/icons/quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo; ICONS.mnu with day/night/yoake/higure/boot overrides; widgets/other/texenv.dds (env map), amb.bmp; `icontex.qrc` 1n–70n normal maps + dif | **Geometry ported; material is a stand-in** (`icons.cpp`): firmware quad.vpo + icons_no_ss_no_shad.fpo (the variant the firmware runs at 1080p) and ICONS.mnu uniforms from an RPCS3 capture. The visible surface is the opaque native normal-map material for every built-in colour (`src/icons.cpp:110-130`); the firmware glass program runs only behind the diagnostics. Side-by-side frame parity is not claimed. |
| Item icons, labels, dialogs, info bar | explore_plugin / system_plugin via paf (RCO layouts and textures) | **Reconstructed** (item icons ported, labels moved to the GL atlas): `icontex` item icons and settings bullets draw through `IconRenderer` (firmware vertex program plus the opaque normal-map material, `src/icons.cpp:110-130,164-166`), with captured sizes 77.616/184.800 (items) and 35.721/85.050 (bullets), row pitch 80, brightness 1.0 in the open column and 0.247/0.698 behind an open page, all confirmed in `ITEM-ICONS.md`. Labels, dialogs, the info bar, the OSK, the options panel, the notification pop-up, subtitle cues and the volume HUD draw through the GL glyph atlas (`src/text_renderer.cpp`, called from `View::paintGL`); RCO, artwork and theme icons still use QPainter. Three corner/overlay composites were added on that path on 2026-10-26/27 - the notification plaque (`View::drawNineSlice` / `View::drawNotification`, audit/NOTIFICATIONS.md), the volume HUD (`View::drawVolumeHud`, audit/HTPC-IDEAS.md section 6) and the subtitle band (`View::drawSubtitle`, audit/SUBTITLES.md); none of them is a firmware layer, so they have no row here. |
| Text | paf font renderer with the RD-R/B (SCE-PS3 Rodin) faces from dev_flash/data/font | **Partial**: native-resolution GL glyph atlas with FreeType coverage (firmware load flags 10) and Qt shaping only; PAF coverage/style trace in TEXT-RENDERER.md; PAF metric authority, per-style weight/slant and glow parity open. Per-phase coverage landed 2026-10-26 as a fit (TEXT-RASTER.md 2.4) |
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

The cross bar is the paf widget class `PhXmBar`, inside vsh.elf's paf (around 0x3ab000–0x3b1000), not
explore_plugin. Its focus moves (`FUN_003ad3bc` next, `FUN_003ad728` previous, on input event 0x11 keys 4/3) step
the focus index (+0x2a8) and start widget animations of **200 ms** (DAT_0068f054). The XmBar animate calls pass
acceleration mode **5**, and so does the exported wrapper paf_E8149213 used by explore_plugin. explore_plugin's own
200 ms calls (65 of them) carry slot ids 0x10000NN; the mode is a separate argument.

paf's animation setup (`FUN_003981ec`) converts the duration to n = int(ms × 0.001 × 60) frames and the rate to
1/(n/60 × 0.999999). Its evaluator (`FUN_00398748`, mode in byte +0x23) is:

| Mode | Curve |
|---|---|
| 1 | √t (ease-out) |
| 2 | t² (ease-in) |
| 3 | (sin(π(t − 0.5)) + 1)/2 |
| 4 | per frame v = ((1 − v)k + v) × 1.0001, k from a 14-entry table (0.995 … 0.281), then 1/(3.775 + 0.217(n − 15)) |
| 5 | per frame v = ((1 − v)k + v) × 1.000001, k = 0.9615, 0.78125, 0.637, 0.529 for n = 1–4, then 1/(2.2 + (n − 5)/3) |
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
  1479 … 2144 ms. There is no acceleration over 14 repeats. Each repeat restarts the ease from the current position,
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
  0.765 per frame for the first 8 frames. That fits mode 5 with n = 11–12 (0.762 at 183 ms, 0.779 at 200 ms), so the
  200 ms assumption stands. A small tail of motion afterwards comes from the icons' scale change, which the tracker
  mixes in.
- **Page move** (into a column's page and back; Settings → Game Settings, then back): the toolbox icon travels
  261 px. Entering starts about 530 ms after the press, while the plugin loads the page; leaving starts after 34 ms.
  Both directions follow the same curve. The remaining distance falls by 0.760 per frame, steadily, for 17 to 18
  frames, and there is no snap at frame 12. So this is not the 200 ms mode-5 ease (0.779, with a 5% snap on frame 12).
  This capture method reads about 0.014 below the model on moves whose timing is known (the category move: 0.765
  against 0.779). The page move therefore fits mode 4 at 300 ms: n = 18, k = 1/(3.775 + 0.217 × 3) = 0.226, so
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