# Ribbon A2 remeasurement — 2026-11-10

**Partial fix, not closure of R2/R3.** Base `f45bc2d`. Only the ribbon composite's emissive
gain changes (1 -> 0.7). No background/HDR, particle, icon, layout or typography constants change.
The gain is a **fit**, not a recovered firmware constant. R4 improves but is not closed at every column.

## Reproduction and measurement

Console: `C:/dev/out/rpcs3-allgraphics-2026-10-28/frames/col-game-r00.png`.
Native: the `FIDELITY-ROOT-BAR` section 0 recipe, with an empty `XMB_STEAM_ROOT` so installed
games do not change the row content. Scratch captures: `out/ribbon-a2/`.
Two paths are explicitly distinguished:

- Bare recipe: no modules; the 14,400-step fallback.
- Firmware tables: additionally `--vsh-modules C:/dev/xmb/fw493/vsh_module`, as the current
  regression harness already does. It does **not** use the fallback warm-up.

The old audit's numbers are not HEAD's numbers. Its maximum-of-column metric also includes
particle glints: the console raw maximum at x300 is 223.50 at y371, not ribbon-only energy.
To reduce isolated specks, the tables below use the median of nine horizontal pixels at each
y, then the maximum over y330..859 (Rec.709 luma). Edge samples clamp x to the image.
This is still a frame-space proxy, **not an isolated ribbon measurement**: large soft particles
remain, particularly the native table-backed x0 peak. Maxima can switch between folds when
gain changes; a changed peak y does not itself prove a geometry change.

## Crest y: console / HEAD -> reduced-energy build

| x | Console | Bare fallback | Firmware tables |
|---:|---:|---:|---:|
| 0 | 555 | 473 -> 472 | 530 -> 530 |
| 300 | 371 | 576 -> 576 | 512 -> 512 |
| 400 | 361 | 444 -> 444 | 527 -> 527 |
| 1520 | 635 | 429 -> 429 | 719 -> 719 |
| 1700 | 648 | 443 -> 443 | 719 -> 719 |
| 1900 | 653 | 439 -> 439 | 720 -> 720 |

HEAD bare y deltas: -82, +205, +83, -206, -205, -214 px. The fallback is still inverted.
The table-backed path is not inverted in the same way, but it is a different instant from this
reference. Neither path meets the requested few-pixel bound. No phase guess is shipped.

## Peak luma: console / HEAD -> reduced-energy build

| x | Console | Bare fallback | Firmware tables |
|---:|---:|---:|---:|
| 0 | 114.86 | 137.72 -> 111.58 | 175.19 -> 171.19 |
| 90 | 112.79 | 134.50 -> 109.58 | 125.79 -> 102.86 |
| 200 | 98.50 | 100.65 -> 84.72 | 133.00 -> 108.79 |
| 300 | 102.43 | 113.86 -> 93.93 | 125.79 -> 102.86 |
| 400 | 104.43 | 128.72 -> 106.58 | 111.79 -> 93.79 |
| 1520 | 105.07 | 162.16 -> 135.16 | 147.36 -> 117.14 |
| 1700 | 106.36 | 100.58 -> 86.36 | 160.36 -> 125.43 |
| 1900 | 118.07 | 132.93 -> 124.64 | 152.21 -> 120.29 |

HEAD bare luma deltas at the original R4 columns (0/90/200/400/1520/1700):
+22.86, +21.71, +2.15, +24.29, +57.09, -5.78. The older +30..105 claim is stale.
The gain acts on the sum immediately before the BACKGROUND.mnu colour3/4 gradient, before HDR.
It scales all ribbon emission, rather than changing the shared exposure or only `_Brightness`
(which leaves the Fresnel term unscaled). The particle emitter and energy are untouched.

## Phase provenance and rejected probes

`LINE1.mnu` day supplies STATE=1, BRIGHTNESS=.701917, TIMESTEP=2, etc.; it does not identify
the live reference still's cloth state. Existing `PIPELINE.md` "Ribbon extent" traces the
preset-1 positions at plugin VA 0x97c18 and velocities at 0x98d04. The current harness already
passes the local decrypted module to the runtime reader.

Rechecked the local decompilation `out/native-reference/lines/custom_render_all.c`:
`FUN_0004e624` calls `FUN_0004b6a4(DAT_00092da8)` before loading preset 1;
`FUN_0004b6a4` writes cloth+0x2c0. The local ELF float there is 98.6201019.
`FUN_0004f814` multiplies that clock by 10 for FFD, and `FUN_0004bed0` wraps it to zero
above 10. Testing that initializer on the pinned table state did not match this still
(six-column RMS peak-y error 170.7 px). It is not silently substituted for the reference's phase.

Also tested FFD clocks 0..7.5 at .5 intervals, and 0..30,000 additional cloth steps from
preset 1 in 1,000-step intervals. Best sampled six-column error was 50.1 px at 15,000 steps;
its y values were 555/356/450/606/598/593. This is **not** a valid few-pixel fit, and would
also move the particle birth sheet. Temporary probe hooks were removed. No camera, start,
warm-up, normal or screen-space warp is committed to hide that failure.

## Whole-frame scores against col-game-r00

| Path | HEAD PSNR / SSIM | Reduced energy PSNR / SSIM |
|---|---:|---:|
| Bare recipe | 19.73 / .8490 | **20.90 / .8598** |
| Firmware tables | 19.34 / .8480 | **20.27 / .8587** |

These include content differences (row set, clock string, TV category), unchanged by this patch.
The lower-energy trial 0.4 scored 20.92/.8713 table-backed but dimmed median ribbon peaks
to ~76..96 in several columns; 0.7 is the less aggressive energy-only correction.

## Verification

Build, named-state gate and parity-root results are recorded below after the final run.
