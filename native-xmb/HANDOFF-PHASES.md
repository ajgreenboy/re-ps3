# Native XMB — handoff: what remains for an exact match

**Forward plan (2026-10-27): [PHASES-2026-10-27.md](PHASES-2026-10-27.md)** supersedes the phase list below as the
plan of record; the per-screen 1:1 fidelity plan is [PARITY-PHASES.md](PARITY-PHASES.md). This file remains the record of the 2026-10-25/27 pass, including the RPCS3 capture how-to, the
values traced in that pass and the evidence links.

**Goal.** A C++ program whose output is identical to the PS3 4.93 XMB. Every layer comes from the firmware's own
programs, parameters and assets, read from the user's local dev_flash. Where porting isn't practical, a layer is
reimplemented, and its result is measured against the reference video (`nDL5YJt2aaA`, 60 fps, 1080p).

`audit/PIPELINE.md` maps each layer to its firmware source. `audit/VIDEO-REFERENCES.md` records every measurement.

## Ground rules (unchanged)

- Firmware, decrypted modules, extracted assets, captures, recordings, console identity and the VFS overlay never go
  into Git. Commit only source, docs and approved screenshots.
- Use only the local firmware. Scripts use Bun/TypeScript; no Python without approval.
- Commit as `Al <ajgreenboy@gmail.com>` to the private origin `ajgreenboy/re-ps3`. Other agents also commit here:
  run `git status` first and commit only your own files.
- Build with `native-xmb/build.ps1`. It fails with LNK1104 while `re-ps3-native.exe` is running; stop the process
  first. The build log is UTF-16.
- Verify with `--script` (keys, `grab=`, `burst=<prefix>@KEY`), never by eye alone. Compare against frames of the
  reference video at the same state.

## Native Jellyfin continuation (2026-10-26)

[Native service/player and verification handoff](audit/JELLYFIN-NATIVE.md): local authentication, native library pages, direct and server-transcoded HLS playback, queue/seek/pause/return, session reporting and sign out. Real Jellyfin 12.4 verified with generated media. Posters, selectors, resume UI and remembered sign-in remain open.

## Latest broader continuation (2026-10-26)

[Phase continuation and resume map](audit/PHASE-CONTINUATION.md): integrated app-registration editor, video controls that fade during playback, bounded seeking/hour labels, and Display Settings → Screensaver with verified native dim/wake. Exact firmware player panels, dim brightness and timings remain open.

## Where it stands

| Layer | State |
|---|---|
| Background, HDR glare | Firmware Cg programs (RSX→GLSL) with traced inputs |
| Ribbon | Ported end to end (2026-10-25): plugin cloth, ffd_shader1 lattice, spline.elf FFD/camera/bicubic sheet, each stage matched to RPCS3 captures; LINE1 presets follow the plugin's clock keyframes and boot schedule (audit/PIPELINE.md "Ribbon port", "Ribbon presets") |
| Particles | Firmware pool port (2026-10-26): the SPU `particles.elf` simulation with PARTICLES.mnu physics at the 8.8883 ms step, a 1536-record pool and the 48-byte vertex format, drawn additively into the HDR scene before the tone map plus a second draw into the glare source; deterministic from `--render-clock` (audit/PARTICLES.md) |
| Category icons | All built-in colours use an opaque normal-map material; Original follows the firmware monthly palette. Six day/night pairs verified; firmware shader remains diagnostic. See audit/CATEGORY-ICON-FIX.md; full parity open |
| Item icons, labels, columns, dialogs, info bar, options panel, OSK | `icontex` item icons and settings bullets draw through `IconRenderer` (firmware vertex program, opaque normal-map material) - sizes 77.616/184.800 and 35.721/85.050, pitch 80, brightness 1.0/0.247/0.698 confirmed against decoded RSX captures (audit/ITEM-ICONS.md); labels, dialogs, the info bar, the options panel and the OSK draw through the GL glyph atlas with Qt shaping, and only RCO/artwork/theme icons remain QPainter |
| Text | Original font faces rasterized with FreeType and recovered load flags 10 into the 2x GL atlas; label glow/idle fade are MaXMB observations; PAF metrics, per-style weight/slant and full parity remain open (audit/FONT-MATCHING.md). Per-phase glyph coverage landed 2026-10-26 as a labelled fit (audit/TEXT-RASTER.md 2.4) |
| Motion | paf's own eases (`View::PafEase`): category, item and cross-fade moves are 200 ms mode 5 (traced in vsh.elf PhXmBar); the page move is 300 ms mode 4 (fitted from RPCS3, not traced). Held-key repeat 300 ms then 66.7 ms; help text drawn on the selection with no delay (audit/PIPELINE.md "Menu motion") |
| Cold boot | 4.93 intro ported: coldboot.raf scene and script, boot sound, anim_coldboot2 events, background boot presets; skippable, `--no-intro` (audit/PIPELINE.md "Cold boot") |
| Themes | `.p3t` still background, icons and five menu-sound roles; RAF scenes and font/colour open |
| Input | Keyboard, XInput, and DualShock 3/4 / DualSense over HID (`src/pad.cpp`; HID untested without a controller) |
| Installed titles | Installed Steam games appear like installed PKGs directly in Game; dev_hdd0 and games.yml titles retain their RPCS3 launch path. See audit/STEAM-INTEGRATION.md |
| Steam Friends | Signed-in local client helper reads 46 real friends/presence without changing profile privacy; Web API remains optional fallback. See audit/STEAM-CLIENT-FRIENDS.md |
| Imported apps | Native editor and versioned importer add executables, browser apps and authenticated Jellyfin services. Jellyfin direct/HLS playback verified; see audit/JELLYFIN-NATIVE.md |
| Host settings | System Information, network list, video/audio output and master volume read and write the real PC |

## In flight (2026-10-25; updated 2026-10-26)

- Third-party code (MaXMB, DeckyXMB, linkev/PlayStation-3-XMB) is credited in `THIRD-PARTY.md`.
- `src/icons.cpp` had an uncommitted change from another agent during the 2026-10-25 session. `git status` was clean
  at the 2026-10-26 snapshot, so that warning applies only to the earlier session.

## 2026-11-04: particle glare pass on the firmware programs (particles_second, drawn into the scene)

Pass 2 now runs the firmware's own `lib/particles/particles_second.vpo/.fpo` through the same path as pass 1
(`Particles::drawFirmwarePass`), with every uniform packed by the same `FUN_0005c254` setters, plus `_Glare`.
- **`_Glare` (traced, 0x5c5a8-0x5c5d4).** (GLARE, GLARE SCALE, GLARE P1, GLARE P2) from renderer+0x1438..0x1444 in key
  order, to both programs. The vpo reads only .y; the fpo's halo is .x x exp(-.w x r^.z) on the centred uv radius
  (the z/w roles are read from the fpo, the order from the key layout).
- **What pass 2 is.** A camera-facing quad (`_FrontFacingQuaternion` alone) of size S x GLARE SCALE x |N.H|^SPECULAR
  POWER, with S the pass-1 depth-of-field size: only a particle catching the spot's specular highlight gets a glare
  quad. Same darkness, notch, life box and fresnel terms; `_Color` and `_Gamma` scale it like pass 1.
- **Target (traced, corrects the audit).** `FUN_00060660` draws pass 1 then pass 2 (holder field 0x75 = 2, both bits;
  no preset key) into the one bound target with ONE/ONE and no framebuffer bind between them. Pass 2 is therefore drawn
  into the HDR scene right after pass 1, not into the glare source: the dust reaches the glare chain through the scene.
  The audit's "pass 2 into the glare source" was a reading of the shader's purpose, not of the draw. With the firmware
  programs loaded, `renderGlare` is a no-op; the hand-written glare program remains the fallback.

Result: the glare quads are glint-only, so the effect is small (speck histogram within 3 of the previous run in every
bin). Gate: frame 19.51 -> **19.61**, other layers unchanged, 6/6; named states re-saved deliberately, 12/12;
byte-stable across runs. Still open: the `_Color` grey (measured 0.2), the life box's camera transform,
PARTICLES_SPE's offsets (playback only), and the caller `FUN_0002b710`'s target bind (not traced, assumed the scene
because pass 1 must be there).

## 2026-11-04: particles on the firmware programs (particles_quads.vpo/.fpo, traced packing)

Pass 1 now runs the firmware's own `lib/particles/particles_quads.vpo/.fpo` (lines.qrc, through `CgProgram`), with
the vertex and fragment constants packed as `FUN_0005c254` (custom_render_plugin 0x5c254) packs them from the renderer's
PARTICLES.mnu fields (renderer+0x12b0+4 x key index). The hand-written program stays as the fallback
(`XMB_PARTICLES_FALLBACK=1`, or a tree without the qrc).
- **Traced packing.** `_ParticleSize` = (size middle, size near, size far); `_Transparency` = (fresnel, global alpha);
  `_Focus` = (near focus, near focus + near focus_dist, far focus, far focus + far focus_dist); `_FocusCurves` =
  (near focus_pow, far focus_pow, camera fovy in radians); `_NearControl` = (size align, near fuzziness, near align);
  `_Darkness` = (near darkness, far darkness); `_LightPack` rows (eye), (spot pos, specular power), (spot attn),
  (specular coeff, lambert coeff, exposure, color_control) stored as columns - the fpo's slot table only resolves with
  that transpose. Blend ONE/ONE, FUNC_ADD, alpha test GREATER 0 (`FUN_00060660`).
- **What the vpo does with them.** Size and defocus follow the true distance to the eye: SIZE NEAR inside the near band,
  SIZE MIDDLE in focus, SIZE FAR past far focus + dist; NEAR/FAR DARKNESS fade a particle by its angular size; flakes
  blend toward the camera-facing quaternion by angular size x SIZE ALIGN + near defocus x NEAR ALIGN.
- **Space.** The simulation keeps flat viewport x/y and camera depth; a patch on `in_0` rebuilds camera-space positions
  under the ribbon camera (fovy 53) so each particle projects where it did. `_Modelview` is the identity, the eye the
  origin, `_FrontFacingQuaternion` the identity rotation.
- **Measured, not traced.** `_Color` is one grey the shell pushes from a state-machine message (`FUN_0001afdc`), never
  from a preset; its steady-state value is not traced. g = 0.2 puts the speck contrast histogram in the UI-free strips
  on the console still's (24..48 bins 108/147/23 against 128/140/26). `_Gamma` is its initialiser 1.0 (a fade: the
  draw is skipped at 0). Replace g with fc[34] from an `XMB_FP_SERIES` capture of the particle draw.
- **Open.** The life box is a constant world box (0x940b0/0x940c0: -10,-10,-12 .. 10,10,7) whose camera transform is not
  traced, so it is not applied; pass 2 (`particles_second`) is still the hand-written program; the console's dimmest
  specks (16..24) and its bright tail (128+) are both thinner in the native.

Result: specks in the side strips 297 -> 419 (console 543..1063 over six frames), now small flakes instead of large
ellipses, mean contrast 46 -> 31 (console 29..37). Gate: frame 19.41 -> **19.51**, other layers unchanged, 6/6; named
states re-saved deliberately, 12/12; byte-stable across runs.

## 2026-11-03: the glare weights traced (GLARE LEVEL squared, _GlareWeight = 1)

The two glare weights the glare port left as reconstructions are now traced. The field map comes from a hand
disassembly of HDRMenu vtable slot 2 (0x25c0c), the per-frame apply that Ghidra left unsplit. It copies each menu
record's value (+0x38) into the renderer: 0x29c EXPOSURE, 0x2a0 WHITE LEVEL, 0x2a4 GLARE LEVEL, 0x2a8 GLARE THRESH,
0x2b0 GAUSSIAN RAD R/G/B, 0x2c0 GLARE SUM POW, 0x2d0..0x2d4 GLARE / GLARE_ONLY / TONEBEFORE / BLUR / ENABLED,
0x2dc DITHER. 0x2e0 is not a menu parameter: it feeds ToneApplyDisplay's Gamma and is set from another object
(FUN_00070bf8, through FUN_0001b128).
- **AccGlare `_Weight`:** GLARE LEVEL enters twice (custom_render_all.c ~55005 and ~55852):
  _Weight_l = GLARE LEVEL^2 x SUM POW^l / sum_k SUM POW^k.
- **`_GlareWeight`:** the ToneApplyDisplay class (vtable 0x9ea48, name binding at 0x76b38) binds GlareWeight to the
  constant 1.0 (DAT_00093250). Its other records are NoiseJitter, Exposure, WhiteSqrRcp, Threshold, BlurRangeRcp,
  Dither and Gamma, and they match the values the code writes. The composite adds the glare x 1.
- **Change (`hdr.cpp`):** the AccGlare weight takes GLARE LEVEL^2, and ApplyGlare's `_Weight1` is (1, 1). The
  native glare was previously short by a factor of GLARE LEVEL (1.10 day, 1.33 night).

Effect: at most 2.3 levels, all at the ribbon highlights (pixels moving by 2 or more: under 0.01%).

Not traced: the level count this[0x1c4] (the native uses its mip count), the Gaussian weight function
FUN_00070b20, and what sets this[0x2e0].

## 2026-11-03: the pre-expose dither ported (preexpose_Noise)

The console frame carries a pre-expose dither that the native had left out. The "night bottom-right blue gap"
(-13.6) that started this pass was not a background error: the night reference has its Options button inside that
measuring window (x 1655..1815, y 950..995). With the button excluded, the night lower field is within ~1 level.
- **Measured (console stills):** in pure black, 36..40% of pixels read 1,1,1 and the rest 0. The pattern is exactly
  32x32-periodic (pixel agreement 1.000 at shifts of 32 and 64, ~0.5 elsewhere) and is a different tile in every
  still (no shift relates two of them). The day still carries the same period in its smooth field (high-pass
  autocorrelation 0.57 at 32 px, about 0 at 31). This reverses the earlier audit note that the frame has no
  30-px pattern, which tested day energy only.
- **Traced (custom_render_plugin, Sonnet trace of FUN_00071794 / FUN_00077398):** `preexpose_Noise` is 32x32,
  wrap REPEAT, sampled in texel coordinates at gl_FragCoord, and refilled every frame from a Hugo-Elias hash on a
  counter that advances 1024 per frame: n = c ^ (c << 13), r = (n(n^2 15731 + 789221) + 1376312589) & 0x7fffffff,
  x = (1 - r/2^30)/2 + 1/2, byte = trunc(255 clamp(8 DITHER x)), stored [v,v,v,0]. DITHER is the HDR parameter
  registered at 1/255 (DAT_000921a4) that no HDR.mnu sets, so a texel is trunc(8u), 0..7.
  LinesController adds noise/8 to the screen. ToneApplyDisplay draws ONE/ONE and adds glare x weight - noise/8, which
  the fixed-point target clamps at 0, so where the glare is below it the dither stays.
- **Prediction:** a black pixel is 1 when round-half-even(v/8) = 1, i.e. v >= 5: 3/8 = 37.5% (measured 36..40%).
- **Change (`hdr.cpp`):** `Hdr::fillNoise` builds the tile with the firmware's arithmetic. The ToneMap emit adds
  noise/8 and quantises the screen to 8 bits (round half even). The ApplyGlare emit takes the clamped glare-minus-noise
  term. The counter's initial value is not traced, so it is 1024 per 60 Hz frame of render time
  (`Hdr::setNoiseClock`), which keeps pinned captures byte-stable.

Result: native black field 37.0% ones, exactly 32-periodic, mean 0.4/0.4/0.4 (console 0.4/0.4/0.4, previously 0).
Day high-pass grain rms 0.42 (console 0.40, previously 0.28). Canonical gate: frame 19.42, top background 23.56,
category icon row 23.56, item label row 13.30 (contentDiffers), clock 11.47, info bar 15.18, all at or above the
previous pass. Named states re-saved deliberately (the dither is new in every state). Byte-stable.

Still open: `_GlareWeight` (ToneApplyDisplay slot 0) is one of the ToneApplyDisplay uniform writes `this[0x29c]`,
`this[0x2dc]` and `this[0x2e0]`, but the name table that maps slots was not found. The AccGlare per-level weight
traced as pow(SUM POW, l) / sum x `this[0x2a4]`, applied twice (FUN at line 55005 / 55852 of custom_render_all.c),
and `this[0x2a4]` is unidentified. The noise texel's channel remap (format 0x8018) is inferred from the measured
achromatic 1,1,1.

## 2026-11-03: the day curve traced (_DayTime), and the gate's background pin corrected

The background's time of day now runs the firmware's `_DayTime` curve, which replaces the cosine fitted through one point.
The background "residue" left after the pre-expose fix was mostly the gate scoring a 12:32 still against a 10:00 pin.
- **Traced (custom_render_plugin, Ghidra disassembly; a Sonnet subagent read it):** `FUN_00052ad8` clamps the day
  fraction t to 0..1. It builds the control vector (0, 1, k, k), with k = `obj+0x2ec`. `FUN_00052a28` multiplies
  (t^3, t^2, t, 1) by the cubic Hermite basis at 0x94840..0x9487f, and `FUN_0004584c` dots the result with that
  vector. So `_DayTime = 2400 x [(3t^2 - 2t^3) + k (2t^3 - 3t^2 + t)]` (2400 at 0x92e60). The curve is monotonic and
  point-symmetric about noon: f(1-t) = 2400 - f(t). The old cosine was mirror-symmetric, so 14:00 rendered exactly like
  10:00 and the hump could never sit right of centre.
- **k is the preset's DAYSPREAD** (inferred: the store into `obj+0x2ec` was not found). The one console constant,
  1168.77 at 10:00, solves to k = 2.73592. That is override/coldboot1's DAYSPREAD, 2.73593, and that capture's
  `_NightBrightness` 0.513834 is coldboot1's NIGHT WHIT BIAS. So that capture ran in the cold-boot preset. The clock
  presets day, yoake and higure share k = 2.68038. `View::dayCurve` uses that, blended from coldboot1 by the boot
  weights the same way NIGHT WHIT BIAS already is.
- **The two console stills.** `root__original__day__1080p` (console 10/28 10:00 AM, recorded provenance) against a
  native render at 10-28 10:00 is within 0.0..1.1 levels in every ribbon- and UI-free window (top, y150..230, bottom).
  The gate's `ab-rpcs3-root.png` shows 25/10 12:32 on its own clock. The 10-28 10:00 pin the gate had used belonged
  to the other still. At 25/10 12:32 the native now reads -0.7..-3.2 in the top windows (was -6.5..-13.7 at the old
  pin) and -0.9..-2.0 at y150..230 (was up to -8.9). A `_DayTime` sweep on that still (1168..1350, `XMB_DAYTIME`,
  kept as an instrument) put its hump at 1200..1230, and the traced curve gives 1208.6, worked out independently.
- **Night (console 10/27 21:53):** bottom-left goes from -3.5/-2.8 to +0.6/0.0, bottom-right R/G from -8.0/-11.8 to
  -4.7/-9.7. Blue is unchanged.

Gate, with the background pin moved to the reference's own clock (2026-10-25T12:32): frame 19.19 -> **19.41**,
category icon row 23.28 -> **23.52**, item label row 13.17 -> 13.28 (contentDiffers), clock 11.36 -> 11.47, info
bar 14.98 -> 15.18, top background 23.69 -> 23.50. All PASS. The 12 named states were re-saved deliberately: their
backgrounds follow the curve, so the old references scored about 37 dB. Re-run 12/12.

## 2026-11-03: the glare stage ported (GlareSourcePre on the encoded screen)

The glare source is now the console's: `GlareSourcePre.fpo` on the tone-mapped 8-bit screen, in place of
`GlareSourceNoTone.fpo` on the float scene (the divergence the third pass filed in "The glare term").
- **Traced (the firmware's own programs):** `GlareSourcePre` (17 instructions, `_WhiteSqrRcp` fc1..4, `_Exposure`
  fc6) writes rgb = screen.rgb x screen.a x 8 and puts the inverted-tone-map scene luminance (0.27/0.67/0.06) in
  alpha, which the chain does not read. The screen's alpha is the weight LinesController takes from
  preexpose_ExposeTex1 .w: G = max(1,t)(t - GLARE THRESH)/8, t the tone curve at max(R,G) (`FUN_00071794`). The
  captured call list draws it 4 times, each followed by the 64/48/640-byte Gaussian and AccGlare draws.
- **Change (`src/hdr.cpp`):** ToneMap.fpo's emit now also writes G into the toned target's alpha (same pre-expose
  lookup, `uGlareTable` = EXPOSURE, 1/WHITE^2, GLARE THRESH). GlareSourcePre reads that target with colour and
  weight clamped to 0..1 and rounded to 8 bits, as the A8R8G8B8 screen holds them. So the source is bounded: at most
  8 x the clipped colour, where the old feed was unbounded (scene 2.0 gave an excess of 1.30). The source is in
  display units, so AccGlare's per-level weight drops its EXPOSURE factor.
- **Not traced (unchanged):** `_GlareWeight` of ToneApplyDisplay and AccGlare's per-level `_Weight`. Those stay
  GLARE LEVEL and SUM POW^level normalised. The composite stays ApplyGlare (the same sum, without the noise dither
  that cancels).

Measured. Paired before/after captures at 12 render clocks with the cloth advanced (`XMB_RIBBON_ADVANCE`), same
cloth in both: max change 3.7..8.2 levels, at the ribbon's highlights. 0.04..0.59% of pixels move by 2 or more,
and the mean change is under 0.05. Ribbon cross-profile around each frame's own crest (column bands x 40..240 and
1680..1880): before and after agree within 0.8 levels at every offset, and both sit within the combined standard error of the console's
83-frame mean from the crest down (sem 1.6 console, 5..6 native); above the crest both run 2..4.5 high at d -80..-30. Canonical gate:
frame 19.19 (19.21 before), top background 23.69, category icon row 23.28, item label row 13.17, clock 11.36, info
bar 14.98, all PASS. 12/12 named states PASS against the existing references, none re-saved (57..61 dB). Two
captures byte-identical (day 674EEA18F3C0, night B95EFEEE50AC).

So at the shipped presets the old and new sources nearly coincide: the cap only bites on clipped highlights. The
port removes a known divergence rather than a visible error. What remains of the glare is the two untraced weights.

## 2026-11-03: background field fixed (the console's pre-expose tables)

The background field's day excess (FIDELITY-ROOT-BAR B3/B4/B6, the "pedestal" four passes chased) is the console's
pre-expose tables, now ported.
- **Traced (custom_render_plugin):** the console never draws ToneMap.fpo. LinesController writes the scene through
  preexpose_ExposeTex0/1 (units 15/14), two 128x128 two-channel half-float tables that `FUN_00071794` rebuilds every
  frame from `HDR::beginFrame` (`FUN_00071cc4`). Texel i holds the tone curve x(1 + x/W^2)/(1 + x) at
  x = 16 i/127 x EXPOSURE (constants 0x93210..0x93234), plus the glare weight G = max(1,t)(t - GLARE THRESH)/8 indexed
  by max(R,G). The noise texture is Hugo-Elias noise at 1/255 that ToneApplyDisplay subtracts again.
- **The offset:** the table is built at i/127 but sampled at texel centres, so the curve sees (128c - 8)/127,
  clamped to 0..16: an achromatic -0.063 in scene units. Against the fourth pass's CPU numbers it predicts the
  console's top window to 0.002 (0.3638/0.2914/0.1527 against 0.3641/0.2926/0.1549), and the night lower half
  (band 8: 0.059/0.018/0.000 against 0.062/0.020/0.001) that the third pass had filed as a separate palette gap.
  The one input not traced is the composite's lookup scale (u = c/16); the match is what supports it.
- **Change:** `Hdr::load` patches the scene fetch of ToneMap.fpo and GlareSourceNoTone.fpo to that lookup, and the
  fitted lower-field veil in `view.cpp` (its own comment said to remove it once B6 was fixed) is gone.

Result, mean luma native - console per 90 px band, canonical capture: ribbon-free bands 0..3 from +10..+14 to
-3..-7, bands 9..11 from +2.5..+4 (with the veil) to -0.7..-1.7 (without it). Gate: top background 21.06 -> **23.69**,
category icon row 22.11 -> **23.28**, frame 19.64 -> 19.21, item label row 14.49 -> 13.17 (contentDiffers). The
frame and label losses sit in bands 4..8, where the native and console ribbons are at different instants (see
"the ribbon offset is timing"); the veil had been darkening the native sheet there. Named states re-saved;
`--all` exits 0; byte-stable.

Still open: the top 70 rows read 4..8 levels dark and the bottom-left corner 6..9 dark, which is about the size of
the month-phase `+t` term (B2) and the half-float/8-bit quantisation of the tables; the glare path still differs
(GlareSourcePre on the encoded buffer, not ported).

## 2026-11-03: the ribbon "offset" is timing, not geometry

The canonical capture's sheet sits ~120 px below the reference still (right end ~160 px). Measured, it is not a
renderer error, so nothing in the drawing changed:
- **Over time the positions agree.** Crest y in two UI-free column bands (x 40..240 / 1680..1880, icon row masked),
  all 83 `col-*` console frames over four minutes: left **546 +/- 110**, right **560 +/- 105**. Native with the cloth
  advanced to 20 times over ten minutes: left **613 +/- 106**, right **541 +/- 89**. The right is the same, the left
  within ~2.5 standard errors and far inside either side's own swing.
- **The boot state agrees.** The rest table reproduces the console's vertex burst to 4 px (section "Ribbon extent").
- **The pinned capture is the outlier.** With the cloth frozen at the boot-instant table, the crest is 493 / **722**,
  which is the right end hanging ~160 px below its long-run mean. The reference still was taken an unknown time
  after boot, so it shows a different instant of the swell.
- **No frame-exact sync is possible.** `boot-02-15s.png` dates boot 2 to 17:09:01, so every `col-*` frame has a known
  time since boot (230..436 s). Rendering the native at those times gives the same family of shapes, and some crests
  agree (t324: 752 vs 754; t424: 538/468 vs 535/476), but most pairs differ. The cloth takes a random kick every step
  and RPCS3 runs its own step count, so two runs diverge within seconds.

Choosing a render time that makes the pinned frame match one still would fit a chaotic animation to a single frame.
The audit already refused that, so the canonical capture stays at the rest table. `XMB_RIBBON_ADVANCE=1` (in
`ribbon.cpp`) starts the cloth at the pinned `--render-clock`, which is the instrument for these over-time
comparisons.

## 2026-11-03: particles fixed (traced motion and light, dust born on the ribbon)

The dust was nearly invisible: 115 flecks in the UI-free side strips (x 0..380 and 1540..1919) peaking 51 luma
above their surroundings, against the console's 573..966 peaking 155..193 over seven allgraphics frames. Four causes,
three of them traced from the firmware:
- **Motion (traced, particles.elf 0x06f64-0x07134).** The kernel is semi-implicit Euler with dt = DELTA TIME:
  `vel += (gravity + BROWNIAN x R - FRICTION x vel) x dt`, `pos += vel x dt`, and the spin advances SPIN TIME SCALE
  x 1/60 per step. The port added the whole velocity every step (112x too fast, a newborn fleck crossed ~540 px in
  a second) with friction per step; `kBrownianStep` is gone.
- **Light (traced, particles_quads.vpo r[4] / .fpo).** SPOT POS is a point light: the light vector is
  normalize(spot - particle) per particle, and the fresnel alpha is taken against it, not the view axis. The 10-27
  "z sign fix" had applied it as one fixed direction, which is what dropped the peaks 127 -> 48.
- **Alpha.** The fitted squared depth fade (0.04..0.38 at the dust's depth) is removed. The life-bounds box it partly
  stood in for is +/-10000 (FUN_0005d234 from 0x93024/0x93028), so the vpo's edge fade is 1. NEAR/FAR DARKNESS
  and the DoF packing are still not ported (6-B3).
- **Emission and size (evidence, not traced).** Births go through a PPU command queue (FUN_0005cad0, type 0 =
  FUN_0005b6bc) whose producer is not decompiled, so the origin stays untraced. Every console frame shows the dust
  hugging the ribbon sheet, and NEAR/FAR FOCUS (6.1/12.8) and the spot depth (7.6) bracket the sheet's camera depth
  (6.6..11), so dust is now born on the sheet (`Ribbon::sheetSample` -> `Particles::setEmitter`, bilinear in a
  16x16 sample). The flakes keep their own orientation (edge-on ones draw as slivers, as on the console; the vpo's
  blend toward _FrontFacingQuaternion by _NearControl is untraced), and size goes through the ribbon camera's
  perspective (fovy 53) instead of the 1.15 fit. The box emitter remains the no-ribbon fallback.

Result: 262..391 flecks in the strips, contrast 45..52 (console 33..37), peaks 119..156, confined to the sheet's band.
Canonical gate: frame 19.64 (-0.21 against 11-03's ribbon run: three times as many randomly placed flecks cost
PSNR against a still they cannot match point for point), icon row 22.11, label row 14.49; byte-stable across runs.
Named states re-saved; `--all` exits 0.

Still open: the console has about twice the flecks and they are a bit dimmer (the darkness/DoF terms and the
front-facing blend are the likely gap); the dust sits ~120 px low because the sheet does; no iridescent colour
glints yet.

## 2026-11-03: ribbon fixed (rest table + settled preset)

The inverted crest (FIDELITY-ROOT-BAR R1-R3) and the thin, hard, over-bright sheet were two separate causes, and both
are fixed with traced data, no fit:
- **Shape.** Captures never passed `--vsh-modules`, so the cloth settled from the approximate fallback start and drew
  the sheet's slope inverted. The plugin's preset-1 rest table (`custom_render_plugin.prx` 0x97c18/0x98d04) is what
  matched the console's own vertex buffer to 4 px (PIPELINE.md "Ribbon extent"). `work/ab-layers.ts` now passes
  `capture.vshModules` (`C:/dev/xmb/fw493/vsh_module`, local only, never committed) to every capture, with a
  `--vsh-modules` override and a warning when the directory is absent. The app also reads `XMB_VSH_MODULES` when
  the flag is not given, so other tools and normal launches can pick the table up by setting one variable.
- **Look.** With the intro off, `view.cpp` now pins the ribbon's boot clock past the schedule
  (`setBootTime(1e9,Coldboot::normalBg)` in the no-intro branch), so it holds the clock preset like HDR, particles and
  the background instead of coldboot1 (FRESNEL 2, FFD SCALE2 Y 1.0).

Result, canonical capture against the RPCS3 frame: frame **19.85 dB / 0.8608** (was 18.84 on 11-02; stored base
19.21), category icon row 22.10 (+0.48 on base), item label row 14.45 (+2.39; still contentDiffers), top background,
clock and info bar unchanged. Three captures are byte-identical (D77B979A...), so the settle variance seen on 11-02
is gone: it came from the fallback start, not the preset. Named states re-saved on purpose; `--all` exits 0 (6/6,
12/12).

Measured and not kept: starting the cloth `--render-clock` seconds into its simulation (1,800 steps at 30 s) moved
the sheet up into the icon row (frame 18.42, icon row 18.79). The rest table is the console's own state.

Still open on the ribbon: the band sits ~120 px lower than in the reference still at both ends (the still is a later
instant of the live swell than the vertex burst the table matches; do not fit the step count to it), and the
sparkle particles are sparser than the console's. `work/parity.ts`, `fidelity-sweep.ts` and the other capture
scripts do not pass the modules yet; set `XMB_VSH_MODULES` before running them or their ribbon is the fallback.

## 2026-11-02: visual review pass (captured, compared with the RPCS3 corpus, fixed)

The app was run with `--script` across every category, a settings page, the options panel and the PS menu, and
each frame was compared with `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames`. Capture note: use `full`, not
`size=1920x1080` - the window size includes the title bar, so the frame comes out 1920x1065 and scaled 1.4%
(13 px side strips, rows ~7 px high), which reads as layout bugs that are not there.

Fixed and verified on captures:
- BG1 plate only behind a focused ICON0 tile row (it is the empty ICON0 backdrop, GEOMETRY-ROOT 12.7); it was drawn
  behind every focused row. The tile rule is one `isTile` lambda in `view.cpp` used by both.
- Photo/Music/Video Library and Continue Watching rows use `item_tex_plain_folder`; their album-art texture names
  made them wide album cards that overlapped the next rows and pushed the label to x741.
- Users column follows `category_user.xml`: Turn Off System (`explore_category_user:tex_power_off`,
  `Action::TurnOffSystem` -> the PS menu's confirm), Create New User (`explore_category_user:item_tex_NewUser`, the
  smiley with a plus), then the users with `tex_Avatar_Default`; the signed-in user is marked with the `tex_indi_Me`
  diamond left of the avatar instead of a dot in the value column. Steam Account stays last.
- PS menu: the focused entry was drawn with the label's dark drop shadow over a saturated bloom (a double-struck
  "Turn Off the System"); it is drawn without the shadow and the bloom strength is 1.8 (fit by crops; was 3.6).
- Installed Steam games are inline at the head of the Game column as ICON0 tiles that launch directly (user
  direction), ahead of Steam Store and the All Games folder; b046673 had moved them into the folder only.

Gate: `bun work/ab-layers.ts --all` exits 0 - canonical 6/6 (frame 18.84, -0.37; item label row annotated
contentDiffers because the reference's row is the Corrupted Data tile and the native's is Steam Store), named states
12/12 after a deliberate `--states --save-reference`. `catalogueRowIds` accepts `steam:*` for the run of installed
Steam rows (checked against the dump's installedSteam count), so the gate does not hard-code this PC's library.

**Found, not kept - the next ribbon task.** With the intro off, nothing settles the ribbon's boot clock (HDR and
particles default to the clock preset; the ribbon starts at boot 0). Unpinned, it replays coldboot1 -> clock over the
first seconds; under `--render-clock` (dt = 0) it stays on coldboot1 for good: FRESNEL 2, FFD SCALE2 Y 1.0, no
perturbation - the thin, hard, over-bright sheet in every pinned capture and baseline. `ribbon_.setBootTime(1e9,
Coldboot::normalBg)` in paintGL's no-intro branch gives the correct day sheet (broad and soft like the console), but
it scored worse (frame -1.28, top background -1.13, category icon row up to -5.68 dB, and not stable between runs)
because the settled sheet exposes the cloth's wrong phase (FIDELITY-ROOT-BAR R2/R3: crest high on the right where the
console's is low). Fix the phase, and the settle's run-to-run variance, first; then land the settled preset and
re-baseline.

## Next steps (recommended order)

Latest user direction adds Steam games/Friends and future content tooling. The implemented first slice and its
remaining requirements are in `audit/STEAM-INTEGRATION.md`: prioritize a recorded launch-return capture, permitted friends
connection, per-game ICON0/PIC1 overrides and the importer editor (now integrated; see the latest continuation). Updated 2026-10-26:
Jellyfin is a native service and player, not a URL app - local sign-in, library/folder pages, direct and
server-negotiated HLS playback, pause/seek/Back, session reporting and sign-out are verified against Jellyfin
12.4 (`audit/JELLYFIN-NATIVE.md`); posters, selectors and resume UI remain open. The superseded text ("Jellyfin
is currently a URL app, not a native player") was accurate before the native work landed.
Info-bar proportions and an analog clock now follow saved reference measurements (`audit/INFO-BAR.md`); exact
firmware composition/projection remains open.

Phase 7 continuation is in `audit/VERIFICATION-HARNESS.md`: named-state capture, expected-state guards,
deterministic UI/focus controls and correct PNG scoring are now exercised. Matched firmware states and
fidelity thresholds remain open. Do not follow the RPCS3 capture/input workaround below from this Codex session.

0. **Content tooling (in progress):** the Game column lists the account's installed Steam titles
   inline (10 here) and keeps the rest of the owned library in one **Not Installed** folder
   (owned not installed plus family-shared from `<data>/family-shared.json`, 275 rows here),
   with an install handoff for the titles that are not installed yet (`src/steam_owned.*`,
   `src/steam_collections.*`; design in `audit/XMB-TOOLING.md`). Purchased titles need
   `STEAM_WEB_API_KEY` in the launch environment; collections are read locally without it.
   Next there: per-game ICON0/PIC1 overrides (`src/steam_overrides.*`, done:
   `<data>/artwork/<appid>[_icon|_bg].png`), the Game column's Steam layout (installed titles
   inline, one `Not Installed` folder for owned and family-shared rows, done) and Steam Families
   shared titles, which need an OAuth token (a Web API key is rejected with 401;
   `<data>/family-shared.json` fills the gap meanwhile); next: a manifest-editor page, embedded
   media playback, and an end-to-end install -> launch -> return capture.
1. **Phase 7 harness first:** RPCS3 reference captures of named states plus `work/ab-shot.ts` scores, so every later
   change has a number.
2. **Phase 4 leftovers:** the page move's animate call is still not traced. The five explore_plugin constants at
   DAT_00270674 / DAT_002706c0 / DAT_00270658 / DAT_00270d30 / DAT_00271d0c are exactly 300.0f but drive content and
   dialog transitions, NOT the page move, so they cannot be cited as its evidence; the page move lives in vsh.elf paf
   PhXmBar (genuine 0.3 s bar constant DAT_0068f020). 300 ms at mode 4 remains a fit - the only model matching the
   measured 17-18 frame ramp, with mode 5 excluded - not a traced call. Updated 2026-10-26: the XMB clock is a
   `PhText` named `clock`, not a passive `PhClock`; the writer chain reaches vshcommon export 0x33FB58, where the
   per-minute tick still sits outside the local artifacts (audit/INFO-BAR.md:77-227; the only `Clock` objects are
   the Date and Time screens). See [PAGE-MOVE.md](audit/PAGE-MOVE.md).
3. **Phase 5 particles:** done 2026-10-26 for pass 1 and the glare draw - firmware-shaped pool, PARTICLES.mnu physics,
   48-byte records, HDR-scene placement, deterministic from `--render-clock` (audit/PARTICLES.md). Still open: the real
   `particles_second` program, `proc_iridescent.tga`, and the untraced `_Color`/`_Gamma`/spot and darkness/life-bounds terms.
4. **Phase 3 layout:** replace measured column, row and icon numbers with values traced from RCO objects and
   explore_plugin.

## Capturing the real XMB in RPCS3 (how-to, learned 2026-10-25)

- Boot: `rpcs3.exe --no-gui --config <scratch yml> <dev_flash>shmodulesh.self`. Keep the scratch config outside
  the repo and the RPCS3 folder. Never close an RPCS3 the user started.
- Set **"Exclusive Fullscreen Mode: Disable"** in the scratch config. With exclusive fullscreen, desktop capture
  (CopyFromScreen) returns one frozen frame while the title bar still shows 60 FPS. PrintWindow returns stale frames
  under Vulkan, so use CopyFromScreen.
- If the "Preloading cached shaders" overlay stalls for more than a minute, stop that RPCS3 and start it again.
- Input: for vsh.self, `--input-config` is ignored, and RPCS3 uses the global
  `config/input_configs/global/Default.yml` (the user's DualSense). Changing that file needs the user's approval
  every time. Back it up and check its sha256 before and after: the original is
  `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee`. Restore it as soon as the capture is done.
- With the Keyboard handler, arrows sent by `keybd_event` arrive, but **letter keys do not**. Map Cross and Circle
  to `PgDown` and `PgUp` in the temporary Default.yml, and send those keys as extended keys.
- Record by holding or tapping a key while capturing timestamped frames (about 55–118 a second). Analyse with
  `bun work/motion-track.ts <dir> [vertical|horizontal] x0 x1 y0 y1 threshold maxShift` (scroll per frame) and
  `bun work/region-luma.ts` (fades). The capture reads about 0.014 low per frame on known eases; allow for that
  when fitting.
- In the native app, `XMB_LOG=<file> XMB_MOTION_LOG=1` logs the column and depth eases each frame. Launch it with
  `--dev-flash … --data-dir <scratch> --no-intro`. Without `--dev-flash` it prints usage and exits with code 2.

## Phases to 100%

### Phase 1: category icon renderer (done for category and icontex item icons; RCO/artwork icons still QPainter, see below)
- Item-icon parity: [ITEM-ICONS.md](audit/ITEM-ICONS.md). Item icons and settings bullets share the category program and
  material (quad.vpo + icons_no_ss_no_shad.fpo, night override); captured sizes 77.616/184.800 (items) and 35.721/85.050
  (bullets), row pitch 80 (+175 first row, level-0 row 385 above focus), brightness 1.0 in the open column and
  0.247/0.698 behind an open page, parent column at distance 2857.2 with x 303/455.5 - all confirm the native values.
  Open: the opaque path tints RGB by brightness where the firmware scales alpha, a second 1.1022x size class, level>0
  sizes, and the Qt fallback for non-icontex icons.
- Firmware: `icons.qrc` quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo, ICONS.mnu (+ day, night, yoake,
  higure, boot and music overrides), texenv.dds, amb.bmp, `icontex.qrc` normal maps and `dif`.
- The uniform packing in custom_render_plugin is VMX code that Ghidra decompiles poorly (apply = FUN_000473b4, draw =
  FUN_00040ce8). Read the values instead from a real run. The local RPCS3 build (`C:\dev\rpcs3`, rebuilt with
  `build-rpcs3.bat`) dumps each distinct fragment program ≥2000 bytes at draw time when `XMB_FP_DUMP=<dir>` is set:
  `.ucode` with patched constants, `.vc` vertex constants and `.txt` texture units. Boot
  `bin\dev_flash\vsh\module\vsh.self`, then run `re-ps3-native --decode-fp icons:lib/icons/icons_full_ss.fpo@<dir>` to
  get the named uniform values per draw.
- Then add an `IconRenderer` (like `Hdr`/`Ribbon`): draw a quad per icon with the firmware programs, with the
  framebuffer behind it as `_BackgroundMap`. Pick the ICONS.mnu override from the clock as the day/night code does.
  Match against icon crops from the reference.

### Phase 2: text renderer
- Rasteriser traced: [TEXT-RASTER.md](audit/TEXT-RASTER.md). The firmware loads glyphs with FT_LOAD_NO_HINTING and
  rasterises outlines (FT_Outline_Get_Bitmap), so there is no stem snapping at all; coverage is copied to alpha with an
  identity gamma (no LUT), and overlapping coverage is unioned by compare-and-keep-larger, never alpha-over.
- Latest continuation: `audit/FONT-MATCHING.md`. Matched original faces now use FreeType with recovered
  NO_HINTING|NO_BITMAP flags; source identity is checked by head+cmap tables. PAF metric/pixel parity remains open.
- Active from 2026-10-25 at the user's direction. See `audit/TEXT-RENDERER.md` for the implementation, recovered
  PAF coverage/style behavior, RCO Text offsets, local evidence paths and remaining uncertainties.
- GL atlas now handles menu/subtitle/dialog/info/OSK text, with batched glyphs and separate coverage/halo.
  Native size snaps at the viewport scale; resizing clears the caches. Qt shaping preserves fallback and bidi.
  This is the renderer foundation, not a completed port of the PS3 rasterizer.
- Corrected the visible dark duplicate behind labels/clock: a restrained provisional shadow replaces unconverted
  RCO template offsets, and whole-string shadows render before foreground glyphs. See audit/TEXT-RENDERER.md.
- Build independently with `build.ps1 -OutputDirectory ../out/native-phase2` when running from native-xmb;
  this avoids build-log/executable locks from simultaneous Phase 1 work. Scripted captures support `size=1920x1080`.
- The firmware draws text through paf (libfont with FreeType-style rasterisation of `dev_flash/data/font` SCE-PS3 Rodin
  faces) and applies glow and shadow from RCO text attributes.
- Work: find the glyph rasteriser settings (hinting, gamma, size snapping) and the glow/shadow parameters in
  paf/explore_plugin. Render glyphs into a GL atlas at native resolution instead of QPainter text. Compare with 3×
  crops of reference labels: focused, unfocused, subtitle and dialog text.

### Material and background gaps reported by the user (2026-10-25)
- `_ChangingColor` now samples amb.bmp at firmware clock/season coordinates; `_LuminanceParams` now derives
  from the control-point curve scaled by preset GLASS. Both reproduce the prior night capture numerically;
  daytime capture parity is still pending. See `audit/ICON-MATERIAL-CLOCK.md` for proof and limitations.
- Updated 2026-10-26: icontex item icons and settings bullets no longer take the Qt path (IconRenderer,
  the item-icon queue in `View::paintGL`); original RCO artwork images still do. Route the opaque material's
  remaining firmware inputs through the item renderer after its asset/UV mapping is understood.
- **Resolved 2026-10-25: the "8x night brightness" was a clock-matching error, not a rendering bug.** The earlier
  comparison used `--background-clock 00:00` (midnight) against an RPCS3 capture whose console clock was actually
  **~10:00 daytime**. Dumping the real draw constants with RPCS3's `XMB_FP_DUMP` (`RSXThread.cpp` local patch) shows
  `back_colours0` receives `_NightDayBlend=1.0` (full day), `_DayTime≈1168` (near noon), `_NightTime=2200` at draw time —
  i.e. the console was in daytime, while the native was pinned to midnight. The native clock mapping itself is correct:
  `_NightTime=fmod(t+0.5,1)*2400` reproduces the firmware's `2200` at `t=0.4167` (10:00). The native day background at
  10:00 measures top-band luma ~102 (bright gold day), as expected. Remaining (separate, smaller) item: the native
  `_DayTime` curve is the fitted approximation (`~1125` vs firmware `~1169` at 10:00, ~4%), already noted as fitted in
  `VIDEO-REFERENCES.md`; the day path otherwise matches. No night-brightness correction is warranted.

### Phase 3: item, column and dialog drawing through the firmware's own layout
- Traced values and remaining gaps: [LAYOUT-TRACE.md](audit/LAYOUT-TRACE.md).
- Initial trace: system_plugin infobar/infotext and OSK templates mostly have zero/dynamic geometry. Do not
  replace measured pixels with those zeros. `basic_plugins.prx` FUN_00007c7c owns infobar initialization;
  its final configuration goes through vshcommon_28549FD0 and runtime PAF setters. Local read-only export:
  `out/native-reference/phase3-infobar-init.c`. Exact geometry projection remains to trace.
- Most of the 2D XMB is explore_plugin/system_plugin driving paf widgets from RCO objects: positions, scales and
  animations live in the RCO object trees (`rco-behavior` dump) and plugin code. The current values are measured
  from the video.
- Work: decode RCO object/anim attributes (objattribdef/animattribdef ini are already extracted) and the
  explore_plugin item layout constants. Replace measured numbers with traced ones: column x, row pitch, icon sizes,
  focus scale, label offsets, fade curves, the options panel and the dialog layout.
- Item icons: the firmware's icon glow/focus effect and the "PS" frame for game icons.

### Phase 4: motion and timing from firmware
- Page-move duration and info-bar clock trace: [PAGE-MOVE.md](audit/PAGE-MOVE.md). The five explore_plugin constants are
  exactly 300.0f but belong to content/dialog transitions; the XMB page move is paf PhXmBar in vsh.elf (0.3 s bar
  duration), and 300 ms at mode 4 is the only model matching the measured 17-18 frame ramp. Updated 2026-10-26: the
  XMB clock is a `PhText` named `clock`, not a passive `PhClock`; the writer chain reaches vshcommon export 0x33FB58,
  and the per-minute tick inside it is still outside the local artifacts (audit/INFO-BAR.md:77-227).
- Smoothness correction (2026-10-25): repaint requests follow Qt swap callbacks instead of the 16 ms input timer;
  cumulative sampling preserves the canonical ease at 60/120/240 Hz. Matched navigation at 1920x1000 improved
  from 62.58 to 236.96 swap callbacks/s (p95 gap 17.37 to 4.20 ms). These are callback measurements;
  physical presentation and sustained workloads remain open. See `audit/ANIMATION-SMOOTHNESS.md` for evidence,
  diagnostics and remaining limits. The rebuilt normal app is open; the per-minute tick inside vshcommon 0x33FB58 remains next.
- Done 2026-10-25: the menu ease is paf's own. The cross bar is vsh.elf's `PhXmBar`; its focus moves are 200 ms
  paf animations in acceleration mode 5, a per-frame ease with k = 0.2206 that lands on frame 12
  (audit/PIPELINE.md "Menu motion"; `View::PafEase`).
- Done 2026-10-25, measured on the real XMB in RPCS3: the held-key repeat is 300 ms to the first repeat, then every
  4 frames (66.7 ms), with no acceleration. The help text has no delay: it is drawn on the selected row from the frame
  the move starts. The category move fits the same 200 ms mode-5 ease. All three are implemented
  (audit/PIPELINE.md "Measured on the real XMB").
- Implemented 2026-10-25, still a fit rather than a trace: the page move is slower than the others. Captured in RPCS3,
  it fits paf mode 4 at 300 ms; `PafEase` has mode 4 and the depth move uses it. The only captured 300 ms bar call
  passes mode 5, and the explore_plugin 300.0f constants are content/dialog durations, so neither can confirm it.
- Open: trace the page move's animate call in vsh.elf paf PhXmBar (not explore_plugin) to confirm 300 ms and mode 4.
  The clock writer chain is bounded to vshcommon 0x33FB58 (audit/INFO-BAR.md); its per-minute tick is still open.

### Phase 5: remaining firmware renderers and states
- Particle system analysed: [PARTICLES.md](audit/PARTICLES.md). The firmware runs SPU particles.elf over a 1536-record
  pool (1492 live at 7.979 spawns/frame, 3.116 s mean life) with 48-byte records (pos float4 carrying life alpha, quad
  corner, orientation quaternion) and physics entirely from PARTICLES.mnu (cone 51.87 deg, speed 0.287-0.394, aging
  0.00285223, DELTA TIME 0.0088883 s, friction 0.030551, gravity -6.8e-5, Brownian 0.225311, spin 2.74), drawn by two
  shader passes: quads into the HDR scene, radial halo into the glare source. Updated 2026-10-26: pass 1 and a glare
  draw are ported (fixed 0.0088883 s step, 1536 records, 48-byte format, HDR-scene placement, deterministic from
  `--render-clock`); the folded in-shader halo still stands in for the separate `particles_second` program, and
  `proc_iridescent.tga` plus the untraced colour/alpha terms remain open.
- Ribbon geometry from spline.elf is done. Next: the particles' `PARTICLES_UI` wind response, which is not wired.
  The same tools apply: `work/spu/` disassembler, the RPCS3 SPU DMA log, buffer watch and local-store snapshots
  (`rpcs3-rsx-capture.patch`).
- Ribbon presets: the measured boot durations (3.82 s, 2.8 s) differ from the plugin's call arguments (3 s, 7.5 s);
  qgl_base's animation player is not traced.
- Boot: the cold-boot intro is ported (`src/coldboot.cpp`, audit/PIPELINE.md "Cold boot"): coldboot.raf decoded
  (RAF container, Edge plane, GTF textures, coldboot.jsx VSMX), drawn with raf.qrc vert0/sce01, the script's timeline,
  coldboot_stereo.ac3 and anim_coldboot2's events (BootBG2 0 s, NormalBG2 4 s, ShowGUI 5.5 s, Finished 10 s), checked
  against an RPCS3 capture. The logo is the RAF's PS symbol + "PS3" + "PlayStation 3"; the RCO's tex_ps3logo/tex_scelogo
  stay at alpha 0. Open: the epilepsy notice after ShowGUI, the SD actors, the boot brightness curve of RPCS3's
  LinesController/pre-exposure path, and the "gameboot" sequence when launching.
- Theme system: other colour themes, wallpaper mode as the firmware composites it. `.p3t` themes: still background,
  icons and menu sounds are in (README "PS3 themes", audit/MAXMB-INTEGRATION.md). Sounds use MaXMB's five-role
  mapping and per-role system fallback. Still open: dynamic RAF scenes and the `font` / `color` selections.
- Screen states: screensaver/dim, Power off, user login screen, notifications (tex_notification_*), the in-game
  XMB (xmb_ingame). Updated 2026-10-27: the notification composite is implemented (plaque and glyphs from
  `system_plugin`, bounded FIFO queue, 200 ms mode-5 slide, two honest producers - audit/NOTIFICATIONS.md); its
  sound, overlap policy and per-type suppression stay open, and the in-game XMB itself is still absent (the PS
  watcher only brings the XMB forward and opens the system menu).

### Phase 6: content and features
- Remaining XMBML categories and items that are placeholders ("Unavailable"), driven by the firmware XML.
- Media playback UI (photo/music/video controls from photoviewer/videoplayer/audioplayer RCOs).
- Folder playback queues, repeat modes, shuffle, bounded failed-file skipping and photo zoom/pan/reset are now
  implemented and observed in local replays (audit/MEDIA-CONTINUATION.md). Original player panels and broader
  library/playlist semantics remain open.
- PSN-dependent indicators stay off unless a real state exists; don't fake online features.

### Phase 7: verification harness
- Latest: `audit/VERIFICATION-HARNESS.md`. `work/ab-scenarios.ts` adds isolated named captures, optional region
  gates, hashes, state checks and failure reports. Game/Settings/Game Settings independently repeated pixel-exactly;
  this verifies repeatability, not firmware fidelity. Scripted arrow taps now release keys after the repeat change.
- `--ui-clock` pins the displayed clock and `--focus-time` pins label glow/idle fade; combine with the existing
  background/render clocks. PNG scoring now handles RGB/grayscale packed rows correctly and rejects invalid crops.
- Automated A/B: render named states with `--script` and compare them with frames extracted from the reference
  (PSNR/SSIM on crops). Add a threshold per layer so regressions are visible.
- **Tooling landed (2026-10-25):** `--render-clock <seconds>` freezes the background/ribbon/particles/icon animation
  time (so two scripted runs are bit-near identical, PSNR ~81 dB run-to-run), and `bun work/ab-compare.ts <ref.png>
  <native.png> [crop x,y,w,h]` prints PSNR/SSIM (whole frame or a crop). `bun work/ab-shot.ts <ref.png> <tokens> [--crop
  x,y,w,h] [--flash dir] [--exe path] [--bg-clock ISO] [--render-clock sec]` runs the native to a named state and scores
  it in one command. First settled-root baseline against a live RPCS3 capture at the matched day clock: whole frame
  **18.20 dB / 0.8388 SSIM**, category-icon row **20.13 dB / 0.9022**, top background **19.03 dB / 0.9248**. These are
  starting numbers, not thresholds yet.
- A second reference: capture the same states from RPCS3 running the real XMB (same firmware) at 1080p. This is a
  pixel-exact target with no video compression.
- Include a clip resting on one row for 8 s or more: it settles the breathing label glow and idle label fade, which
  are taken from MaXMB and not yet confirmed on our references (audit/TEXT-RENDERER.md).
- Recommended as the next phase: every other phase then gets a pass/fail number.

## Useful entry points

- `src/view.cpp`: scene, layout, UI layer (MSAA FBO), background/ribbon/HDR/particles orchestration.
- `src/shell.cpp`, `src/settings_menu.cpp`: XMB model, pages, actions. `src/host.cpp`: real-PC settings.
- `src/rsx_program.cpp`: RSX→GLSL translator for firmware Cg binaries (`--dump-cg`, `--decode-fp`).
- `src/assets.cpp`: RCO/QRC/GIM/DDS/TGA decoding, `bestImage`, fonts.
- Local analysis (not committed): `out/native-reference/` holds the decompiles (`lines/*.c`, `icons_vtable.c`), the
  translated shaders (`cg-icons/`), `qrc-inventory.txt` and `icons/` (ICONS.mnu files).
