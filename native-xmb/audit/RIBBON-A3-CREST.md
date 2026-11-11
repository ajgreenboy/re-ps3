# Ribbon crest amplitude / asymmetry — 2026-11-11

Base: `023f7e7`, preserving the prior session's unstaged 0.7 emissive gain.
Scope: fallback ribbon geometry only; no background, UI or particle parameters.

**Outcome: no geometry change accepted.** A fit met the approximately 30 px
six-point target but violated both mandatory PSNR floors. This pass commits
the trace and rejected-fit evidence only. Crest refinement remains open.

## Fresh baseline

The root-bar recipe, with an empty Steam directory and a scratch data directory,
reproduces the six prior crest measurements exactly in `out/ribbon-a3/before.png`:
**512 / 462 / 407 / 572 / 605 / 630**, at x **0 / 300 / 400 / 1520 / 1700 / 1900**.
The console is **555 / 371 / 361 / 635 / 648 / 653**.
`work/ribbon-crest.ts` uses Rec.709 and a nine-horizontal-pixel median over
y330..859; this is a frame-space proxy and can switch bright folds or include
broad particles. Band `0,330,1920,450`: **20.14 dB / .7868**;
whole frame: **22.03 dB / .8767** (`work/ab-compare.ts`).

## Trace boundary and asymmetric firmware field

Read the local extracted day `LINE1.mnu`, the translated `ffd_shader1.fpo`,
`lines1.vpo` / `lines1.fpo`, and `custom_render_all.c`. The menu selects
STATE=1 / SHADER=1 and supplies scales/transforms; it does not encode the phase
of this still. `FUN_0004f814` calls `FUN_0004a220` with the cloth clock times 10
(local decompilation line 32252). The line vertex program passes the spline
position through, and the fragment program shades stripes, not crest geometry.

Reducing the 43-instruction FFD program shows that the phase-dependent y field
contains all of:

- a travelling asymmetric ridge `exp(-50*(u-(sin(.1*T)+.2)*.833333)^2)`;
- `.24*sigmoid(2*(u-.5))*(sin(2*T)+3)*exp(-.0001*T)*sin(7.85*u-2.5*T-1.25)`;
- `.5*sin(.25*T)` and `.125*sin(T-6.28*w)`.

Here `u,w` are the input node x/z coordinates and `T` is `_Time`, not wall
seconds. This explains why a symmetric vertical stretch of a settled cloth is
not a trace of the console crest. The shader's x output, by contrast, is the
time-independent `.2*u+1.3*u*u-.15`. The existing port executes these
instructions; they are not replaced with a hand-drawn screen curve. The
console still's live cloth/noise state and clock remain unknown. Any selection
against this still is a **fit**, not a
recovered firmware phase. No firmware bytes or simulated binary states ship.

Trace anchors are the local `out/native-reference/lines/glsl/ffd_shader1.fpo.glsl`
instructions 42..84, `lines1.vpo.glsl:26` (position pass-through),
`lines1.fpo.glsl:32..47` (stripe/Fresnel/encode), and
`custom_render_all.c:31854..31859` (the preset setter's final clock assignment).
The examined `ffd_shader1.fpo` SHA-256 is
`394E279D1C4850BF8CCB36C58EDD74B76274EBEFA71A270DFA4433E16CF6B5FF`;
day `LINE1.mnu` is
`36999384E78F32F6663AE5A58DAC5DEDEDD8046E3B416A6C4E2A5E8DBB02CA8B`.

## Fit search

A scratch full-size GPU sweep held the 41,550-step cloth fixed and sampled the
FFD clock from 0 to 100 in steps of 2, followed by amplitude/offset trials.
Some phases reduced the six-point error but worsened the ribbon-band score;
those were rejected rather than claiming a crest-only win. Subsequent trials
adjusted the asymmetric ridge term locally, leaving the other firmware field
terms and simulation intact. Further searches covered nearby warm-ups
39,550..43,550 every 250 steps, cloth y/z amplitudes, the transverse sine's
phase/amplitude, and local FFD clock/crest-position refinements. None supplied
an acceptable bounded crest improvement above the baseline band score.
The scratch ledger contains **208 trials** (including identity controls and
repeated control settings); **9** met a 30 px maximum error, and **none** of
those cleared 20.14 dB. The two recorded trials at/above that band floor were
identity controls. This is a bounded negative search, not proof that no safe
fit can exist.

The highest-scoring tested geometry fit with all six errors under 30 px
(before extra energy scaling) used:

- the unchanged 41,550-step warm-up;
- an independently fitted FFD clock **T=83.0675** at the pinned capture;
- drawn cloth y/z amplitudes **0.9 / 0.9**;
- `fieldY += .3*(exp(-150*(u-.4)^2)-exp(-50*(u-c(T))^2))`,
  where `c(T)=(sin(.1*T)+.2)*.833333` is the firmware ridge centre.

These are **rejected fit parameters**, not traced firmware constants and not
shipping settings. The probe's rounded, independently set field clock is not
proof of the original clock's exact bits. Fold/particle maxima can switch
even for a tiny phase change; do not treat this proxy as a geometric vertex
measurement.

| x | Console y | Before y | Rejected fit y | Fit delta | Retained after y |
|---:|---:|---:|---:|---:|---:|
| 0 | 555 | 512 | 539 | -16 | 512 |
| 300 | 371 | 462 | 397 | +26 | 462 |
| 400 | 361 | 407 | 390 | +29 | 407 |
| 1520 | 635 | 572 | 652 | +17 | 572 |
| 1700 | 648 | 605 | 661 | +13 | 605 |
| 1900 | 653 | 630 | 638 | -15 | 630 |

The rejected fit's six peak lumas were **102.86 / 92.50 / 100.50 / 98.93 /
97.14 / 88.93**, versus console **114.86 / 102.43 / 104.43 / 105.07 / 106.36 /
118.07**. It did not solve brightness uniformly either. Extra ribbon-only
energy trials at .85/.95/1.05/1.15 relative to the inherited .7 gain did not
recover the band floor; the best was still only 20.06 dB (.95 relative energy,
.7895 band SSIM, 21.98/.8774 frame). Independent scoring of all nine bounded
trials with the exported `work/ab-compare.ts` PSNR/SSIM functions is retained
in `out/ribbon-a3/verified-bounded.json`.

| Measure | Before | Rejected 29 px fit | Retained after |
|---|---|---|---|
| Max six-point y error | 91 px | 29 px | 91 px |
| Ribbon band PSNR / SSIM | 20.14 / .7868 | **20.06 / .7877** | 20.14 / .7868 |
| Frame PSNR / SSIM | 22.03 / .8767 | **21.98 / .8767** | 22.03 / .8767 |

An earlier unscaled-cloth ridge fit reached a 24 px bound (574/359/368/659/670/660)
but scored even worse: **19.90/.7795** band and **21.86/.8731** frame. Matching
only maxima moves the whole sheet incorrectly: a bright fold's location is
not the sheet's upper silhouette. Neither candidate justifies shipping a
regression under the requested floors.

Scratch evidence: `out/ribbon-a3/scores.jsonl`, the corresponding full-size PNGs,
and the sweep logs. The near-target 29 px candidate is
`p83.0675g0.3l0blend-r0.4w150cy0.9cz0.9.png`. Scratch probes and environment
overrides were removed from source; none is part of the committed implementation.

## Retained build and next evidence needed

The required build passed after probe removal:
`Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe`
(`out/ribbon-a3/final-build.log`). Its root-bar capture `out/ribbon-a3/final.png`
is **pixel-identical** to `before.png`: infinite PSNR / 1.0000 SSIM over the
whole frame. All six original points, brightness, sag direction and both
fidelity scores are therefore retained, not improved. The only remaining
source diff is exactly the pre-existing owner's .7 gain/comment hunk.

`XMB_RIBBON_SETTLE_SYNC=1` is pixel-identical to the prefetched retained build.
The optional `--vsh-modules C:/dev/xmb/fw493/vsh_module` capture is also
pixel-identical to the prior pass's `out/ribbon-a2/energy-tables-fresh.png`.
Both comparisons are infinite PSNR / 1.0000 SSIM. No binary or reference
snapshot is included in this commit.

A useful next acquisition is the spline task's cloth lattice/clock/noise state
at the **same console frame** as `col-game-r00.png`, not another boot-state
table or another still's phase. The existing shader decomposition does not
recover that missing live state. Relaxing the PSNR floors would remove one
rejection reason for the near-target fit, but this pass did not assume
permission to do so or claim that this would establish full fidelity.

## Required gate

Ran `bun work/ab-layers.ts --all --exe C:/dev/xmb/out/native-xmb/re-ps3-native.exe`
before refresh and again afterwards. Both completed **21/22 named states
PASS**, or **21/21 excluding steam-download-page**. The sole final failure is
that known live-download-row environmental flake (frame 33.84/.9962, rows
32.22/.9925); it was not chased or re-saved. The command's exit status is 1
because it includes that state, not because a different failure was ignored.

Stored baseline pair: **6/6 layer gates PASS**. Canonical capture: **5 gating
layers PASS + 1 informative content-differs layer**; the clock text-presence
guard also passes (1,681 bright pixels, minimum 900).

Only **steam-all-games** had any measured non-excluded reference movement
(previous reference versus re-save: **92.70 dB / 1.0000**), so only it was
re-saved with `--state steam-all-games --save-reference`. This microscopic
pixel drift is not claimed as a ribbon improvement; the retained root recipe
is pixel-identical. No other reference, threshold, recipe or reference pointer
was edited. References remain local artifacts, not part of this commit.

Logs: `out/ribbon-a3/gate-before.log`, `save-steam-all-games.log`,
`gate-final.log`; the reference selection is recorded in `moved-states.json`.
