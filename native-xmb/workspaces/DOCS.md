# Workspace handoff: docs

**Mission**: own the evidence documents under `native-xmb/audit/` so that the project's claims stay accurate,
dated and checkable. Docs is not free writing: every correction cites the source line, RCO object, capture or
command that proves it, and measurements are never deleted - they are dated and superseded.

## Owns (write)

`native-xmb/audit/*.md` and `native-xmb/audit/README.md`, except the two documents owned by tooling
(`VERIFICATION-HARNESS.md`, `PARITY-SCORECARD.md`).

## Must not edit

Application source, `CMakeLists.txt`, `work/**`, `HANDOFF-*.md`/`PHASES-*.md` (integration owns those), and any
document owned by tooling.

## What a good audit document contains

1. What was measured, on which firmware artifact/build, when, and by which command or tool.
2. The evidence path for every claim (RCO container and object/offset, decompile function, capture frame).
3. The distinction between **traced**, **measured**, **fitted** and **invented** - the last is named as a defect,
   not hidden.
4. A dated correction section when reality moves; old text kept and marked superseded rather than rewritten.
5. The exact reproduction command a later session can paste.

## Build/run needs

Docs rarely builds; when verifying a claim it may run the app or a CLI read-only. It must not build into
`out/native-xmb`, must not run RPCS3 while another agent owns the rig, and must not modify the tree it is
describing.

## Current state (2026-10-27)

The audit set is large (~50 documents). Documents that are current and used by other workspaces:

| Doc | Owner subject | State |
|---|---|---|
| `FIRMWARE-PARITY-GAPS.md` | whole-project register | current-ish; scores predate the last night's work, status banner added |
| `PIPELINE.md` | layer map | current after the parity sweep |
| `ITEM-ICONS.md`, `ICON-MATERIAL-CLOCK.md` | icon material | current, includes the glass-path decision and the 1.1022x wiring |
| `TEXT-RASTER.md`, `TEXT-RENDERER.md`, `FONT-MATCHING.md` | text | current; phase/glyph union recorded |
| `PARTICLES.md`, `THEMES.md`, `PAGE-MOVE.md`, `LAYOUT-TRACE.md` | scene/motion | current; page move traced at 18 frames/~300 ms |
| `DIALOGS-FIRMWARE.md`, `ITEM-MENU-INFO-FIRMWARE.md`, `MEDIA-PANELS-FIRMWARE.md` | free-hand surfaces exposed | new: dialogs have no RCO; options menu is `expage_menu`; volume is a `PhLevelMeter` |
| `MENUS-XMBML.md`, `XMBML-NETWORK.md` | firmware-driven columns | current |
| `STEAM-*.md`, `SERVICES-BREADTH.md` | services | current, with explicit unproven lists |
| `JELLYFIN-NATIVE.md`, `SUBTITLES.md`, `MEDIA-CONTROLS.md`, `INTERACTION-AUDIO.md` | media/audio | current |
| `SESSION-POWER.md`, `PS-BUTTON.md`, `PS-INGAME-HOOK.md`, `NOTIFICATIONS.md` | HTPC behaviours | current |
| `HANDOFF.md`, `HANDOFF-2026-10-26.md`, `PHASE-CONTINUATION.md`, `MEDIA-CONTINUATION.md` | old snapshots | historical, labelled |
| `FUNCTIONS.md`, `FEATURE-CATALOG.md`, `FEATURE-COVERAGE.md` | inventory | partially stale; the parity sweep fixed the rows it could prove |

## Gotchas already paid for

- The docs drifted ahead of the code in a whole pass (28 claims corrected on 2026-10-26) because features landed
  while docs were being written. Date every claim and prefer function names over line numbers.
- Line numbers rot within hours in a shared tree; use function names and object names.
- Never commit firmware-derived content (dumps, decompiles, extracted textures): they belong under `out/`.
- Two docs claiming different clock baselines forced a consistency grep; do that before finishing.
- Do not "tidy" historical measurements - they are the record of what was true then.

## First tasks

1. After the in-flight feature wave lands, correct the register's scores and rows (it currently predates: glass
   icon path, dialog firmware findings, options-menu findings, media-panel findings, Auto-Off, account switch,
   DPAPI sign-in, parity P0-P2).
2. Bring `FUNCTIONS.md`, `MEDIA-CONTINUATION.md` and `PHASE-CONTINUATION.md` into the same dated-correction
   convention as the rest.
3. For every new firmware analysis, add one index line to `audit/README.md` so the surface is findable.
4. Keep the "free-hand vs firmware-derived" ledger honest: when an implementation replaces an invented design,
   record what was invented (e.g. the dialogs dim, the options Close row, the volume HUD plate/caption/percentage)
   and the commit that removed it.

## Report format

Files changed, the claims corrected (doc:line, old, new, evidence), the claims deliberately left (and why), and a
note on any document that needs the owning workspace to re-verify it.
