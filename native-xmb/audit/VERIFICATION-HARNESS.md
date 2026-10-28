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

## Phase 5 - verification and performance hardening (2026-10-27)

Phase 5.1 gave every shipped surface a named state, 5.3 measured swap intervals from the frame trace,
5.4 measured process start to first frame with the loader breakdown, and 5.5 added a churn soak. All
of the evidence is local under `out/native-reference/layers/` and `out/native-reference/phase5/`; none
of it is committed, and none of it is a firmware capture.

### 5.1 Named states for the surfaces that had none

`bun work/ab-layers.ts --list-states` now prints twelve recipes. The eight added here follow the four
Steam states' contract exactly: a deterministic capture with every animation clock pinned, a state
assertion that fails with exit 2 when the app is somewhere else, one or two scored regions against a
deliberately saved native reference, and `--states --save-reference` as the only way to refresh it.

| state | route from the Game-column root | assertions | regions |
|---|---|---|---|
| `xmb-notification` | none: `NOTIFY=Message received\|From Albert` before the grab | depth 0, `pageKind root`, sidecar `notificationCount 1`, `notificationTitle Message received`, `notificationIcon tex_notification_info`, `notificationRemainingMs > 0`; log `notify: enqueue Message received` | frame; plaque 1480,135,440,110 |
| `xmb-volume-hud` | none: `VOLSET=25` before the grab, `VOLRESTORE` after it | depth 0, `pageKind root`; log `volume: VOLSET request 25% -> Windows reports 25% (muted 0)` | frame; hud 990,65,250,80 |
| `video-options-panel` | Video > Video Library, then `TRIANGLE` | depth 1, `entryTitle Movies`, 3 rows, `pageKind folder`; log `shell: options opened category= video depth= 1 ... rows= 4 Open\|Information\|Sort By\|Close` | frame; panel 1300,430,620,300 |
| `continue-watching` | Video > Continue Watching, from a seeded resume record | depth 1, 1 row, `pageKind continueWatching`, entry id prefix `continue:`, title `Top Level Clip.mp4`; log `continue watching: page opened with 1 row(s)` | frame; row 400,410,760,220 |
| `continue-watching-empty` | Video > Continue Watching, no record | depth 0, `entryId continue`, `pageKind root`; log `continue watching: nothing to continue` | frame; dialog 240,190,1440,700 |
| `subtitle-cue` | Video > Video Library > row 0, pause, then seek left to 0 | 1 row, title `Cue Test.mp4`; sidecar `mediaOpen`, `mediaPaused`, `mediaPosition <= 500`, `subtitleCue 1`, `subtitleTrackCount 1`, file contains `Cue Test.srt`, cue text contains `First cue`; log `subtitle: sidecar Cue Test.srt` | frame; cue band 210,650,1500,200 |
| `jellyfin-signin` | TV/Video Services > the seeded `custom:jellyfin` registration | depth 1, 5 rows, `entryId jellyfin-signin:username`, `pageKind jellyfinSignIn`; sidecar `signInOpen true`, `signInState idle`, `signInError ""`, `focusedRow jellyfin-signin:username`, `modalWidget ""`; log `jellyfin: sign-in page opened ...` | frame; rows 400,300,900,620 |
| `steam-download-page` | Network > Download Management, with the fixture Steam root from the recipe's `env` | depth 1, 4 rows, `entryId download:1001`; the page's own `.dl.json` asserts count 4, empty error and message, and each row's id prefix, title, state, text and detail; log `steam downloads: page opened with 4 row(s)` | frame; rows 400,430,800,520 |

Every one of the eight was captured twice - once with `--state <name> --save-reference`, once with
`--state <name>` against the reference just written - and every region of every state scored
`PSNR infinity / SSIM 1.0000`, including the paused-media state and the page whose row list is built by
a folder scan. The four Steam states were re-run on the same build and also scored infinity/1.0000
against their old references, so this phase changed no existing gate.

Four additions to the runner made those surfaces reachable:

- A stage may name another column (`categoryId`). The move is the column-index difference from the Game
  column the run starts in (the canonical capture already asserts the run starts there), followed by a
  clamp to the new column's first row: `Shell::vertical`/`horizontal` clamp rather than wrap, so a
  fixed number of `UP`s reaches row 0 whatever row the column was left on, and the row index stays
  dump-derived instead of hardcoded.
- A stage may be a zero-based `rowIndex` instead of a dump-resolved row, for pages this build writes no
  dump for (a media library page). The sidecar assertion is the oracle for the row that was hit.
- A recipe may set extra environment for its capture (`env`, with `${repo}` for a fixture path), script
  tokens before the grab (`preGrab`: `TRIANGLE`, `NOTIFY=`, `VOLSET=`), and tokens after it
  (`postGrab`: `VOLRESTORE`).
- `expect` gained `pageKind`, a `sidecar` map (exact value, or `gt`/`ge`/`lt`/`le`/`prefix`/`contains`),
  a `log` list of regexes matched against that capture's own `XMB_LOG` file, and a `downloads` block
  for the Download Management page's `.dl.json` dump. Two of the states needed a log switch in the
  fixture environment to make their own evidence visible: `XMB_NOTIFY_LOG=1` (the enqueue/show lines are
  opt-in) and `XMB_SUBTITLE_LOG=1` (the sidecar discovery line is opt-in).

The discovery capture is now per data seed and environment (`dataSeed` + `env`), because the catalogue
dump depends on both: `apps.json` adds the `custom:` row the Jellyfin state opens, and `XMB_STEAM_ROOT`
decides the Game column and the download page. `--all` therefore runs one discovery capture per distinct
pair (six today) instead of one for everything.

Two timing defences came out of running this on a loaded machine, and both only change *when* a capture
settles, never what it shows, so no reference moved:

- A path-resolution probe that produces no folder dump is retried once before the recipe is reported
  unresolvable (the row a recipe targets only exists after the asynchronous Steam/app content scan has
  rebuilt the column).
- `steam-not-installed` now targets the All Games page's twelfth row by `rowIndex` instead of resolving
  the `Not Installed` title from a probe dump. The recipe still asserts the resulting page (folder mode
  `library`, title `Not Installed`, `owned - installed + familyShared` rows, every row `steam:` with a
  "Not installed" detail), so a different row fails loudly with exit 2 - the index is pinned, the state
  is still verified.

**Open issue found on 2026-10-27 (not fixed - app side, outside this phase's write fence).** From about
22:00 every scripted `DOWN` stopped being applied on the Game-column root: a capture whose only
navigation is `DOWN` leaves `selection` at 0 while its `idleMilliseconds` shows the key was delivered,
`UP`/`ESC`/column moves still work, and a following `ENTER` activates row 0 (`steam-store`). The same
vertical move then started failing intermittently in other columns: at 22:30 a rerun of the eight new
states passed five and failed `subtitle-cue` (its `DOWN` to Video Library was ignored, so `ENTER`
opened Continue Watching), `steam-download-page` (its four `DOWN`s were ignored, landing on `manual`)
and `jellyfin-signin` (the page opened with `Server URL` focused instead of `Username`). All eight had
passed individually with their references saved and re-verified (infinity/1.0000) between 21:40 and
22:10 the same evening, and `--all` was fully green at 21:08
(`out/native-reference/phase5/all-2.log`, 12/12 states, exit 0) with the same executable
(`C:\dev\out\native-phase3\re-ps3-native.exe`, 19:25:42, unchanged), so this is a runtime input/state
regression or a machine-level interference effect rather than a harness change. Until it is understood,
a state run that presses `DOWN` can fail its assertion (exit 2) even though its reference is correct;
the retry only helps when the failure is a one-off.



Fixtures (local, under `out/native-reference/phase5/`, never written by a run - each capture copies its
seed into a temp directory first):

- `media/Cue Test.mp4` and `media/Cue Test.srt`: a copy of `out/native-reference/video-library/Top Level
  Clip.mp4` (20 s test pattern with a burned-in timecode) and a three-cue SRT whose first cue starts at
  00:00:00.000 and ends at 00:00:05.000. The 0 s start is what makes the subtitle state deterministic:
  pausing and then seeking left clamps the position to exactly 0, so the video frame, the `00:00 / 00:20`
  controls and the first cue are identical on every run.
- `subtitle-data/`: preferences pointing `library/video` at `media/`, plus the usual profiles and
  settings files.
- `resume-data/`: the same, plus one `media/resume` record - the shape the build writes itself
  (`{"<sha1-24>":{"at":0,"dur":20000,"ms":2240,"path":"C:/.../Top Level Clip.mp4","title":"Top Level
  Clip.mp4"}}`), with `at` zeroed so the 30-day age filter can never hide it.
- `steam-dl/steamapps/appmanifest_1001..1004.acf`: the four synthetic manifests from the download-page
  work (active 43% download, paused at 50%, queued, pending update), so the page's rows are fixture data
  and not this machine's real Steam queue.
- `pacing-1/`, `pacing-2/`, `startup.json`, `soak.json`, `all-1.log`, `all-2.log`: the measurement
  outputs described below.

What the states do not pin, stated so a pass is not read as more than it is:

- `xmb-volume-hud` pins the level (25%) but not the endpoint's mute state: the HUD prints `Mute` instead
  of `25%` when the default endpoint is muted. The recipe therefore asserts the app's own read-back line
  `... (muted 0)` and fails loudly if the endpoint is muted at capture time rather than scoring a
  different HUD silently. The capture sets the real Windows endpoint for about a second and puts it back
  with `VOLRESTORE`; a killed capture can leave it at 25%.
- The four Steam states still depend on this machine's Steam account (owned list, collections,
  `family-shared.json` seed), and `xmb-notification`/`xmb-volume-hud`/`steam-download-page` still depend
  on the real dev_flash, so those assertions fail loudly (exit 2) instead of scoring a different state.
- The notification's remaining lifetime is asserted as "greater than zero", not as an exact number: the
  plaque is the same once its 200 ms slide has settled, but the counter keeps running.
- Nothing here is a firmware-fidelity claim. The references are deterministic native renders.

### 5.3 Frame pacing at the display's current rate

The paced measurement is the app's own frame trace, not the loop's `native frames:` line. That line
reports frames per second over a 5 s window from the paint loop; the trace records every
`QOpenGLWidget::frameSwapped` callback and every paint with its elapsed nanoseconds, so the swap
intervals can be summarised directly.

```powershell
bun work/perf-harden.ts pacing --run out/native-reference/phase5/pacing-1 --json .../pacing.json
bun work/perf-harden.ts pacing --trace <frames.txt> --log <log> --warmup-ms 2000
```

`pacing --run` launches the app with live animation clocks (no `--render-clock`, no
`--background-clock`, because pinned clocks would remove exactly the background/ribbon/particle work
being measured), a pinned UI clock, and a 32-token script that settles the Game root, opens the Video
Library page, moves inside it, crosses back to the Game column and quits. It filters the first 2 s of
samples (window creation, first shader and atlas use) and reports the swap-interval quantiles. Two runs
were recorded on the phase-5 build:

| run | samples | display | p50 | p95 | p99 | max | > 1.5 periods | > 2.5 periods |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `pacing-1` | 2585 swaps | 239.964 Hz (4.167 ms) | 4.211 ms | 7.136 ms | 8.932 ms | 82.914 ms | 211 | 16 |
| `pacing-2` | 2416 swaps | 239.964 Hz (4.167 ms) | 4.318 ms | 8.077 ms | 11.487 ms | 65.743 ms | 367 | 30 |

Paint CPU per frame from the same traces: p50 3.223 / 3.547 ms, p95 4.953 / 7.037 ms, p99 6.293 /
8.094 ms; the moving-frame subset p95 6.787 / 9.355 ms. A p50 of one display period means the app is
presenting every refresh when it keeps up; the tail is where the misses are.

What a 120 Hz target needs: 8.333 ms per frame. `p95` fits in both runs (7.14 and 8.08 ms) but `p99`
does not (8.93 and 11.49 ms), and the moving-frame paint CPU p95 in the second run (9.36 ms) is already
over the budget on its own. So at 120 Hz this build would drop roughly one frame in a hundred to one in
fifty, concentrated in the frames that rebuild UI while something moves. Getting inside 8.33 ms at p99
needs: (1) the moving-frame paint CPU p99 cut by about 1-2 ms (the per-frame text/atlas and icon work),
(2) the same at p50 for 120 Hz headroom (3.2-3.5 ms today leaves 4.8 ms, which is enough only if the
tail is flat), and (3) the 65-83 ms outliers understood - they are single frames, not a rate problem,
and are most likely first-use compilation or another process on this machine (the two runs differ by a
factor of two in p99 while running the same script, and this machine was carrying three other harness
sessions and several full-screen app runs during the measurements). At 60 Hz (16.67 ms) every sample
except the outliers fits.

Caveats that belong with the numbers: the trace measures swap callbacks with `swapInterval=1`, i.e. the
app's CPU dispatch cadence, not physical panel presentation or GPU completion, and this machine has no
120 Hz or 60 Hz TV output to test against - the display it ran on is 239.964 Hz, so 120 Hz is an
arithmetic requirement here, not a measurement.

### 5.4 Startup budget

```powershell
bun work/perf-harden.ts startup --runs 7 --json out/native-reference/phase5/startup.json
```

Each run launches the app on a fresh copy of the UI seed with `--no-intro`, captures Qt's own
`%{time process}` timestamp on every log line, and sets `XMB_FRAME_TRACE` so the first
`frame swap:` timestamp can be added to the View-construction timestamp. That gives process start to
the first presented frame, and the loader milestones are read from the lines the app already writes.

Seven runs on the phase-5 build (`C:\dev\out\native-phase3\re-ps3-native.exe`):

| milestone | median | min | max |
|---|---:|---:|---:|
| catalog (XMBML settings projection resolved) | 25 ms | 24 ms | 32 ms |
| XMBML Network column resolved | 23 ms | 22 ms | 29 ms |
| text atlas (GL init, FreeType coverage) | 714 ms | 593 ms | 906 ms |
| native renderer ready | 1005 ms | 902 ms | 1221 ms |
| native display refresh (window shown) | 1506 ms | 1356 ms | 1623 ms |
| **first frame** | **1497 ms** | **1354 ms** | **1617 ms** |
| Steam content scan (async, after the first frame) | 1698 ms | 1553 ms | 1817 ms |
| Steam owned library (Web API, async) | 2080 ms | 1943 ms | 2145 ms |

What the budget says: the two firmware loaders are not the cost - the XMBML settings projection and
the Network column together are under 50 ms, about 3% of the first frame. The cost is the GL context
plus the glyph atlas (593-906 ms) and the first window show. The Steam scan and the owned-library fetch
land about 200 ms and 600 ms *after* the first frame, so they do not gate the user-visible start; the
Jellyfin client constructs without a request and logs no startup line, so its whole startup cost is
inside the Shell constructor. Seven runs of the same binary span 1354-1617 ms; on a quiet machine
measured earlier in the same session the same build presented at 1118-1268 ms, so the ~250 ms
difference is this machine's load, not the build. The phase target was "under the current ~1 s"; at
1.35-1.62 s under load and ~1.2 s quiet, this build is not inside 1 s, and the number to attack is the
text atlas / GL init block, not the loaders.

### 5.4a Where the 714 ms actually goes (measured, 2026-10-27)

This section is appended here, next to the numbers it corrects, rather than to `TEXT-RASTER.md`: the
startup budget lives in 5.4 and the text renderer's share of it is only one line of that table.

The 5.4 table's "text atlas" row is a **timestamp, not a duration**. `text atlas:` is printed by
`TextRenderer::init()` on return, so 714 ms is the process time at the end of GL init - it contains the
QApplication, the Shell constructor, the View constructor, the first native window, the GL context and
the shader programs. The glyph atlas is built lazily during the first frame and costs ~21 ms.

Measurement: `XMB_TEXT_PROFILE=1` (timers compiled in, no timer runs when it is unset; one line per
interesting point), one quiet-machine run with the ui3-data seed and the stored window geometry:

| process time | segment | ms |
|---|---:|---:|
| 0.048 | Qt + Assets + Shell constructor to the View constructor's volume read (XMBML loaders 20-25 ms of it) | 48 |
| 0.048 -> 0.497 | View constructor tail + QMainWindow setup + `restoreGeometry()` = **first native window creation** | 449 |
| 0.497 -> 0.505 | show() -> GL context current -> `initializeGL` entry | 8 |
| 0.505 -> 0.523 | `TextRenderer::init`: font configure 13.3, atlas program link 4.1, mask programs 0.2, VAO 0.1 | 17.7 |
| 0.523 -> 0.750 | `initializeGL` rest: ribbon (shader translation + a 14,400-step cloth settle, `ribbon.cpp:241`) 221-230, hdr/icons/particles/coldboot 8 | 238 |
| 0.750 -> 0.795 | first paintGL: MSAA layer FBOs, background, RCO image inventory | 45 |
| 0.795 -> 0.950 | first `QTextLayout` shaping (`"Game"` at 42 px) - Qt application-font startup | 155 |
| 0.950 -> 0.969 | 55 glyph tiles + composite + swap | 19 |

Total 0.97-1.00 s on a quiet machine with the warm-up disabled. The 714 ms timestamp = 48 + 449 + 8 +
17.7 + 238 (about 760 at this run's speed; the committed 714 ms is the same segment on a loaded run
whose first frames were 1354-1617 ms, and this tree's 10-run median for that line is 537 ms).

The text renderer's own parts, from the same run (55 tiles rasterised on the first frame, 175 glyph
quads drawn, 10 draw calls):

| part | ms | note |
|---|---:|---|
| `FontRaster::configure` (28 faces, 24 MB of TTF, head+cmap copies, sha256) | 13.3 | part of init |
| three shader programs (link) | 4.3 | part of init |
| FreeType rasterisation + coverage conversion (55 tiles) | 0.85 | 0.015 ms/tile |
| halo Gaussian (`tileFor`, 2-pass, sigma 4.4 at the 2x layer) | 19.4-20.6 | 0.35-0.37 ms/tile |
| atlas page allocation (4 MB RGBA8 zero upload) | 1.9-2.1 | once, first page |
| `glTexSubImage2D` of the 55 tiles + 1 page | 0.31-0.35 |  0.006 ms/tile |
| R8 union mask target (3840x2160) creation | 0.55-0.74 | once |
| GL state / painter begin-endNativePainting / glBufferData | 1.2 | whole first frame |
| first `QTextLayout` shaping | 155 | Qt, once per process, not glyph work |

So the 714 ms is 97% not the atlas: about 450 ms is the first native window, 240 ms is ribbon/hdr/
icon/particle GL init, 155 ms is Qt's first application-font shaping, and the glyph atlas itself
(rasterise + halo + upload + mask) is 21 ms. "Rasterise only the glyphs the first frame needs" is
already the behaviour (55 tiles, 175 quads); "avoid re-uploading the atlas" has nothing to avoid
(0.35 ms of uploads); batching uploads cannot beat 0.35 ms; a disk atlas cache would have to beat the
21 ms of rasterise+halo it replaces, and would have to be proven byte-identical - not worth the proof
surface at this size. The one part of the 714 ms that is both large and inside the text renderer was
the 155 ms first shaping, and it is process-global: a system face costs 0.66 ms, a first firmware face
155 ms, a second firmware family 0.83 ms (measured by warming each from `init`).

Change kept: the first firmware-family layout is warmed on a worker thread started in
`TextRenderer::init()` before the shader programs, and `layout()` joins it before the first real
shaping. The worker runs while `initializeGL` finishes and the first paint begins; the shaping result is
discarded and no renderer state is touched, so the frame is unchanged. `XMB_TEXT_WARM=0` restores the
synchronous path for the A/B.

`bun work/perf-harden.ts startup --runs 5` x 2 batches per arm, same binary, same seed, alternating
arms (`out/startup-warm0-*.json`, `out/startup-warm1-*.json`; 10 runs each):

| milestone | warm=0 median (min-max) | warm=1 median (min-max) |
|---|---:|---:|
| XMBML Network column | 20 (18-24) | 19 (19-23) |
| text atlas (end of init; unchanged by design) | 538 (482-607) | 537 (511-698) |
| native renderer (end of `initializeGL`) | 766 (708-868) | 804 (747-983) |
| **first frame** | **1063 (987-1194)** | **916 (848-1105)** |
| Steam content scan (async) | 1205 (1100-1369) | 1040 (961-1246) |
| Steam owned library (async) | 1554 (1382-1730) | 1414 (1321-1617) |

The first frame median drops 147 ms (13.9%). The 155 ms shaping leaves the first paint but 38 ms of it
reappears at the end of `initializeGL` (the worker competes with the ribbon settle for CPU), so the net
is 147 ms. The arms were interleaved in time, so machine drift affects both equally.

Measured and rejected (numbers from the same profiled runs): synchronous warm-up in `init()` (moves the
155 ms, saves nothing); clearing the stored fullscreen flag (the 449 ms reappears inside show(), net 0);
dropping `restoreGeometry()` entirely (same).

Still outside this fence, largest first: the first native window creation (420-450 ms, moves between
`restoreGeometry()` and `show()` but never goes away; clearing the stored fullscreen state so that
show() opens a plain window does not help, measured); the ribbon's 14,400-step cloth settle in
`initializeGL` (221-230 ms,
`ribbon.cpp:241` - a candidate for the ribbon owner to make lazy or table-cached); the 45 ms first-paint
layer setup. The Steam scan and owned library still land after the first frame.

Frame proof (pinned clocks, `--render-clock 20`, the 5.4 capture recipe): four captures, two with
`XMB_TEXT_WARM=1` and two with `=0`, are all SHA-256
`C5CBA50A5B7CE84BCAE532F10A4047C96912BEC8BA36CBD493BFA74A38EA75ED`, i.e. the warm-up is
render-neutral and the capture is deterministic. A reference build of the same tree with these four
files' changes reverted (`out/head-src`, `out/native-headref`) produces the *same* hash, so the pixels
below are the tree's, not this change's.

Canonical gate on this build (`bun work/ab-layers.ts --capture --exe
C:/dev/out/native-startup/re-ps3-native.exe`):

| layer | PSNR | base | SSIM | base | gate |
|---|---:|---:|---:|---:|---|
| frame | 18.92 | 18.02 | 0.8553 | 0.8301 | PASS |
| item label row | 12.04 | 11.94 | 0.3860 | 0.3910 | FAIL (SSIM floor 0.3870) |
| clock text | 13.64 | 11.39 | 0.5128 | 0.4105 | PASS |
| info bar | 17.61 | 14.78 | 0.8244 | 0.6117 | PASS |

The item-label-row SSIM miss is **pre-existing at the tree this phase started from**, and the reference
build above scores it identically (12.04 / 0.3860), as it does every other layer. The clock and info-bar
numbers are also above the 5.4 baselines on both builds (the tree gained a clock/information-bar change
after those baselines were saved). So the change in this phase moves no layer: 5/6 is the tree's
current state, not a regression from the warm-up.

`XMB_TEXT_CHECK=1` has one failing assertion on this build, `union-mask` ("GPU GL_MAX mask ==
keep-larger helper on 169 px"), and the same assertion fails identically on the reference build, so it
also predates this change. The probe reads back an empty mask while the shipped mask pass keeps
rendering: forcing the union (`XMB_TEXT_MASK=1`), disabling it (`=0`) and the auto rule all produce the
byte-identical canonical frame on the current build and on the 22:48 build, so the failure is in the
self-check's GL probe state, not a visible regression in the frame. Not fixed here: the probe's
readback path is shared with the shipped path and belongs with the change that made it fail.

Reproduce: `XMB_TEXT_PROFILE=1 XMB_TEXT_WARM=0` + the 5.4 recipe prints the init split, the per-frame
tile split and the slow-layout line; the gate and check commands are the ones above.

### 5.5 Soak

`bun work/perf-harden.ts soak --minutes <n>` runs one process for n minutes of wall clock, samples the
running process's working set, private bytes, handle count and CPU every 5 s, and reports the memory
trend plus what the app itself logged as churn events (`media: directory result`, `continue watching:
page opened`, `media queue: play`, `notify: enqueue`, `steam folder ...`, `motion: ...`). `--no-churn`
drops the script and measures continuous rendering only.

Why ten minutes and not thirty: a single scripted session is bounded by the Windows 32,767-character
command line, and the app delivers a token when its event loop is idle rather than on the nominal 300 ms
interval - measured here at 0.37-1.0 s per token (32 tokens in 20.5 s; 402 tokens in 151.8 s). A
30-minute churn session would need roughly 6,000 tokens, about 30 KB with the route's commas, with no
margin for the slower cadence. Ten minutes is what was attempted, and ten minutes of continuous
rendering is what the numbers below cover.

The churn session could not be sustained, and that is the result to report. Two long sessions on this
build ended the same way:

- A 2001-token churn session ran 203 s, rendered the whole time (`native frames` reports every 5 s at
  194-238 fps), and then the process disappeared with exit 255. The app logged exactly one churn event:
  the cycle's notification (its `notify: view slide/fade` lines run to 15.3 s). No `notify: expire` line
  arrived at the 4 s lifetime, no column-move `motion:` line appeared, and no later script token had an
  effect: every QTimer in the process stopped while the paint loop kept running. The same build consumes
  a 32-token script normally (20.5 s) and a 402-token wait-only script normally (151.8 s) under the same
  conditions, so the stall follows sustained interactive input, not script length. Two shorter repeat
  attempts (76 and 97 tokens) consumed their tokens but logged no churn at all.
- A ten-minute no-input session ended the same way: exit 255 at 208 s with the frame loop still healthy
  at its last sample (228 fps) and no crash record - the app's own handler writes a symbolized stack to
  `XMB_LOG`/`crash.log` and wrote none. This workspace had several other sessions running and reaping
  `re-ps3-native` by name (the cleanup this project's ad-hoc runs use), so the most likely cause is an
  external kill rather than an application fault. Either way it means long sessions cannot be trusted
  on this machine right now.

So no 10- or 30-minute churn session is claimed. The longest reliable window is 3.4 minutes, and both
runs are reported over it (`soak.json` = churn attempt, `soak-idle.json` = continuous rendering with no
input; one process each, 5 s samples of working set, private bytes, handles and CPU):

| run | first sample | settled (after 60 s) | end | post-startup trend | handles (settled -> end) |
|---|---:|---:|---:|---:|---:|
| churn attempt (203 s) | 130.1 MB | 319.8 MB | 321.1 MB | +33 MB/h over 2.34 min | 2054 -> 2071 |
| no input (208 s) | 130.2 MB | 274.5 MB | 274.8 MB | +7 MB/h over 2.42 min | 1524 -> 1514 |

Read that as: the 130 MB startup ramp is asset decode and the first page builds (roughly +145 MB with
one churn burst, +190 MB with a page build), after which the working set is flat to within noise
(+33 MB/h on a 320 MB process is 0.1% per hour) and the handle count does not climb. It says nothing
about a page-build leak over ten minutes, which is what a real churn soak would measure.

What a future soak needs, in order: (1) diagnose why all Qt timers stop after sustained input while the
paint loop is hot (a build-side question, outside this phase's write fence); (2) run the churn route for
10-30 minutes on a machine where no other session reaps the executable; (3) only then read the memory
trend as a leak signal. The pacing quantiles in 5.3 move between runs for the same environmental reason.



