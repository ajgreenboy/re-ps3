# Animation smoothness handoff — 2026-10-25

The reported loss of smoothness had two native causes: repaint requests were limited by a 16 ms timer on a display reporting 239 Hz, and fractional host ticks changed the easing curve. Both are corrected. The normal build is rebuilt and open for interactive use.

## Implementation

- `View` requests its next repaint from Qt's queued `frameSwapped` signal. Swap interval 1 remains enabled. The 16 ms timer now polls input only.
- `PafEase` samples the canonical 60 Hz recurrence at total elapsed time, interpolating adjacent canonical samples. It no longer compounds fractional host ticks. Integer firmware-frame samples, 200 ms mode-5 moves, 300 ms mode-4 page moves and retargeting from the current value are retained. Mode 4's selection remains a reference fit, not a traced call.
- Frame time uses the elapsed timer's nanoseconds. The existing 100 ms animation catch-up cap remains; long stalls and blocking content loads need separate work.
- `XMB_FRAME_TRACE` enables a bounded 20,000-event memory buffer, written at orderly shutdown. It records paint CPU dispatch time, movement/rebuild flags and swap callbacks. Normal use does not enable it.

## Measured navigation cadence

Same machine, Green theme/preferences, visible native windows, requested viewports verified by capture metadata. Each replay resized, waited, then repeated RIGHT/LEFT/DOWN/UP six times before the final screenshot and quit. Both versions were profiled. Reports below use only timestamps **6000–10000 ms**, excluding startup and final PNG compression.

| Viewport | Version | Swap callbacks/s | Swap gap p95 | Moving paint gap p95 | Moving CPU dispatch p95 |
|---|---|---:|---:|---:|---:|
| 960×540 | Baseline | 62.52 | 17.68 ms | 22.15 ms | 3.95 ms |
| 960×540 | Corrected | 236.96 | 4.20 ms | 4.57 ms | 2.58 ms |
| 1920×1000 | Baseline | 62.58 | 17.37 ms | 21.95 ms | 4.17 ms |
| 1920×1000 | Corrected | 236.96 | 4.20 ms | 4.55 ms | 2.80 ms |

These are Qt callback/CPU measurements, not physical presentation timestamps or GPU completion. At 1920×1000 the corrected window's largest swap gap was 10.71 ms; no swap gap exceeded 25 ms. This short navigation workload does not establish sustained media/resize/controller performance or close ISC-9.

The corrected profiling build temporarily omitted the prior 100 ms catch-up cap; the final build restores it. Measured navigation intervals were below that cap. Do not claim improved long-stall recovery from these results. The reopened final build's ordinary idle log reports approximately 240 paint frames/s.

## Easing and visual guards

The native `--motion-report` invokes the actual `View::PafEase` at 60/120/240 Hz. Previously, a mode-5 move at 100 ms produced respectively **0.7758198506 / 0.7539896043 / 0.7437066212**. Corrected sampling produces **0.7758198506** at every rate. All sampled positions for both modes agree within 1e-12; interrupted retargeting preserves the displayed value and a subsequent long advance reaches its target.

Existing isolated Game, Settings and Game Settings page scenarios remain pixel-identical to their settled self-references (PSNR Infinity, SSIM 1). This guards settled rendering, not firmware motion parity. Isolated and normal Release builds succeeded. The normal window was restored to PlayStation Network / Sign In after profiling.

## Reproduce

Run from the repository root, substituting local paths:

```powershell
$env:XMB_FRAME_TRACE = Join-Path $PWD 'out/frame.trace'
& .\out\native-xmb\re-ps3-native.exe --dev-flash <local-dev_flash> --no-intro
# Navigate, then exit normally to flush the trace.
Remove-Item Env:XMB_FRAME_TRACE
bun work/frame-report.ts out/frame.trace --warmup-ms 6000 --until-ms 10000 --output out/frame-report.json
& .\out\native-xmb\re-ps3-native.exe --dev-flash <local-dev_flash> --motion-report out/motion.json
```

The report rejects empty/unrecognized input, backward timestamps and invalid options with exit 2. Warmup and optional end bounds filter each stream independently. Paint intervals marked moving have at least one endpoint with a running menu ease. CPU dispatch includes submission overhead and excludes later GPU completion.

## Local evidence and continuation

Evidence is local under `out/native-reference/smoothness/`: `baseline-*-window.json`, `after-*-window.json`, corresponding `.trace` files, `motion-before.json`, `motion-after.json`, `default-build.log`, `live.log`, and `settled-guards/ab-scenarios-report.json`. Initial `before-*` probes had malformed resize tokens and are excluded. The unbounded reports include the final screenshot-saving pause; use the bounded `*-window.json` reports above.

Do not commit captures, firmware or generated outputs. Preserve the other session's existing `src/icons.cpp` diagnostics. Next fidelity work remains the info-bar/clock geometry and clock icon; it was deferred for this user-reported regression. Physical presentation pacing, minimized/occluded rendering cost, long blocking operations and a sustained interactive run remain open.
