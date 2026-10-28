# PARITY SCORECARD

Method, threshold policy and the first measured scorecard for the 1:1 fidelity track
([PARITY-PHASES.md](../PARITY-PHASES.md), P0.1-P0.5). Written 2026-10-27.

**Status: infrastructure complete, no screen at parity.** The catalogue covers 35 capturable states and
3 honestly-uncapturable ones (38 in all, 5 of them native-only with no firmware counterpart): ten states
were added on 2026-10-28 for the surfaces the second capture batch photographed and this app can actually
reach (section 12). Every pre-existing capturable state was captured on 2026-10-27 and gated on Tier A
(dumps) and Tier C (input traces), with 19 passing Tier A; the ten new states were captured on 2026-10-28
and all ten pass Tier A (12.5). Since the three imports in sections 10, 11 and 12, **9 of 30**
firmware-reference canonical cells and **24 of 720** firmware-reference matrix cells have a real RPCS3
reference (the matrix itself is 840 cells; 120 of them belong to the five native-only states, which owe no
reference). The references are: the one pre-contract frame, eight frames from the
`rpcs3-ref-2026-10-28` batch, and fourteen night cells from `rpcs3-ref2-2026-10-28`. Every imported
canonical reference is labelled mismatch-by-construction on its background pin, its console clock, its
theme assumption or its page beneath; the night cells are the cells where the pin and the state agree, and
they are measured in 11.4. **22 of the 30** firmware-referenced states now have a reference somewhere
(12 did before section 12's import); 21 of the 30 canonical cells have none, and 8 states have no reference
anywhere. That is the honest state of the corpus, and it is what P0 exists to make visible.

Nothing here is a visual-parity claim. A screen is 1:1 only when Tier A, Tier B and Tier C all pass for
it ([PARITY-PHASES.md](../PARITY-PHASES.md), "What 1:1 means here").

## 1. Method

### 1.1 The three tiers, as this corpus measures them

| Tier | Claim | Measured by | Tool |
|---|---|---|---|
| **A. Structural** | the same firmware data drives the same screen | the app's own dumps at the grab instant: `XMB_DUMP_ENTRIES` catalogue dump (per column, in display order), `<dump>.folder.json` for an open page, `<dump>.dl.json` for the download page, the `<png>.json` grab sidecar (category, depth, row id/title/count, page kind, media/sign-in/notification fields), and the app's `XMB_LOG` lines | `work/parity.ts` per-state `expect` block |
| **B. Perceptual** | the frame matches a real RPCS3 frame within a budget | RGB PSNR and 8x8-luma SSIM per region against `<state>__<theme>__<background>__<resolution>.png` under the reference root | `work/parity.ts --score`, metrics from `work/ab-compare.ts` |
| **C. Behavioural** | the same inputs produce the same state transitions | ordered `XMB_LOG` / `XMB_PS_LOG` trace lines the scripted route must produce (options panel opened with its four rows, notification enqueued, volume request read back, sign-in page opened, subtitle sidecar discovered, PS menu opened) | `work/parity.ts` per-state `trace` block |

Two consequences are built in rather than papered over:

- A state with no dump or sidecar field that names its surface **cannot pass Tier A**. The OSK and the
  controller indicator are in that position today; their assertions prove the page underneath, and the
  scorecard marks the scope instead of calling the picture proof. Closing it needs one sidecar field each
  (`oskOpen`/`oskTitle`, `controllerIndicatorVisible`) from the app owner - a 5-line change in
  `src/main.cpp`'s grab sidecar, not a harness change.
- A state whose route is just "open the column" has nothing to trace, so Tier C is `n/a`, not `pass`.
  `A+B+C` therefore cannot be claimed for a state with no scripted input until a trace exists for it.

### 1.2 Capture conventions

Reused unchanged from the per-layer harness ([VERIFICATION-HARNESS.md](VERIFICATION-HARNESS.md),
`work/ab-layers.ts`), so a Tier B number here and a layer number there are the same measurement:

- **Clocks pinned**: `--render-clock 30`, `--background-clock <cell preset>`, `--ui-clock 2026-10-25T12:32:00`,
  `--focus-time 2000`. Two independent runs of the same cell are pixel-identical: `root` and
  `notification` were captured twice on 2026-10-27 and compared, both at PSNR Infinity / SSIM 1.0000
  (runs `run-20261027-224129` and `run-20261027-224203`).
- **Script tokens**: the app's own (`src/main.cpp`): `full`/`size=WxH`, `wait`, arrows, `ENTER`, `TRIANGLE`,
  `ps`, `pshold`, `NOTIFY=`, `VOLSET=`, `VOLRESTORE`, `type=`, and the harness-owned `grab=`/`quit`.
- **Seeded data directory**: every cell copies a recorded seed (`out/native-reference/store/ui3-data`,
  `.../video-data`, `.../phase5/resume-data`, `.../phase5/subtitle-data`, `.../store/jf-smoke`) into its own
  directory, so the live profile is never written and the state cannot drift with the user's settings.
- **Route resolution, not typed offsets**: one discovery capture per data seed reads the catalogue dump and
  resolves the column's LEFT/RIGHT count and each `catalogueId` row index. A changed column fails the run
  loudly instead of silently opening a different row.
- **Network-free states only**: `XMB_STEAM_ARTWORK=0`, and a cell that makes a live store request is
  rejected by `noStoreDump` (a `<dump>.store.json` appearing is a failure).
- **One retry policy, honestly labelled**: a failed Tier A/C assertion is retried (two retries, three
  attempts) because a host focus change can make a scripted PS hold a no-op and a key can be swallowed
  while the app rebuilds its catalogue under a Steam scan. The manifest records `attempts`, the best
  attempt is scored, and a state that fails all three attempts is reported FAIL - never silently widened
  into a pass. This mirrors the named-state convention in the per-layer harness, only with the count
  recorded.

### 1.3 Commands

```powershell
# the catalogue: ids, classes, XMBML views, routes, reachability, region crops
bun work/parity.ts --catalogue

# the planned matrix (600 cells), or one cell per state
bun work/parity.ts --matrix
bun work/parity.ts --matrix --state root --background night --resolution 720p

# capture the canonical cell for every capturable state, then score it
bun work/parity.ts --capture --score

# one state, or a slice of the matrix
bun work/parity.ts --capture --score --state root,ps-menu
bun work/parity.ts --capture --score --resolution 720p

# score the newest run again, or a recorded run
bun work/parity.ts --score
bun work/parity.ts --score --run out/parity/run-20261027-211034

# regression: score, then diff against the previous scorecard
bun work/parity.ts --regression

# what has no reference yet (no capture, no scoring)
bun work/parity.ts --references-missing
bun work/parity.ts --references-missing --matrix
```

Exit codes: `0` every checked gate passed, `1` a Tier gate failed or a regression was found, `2` an
input/runtime error (bad catalogue, missing executable, failed capture). `--json` prints the machine
readable form of every command. Scorecards are written to `out/parity/scorecards/scorecard-<stamp>.json`
and `.md` (local, gitignored); `--markdown <file>` also writes the markdown where asked.

The default executable is `C:/dev/out/native-parity/re-ps3-native.exe` and the default flash
`C:/dev/rpcs3/bin/dev_flash`; `--exe`/`--flash` override both. **The tool never launches RPCS3.**

### 1.4 Capture conditions on 2026-10-27 (why some cells failed)

The corpus was captured against the build at `C:/dev/out/native-parity/re-ps3-native.exe`
(sha256 `0a82595e524455394f8f1b5484fd124a29763883db00a654d0474d49289347a2`, built from the tree at
20:47 local). Three conditions are worth recording because they changed what the scorecard could prove:

- **The tree moved under the build.** Another parity agent is editing `src/` (a diagnostic
  `DIAG vertical` line appeared in `Shell::vertical`, and the Jellyfin sign-in page gained a row). A
  rebuild for diagnostics at 22:05 failed mid-edit, so this scorecard describes the 20:47 snapshot only.
  Re-running it on any later build is one command (`--capture --score --exe <new exe>`); the scorecard
  header pins the executable hash so the two are never confused.
- **Scripted vertical moves stopped registering at about 21:45.** From that point every scripted
  `UP`/`DOWN` left the selection on row 0 while `LEFT`/`RIGHT` still moved the column, so the six states
  whose route opens a row below the first failed Tier A: `page-settings-system`, `page-game-all-games`,
  `page-video-library`, `page-download-management`, `options-panel`, `media-player-panel`. The same build
  and the same scripts passed those routes at 21:22-21:28 (three consecutive isolated runs of
  `page-game-all-games` and a full-suite run at 21:24), so this is a capture-condition regression, not a
  catalogue error: the harness reports the state it actually reached instead of the one the route
  intended. **Root cause, 2026-10-27 (`07884d7`):** a live DualSense on USB was reporting a held up and
  `View::pollController` merged it into the navigation bits, producing a `vertical(-1)` every 67 ms that
  cancelled the scripted DOWN. The fix gates live pad input during `--script`; this scorecard predates
  it and has not been re-captured.
- **The app can also hang on exit under the host Steam root.** Three direct probes stopped responding
  after the grab (no further token processed, process never exited) with the host Steam library, and
  completed cleanly with an empty `XMB_STEAM_ROOT` - the Steam scan is implicated. The harness bounds
  that with `--timeout-ms` (120 s) and a per-capture failure record rather than an open-ended wait.

None of this is hidden in the numbers below: a state whose route did not reach its page shows FAIL with
the row it actually landed on.

## 2. The catalogue (P0.1)

`work/parity-catalogue.json` - 38 states, 35 capturable today, 3 recorded as not capturable with the exact
missing backend, and 5 marked `noFirmwareReference` (native surfaces with no firmware counterpart, section
12.4). Every state carries: id, screen class, the firmware XMBML document and view that defines
it (or an explicit "native, no firmware view" with the reason), the native reachability, the script route,
the dump assertions, the trace assertions and the region crops.

`--catalogue` cross-checks each claimed XMBML view against the actual document in dev_flash (206 views
across 28 documents), so a stale document or view id fails without a capture.

| Class | States | What they are |
|---|---:|---|
| `icon-heavy` | 11 | the root and the ten columns (Users, Settings, Photo, Music, Video, TV/Video Services, Game, Network, PlayStation Network, Friends) |
| `text-heavy` | 14 | Settings > System Settings, Network Settings, the depth-2 status list, the two Network message pages, Theme Settings, Video Settings, Game > All Games, Video > Video Library, Game > Steam Store sections, Network > Download Management, TV > Jellyfin sign-in, Continue Watching, Trophy Collection (not capturable) |
| `composite` | 13 | options panel, dialog, OSK, notification, volume display, information panel indicator, PS menu, media player panel, the four Theme choice lists, PSN sign-in (not capturable) |

Ordered by class, not by section: the ten states added on 2026-10-28 are `page-settings-network`,
`page-network-status-list`, `page-network-test-error`, `page-network-connection-settings`,
`page-settings-theme`, `theme-list`, `color-list`, `background-list`, `font-list` and `page-settings-video`
(12.1).

Coverage of the P0 seed list:

| P0 seed item | Catalogue state(s) |
|---|---|
| root | `root` |
| each column's page | `column-user`, `column-settings`, `column-photo`, `column-music`, `column-video`, `column-tv`, `column-network`, `column-psn`, `column-friends` |
| an options panel | `options-panel` (Triangle on the Video Library page) |
| a dialog | `dialog` (Continue Watching with nothing to continue) |
| the OSK | `osk` |
| the info panel | `info-indicator` (PS-tap controller indicator) + the `info bar` region scored in every root/column state |
| a media player panel | `media-player-panel` (paused local media, sidecar subtitle cue) |
| the volume display | `volume-hud` (pinned 25%) |
| the PS menu | `ps-menu` (PS hold) |
| a notification | `notification` |
| Continue Watching | `continue-watching` (populated) and `dialog` (empty state) |
| the download page | `page-download-management` |
| Jellyfin pages/sign-in | `page-jellyfin-signin`, `osk` (the keyboard over it) |
| a settings submenu | `page-settings-system` |
| (also) depth-1 pages | `page-game-all-games`, `page-video-library`, `page-steam-store-sections` |
| (also) honest gaps | `psn-signin`, `trophy-collection` (message-only: no PSN backend), `disc-game` (no BD drive) |

The 3 non-capturable states are catalogued, not hidden: `psn-signin` and `trophy-collection` answer with
the firmware's own unavailable message because no PSN backend may be fabricated, and `disc-game` needs an
inserted disc the PC does not expose. Each is printed by `--catalogue` with its reason.

## 3. The capture matrix (P0.2)

`work/parity-reference.json` `matrix`: **35 states x 3 themes x 4 backgrounds x 2 resolutions = 840 cells**
(was 25 x 24 = 600 before the ten states of section 12 were added; 120 of the 840 belong to the five
native-only states, which owe no firmware reference and are excluded from the reference debt).

- **Theme axis**: the three states `appearance/background` and `appearance/theme` actually produce -
  `original` (the wave), `classic`, `p3t-01` (the firmware's own theme through the p3t path).
- **Background axis**: `day` (10:00, the traced daylight pin the stored reference was captured at),
  `night` (00:00), `yoake` (05:00), `higure` (17:00).
- **Resolution axis**: `1080p` (`full`, 1920x1080) and `720p` (`size=1280x720`).

**One deviation from the plan's wording, stated rather than silently chosen**: PARITY-PHASES lists the
theme axis as "Original + the four presets" and the background axis as "day/night/yoake/higure". In this
build one pinned background clock selects *both* the ICONS.mnu override (`src/icons.cpp:50-65`) and the
LINE1/PARTICLES preset (`src/ribbon.cpp:341-345`), so "the four presets" **are** the four backgrounds.
Capturing them as two axes would count the same pin twice and inflate the corpus by 4x. The theme axis
therefore carries what the app can actually change independently: Original, Classic, and the firmware
theme. If a future build separates the icon-material preset from the background preset, this axis gets its
fourth entry and the matrix grows by the missing factor.

720p is a 1280x720 **windowed** framebuffer: the host exposes no 720p display mode. The console's own 720p
projection is P10's work, not a scaled 1080p frame, and the scorecard says so rather than comparing a
window to a console mode.

The default run captures the **canonical cell** (`original` / `day` / `1080p`) for every capturable state -
one cell each, 35 captures. `--matrix` with `--capture` runs all 840; `--theme/--background/--resolution`
slice it.

## 4. The reference contract (P0.3)

`work/parity-reference.json` is the contract; the images live outside git.

- **Root**: `out/parity-references/` (inside `out/`, which `.gitignore` excludes - firmware-derived
  captures are never committed).
- **Naming**: `<state>__<theme>__<background>__<resolution>.png`, one file per cell, stored at the cell's own
  resolution. A reference is never rescaled to make it pair.
- **Provenance**: `<state>__<theme>__<background>__<resolution>.state.json` - kind, capture date, firmware,
  RPCS3 build, renderer preset, console/pinned clock, PNG sha256, and the note a reader needs before
  treating the number as fidelity. A reference without provenance is scored but **cannot pass**.
- **Pairing**: a captured cell pairs only with the reference whose basename equals its cell id. No reference
  means "no reference yet" - never a score against a different state, never a native self-baseline.
- **Registered exceptions**: pre-contract frames are listed in `registered` with their real path. There is
  exactly one: `root__original__day__1080p` -> `out/native-reference/ab-rpcs3-root.png`, the 2026-10-25
  frame the per-layer harness scores against. Its content does not match by construction (the reference
  shows Corrupted Data and the pre-Steam column; the native build shows the Steam Store rows; the reference
  clock is DD/MM 24-hour, the native default MM/DD 12-hour). It is kept as a real number with a real
  reference and labelled, not deleted.
- **Not references**: `out/native-reference/layers/*.png` are deterministic **native self-baselines** for
  `work/ab-layers.ts`; they prove repeatability, says the harness itself, and say nothing about firmware
  fidelity. They must never be scored as Tier B.

### 4.1 The RPCS3 recipe, and why this tool does not run it

The boot how-to is `HANDOFF-PHASES.md` section "Capturing the real XMB in RPCS3 (how-to, learned
2026-10-25)", and `work/parity-reference.json` `rpcs3` records it as the contract: no-GUI boot of
`vsh.self` with a scratch config outside the repo and the RPCS3 folder, Vulkan at 1920x1080, exclusive
fullscreen disabled (otherwise desktop capture returns one frozen frame), `CopyFromScreen` for the whole
frame (PrintWindow is stale under Vulkan), and the global input config backed up, sha256-checked
(`e624da0d...ceee`) and restored in the same run because changing it needs the user's approval. Cross and
Circle go on PgDown/PgUp because letter keys do not reach `vsh.self`.

The per-cell recipe (boot, drive the same route as the native script key for key, match theme/preset/
resolution and the console clock, settle two seconds, grab, write provenance) is in
`work/parity-reference.json` `rpcs3.perCellRecipe`; the matrix recipe is `rpcs3.matrixRecipe`. **This tool
never launches RPCS3** - the rig belongs to the capture agent. `--references-missing` is how this track
reports the work the rig owes.

## 5. Threshold policy (P0.5)

Per screen class, in `work/parity-reference.json` `budgets`. All Tier B budgets are **provisional**: no
matched reference exists yet, so they are targets, not measurements. They are deliberately *not* set at
"today's value minus a delta" - that is the regression-gate pattern in `work/ab-layers.thresholds.json` and
it would prove nothing about firmware fidelity.

| Class | Frame PSNR / SSIM | Region PSNR / SSIM | Rationale |
|---|---|---|---|
| `icon-heavy` | 20.00 dB / 0.9000 | 20.00 dB / 0.9000 | structure is icons, labels and the clock over the wave; the unmatched root pair measures 18.20 / 0.8388, so a matched column should clear 20 / 0.90 |
| `text-heavy` | 18.00 dB / 0.8500 | 18.00 dB / 0.8500 | text scores lower even when matched: the console's glyph phase and coverage compositing are not reproduced bit-exactly (TEXT-RASTER.md) |
| `composite` | 18.00 dB / 0.8800 | 18.00 dB / 0.8800 | the surface region is what matters, not the scene under it |

Region overrides may **tighten** a class budget freely; the corpus does so in three places, all on the
two `root`-family bands and on the PS menu:

- `root` / `info bar` 17.0 dB / 0.75, `root` / `clock text` 16.0 dB / 0.70 - these bands are content-bound
  (clock format, indicator state) and would otherwise dominate a frame that is otherwise structural.
- `ps-menu` / `ps menu entries` 26.0 dB / 0.95 - the console blacks the whole frame there, so the entries
  and the clock are the only content and the achievable number is far higher.

**Relaxing a budget is a dated change**, not an edit: `budgets.updated` and `budgets.revisionNote` in
`work/parity-reference.json`, plus a dated entry in section 9 below. The tool refuses a budget block with
no note, so "the number moved and nobody said why" fails validation.

**Tier C** is an ordered trace, not a timeout: each pattern must match a log line at or after the previous
one. That proves the transition happened *in order*, which is the half of behaviour a screenshot cannot
show. Frame-counted timing (the burst convention in `work/ab-layers.ts` / `audit/PAGE-MOVE.md`) is P6's
instrument and is not re-implemented here.

**Tier A** assertions are dump equality, not pixels: the column's row ids in display order, the opened
page's mode/title/row prefix, the download page's rows and state text, the grab sidecar's page kind,
focused row, media/sign-in/notification fields, and the app's own log lines. A state that cannot be
identified from its dump fails before any pixel is compared - the same rule the per-layer harness uses for
its named states.

## 6. First scorecard (2026-10-27, canonical cell)

Run `out/parity/run-20261027-222734`; scorecard
`out/parity/scorecards/scorecard-20261027-223917.json` and `.md`; build
`C:/dev/out/native-parity/re-ps3-native.exe` (sha256 `0a82595e...347a2`, the 20:47 snapshot); canonical
cell `original / day / 1080p`; pins `render 30 s`, `ui 2026-10-25T12:32:00`, `focus 2000 ms`.

| Tier | Passed | Out of | What that means |
|---|---:|---:|---|
| **A. dumps** | 19 | 25 capturable | the app's own dump/sidecar/log proves the screen and the rows on it. The six failures are route failures, not wrong assertions (1.4). |
| **B. references** | 0 | 25 | 24 cells have no reference yet; the 25th has one, and it is the unmatched legacy frame - so even that one cannot pass. |
| **C. traces** | 6 | 25 | the six states whose route performs a scripted transition that logs. The other 18 routes are "open the column", so there is no transition to trace: `n/a`, never counted as a pass. |
| **A+B+C (a real 1:1 claim)** | **0** | **25** | no screen on this build is 1:1, and the scorecard says so instead of aggregating the tiers away. |

### 6.1 The only Tier B numbers in existence

| Region | Crop | PSNR | SSIM | Budget | Gate |
|---|---|---:|---:|---:|---|
| frame | 0,0,1920,1080 | 18.34 | 0.8314 | 20.00 dB / 0.9000 | FAIL |
| top background | 0,0,700,200 | 20.22 | 0.9336 | 20.00 dB / 0.9000 | PASS |
| category icon row | 0,240,1920,120 | 20.89 | 0.8966 | 20.00 dB / 0.9000 | FAIL |
| item label row | 400,425,800,100 | 11.94 | 0.3909 | 20.00 dB / 0.9000 | FAIL |
| clock text (not gating) | 1570,80,260,50 | 11.39 | 0.4105 | 16.00 dB / 0.7000 | FAIL |
| info bar | 1329,75,591,58 | 14.78 | 0.6117 | 17.00 dB / 0.7500 | FAIL |

These are `root` against the pre-contract frame: content differs by construction, so four of the six
gates failed and none of these numbers is a parity measurement. What they prove is that the whole Tier B
path works end to end - pairing, provenance, decode, crop, metric, budget, gate - on real firmware
pixels. They agree with the per-layer harness's independently measured pair on the same reference
(18.20 dB / 0.8388 whole frame, `work/ab-layers.thresholds.json`) to within its own tolerance
(18.34 vs 18.20 dB), which is what two tools sharing the metrics and differing in capture recipe should do.

### 6.2 The state table

| State | Class | A | B | C | Claim |
|---|---|---|---|---|---|
| root | icon-heavy | PASS | FAIL (unmatched reference) | n/a | A |
| column-user, column-settings, column-photo, column-music, column-video, column-tv, column-network, column-psn, column-friends | icon-heavy | PASS | no reference | n/a | A |
| page-steam-store-sections | text-heavy | PASS | no reference | n/a | A |
| page-jellyfin-signin | text-heavy | PASS | no reference | PASS | A+C |
| page-download-management | text-heavy | FAIL (route) | no reference | FAIL | none |
| page-settings-system | text-heavy | FAIL (route) | no reference | n/a | none |
| page-game-all-games | text-heavy | FAIL (route) | no reference | n/a | none |
| page-video-library | text-heavy | FAIL (route) | no reference | n/a | none |
| osk | composite | PASS (page only) | no reference | n/a | A |
| info-indicator | composite | PASS (page only) | no reference | n/a | A |
| dialog | composite | PASS | no reference | PASS | A+C |
| continue-watching | text-heavy | PASS | no reference | PASS | A+C |
| notification | composite | PASS | no reference | PASS | A+C |
| volume-hud | composite | PASS | no reference | PASS | A+C |
| ps-menu | composite | PASS | no reference | PASS | A+C |
| options-panel | composite | FAIL (route) | no reference | FAIL | none |
| media-player-panel | composite | FAIL (route) | no reference | FAIL | none |

The six "FAIL (route)" rows all reached row 0 of the intended column instead of the row the route names,
which is the vertical-input condition in 1.4: `page-settings-system` opened System Update instead of
System Settings, `page-video-library` stayed on Continue Watching, `page-download-management` stayed on
Online Instruction Manuals, and the two composite routes (`options-panel`, `media-player-panel`) inherit
the same miss. Their assertions are unchanged from the run at 21:24 where the same routes passed.

### 6.3 Regression mode, exercised on two real runs

```powershell
bun work/parity.ts --regression --previous out/parity/scorecards/scorecard-20261027-213411.json
```

```
regression: FAIL (5 finding(s))
  - page-settings-system: claim fell from A to none
  - page-game-all-games: claim fell from A to none
  - page-video-library: claim fell from A to none
  - options-panel: claim fell from A+C to none
  - media-player-panel: claim fell from A+C to none
```

Exit 1, and the five claims that fell are exactly the states the capture-condition regression hit. That
is the gate P11.3 asks for: a change that lowers the published score cannot do so silently. The report is
written next to the scorecard as `regression-<stamp>.json`.

## 7. What lacks a reference, and what that costs

`bun work/parity.ts --references-missing` is the live list: **1 of 25 canonical cells has a reference**
(the registered pre-contract root frame). Over the whole matrix it is **1 of 600 cells**
(`--references-missing --matrix`). The other 24 canonical cells are captured and gated on Tier A/C and
reported as "no reference yet".

> **Updated 2026-10-27 (section 10):** after the first corpus import the live list is **9 of 25**
> canonical cells and **10 of 600** matrix cells. The numbers above are the measurement this section was
> written from and are kept as the dated record; section 10 carries the new state.

> **Updated 2026-10-27 (section 11):** the second import adds four night cells and leaves the canonical
> list at **9 of 25**; over the matrix it is now **14 of 600**. The three states that batch newly
> references (`column-network`, `column-psn`, `column-friends`) are still listed here because their
> canonical (day) cell is empty - the report counts cells, not states with a reference somewhere.

> **Updated 2026-10-28 (section 12):** ten states were added and ten night cells imported, so the live
> list is **9 of 30** firmware-reference canonical cells (35 states in the catalogue are capturable, 5 of
> them native-only with no firmware reference and therefore not owed one; 30 is the denominator that
> matters) and **24 of 720** firmware-reference matrix cells (the matrix is 840 cells, 120 of them on the
> five native-only states). The report now also answers the question this section flagged: **22 of the 30**
> firmware-referenced states have a reference somewhere (9 did at the canonical cell). The canonical count
> still does not move, for the same honest reason as section 11 - every new cell is a night cell.

> **Measured result, not a placeholder.** This section carries no unfilled template block: the results
> are the filled tables in section 6 and the header above. For run `run-20261027-222734` /
> `scorecard-20261027-223917`: **Tier A 19/25, Tier B 0/25, Tier C 6/25, A+B+C 0/25.** The six Tier A
> failures are route failures, named: `page-settings-system`, `page-game-all-games`,
> `page-video-library`, `page-download-management`, `options-panel`, `media-player-panel`. An earlier
> pass summarized the tier as "Tier A is proven today for every capturable state"; that is not what this
> scorecard measured - six states are FAIL and the score is 19/25. The named ids are the correction.

What this means per tier:

- **Tier A is not proven for every capturable state.** It is proven for 19 of the 25; the six states
  named above are FAIL, all on a route that needs a DOWN move, and they stay FAIL until a re-capture
  takes them. The cause is not a missing dump field. The OSK and controller-indicator scope caveat in
  1.1 still applies to the two states that pass "page only".
- **Tier C** is proven for the 6 states with a scripted transition that logs; the rest are `n/a` because
  their route is "open the column" and there is no transition to trace.
- **Tier B** is a single number for a single state, against a frame whose content differs by construction.
  It is a plumbing proof, not a fidelity claim.

**In-band caveat: the scripted-vertical defect.** From about 21:45 on 2026-10-27 every scripted `UP`/
`DOWN` was cancelled while `LEFT`/`RIGHT` still moved the column. That is the defect behind all six
failures. On 2026-10-27 it was root-caused, not excused: a live DualSense on USB reported a stick at raw
Y=55 (-0.575), which `View::pollController` turned into a held D-pad up repeating every 67 ms (188
phantom `vertical(-1)` calls in 12.6 s); the scripted DOWN was undone before the next token. `07884d7`
makes `pad::latest()` withhold live pad input while `--script` is on, traces each source, and verifies a
DOWN-dependent settings route twice with byte-identical frames. **No scorecard re-capture exists after
that fix.** The six states are therefore still reported FAIL at 19/25; the next run is what changes the
number, and a widened assertion before that run would be the failure this corpus exists to prevent.

The corpus cannot become a parity claim until the RPCS3 rig fills cells. The order that buys the most:
`root` first (it is the frame that already exists - re-capture it matched, with the Steam rows the native
build shows and the console clock the app renders), then the ten columns at `original/day/1080p`, then the
composite surfaces, then the 4 background presets and 720p.

## 8. Honest limits

- **Tier B is a perceptual budget, not bit-exactness.** The reference is an emulator frame; the console's
  rasterisation (half-pixel rules, precision modes, RSX quirks) is only partly recoverable. A pass is
  "within a measured budget", never "identical" (PARITY-PHASES, "What this track will still not deliver").
- **The one reference in the tree is unmatched by construction.** Its 18 dB-class numbers must not be read
  as near-parity; matched-state numbers do not exist yet.
- **The caption of every Tier B number is a cell**, and a cell is only comparable when theme, background
  pin, resolution, clock settings, profile and content set all agree. The provenance sidecar is where that
  is recorded; a cell without it cannot pass.
- **Two surfaces have no Tier A oracle** (OSK, controller indicator): their catalogue entries assert the
  page underneath. The fix is one sidecar field each, owned by the app, not by this harness.
- **The capture reads host state where a state includes it**: the host Steam library (Game column
  contents), the host's Steam download queue fixture, the pinned UI clock, and the Windows master volume
  for the HUD (restored by `VOLRESTORE` after the grab). A changed library or a muted endpoint changes the
  frame; the dump assertions fail loudly rather than letting a different state pass.
- **The PS hold depends on the host's foreground.** The app refuses to open its menu while the XMB is not
  in front (`src/view.cpp:1769`), so a capture can lose that race. It is retried (three attempts) and the
  failure mode is visible in `ps.log` as `event: press not actionable`; a run that loses three times
  reports FAIL rather than a fake pass.
- **Scripted input was unreliable on this build until `07884d7`.** As 1.4 records, vertical moves
  stopped registering mid-session, and the same binary had passed those routes four hours earlier. The
  cause is now measured - a live pad injecting a held up, not an app route or a harness script - and the
  gate withholds live pad input while a script runs. That makes the instrument trustworthy on the
  verification route, but it does not retroactively fix this scorecard: a Tier A FAIL on a DOWN route
  here is that defect, and only a re-capture after `07884d7` can move it to PASS.
- **Shared seeds are local scratch, not fixtures.** `out/native-reference/store/*` is written by whoever
  runs a capture; the Jellyfin focus drift in 6.2 came from a seed edit between runs. A state that depends
  on a seed field should assert the field's *shape* (a focused row on this page) rather than one value
  that the next seed edit moves.
- **`--capture --matrix` is 600 captures** at roughly ten seconds each; the default run is 25. The matrix is
  infrastructure, not something to run on every commit.
- **Frames are 8-bit PNGs at the cell resolution.** No HDR/metadata comparison is attempted (the console has
  none), and a dimension mismatch is reported, never rescaled.
- **This tool cannot see audio, motion or input latency.** Those are P6/P8 instruments; Tier C bounds state
  order only.

## 9. Dated change log

- **2026-10-27** - created. Catalogue (28 states, 25 capturable), matrix (600 cells), reference contract,
  budget policy v1 (provisional), and the first end-to-end scorecard against the `out/native-parity` build:
  Tier A 19/25, Tier B 0/25 (24 cells have no reference; one unmatched legacy reference), Tier C 6/25,
  no state at A+B+C. One registered pre-contract reference; everything else has no reference yet.
- **2026-10-27, later the same day** - the Jellyfin sign-in assertion was relaxed from one focused row to
  the page's identity (`focusedRow` prefix) because the shared `jf-smoke` seed had a server URL
  pre-filled, which moved the initial focus to Server URL. The state passes A+C from the 22:39 run. No
  budget was changed.
- **2026-10-28** - section 7 reconciled with section 6: Tier A 19/25, Tier B 0/25, Tier C 6/25,
  A+B+C 0/25, with the six failing state ids named and no results placeholder. The scripted-vertical
  defect is root-caused in `07884d7` (a live DualSense injecting a held up) and recorded as an in-band
  caveat on the measurement. No budget, reference, score or measured number changed; the scorecard still
  describes the pre-fix capture, and a re-run after the input fix is owed.
- **2026-10-27, later still (reference corpus import)** - `work/parity-import.ts` and
  `work/parity-references-map.json` added; 9 frames from `C:/dev/out/rpcs3-ref-2026-10-28` imported
  (8 canonical cells plus a matched root frame at `root__original__night__1080p`). `--references-missing`
  is now 9/25 canonical (was 1/25) and `--references-missing --matrix` 10/600 (was 1/600). Run
  `run-20261027-222734` re-scored as `scorecard-20261027-230329`: Tier A 19/25 (unchanged - nothing was
  re-captured), Tier B 0/25 pass but 9 cells measured (was 1), Tier C 6/25, A+B+C 0/25. Every imported
  cell is labelled mismatch-by-construction on the theme assumption, the background pin or the console
  clock (10.5). The second batch `rpcs3-ref2-2026-10-28` was refused by the importer: no `MANIFEST.md`
  while it was still being written. The registered-root contract conflict is recorded in 10.7 and not
  worked around. No budget, matrix axis or catalogue entry changed.
- **2026-10-27 (second reference corpus import)** - four night cells from `rpcs3-ref2-2026-10-28` added
  (`column-network`, `column-psn`, `column-friends`, `page-settings-system`), imported through a disclosed
  byte-faithful import-copy manifest because the batch manifest carries no `- Session:` line (11.1).
  `--references-missing` stays **9/25** canonical and moves **10 -> 14 of 600** matrix cells: every frame
  in the batch is a night frame, so nothing could fill a canonical day cell. A night capture
  (`run-20261027-233820`) measures the new cells at their own pin for the first time: 16.17-16.89 dB /
  0.37-0.39 whole frame, all FAIL against 20 dB / 0.90, and the System-page cell's capture failed its
  route (Tier A), so its 17.63 / 0.3790 is not a surface comparison. Canonical re-score unchanged:
  `scorecard-20261027-233626`, Tier A 19/25, Tier B 0/25 (9 measured), Tier C 6/25, A+B+C 0/25. The
  batch's other stills are refused in 11.5 (bursts are crops, the update frames are a host overlay, and
  about 100 frames show surfaces no catalogue state names). No budget, matrix axis or catalogue entry
  changed.
- **2026-10-28 (catalogue extension and third import)** - ten states added for the surfaces batch 2
  captured and the app can reach: `page-settings-network`, `page-network-status-list`,
  `page-network-test-error`, `page-network-connection-settings`, `page-settings-theme`, `theme-list`,
  `color-list`, `background-list`, `font-list`, `page-settings-video` (12.1-12.3). Catalogue 28 -> 38
  states, capturable 25 -> 35, matrix 600 -> 840 cells. Five native-only pages marked
  `noFirmwareReference: true`; `work/parity.ts` gained that field (validation, `--catalogue`, `--list`),
  honours it in `--references-missing` (which now also reports state coverage) and in the scorecard's
  reference-debt denominators (12.4). All ten new states captured on the current build
  (`C:/dev/out/native-status3/re-ps3-native.exe`, sha256 `BE55FF1E...a631E9`) - 10/10 cells, **Tier A
  PASS for all ten** - and again on the pre-regression build with text
  (`C:/dev/out/native-choices/re-ps3-native.exe`, sha256 `6B668353...036DF0`) for pixel evidence, where the
  six page states pass and the four list states fail only the band/panel log oracle that build predates
  (12.5). Ten night cells imported from `rpcs3-ref2-2026-10-28` (23 cells total, 0 skipped, exit 0;
  `--references-missing` **9/30** canonical and **14 -> 24 of 720** firmware-reference matrix cells; 22 of
  30 states now have a reference somewhere, 12 before). No budget, no contract change, no matrix axis
  changed, no pre-existing reference re-scored.
- **2026-10-28 (day-window matched session and fourth import)** - the recipe 11.6/11.7/12.8 asked for: one
  RPCS3 boot (00:25:52-00:35:25 local) with the guest clock at 10:00 put there by
  `System > Console time offset (s)` (34,362 s) rather than by the host clock, Date Format `MM/DD/YYYY` and
  Time Format `12-Hour Clock` committed on the console through Date and Time Settings, Theme evidenced on
  the frame (`Theme/Color/Background/Font = Original`), Vulkan 1920x1080 with exclusive fullscreen disabled,
  clock scale 100, 2 s settle. The batch manifest carries a `- Session:` line, so the importer took the real
  manifest with no import copy: 36 cells, 0 skipped, exit 0 - 13 new canonical day cells plus 8 deliberate
  replacements of night frames that had been filed at day cells. `--references-missing` **9 -> 22 of 30**
  canonical and **24 -> 37 of 720** matrix cells; states-with-a-reference stays 22 of 30 because every state
  here already had a night cell. `root` is captured and staged but **not imported**: its cell is the
  registered pre-contract frame and the importer refuses a registered cell (10.7 still open). The first
  genuine canonical Tier B run (`run-20261028-003804`, `scorecard-20261028-004646`, current build
  `706b43f3...c712d`) captures 22/22 with Tier A PASS for all and Tier B 0 pass; the columns move from about
  10.5 dB / 0.22 SSIM (night frame at a day cell) to 17.7-19.0 dB / 0.83-0.86, with `root` as the
  build-change control at +0.58 dB / +0.024. No budget, no contract change, no matrix axis and no tool
  changed; section 13 carries the per-cell numbers and the two that are not surface comparisons.

## 10. Reference corpus import - 2026-10-27 (batch `rpcs3-ref-2026-10-28`)

This is the section that changes the corpus from "one legacy frame" to "a reference set". It is dated
2026-10-27 local (America/Chicago); the capture batch's folder is named `2026-10-28` by the rig that
produced it.

The two batches live outside git:

| Batch | Manifest | What it holds | Imported |
|---|---|---|---|
| `C:/dev/out/rpcs3-ref-2026-10-28/` | `MANIFEST.md`, complete (rig table, session line, frame sizes) | 135 stills + 9 bursts, all 1920x1080: dialogs/options/information, the clock plate in both formats, all 15 settings pages, the choice lists, the OSK page/palette/caret, the three empty media columns, 24 nav frames | **9 cells** (8 canonical + 1 night root) |
| `C:/dev/out/rpcs3-ref2-2026-10-28/` | **absent** | menus/, lists/, depth2/, nav2/, bands/, tails/ were still being written at 23:04 local (7,608 files, newest mtime seconds old) | **0 cells - refused** |

The second batch was refused rather than half-imported: `work/parity-import.ts` refuses a batch with no
`MANIFEST.md`, and frames are never mapped from file names. When its manifest lands, the mapping table
gains rows and the importer runs again; nothing else has to change.

### 10.1 The importer (`work/parity-import.ts`)

```powershell
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md [--replace] [--link] [--dry-run] [--json]
```

It takes a manifest plus an authored mapping table (`state`, `theme`, `background`, `resolution`,
`source`, `note`) and writes each frame into the reference root under the contract name, with the
`.state.json` provenance sidecar. Every value that came from the manifest is **re-read from the
manifest**, not retyped: capture date from the `- Session:` line, RPCS3 build from the `| RPCS3 build |`
row, firmware from the `PS3 4.93 XMB` statement, renderer/window preset from the `| Video |` and
`| Window |` rows, expected frame size from `every image is 1920x1080`. The sidecar carries the
contract's fields (`cell`, `kind`, `captured`, `firmware`, `rpcs3Build`, `resolution`, `preset`, `clock`,
`sha256`, `note`) plus the audit fields the contract does not require but a reader does: `source`
(manifest, file, sha256), `destination` (path, sha256), `config`, `conversion: "none"`,
`manifestRef`, `mapping` (file + entry index), `imported`, `tool`.

**It refuses (exit 2, nothing written - the whole batch is validated before a byte is copied):**

- a state id `work/parity-catalogue.json` does not define;
- a theme/background/resolution token `work/parity-reference.json` does not define;
- two mapping rows for one cell, or a cell that already has a reference (its PNG, its sidecar, or a
  `registered` entry) unless `--replace`;
- a cell backed by a `registered` reference, **even with `--replace`**: retiring a registered entry is a
  contract change, not an import (see 10.7);
- a missing manifest, or an incomplete one (no capture date, RPCS3 build, firmware version, renderer
  preset or expected frame size) - the refusal names each missing field;
- a source frame that is missing, is not a `.png`, or that the manifest does not name (neither the file
  nor its directory appears in the manifest text);
- a frame whose own dimensions differ from the manifest's expected frame size.

**It skips (exit 1, recorded in the map, that cell untouched):** a frame whose dimensions differ from the
*cell's* resolution. A 720p cell needs a 1280x720 frame; the batch's frames are 1920x1080, so a 720p row
is skipped with the message `frame is 1920x1080, the 720p cell is 1280x720: skipped, nothing is
rescaled`. Nothing is ever rescaled, here or downstream.

The commits are copies (or `--link` hard links) whose source and destination sha256 are compared and must
match: the importer has no conversion path, so a differing hash is a refusal, not a warning. A `--dry-run`
validates and prints without touching the corpus, the map or the root.

### 10.2 The mapping that was applied

`work/parity-references-map.json` is the committed record: one entry per cell, with the source path, the
manifest, the authored note, and the `applied` block the importer rewrote (cell, reference, provenance,
source sha256, destination sha256, `manifestRef`, import time). Each note cites the manifest section the
mapping was authored from.

| Cell | Source frame | Manifest names it | Native state | Caveat class |
|---|---|---|---|---|
| `root__original__night__1080p` | `dialogs/root-clock-mmdd-12h.png` | exact (4.1) | root, Game column | content + clock + pin (filed at night) |
| `column-user__original__day__1080p` | `nav/users-column.png` | group (4.5 `nav/`) | Users column | pin + clock |
| `column-settings__original__day__1080p` | `settings/column-system-update.png` | exact (4.2) | Settings column | pin + clock |
| `column-photo__original__day__1080p` | `media/photo-column-empty.png` | exact (4.4) | Photo column | pin + content |
| `column-music__original__day__1080p` | `media/music-column-empty.png` | exact (4.4) | Music column | pin + content |
| `column-video__original__day__1080p` | `media/video-column-empty.png` | exact (4.4) | Video column | pin + content |
| `page-settings-system__original__day__1080p` | `settings/page-system-settings.png` | group (4.2 rows 2-16) | Settings > System page | pin + clock + content |
| `osk__original__day__1080p` | `osk/osk-default-focus.png` | exact (4.3) | OSK panel | pin + page beneath |
| `options-panel__original__day__1080p` | `dialogs/options-open-01.png` | exact (4.1) | options menu | pin + row set + page beneath |

Two frames are `manifestRef: group`, not exact: the settings-page group is written as a
`page-game-settings.png` ... `page-network-settings.png` range (so the file name for row 6 is not in the
manifest - the page identity was checked on the frame), and `nav/` is described as a group of 24
state-check frames. The importer records which it is rather than silently treating every row as exact.

### 10.3 Coverage after the import

```powershell
bun work/parity.ts --references-missing          # 9/25 canonical cells present (was 1/25)
bun work/parity.ts --references-missing --matrix # 10/600 matrix cells present (was 1/600)
bun work/parity.ts --score --run out/parity/run-20261027-222734
```

The tenth matrix cell is the matched root frame at `root__original__night__1080p`; the canonical root cell
still pairs with the registered pre-contract frame, which is why canonical coverage is 9 and not 10.

Re-scoring the canonical-cell run `run-20261027-222734` (the 25-state run this document's section 6
measured) writes `out/parity/scorecards/scorecard-20261027-230329.{json,md}`:

| Tier | Before this import | After |
|---|---:|---:|
| **A. dumps** | 19 / 25 | 19 / 25 (same run: nothing was re-captured) |
| **B. references, pass** | 0 / 25 | **0 / 25** |
| **B. references, measured at all** | 1 / 25 | **9 / 25** |
| **C. traces** | 6 / 25 | 6 / 25 |
| **A+B+C** | 0 / 25 | 0 / 25 |

Tier B still passes nothing, and that is the correct reading: the newly referenced cells are
state-matched and pin-mismatched, so their numbers are content-difference numbers, not fidelity numbers.
What changed is that the pipeline now runs on 9 real same-state firmware frames instead of one.

### 10.4 The Tier B numbers that are now real

Measured 2026-10-27 against run `run-20261027-222734` (native executable
`0a82595e...347a2`, pins `render 30 s` / `ui 2026-10-25T12:32:00` / `focus 2000 ms`). Budgets are
unchanged from `work/parity-reference.json` (`icon-heavy` 20 dB / 0.90, `text-heavy` 18 / 0.85,
`composite` 18 / 0.88). "Surface" says whether the native frame for that cell is the same screen as the
reference; where it is not, the number compares two screens and must not be read as fidelity.

| Cell | Surface | Frame | Gating region 1 | Gating region 2 |
|---|---|---|---|---|
| `root` (registered legacy) | yes (content differs) | 18.34 / 0.8314 | top background 20.22 / 0.9336 **PASS** | category icons 20.89 / 0.8966 FAIL; item row 11.94 / 0.3909 FAIL |
| `column-user` | yes | 10.74 / 0.2408 | category icons 9.63 / 0.0549 | item row 8.35 / 0.2358 |
| `column-settings` | yes | 10.63 / 0.2528 | category icons 9.68 / 0.0717 | item row 7.36 / 0.0904 |
| `column-photo` | yes | 10.49 / 0.2181 | category icons 9.67 / 0.0760 | item row 7.17 / 0.0378 |
| `column-music` | yes | 10.50 / 0.2177 | category icons 9.50 / 0.0596 | item row 7.32 / 0.0464 |
| `column-video` | yes | 10.45 / 0.2147 | category icons 9.53 / 0.0507 | item row 7.48 / 0.0751 |
| `page-settings-system` | **no** (native frame is the System Update page) | 10.57 / 0.2606 | page rows 11.01 / 0.3315 | category icons 9.72 / 0.0217 (not gating) |
| `osk` | panel yes, page no | 15.29 / 0.2395 | osk panel 13.96 / 0.4684 | - |
| `options-panel` | **no** (native frame is the "Nothing to continue." message dialog) | 17.68 / 0.4932 | options panel 18.40 / 0.7098 | - |

Three readings that matter:

- **The 10-11 dB column numbers are mostly the day/night pin, not fidelity.** The native canonical
  capture is a bright daylight wave (`background-clock 2026-10-25T10:00:00`); every firmware frame in
  this batch is a 21:3x-21:5x frame, which is the firmware's night wave. The whole-frame and icon-row
  regions are background-dominated, so the loss is dominated by a pin the reference does not share. The
  numbers are recorded because a recorded number with a stated caveat beats a blank, but they are not a
  measurement of the native build's columns.
- **`options-panel`'s 18.40 dB on the panel region is the most misleading number in the table.** It clears
  the 18 dB PSNR half of the budget while SSIM is 0.71 against 0.88, and it compares a firmware options
  band over the Game column with the native message dialog (the cell's own capture missed the Video
  Library page - Tier A FAIL on the same cell). It fails on SSIM and it is not a surface comparison; it
  should be retired by a re-capture of `options-panel`, not by a budget change.
- **The OSK panel is the one overlay whose geometry the manifest pins exactly** (x 493-1427, y 282-755),
  and its 13.96 dB / 0.4684 measures the panel art and key grid, with the page beneath differing by
  construction. It is the most directly useful new number in the table.

### 10.5 Mismatch-by-construction, per cell

No frame in either batch records the console's **Theme** setting (the manifest captures the Theme page
and the choice list but never says which theme is selected). `original` is the stock default and the only
assumption that fits a stock rig; it is an assumption, stated here, not a verified selection. Every
imported cell is otherwise mismatched as follows:

| Cell | Background pin | Console clock | Content difference |
|---|---|---|---|
| `root__original__night__1080p` | night (21:53) - the cell is filed at night, so the pin is correct | MM/DD 12-hour, 9:53 PM | Game column holds PlayStation Store / Saved Data Utility (PS3) / Corrupted Data; the native root holds the Steam Store rows |
| `column-user` | frame is night, cell token is day | MM/DD 12-hour, 9:47 PM | console rows `Turn Off System` / `Create New User` plus a `*User` row; the native column draws `Create New User` / `User` |
| `column-settings` | frame is night, cell token is day | **DD/MM 24-hour**, 21:39 | console expands the focused first row (title + one-line help + icon plate) and lists Game Settings ... Theme Settings; the native column draws its own row treatment |
| `column-photo` | frame is night, cell token is day | MM/DD 12-hour, 9:53 PM | console has no photos (`Search for Media Servers` / `Photo Gallery` / `Playlists`); the native column lists the host's photo folder |
| `column-music` | frame is night, cell token is day | MM/DD 12-hour (same walk as photo) | console shows `Search for Media Servers` / `Playlists`; the native column lists the host's music folder |
| `column-video` | frame is night, cell token is day | MM/DD 12-hour (same walk as photo) | console shows BD Data Utility / Search for Media Servers / Video Editor & Uploader; the native column shows Continue Watching / Video Library |
| `page-settings-system` | frame is night, cell token is day | **DD/MM 24-hour**, 21:40 | console System page: Automatic Update focused with a two-line help block, then System Name, System Language, Character Set, Dictionary Type, Add/Edit Term, Delete Predictive Text Dictionary; the native page is built from sysconf_plugin RCO schemas and the host's strings |
| `osk` | frame is night, cell token is day | not readable (the dimmed parent hides the plate) | same panel geometry, but the console's title is `User Name` over the Create New User dialog while the app's is `Server URL` over the Jellyfin sign-in page |
| `options-panel` | frame is night, cell token is day | not readable (the menu band covers the plate) | console rows `Start / Delete / Information` (plus Sort By / Group Content) over the Game column; the native panel is Open / Information / Sort By / Close over a folder page |

The clock layer is therefore a **content difference by construction** on 7 of the 9 cells: 5 carry the
MM/DD 12-hour format the app renders (a format match with a different time-of-day), and 2 carry the
DD/MM 24-hour format the console was restored to, which the app never renders. The other 2 have no
readable clock at all. The `clock` field in every sidecar says which case it is, so a reader of the JSON
does not have to find this table.

**What would make these matched:** one console frame per cell taken in the day window (console clock in
the 07:00-19:00 range the day preset covers - `work/parity-reference.json` uses 10:00) with the theme
confirmed on the console, plus the app's own clock format. That is a re-run of the rig, not a change to
this document.

### 10.6 What still has no reference, and the exact capture needed next

16 canonical cells, in the order that buys the most:

| State | Class | Exact capture needed next |
|---|---|---|
| `column-tv` | icon-heavy | TV/Video Services column, day pin, signed out with no registered services |
| `column-network` | icon-heavy | Network column, day pin |
| `column-psn` | icon-heavy | PlayStation Network column, day pin (the console is signed out; the column still renders) |
| `column-friends` | icon-heavy | Friends column, day pin (signed out) |
| `page-download-management` | text-heavy | Network > Download Management page, day pin (an empty queue is enough) |
| `dialog` | composite | a message dialog with an OK action on the console - the nearest is the corrupt-item start attempt; the 2026-10-27 surface never opened one |
| `notification` | composite | any notification pop-up (a message, trophy or download event); the rig is signed out with no events |
| `ps-menu` | composite | PS button held ~1.2 s with the system menu open (a pad hold, not a key) |
| `info-indicator` | composite | PS tap with the controller indicator visible beside the clock |
| `volume-hud` | composite | the volume display at a pinned level; the console drives it from BD-remote volume keys / hardware IR, and there is no key path in the XMB |
| `media-player-panel` | composite | the console's video player panel with a playing local file: needs one video in `dev_hdd0/video` (the 2026-10-27 VFS was empty) |
| `page-game-all-games` | text-heavy | **no firmware view** - Steam library folder |
| `page-video-library` | text-heavy | **no firmware view** - the host's video folder page |
| `page-steam-store-sections` | text-heavy | **no firmware view** - Steam store sections |
| `page-jellyfin-signin` | text-heavy | **no firmware view** - a registered Jellyfin service |
| `continue-watching` | text-heavy | **no firmware view** - the native resume page |

The last five should stay "no reference" **by design**, not "no reference yet": they are native surfaces
with no firmware counterpart (`xmbml: null` in the catalogue, or a native page body under a firmware row).
The scorecard cannot say that today - `--references-missing` lumps them in with the 11 capturable ones -
so a small future change (a `noFirmwareReference` marker in the catalogue, or a separate list in the
tool's report) would make the distinction visible. That is a catalogue/report change, not made here.

The 11 firmware-capturable states are all reachable in principle; 6 of them (`dialog`, `notification`,
`ps-menu`, `info-indicator`, `volume-hud`, `media-player-panel`) need an event or an input path the
2026-10-27 rig did not have, which is why "no reference" there is a harness gap and not a native gap.

### 10.7 A contract change this import needs, reported not made

`work/parity.ts` resolves a reference cell by checking `registered` **before** the contract path
(`referencePathFor`, `work/parity.ts:1058-1062`). The `root__original__day__1080p` cell is registered to
the pre-contract frame `out/native-reference/ab-rpcs3-root.png`, so a new matched root frame written to
`out/parity-references/root__original__day__1080p.png` would never be paired - the registered path wins
and the imported PNG would be dead weight. The importer therefore refuses that cell even with
`--replace`, and the matched root frame from this batch is filed at `root__original__night__1080p`
instead (a correct cell: the frame was taken at 21:53).

Retiring the registered entry is a one-way, deliberate contract change: remove the entry from
`work/parity-reference.json` `registered`, move its sidecar to the contract name (the sidecar already
lives at `out/parity-references/root__original__day__1080p.state.json` while the PNG lives under
`out/native-reference/`, so the two halves are already split), and re-import the matched frame. Either
that, or extend `registered` with an explicit `retired: true` / priority field so a new capture can
supersede a registered one without deleting the history. **Not done here** - the brief forbids contract
edits, and the ledger should record that decision with whoever owns `work/parity-reference.json`.

No budget, matrix axis, catalogue state or scorecard threshold was changed by this import.

## 11. Second corpus import - 2026-10-27 (batch `rpcs3-ref2-2026-10-28`)

Section 10 refused the second batch because it had no manifest while it was being written. The manifest
landed at 23:25 local, and this section is the import that followed, the night run that measures the new
cells, and the list of what the batch offered that the corpus still cannot take.

The batch is 22,400 PNGs: **251 stills + 22,149 burst frames in 19 bursts**, one session in two RPCS3
boots (boot 1 22:39:07-22:47, ended by an emulator fault on the System Update path; boot 2 22:50:31-23:52),
4.93, same RPCS3 build as batch 1, all stills 1920x1080. **The console clock was left at its night time and
nothing was committed** (manifest section 8), and the theme is Original. Those two facts decide the whole
mapping: every still in this batch is a night frame, so every cell it can fill is a `night` cell.

### 11.1 The import run

```powershell
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md `
  --manifest C:/dev/xmb/out/parity-references/import/rpcs3-ref2-2026-10-28.MANIFEST.md `
  --replace
```

Result: 13 cells imported, 0 skipped, exit 0 - the nine batch-1 cells re-validated and re-copied
byte-identically (source and destination sha256 compare equal; only the `applied` blocks moved), plus four
new cells. `--replace` is needed because the batch-1 cells are occupied and the importer refuses an
occupied cell without it; nothing is converted, and a differing hash is a refusal, so the nine are
untouched pixels.

**Why an import-copy manifest, and why that is reported rather than fixed.** The batch manifest does not
carry the `- Session: <YYYY-MM-DD> <HH:MM>` line that `work/parity-import.ts` reads the capture date from.
It says `One capture session, **two RPCS3 boots**: boot 1 22:39:07 ...` with no date on that line, so the
importer refused the batch as incomplete (`exit 2`, `manifest incomplete: ... has no capture date`). Two
things were not done: the tool was not edited (the brief fences it), and the rig's manifest was not
edited (a capture record is evidence; adding a date to it after the fact is worse than refusing it). The
import instead ran against a byte-faithful copy under the gitignored `out/`:

- original `C:/dev/out/rpcs3-ref2-2026-10-28/MANIFEST.md`, 30,006 bytes, sha256 `9e0d66e5...f1ec372c`;
- import copy `out/parity-references/import/rpcs3-ref2-2026-10-28.MANIFEST.md`, 30,599 bytes, sha256
  `218c4aad...9de7d80d0` - the original bytes plus **exactly one inserted bullet** (verified by rebuilding
  the original from the copy by deleting the inserted bytes and reproducing the original hash);
- the inserted bullet's date, 2026-10-27 local, is read from the batch's own `logs/refcap-*.log` stamps
  (2026-10-27T22:39 to 23:21) and the section 8 restore audit, not chosen. 2026-10-27 local is
  2026-10-28 UTC, which is the folder name.

The provenance chain is therefore explicit: the map's batch block records both hashes, every sidecar's
`source.manifest` names the copy, and re-running the same command against a rig manifest that carries the
line replaces that field with the primary file and changes nothing else. **The rig should add the line**;
that is the one defect this import had to route around.

Two smaller readings of the same manifest, also reported and not fixed, both cosmetic in the sidecars:
`config` ends with `capture Route | Purpose | Kind` because the tool's `| Capture |` row match hits the
section 4.1 table header (this manifest labels the capture method `Stills`, so the real method is not
read), and `rpcs3Build` ends `(git 0b0fa082?)` because the manifest abbreviates the hash with an ellipsis
and the tool transliterates non-ASCII to `?`. Neither touches a cell, a hash or a number.

One reading of the batch worth stating because it is the opposite of batch 1's: the theme token is
**evidence, not assumption**. Batch 1 could only assume `original`; batch 2 has
`lists/theme-page-rest.png` showing `Theme`, `Color`, `Background` and `Font` all reading `Original` at
22:52, and section 8 records that every list was opened and cancelled. The batch-1 rows keep their
assumption and their `note` says so; the new rows cite the frame.

### 11.2 The mapping that was applied

| Cell | Source frame | Manifest names it | Native state | Caveat class |
|---|---|---|---|---|
| `column-network__original__night__1080p` | `nav/col-01.png` | group (4.7 `nav/`, 41 stills) | Network column | pin nearest (22:45) + clock format/time + row layout |
| `column-psn__original__night__1080p` | `nav/col-02.png` | group (4.7 `nav/`) | PlayStation Network column | pin nearest (22:45) + clock format/time + signed-out content |
| `column-friends__original__night__1080p` | `nav/col-03.png` | group (4.7 `nav/`) | Friends column | pin nearest (22:45) + clock format/time + empty column |
| `page-settings-system__original__night__1080p` | `tails/system-page-top.png` | exact (4.6) | Settings > System Settings page | clock format/time + row values; pin inside the night window |

Each row's `note` in `work/parity-references-map.json` cites the manifest section it was authored from and
names its caveats; the sidecar `clock` field carries the frame's own clock (27/10 22:45 for the three
columns, 27/10 23:05 for the System page) so a reader of the JSON does not need this table.

Why these four and no more:

- **The three columns have no reference at all**, not at the canonical cell and not anywhere. That is new
  coverage of a state, which is what the corpus is short of.
- **The System page tail is this batch's own documented capture target** (manifest 4.6 lists all 25 rows
  and the last row), it reaches the page by the catalogue's own route (Settings column, row 6, Cross), and
  the state's only existing reference is a night frame filed at a day cell - so this is the first
  correctly labelled reference for that state. It is a second cell, not a replacement: the canonical day
  cell keeps the batch-1 frame.
- **Nothing was filed at a day cell.** Every frame here is 22:39-23:21 with a DD/MM 24-hour clock, and only
  the 23:05 frame is inside the contract's night window (23:00-01:00); the 22:45 column frames are 15
  minutes before it. `night` is the cell the clock supports; `day` would be the batch-1 mistake repeated,
  and a cell token a frame contradicts is exactly what this corpus must not add.
- **The `nav/colL-*.png` left-walk was not imported.** It is PSN, Network, Game, Video, Music, Photo,
  Settings, Users, Users at 22:46 (verified frame by frame). The PSN/Network frames duplicate the cells
  above; the other five are night twins of states that already have a batch-1 reference, and their focused
  row differs from the canonical route's row 0, so each would be a second reference for a state that
  already has one. They are available (section 11.7, item 7) if the corpus decides it wants a night
  reference for every column.

### 11.3 Coverage after the import

```powershell
bun work/parity.ts --references-missing          # 9/25 canonical cells present (unchanged)
bun work/parity.ts --references-missing --matrix # 14/600 matrix cells present (was 10/600)
bun work/parity.ts --score --run out/parity/run-20261027-222734
```

| Measure | Before section 11 | After |
|---|---:|---:|
| canonical (`original/day/1080p`) cells with a reference | 9 / 25 | **9 / 25** |
| matrix cells with a reference | 10 / 600 | **14 / 600** |

The canonical count does not move, and that is the correct reading rather than a failure of the import:
the canonical cell is `original/day/1080p`, no frame in this batch is a day frame, and filing a night frame
there would buy a number at the price of a false label. What moved is the matrix (four night cells) and
what states have a reference at all (three: `column-network`, `column-psn`, `column-friends`).

`--references-missing` still lists those three states, because it asks for their canonical cell. That is
the same limitation section 10.6 names from the other side: the report counts cells, not "does this state
have a reference anywhere". It is worth fixing in the tool, and it is not fixed here.

### 11.4 The Tier B numbers for the new cells

The existing run is a day run (`run-20261027-222734`, 25 canonical cells), and the scorer pairs a capture
with the reference whose basename equals the cell id, so it cannot pair the new night references with it -
there is no cell to pair. The night cells were therefore captured at their own pin, which is the only
honest way to measure them:

```powershell
bun work/parity.ts --capture --score --background night `
  --state column-network,column-psn,column-friends,page-settings-system
```

Run `out/parity/run-20261027-233820`, scorecard `out/parity/scorecards/scorecard-20261027-234027.{json,md}`;
build `0a82595e...347a2` (the same 20:47 snapshot); pins unchanged - render clock 30 s, UI clock
`2026-10-25T12:32:00`, focus 2000 ms - and the only axis that changed is the background (night,
`2026-10-25T00:00:00`). A first 3-cell run (`run-20261027-233653`, scorecard `...233755`) produced the
same numbers for the three columns, digit for digit, so the new cells are as repeatable as the old ones.

| Cell | Tier A | Frame | Gating region 1 | Gating region 2 |
|---|---|---|---|---|
| `column-network` night | **PASS** (category network, 5 rows, row "Online Instruction Manuals") | 16.89 dB / 0.3883 | category icon row 19.47 / 0.1365 FAIL | item label row 9.27 / 0.0883 FAIL |
| `column-psn` night | **PASS** (category psn, 3 rows, row "Sign In") | 16.17 / 0.3767 | category icon row 17.90 / 0.1158 FAIL | item label row 8.73 / 0.0797 FAIL |
| `column-friends` night | **PASS** (category friends, 3 rows, row "Open Steam Friends") | 16.17 / 0.3704 | category icon row 21.22 / 0.1296 FAIL | item label row 8.50 / 0.0577 FAIL |
| `page-settings-system` night | **FAIL** (route; landed on the System Update page, 2 rows, "Update via Internet") | 17.63 / 0.3790 | page rows 15.00 / 0.4745 FAIL | category icon row 22.01 / 0.0778 (not gating) |

Budgets are the class budgets from `work/parity-reference.json` (`icon-heavy` 20 dB / 0.90,
`text-heavy` 18 dB / 0.85); no budget changed.

**What is genuine in those numbers.** For the three columns, this is the first Tier B measurement in the
corpus where the reference and the capture agree on the cell rather than only on the state: same state
(Tier A PASS), same theme (`original`; the console side evidenced by the Theme page frame), same
background token (`night`), same resolution (1920x1080), same build, firmware and rig. The numbers are
perceptual distances between two frames of the same screen, which is what Tier B is for. The row sets
partly agree and partly do not, and that is itself part of the measurement: the console's Network column
shows the firmware's own items (`Online Instruction Manuals`, `Remote Play`, `Internet Browser`,
`Internet Search`, in the console's around-the-band layout) over the native column's five rows ending
`Online Instruction Manuals`, while the console's signed-out PSN column lists `Sign Up`, `What's New`,
`Trophy Collection`, `PlayStation Plus`, `PlayStation Store` against the native column's three rows, and
the console's Friends column is empty against the native column's three Steam rows. The last two are a
**content difference by construction** (signed out, no PSN account, no friends - manifest section 5
item 10), not a defect the number can attribute.

**What is mismatch-by-construction, and must not be read as fidelity.**

- **The clock string.** The console renders `27/10 22:45` (DD/MM, 24-hour); the app renders
  `10/25 12:32 PM` at its pinned UI clock. That is a content difference on the clock band, and for the
  column states the clock is not a required region, so it does not gate; it still contributes to the frame
  number.
- **The wave's animation phase.** The console frame freezes the wave wherever it was at the grab; the app
  is pinned at render clock 30 s. Two stills of an animated wave at different phases differ structurally,
  and no capture recipe removes that. It is part of the SSIM, and it is why an SSIM of 0.37-0.39 on a
  matched cell is not by itself a rendering verdict.
- **The pin is nearest, not exact, for the three columns.** The 22:45 frames are 15 minutes before the
  contract's night window (23:00-01:00, held at 00:00), so the console's wave is its own evening
  interpolation and the app's is the 00:00 night preset. The token is right and the wave is not bit-equal;
  the System page frame at 23:05 is inside the window and does not carry that caveat.
- **The `page-settings-system` number is not a surface comparison at all.** The reference is the System
  Settings page; the capture landed on the System Update page after three attempts (the DOWN-route defect
  in 1.4, still present on this pre-fix build), so 17.63 dB / 0.3790 compares two screens. It is in the
  table because a recorded number with a stated caveat beats a blank, and because it is the second
  demonstration of the trap 10.4 named: PSNR nearly clears the 18 dB text budget while SSIM is 0.38, so a
  PSNR-only gate would call a wrong page nearly matched. It must be retired by a capture on a build whose
  DOWN route works, not by a budget change.
- **Not by construction, and the interesting residue.** The item-label regions are 8.5-9.3 dB with SSIM
  0.06-0.09. For PSN and Friends part of that is content (the row sets above); for Network the rows are
  the same and the residue is geometry: the console's items sit around the category icon row (the focused
  item's neighbours above and below the band) while the native column stacks them below the band. That is
  a real layout difference this reference is meant to expose, and it is the harshest number in the table.

**What the night numbers say about the day-cell numbers.** The batch-1 day-cell column references score
10.45-10.74 dB on the frame; the new night-cell columns score 16.17-16.89 dB. The states and the console
frames differ, so this is not a controlled experiment and is not offered as one - but it is consistent
with 10.4's reading that the day/night pin dominated those 10-11 dB numbers, and it says the first thing
to remove from the column pairs is the pin, not the icon art. The one same-state pair available
(`page-settings-system`) moves 10.57 -> 17.63 dB when only the native pin changes (day run vs night run,
same reference page content, both captures on the same wrong page), which points the same way while
remaining a PSNR-only observation.

**The canonical number set did not move.** Rescoring `run-20261027-222734` after the import wrote
`scorecard-20261027-233626.{json,md}`: Tier A 19/25, Tier B 0/25 pass with 9/25 measured, Tier C 6/25,
A+B+C 0/25 - identical to `scorecard-20261027-230329`. Nothing was re-captured on the canonical side, no
canonical cell gained a reference, and every re-imported batch-1 file is byte-identical, so an unchanged
number is the expected result.

### 11.5 What the batch offered that the corpus cannot take, and why

Refusals are grouped by the reason, because the reasons are different problems with different owners.

| Item | Frames | Why refused | Owner |
|---|---:|---|---|
| The bursts | 19 bursts, 22,149 frames | A burst is not a still, and every burst is either a full-frame series of one animation or a **1:1 crop** of an animated region (`rect.txt`); a crop is not a full frame and cannot be a cell. Never usable as a state reference. | nobody - correct by design |
| `error/update2-settle-01..04.png`, `error/update-settle-*.png` | 8 | The frame is a **frozen firmware surface with the RPCS3 host overlay** `The PS3 application has likely crashed, you can close it.` (vsh died on the update path). A host overlay is not the console's UI. | nobody - the path is blocked in the emulator |
| `error/page-at-rest.png`, `error/ictest*`, `error/icp-page-rest.png`, `error/retry-t0*.png`, `error/page-after-*.png`, `error/page-dismissed.png`, `error/back-to-xmb.png`, `error/column-corrupted-focused.png` | ~45 | Real firmware surfaces (Internet Connection Test with `An Ethernet cable is not connected.`; the `Internet Connection Settings` message page; the Game column with `Corrupted Data` focused), but **no state in `work/parity-catalogue.json`** matches them. The importer refuses a state id the catalogue does not define, and inventing a state is a catalogue change, which is fenced here. | catalogue owner |
| `depth2/*` (16) | 16 | `Settings > Network Settings` (depth 1), `Settings and Connection Status List` (depth 2) and the `Internet Connection Settings` message page. No catalogue state, and the route is Settings row 16, **not** Network > Download Management - so they cannot stand in for `page-download-management`; that would be a page reached through a different route. | catalogue owner |
| `lists/*` (47, incl. 3 contact sheets) | 47 | The Theme/Color/Background/Font lists and the Theme Settings page. No catalogue state (the catalogue's only settings page is Settings > System Settings), and the frame is not that page. The one piece of evidence this import *did* use from this group is `lists/theme-page-rest.png`, cited for the theme token. | catalogue owner |
| `tails/video-page-top.png`, `tails/video-down1..22.png` | 23 | The Video Settings page (19 rows documented in 4.6). No catalogue state. The manifest's row list is the real asset here; there is no cell to file the frame under. | catalogue owner |
| `tails/system-down1..20.png` | 20 | The same state as the imported top-of-page frame. **One cell is one file**, so only the frame that shows the page at rest is used; the scroll series is the same cell twenty times. | mapping decision |
| `nav/col-04.png` .. `col-06.png` | 3 | The Friends clamp repeated (manifest 4.7: Friends is the right-hand edge). Duplicates of `column-friends`. | mapping decision |
| `nav/colL-1..9.png` | 9 | PSN, Network, Game, Video, Music, Photo, Settings, Users, Users at 22:46 (checked on every frame). The first two duplicate the imported cells; the other seven are night twins of states that already have a reference, and their focused row differs from the canonical route's row 0. | mapping decision |
| `menus/*` | 13 (4 bursts excluded) | Options-menu open/closed checks over the Game column at night. `options-panel` already has a reference and its state is the menu over the **Video Library** page, not over the Game column. The bursts are crops. | mapping decision + catalogue |
| `bands/*` | 14 (5 bursts excluded) | Theme and Screensaver choice bands at rest and after close - timing evidence for `audit/THEMES.md`, not a catalogue state. | catalogue owner |
| `nav/boot-*.png`, `nav/keytest-*.png`, `nav/recover-*.png`, `nav/game-0*.png`, `nav2/boot-01.png`, `nav2/settings-*.png` | ~25 | Boot frames, key-map probes and route state checks. `nav2/settings-column-*` is the Settings column at 23:0x and is the one genuine column state in that group, but `column-settings` already has a reference (the same night-twin argument). | mapping decision |
| `column-tv` | 0 | **The console has no TV/Video Services column on this rig**: the column map is Users, Settings, Photo, Music, Video, Game, Network, PlayStation Network, Friends (manifest 4.7, checked on the frames). There is nothing to file. | rig state |

The count that matters: of 251 stills, **four** back a cell. The rest are either evidence for an analysis
(the bands, the timing bursts, the error pages), duplicates of cells, or material the catalogue cannot name
yet. The bottleneck has moved from the rig to the catalogue, which is the finding of this section.

### 11.6 Which states still cannot pass Tier B, and what a matched capture needs

Nothing in this batch is a day frame, so no canonical cell changed. Every canonical cell that is still not
a matched reference, plus the night-only references this batch added, splits like this:

| State | Canonical day cell | New night cell | Why it still cannot pass Tier B |
|---|---|---|---|
| `root` | registered legacy frame (2026-10-25, DD/MM 24-hour, Corrupted Data content) | batch-1 frame | **mismatch by construction**: content, clock format and time; theme assumed. The registered entry cannot be superseded without a contract change (10.7). |
| `column-user`, `column-settings`, `column-photo`, `column-music`, `column-video` | batch-1 **night** frames | none | **day-vs-night mismatch by construction** on the background, plus the DD/MM 24-hour clock and content rows. The reference is a night frame wearing a day label. |
| `column-network`, `column-psn`, `column-friends` | **no reference** | new, measured | the canonical cell is empty. The night cell is a genuine cell comparison (11.4): Network fails on layout, PSN and Friends on layout plus their signed-out row sets; none fails on the pin. |
| `page-settings-system` | batch-1 night frame | new, measured | the night cell is pinned correctly, but the native capture landed on the System Update page (route defect), so the number is not a surface comparison. |
| `osk`, `options-panel` | batch-1 night frames | none | **day-vs-night mismatch**, plus a different page under the overlay in both frames. |
| `dialog`, `notification`, `volume-hud`, `info-indicator`, `ps-menu`, `media-player-panel` | none | none | no capture: the rig had no event/input path (1.4, 10.6). |
| `page-download-management` | none | none | no capture: the route needs a DOWN move the build cancels, and the batch's Network Settings page is a different route. |
| `page-game-all-games`, `page-video-library`, `page-steam-store-sections`, `page-jellyfin-signin`, `continue-watching` | none | none | **no firmware view** (`xmbml: null` or a native page body); no reference by design. |

**Theme.** No cell in the corpus is theme-mismatched today: the app's canonical capture is `original`, and
every imported frame is `original` too. But the console side is *evidenced* only for batch 2 (the Theme
page shows `Original`) and *assumed* for batch 1. A capture taken after a theme change would be
mismatch-by-construction on the theme axis; none exists yet, and the `classic` and `p3t-01` theme cells
have no reference at all (0 of 200 each, 400 cells between them).

**What a matched capture needs, exactly.** For any canonical cell:

1. **Console clock inside the day window.** The contract's `day` pin is 10:00 (the traced daylight key,
   `work/parity-reference.json` matrix). Set the console clock to about 10:00 local and leave it there for
   the grab; a 21:xx or 22:xx frame is a night wave and belongs at a night cell, however convenient it is.
2. **Clock format the app renders.** `Date Format = MM/DD/YYYY`, `Time Format = 12-Hour Clock` on the
   console (batch 1 set it temporarily and restored it at 21:58; batch 2 ran at DD/MM 24-hour). Without
   this the clock band is a content difference by construction even when the time of day matches, and the
   app has no DD/MM 24-hour renderer to compare against.
3. **Theme confirmed on the frame.** One frame per session of `Settings > Theme Settings` showing
   `Theme = Original`, `Color = Original`, `Background = Original`, `Font = Original`, so the sidecar's
   theme token is evidence and not an assumption. Batch 2's `lists/theme-page-rest.png` is the pattern.
4. **The cell's preset, not an approximation.** Background preset = the cell's clock (day = 10:00, not
   22:45); resolution 1920x1080 for a 1080p cell (1280x720 windowed for 720p, which no console session has
   produced); Vulkan, exclusive fullscreen disabled, borderless client area exactly 1920x1080, clock scale
   100, settled two seconds after the last key.
5. **The same state by the catalogue route.** The route in `work/parity-catalogue.json`, key for key, so
   Tier A can pass - the System page by Settings row 6, the download page by Network > Download
   Management, not by whatever page happens to be nearby.
6. **A profile/content set that is either the same as the native seed or documented.** The console's empty
   media columns and the host's photo/music/video folders are a content difference by construction, and
   the sidecar note must say so.

For the six event-dependent composites (`dialog`, `notification`, `volume-hud`, `info-indicator`,
`ps-menu`, `media-player-panel`) the missing piece is the event, not the pin: a message dialog with an OK
action, a notification event, hardware/remote volume keys or BD-remote HID, a PS tap with the controller
indicator, and a playing local file. Those are harness gaps on the native side as much as on the rig's.

### 11.7 Next capture, in the order that buys the most (supersedes 10.6)

1. **One day-window session, 10:00 local, MM/DD + 12-hour, theme confirmed, for the nine canonical cells
   that currently hold night frames**: `root` (which also needs the registered-entry decision in 10.7),
   `column-user`, `column-settings`, `column-photo`, `column-music`, `column-video`, `page-settings-system`,
   `osk`, `options-panel`. This is the highest-value re-run in the corpus: it turns nine
   mismatch-by-construction references into matched ones and is the only way any of them can pass Tier B.
2. **The same session, three more canonical cells**: `column-network`, `column-psn`, `column-friends` -
   the day counterparts of the new night cells. Their night cells now exist, so this closes the day side.
3. **`column-tv`** needs a console state that has a TV/Video Services column (a registered service); this
   rig does not produce one. Until then, no reference.
4. **The six event-dependent composites** - harness work first (a dialog, a notification, a pad hold, a
   volume key path, a playing file), then one frame each at a pinned cell. Not a rig-priority item.
5. **The five native-only pages** (`page-game-all-games`, `page-video-library`,
   `page-steam-store-sections`, `page-jellyfin-signin`, `continue-watching`): no firmware reference exists
   or should; the catalogue needs a `noFirmwareReference` marker so `--references-missing` stops counting
   them as owed (10.6). **Done 2026-10-28: section 12.4.**
6. **A catalogue extension, before any further rig work.** Batch 2 captured ten surfaces the catalogue
   cannot name: Settings > Network Settings (depth 1), Settings and Connection Status List (depth 2), the
   `Internet Connection Settings` message page, the Internet Connection Test error page, the Theme Settings
   page, the Theme list, the Color list, the Background list, the Font list, and the Video Settings page.
   Adding them as states (with the batches' geometries and the manifest's row lists) would let most of
   batch 2's stills - the `error/`, `depth2/`, `lists/`, `bands/` and `tails/` groups - become references,
   and would put the corpus's bottleneck back on the rig, where it can be fixed. That is a catalogue edit
   and is not made here. **Done 2026-10-28: sections 12.1-12.6 - the ten states exist, ten cells were
   imported, and the bottleneck is back on the rig (the day-window session in item 1 is now the whole
   remaining gap for these surfaces).**
7. **Night cells for the five columns that already have a reference** (from `nav/colL-4..8.png`, 22:46),
   if the corpus wants a night reference for every column: five map rows and one `--capture --background
   night` run, with the focused-row caveat recorded. Optional; it adds cells, not states.
8. **720p cells**: 0 of 300 have a reference; the host has no 720p display mode, so this is P10's problem
   before it is the rig's.

### 11.8 Where the repository now differs

- `work/parity-references-map.json`: 13 entries, 13 applied, 0 skipped; the batch-2 block is now
  `status: imported` with both manifest hashes and the reason for the import copy; four new rows at the
  end; the import re-ran `--replace` so every entry carries a fresh `applied` block.
- `out/parity-references/`: +4 PNGs and +4 `.state.json` sidecars (`column-network`, `column-psn`,
  `column-friends`, `page-settings-system`, all `original/night/1080p`), plus
  `import/rpcs3-ref2-2026-10-28.MANIFEST.md` (the disclosed import copy). All gitignored.
- `out/parity/`: `run-20261027-233820` (the night run, 4 cells) and its scorecard
  `scorecard-20261027-234027`; the earlier 3-cell repeat `run-20261027-233653` / `scorecard-20261027-233755`;
  and `scorecard-20261027-233626` (the canonical run rescored). All gitignored.
- No change to `work/parity.ts`, `work/parity-catalogue.json`, `work/parity-reference.json`,
  `work/parity-import.ts`, any budget, any matrix axis, or any catalogue state.

**Not a parity claim.** Four cells gained a reference; none gained a pass. Tier B still passes nothing,
and every number in 11.4 either carries a named mismatch-by-construction or, for the System page, compares
the wrong page. The corpus is larger and better labelled, not closer to 1:1.

## 12. Catalogue extension - 2026-10-28 (the surfaces batch 2 captured)

Section 11.5 ended with the finding this section acts on: of batch 2's 251 stills, four backed a cell, and
the rest were refused for one reason above all others - **no catalogue state named the surface**. The
bottleneck had moved from the rig to the catalogue. This section adds the ten states that fix it, marks the
five native-only pages so the reference debt stops counting them, adds the one tool change the marking
needs, captures every new state on two builds, and imports the ten night cells the new states make
possible.

Dated 2026-10-28 local (America/Chicago), which is also the batch folder's date (2026-10-27 local is
2026-10-28 UTC). No budget, no matrix axis, no reference contract field and no pre-existing state was
changed.

### 12.1 The ten states

Every route is **resolved, not typed**: `work/parity.ts` reads the columns and row indices out of one
discovery dump per data seed, so the numbers below are what the tool found (`catalogue:network[15]` means
the discovery dump listed Network Settings as row 15 of the Settings column), not offsets written into the
catalogue.

| State | Class | Native route (resolved) | Firmware surface it names | Regions |
|---|---|---|---|---|
| `page-settings-network` | text-heavy | Settings column > `network[15]` | the depth-1 Network Settings page, 5 rows from `seg_network` (SETTINGS 5.15/10.3) | frame, page rows, category icon row |
| `page-network-status-list` | text-heavy | ... > `network[15]` > row 0 | Settings and Connection Status List, the depth-2 read-only table (SETTINGS 12.2) | frame, status rows, title rule |
| `page-network-test-error` | text-heavy | ... > `network[15]` > row 3 | the Internet Connection Test message page, the one real firmware error surface (BATCH-2 5.2) | frame, message body, back hint |
| `page-network-connection-settings` | text-heavy | ... > `network[15]` > row 2 | the Internet Connection Settings message page (BATCH-2 5.1c, manifest 4.2) | frame, message body, back hint |
| `page-settings-theme` | text-heavy | Settings column > `theme[6]` | the depth-1 Theme Settings page, 4 firmware rows (SETTINGS 5.6/12.4) | frame, page rows, category icon row |
| `theme-list` | composite | ... > `theme[6]` > row 0 | the Theme choice band, 4 captured labels (SETTINGS 9.3f/12.4a) | frame, band column, band rows |
| `color-list` | composite | ... > `theme[6]` > row 1 | the Color choice list, 12 unlabelled chips (SETTINGS 12.4b) | frame, band column, chip column |
| `background-list` | composite | ... > `theme[6]` > row 2 | the Background choice list, 3 items + `Brightness >` (SETTINGS 12.4c) | frame, band column, band rows |
| `font-list` | composite | ... > `theme[6]` > row 3 | the Font choice list, 3 items (SETTINGS 12.4d) | frame, band column, band rows |
| `page-settings-video` | text-heavy | Settings column > `video[2]` | the depth-1 Video Settings page, 19 firmware rows (SETTINGS 12.5) | frame, page rows, category icon row |

`page-settings-network` and `page-settings-theme`/`page-settings-video` claim `category_sysconf.xml`
`seg_network` and `seg_fixed_items` respectively, and `--catalogue` cross-checks both against the firmware
document (both views exist; exit 0, no problems). The four choice lists claim `xmbml: null` with the reason
in `xmbmlNote`: their pages are sysconf_plugin RCO page objects, not XMBML views, and the two of the four
that the RCO authors as empty (`page_theme_config_*`) are built at run time by the plugin.

### 12.2 Regions: the measured geometry they frame

No crop was invented. Each one is the frame measurement the batch's audit recorded:

| Region | Crop | Where the number comes from |
|---|---|---|
| `page rows` (depth-1 settings pages) | 400,380,1000,620 | SETTINGS 2.2: focused row centre y 505, values left at x 1301, help lines to y 564, 80 px row pitch (D24) |
| `status rows` (depth-2 table) | 400,280,800,500 | SETTINGS 12.2: rows 13, ink tops 291 -> 771, pitch 40, labels right-aligned x 939, values left x 979-980 |
| `title rule` (depth-2 table) | 80,150,1520,60 | SETTINGS 12.2: title ink y 167-185 and the 1 px rule at y 199 (the D4/D5 rule pair, with y 879 below) |
| `message body` (ICT page) | 700,470,520,100 | BATCH-2 5.2: heading ghost y 490-523, message ink x 753-1163 y 530-546 |
| `message body` (ICS page) | 500,460,900,110 | checked on `error/icp-page-rest.png` this pass: two left-aligned lines at x about 528-1390, y about 485-525 (the audit records the shape, not the ink box) |
| `back hint` (both message pages) | 1140,880,120,56 | BATCH-2 5.2: `O Back` ink x 1155-1239, y 897-916 |
| `band column` (Theme page lists) | 1324,0,420,1080 | SETTINGS 12.3: Theme band left border x 1324-1325, geometric zero about x 1744, full height |
| `band rows` / `chip column` | per list | SETTINGS 12.4e row ink tops: Theme 261/494/532/576, Background 381/537/581/621, Font 491/539/578, Color label line 494-523 then chips 531-957 with the chevron at 969-977 |

Two honest notes on the crops. First, the **band origin is per page, not a constant**:
SETTINGS-FIRMWARE 12.3(a) showed the Display page's Screensaver band starts at x 1260 and the Theme page's
at x 1324, 64 px apart, so all four of these lists use 1324 - they are opened from the Theme page - and the
1260 number is not used for them. Second, the Color/Background/Font lists' own left edges were not measured
separately; their crops borrow the Theme page's border and their rows are the measured ink tops. Both facts
are in the states' `tierANote`s, not only here.

### 12.3 Honest reachability, state by state

The ten states split into four groups by what the app actually does, and each group is stated rather than
smoothed over:

- **Same surface, same route (4 states).** `page-settings-network`, `page-settings-theme`,
  `page-settings-video` and `theme-list` are pages and a band the app really draws. Row-count differences
  are by design and asserted, not hidden: the Network page is the firmware's 5 rows exactly; the Theme page
  is the firmware's 4 rows plus 2 native-only pickers (6); the Video page is the firmware's measured 7 rows
  plus 3 native-only media rows (10) against the console's 19; the Theme band is the firmware's 4 captured
  labels, drawn from the captured list because the RCO page is empty.
- **Same route, different table (1 state).** `page-network-status-list`: the row opens a D5 information
  page whose label/value rows are the **host's own network report**, not the console's 13 fixed rows. The
  surface class matches (titled page, rule pair, label/value rows, no focus); the content does not.
- **Reached as the build's own message page (2 states).** `page-network-test-error` and
  `page-network-connection-settings`: pressing the row calls `Shell::activate`'s `Action::Unavailable`
  branch, which draws this build's message page titled with the firmware row's title and a body of
  `This feature is not available in the native recreation yet.` No connection test runs and no firmware
  string (`An Ethernet cable is not connected.`, the two-line connection advice) is reproduced, so the
  content difference is by construction and is the state's `tierANote`. The surface itself is the same D4
  shape: on the native captured frame the title rule measures **y = 198** (the firmware's is 199), the
  `O Back` hint sits in the same bottom band, and the body is this build's own string (12.5).
- **Reached as the build's own list (3 states).** `color-list`, `background-list`, `font-list`: the view
  logs that it keeps its own options panel because the firmware's page has no list of its own in the RCO,
  and the app draws 6 labelled colours / 3 backgrounds / 3 fonts where the console draws 12 unlabelled
  chips / 3 values plus a submenu / 3 named fonts.

**The Tier A scope caveat, stated once and applied where it belongs.** `Shell::showMessage` writes no
sidecar field and no log line, so no message page can be identified from the dumps. The three states that
open one (`page-network-status-list`, `page-network-test-error`,
`page-network-connection-settings`) therefore have **page-scoped Tier A**: their assertions prove the
Network Settings page and the focused row under it, not the page on top. That is the same scope the OSK
already carries (1.1), and it is the one place a future sidecar field (`messageTitle`/`messageKind`) would
close a real gap. Their Tier B regions still frame the firmware's measured rects, so the pixel half of the
comparison is not weakened by it.

### 12.4 `noFirmwareReference`, and the one tool change it needed

Five states are native surfaces with no firmware counterpart: `page-game-all-games` (the Steam library
folder), `page-video-library` (the host's video folder page), `page-steam-store-sections` (the Steam store
sections), `page-jellyfin-signin` (a registered Jellyfin service) and `continue-watching` (the native
resume page). Each now carries `"noFirmwareReference": true`. The semantics are deliberately narrow:

- they are **still captured and gated** like every other state (they stay in `--capture`, in the matrix and
  in the Tier A/C denominators - the app reaches them and their dumps are real);
- they **owe no firmware reference**, so `--references-missing` no longer lists them and the scorecard's
  reference-debt denominators exclude them. A rig frame for them must not exist; asking for one was the
  bug section 10.6 named.

`work/parity.ts` changed in five places, all in service of that field (no contract or matrix-axis change):

1. `State` gains `noFirmwareReference?: boolean`, and `loadCatalogue` rejects a non-boolean value.
2. `--catalogue` prints the flag in the state list (`native-only: no firmware surface defines this page, so
   no firmware reference is owed`) and in its JSON (`noFirmwareReference: true`), and its header line now
   reads `38 (35 capturable today, 5 native-only with no firmware reference)`.
3. `--list` gained a `fw reference owed` / `no fw reference (native-only)` column.
4. `--references-missing` now reports **three denominators**: `present/firmwareCells` (cells present out of
   the cells that owe a reference), the selected-cell total with the native-only cells named, and a
   **states** measure (`withReference/firmwareReferenced`, plus the native-only state count). That last one
   closes the limitation 11.3 recorded - the report could count cells but not answer "does this state have
   a reference anywhere". JSON gains `firmwareCells`, `states` and `noFirmwareReference`.
5. The scorecard gains `totals.noFirmwareReference`, `totals.firmwareReferenced`,
   `totals.statesWithReference`, `totals.coveredFirmware` (the firmware-referenced states the run actually
   covered, so a ten-state run does not read as "0 of 30"), a `nativeOnly` list with its own markdown
   section, a reference-coverage line in both the markdown and the console summary, and a limits line.
   `missingReferences` excludes the
   native-only cells. `totals.capturable` keeps its meaning (capture coverage), so the two denominators
   cannot be confused.

The matrix is **840 cells** (35 states); the reference debt is **720 cells** (30 states). `--matrix` still
prints 840, because those 120 cells are genuinely captured - they are just never owed a reference.

### 12.5 Capture evidence

Two runs, same routes, two builds. Neither scored Tier B (see 12.8's note on why), and both are **partial
runs**: the scorecard's `claims/capturable` denominator is the catalogue's 35 capturable states, so a
ten-state run reads `0/35` and means nothing about coverage; the per-run line to read is the reference
coverage one (`0/10 states covered by this run` here, because the ten captured cells are day cells and
every reference for these states is a night cell).

**Run 1 - the current build, Tier A only.** `bun work/parity.ts --capture --state
page-settings-network,page-network-status-list,page-network-test-error,page-network-connection-settings,
page-settings-theme,theme-list,color-list,background-list,font-list,page-settings-video --exe
C:/dev/out/native-status3/re-ps3-native.exe` -> `out/parity/run-20261028-000600`, **10/10 cells captured,
Tier A PASS for all ten**, exit 0. Build sha256 `BE55FF1E912A43956F6F5E2F78BAF5B883A79FE5A6138A3C6A4FE53CB2A631E9`
(the tree with the in-flight text-renderer regression: it draws no glyphs, which is why it is used for the
structural tier only - Tier A is dumps and logs and needs no pixels).

| State | Sidecar oracle (all PASS) | Log oracle (all PASS) |
|---|---|---|
| `page-settings-network` | category settings, depth 1, rows 5, pageKind page, row `net_netlist` ("Settings and Connection Status List") | `settings: network segment page resolved 5 rows from category_sysconf.xml` |
| `page-network-status-list` | same page and row, depth 1 (the info page does not push a page) | same line (page-scoped: see 12.3) |
| `page-network-test-error` | row `net_internet_test` ("Internet Connection Test"), rows 5 | same line (page-scoped) |
| `page-network-connection-settings` | row `net_internet_setting` ("Internet Connection Settings"), rows 5 | same line (page-scoped) |
| `page-settings-theme` | category settings, depth 1, rows 6, row `theme` | `settings: theme page from the RCO row set: 6 rows, 2 native-only` |
| `theme-list` | depth 2, rows 1, row `appearance/theme/0` ("Original") | `view: settings choice band appearance/theme -> 4 items from capture (page_theme_config_theme), current row 1, fits the band` |
| `color-list` | depth 2, rows 6, row `appearance/colour/0` | `view: settings choice panel appearance/colour keeps this build's options panel: ...` |
| `background-list` | depth 2, rows 3, row `appearance/background/0` | `view: settings choice panel appearance/background keeps this build's options panel: ...` |
| `font-list` | depth 2, rows 3, row `appearance/font/0` | `view: settings choice panel appearance/font keeps this build's options panel: ...` |
| `page-settings-video` | category settings, depth 1, rows 10, row `bd_menu_lang` | `settings: video page from the RCO row set: 10 rows, 3 native-only` |

Those log assertions are the load-bearing part, and they say what the pictures cannot: the Theme list is
drawn as the **firmware band with the four captured labels**, while the other three lists are the **build's
own panel** and the view says so. Without them the four list states would be indistinguishable in Tier A
(each only proves "a choice page opened", depth 2 and the model's row count).

**Run 2 - the pre-regression build with text, for pixel evidence.** The same command with `--exe
C:/dev/out/native-choices/re-ps3-native.exe --out out/parity-pre` -> `out/parity-pre/run-20261028-000953`.
Build sha256 `6B668353C20F3522512B13F65ED59098BAB36F71F6A5C8183BF324E10F036DF0`. All ten cells captured;
the **six page states pass Tier A** with the same summaries, and the four list states pass their sidecar
assertions (depth 2, row `Original`) but **fail only the band/panel log line** - that build predates the
Phase 13 draw hunk, so it emits no `view: settings choice ...` line at all. That is a build difference,
reported as a FAIL rather than excused away (the harness retried each list state three times, as designed).

The pixel evidence this run adds:

- `page-network-test-error/frame.png` shows a real message page: title `Internet Connection Test` with the
  globe icon, a 1 px rule at **y = 198** (measured on the frame; the firmware's is 199), the body
  `This feature is not available in the native recreation yet.` and the `O Back` hint in the bottom band.
  That is the surface the state names, with the body difference 12.3 states, visible rather than asserted.
- `theme-list/frame.png` shows the pre-regression build's own panel with one item (`Original`) where the
  current tree draws the firmware's four - the picture of the difference the log line proves in run 1.

What the two runs together say: the routes and the surfaces are right (run 1), the text and message
surfaces are right (run 2), and the only thing neither build can show is the current tree's own pixels -
which is the regression's limit, not the corpus's.

### 12.6 The import: ten more of batch 2's stills

The batch manifest still carries no `- Session:` line, so the import re-used the disclosed byte-faithful
import copy from 11.1 (verified again this pass, both hashes unchanged):

- original `C:/dev/out/rpcs3-ref2-2026-10-28/MANIFEST.md`, 30,006 bytes, sha256
  `9e0d66e5affd629b7d6971869e3570070658daa3d1ce6e62e68dcfe2f1ec372c`;
- import copy `out/parity-references/import/rpcs3-ref2-2026-10-28.MANIFEST.md`, 30,599 bytes, sha256
  `218c4aad1337030d4c0020899f3289b42b190201094fa3ebf7878099de7d80d0` - the original bytes plus exactly one
  inserted bullet.

```powershell
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md `
  --manifest C:/dev/xmb/out/parity-references/import/rpcs3-ref2-2026-10-28.MANIFEST.md --replace
```

Result: **23 cells imported, 0 skipped, exit 0** - the 13 already-imported cells re-validated and re-copied
byte-identically (source and destination sha256 compare equal; only their `applied` blocks moved), plus ten
new cells. Every new row is `manifestRef: exact` (the manifest names the file itself), not `group`.

| Cell (all `original` / `night` / `1080p`) | Source frame | Clock on the frame | Why this frame |
|---|---|---|---|
| `page-settings-network` | `depth2/network-settings-page.png` | **27/10 22:51** | the page with its focused first row expanded (manifest 4.2) |
| `page-network-status-list` | `depth2/statuslist-rest.png` | not readable (the page has no clock plate) | the 13-row table at rest, the source of 12.2's geometry |
| `page-network-test-error` | `error/page-at-rest.png` | **27/10 23:21** (inside the night window) | the error page, text at rest (manifest 4.1d) |
| `page-network-connection-settings` | `error/icp-page-rest.png` | not readable (no clock on the page) | the two-line message page, text at rest (4.1c) |
| `page-settings-theme` | `lists/theme-page-rest.png` | **27/10 22:52** | the Theme page at rest; also the batch's theme-token evidence |
| `theme-list` | `lists/theme-list-rest.png` | not readable (the band covers the plate) | the Theme band with its 4 labels |
| `color-list` | `lists/color-list-rest.png` | not readable (band) | the 12-chip list at rest |
| `background-list` | `lists/background-list-rest.png` | not readable (band) | the Background list at rest |
| `font-list` | `lists/font-list-rest.png` | not readable (band) | the Font list at rest |
| `page-settings-video` | `tails/video-page-top.png` | **27/10 23:04** (inside the night window) | the top of the 19-row page (manifest 4.6) |

Three pin readings, stated because they differ:

- **Four frames carry a readable clock**, and two of those (23:04, 23:21) are inside the contract's
  23:00-01:00 night window; the other two (22:51, 22:52) are 8-9 minutes before it, so their `night` token
  is a **nearest pin**, exactly the caveat 11.4 records for the 22:45 column frames.
- **Six frames have no readable clock at all** - the depth-2 table and the ICS message page have no clock
  plate, and the four list bands cover the plate. Their `night` token rests on the frame's own appearance
  (the night wave is visible in every one) and on the batch's session record, not on a clock string. The
  sidecar's `clock` field says exactly which case each cell is, so a reader of the JSON does not need this
  table.
- **Every cell is a night cell.** Nothing was filed at a canonical day cell, because no frame in this batch
  is a day frame. The canonical cells of all ten states therefore stay empty, and that is the correct
  reading rather than an import failure.

Theme is `original` on evidence (`lists/theme-page-rest.png` shows Theme, Color, Background and Font all
reading `Original`), not assumption. The mapping table's `note` for each row cites the manifest section and
the frame's own clock, and its batch block records which manifest was used and why.

### 12.7 Coverage before and after

```powershell
bun work/parity.ts --references-missing          # 9/30 firmware-reference canonical cells
bun work/parity.ts --references-missing --matrix # 24/720 firmware-reference matrix cells (840 cells, 5 native-only states owe 120 of them)
bun work/parity.ts --matrix                       # 840 cells = 35 states x 3 themes x 4 backgrounds x 2 resolutions
```

| Measure | Before this pass (28 states) | New catalogue, before the import | After the import |
|---|---:|---:|---:|
| canonical (`original/day/1080p`) cells with a reference | 9 / 25 | 9 / 30 | **9 / 30** |
| matrix cells with a reference | 14 / 600 | 14 / 720 | **24 / 720** |
| states with a reference somewhere | 12 (of 25) | 12 / 30 | **22 / 30** |
| native-only states that owe no reference | counted as owed | **5 (120 cells)** | 5 (120 cells) |
| catalogue states / capturable | 28 / 25 | 38 / 35 | 38 / 35 |

Readings:

- **The canonical count does not move (9/30), and its denominator grows (25 -> 30).** Ten states were added
  and every frame that references them is a night frame; filing one at a day cell would be the batch-1
  mistake repeated. The honest effect is a *lower* coverage ratio over a *correctly larger* debt.
- **The matrix moves 14 -> 24 of 720** and the state measure 12 -> 22 of 30. Both are real coverage: ten
  more states now pair with a firmware frame at their own pin.
- **The native-only five leave the debt.** Before this pass `--references-missing` listed them as missing
  firmware references; that was the bug section 10.6 predicted. They are still in the matrix (840), still
  captured, still reported - as native surfaces, not as debt.

### 12.8 What is still owed, and what would measure it

- **A day-window session is now the whole remaining gap for these surfaces**: ten canonical cells
  (`...__original__day__1080p`) and, for the two states whose new reference is a nearest-pin night frame
  (`page-settings-network` at 22:51 and `page-settings-theme` at 22:52), a 10:00 frame too. That is one rig
  run of the same routes with the console clock at 10:00 and the clock format the app renders - the recipe
  11.6 already states.
- **No Tier B number was produced this pass, deliberately.** The imported cells are night cells, so the
  canonical day run cannot pair them; a night capture on the current build has no glyphs, so its number
  would measure the text-renderer regression; and on the pre-regression build four of the ten states draw a
  different row model, so their number would compare two different lists. The first honest run is one
  command once the regression is fixed:
  `bun work/parity.ts --capture --score --background night --state page-settings-network,page-network-status-list,page-network-test-error,page-network-connection-settings,page-settings-theme,theme-list,color-list,background-list,font-list,page-settings-video`.
- **The message-page Tier A gap is a sidecar field, not a harness change.** One field each
  (`messageTitle`/`messageKind`) in `src/main.cpp`'s grab sidecar would let the three page-scoped states
  assert their surface, exactly as the OSK and info-indicator entries need theirs (1.1).
- **Eight capturable states still have no reference anywhere** (`column-tv`, `page-download-management`,
  `dialog`, `notification`, `volume-hud`, `info-indicator`, `ps-menu`, `media-player-panel`), and **21 of the
  30** firmware-reference canonical cells are empty - the ten new states' day cells plus those eight plus
  the three columns whose only reference is a night cell. The authoritative list is
  `--references-missing`'s, cell by cell.

**Not a parity claim.** Ten states and ten references were added; no cell gained a Tier B pass, no budget
moved, and the corpus is larger and better labelled, not closer to 1:1.

## 13. Day-window matched session - 2026-10-28 (batch `rpcs3-day-2026-10-28`)

Section 11.7 item 1 and section 12.8 asked for one thing: the canonical `original/day/1080p` cells captured
with the console in the state the app renders, not a night frame wearing a day label. This section is that
session. It ran once, on 2026-10-28 local (America/Chicago), 00:25:52 - 00:35:25, one RPCS3 boot, and it took
21 canonical cells off mismatch-by-construction. `root` is the exception and is named as one below.

Dated 2026-10-28 local. No budget, no matrix axis, no reference-contract field, no pre-existing state and no
tool was changed. The only edits in the repository are `work/parity-references-map.json` (the dated rows this
section describes) and this section.

### 13.1 The session, and how each axis was matched

| Axis | What was done | Evidence |
|---|---|---|
| Console clock in the day window | RPCS3 v0.0.43 has `System > Console time offset (s)` (`system_config.h:332`, added to the guest clock at `sys_time.cpp:367/382/414`); the scratch config set 34,362 s so the guest clock booted at 09:58:30 and the host clock was never touched | plate reads `10/28 10:00 AM` on the first cell and `10/28 10:07 AM` on the last; the clock was probed 70 s apart and advanced one minute |
| Date Format MM/DD/YYYY | set through the console UI: Settings row 8 `Date and Time Settings` > `Date Format` (list `YYYY/MM/DD`, `DD/MM/YYYY`, `MM/DD/YYYY`; one Down from current, Cross) | `frames/s03-datetime-page.png` (before: `DD/MM/YYYY`, plate `28/10 10:00`) -> `frames/s07-after-dateformat.png` (after: `MM/DD/YYYY`, plate `10/28 10:00`) |
| Time Format 12-Hour Clock | console UI: `Time Format` (list `12-Hour Clock`, `24-Hour Clock`; current is last, so Up selects 12-hour, Cross) | `frames/s11-after-timeformat.png` (after: `12-Hour Clock`, plate `10/28 10:00 AM`) |
| Theme Original, evidenced | Theme Settings page opened in the same session; Theme/Color/Background/Font all read `Original`; every choice list was opened and cancelled, nothing committed | `stills/page-settings-theme__original__day__1080p.png` |
| Preset | Vulkan, 1920x1080, Exclusive Fullscreen Mode: Disable, borderless client area exactly 1920x1080, Resolution Scale 100, Clocks scale 100, 2 s settle after the last key | `config/capture-config.yml`, `MANIFEST.md` section 1 |
| Same state by the catalogue route | Game column row 0 for `root`; each column at row 0; Settings rows 3/6/7/8/16 for the pages; the four lists by their own row indices; Network rows 0/2/3 for the depth-2 surfaces | `MANIFEST.md` section 3, one route per row |

61 stills were taken; 22 are the canonical cell files. No bursts: the day pin's state is a settled frame,
and the one transient surface (`page-network-test-error`) was captured at rest after the test's variable
12-26 s timeout rather than as a burst.

### 13.2 The import: the real manifest, no import copy

`C:/dev/out/rpcs3-day-2026-10-28/MANIFEST.md` carries a `- Session: 2026-10-28 00:25` line, a
`| RPCS3 build |` row, a `| Firmware | 4.93 |` row and a `| Video |` row with `1920x1080`, so
`work/parity-import.ts` accepts it directly - the first batch in this corpus that never needed the
disclosed import-copy workaround 11.1 and 12.6 describe. Manifest sha256
`ca242fc3530f11f7da11861c4791e16dc3978256c89f3192f04a4268c0bf05fa`, 24,387 bytes.

```powershell
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md `
  --manifest C:/dev/xmb/out/parity-references/import/rpcs3-ref2-2026-10-28.MANIFEST.md `
  --manifest C:/dev/out/rpcs3-day-2026-10-28/MANIFEST.md --replace
```

Result: **36 cells imported, 0 skipped, exit 0**. The 23 earlier entries were re-validated and re-copied
byte-identically (source and destination sha256 equal; only their `applied` blocks moved), and 21 canonical
day cells were written: **13 new cells and 8 deliberate replacements** of the night frames that had been
filed at day cells. Every row is `manifestRef: exact` and every sidecar's `clock` field is the string read off
that frame's own plate, or the sentence saying why the plate is covered.

Two rows are mapped with a caveat rather than a clean route. `osk` and `options-panel` have native-only
catalogue routes (`column=tv > custom:jellyfin` and `column=video > library`) that this rig cannot open - no
TV column, no Jellyfin service, no Video Library page - so they were reached the only way a stock console
offers: `Users > Create New User > Edit` for the keyboard and `Game > Corrupted Data > TRIANGLE` for the
options panel. The panels are the surfaces the states name; the pages underneath are not. Both cells are
therefore Tier B pictures only, and their Tier A cannot pass on this frame. The manifest says so per row and
so does each sidecar's `note`.

**`root` is captured but not imported, and this is the one canonical cell the session does not move.**
`root__original__day__1080p` is backed by the `registered` pre-contract frame
`out/native-reference/ab-rpcs3-root.png`, `parity.ts` pairs a registered cell by that path before the contract
path, and `parity-import.ts` refuses a registered cell even with `--replace` (10.7). The brief forbade editing
`work/parity-reference.json`, so the matched frame - Game column, selection 0, depth 0, plate `10/28 10:00 AM`
- is staged at `C:/dev/out/rpcs3-day-2026-10-28/stills/root__original__day__1080p.png` (sha256
`fdd4609008f2cd5d0823c119dfc00d18a05dbc1b98fd53dccc5d75e4456a6c1c`) and **10.7 remains the decision the
corpus owner has to make**. One line out of `registered` plus one import run is all it takes; until then the
canonical root cell is still the 2026-10-25 DD/MM 24-hour Corrupted-Data frame, and its Tier B number below
measures that old pair, not today's frame.

### 13.3 Coverage before and after

| Measure | Before this pass | After this pass |
|---|---:|---:|
| canonical (`original/day/1080p`) cells with a reference | **9 / 30** | **22 / 30** |
| of those, genuinely at the day pin | 1 (the registered legacy root, itself 21:53) | 21 (+ the same registered root) |
| matrix cells with a reference | 24 / 720 | **37 / 720** |
| firmware-referenced states with a reference somewhere | 22 / 30 | 22 / 30 (unchanged - every state here already had a night cell) |
| native-only states that owe no reference | 5 (120 cells) | 5 (120 cells) |

The canonical count moves 9 -> 22, not 9 -> 30, and the difference is worth stating: 8 of the 21 rows replace
a frame that was already sitting at a day cell, so only 13 cells are new. That is exactly the mismatch 11.6
named - those 8 cells were "present" before and were not day frames.

Still empty, 8 cells, all named in 12.8 and unchanged by this pass: `column-tv` (this rig has no TV column),
`page-download-management` (needs a DOWN route the build cancels), and the six event-dependent composites
(`dialog`, `notification`, `volume-hud`, `info-indicator`, `ps-menu`, `media-player-panel`), which need an
event or input path the rig does not have.

### 13.4 The Tier B numbers that are now genuine

One run, one command, the current build `C:/dev/out/native-final/re-ps3-native.exe`
(sha256 `706b43f3b49ad38b72bfbd0fa19fc64ba59a2b1bafe1f1e63e5c652c605c712d`), canonical theme/background/
resolution, the 22 states that now have a canonical reference:

```powershell
bun work/parity.ts --capture --score --state root,column-user,column-settings,column-photo,column-music,column-video,`
column-network,column-psn,column-friends,page-settings-system,page-settings-network,page-network-status-list,`
page-network-test-error,page-network-connection-settings,page-settings-theme,theme-list,color-list,`
background-list,font-list,page-settings-video,osk,options-panel `
  --exe C:/dev/out/native-final/re-ps3-native.exe
```

`run-20261028-003804`, `scorecard-20261028-004646`. 22/22 cells captured, **Tier A PASS for all 22**, Tier B
**0 pass**, Tier C 0 pass (`options-panel` FAILs its trace and was retried three times). No state reaches
A+B+C, so this is not a pass claim - but every number below pairs the right state, at the right pin, with a
reference that is the firmware's own day frame. They are the first genuine Tier B numbers in the corpus.

| Cell | `frame` | required regions (PSNR / SSIM; budget) |
|---|---|---|
| `root` | 18.92 / 0.8554 | top background 20.25 / 0.9363 PASS; category icon row 20.90 / 0.8982 FAIL; item label row 12.04 / 0.3860 FAIL; info bar 17.61 / 0.8244 PASS; clock text 13.64 / 0.5128 FAIL (not gating) - icon-heavy 20 / 0.90 |
| `column-user` | 18.46 / 0.8436 | category icon row 19.42 / 0.8637; item label row 11.70 / 0.3951 |
| `column-settings` | 18.96 / 0.8390 | 19.16 / 0.8863; 12.64 / 0.4481 |
| `column-photo` | 17.71 / 0.8348 | 18.65 / 0.8718; 10.46 / 0.3561 |
| `column-music` | 17.82 / 0.8415 | 18.06 / 0.8587; 10.60 / 0.3627 |
| `column-video` | 18.02 / 0.8393 | 18.16 / 0.8571; 10.68 / 0.4054 |
| `column-network` | 18.50 / 0.8476 | category icon row **20.73 / 0.9221 PASS**; item label row 11.14 / 0.4094 |
| `column-psn` | **8.82 / 0.6202** | 9.69 / 0.7513; 10.45 / 0.4322 - the one cell that is worse than the others, and the reason is content: the console column is the full-bleed Sign Up artwork while the native column is a text list |
| `column-friends` | 18.39 / 0.8602 | category icon row **21.65 / 0.9424 PASS**; item label row 10.97 / 0.4846 (the console column has no rows) |
| `page-settings-system` | 18.83 / 0.8553 PASS | page rows 16.78 / 0.7455 FAIL; category icon row 21.32 / 0.9180 PASS (not gating) - text-heavy 18 / 0.85 |
| `page-settings-network` | 18.17 / 0.8633 PASS | page rows 15.87 / 0.7392 FAIL; category icon row 21.31 / 0.9179 PASS (not gating) |
| `page-network-status-list` | 17.44 / 0.8460 | status rows 12.97 / 0.6427; title rule 17.34 / 0.6934 |
| `page-network-test-error` | 15.53 / 0.7810 | message body 10.20 / 0.3467; back hint 12.20 / 0.5589 |
| `page-network-connection-settings` | 15.55 / 0.7780 | message body 10.01 / 0.3083; back hint 12.20 / 0.5588 |
| `page-settings-theme` | 18.18 / 0.8394 | page rows 15.69 / 0.7158; category icon row 21.33 / 0.9161 PASS (not gating) |
| `theme-list` | 18.59 / 0.8512 | band column 19.92 / 0.8670; band rows 17.32 / 0.7385 - composite 18 / 0.88 |
| `color-list` | 18.61 / 0.8446 | band column 19.01 / 0.8321; chip column 18.93 / 0.8079 |
| `background-list` | 18.28 / 0.8428 | band column 19.35 / 0.8458; band rows 16.54 / 0.6767 |
| `font-list` | 17.85 / 0.8344 | band column 19.48 / 0.8448; band rows 16.72 / 0.6416 |
| `page-settings-video` | 18.61 / 0.8493 | page rows 16.45 / 0.7252; category icon row 21.32 / 0.9181 PASS (not gating) |
| `osk` | **26.12 / 0.8900 PASS** | osk panel 21.27 / **0.7669** FAIL - the PSNR clears 18 and the SSIM is 0.11 short of 0.88 |
| `options-panel` | 18.91 / 0.8746 | options panel 19.56 / 0.8436 - **not a panel measurement, see below** |

**What the pin change was worth.** The eight replaced cells had been scored on 2026-10-27 against night
frames; today's run scores them against day frames, and the control for the build change is `root`, whose
reference did not change at all:

| Cell | 2026-10-27 run (night frame at a day cell, build `0a82595e`) | 2026-10-28 run (day frame, build `706b43f3`) |
|---|---|---|
| `root` (control - same reference both runs) | 18.34 / 0.8314 | 18.92 / 0.8554 |
| `column-user` | 10.74 / 0.2408 | 18.46 / 0.8436 |
| `column-settings` | 10.63 / 0.2528 | 18.96 / 0.8390 |
| `column-photo` | 10.49 / 0.2181 | 17.71 / 0.8348 |
| `column-music` | 10.50 / 0.2177 | 17.82 / 0.8415 |
| `column-video` | 10.45 / 0.2147 | 18.02 / 0.8393 |
| `page-settings-system` | 10.57 / 0.2606 | 18.83 / 0.8553 |
| `osk` | 15.29 / 0.2395 | 26.12 / 0.8900 |
| `options-panel` | 17.68 / 0.4932 | 18.91 / 0.8746 |

The columns move about +8 dB and +0.6 SSIM while the control moves +0.58 dB and +0.024, so the jump is the
pin, not the build. The remaining gap to 20 dB / 0.90 is not: the current build draws **no text at all**.

### 13.5 What the numbers do not measure yet, and the two that are not surface comparisons

**The current build is text-blind.** Every native frame in this run draws icons, plates and the wave but no
glyphs: no column labels, no clock digits, no page rows, no message body. This is the in-flight text-renderer
regression 12.5 used `native-status3` for structure only; `native-final` is built from the same tree. So every
`item label row`, `page rows`, `band rows`, `status rows`, `message body`, `back hint` and `clock text` region
above compares console text against empty pixels, and the frame numbers are dominated by the same gap. They
are honest numbers for this build and this reference set, and they are **not** the ceiling of what the
surfaces can score once glyphs render. The correct reading is: the structure (background, icons, plate, panel
rect, band gradient) is now being measured against a matched pin, and it is close - the category icon row
passes on three columns and is within 0.002-0.04 SSIM of passing on several more - while the text half is
unmeasurable until the regression is fixed.

Two numbers are weaker than the rest and are labelled rather than counted:

- **`options-panel` is not a panel comparison.** The native capture FAILed its Tier C trace
  (`shell: options opened ...`) on all three attempts, and the scored frame shows the Video column with a
  dimmed folder page and **no options band at all**. The 19.56 / 0.8436 on the panel region therefore compares
  the console's panel against a frame without one, and must not be read as "the panels are 19.56 dB apart".
  The honest statement is: the console side is captured (`stills/options-panel__original__day__1080p.png`,
  band at x~1330 with Sort By / Group Content / Start / Delete / Information); the native side did not open
  its panel on this build, so Tier C is the finding, not the number.
- **`osk`'s pass is the whole frame, not the panel.** 26.12 / 0.8900 clears the composite budget because both
  frames are a dark dim with a similar plate; the panel region is 21.27 / 0.7669 and fails on SSIM. With the
  keyboard's glyphs missing on the native side, the panel region cannot be better than that yet.

One more structural difference worth recording because it touches every column cell: the native icon row
carries a **TV/Video Services column** between Video and Game, and the console's does not (this rig has no
registered service). The `category icon row` region is a 1920x120 strip of that row, so it compares a
different set of categories - which is why it can pass on `column-network` and `column-friends`, whose
windows land on icons both sides draw, and fail elsewhere. That is a real firmware-vs-build column-set
difference, not a rendering miss.

### 13.6 What is still owed

- **The registered-root decision (10.7).** One matched frame is staged and ready; the canonical root cell
  cannot leave mismatch-by-construction until `work/parity-reference.json`'s `registered` entry is retired.
  This pass was forbidden from touching that file, so the decision is still the corpus owner's.
- **Glyphs.** Until the text-renderer regression is fixed, no cell can pass A+B+C on text-bearing regions, and
  a re-run of exactly the command in 13.4 on a text-rendering build is the measurement that follows this one.
  The reference set it would use now exists.
- **The six event-dependent composites and `column-tv`/`page-download-management`** - unchanged: they need the
  event/input path on the rig and the native side, not another day session.
- **The OSK's route gap.** The keyboard was captured from `Create New User` because the catalogue's route is
  the Jellyfin sign-in page. A sidecar field naming the modal (12.8's `messageTitle`/`messageKind`, extended
  to the OSK) would let the state assert the panel instead of the page under it; that is a tool change, not
  made here.
- **One workspace fact outside this pass, recorded because it would otherwise look like this pass's write.**
  `work/parity-catalogue.json`'s mtime moved at 00:46:35 during the scoring run (sha256 `76fce2eb...` was
  recorded by the run at 00:38:04, the file is `b67e09c1...` after), and `work/ab-layers.ts` /
  `work/ab-layers.thresholds.json` moved at 00:45:04 / 00:46:39 - concurrent work in the same checkout. **No
  command in this pass writes those files** (`parity.ts` has no write path to the catalogue; the importer only
  reads it), and neither `work/parity-reference.json` nor `work/parity.ts` moved. The run in 13.4 validated the
  `76fce2eb` version; the file still validates after the change (`--catalogue`, exit 0, 38 states, 35
  capturable), so the routes cited in 13.1 are the ones the run used. Flagged for the owner of that file, not
  reverted here - reverting someone else's in-flight edit is not this pass's call.

**Not a parity claim.** 21 canonical cells are now backed by a matched day frame instead of a night frame;
coverage moves 9 -> 22 of 30 canonical and 24 -> 37 of 720 matrix cells; no cell passes Tier A+B+C, no budget
moved, and the first genuine Tier B numbers are structural numbers on a build that cannot draw text.
## 14. The registered root is retired, and the canonical root is matched (2026-10-28)

Scorecard 10.7 left one decision open: `root__original__day__1080p` was backed by the **pre-contract registered**
frame, so the importer refused the matched day-window capture and the single most important cell in the corpus
kept scoring a mismatch-by-construction 18.9 dB. That decision is now executed:

- `work/parity-reference.json`: the `registered` entry is retired into a `retired` array that keeps the old frame,
  its date and its replacement reason. Nothing is deleted, and `work/ab-layers.ts` still uses that PNG for its own
  historical stored-pair comparison - a different instrument, untouched.
- `work/parity-references-map.json`: a `root / original / day / 1080p` row maps the matched day frame
  (`stills/root__original__day__1080p.png`, console clock `10/28 10:00 AM`, MM/DD, 12-Hour, theme Original
  evidenced in the same session).
- Import: **37 cells, 0 skipped**. Coverage is **22/30** firmware-reference canonical cells and **22/30**
  firmware-referenced states (5 native-only states owe none of the 35).

The root now scores against a frame with the same clock format, the same theme and the same preset as the app.
It still differs in content - the console Game column holds PlayStation Store / Saved Data Utility / Corrupted
Data, the app holds Steam rows - so it remains a labelled partial comparison, but no longer a clock mismatch.
A fresh Tier B run on a **text-bearing** build is required before any score is quoted: the batch that filled
these cells was measured against a build whose glyphs were missing, so its text-region numbers are structural
only (see the harness text-presence guard added the same day).
## 15. First scorecard on a text-bearing build with the matched corpus (2026-10-28 06:26Z)

Run `run-20261028-011604`, scorecard `scorecard-20261028-012659`, executable
`native-bands2/re-ps3-native.exe` sha256 `94e57264…c29c54`. This is the first Tier B run where the harness
proves the frame contains text (the clock-ink guard added the same day) and where the references carry the same
clock, theme and preset the app renders.

**Tier A: 35/35 capturable states PASS** - every reachable state's dump, sidecar and log oracles hold, including
the six routes that failed on the 2026-10-27 scorecard because of the live-pad `DOWN` defect.

**Tier B: 0/35 pass, 22 states measured, 13 without a reference.** The numbers are now genuine comparisons
rather than pin mismatches:

| state class | example frame | example worst region | budget |
|---|---|---|---|
| icon-heavy column | 17.6-19.0 dB / 0.83-0.86 SSIM | category icon row 20.7-21.7 / 0.92-0.94 (often passing) | 20.0 / 0.90 |
| text-heavy page | 18.0-18.8 dB / 0.84-0.86 | page rows 15.9 / 0.72 | 18.0 / 0.85 |
| composite band | 17.6-18.6 dB / 0.83-0.87 | band rows 15.8-17.2 / 0.51-0.74 | 18.0 / 0.88 |

The pattern is consistent and actionable: **whole-frame scores are near the budgets (most within 1-2 dB and
0.02-0.05 SSIM), the per-region failures are concentrated in the row/list geometry and in the band's text rows**,
which is the console's item layout around the icon band versus the native column's stacked rows
(SCORECARD 11.4-11.5), not a texture or material gap. `osk` passes its frame (25.95/0.8856) and fails only its
panel region (21.18/0.7695); `options-panel` fails its panel region (19.46/0.8324) because the console route on
this rig differs from the native route.

Tier C is 9/35: the states whose route has no scripted transition are `n/a` by construction, not failures.

Nothing was re-budgeted to make a cell pass: the provisional budgets of section 6 stand. The next fidelity work
is the row/list geometry the region numbers point at, not a budget change.

## 16. Three never-measured composites get their first frames and their first Tier B numbers (2026-10-28 07:56Z)

Batch `rpcs3-gaps-2026-10-28`, two boots, one day pin, executed from the plan in
`native-xmb/audit/REFERENCE-GAPS.md` section 5. Three of the eight reference-less states were captured and
imported (`info-indicator`, `ps-menu`, `dialog`); the fourth (`media-player-panel`, the plan's one group (b)
state) could not be produced, and the reason is a correction to the plan rather than a capture miss
(16.6). This is the first time any of these three surfaces has been compared against firmware at all.

### 16.1 Corpus movement

| Measure | Before | After |
|---|---:|---:|
| canonical (`original/day/1080p`) cells with a reference | 22 / 30 | **25 / 30** |
| matrix cells with a reference | 37 / 720 | **40 / 720** |
| firmware-referenced states with a reference anywhere | 22 / 30 | **25 / 30** |
| states the harness reports "without a reference" | 8 | **5** |
| composite-class states ever measured on Tier B | 6 / 12 | **9 / 12** |

Import: 3 new cells, 0 skipped, `--replace` over the 37 existing ones (the importer refuses to run otherwise,
and the 37 PNGs are byte-identical after it - checked by hashing the reference directory before and after).
Map: `work/parity-references-map.json` entries 37 -> 40. Manifests: `C:/dev/out/rpcs3-gaps-2026-10-28/MANIFEST.md`
(batch A) and `MANIFEST-B.md` (batch B, no cell).

### 16.2 What was captured, and the console state behind it

One boot drove the day pin and the console's own UI, so every cell carries evidence rather than an assumption:

- **Clock.** `| Console time offset (s): 8304` in the scratch config put the guest clock at 09:58:30 while the
  host clock stayed put (`logs/preflight.txt`). The boot plate read `28/10 9:59`.
- **Formats committed on the console.** The registry had been restored to DD/MM + 24-hour by the day batch, so
  both formats were set again through Settings > Date and Time: Date Format `MM/DD/YYYY`
  (`frames/a07-after-dateformat.png`, plate `10/28 10:00`), Time Format `12-Hour Clock`
  (`frames/a11-after-timeformat.png`, plate `10/28 10:00 AM`).
- **Theme Original evidenced, not assumed.** `frames/a14-theme-settings.png` shows Theme/Color/Background/Font
  all `Original` at 10:01 AM in the same session; no theme list was committed.
- **The three cells.** `ps-menu` from a 1200 ms PS hold at the Game column row 0 (plate `10/28 10:02 AM`);
  `info-indicator` from a 60 ms PS tap at the same position (plate `10/28 10:01 AM`); `dialog` from
  `Corrupted Data` > Triangle > Down > Cross, **cancelled with Circle, never confirmed** (the modal page
  replaces the info bar, so its own frame carries no clock; 10:03 AM either side).
- **The PS key never needed a map entry.** The installed map overrides only Cross/Circle/Triangle/Square, and
  PS stays the keyboard handler's default Backspace (`keyboard_pad_handler.cpp:47`). REFERENCE-GAPS 2.1 was
  right that the day manifest's claim of a PS line was wrong; the route needed nothing added.

### 16.3 Two transitions measured for the first time

Both bursts are 1:1 crops with per-frame timestamps, and both were measured rather than eyeballed
(`tools/burst-luma.ps1`, CSVs beside the frames).

- **PS-tap indicator, `burst/info-indicator-tap/`** (573 frames, 238.8 fps, crop 1160,60,760,120): the plate
  fades in 155 ms after the press, reaches full opacity 66 ms later, **holds for about 2.0 s**, and is gone by
  2.39 s. PS-BUTTON.md described "a flash" with no duration; this is the duration, and it is why the three
  grabs at +0.3 s, +1.1 s and +1.8 s all caught the plate.
- **PS-hold open, `burst/ps-menu-entries/`** (496 frames, 206.6 fps, crop 300,470,1320,240): the frame goes
  **fully black 1052 ms after the press** (the console's hold threshold), and the entries fade in between
  1202 ms and about 1970 ms. At rest the entries band averages 4.2 luma - two white lines on black.

### 16.4 Tier B: the first numbers these surfaces have ever had

Run `run-20261028-075444`, scorecard `scorecard-20261028-075620`, executable
`C:/dev/out/native-text3/re-ps3-native.exe` sha256 `cff205dd...4164`, pins `uiClock 2026-10-25T12:32:00`
(the corpus default, not the reference's 10:0x - see the residual below).

| state | A | C | frame (budget 18.0 / 0.88) | gating region | region (budget) | verdict |
|---|---|---|---|---|---|---|
| `ps-menu` | PASS | PASS | **22.37 / 0.9583 PASS** | `ps menu entries` | **19.39 / 0.9396** (26.0 / 0.95) | **FAIL** |
| | | | | `ps menu footer` | 18.54 / 0.8914 (18.0 / 0.88) | PASS |
| `info-indicator` | PASS | n/a | 17.97 / 0.8243 **FAIL** | `controller indicator` | 16.83 / 0.7718 (18.0 / 0.88) | **FAIL** |
| | | | | `info bar` (not gating) | 14.48 / 0.7644 | FAIL |
| `dialog` | PASS | PASS | 18.08 / 0.8384 **FAIL** | `dialog` | 16.42 / 0.7576 (18.0 / 0.88) | **FAIL** |
| `media-player-panel` | PASS | PASS | - | - | - | **no reference** (16.6) |

**The `ps-menu` entries budget is not met.** The corpus's one hand-set region budget is 26 dB / 0.95; the first
measurement is **19.39 dB / 0.9396** - 6.61 dB and 0.0104 SSIM short. The frame and footer pass, so this is a
region-local gap, and the geometry says exactly where it is (16.5).

### 16.5 Why each number is what it is (measured, not guessed)

**`ps-menu` - the closest of the three, and the only one where both sides draw the same structure.** Row-band
analysis of the entries crop: the console's two lines occupy y 511-537 and y 554-572 (a 43 px pitch) with 4,533
bright pixels in the region; the native lines occupy y 508-525 and y 548-565 (a 40 px pitch) with 2,259. Three
differences account for the 6.6 dB:

1. **Pitch and placement.** The native block is 3-6 px high and its two rows are 40 px apart against the
   console's 43 px, so both lines drift within the region.
2. **The focused row's glow.** The console's selected entry is a 27 px tall band of 239 bright pixels in its
   brightest row; the native selected entry is 18 px tall with 115. The native menu has no focus glow at all,
   so it has roughly half the ink in a region whose entire content is ink.
3. **The native footer intrudes into the region.** The native controller/charge panel sits at y 682-731, which
   puts its first rows inside the bottom of the entries crop (470-710); the console's sits at y 813-851, 130 px
   lower. That is also why `ps menu footer` (crop 140,650,600,130, i.e. y 650-780) passes at 18.54: it compares
   native text against **console black**, because the console's actual footer is below the crop. The pass is a
   coincidence of a mis-specified region and must not be read as agreement - section 16.8.

**`info-indicator` - the native side draws no indicator at all.** In the 340x60 `controller indicator` crop the
console frame has a mean luma of 72.34 with 592 bright and 297 dark pixels (the plate's dark box and its white
`1 -`); the native frame has 96.78 mean, 190 bright and **0 dark** - there is no plate, only the left edge of
the date plate. The catalogue lists the route as "root, then the `ps` script token", and the run's own frame
proves the token did not leave a visible plate in the capture window. The 17.97 dB frame misses its 18.0 budget
by 0.03 dB, which is the least interesting fact in this table: the state is a **missing surface**, not a
misplaced one.

**`dialog` - the native frame is not a dialog.** The console frame is the firmware message box (dimmed scene,
plate, `The installed game will be deleted.` / `Are you sure you want to continue?`, `Yes`/`No` with `No`
focused); the native frame for the same state is the Video column root with an inline row label
`There are no content items.` In the 1440x700 region the console has 11,937 bright pixels over a mean luma of
68.03; the native has 84,345 over 90.18 - a bright list page, no plate, no dim. Tier A and Tier C both PASS
(the route and the `continue watching: nothing to continue` trace are right), so this is a **surface** gap in
the build, and the first number for the corpus's oldest spec-only row is 16.42 / 0.7576 against a native
render that does not attempt the plate.

### 16.6 `media-player-panel` is still not capturable, and the plan's prerequisite is wrong

REFERENCE-GAPS 3.1 says the prerequisite is "one playable video file in the emulated VFS" and calls it "one
file copy away". That was tested and it is not sufficient. `MANIFEST-B.md` is the record: a 3,459,152 B
H.264/AAC MP4 (`avc1` + `mp4a`, 20.00 s, read from its own boxes) was placed in `dev_hdd0/video` before a boot
and the Video column drew only `BD Data Utility` / `Search for Media Servers` / `Video Editor & Uploader`; the
same file copied to `dev_usb000/VIDEO` and a column re-entry changed nothing; a JPEG in `dev_hdd0/photo`
behaved identically, and the Photo Gallery - opened for the first time in this corpus - showed its own
first-run page and no file.

The reason is in `bin/log/RPCS3.log` for that boot (47,749 lines): vsh opens `/dev_hdd0/mms/db/metadata_db_hdd`
at 0:00:10 and reads its log/index files, and **`/dev_hdd0/video` appears in the log zero times**. The media
columns are drawn from the MMS metadata database, not from a directory scan, and `/dev_usb000` appears only in
the VFS mount dump - no enumeration, no hotplug. So the console's own importer has to write a metadata row, and
its only sources are devices RPCS3 does not present here. On this rig `media-player-panel` is **group (c)**, not
group (b); the copy-completion notification experiment of 4.3 was not attempted because it had no item to open
an options menu on. The content was removed and both columns were re-grabbed empty
(`frames/b11-video-after-removal.png`), and every hash is back to its pre-session value.

### 16.7 Rig restoration

| Item | Value |
|---|---|
| `dev_flash2/etc/xRegistry.sys` | 262,144 B, sha256 `4de4dcdcaecda324093a9246888cc337a5c872103b779047c2a52387fc57d80d` (both boots, restored byte-for-byte, mtime reset) |
| global input config | 66 B, sha256 `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` = the required target |
| live `config.yml` | sha256 `22d8ffe22461284c6756fc80ab0de2b7e76415981299fdcc6621a697971e4adb`, untouched |
| `dev_hdd0/game/TEST12345` | backed up before batch A, restored byte-for-byte after it (0 files, empty `USRDIR`, as before) |
| media content | removed; `dev_hdd0/video`, `dev_hdd0/photo` and `dev_usb000/VIDEO` empty again |
| RPCS3 | only pid 4264 and pid 31300 (this batch's own boots) were stopped |

### 16.8 What this changes for the next pass

1. **`ps-menu` is a 6.6 dB region fix with a known shape**: match the 43 px pitch, draw the focus glow, and
   move the controller/charge panel down ~130 px. Two of those three are also what makes the entries region
   comparable at all.
2. **The catalogue's `ps menu footer` crop does not contain the console's footer** (console y 813-851 vs crop
   y 650-780) and its `controller indicator` crop starts 15 px right of the console's plate (x 1185 vs 1200).
   Both were left alone - changing a region is a catalogue change and this batch's fence forbids it - but a
   future pass should re-derive them, or the footer's 18.54 pass will keep reading as agreement when the two
   sides are not even drawing in the same place.
3. **The build owes two surfaces, not two numbers**: `info-indicator` needs the tap plate (the corpus's
   `controllerIndicatorVisible` field, 1.1) and `dialog` needs an actual message box behind the Continue
   Watching empty state. Until then their Tier B numbers measure absence.
4. **`media-player-panel` moves out of the queue** on this rig (16.6) unless the MMS database can be written by
   the console itself, which needs a device RPCS3 does not present.
5. **Residual, stated:** the capture pins are the corpus default `uiClock 2026-10-25T12:32:00` while the
   references' plates read `10/28 10:01-10:02 AM`. That depresses the `frame` and `info bar` numbers by the
   clock string alone (three characters and the AM/PM); the region numbers that gate these three cells are not
   clock-bearing. A future run that wants the plate to agree should pin the capture to the reference's own
   clock, and say which pin it used.

## 17. Scoring validity: content-difference regions and the ribbon-phase diagnostic (2026-10-28 08:00Z)

Two scoring-validity defects were fixed in the scorecard tool (`work/parity.ts`) and in the catalogue
annotation (`work/parity-catalogue.json`) only. No source file, CMake line, budget, reference frame or
capture pin was changed. Every number below is on `C:/dev/out/native-text3/re-ps3-native.exe`, sha256
`cff205dd2baded517dbf15379ecca90b9f32c42c13947cbf1e72ed55f0614164`, the current text-metric build.

The defects, both already visible in sections 14-15 and in `PIPELINE.md` "Exposure and ribbon phase":

1. **The ribbon phase was not comparable, and the scorecard charged for it anyway.** The pinned
   `--render-clock 30` freezes the cloth: `dt = seconds - lastSeconds_` is 0 after the first frame, so
   captures at 30 s and 45 s differ by at most 1/255 and every parity still holds the settle's phase,
   while the console reference holds an arbitrary instant of the swell. The scorecard then read the
   difference as a fidelity failure of the picture.
2. **Content-difference regions were charged as fidelity failures.** The console Game column holds
   PlayStation Store / Saved Data Utility / Corrupted Data; the native column holds Steam Store rows. The
   `item label row` is 11.03 dB on that content, not on a rendering defect, and the scorecard gated it.

### 17.1 The ribbon-phase sweep (scoring-only)

`work/parity.ts` now has a capture-time diagnostic:

- `--phase-sweep <sec,...>` (env `PARITY_PHASE_SWEEP`) and `--phase-state <id,...>` (env
  `PARITY_PHASE_STATE`, default `root`).
- The shipped cell is captured exactly as before: `--render-clock 30`, the same pins, the cell that
  gates. The sweep never replaces it.
- For each offset the tool captures the same cell again **without** `--render-clock`, so the app's own
  live render clock advances the cloth (60 fixed steps per second), and inserts `pause=<offset*1000>`
  as a script token immediately before the grab. The route is run first and settled, so only the
  animation clock moves during the pause.
- Phase cells are stored as `<cell>.phaseN`, recorded in the manifest under `phase`, and kept out of
  `cells`, the per-state worst status, `totals.failed` and every gate. They are scored against the same
  reference and the same budgets; the best is the Tier A-valid phase with the highest whole-frame PSNR
  (SSIM breaks ties). The scorecard prints shipped vs best plus every phase, with the ribbon band
  (y 360-840) PSNR for each, in `phaseSweep` and in the markdown section "Ribbon-phase sweep
  (scoring only - not a gate)".

Root at the canonical cell, eight offsets covering the sampled cycle (`run-20261028-073733`):

| phase | frame PSNR / SSIM | ribbon band PSNR |
|---|---:|---:|
| shipped (`--render-clock 30`) | 17.97 dB / 0.8225 | 16.00 dB |
| live+0 s | 17.80 dB / 0.8257 | 15.82 dB |
| live+6 s | 17.79 dB / 0.8270 | 15.74 dB |
| live+12 s | 18.26 dB / 0.8321 | 16.39 dB |
| live+18 s | 18.52 dB / 0.8377 | 16.79 dB |
| live+24 s | 18.09 dB / 0.8328 | 16.32 dB |
| live+30 s | 18.08 dB / 0.8339 | 16.15 dB |
| **live+36 s (best)** | **18.59 dB / 0.8400** | **16.89 dB** |
| live+42 s | 17.82 dB / 0.8318 | 15.76 dB |

The best sampled phase recovers **+0.63 dB on the frame and +0.89 dB in the ribbon band**, and root is
still below its 20.00 / 0.900 icon-heavy frame budget. The phase term is therefore real but it is not
the whole residual: the remaining band gap is the sheet's extent (about 140 px higher and about 220 px
shorter, PIPELINE 3) and the background pedestal, neither of which a phase can move. The sampled peak is
about 6 s wide; a finer sweep could find a few tenths more, not a different verdict.

A scratch diagnostic that subtracts that pedestal (the 17/255 correction PIPELINE 4 used; not a tool
number) reads the same pair as 19.47 -> 20.51 dB on the frame and 16.75 -> 18.00 dB in the band: the
phase signal is about 1.0 dB once the additive term is out of the way, and at the best phase the
corrected frame clears 20 dB. Raw PSNR hides it because the pedestal sits on both phases equally. Even
corrected, the extent gap does not close, so the sheet's shape remains a cloth-state question and not a
phase.

Limits, stated because they bound what the number means:

- The app has one animation clock. The pause advances the cloth **and** the background wave, particles
  and icon animation, so a swept frame is a scene-instant match, not a cloth-only re-render. The ribbon
  band is printed so the cloth's own band can be read apart from the whole frame.
- The offsets are requested in integer milliseconds and the cloth integrates at 60 fixed steps per
  second, so an offset is a deterministic number of cloth steps in expectation; the pause itself is a
  real-time timer. Two captures at the same offset measured 45.1 dB / 0.9921 in the ribbon band (47.4 dB
  over the frame), i.e. they agree to a few cloth steps. The absolute phase is the app's live start-up
  phase, which this pass cannot pin because it may not touch `src/`; the tool therefore reports offsets
  from the shipped run's live base, not an absolute phase.
- The sweep is a correlation probe, not a fidelity claim: it says "at this phase the frame is this
  close", not "the renderer is this close".

**Method note (ribbon phase).** Ribbon-phase comparison is scoring-only. The gate is computed on the
shipped cell at `--render-clock 30`; no gate, total or claim in this corpus uses a swept phase. A
swept-phase number may be quoted only with its offset (`live+36 s`) and never as a gate. If a future gate
is ever defined on a ribbon region, it must state the phase it used - the shipped pin, or a named sweep
offset - because the number is phase-dependent.

### 17.2 Content-difference regions (informative, not gating)

A region in `work/parity-catalogue.json` may now carry `contentDiffers: true` and a one-line
`contentDiffersReason`. The contract is validated in `loadCatalogue`:

- `contentDiffers` must be a boolean and a reason is mandatory when it is true;
- the `frame` region can never be annotated (the whole-picture score would excuse any rendering error);
- the scorecard prints every annotation, its measured value and its reason on every run, and prints the
  catalogue-wide count, so the set cannot grow silently.

`scoreCell` makes `gating = required && (!contentDiffers || strict)`. A cell is `pass` when no gating
region fails, `informative` when the only failed required regions are content-difference regions, and
`fail` otherwise. The state verdict spells it out (`A pass, B informative, C n/a`), `totals.failed`
excludes informative cells, and `--strict` puts the annotated regions back in the gate, reproducing the
pre-annotation numbers from the same frames.

The sixteen annotations added, with the measured value in this run and the reason tied to the reference
frame (all ASCII, all one line in the catalogue):

| state | region | measured (this run) | why the content cannot match |
|---|---|---:|---|
| root | item label row | 11.08 dB / 0.4003 | reference Game column holds PlayStation Store / Saved Data Utility (PS3) / Corrupted Data; the native column holds Steam Store rows, so the focused label is PS Vita System Application Utility on the console and Steam Store here |
| column-user | item label row | 11.60 dB / 0.4089 | reference Users column focuses Turn Off System from the console account rows; the native column focuses Create New User from the host profile store |
| column-photo | item label row | 10.54 dB / 0.3692 | reference Photo column focuses Search for Media Servers over an empty console VFS; the native column focuses Photo Library from the host photo folder |
| column-music | item label row | 10.68 dB / 0.3707 | reference Music column focuses Search for Media Servers over an empty console VFS; the native column focuses Music Library from the host music folder |
| column-video | item label row | 10.32 dB / 0.3958 | reference Video column focuses BD Data Utility; the native column focuses Continue Watching from the host video folder |
| column-psn | item label row | 10.23 dB / 0.4227 | reference PSN column shows the console signed-out Sign Up row over its sign-up artwork; the native column shows Sign In from its own signed-out list |
| column-friends | item label row | 9.85 dB / 0.4348 | reference Friends column is rowless because the console is signed out and offline; the native column draws its own Open Steam Friends row |
| page-settings-system | page rows | 16.60 dB / 0.7494 | reference page is the console 25 firmware rows with console values (PS3-354, English (UK)); the native page is built from sysconf_plugin schemas and host strings, so the row set and values differ |
| page-settings-video | page rows | 16.91 dB / 0.7967 | reference page is the console 19 BD/DVD rows; the native page draws seven measured rows plus three native-only media rows |
| page-network-status-list | status rows | 12.02 dB / 0.5804 | reference rows are the console fixed 13-row network report (Internet Connection Enabled through UPnP Enabled); the native page answers with the host own network report |
| page-network-test-error | message body | 9.16 dB / 0.2567 | reference body reads "An Ethernet cable is not connected."; the native row answers with this build own "not available in the native recreation yet" message |
| page-network-connection-settings | message body | 9.64 dB / 0.3163 | reference body is the firmware "Adjust settings for connection to the Internet" text; the native row answers with this build own not available message |
| color-list | chip column | 19.93 dB / 0.8404 | reference draws 12 unlabelled colour chips; the native list draws six labelled colours in its own panel, so the chip content has no native counterpart |
| background-list | band rows | 16.81 dB / 0.6845 | reference rows are Brightness (submenu), Original, Classic, Wallpaper; the native list has three values and no Brightness row |
| font-list | band rows | 17.59 dB / 0.6630 | reference rows are Original, Rounded, Pop; the native list draws the firmware faces this build registers (SCE-PS3 Seurat LATIN, VAGRundschriftDLig) |
| options-panel | options panel | 19.46 dB / 0.8324 | reference panel was opened on Game > Corrupted Data and lists Sort By, Group Content, Start, Delete, Information; the native route opens Video Library and lists Open, Copy, Delete, Information |

Judgement calls that were deliberately **not** annotated, so the set stays honest:

- **root `clock text` and `info bar`** (11.57 / 0.5376 and 14.54 / 0.7669): the console plate reads
  `10/28 10:00 AM`; the shipped pin renders `10/25 12:32 PM`. This is a pin difference the app *can*
  reproduce by pinning to the reference's clock (section 16.8 item 5 says the same), so it is not
  content the app cannot match. `info bar` stays gating; `clock text` was already non-gating.
- **column-settings and column-network `item label row`**: the focused first label is the same string on
  both (`System Update`; `Online Instruction Manuals`), so the region's content matches. They stay
  gating.
- **page-settings-theme `page rows`**: the same four firmware rows and values (Theme, Color, Background,
  Font, all Original) are drawn on both. It stays gating; its 15.53 dB is geometry, not content.
- **page-settings-network `page rows`**: the five row labels match; only the two console value cells
  (`Enabled`) differ. The region is mostly matchable, so it stays gating rather than being excused.
- **osk `osk panel`**: the key grid, function row and prediction column are the same content; only the
  title/field strings differ (`User Name` / `User 1` vs `Username` / empty) because the reference was
  opened from Create New User and the native OSK from the Jellyfin sign-in. Most of the panel is
  matchable, so it stays gating.
- **theme-list `band rows`**: the same four labels (Install, Original, Classic, Air Paint) are drawn on
  both; its 16.96 dB is geometry. It stays gating.

### 17.3 Before and after

The tool change is separated from the capture change by rescoring the **same** frames both ways. Run
`run-20261028-072013` was captured with the old tool; its stored scorecard is
`scorecard-20261028-073317`.

| scoring of run `run-20261028-072013` | B pass | B informative | gating cell failures | claims |
|---|---:|---:|---:|---:|
| old tool (stored scorecard) | 0 | - | 22 | 0 |
| new tool, `--strict` (pre-annotation gate) | 0 | 0 | 22 | 0 |
| new tool, informative default | 0 | 2 | 20 | 0 |

The two informative cells are `page-settings-system` and `page-settings-video`: their frames pass and
their only failed required region is an annotated content-difference region.

The fresh after run is `run-20261028-073733`, scorecard `scorecard-20261028-075605`, with the phase sweep
on root. Total as captured:

| metric | before (`run-20261028-072013`, old tool) | after (`run-20261028-073733`, new tool) |
|---|---:|---:|
| Tier A pass | 32 / 35 | 32 / 35 |
| Tier B pass | 0 | 0 |
| Tier B informative | - | 2 |
| Tier B fail (measured) | 22 | 23 |
| gating cell failures | 22 | 23 |
| content-difference annotations | 0 | 16 regions / 16 states |
| claims (A+B+C) | 0 | 0 |

The raw counts are not like-for-like: a concurrent pass imported three new composite references
(`dialog`, `info-indicator`, `ps-menu`) at 07:43-07:48, between the two runs, so the after run scores
three more cells and all three fail. Restricted to the 22 states that had a reference in both runs, the
before/after split is identical and is the honest comparison:

| 22-state like-for-like | gating failures | informative |
|---|---:|---:|
| before, informative mode (`scorecard-20261028-073335`) | 20 | 2 |
| after, informative mode (`scorecard-20261028-075605`) | 20 | 2 |
| after, strict mode (`scorecard-20261028-075736`) | 22 | 0 |

Strict mode on the after run reports 25 gating failures (the 22-state 22 plus the three new composites),
and zero informative cells. The gate delta is exactly the two annotated states; no other cell moved.

Tier A is 32/35 on this build, not 35/35: `color-list`, `background-list` and `font-list` fail their
settings-choice-panel assertions (row counts and the panel log line) in both runs. That is a pre-existing
build issue recorded by the same assertions, not a consequence of the annotations, and their Tier B
numbers are still measured.

### 17.4 What still gates, per annotated state

Every annotation removes exactly one region from the gate; the state still fails unless every remaining
gating region passes. This is the split for each annotated state in the after run:

| state | verdict | still gating (and failing) |
|---|---|---|
| root | A pass, B fail | frame 17.97 / 0.8225; info bar 14.54 / 0.7669 |
| column-user | A pass, B fail | frame 18.42 / 0.8449; category icon row 19.60 / 0.8682 |
| column-photo | A pass, B fail | frame 17.66 / 0.8342; category icon row 18.73 / 0.8753 |
| column-music | A pass, B fail | frame 17.75 / 0.8376; category icon row 18.20 / 0.8633 |
| column-video | A pass, B fail | frame 17.81 / 0.8325; category icon row 18.30 / 0.8621 |
| column-psn | A pass, B fail | frame 8.82 / 0.6140; category icon row 9.68 / 0.7513 |
| column-friends | A pass, B fail | frame 17.76 / 0.8456 |
| page-settings-system | A pass, B informative | none (frame 18.69 / 0.8560 and category icon row pass) |
| page-settings-video | A pass, B informative | none (frame 18.81 / 0.8738 and category icon row pass) |
| page-network-status-list | A pass, B fail | frame 16.84 / 0.8335; title rule 16.22 / 0.6867 |
| page-network-test-error | A pass, B fail | frame 15.36 / 0.7783; back hint 11.39 / 0.5155 |
| page-network-connection-settings | A pass, B fail | frame 15.44 / 0.7774; back hint 11.40 / 0.5154 |
| color-list | A fail, B fail | frame 19.23 / 0.8639; band column 19.77 / 0.8766 |
| background-list | A fail, B fail | frame 18.82 / 0.8617; band column 19.93 / 0.8797 |
| font-list | A fail, B fail | frame 18.38 / 0.8544 |
| options-panel | A pass, B fail, C pass | frame 18.84 / 0.8697 |

The six unannotated scored states still gate every required region: `column-settings`,
`column-network`, `page-settings-network`, `page-settings-theme`, `theme-list` and `osk`. The three
composites whose references arrived during the run (`dialog`, `info-indicator`, `ps-menu`) gate their
frames and regions as before. The five states without a reference, and the five native-only surfaces,
are unchanged.

### 17.5 Verification, exit codes and files

- `bun work/parity.ts --catalogue` - **exit 0**; prints the 16 annotations with `*`, the count and the
  reasons, and its JSON carries every region's `contentDiffers` and reason. The annotation validation
  rejects a missing reason, a non-boolean flag, or a `frame` annotation before any capture runs.
- `bun work/parity.ts --references-missing` on the full inventory - **exit 1**, unchanged from before
  this pass: 5 firmware-referenced states still owe a reference (`column-tv`,
  `page-download-management`, `notification`, `volume-hud`, `media-player-panel`). Scoped to the 25
  states that have one, the same command exits **0** (`25/25 firmware-reference cells present`). The
  exit-1-on-debt contract was left alone; annotations do not touch it.
- `bun work/parity.ts --capture --score --exe C:\dev\out\native-text3\re-ps3-native.exe` - exit 1 on
  both runs because the gated cells fail, which is the scorecard's contract, not a tool error.
- `bun work/parity.ts --score --strict --run ...` reproduces the pre-annotation gate from the same
  frames (22 failures on the 22-state subset, 25 on the full after run), and `--regression` reports an
  annotation change as a finding, so the informative set cannot grow through a scoring run unnoticed.

Files touched by this pass: `work/parity.ts` (the phase sweep, the informative gate, `--strict`,
`--phase-sweep`/`--phase-state`, the printed split and totals); `work/parity-catalogue.json` (only the 16
`contentDiffers` / `contentDiffersReason` fields); `native-xmb/audit/PARITY-SCORECARD.md` (this section).
No reference frame, no budget, no capture pin, no app source, no CMake and no `work/ab-layers.*` file was
touched. Scratch captures and comparison images live under `out/phase-probe/` and are not tracked.
## 18. Consolidated scorecard after the composite and pin waves (2026-10-28)

Scorecard `scorecard-20261028-085426` (run `run-20261028-084205`, executable `native-head2` sha256
`c88b1404983ad0430ead289682c42c286a58d0aa21fa19b29000b5be0eb04381`), the first full run after the
composite renders and the canonical pin correction.

**Tier A 34/35, then 35/35 after one catalogue fix.** The only miss was `background-list`'s `entryId`, which is
`appearance/background/1` now that the measured `Brightness` submenu row sits first; `f4f5920` corrects the
assertion rather than the app.

**Tier B: 0 pass, 2 informative, 23 gating cell failures, 10 captured cells without a reference.** The two
informative states are `page-settings-system` and `page-settings-video`, whose frames pass and whose only failing
region is the annotated `contentDiffers` one - the category the annotation system exists for.

**Tier C: 9 pass**, the rest `n/a` because their routes have no scripted transition.

Composite results that are now passing their own budgets: `ps-menu` frame **28.82/0.9918**, entries
**30.89/0.9857** (budget 26/0.95), legend **23.93/0.9423**; `info-indicator`'s indicator region
**22.37/0.9277** (its frame fails on the background, not the indicator). `dialog` is at 18.12/0.8420 on its
region - over the PSNR budget, short on SSIM - with the box now measured from the console frame.

**The canonical pin moved from 2026-10-25 to 2026-10-28** (the date the reference was captured) after the fourth
background pass measured that the reference's `_MonthTime` is 26.129, not the 23.2258 of the 10-25 session. Every
gated layer improved: frame **18.29 -> 18.76**, top background **20.25 -> 21.06**, category icon row
**20.93 -> 21.62**, clock 11.39 -> 11.41, info bar 14.98 -> 15.05, all SSIM up, gate **6/6**, and the twelve named
states were re-saved against it.

The remaining gating failures are real rendering differences with named regions; the largest is still the
background's flat achromatic residue (+0.0594 linear scene, past the draw), whose closing artifact is the
console's 64x32 surface dump (`work/fp-grid-day.npy` is the expected value). No budget was changed to move a cell.
