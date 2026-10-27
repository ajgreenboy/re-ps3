# Triangle options menu (item-level)

Status: implemented for one surface, 2026-10-26. See section 6 for the one open gate issue, which is
caused by a concurrent change outside this work.

## 1. What was missing

`audit/FIRMWARE-PARITY-GAPS.md` M3/B1 and top-ten item 4: Triangle on a selected item opened nothing.
The shell had an `Action` enum, `activate()` and `back()`, but no menu model, no per-item options and no
focus transfer, and `View::drawOptionsPanel` existed only for settings choice columns (a synthetic page
whose rows are all `SetSetting`).

Firmware behaviour taken as the target (manual evidence): Triangle opens and hides an options list; it
is a panel on the right, not a new column or page; the listed items vary by category (Play/Start/View,
Eject Disc, Delete, Copy, Information, Add to Playlist, Display All, Sort By, Group Content, Delete
Multiple, Copy Multiple); Circle closes.

## 2. First surface: a local folder page (Video Library)

Chosen because it is provable from the existing `--script` harness and because both a folder row and a
file row exist there:

- Data directory `out/native-reference/store/video-data` already points `library/video` at
  `out/native-reference/video-library` (Movies, Shows, Top Level Clip.mp4).
- The page is a real `Page` (`pages_.last().directory` non-empty), so its rows are `Action::Folder` or
  `Action::File` entries that carry a real path in `Entry::value`.
- Every command offered maps to code that already runs from Cross on the row.

The Game column (Steam Store / All Games) is the next surface; section 7 lists what changes there.

## 3. The options, and what each one does

The list is derived from the selected entry in `Shell::buildOptions()`, so every content surface gets
the same panel; the surface only decides which labels and rows are honest.

| Option | Shown when | What Cross does |
|---|---|---|
| `Open` / `Play` / `View` / `Start` | entry action is not `Unavailable` | Calls `Shell::activate()`: the entry's own action, exactly as Cross on the row. Label by kind: `Open` for Folder/Library/SteamFolder/JellyfinFolder/store rows, `Play` for File (video/music), Launch, LaunchSteam, LaunchApp, JellyfinPlay, `View` for a photo file and for Information rows, `Start` otherwise. |
| `Information` | always | Closes the panel and shows the existing information page (title, icon, two rules, `Key: value` rows) built from the entry: `Kind:` (Folder / Video / Music / Image / Game / Steam game / Settings / Unavailable / Item), `Detail:` when the entry has one, then `Path:` for a folder/file/library entry (`Value:` otherwise). |
| `Sort By` | only on a real directory page with 2+ rows | Opens a second level of the same panel: `Name (A-Z)`, `Name (Z-A)`, `Close`. Choosing a sort reorders the page's own entries (folders first, then titles, case-insensitive, stable) and keeps the previously selected row selected. It is the page list that changes, not a facade. |
| `Close` | always | Closes the panel (same as Circle when there is no submenu). |

Deliberately absent, and why:

- **Display All**: on this build a page already lists every row it has; nothing is grouped, filtered or
  hidden, so Display All would be a no-op and is not offered.
- **Delete, Copy, Delete Multiple, Copy Multiple, Group Content, Add to Playlist, Eject Disc**: no safe
  native implementation exists (delete/copy would touch the user's media files; the disc and playlist
  surfaces do not exist). Not shown rather than shown-and-inert.
- **Play/Start on an `Unavailable` row**: omitted, so a row the shell cannot act on only offers
  Information and Close.

## 4. Input handling

While `Shell::optionsOpen()` is true the panel owns input; the XMB behind it receives nothing:

- Keyboard: Up/Down move inside the panel; Return/Enter is Cross; Escape/Backspace is Circle (step back
  from a submenu, then close); `T` is Triangle (hide the whole menu). `T` is the Triangle binding added
  for the XMB: `R` was already Repeat on media and `Q`/`E` were photo rotate, and no key opened the
  item menu before this change. `TRIANGLE` is the `--script` token for it (`Qt::Key_T`).
- Controller: D-pad Up/Down move, Cross runs, Circle steps back/closes, Triangle dismisses. In normal
  navigation Triangle (`XINPUT_GAMEPAD_Y`) opens the panel except during media playback, where Triangle
  keeps its repeat function. The controller path is coded but was not exercised: no pad is attached to
  this machine, and `work/ab-layers.ts` has no controller injection.
- Gating: `keyPressEvent` handles the panel before the arrow/D-pad paths, and `pollController` returns
  from its own panel block before the normal navigation block, so arrows/Cross/Circle/Triangle cannot
  also move the XMB. A LEFT while the panel was open was captured to prove it (section 6, state still
  `category=video depth=1 selection=0`).
- The panel cannot open while a message/information page, media, the PS menu or a folder scan is up, and
  not on a settings choice column (that column already is the options panel).

## 5. Rendering

`View::drawOptionsPanel` was generalised instead of duplicated: it now takes a list of titles, a focus
position, an alpha, a slide amount and an optional heading. The settings choice columns pass their
`Column` titles/position exactly as before, and the item menu passes `Shell::options()` and the
`optionsPanelEase_`/`optionsOpenEase_` paf curves.

- Same visual language as the settings panel: translucent white strip from x 1330 to the right edge, a
  bright left edge, options at 23 px on a 40 px pitch centred on y 505, the focused option glows.
- The item panel slides in from the right over 200 ms (paf accel mode 5, the same curve as the focus
  moves) and moves between rows on the same curve. A submenu draws its name (`Sort By`) above the list.
- The panel is drawn only while `Shell::optionsOpen()`; it is not pushed onto `pages_`, so it is not a
  level, `depthPosition_` is untouched, and the page under it keeps its layout.
- Existing named states are unaffected because none of them opens the panel; the UI signature only
  gains the panel term while it is open or still sliding.

## 6. Verification

Build (run after the last source edit; `re-ps3-native.exe` 2026-10-26 21:24:40, later than every edited
source file):

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-options
```

Capture executable: `C:/dev/out/native-options/re-ps3-native.exe`, flash `C:/dev/rpcs3/bin/dev_flash`,
data directory a copy of `out/native-reference/store/video-data` (preferences point `library/video` at
`out/native-reference/video-library`).

### 6.1 Open, move down, Information, close

Script (exact):

```
full,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,RIGHT,RIGHT,RIGHT,RIGHT,wait,wait,ENTER,wait,wait,wait,wait,wait,TRIANGLE,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-open.png,DOWN,wait,ENTER,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-info.png,wait,ESC,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-closed.png,quit
```

Log (`XMB_LOG`), in order:

```
shell: options opened category= video depth= 1 entry= C:/dev/xmb/out/native-reference/video-library/Movies rows= 4 Open|Information|Sort By|Close
script: grab .../options-open.png QSize(1920, 1080) saved
shell: options selection 1 Information
shell: options information title= Movies rows= 2
script: grab .../options-info.png QSize(1920, 1080) saved
script: grab .../options-closed.png QSize(1920, 1080) saved
```

Captures (local, not committed):

- `out/native-reference/options/options-open.png`: folder page with the panel on the right,
  `Open / Information / Sort By / Close`, glow on Open.
- `out/native-reference/options/options-info.png`: information page: Movies icon/title, rules,
  `Kind  Folder`, `Path  C:\dev\xmb\out\native-reference\video-library\Movies`, `Back` hint.
- `out/native-reference/options/options-closed.png`: the folder page again, no panel.

### 6.2 Input isolation, submenu, back, sort applied, Triangle closes

Script (exact):

```
full,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,RIGHT,RIGHT,RIGHT,RIGHT,wait,wait,ENTER,wait,wait,wait,wait,wait,TRIANGLE,wait,LEFT,wait,grab=C:/dev/xmb/out/native-reference/options/panel-after-left.png,DOWN,DOWN,wait,ENTER,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/options/sort-menu.png,ESC,wait,wait,grab=C:/dev/xmb/out/native-reference/options/back-to-items.png,DOWN,DOWN,wait,ENTER,wait,wait,DOWN,wait,ENTER,wait,wait,grab=C:/dev/xmb/out/native-reference/options/sorted.png,TRIANGLE,wait,wait,TRIANGLE,wait,grab=C:/dev/xmb/out/native-reference/options/closed-triangle.png,quit
```

Log:

```
shell: options opened category= video depth= 1 entry= .../Movies rows= 4 Open|Information|Sort By|Close
(grab panel-after-left.png: state category=video depth=1 selection=0 -> LEFT did not reach the XMB)
shell: options selection 1 Information
shell: options selection 2 Sort By
shell: options sort menu opened
(grab sort-menu.png)
shell: options back to item list
(grab back-to-items.png)
shell: options selection 1 Information
shell: options selection 2 Sort By
shell: options sort menu opened
shell: options selection 1 Name (Z-A)
shell: options sorted page video-library Z-A 3 rows
(grab sorted.png: rows now Shows, Movies, Top Level Clip.mp4; state selection=1 keeps Movies)
shell: options opened ... rows= 4 ...
shell: options closed
(grab closed-triangle.png: no panel)
```

`Sort By` is therefore a real action on the page's entries, and Circle from the submenu returns to the
item list (the panel stays open) while Triangle closes everything.

### 6.3 Play on a video file

Script tail: open the folder page, `DOWN,DOWN,TRIANGLE`, grab, `ENTER`, grab. Log:

```
shell: options opened category= video depth= 1 entry= .../Top Level Clip.mp4 rows= 4 Play|Information|Sort By|Close
shell: options activate Play entry= .../Top Level Clip.mp4
(script: state ... mediaOpen:true, mediaPaused:false, mediaDuration:20000, mediaPosition:1322)
```

Cross on the `Play` row ran `Shell::activate()`, i.e. the same media path Cross on the row uses.

### 6.4 Settings choice panel regression

Script: `LEFT` x5 to Settings, six `DOWN` to Theme Settings, `ENTER`, `ENTER` on Theme. The existing
choice panel still renders in the same place with the same style (`settings-panel.png`, one row
`Original`), so the `drawOptionsPanel` generalisation kept that path intact.

## 7. Gate result

Mandated command:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-options/re-ps3-native.exe
```

Final result on the build above (`re-ps3-native.exe` 2026-10-26 21:24:40, later than every edited source):

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---:|---:|---:|---|
| frame | 17.95 | 18.02 | -0.07 | .8286 | .8301 | -.0015 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget`. No harness file, reference or threshold was edited by this work.

Two notes on that number, for honesty:

- The clock and info-bar capture baselines in `work/ab-layers.thresholds.json` were refreshed to the
  American clock format (MM/DD, 12-hour) by the clock/info-bar work, per that file's revisionNote; the
  pinned 12:32 renders as `10/25 12:32 PM` in the gate log. The panel is not involved: it draws nothing
  in the idle Game-root state, which is what this capture is.
- An intermediate run of the same command, taken before that baseline refresh landed, failed only
  `clock text` (11.39 vs the then-current 13.51) and `info bar` (14.78 vs 17.12) because of exactly that
  format change. The same capture with preferences pinning the recorded DD/MM + 24-hour format scored
  13.61/.6226 clock and 17.21/.7114 info bar, matching the revisionNote's stated reproduction as well.
  That history is noted so a reader with the old thresholds knows why the numbers moved.

## 8. Extending the panel to other surfaces

The panel is surface-agnostic. To add options for the next surface (Game column's All Games page,
Jellyfin, Steam Store sections):

1. Add a command to `OptionCommand` (`src/shell.h`) and a row in `Shell::buildOptions()` under the
   condition that makes it honest (e.g. `!pages_.last().directory.isEmpty()` already gates Sort By).
2. Handle the command in `Shell::optionsActivate()`. Panel navigation, drawing, slide-in, input gating,
   logging, the `TRIANGLE` script token and the sound cues are already shared.
3. If a new option needs a second level, follow `OptionCommand::SortMenu`: push the current list onto
   `optionsParent_`, set `optionsNested_` and `optionsTitle_`, and Circle will step back.

Known differences by surface today:

- **Steam folders** (All Games, Not Installed, collections) have no `Page::directory`, so they get
  `Open` (SteamFolder rows) or `Play` (game rows), `Information` and `Close`, but no `Sort By`. If a
  sort is wanted there it should sort `steamFolderEntries()` and is a separate, explicit change.
- **Game column root** rows are folders/store rows, so the panel there is `Open`/`Information`/`Close`.
- **Display All** stays out until a surface actually groups or hides rows.

## 9. Files touched

- `native-xmb/src/shell.h`: `OptionCommand`, `MenuOption`, public panel API, private state and helpers.
- `native-xmb/src/shell.cpp`: `toggleOptions`, `optionsMove`, `optionsActivate`, `optionsClose`,
  `dismissOptions`, `buildOptions`, `activateLabel`, `entryKind`, `showEntryInformation`,
  `sortPageByName`; all log through `qInfo()` (`shell: options ...`).
- `native-xmb/src/view.h`: `optionsPanelEase_`, `optionsOpenEase_`, `optionsWasOpen_`.
- `native-xmb/src/view.cpp`: generalised `drawOptionsPanel`, item panel draw (open only), paf curves and
  UI signature term, keyboard and controller blocks, `T` binding.
- `native-xmb/src/main.cpp`: `TRIANGLE` script token (`Qt::Key_T`).
- `native-xmb/audit/OPTIONS-MENU.md`: this document.

Not touched: `settings_menu.cpp`, `preferences.cpp`, `INFO-BAR.md` (another agent's fence), and the
verification harness.
