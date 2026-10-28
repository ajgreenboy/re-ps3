# Workspace handoff: ui

**Mission**: own everything the user sees on the XMB page itself - the column/page drawing, glyph rendering,
icon materials, dialogs, the OSK and the item panels. If a pixel is wrong, it is probably here.

## Owns (write)

`src/view.cpp`, `src/view.h`, `src/text_renderer.*`, `src/font_raster.*`, `src/icons.*`, `src/dialogs.*`
(when the dialogs module lands), `src/osk_layout.*` (future), `src/photo_viewport.*`,
`src/media_controls.*`.

## Must not edit

`shell.*`, settings/preferences/profiles/catalog, `hdr/ribbon/particles/coldboot/p3t`, service modules,
`main.cpp`, `CMakeLists.txt` (integration owns the source list), `work/**`.

## Boundaries

- `src/view.h` is yours and is consumed by core (`View` construction, `setRenderClock`, `setBackgroundClock`,
  `setFocusTime`, `setIntroEnabled`, `replayKeyTap`, `replayPsTap`, `replayPsHold`). Announce changes.
- You **read** `shell.h`; never add methods to it. If a screen needs new state, request it from core.

## Build and run

```powershell
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\ws-ui
& C:\dev\out\ws-ui\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <scratch> `
    --render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000
```

## Verification: what green means here

- Canonical capture: `bun work\ab-layers.ts --capture --exe C:/dev/out/ws-ui/re-ps3-native.exe` - 6 layers
  inside the 0.5 dB / 0.004 SSIM budget (current baselines: frame 18.02/0.8301 stored-pair, canonical frame
  18.28/0.8285, category icon row 20.89/0.8939-21.05/0.8950, item label 11.94/0.3909, clock 11.39/0.4105,
  info bar 14.78/0.6117).
- A visual change must show its own evidence: a before/after crop score, or a measured table against the
  firmware frame/reference. "Looks closer" is not evidence.
- Determinism: two `--render-clock 30` captures hash-equal.
- After an intended rendering change, **ask tooling** to refresh the named-state references (`--states
  --save-reference`); never do it from this workspace mid-task.

## Current state (2026-10-27)

Landed:

- Icon material: the firmware **glass program is the normal path at full `_Ambiant.w`**; opaque fallback for
  dimmed states; category material follows the traced four-preset `ICONS.mnu` clock keyframes
  ([../audit/ITEM-ICONS.md](../audit/ITEM-ICONS.md), [../audit/ICON-MATERIAL-CLOCK.md](../audit/ICON-MATERIAL-CLOCK.md)).
- Text: FreeType coverage, sub-pixel phase, coverage-union mask, `elideToWidth` wired with the firmware's
  separate ellipsis, metric assertions at every label size, caret from `TextRenderer::measure`
  ([../audit/TEXT-RASTER.md](../audit/TEXT-RASTER.md), [../audit/TEXT-RENDERER.md](../audit/TEXT-RENDERER.md)).
- UI glare measured and deliberately not adopted (fails at every strength); switch `XMB_UI_GLARE` stays off.
- PS menu, PS tap indicator, controller/charge panel, host tap/battery hooks (hunk recorded in
  [../audit/SESSION-POWER.md](../audit/SESSION-POWER.md)), volume HUD (fitted - see below), media overlay,
  photo viewer, options panel (free-hand - see below).

In flight at this commit: the **dialogs implementation** pass writes `view.cpp`/`view.h` (and `shell.*` for its
own state); the OSK firmware analysis is finishing. Treat `view.cpp` as busy until both land.

Known free-hand surfaces with firmware answers now available (not yet implemented):

- **OSK**: reference capture + layout spec arriving from the OSK analysis; the current keyboard is invented.
- **Dialogs** ([../audit/DIALOGS-FIRMWARE.md](../audit/DIALOGS-FIRMWARE.md)): no dialog RCO; plate is flat
  12x12 `tex_msgdialog_bg` (no nine-slice), two centre-bright bars, hint row from `page_btnnavi` + `system_plugin`
  labels at x 1155, fades 100/200/1000; the current 135-alpha dim is invented and the info-page row labels use
  strings that do not exist in firmware.
- **Item options menu + information panel** ([../audit/ITEM-MENU-INFO-FIRMWARE.md](../audit/ITEM-MENU-INFO-FIRMWARE.md)):
  labels from `assets_.text("explore_plugin_full", msg_*)`, **no Close row** (it is invented), "Remove Disc" not
  "Eject Disc", BG0/BG1 plates, 100-unit open; info panel plate is the 9-slice `tex_indi_bg` (interior black
  alpha 13), not `tex_infobar`, with a drawn 32 px analog clock and five slots (message/chat/online/avatar/Plus).
- **Media panels** ([../audit/MEDIA-PANELS-FIRMWARE.md](../audit/MEDIA-PANELS-FIRMWARE.md)): the video panel is
  a 25-button 13x3 matrix over a time page; the music panel exists twice in firmware and not at all natively;
  the **volume display is a `PhLevelMeter` of 20x40 `tex_default_lvmeter_*` segments, 150 ms fade, no caption or
  percentage** - the current HUD invents the plate, caption, percentage and 3000/250 timing; the photo viewer
  has a 20-button panel and screen planes.

## Evidence index

[../audit/ITEM-ICONS.md](../audit/ITEM-ICONS.md), [../audit/TEXT-RENDERER.md](../audit/TEXT-RENDERER.md),
[../audit/TEXT-RASTER.md](../audit/TEXT-RASTER.md), [../audit/DIALOGS-FIRMWARE.md](../audit/DIALOGS-FIRMWARE.md),
[../audit/ITEM-MENU-INFO-FIRMWARE.md](../audit/ITEM-MENU-INFO-FIRMWARE.md),
[../audit/MEDIA-PANELS-FIRMWARE.md](../audit/MEDIA-PANELS-FIRMWARE.md),
[../audit/LAYOUT-TRACE.md](../audit/LAYOUT-TRACE.md), [../audit/INFO-BAR.md](../audit/INFO-BAR.md),
[../audit/PAGE-MOVE.md](../audit/PAGE-MOVE.md), [../audit/OPTIONS-MENU.md](../audit/OPTIONS-MENU.md),
[../audit/NOTIFICATIONS.md](../audit/NOTIFICATIONS.md), [../audit/SUBTITLES.md](../audit/SUBTITLES.md).

## Gotchas already paid for

- The UI layer is 2x supersampled with its own signature; a transient overlay (notification, HUD, fade) must be
  in the signature or it will not repaint.
- Elision changes a label's pixels by one glyph only; measure before claiming.
- Named states are native self-baselines: a rendering change moves them, and refreshing them mid-task hides a
  regression. Ask tooling after the change is accepted.
- `view.cpp` is the second integration hub: one writer. Return hunks for anything outside it.

## First tasks

1. Implement the **OSK** from its firmware spec: load the layout at runtime from the RCO, draw the real key
   textures/rows/entry field/caret, then compare regions against the RPCS3 OSK captures.
2. Implement the dialogs fix list (surface split, no dim, flat rules, measured header/body/hint geometry, PAF
   fades, firmware strings) - the pass is in flight; continue it rather than starting a second one.
3. Item-menu + information-panel fixes (RCO labels, no Close row, BG plates, `tex_indi_bg` plate, analog clock,
   five slots, busy icon, 30x30 hints).
4. Media panels: real volume meter, video panel matrix, music panel (absent today), photo chrome.
5. P3/P4 residuals: dimmed-glass class (blocked on the background brightness layer - coordinate with scene),
   label glow peak, item-icon visible size, chromatic fringe over the ribbon.

## Report format

Branch/commit, build dir, per-layer numbers before/after, kept vs reverted with the measured reason, captures
and hashes, files touched, hunks needed elsewhere, and whether named references now need a refresh.
