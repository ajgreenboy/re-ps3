# PARITY SCORECARD

Method, threshold policy and the first measured scorecard for the 1:1 fidelity track
([PARITY-PHASES.md](../PARITY-PHASES.md), P0.1-P0.5). Written 2026-10-27.

**Status: infrastructure complete, no screen at parity.** The catalogue covers 25 capturable states and
3 honestly-uncapturable ones; every capturable state was captured on 2026-10-27 and gated on Tier A
(dumps) and Tier C (input traces), with 19 passing Tier A; and **one** state has a firmware reference at
all - a pre-contract frame whose content does not match by construction. Everything else is "no reference
yet" for Tier B. That is the honest state of the corpus, and it is what P0 exists to make visible.

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
