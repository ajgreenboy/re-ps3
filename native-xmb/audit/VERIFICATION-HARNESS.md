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

## Continuation

1. Assemble matching reference states and provenance using permitted saved/user-provided evidence. Do not use
   the RPCS3 input/capture workaround in the older handoff from this Codex session.
2. Use the clock-band crop to investigate info-bar geometry, clock icon and update behavior; its mismatch is
   substantially larger than the shared category-column crop. Keep font/phase differences separate.
3. Continue `FONT-MATCHING.md` for overlap coverage, subpixel placement, PAF metrics and weight effects.
4. Trace the page move's actual 300 ms/mode-4 call, then particles and RCO-driven layout as routed by the main handoff.
5. Set fidelity/regression thresholds only for matched states; do not turn these exploratory scores into pass gates.
