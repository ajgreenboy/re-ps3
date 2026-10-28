# PARITY SCORECARD

Method, threshold policy and the first measured scorecard for the 1:1 fidelity track
([PARITY-PHASES.md](../PARITY-PHASES.md), P0.1-P0.5). Written 2026-10-27.

**Status: infrastructure complete, no screen at parity.** The catalogue covers 25 capturable states and
3 honestly-uncapturable ones; every capturable state was captured on 2026-10-27 and gated on Tier A
(dumps) and Tier C (input traces), with 19 passing Tier A. Since the two imports in sections 10 and 11
(2026-10-27), **9 of 25** canonical cells and **14 of 600** matrix cells have a real RPCS3 reference: the
one pre-contract frame, eight frames from the `rpcs3-ref-2026-10-28` batch, and four night cells from
`rpcs3-ref2-2026-10-28`. The second batch moved the matrix (10 -> 14) but not the canonical count, because
every frame in it is a night frame; it does give three states (`column-network`, `column-psn`,
`column-friends`) their first reference of any kind, at night. Every imported canonical reference is
labelled mismatch-by-construction on its background pin, its console clock, its theme assumption or its
page beneath; the four night cells are the first cells where the pin and the state agree, and they are
measured in 11.4. The other 16 canonical cells have no reference, and 5 of those 16 states have no
firmware view to capture. That is the honest state of the corpus, and it is what P0 exists to make
visible.

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

`work/parity-catalogue.json` - 28 states, 25 capturable today, 3 recorded as not capturable with the exact
missing backend. Every state carries: id, screen class, the firmware XMBML document and view that defines
it (or an explicit "native, no firmware view" with the reason), the native reachability, the script route,
the dump assertions, the trace assertions and the region crops.

`--catalogue` cross-checks each claimed XMBML view against the actual document in dev_flash (206 views
across 28 documents), so a stale document or view id fails without a capture.

| Class | States | What they are |
|---|---:|---|
| `icon-heavy` | 11 | the root and the ten columns (Users, Settings, Photo, Music, Video, TV/Video Services, Game, Network, PlayStation Network, Friends) |
| `text-heavy` | 9 | Settings > System Settings, Game > All Games, Video > Video Library, Game > Steam Store sections, Network > Download Management, TV > Jellyfin sign-in, Continue Watching, Trophy Collection (not capturable), plus the dialog's Video column |
| `composite` | 8 | options panel, dialog, OSK, notification, volume display, information panel indicator, PS menu, media player panel, PSN sign-in (not capturable) |

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

`work/parity-reference.json` `matrix`: **25 states x 3 themes x 4 backgrounds x 2 resolutions = 600 cells**.

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
one cell each, 25 captures. `--matrix` with `--capture` runs all 600; `--theme/--background/--resolution`
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
   them as owed (10.6).
6. **A catalogue extension, before any further rig work.** Batch 2 captured ten surfaces the catalogue
   cannot name: Settings > Network Settings (depth 1), Settings and Connection Status List (depth 2), the
   `Internet Connection Settings` message page, the Internet Connection Test error page, the Theme Settings
   page, the Theme list, the Color list, the Background list, the Font list, and the Video Settings page.
   Adding them as states (with the batches' geometries and the manifest's row lists) would let most of
   batch 2's stills - the `error/`, `depth2/`, `lists/`, `bands/` and `tails/` groups - become references,
   and would put the corpus's bottleneck back on the rig, where it can be fixed. That is a catalogue edit
   and is not made here.
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
