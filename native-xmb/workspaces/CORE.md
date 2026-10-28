# Workspace handoff: core

**Mission**: own the menu model - the columns, pages, selections, actions, settings and profile state that
everything else renders and acts on. Core is the integration hub: if it is wrong, every screen is wrong.

## Owns (write)

`src/shell.cpp`, `src/shell.h`, `src/settings_menu.cpp`, `src/settings_menu.h` (if present),
`src/preferences.*`, `src/profiles.*`, `src/catalog.*`, `src/content_registry.*`, `src/app_editor.*`.

## Must not edit

`view.*`, `icons.*`, `text_renderer.*`, `font_raster.*`, `hdr.*`, `ribbon.*`, `particles.*`, `p3t.*`,
`steam_*`, `jellyfin_*`, `subtitles.*`, `host.*`, `pad.*`, `main.cpp`, `CMakeLists.txt`, `work/**`.

## Boundaries it owns for others

`src/shell.h` is the public surface for the ui and services workspaces: `Entry`, `Page`, `Action`,
`OptionCommand`, `MenuOption`, the page API, and the signals (`changed`, `requestQuit`, `psTapIndicator`,
`requestLibrary`, `requestWallpaper`, ...). Changing it is a broadcast: announce, then dependents rebase.

## Build and run

```powershell
# from a core worktree
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\ws-core
& C:\dev\out\ws-core\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <scratch>
```

Scripted verification uses the existing tokens (`full`, `wait`, `LEFT/RIGHT/UP/DOWN`, `ENTER`, `ESC`,
`TRIANGLE`, `F10`, `VOLSET=`, `NOTIFY=`, `idle=`, `pause=`, `grab=`, `quit`) and `XMB_DUMP_ENTRIES=<file>`.

## Verification: what green means here

- `bun work\ab-layers.ts --all --exe C:/dev/out/ws-core/re-ps3-native.exe` - layers 6/6, canonical 6/6,
  named states 4/4 (until more states exist).
- For a menu change: a scripted route that reaches the changed page, its `XMB_DUMP_ENTRIES`/`.folder.json`/
  `.store.json`/`.dl.json` assertion, and a capture whose sidecar shows the expected ids/rows.
- Profiles: a round-trip check that switches profile, changes a setting, switches back and reads it from disk.

## Current state (2026-10-27)

Landed and green:

- Category model, pages stack, actions, options panel API, PS menu state, notifications queue, volume HUD
  dispatch, Continue Watching page + resume-record migration, Steam rows (store, All Games with collection
  folders, Not Installed, family), download page, Jellyfin rows/search/sign-in page/tracks, profiles adoption and
  per-profile preferences (`profile/<id>/<key>`), tracked launch/return lifecycle.
- Phase 3's firmware-driven column reader is **implemented but not wired**: `src/xmbml_view.*` resolves every
  category's rows from `explore/xmb/category_*.xml`; the shell hunks are in
  [../audit/MENUS-XMBML.md](../audit/MENUS-XMBML.md) section 7.1-7.5. Applying them reorders Friends (Block List
  first, per the firmware) and adds trademark signs to two Game labels, so a named-state refresh is required.
- Phase 4's shell hunks are **not applied**: Auto-Off -> `host::requestDisplayOff()` and `touch()` re-arm, in
  [../audit/SESSION-POWER.md](../audit/SESSION-POWER.md).
- Phase 6's shell hunks are **not applied**: Steam Account page + two-step confirmation, `+app_stop` evidence
  read, friend avatars, honest `Unavailable` wording, disc note, in
  [../audit/SERVICES-BREADTH.md](../audit/SERVICES-BREADTH.md) hunks A-E.

In flight at this commit: `view.cpp`/`shell.cpp` are being edited by the dialogs implementation pass (it writes
both files this wave); the Phase 5 verification agent owns `work/ab-layers.*`. Do not start a core task until the
dialogs pass lands and the harness owner releases `work/`.

## Evidence index

[../audit/OPTIONS-MENU.md](../audit/OPTIONS-MENU.md) (panel + options actions + the free-hand panel findings),
[../audit/MENUS-XMBML.md](../audit/MENUS-XMBML.md) (columns, profiles), [../audit/ITEM-MENU-INFO-FIRMWARE.md](../audit/ITEM-MENU-INFO-FIRMWARE.md)
(the real options menu is `expage_menu`; **no "Close" string exists**; eject is "Remove Disc"),
[../audit/NOTIFICATIONS.md](../audit/NOTIFICATIONS.md), [../audit/SESSION-POWER.md](../audit/SESSION-POWER.md),
[../audit/SERVICES-BREADTH.md](../audit/SERVICES-BREADTH.md), [../audit/MEDIA-CONTROLS.md](../audit/MEDIA-CONTROLS.md),
[../audit/STEAM-INTEGRATION.md](../audit/STEAM-INTEGRATION.md), [../audit/JELLYFIN-NATIVE.md](../audit/JELLYFIN-NATIVE.md),
[../audit/ISA.md](../ISA.md) (criteria state), [../audit/FEATURE-COVERAGE.md](../audit/FEATURE-COVERAGE.md).

## Gotchas already paid for

- `shell.cpp` is edited concurrently by agents who are told they own "their" feature; only one writer. Return
  hunks instead.
- A row count in a harness state assertion can be invalidated by a feature in this workspace (the
  `jellyfin-signin` state expects 5 rows; DPAPI added a sixth). Tell tooling when row counts change.
- `Entry::value` is displayed by the "not available" path; wording belongs to core and is dump-checkable.
- Preferences are v2 profiles now: a v1 file is migrated on load; do not parse `preferences.json` by hand.
- `psMenuItems()` currently doubles as "is a title running" in the PS watcher; if you change it, keep
  `psMenuItems().size()>2` semantics or add `titleRunning()`.

## First tasks

1. Apply Phase 3's 7.1-7.5 hunks; verify each column's dump against [../audit/MENUS-XMBML.md](../audit/MENUS-XMBML.md)
   section 3, then hand the Friends reorder and Game label changes to tooling for a deliberate reference refresh.
2. Apply Phase 4's Auto-Off and `touch()` hunks; verify with an `idle=`/`pause=` scripted run that the XMB stays
   alive and logs the display-off request.
3. Apply Phase 6's hunks A-E; verify the account page strings/step machine with a dry run (no real restart) and
   the `Unavailable` wording via the catalogue dump.
4. Replace the free-hand options-panel rules with the firmware findings: labels from
   `assets_.text("explore_plugin_full", msg_*)`, **remove the invented Close row**, 100-unit open, BG0/BG1 plate
   behaviour (the object tree is in [../audit/ITEM-MENU-INFO-FIRMWARE.md](../audit/ITEM-MENU-INFO-FIRMWARE.md)).

## Report format

Branch/commit, build dir, gate result, the scripted route + dump assertion, captures with sidecars, files
touched, and an explicit "hunks needed elsewhere" list.
