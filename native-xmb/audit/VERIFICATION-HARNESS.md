# Phase 7 — repeatable native captures and layer comparisons

Updated 2026-10-25. The capture/scoring foundation is implemented and exercised. Firmware presentation parity remains open.

## What changed

- `work/ab-compare.ts` now decodes packed PNG rows before expanding them to RGBA. The old expansion overwrote
  the packed previous row and used the wrong destination stride for RGB/grayscale screenshots. CRC, chunk,
  decompressed-length, filter and crop checks reject invalid inputs instead of producing misleading scores.
- RGB, RGBA, grayscale and gray-alpha 8-bit non-interlaced PNGs support all five row filters. Metrics use RGB;
  SSIM uses non-overlapping 8x8 luma tiles and excludes incomplete edge tiles. Its crop must be at least 8x8.
- `work/ab-scenarios.ts` runs named states with isolated data, per-run output directories, timeouts, logs,
  reference/executable/manifest SHA-256 hashes, effective inputs and per-region scores. References stay under
  the caller's reference directory; no reference or threshold is automatically created or updated.
- `work/ab-shot.ts` keeps the one-shot interface, adds validated crops, clock/focus controls and a timeout,
  and derives its default paths from this checkout rather than a hardcoded drive.
- Native capture controls now include `--ui-clock <ISO>` (displayed clock only) and `--focus-time <ms>`
  (focused-label glow/idle-fade age only). Neither writes settings or changes the host clock.
- Scripted keys now send both press and release. The new held-key repeat path previously left every scripted
  arrow held: one LEFT from Game reached Users. It now reaches TV/Video Services exactly once, even after waits.
  Burst-triggered keys use the same tap helper. Physical held-key repeat behavior is unchanged.
- Each `grab=<file.png>` also writes `<file.png>.json`: category ID/index, depth, selection, dimensions and clock.
  Optional scenario expectations reject the wrong category/page/selection before comparing image pixels.

## Running named states

From the repository root, with Bun available:

```powershell
$repo = (Get-Location).Path
bun work/ab-scenarios.ts work/ab-scenarios.example.json `
  --exe (Join-Path $repo 'out/native-xmb/re-ps3-native.exe') `
  --flash (Join-Path (Split-Path $repo -Parent) 'rpcs3/bin/dev_flash') `
  --references (Join-Path $repo 'out/native-reference/named-states') `
  --output (Join-Path $repo 'out/native-reference/scenario-results')
```

The example deliberately contains placeholder reference filenames and no fidelity thresholds. Supply matching
reference images and edit its dates/times for their metadata. Its Settings state uses five LEFT taps from the
current default Game category. Expected category/depth guards catch navigation drift.

Each invocation creates `run-*` under output, including native images, isolated settings, state sidecars,
stdout/stderr/Qt logs and `ab-scenarios-report.json`. The latest report is also copied to output's root.
Reusing an output directory cannot reuse an earlier PNG or earlier scenario preferences.

Regions accept optional `crop: [x,y,width,height]`, `minPsnr` and `minSsim`. A region without thresholds is
**unscored**, not a fidelity pass. Exit 0 means captures/declared gates succeeded; exit 1 is a failed declared
threshold; exit 2 is an input/runtime/state/reference/dimension error. A missing reference skips its launch.
The runner retains failures and continues to the remaining scenarios. Timeout kills only the spawned capture.

For a comparable settled menu, pin background date/time, render animation time, UI date/time and focus age.
Use enough wait tokens for navigation to finish. `full` obtains the monitor's actual fullscreen framebuffer;
`size=1920x1080` can be trimmed by Windows to a 1920x1061 client. Dimension mismatch is reported, never rescaled.
The overrides do not freeze OSK cursor blinking, media playback, service results or every menu animation.
State, profile/content, font, theme, viewport and reference provenance must also agree before judging fidelity.

## Observed verification

All artifacts below are local under `out/native-reference/phase7/`, not committed reference assets.

| Probe | Observed result |
|---|---|
| Known 16x16 pixel images: 4 PNG colour types × 5 filters | Before fix: 15/20 wrong. After fix: 20/20 byte-exact |
| Equivalent RGB/RGBA input | PSNR Infinity; SSIM 1 |
| Invalid out-of-frame crop | Rejected, exit 2 |
| Independent Game captures, 960x540 | Pixel-identical: PSNR Infinity; SSIM 1 |
| Independent Settings captures, 960x540 | Pixel-identical: PSNR Infinity; SSIM 1 |
| Independent Game Settings page captures, 960x540 | Pixel-identical: PSNR Infinity; SSIM 1 |
| Native self-repeatability thresholds | PSNR ≥60 dB and SSIM ≥.999 passed for all three states |
| Deliberately different same-size reference | Threshold failure retained; exit 1 |
| Expected Settings, actual Game | State mismatch retained; exit 2; no pixel score |
| Missing reference | No launch; exit 2 |
| Forced 1 ms timeout | Spawned capture terminated; exit 2; prior live preview remained running |
| One-shot capture with all clocks pinned | PSNR Infinity; SSIM 1; exit 0 |

The pixel fixtures and native self-comparisons verify tooling and repeatability, not a match to firmware.
The isolated and normal builds succeeded; Computer Use confirmed the reopened normal window showing live
18:47 time rather than the diagnostic 12:32 override. Builds still emit existing deprecation warnings; capture logs include existing
OpenGL texture destruction/context warnings on exit. These results do not establish long-session stability.

## Stored firmware observation

The existing offline `ab-rpcs3-root.png` was used without capturing or controlling RPCS3. Its earlier saved
native pair still scores 18.20 dB / .8388 after the decoder correction (both are RGBA, unaffected by that bug).
A new deterministic native capture produced these diagnostic scores, with no fidelity thresholds:

| Region | Crop | PSNR dB | SSIM |
|---|---|---:|---:|
| Whole frame | full | 17.7806 | .8207 |
| Shared category columns | 470,220,810,180 | 21.2169 | .9449 |
| Top background | 0,0,450,200 | 21.8443 | .9684 |
| Clock band | 1300,60,620,100 | 14.9205 | .5296 |

These are not an improvement/regression comparison with the old pair. Firmware shows Corrupted Data while
native shows PS Vita System Application Utility, and the ribbon phase is unmatched. The shared-column crop
avoids the differing left-side category sequence. Reference UI clock is 25/10 12:32; the background override
uses the previously traced daytime mapping at 10:00. Full-frame state/content matching remains outstanding.

## Per-layer thresholds (2026-10-25)

`work/ab-layers.ts` gates six named regions against the stored RPCS3 frame using the threshold table in
`work/ab-layers.thresholds.json`. The default invocation scores the stored baseline pair; `--capture` re-renders
the native with every clock pinned and gates that fresh frame instead.

```powershell
# score the stored baseline pair (deterministic; reproduces the 18.20 dB baseline)
bun work/ab-layers.ts

# regression check: render the current build, then gate the fresh frame
bun work/ab-layers.ts --capture

# machine-readable report for tooling
bun work/ab-layers.ts --capture --json
```

Exit 0 means every layer is within budget, 1 means a layer left its budget, and 2 is an input/runtime error
(missing file, dimension mismatch, capture failure). `--exe`, `--flash`, `--steam-root`, the three clock options,
`--focus-time`, `--tokens` and `--timeout-ms` override the canonical capture recipe stored in the thresholds file;
the default exe is the phase-2 build at `C:/dev/out/native-phase2/re-ps3-native.exe`.

### Threshold rationale

Baselines are measured values, not goals; each gate is `measured >= baseline - budget` with a global budget of
0.5 dB PSNR and 0.004 SSIM. Two canonical captures are pixel-identical (PSNR Infinity, SSIM 1.0000), so the budget
does not need to cover run-to-run jitter. 0.5 dB PSNR is a 12% MSE increase and 0.004 SSIM is a comparable visible
change, while a moved icon, a dropped label row or a shifted gradient moves a layer by more than 1 dB. Stored-pair
scoring is deterministic, so its gate mainly catches decoder, metric or crop regressions. These are regression
gates on an unmatched state, not firmware-fidelity passes: the item-label and clock baselines are low because the
native content and old info bar still differ from the reference frame.

### Measured baseline

Stored pair = `out/native-reference/ab-rpcs3-root.png` vs `ab-native-root.png`; canonical capture = current build
with pinned clocks and an empty Steam root.

| Layer | Crop (x,y,w,h) | Stored pair PSNR / SSIM | Canonical capture PSNR / SSIM |
|---|---|---:|---:|
| Whole frame | 0,0,1920,1080 | 18.20 / 0.8388 | 18.00 / 0.8278 |
| Top background | 0,0,700,200 | 19.02 / 0.9251 | 20.25 / 0.9364 |
| Category icon row | 0,240,1920,120 | 20.13 / 0.9022 | 21.08 / 0.8982 |
| Item label row | 400,425,800,100 | 11.86 / 0.3791 | 11.85 / 0.3774 |
| Clock text | 1570,80,260,50 | 10.33 / 0.3504 | 13.51 / 0.6131 |
| Info bar | 1329,75,591,58 | 13.40 / 0.4973 | 17.12 / 0.7046 |

The capture recipe pins `--render-clock 30 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00
--focus-time 2000`, uses the `full,wait,wait,wait` script tokens and points `XMB_STEAM_ROOT` at an empty directory
so the Game column keeps the reference's pre-Steam dev_flash content. The stored-pair row reproduces the earlier
whole-frame 18.20 dB / 0.8388 and category-row 20.13 dB / 0.9022 baselines; the older top-background 19.03 dB /
0.9248 used an unrecorded crop and is re-measured here as 19.02 / 0.9251.

## Continuation

1. Assemble matching reference states and provenance using permitted saved/user-provided evidence. Do not use
   the RPCS3 input/capture workaround in the older handoff from this Codex session.
2. Use the clock-band crop to investigate info-bar geometry, clock icon and update behavior; its mismatch is
   substantially larger than the shared category-column crop. Keep font/phase differences separate.
3. Continue `FONT-MATCHING.md` for overlap coverage, subpixel placement, PAF metrics and weight effects.
4. Trace the page move's actual 300 ms/mode-4 call, then particles and RCO-driven layout as routed by the main handoff.
5. `work/ab-layers.ts` now enforces per-layer regression budgets against the stored RPCS3 frame. It does not
   establish firmware fidelity while states are unmatched: replace each baseline deliberately when a matched
   reference state and a corrected layer are captured, and do not read a pass as visual parity.
