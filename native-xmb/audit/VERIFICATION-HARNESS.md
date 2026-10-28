# Phase 7 — repeatable native captures and layer comparisons

Updated 2026-10-26. The capture/scoring foundation is implemented and exercised, including named Steam/XMB states
and per-layer regression gates. Firmware presentation parity remains open.

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
Updated 2026-10-26: the stored RPCS3 frame is a DD/MM, 24-hour console, while the native clock now defaults
to MM/DD and 12-hour (`Preferences::defaultValue` returns 1 for both `clock/dateFormat` and
`clock/hourFormat`; `clockDate()` / `clockTime()` in `src/settings_menu.cpp:19-21` format them); the clock band
is therefore a content difference by construction. Setting the preferences back to DD/MM and 24-hour reproduces
the reference string (INFO-BAR.md:16-25, :68-71) and scores 13.61 / 0.6226 on clock text and 17.21 / 0.7114 on
the info bar, against the 11.39 / 0.4105 and 14.78 / 0.6117 baselines the American default is gated on.

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
| Whole frame | 0,0,1920,1080 | 18.20 / 0.8388 | 18.02 / 0.8301 |
| Top background | 0,0,700,200 | 19.02 / 0.9251 | 20.24 / 0.9344 |
| Category icon row | 0,240,1920,120 | 20.13 / 0.9022 | 21.05 / 0.8950 |
| Item label row | 400,425,800,100 | 11.86 / 0.3791 | 11.94 / 0.3910 |
| Clock text | 1570,80,260,50 | 10.33 / 0.3504 | 11.39 / 0.4105 |
| Info bar | 1329,75,591,58 | 13.40 / 0.4973 | 14.78 / 0.6117 |

The canonical column was re-measured three times on 2026-10-26, and the last one supersedes the earlier numbers.
After the particle layer was toned down (fewer specks, matched to the reference), frame, top background, category
icon row and item label row moved while clock and info bar did not (13.51 / 0.6131 and 17.12 / 0.7046). That was
true of the particle pass only: the sub-pixel glyph-phase pass then read 13.66 / 0.6198 and 17.26 / 0.7098, and the
American clock default (MM/DD, 12-hour) finally moved the band for real - the pinned `12:32` now renders
`10/25 12:32 PM`, so the table above carries the current baselines **11.39 / 0.4105** and **14.78 / 0.6117**
(`work/ab-layers.thresholds.json` revisionNote items 1 and 3). With `clock/dateFormat` and `clock/hourFormat` set
back to DD/MM and 24-hour the reference string is reproduced and the same two layers score 13.61 / 0.6226 and
17.21 / 0.7114.

The capture recipe pins `--render-clock 30 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00
--focus-time 2000`, uses the `full,wait,wait,wait` script tokens and points `XMB_STEAM_ROOT` at an empty directory
so the Game column keeps the reference's pre-Steam dev_flash content. The stored-pair row reproduces the earlier
whole-frame 18.20 dB / 0.8388 and category-row 20.13 dB / 0.9022 baselines; the older top-background 19.03 dB /
0.9248 used an unrecorded crop and is re-measured here as 19.02 / 0.9251.

## Named Steam/XMB states (2026-10-26)

The Steam pages have no RPCS3 capture, so each named state's reference is the deterministic native render of that
same state. These are self-baseline regression gates: they catch drawing, layout and content changes, and they say
nothing about firmware fidelity. `bun work/ab-layers.ts --list-states` prints exactly the four recipes below
(`steam-game-root`, `steam-all-games`, `steam-store-sections`, `steam-not-installed`), and `--all` runs the stored
pair, the canonical capture and all four in one command.

Two deliberate changes on 2026-10-26 forced a refresh. The Game column was reshaped to Steam Store plus a single
All Games folder: the inline installed rows and the top-level Not Installed row are gone, and Not Installed is now
reached through All Games. The particle layer was also toned down to fewer specks to match the reference frame.
The recipes below describe the new shape; the references were regenerated from it with
`--states --save-reference` and the canonical-capture baselines in the table above were re-measured on the same
build.

```powershell
# every gate in one command: stored pair + canonical capture + all named states
bun work/ab-layers.ts --all

# only the named states, or one of them
bun work/ab-layers.ts --states
bun work/ab-layers.ts --state steam-all-games

# recipes and machine-readable reports
bun work/ab-layers.ts --list-states
bun work/ab-layers.ts --states --json

# deliberately refresh the references after an intended rendering or layout change
bun work/ab-layers.ts --states --save-reference
```

Exit 0 = all gates pass, 1 = a metric gate failed, 2 = a named-state dump assertion failed (the capture shows a
different state than the recipe expects) or an input/runtime error.

### Recipes and assertions

One discovery capture of the Game column root reads the `XMB_DUMP_ENTRIES` catalogue dump to find the root row
indices. A multi-stage recipe then opens its parent page and reads that page's `<dump>.folder.json` to resolve the
child row, so the scripted navigation follows the rebuilt column instead of fixed offsets. Every recipe copies its
data directory from `out/native-reference/store/ui3-data` (preferences, profiles, settings, `family-shared.json`,
cached Steam headers, artwork overrides), pins all clocks, and sets `XMB_STEAM_ARTWORK=0` so background artwork
fetches cannot change a frame mid-capture. References and their state sidecars live under
`out/native-reference/layers/` (gitignored local reference data, never committed).

| State | Navigation and assertions | Regions |
|---|---|---|
| `steam-game-root` | Game column root, no input. Asserts depth 0 and the exact 11-row column: `steam-store`, `steam-folder-all-games` (detail = owned titles, 282), then `vita`, `data`, `memory`, `ps2`, `psp`, `ps3`, `store`, `content:refresh`, `content:import`. | whole frame; item column 340,380,1080,600 |
| `steam-all-games` | Select `steam-folder-all-games` (root index 1) and press ENTER. Asserts depth 1, `entryId=steam-folder-all-titles`, 14 rows, folder mode `all`, and the ordered row list: All Titles (owned titles, 282), the 11 non-empty collection folders in name order (Apps 6, Canyon Dogshit 1, Favorites 1, Fighting 16, Hidden 41, HTPC 3, Idle/CLicker/Shit 2, Lego 21, Multiplayer 99, Singleplayer 91, Valve 19), Not Installed (owned - installed + familyShared, 275), Family Shared (3). | whole frame; rows 430,420,700,560 |
| `steam-store-sections` | Select `steam-store` (root index 0) and press ENTER. Asserts depth 1, `entryId=store-section:top_sellers`, title Top Sellers, 3 rows, and no `<dump>.store.json` (the sections page must make no store request). | whole frame; sections 440,400,640,320 |
| `steam-not-installed` | All Games (root index 1), then the `Not Installed` row (index 12, resolved from the All Games folder dump) and press ENTER. Asserts depth 2, folder mode `library`, row count = owned - installed + 3 family-shared apps (275), every row `steam:*` with a "Not installed" detail, and the sidecar count equal to the folder count. | whole frame; rows 430,420,640,560 |

### Determinism and measured numbers

Each recipe was captured twice and the frames were byte-identical (PSNR Infinity, SSIM 1.0000), including the
275-row folder page: cached artwork comes from the seed data and asynchronous fetches are disabled. The fixed gate
is `PSNR >= 55 dB and SSIM >= 0.9995` against the stored reference. One changed 255-level pixel already costs about
68 dB on a 1080p frame, so any deterministic drawing change fails while GPU/driver noise has room. A host-level key
event can move the selection while the app has focus, so an assertion-mismatched capture is retried once before the
state is reported as failed; that keeps a single external flake from reading as a regression.

| State | Captured state (observed) | Whole frame PSNR / SSIM | Content region PSNR / SSIM |
|---|---|---:|---:|
| `steam-game-root` | game, depth 0, 11 rows | inf / 1.0000 | inf / 1.0000 |
| `steam-all-games` | game, depth 1, 14 folder rows | inf / 1.0000 | inf / 1.0000 |
| `steam-store-sections` | game, depth 1, Top Sellers, 3 rows | inf / 1.0000 | inf / 1.0000 |
| `steam-not-installed` | game, depth 2, folder 275 rows | inf / 1.0000 | inf / 1.0000 |

Limits: the owned list and the collection names/counts are host data (Steam Web API key plus `family-shared.json`
seed), so a changed account, library or collection set makes the dump assertions fail loudly (exit 2) instead of
scoring a different state; update recipe and reference deliberately then. The store *section content* pages (Top
Sellers list and friends) are not covered because they need a live store request; only the network-free sections
page is. State captures use the feature build named in the thresholds file (`C:/dev/out/native-folders` at the time
of writing); pass `--exe` when the integration build moves.

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
6. The named-state references under `out/native-reference/layers/` are native self-baselines. Add a matched RPCS3
   reference for the Steam pages if one is ever captured, then replace the self-baseline gate with a fidelity gate
   instead of widening the budget.

## RPCS3 timestamped band burst (added 2026-10-27)

A new capture recipe for motion evidence, used by LAYOUT-TRACE phase 4c and PAGE-MOVE phase 4c. It is local
scratch, not part of the committed harness:

- Boot `C:\dev\rpcs3-cap\rpcs3.exe --no-gui --config <scratch yml> --fullscreen <dev_flash>\vsh\module\vsh.self`
  with a scratch config outside the repo and RPCS3 folder: Vulkan, 1920x1080, exclusive fullscreen disabled.
- vsh.self ignores `--input-config`; temporarily replace
  `C:\dev\rpcs3-cap\config\input_configs\global\Default.yml` with a Keyboard handler (PS Button Backspace,
  Cross PgDown, Circle PgUp), back it up first and restore it in the same run, checking SHA-256
  `E624DA0D66A8E971173C39A69A1DC2C89FFE146D57BF2A03C2C8CFACB459CEEE` before and after.
- `burst.ps1` copies a screen band (here 1920x180 at y 200..380) with BitBlt into a 32bpp DIB, writes a
  Stopwatch timestamp per frame and the raw pixels to one file, and fires one key at a scheduled time. The
  2026-10-27 run is 642 frames in 4,588.7 ms (139.7 fps); `raw2png.ps1` converts to `f_<ms>.png`.
- Analyse with `template-track.ps1` (isolates the focused icon) and `pitch.ps1` / `diffpitch.ps1` (icon-row
  pitch by autocorrelation). `work/motion-track.ts` and `work/region-luma.ts` still run on the same frames, but
  the full-band tracker mixes the moving icon, the fading bar and the label, so it reports a compromise track
  (201-223 px instead of 262 px). Use the template track for absolute distances and the project tools for the
  independent start frames and brightness channel.
- Scratch artifacts stay under `C:\dev\out\rpcs3-xmb-cap\`; only the results belong in the audit notes.
