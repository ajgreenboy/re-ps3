# Parity & phase handoff — 2026-11-10 (night session)

A handoff for **everything that is not tonight's render-fidelity session**. Tonight's session owns the
"blocks the look" render A-list (see "Out of scope" below). This document hands off the rest: the parity
(1:1 fidelity) track and the product/HTPC phase track, with their current state, the remaining work, and
the instruments to drive them.

## Repo state (as of writing)

- Tree clean, pushed. Recent HEAD: `fb8f176` "Audit in-game XMB fidelity and estimate backdrop dimming",
  on top of `e3697dc` (in-game perf/dialog/crop-fill), `8de5bc3` (in-game lifetime + quit-row scope),
  `b67ad99` (guide-shadow alpha + fill), `b2167e7` (Download-from-Sony + HTTP firmware fetch),
  `eeb4246` (PUP hash-by-index), `2914349` (PUP extraction), and the HTPC D1-D12 + updater/release work.
- Regression gate (`bun work/ab-layers.ts --all`) is **22/22 green**.
- A live-borderless-overlay subagent was in flight at the time of writing; its commit lands on top.

## Two tracks

1. **Parity track** — the 1:1 fidelity track. Plan of record: `archive/PARITY-PHASES.md` (P0-P11).
   Instrument: `work/parity.ts` + `work/parity-catalogue.json` + `work/parity-reference.json`.
   State: `audit/PARITY-SCORECARD.md` + `audit/REFERENCE-GAPS.md`.
2. **Product/HTPC track** — finish features, make it a usable HTPC appliance. Plan of record:
   `audit/PHASES-HTPC-2026-11-09.md` (D1-D12) and `audit/PHASES-2026-11-07.md`.

---

## Parity track

### Where it stands

- **P0 (infrastructure) is done.** `work/parity.ts` implements the three tiers and all commands
  (`--catalogue`, `--matrix`, `--capture`, `--score`, `--regression`, `--references-missing`,
  `--references-missing --matrix`). Catalogue: 38 states (35 capturable, 5 `noFirmwareReference`).
  Matrix: 840 cells. Reference contract + provenance sidecars + importer
  (`work/parity-import.ts`) + `work/parity-references-map.json`. See `PARITY-SCORECARD.md`.
- **Tier B is 0/25 pass.** 22 of 30 firmware-referenced states have a reference *somewhere*, but every
  measured number is below its class budget — that is the render gap tonight's session is attacking, not
  a missing-instrument problem. The honest headline from `FIDELITY-VERDICT.md`: at element level the
  recreation is roughly half-exact, a quarter visibly wrong, a sixth missing.
- **The three tiers (claim separately; a screen is "1:1" only when A+B+C all pass):**
  - A structural (dumps equal firmware data) · B perceptual (frame vs a real RPCS3 reference within budget)
  - C behavioural (scripted input trace matches).

### The ranked remaining work, in the order the audits rank it

From `FIDELITY-VERDICT.md` §4 and `FIDELITY-ROOT-BAR.md` §7. **Tonight's A-list is carved out in
"Out of scope"; everything else is this handoff's business.**

**B. Whole surfaces missing (breadth — Phase 9 / P7):**
Internet Browser (the firmware's largest plugin), PlayStation Store, Trophy Collection, friends
messaging/chat, save-data management, the real media players, Video Editor & Uploader, settings sub-flows
(netconf / deviceconf / software-update / print / backup), copy/move/progress dialogs, notification
producers, in-game XMB/impose, What's New, Photo Gallery, playlists, sign-up/sign-in, disc/BD playback.
Breadth is the weakest axis: 125 firmware surfaces → **23 reachable / 40 partial / 62 absent (49.6%)**.

**C. Player panels (thinnest area — Phase 11/12):**
Video player's 25-button control matrix and 14 status planes absent; photo viewer's 20-button panel and
hint/slideshow strips absent; audio player's progress page not rebuilt; volume HUD fade-in missing from the
UI-layer signature and its focus segment never drawn.

**D. Structure and behaviour (Phase 12/15):**
- Options menu has **2 rows vs the console's 5** (no Sort By / Group Content group, no 199 px gap), band
  sits +66 px right.
- Settings writes commit and pop instantly (no confirm chain or result page).
- Media columns carry a different row set (0 of the console's 8 media rows); Theme page 6 rows vs 4;
  message pages lose the y=879 rule; idle hint plate never draws; `*User` sub-label never fills.
- PS-menu power semantics, Auto-Off re-arm via input, OSK extras (L1/R1 caret, L2/R2 pages, Select mode,
  open/close fade), gameboot/XMB return, multi-select copy/delete, Steam account-switch surface.

**P phases still open** (framework, from `PARITY-PHASES.md`): P1 resource/object model (L), P2 geometry
(L), P3 text at PAF exactness (M-L), P4 icon materials (L), P5 background/wave/HDR (M-L), P6 motion traces
(L), P7 screens/behaviour (L, the bulk), P8 audio (M), P9 themes (M + L if P9.3 funded), P10 display/
resolution (M), P11 closure (S-M). P0 done; P1 runs parallel to P0; then P2/P3/P4/P5/P6, then P7 screen by
screen, then P8/P9/P10, then P11.

### Reference corpus — the 8 missing states (the capture-side debt)

From `REFERENCE-GAPS.md`. `bun work/parity.ts --references-missing` is the live list (8 states / 192 cells;
491 more are matrix depth on states that already have a reference).

| State | Group | Why |
|---|---|---|
| `ps-menu` | **(a) attainable now** | PS-hold black system menu. Route: Game column row 0 → `key PS 1200` → grab. One boot. Closes the only non-class budget (26 dB / 0.95). |
| `info-indicator` | **(a) attainable now** | PS-tap controller plate. Route: `key PS 60`, grab at +400/+900/+1600 ms + info-bar `cburst`. |
| `dialog` | **(a) attainable now** | Any D1 "are you sure?" box. Route: `Corrupted Data` → Triangle → Down (Start→Delete) → Cross. Backup `dev_hdd0/game/TEST12345` first; exit with Circle only. Fallback: media item delete or Format Utility (last resort, never confirm). |
| `media-player-panel` | **(c) not attainable** | CORRECTION in REFERENCE-GAPS §"CORRECTION": a host-copied MP4 is invisible because media columns read the **MMS metadata database**, not a directory scan; RPCS3 presents no USB enumeration. Joins do-not-chase. |
| `column-tv` | **(c) not attainable** | Console draws 9 columns; `category_tv.xml` populates from `xil://localhost/list?t=tv` (needs a registered service). A real column-set difference, not a rig gap. |
| `page-download-management` | **(c) not attainable** | `seg_dlctrl` query returns nothing offline (Network column draws 4 rows, no Download Management). |
| `notification` | **(c) not attainable** | Every glyph class needs PSN/network/a running title/Bluetooth/USB hardware. One untested candidate: file-copy completion (tie to the media boot, evidence only). |
| `volume-hud` | **(c) not attainable** | BD-remote volume keys/IR only; RPCS3 emulates no BD remote; the page belongs to a player that can't start on an empty VFS. |

**Group (a) capture plan** (from `REFERENCE-GAPS.md` §5.1, one boot ≈ 12 min): backup `xRegistry.sys` +
`TEST12345`, boot with the day-pin scratch config (Console time offset 34,362 s, Date MM/DD/YYYY, Time
12-Hour, Vulkan 1920×1080 exclusive-fullscreen off), commit formats + Theme evidence frame, then in order
`info-indicator` → `ps-menu` → `dialog`, restore registry + input map byte-for-byte, re-hash. Import via
`bun work/parity-import.ts --map work/parity-references-map.json --manifest <batch>/MANIFEST.md`
(dry-run first). Expected: `--references-missing` 22/30 → 25/30 states.

**Do-not-chase** (REFERENCE-GAPS §7, each already tried): numeric error dialogs, game-launch error pages,
System Update failure page, disc/BD rows, psn-signin/trophy-collection, signed-in info-bar indicators,
copy/busy/progress dialogs, masked/password OSK, 720p console cells, the five native-only pages.

**The RPCS3 rig** is owned by a capture agent, never launched by `parity.ts` itself. Boot how-to:
`HANDOFF-PHASES.md` "Capturing the real XMB in RPCS3"; recipe in `work/parity-reference.json` `rpcs3`.
Frames live under `C:\dev\out\rpcs3-*` (gitignored, never committed). PS = Backspace by default
(`rpcs3/rpcs3/Input/keyboard_pad_handler.cpp:47`).

---

## Product / HTPC track

`PHASES-HTPC-2026-11-09.md` D1-D12 are done (Desktop Mode, single instance, start-at-sign-in, boot polish,
controller desktop, safe start, updater, first-run setup, display/CEC, bluetooth/wifi, phone remote, idle
cost). **Carried over and still open:**

- V1 release run on the real HTPC (the release is live at `ajgreenboy/XMB-Rewritten`; the on-hardware run
  is the owner's step).
- In-game XMB step 4 (fullscreen D3D capture) and step 10 (owner validation matrix) — see
  `INGAME-XMB-DESIGN.md`.
- P6 leftovers: editor field for the web-app window style, a stale browser profile still holding the lock.
- Photo Gallery Playlist/Network areas.
- RAF playback (needs real dynamic themes).

---

## Out of scope — tonight's render-fidelity session (do not duplicate)

> **STALE-AUDIT NOTE (2026-11-10 night):** the ranked gap lists in `FIDELITY-ROOT-BAR.md` §7 and
> `FIDELITY-VERDICT.md` §4 were measured on build `07a4fe4` and are partly superseded. **A1 (background
> field residue) and A3 (vita row glyph) are already fixed on HEAD** — A1 by ancestor `64b26b9`
> (pre-exposure lookup offset in `hdr.cpp:50`, re-measured `f45bc2d`: B6 strip 83.70 vs console 84.33),
> A3 by `shell.cpp:1426` (the vita row already uses `tex_album_icon`, not `item_tex_vitaformat`). Before
> re-dispatching any "blocks the look" item, **re-measure it on HEAD against `col-game-r00.png`** — the
> remaining items are A2 (ribbon crest inversion + brightness, `ribbon.cpp`), A5 (TV column, a product
> decision), and D8 (options menu 2 vs 5 rows). **Fresh measurement (night, HEAD `f45bc2d`):** top
> background 0,0,700,200 = **34.75 dB / 0.9937** (A1 confirmed fixed), ribbon band 0,330,1920,450 =
> **16.47 dB / 0.7138** (A2 confirmed still open — the dominant remaining gap), whole frame =
> **18.78 dB / 0.8263**.

Tonight's session owns the **A-list "blocks the look" render fixes**, each with a measured target in
`FIDELITY-ROOT-BAR.md` §7/§11:

1. Background field +10..21 luma residue (`hdr.cpp`, the `-0.0594` linear residue; also retires the B4 veil).
2. Ribbon crest inverted on the right half + too bright (`ribbon.cpp`, R2/R4).
3. TV column (10 vs 9; `shell.cpp` C1 decision).
4. Wrong row glyph for "PS Vita System Application Utility" (`shell.cpp`, gap 11; the 11.7 hunk exists,
   needs a 1:1 crop check).
5. Options structure (2 rows vs 5, `shell.cpp` D8).

Also tonight: the live borderless overlay (`view.cpp`/`game_capture.*`, DirectComposition) and the
in-game game-close transition. **Already closed — do not re-open** (`FIDELITY-ROOT-BAR.md` §8 "at ceiling"):
category grid, label typography, row-block geometry, BG1 plate, clock disc, focused-icon 2.56×, plate
ramps, bottom band, PS-tap plate, info-bar rect, indicator slots.

---

## Instruments (how to drive this)

```powershell
# build
$env:PATH="C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File native-xmb\build.ps1 -OutputDirectory C:/dev/xmb/out/native-xmb

# regression gate (native self-baseline, NOT firmware fidelity)
bun work/ab-layers.ts --all --exe C:\dev\xmb\out\native-xmb\re-ps3-native.exe
bun work/ab-layers.ts --state <name> --save-reference --exe ...   # only after an intended render change

# parity (firmware fidelity)
bun work/parity.ts --catalogue
bun work/parity.ts --references-missing                 # 8 states / 192 cells
bun work/parity.ts --references-missing --matrix        # 491 depth cells
bun work/parity.ts --capture --score --state root,ps-menu
bun work/parity.ts --regression --previous <scorecard.json>
bun work/parity-import.ts --map work/parity-references-map.json --manifest <batch>/MANIFEST.md --dry-run
```

The parity default exe is `C:/dev/out/native-parity/re-ps3-native.exe`, default flash
`C:/dev/rpcs3/bin/dev_flash`; `--exe`/`--flash` override.

## Conventions (hard rules)

- Commit as `git -c user.name=Al -c user.email=ajgreenboy@gmail.com commit`, no AI attribution.
- **No firmware, keys, captures, or binaries committed.** Reference frames, `out/`, `dist/`,
  `firmware_keys.inc` are gitignored. The repo is public (`ajgreenboy/XMB-Rewritten`).
- A fitted constant must be labelled `fit` and carry its bounding measurement; an RCO value must be read at
  runtime or generated from a documented extraction with provenance. No hardcoded number where an RCO value
  exists.
- A Tier B budget changes only with a dated reason (`work/parity-reference.json` `budgets.updated` +
  `revisionNote`); the tool refuses an undocumented change.
- The `ab-layers` gate is a self-baseline (repeatability), never a firmware-fidelity claim. Re-save a
  reference only after an intended render change, and only the states that moved.
- Sequential builds only: concurrent `build.ps1` runs collide on `out/native-xmb`. Parallel subagents must
  not share a write fence (see `PARITY-PHASES.md` "Dispatch queue" for the per-phase fences).

## Key source docs (read before diving into any one area)

`PARITY-SCORECARD.md` (tiers + corpus + numbers), `FIDELITY-VERDICT.md` (consolidated gap ranking),
`FIDELITY-ROOT-BAR.md` (root-frame element table + the 2026-10-29 fix pass), `REFERENCE-GAPS.md`
(the 8 states + capture plan + do-not-chase), `FIDELITY-SURFACES.md` / `-MAP.md` (breadth),
`FIDELITY-MEDIA.md` (players/OSK), `FIDELITY-BEHAVIOUR.md` (behaviours), `SETTINGS-COVERAGE.md` (143
settings rows), `PHASES-HTPC-2026-11-09.md` (product track), `archive/PARITY-PHASES.md` (the P0-P11 plan).
