# REFERENCE GAPS

The capture plan for the firmware-referenced states that still have no RPCS3 reference: which ones this
console can produce, by what exact route, what has to be true first, what can go wrong, what the frame buys
once imported, and which ones are not worth chasing again.

Written 2026-10-28 (America/Chicago) by a read-only pass. Nothing was captured, no RPCS3 process was
started, no source, contract, catalogue or manifest was edited. The only file written is this one.

Sources, all read this pass:

- `bun work/parity.ts --references-missing` and `--references-missing --matrix` (the live gap list).
- `work/parity-catalogue.json` (state ids, classes, routes, regions), `work/parity-reference.json`
  (matrix, budgets, `rpcs3.perCellRecipe`), `work/parity-import.ts` (the import contract),
  `work/parity-references-map.json` (the map's row shape).
- `native-xmb/audit/PARITY-SCORECARD.md` sections 10-15.
- The three batch manifests: `C:/dev/out/rpcs3-ref-2026-10-28/MANIFEST.md` (batch 1),
  `C:/dev/out/rpcs3-ref2-2026-10-28/MANIFEST.md` (batch 2), `C:/dev/out/rpcs3-day-2026-10-28/MANIFEST.md`
  (day window).
- `native-xmb/audit/PS-BUTTON.md`, `NOTIFICATIONS.md`, `DIALOGS-FIRMWARE.md`, `HTPC-IDEAS.md`,
  `INFO-BAR.md`, `STEAM-DOWNLOADS.md`.
- The console's own XMBML: `C:/dev/rpcs3/bin/dev_flash/vsh/resource/explore/xmb/category_network.xml` and
  `category_tv.xml`.
- The rig's own tool (`C:/dev/out/rpcs3-day-2026-10-28/tools/refcap3.ps1`) and the frames the earlier PS
  capture left behind (`C:/dev/xmb/out/native-reference/ps-menu/`).

## 0. The count, corrected before anything else

The brief for this pass says "13 firmware-referenced states still have no reference". The tool and the
scorecard both disagree with that phrasing, and the difference matters because it changes the size of the
work by a third:

| Measure | Value | Source |
|---|---:|---|
| catalogue states | 38 | `--catalogue` |
| capturable states | 35 | `--catalogue` |
| native-only states that owe no reference | 5 | `--references-missing` (12.4) |
| firmware-referenced capturable states (the denominator) | 30 | `--references-missing` |
| canonical (`original/day/1080p`) cells with a reference | 22 / 30 | `--references-missing` |
| matrix cells with a reference | 37 / 720 | `--references-missing --matrix` |
| matrix cells with no reference | 683 | derived: 720 - 37 |
| **states with no reference anywhere** | **8** | `--references-missing`, and 12.8, 13.3 |
| states the harness reports "without a reference" in a 35-state run | 13 | section 15: 8 owing states + the 5 native-only states that owe none |

So the real debt is **8 states**, not 13. The 13 in section 15 is "22 states measured + 13 without a
reference = 35 capturable", and five of those thirteen are native surfaces that will never have a firmware
frame (`page-game-all-games`, `page-video-library`, `page-steam-store-sections`, `page-jellyfin-signin`,
`continue-watching`; `noFirmwareReference: true`, 12.4). Asking the rig for those five was the bug 10.6
named and 12.4 fixed.

The 683 empty cells split in two, and only the first half is this plan's business:

- **192 cells are eight whole states** (8 x 24). Every one of them is empty at every theme, background and
  resolution.
- **491 cells are matrix depth on states that already have a reference** (the 22 referenced states hold 37
  of their 528 cells; the four background pins, the two non-original themes and the 720p resolution are
  mostly uncaptured). Those are not missing states and no route work closes them - section 5.3 prices them.

Three further states are catalogued as not capturable at all and are outside every denominator
(`psn-signin`, `trophy-collection`, `disc-game`, scorecard section 2). They are in the do-not-chase list, not
the gap list.

## 1. The gap list, and how the three groups are decided

`--references-missing --matrix`, one row per state, in the tool's own order:

| # | State | Class | Cells missing | Group |
|---:|---|---|---:|---|
| 1 | `column-tv` | icon-heavy | 24 | **c** |
| 2 | `page-download-management` | text-heavy | 24 | **c** |
| 3 | `dialog` | composite | 24 | **a** |
| 4 | `notification` | composite | 24 | **c** |
| 5 | `volume-hud` | composite | 24 | **c** |
| 6 | `info-indicator` | composite | 24 | **a** |
| 7 | `ps-menu` | composite | 24 | **a** |
| 8 | `media-player-panel` | composite | 24 | **b** |

**Group counts: (a) 3 states / 72 cells, (b) 1 state / 24 cells, (c) 4 states / 96 cells.**

The decision rule, applied the same way to all eight, so a future pass does not have to re-argue it:

- **(a) attainable now** - a route exists that needs no new content, no network, no credential and no
  install. The console state the rig is already in produces the surface; two of the three below are not
  even new work, they are proven routes that were never filed as cells.
- **(b) attainable only with prerequisites** - the surface exists on this console but one ingredient is
  missing: a media file in the emulated VFS, an installed title, a disc, a registered service, a signed-in
  PSN account, or the network. The prerequisite is named and judged for safety against this `dev_flash`.
- **(c) not attainable on this rig** - the console does not produce the surface at all, and the reason is
  structural (no row, no producer, no input path, no backend), not a missing keypress.

Three of the eight move group between the earlier audits and this pass, all toward "attainable", and each
because an audit already in the tree contains the proof the scorecard said did not exist:

- `ps-menu` and `info-indicator` were filed as "need an input path the rig does not have" (11.6, 12.8,
  13.3, 13.6). `audit/PS-BUTTON.md` captured both from this same rig on 2026-10-26 by mapping the PS
  button to Backspace, and three of its frames are still in the tree. The blocker was never the console and
  never the mapping - RPCS3's keyboard handler binds PS to Backspace by default, so the four-line capture map
  every batch ships never had to mention it. The blocker was that no later batch put a PS press in a plan.
- `dialog` was filed as "the 2026-10-27 surface never opened one" (10.6). The options menu the rig itself
  captured on `Corrupted Data` carries a live `Delete` row (batch 2 `menus/game-menu-rest.png` region,
  day batch `stills/options-panel__original__day__1080p.png`), and `DIALOGS-FIRMWARE.md` names exactly that
  route as what a "are you sure?" capture needs (lines 544, 874, 994). One keypress from a state that is
  already a reference frame.

## 2. Group (a) - attainable now

Three states, one boot, one day pin. Everything below is grounded in a frame or a log line that already
exists; the one unproven hop in each recipe is named as such and not smoothed over.

### 2.1 The shared instrument

| Item | Value | Evidence |
|---|---|---|
| Boot | `rpcs3.exe --no-gui --config <scratch>/config/capture-config.yml <dev_flash>\vsh\module\vsh.self` | all three manifests, section 1 |
| Config | copy of the live `config.yml` with only `Resolution: 1920x1080`, `Vulkan > Exclusive Fullscreen Mode: Disable`, `Clocks scale: 100`, `Date Format: mmddyyyy`, `Time Format: clock12`, `Console time offset (s): 34362` | day manifest section 1 (the offset put the guest clock at 09:58:30 and left the host clock alone) |
| Console clock | 10:00-10:07 (the day pin is 10:00 in `work/parity-reference.json`) | day manifest section 2, `frames/s03..s11-*` |
| Formats | Date Format `MM/DD/YYYY`, Time Format `12-Hour Clock` - set through the console UI each session | day manifest section 2; the registry is restored afterwards |
| Theme | `Original` evidenced by one Theme Settings page frame per session | day manifest section 2, `stills/page-settings-theme__original__day__1080p.png` |
| Frame | `CopyFromScreen` of the 1920x1080 borderless client area, 2 s settle after the last key | 13.1, all manifests |
| Bursts | `cburst <rel> <durMs> <x,y,w,h>` - 1:1 crop at 130-235 fps | batch 2 section 1; `tools/refcap3.ps1` line 14 |
| Keys | `key <NAME> [holdMs]`; `PS` is `0x08` in the runner's own table | `refcap3.ps1` lines 10 and 76 |

**The PS key needs no map entry, and that is worth stating because two documents disagree about it.** The
capture map shipped with every batch (`config/Default.yml.capture`) overrides only four buttons: `Cross:
PgDown`, `Circle: PgUp`, `Triangle: Insert`, `Square: Delete`. The day manifest's section 1 says the map
also had `PS = Backspace`, but the file it ships has no PS line - and no plan in any batch presses PS
(`plans/*.txt` contain no PS step; the only match is a file name, `c-psn`). No PS press has happened since
2026-10-26. The route still needs nothing added: RPCS3's keyboard pad handler binds PS to Backspace as its
default (`rpcs3/rpcs3/Input/keyboard_pad_handler.cpp:47`, `cfg->ps.def = GetKeyName(Qt::Key_Backspace)`), so
the four-line override map leaves PS on Backspace, and `refcap3.ps1` already sends `0x08` for `key PS`.
**The next manifest must state the map that is actually installed, not the one that was intended.**

### 2.2 `ps-menu` - the system menu raised by a PS hold

| | |
|---|---|
| Class / cells | composite / 24 (all empty) |
| Native surface it scores | the black system menu with `Turn Off the System`, `Turn Off the Controller`, the controller/charge panel and the `X Enter` / `O Back` legend; catalogue regions `ps menu entries` (300,470,1320,240, budget 26 dB / 0.95 - the only non-class budget in the corpus) and `ps menu footer` (140,650,600,130) |
| Console route | Game column at row 0 (the canonical root position) -> hold PS for 1200 ms -> release. Plan: `key PS 1200` |
| Prerequisites | RPCS3 window foreground (vsh needs focus for the hold); nothing else. No content, no network |
| How the state settles | immediately - the frame goes completely black on the hold and the menu is at rest; grab 1.5-2.0 s after the press |
| What to capture | one still (`stills/ps-menu__original__day__1080p.png`), plus one `cburst` of the entries band (300,470,1320,240) from the press for the open transition, which no audit has measured yet |
| Evidence the route works | `audit/PS-BUTTON.md:8-17` (behaviour) and `:29-30`: `out/native-reference/ps-menu/03-hold-06.png` is the menu itself - black frame, both entries, the 1-4 controller slots with slot 1 filled red, `Controller 1`, `Charge Level -`, `X Enter` / `O Back`, clock top right |
| Unproven hop | none in the console route. What is unproven is the *hold length through the runner's background-thread key dispatch*: the 2026-10-26 capture predates the batch runners (its frames include the window chrome and are not client-area grabs), so no 1920x1080 hold has been driven by `refcap2/3`. If the frame comes up with the indicator instead of the menu - a hold read as a tap - lengthen to 1500 ms and retry once |
| Risk | **low, with one hard rule**: while the menu is open, Cross is `Turn Off the System`. The recipe must close it with Circle only, and the plan must contain no CROSS between the hold and the close. A 1.2 s hold also has to be sent on the runner's threaded path so it does not gap the capture loop (batch 2 section 1) |
| Why the existing frames cannot simply be imported | `03-hold-06.png` and friends are 917x584 **with the RPCS3 window chrome in frame**, at 20:28 local, with no manifest. `parity-import.ts` skips a frame whose size is not the cell's 1920x1080 and refuses one the manifest does not name (preflight, lines 336-367). They are route proof, not cells |
| Import | map row `{state: "ps-menu", theme: "original", background: "day", resolution: "1080p", source: "stills/ps-menu__original__day__1080p.png", manifest: "<batch>/MANIFEST.md", clock: "10/29 10:04 AM", note: "..."}` then `bun work/parity-import.ts --map work/parity-references-map.json --manifest <batch>/MANIFEST.md` (section 8). Expected: `--references-missing` 22/30 -> 23/30 states, `--matrix` 37/720 -> 38/720 |
| Caveat on the 24 cells | the frame is **black below the clock**, so the theme and background axes are unobservable on this surface - a 24-cell sweep would file the same picture 23 more times. Capture the canonical cell and record the redundancy in the sidecar note; the honest cell count for this state is 1, not 24 |

### 2.3 `info-indicator` - the controller indicator raised by a PS tap

| | |
|---|---|
| Class / cells | composite / 24 (all empty) |
| Native surface it scores | the indicator plate beside the clock (`controller indicator` region, 1200,80,340,60, required) plus the `info bar` region (1329,75,591,58) that every root/column state also uses |
| Console route | Game column at row 0 -> short PS press (< 500 ms) -> release. Plan: `key PS 60`, then grab at +400 ms, +900 ms, +1600 ms |
| Prerequisites | as `ps-menu`: foreground only |
| How the state settles | **it does not settle - it flashes.** PS-BUTTON describes a flash with no measured duration, and the 2026-10-26 frame caught it about 2 s after the tap. So the recipe is the opposite of every other state: three grabs on a decay, plus a `cburst` of the info-bar band (1180,60,760,120) from the press at >100 fps for the lifetime, which is new information |
| What to capture | the still for the cell = the grab where the plate is at full opacity; the `cburst` is the timing evidence; keep all three grabs as state checks |
| Evidence the route works | `out/native-reference/ps-menu/02-after-tap.png`: the XMB at rest with the controller plate (pad glyph, `1`, `-`) immediately left of the date plate. `audit/PS-BUTTON.md:8-9` describes exactly this |
| Unproven hop | the catalogue's crop was derived from that 917x584 chrome-bearing frame, not from a 1080p cell. Measured off it, the plate sits around x 1186-1329, y 118-156 in 1920x1080 space, so the 1200,80,340,60 crop catches most of the plate but also the left edge of the clock plate. **Re-derive the crop from the first 1080p tap frame before scoring**, and record the numbers in the catalogue's `tierANote` rather than quietly widening the region |
| Risk | low. The plate may be gone by the first grab if its lifetime is short - that is why the first grab is at +400 ms and the crop burst runs from the press |
| Tier A scope | unchanged and honest: no sidecar field reports the indicator (1.1, catalogue `tierANote`), so the assertions prove the root underneath and the indicator is a pixel claim. A+B+C cannot be claimed for this state until the app emits one field (`controllerIndicatorVisible`) |
| Import | as 2.2, state `info-indicator`. Expected 23/30 -> 24/30 states, 38/720 -> 39/720 |

### 2.4 `dialog` - a message box with a confirm/cancel pair

| | |
|---|---|
| Class / cells | composite / 24 (all empty) |
| Native surface it scores | the message box plate with its two-button row (`dialog` region 240,190,1440,700). The native side is `Remove from Continue Watching` (DIALOGS-FIRMWARE lines 695, 721); the console side is any D1 box, which is the same surface with a different body |
| Console route | Game column -> `Corrupted Data` focused -> `key TRIANGLE 60` (options menu) -> `key DOWN 60` (Start -> Delete) -> `key CROSS 60` -> the delete confirmation box. Grab at +800 ms and +1600 ms; `cburst` the box region from the press for the open fade |
| Prerequisites | none beyond the rig's existing state. `Corrupted Data` is the Game column's last item (`dev_hdd0/game/TEST12345`, no `PARAM.SFO`) and it is in every Game-column frame in the corpus |
| Making it safe | **before the session, copy `dev_hdd0/game/TEST12345` to the batch's `backup/`** (the same practice as the `xRegistry.sys` backup every batch already does) and after the grab cancel with Circle, then take a state frame proving `Corrupted Data` is still in the column. If the box does not appear and the item is gone, restore the folder from the backup and say so in the manifest |
| Evidence the route is one keypress away | the console's own options menu on that item lists `Delete`: batch 2 `menus/game-menu-rest.png` region and day `stills/options-panel__original__day__1080p.png` (rows `Start`, `Delete`, `Information`, band left edge about x 1330). `DIALOGS-FIRMWARE.md:544`, `:874` and `:994` each ask for exactly this: "one console/RPCS3 capture of any 'Are you sure?' box", and name "delete a copied file on a scratch device" as the example. The RCO evidence for the box is `expage_fdelete_dialog` (D6) with `filedel_dialog_msgdialog` attached, and the D1 plate textures are measured (DIALOGS 68-104) |
| Unproven hop | **whether Delete on a corrupt title opens the box, or is a no-op like Start, or deletes without asking.** The batch proved `Start` is a no-op on this item; `Delete` was never pressed. The no-op case costs 30 seconds. The "deletes without asking" case is why the backup exists |
| Risk | medium, contained by the backup and the Circle-only exit. **Do not press Cross a second time** on any confirm |
| Fallback if the box does not appear | (i) the same box from a media item - put one JPEG in `dev_usb000/PHOTO` or `dev_hdd0/photo`, Photo column -> the item -> Triangle -> Delete (this is the `media-player-panel` boot's content, so it costs nothing extra); (ii) System Settings row 19 `Format Utility` -> Cross, which raises a hard confirm box. (ii) is the last resort: a mis-key there destroys the emulated HDD. Capture and Circle out; never confirm |
| Import | as 2.2, state `dialog`. Expected 24/30 -> 25/30 states, 39/720 -> 40/720. The sidecar note must say the console body is the delete confirmation and the native body is the Continue Watching empty state, i.e. same surface, different content - the precedent set for `osk` and `options-panel` in 13.2 |
| What it pays beyond the cell | DIALOGS' two-button confirm box is the corpus's longest-standing "spec-only: RCO + TRACED, no frame" row (544, 874, 994). This is its first frame, and the first measurement of the open fade (`DIALOGS` records the *sound* as unproven and has no motion number at all) |

### 2.5 The group (a) cost

One boot. The day batch did 22 cells in 9.5 minutes (00:25:52 -> 00:35:25); three states plus three bursts and two state checks is the same order of magnitude. **3 cells and 3 states per boot, about 12 minutes.**

## 3. Group (b) - attainable only with prerequisites

### 3.1 `media-player-panel`

| | |
|---|---|
| Class / cells | composite / 24 (all empty) |
| Native surface it scores | the player panel strip (`player panel` 200,860,1520,160) and the subtitle band (210,650,1500,200) |
| Prerequisite, exactly | **one playable video file in the emulated VFS.** The console's Video column has no playable item: batch 1 section 4.4 and section 7 item 3 record that `dev_hdd0/{savedata,music,video,photo,theme}` and `dev_usb000` hold no content files, that the Video column is empty, and that "no player can start". Day manifest section 4 repeats it: "the VFS holds no video" |
| What would have to change | one H.264/AAC MP4 copied into `dev_hdd0/video/` or a FAT32 `dev_usb000/VIDEO/` from the host, then Video column -> the file -> Cross -> play -> press any button (or pause) to raise the panel. Console route is three keys from the column |
| Is that acceptable on this dev_flash | **yes, with one condition.** It writes the emulated HDD/USB tree (`dev_hdd0`, `dev_usb000`), never `dev_flash`. It is reversible by deleting the file. The condition: it changes the *content* of the three media-column states that already have references - the day manifest's own descriptions of `column-photo`, `column-music` and `column-video` say "The VFS holds no photos / music / video" - so run it **in its own boot, after every other cell**, then remove the file and take one frame proving the Video column is back to BD Data Utility / Search for Media Servers / Video Editor & Uploader |
| How the state settles | the panel is transient - it appears on input and hides after a few seconds. Grab at +900 ms after the pause, and `cburst` the panel strip from the press for the hide timer |
| Content difference to record, not hide | the native fixture plays with a **sidecar `.srt`** (`subtitle: sidecar`, `subtitleCue: 1`); a stock console plays in-container subtitles and will not read a sidecar file. The subtitle band region is therefore mismatch-by-construction for this cell, and the sidecar note must say so. The panel strip is the comparable half |
| Risk | low. The only failure is a codec the firmware will not open (then the column shows the item and refuses to play); pick H.264 baseline/main + AAC in MP4, which the 4.93 player accepts |
| Import | as 2.2, state `media-player-panel`. Expected 25/30 -> 26/30 states, 40/720 -> 41/720 |

Nothing else in the debt is group (b). `column-tv` and `page-download-management` would each need a
prerequisite that this rig is not allowed to create (a registered service, a network download), so they are
group (c) with their prerequisites named there - the difference is not pedantry: (b) is "one file copy
away", (c) is "a PSN account and a service registration away".

## 4. Group (c) - not attainable on this rig

### 4.1 `column-tv` - the console does not draw a TV/Video Services column

The console's column map is `Users`, `Settings`, `Photo`, `Music`, `Video`, `Game`, `Network`,
`PlayStation Network`, `Friends`, and Friends is the right-hand clamp (batch 2 section 4.7, checked on the
frames; day manifest section 4 says "this rig has no TV/Video Services column"). Two independent sessions
agree, and the day `column-network` frame shows five category icons with no TV between Video and Game.

Why it is structural, not a routing mistake: `category_tv.xml` exists in the console's own dev_flash and its
category row is a **service list** - `xil://localhost/list?t=tv` (lines 23-27) - so the column is populated
by registered TV services, and with none registered vsh omits the category instead of drawing it empty
(unlike Friends, which is drawn rowless). The prerequisite is a registered video service, i.e. PSN sign-in
plus a service registration, which changes this `dev_flash` beyond the fence. **Not acceptable here.**

What the gap means for the native side, which no capture can fix: the app draws a TV/Video Services column
between Video and Game (13.5), so the `category icon row` region compares a 10-category row against a
9-category row on every root and column cell. That is a real firmware-vs-build column-set difference. It is
not a reference gap and no rig run closes it.

### 4.2 `page-download-management` - the row is not on the console, so the page cannot be opened

This one is worth being precise about, because two earlier notes give the wrong reason.

The scorecard says "needs a DOWN route the build cancels" (13.3) - that was the **native** build's
2026-10-27 live-pad defect, fixed in `07884d7`, and section 15 re-captured all six of those routes to Tier A
PASS. It is not the console's problem. Section 11.5's refusal is the accurate one: batch 2's `depth2/`
frames are Settings > Network Settings, "not Network > Download Management", so they cannot stand in for
this state.

The console-side fact: the Network column draws **four** rows - `Online Instruction Manuals`, `Remote Play`,
`Internet Browser`, `Internet Search` - in both the night frame (22:45) and the day frame (10:00), with no
Download Management row. The row is in the document - `category_network.xml:32-36`, `key="seg_dlctrl"`,
`src="ese://localhost/query?type=download_control"` - but the category is built from query results, and an
empty download query yields no row to draw. Its title, icon and action come from the RCO
(`tex_dl_manage`, `audit/STEAM-DOWNLOADS.md:306, 313`), which is why the native build can list Steam's queue
where the console lists nothing.

So the page needs at least one background-download item to exist, which needs the network. **Not attainable
offline, and not a missing keypress.** The go-around (Settings > Network Settings) is a different page and
the corpus correctly refuses it.

### 4.3 `notification` - every producer of a notification needs something this rig has not got

The firmware's notification composite has one-line and three-line variants and fourteen glyphs, one per
event class (NOTIFICATIONS 2.1-2.3): message, friend, trophy x4, headset, keypad, media server, music,
settings, info, caution. The rig has no producer for any of them:

- message / friend / online count -> PSN sign-in and the network (batch 1 section 7 item 1: "the emulated
  console is offline ... no PSN/RPCN account on this rig. Signing in is a network + credential operation and
  cannot be faked");
- trophy -> a running title, i.e. an installed game (batch 2 section 5 item 10 keeps this unchanged);
- download -> the network, and the row it would notify about is 4.2;
- headset / keypad -> Bluetooth or USB audio hardware the emulator does not present;
- media server -> DLNA on the network;
- music / settings / info -> the audits record no offline XMB action that raises a pop-up; the two native
  producers are this build's own events, and NOTIFICATIONS 5 says plainly that the launch-return pop-up has
  **no firmware counterpart**.

One untested candidate exists and should be listed as an experiment, not a route: the console's own
"background work finished" class fired by a **file copy** (Photo -> item -> Triangle -> Copy -> USB). This
needs the media file from 3.1 and it is unproven that a copy completion raises the pop-up rather than a
progress dialog. If the media boot happens (5.2), spend one keypress on it; if it produces nothing, this
state stays (c) permanently.

Also recorded rather than chased: the console lets the user suppress whole notification classes
(`msg_notification_message`, `msg_trophy_notification`, NOTIFICATIONS 425), so even a captured pop-up would
have to state which class it is.

### 4.4 `volume-hud` - no input path, and no player to host the page

The firmware's volume display is a page in the **media player plugins** - `videoplayer_volume_page` /
`videoplayer_volume_level_meter`, `audioplayer_*`, and `explore_plugin_full.rco`'s `volume_page`
(HTPC-IDEAS 557-561) - and it is raised by the BD remote's volume keys or hardware IR. Batch 1 section 7
item 3 measured the negative: "The volume OSD is driven by the BD remote's volume keys / hardware IR; there
is no keyboard-pad path to it and no such control in the XMB menus." RPCS3 v0.0.43 emulates no BD remote,
and the keyboard handler's bindings are pad buttons only (Cross/Circle/Triangle/Square/L1-R3/Start/Select/
PS), so there is no button to press.

The second half is worse: even with a key, the page belongs to a player that is running, and the VFS holds
no playable media (3.1 above). So this needs **both** a media file **and** BD-remote emulation. **Not
attainable.** The native HUD is a home-theatre feature built on the Windows media keys (HTPC-IDEAS 527-561);
its console counterpart has no cell on this rig and that should be recorded as a permanent gap, not a debt.

## 5. The recommended run order

Two boots. Everything else is optional and priced at the end.

### 5.1 Batch A - one offline boot, the day pin, three states (3 cells)

Order inside the boot, chosen so the destructive-adjacent step is last and the state checks come free:

| Step | Action | Plan keys (sketch) | Cell / evidence |
|---:|---|---|---|
| 0 | Backup `dev_flash2/etc/xRegistry.sys` and `dev_hdd0/game/TEST12345`; install the four-line capture map (PS is Backspace by default, so nothing extra) | - | `backup/`, `logs/preflight.txt` |
| 1 | Boot with the day-pin scratch config (offset 34,362 s), wait for the settled XMB | - | - |
| 2 | Commit Date Format `MM/DD/YYYY`, Time Format `12-Hour Clock`; open Theme Settings | Settings row 8, lists, then row 7 | theme evidence frame |
| 3 | Game column at row 0 | `key LEFT`.. | control frame (and the state the two PS surfaces sit over) |
| 4 | **PS tap** + three grabs + info-bar `cburst` | `key PS 60`, grab x3, `cburst 1180,60,760,120` | **`info-indicator`** |
| 5 | **PS hold** 1200 ms, settle, grab + entries `cburst`, then Circle | `key PS 1200`, grab, `cburst 300,470,1320,240`, `key CIRCLE 60` | **`ps-menu`** |
| 6 | **Delete confirm** on `Corrupted Data`: Triangle, Down, Cross, grab x2 + box `cburst`, Circle, state frame | `key TRIANGLE 60`, `key DOWN 60`, `key CROSS 60`, grabs, `key CIRCLE 60` | **`dialog`** |
| 7 | Stop; restore registry and the input map byte-for-byte, re-hash | - | `logs/restore-audit.txt` |

Expected manifest/import result: 3 cells imported, `--references-missing` **22/30 -> 25/30** states and
**37/720 -> 40/720** matrix cells.

Then, in the same write pass, the measurement that has never happened:
`bun work/parity.ts --capture --score --state ps-menu,info-indicator,dialog` - 3 states with a reference,
3 Tier B measurements, and the first numbers this corpus has ever produced for those three surfaces.

### 5.2 Batch B - the media boot, last, one state (1 cell)

| Step | Action | Cell |
|---:|---|---|
| 1 | Boot with the same scratch config (day pin) | - |
| 2 | Host copy one H.264/AAC MP4 into `dev_hdd0/video/` (or `dev_usb000/VIDEO/`); note the file's hash and the fact that the VFS changed | - |
| 3 | Video column -> the file -> Cross -> play -> pause, grab + panel `cburst` | **`media-player-panel`** |
| 4 | One keypress experiment: Photo column -> the copy of a JPEG -> Triangle -> Copy, watch 6 s for a pop-up | the `notification` experiment (4.3), evidence only |
| 5 | Remove the media file from the host, leave and re-enter the Video column, grab a frame proving the column is empty again | content-restored evidence |
| 6 | Stop; restore registry | - |

Expected: 1 cell imported -> **26/30** states, **41/720** cells.

### 5.3 Why this order and not the other one

The 8 gaps are the visible debt but they are not the best cells-per-minute. If the metric is *cells*, the
491 depth cells on states that already have a reference dominate: one boot at a fixed background pin can
walk ~22 states in about 10 minutes (the day batch measured this rate), so a four-boot background sweep
(day, night, yoake, higure) at 1080p buys ~66 more matrix cells for ~40 minutes.

But cells are not what the corpus lacks. Section 15's Tier B is 0/35 pass with 22 states measured and the
remaining failures concentrated in row/list geometry - the depth cells would multiply the same failing
measurement 20 times. **The eight states here are the only places where a whole native surface has never
been compared against firmware at all**, so they come first even though they are the worst cells-per-boot in
the corpus (0.25 cells/minute against 2.2). After batch A and B, the depth sweep is the right way to spend
an afternoon - and at that point 26 of 30 states will already be measured, so a 720p or `higure` failure
means something.

### 5.4 What the run order deliberately does not do

- No PSN sign-in, no network change, no PKG install, no disc: batch 1's "not attempted deliberately" list
  (section 7, last paragraph) is still the right fence, and it is what makes `column-tv`, 4.2 and 4.3
  unattainable rather than postponed.
- No theme commits: every batch so far has kept `Original` and cancelled every list. The `classic` and
  `p3t-01` axes stay at 0 of 200 cells each (11.6) until someone decides a theme-commit sweep is worth the
  registry churn; nothing in the 8 gaps needs it.
- No 720p console pass: the native app's 720p is a windowed 1280x720 framebuffer and the console's 720p
  projection is P10's work (scorecard section 3). A console 720p frame would not pair with anything.

## 6. What becomes measurable for the first time

Corpus effect, in the tool's own denominators:

| Measure | Today | after batch A | after batch B |
|---|---:|---:|---:|
| firmware-referenced states with a reference anywhere | 22 / 30 | **25 / 30** | **26 / 30** |
| canonical (`original/day/1080p`) cells with a reference | 22 / 30 | **25 / 30** | **26 / 30** |
| matrix cells with a reference | 37 / 720 | **40 / 720** | **41 / 720** |
| states the harness reports "without a reference" in a 35-state run | 13 | 10 | 9 (5 native-only + 4 unattainable) |
| composite-class states ever measured on Tier B | 6 of 12 | **9 of 12** | **10 of 12** |

Tier B surfaces that have **never** had a number and get one:

| State | Region measured | Budget | What the number would finally say |
|---|---|---|---|
| `ps-menu` | `ps menu entries` 300,470,1320,240 | 26 dB / 0.95 (the corpus's one non-class budget) | whether the black frame's entry block, its two-row pitch and its centred position match the console; also the first `ps menu footer` number for the controller/charge panel |
| `ps-menu` | `frame` 1920x1080 | composite 18 / 0.88 | whether the clock plate survives on the black frame - the only structural element both sides draw |
| `info-indicator` | `controller indicator` 1200,80,340,60 | composite 18 / 0.88 | the first measurement of the PS-tap plate at all; today the audit has a description and one 917x584 chrome frame |
| `info-indicator` | `info bar` 1329,75,591,58 | composite 18 / 0.88 | the plate every root/column cell also uses, now with a reference taken in the same session as those cells |
| `dialog` | `dialog` 240,190,1440,700 | composite 18 / 0.88 | the first frame and the first number for a two-button message box; DIALOGS' `spec-only` row stops being spec-only |
| `media-player-panel` | `player panel` 200,860,1520,160 | composite 18 / 0.88 | the first player-panel comparison; MEDIA-PANELS' "no player can start" note becomes a measurement |
| `media-player-panel` | `subtitle band` 210,650,1500,200 | composite 18 / 0.88 | a labelled mismatch: sidecar cues vs in-container subtitles |

Two of these are worth more than their cells. `ps-menu` is the only surface in the corpus with a
hand-set region budget, and it has never been measured against it. `dialog` closes the audit's oldest
open request in three separate rows (DIALOGS 544, 874, 994).

Per-state, the four states also change what can be *claimed*: `ps-menu` and `dialog` can reach A+B+C (their
native traces exist and both routes are scriptable), while `info-indicator` stays A+B only until the app
emits `controllerIndicatorVisible` (1.1) and `media-player-panel` reaches A+C+B only on a build whose
subtitle sidecar path runs.

## 7. Do not chase

Each line is a reason to stop re-deriving the same conclusion, with the one piece of evidence that settles
it. None of these is "not tried yet"; all were tried or measured.

| # | Surface | One-line evidence |
|---:|---|---|
| 1 | A numeric `800xxxxx` error dialog / error-code page (D2) | batch 2 section 5 item 1: the only error surface this rig produces is the string-only message page (`An Ethernet cable is not connected.`); the corrupt title's Start is a no-op, the update path kills vsh, the Store path shows a network message with no code |
| 2 | Any game-launch error page | batch 2 section 5 item 2: `dev_hdd0/game/TEST12345` has no `PARAM.SFO`, so the item is `Corrupted Data` and Start is a no-op - there is no failure to report (batch 1 section 4.1a: 9 s + 13 s + three 20 s grabs, nothing) |
| 3 | System Update failure page | batch 2 section 5 item 3: `sys_fs_write` -> `Access is denied (0x5)`, the console freezes, and the visible frame is the RPCS3 host overlay, which is not firmware |
| 4 | Disc / BD rows and `disc-game` | catalogue: no BD drive is exposed to the PC; the disc states are catalogued as not capturable with that exact reason (scorecard section 2) |
| 5 | `psn-signin`, `trophy-collection` | catalogued not capturable: no PSN backend may be fabricated, and the app answers with the firmware's own unavailable message (scorecard section 2) |
| 6 | Signed-in info-bar indicators (message/chat/online count/avatar) | batch 1 section 7 item 1: offline console, `PSN status: Disconnected`, no account; signing in is a network + credential operation that cannot be faked |
| 7 | Copy / busy / progress dialogs | batch 1 section 7 item 2: a copy needs a copyable object, and `dev_hdd0/{savedata,music,video,photo,theme}` and `dev_usb000` hold none |
| 8 | Masked / password OSK | batch 1 section 7 item 4: WPA needs a scanned SSID (no network device) and the BD-remote passkey prompt waits for hardware (`osk/bd-remote-passkey-02.png` shows no field) |
| 9 | `column-tv` | 4.1: nine-category console map in two sessions; `category_tv.xml` populates from `xil://localhost/list?t=tv`, so the column needs a registered service (PSN), not a keypress |
| 10 | `page-download-management` | 4.2: the Network column draws 4 rows in both sessions and the document's `seg_dlctrl` query returns nothing offline; "the DOWN route the build cancels" is a fixed *native* defect, not the console's reason |
| 11 | `notification` | 4.3: every glyph class needs PSN, network, a running title or Bluetooth/USB hardware; the one untested candidate (file-copy completion) is an experiment in batch B, not a route |
| 12 | `volume-hud` | 4.4: BD-remote volume keys / IR only, RPCS3 emulates no BD remote, and the volume page belongs to a media player that cannot start with an empty VFS |
| 13 | Media-server pages (Photo/Music/Video "Search for Media Servers") | batch 1 section 4.4 / day manifest section 4: the rows exist and are captures of their own states; the server list needs DLNA on a network that is off |
| 14 | 720p cells (0 of 300) | scorecard section 3 and 11.7 item 8: the native 720p cell is a windowed 1280x720 framebuffer, so a console 720p frame would pair against a window and not a console mode - that is P10's work, not the rig's |
| 15 | The five native-only pages | 12.4: `noFirmwareReference: true`; a rig frame for them must not exist, and asking for one was the bug 10.6 named |

## 8. The import step, precisely

The importer is the only writer; this document does not run it. For each batch:

```powershell
# 1. dry run first: validates every entry, writes nothing
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-gap-2026-10-29/MANIFEST.md --dry-run

# 2. import
bun work/parity-import.ts --map work/parity-references-map.json `
  --manifest C:/dev/out/rpcs3-gap-2026-10-29/MANIFEST.md

# 3. confirm the debt moved by exactly the mapped cells
bun work/parity.ts --references-missing
bun work/parity.ts --references-missing --matrix

# 4. the numbers that never existed
bun work/parity.ts --capture --score --state ps-menu,info-indicator,dialog
```

A map row (the shape `work/parity-references-map.json` already uses for 37 entries - the importer fills
`applied` and `skipped` itself):

```json
{
  "state": "ps-menu",
  "theme": "original",
  "background": "day",
  "resolution": "1080p",
  "source": "stills/ps-menu__original__day__1080p.png",
  "manifest": "C:/dev/out/rpcs3-gap-2026-10-29/MANIFEST.md",
  "clock": "10/29 10:04 AM",
  "note": "Manifest section 3 row N: the system menu raised by a 1200 ms PS hold at the Game column root, day pin, plate 10:04 AM; black frame with the two entries centred and the controller/charge panel bottom left. Console route is a pad hold; the state's own route is the app's pshold token. The frame is background-independent below the clock, so this cell is the only informative one of the 24."
}
```

What the importer enforces, so a batch is not refused after the fact (`work/parity-import.ts` preflight):

| Requirement | Detail |
|---|---|
| Manifest carries a capture date | a `- Session:` line matching `YYYY-MM-DD HH:MM` (batch 2 lacked it and needed the disclosed import copy, 11.1) |
| Manifest carries `\| RPCS3 build \|` | the build string |
| Manifest carries the firmware | `\| Firmware \| 4.93 \|` or a "PS3 4.93 XMB" mention |
| Manifest carries `\| Video \|` with a `WxH` | and/or a sentence "every image is 1920x1080" - the frame size is checked against the frame too |
| The frame is exactly the cell's size | 1920x1080 for a 1080p cell; never rescaled; a mismatch is skipped, a manifest/frame disagreement is a refusal |
| The manifest names the frame | the file name (`exact`) or its directory as `dir/` (`group`) |
| The state is in the catalogue, the axes are in the matrix | a state id the catalogue does not define is refused (the 11.5 lesson) |
| One file is one cell | a duplicate cell in one map is refused |
| An occupied cell needs `--replace` | and a `registered` cell cannot be imported at all (root's history, 10.7 / section 14) |

The batch's own manifest should also carry, because the last three batches had to note it by hand: what the
installed input map actually was (2.1), which frames are route-equivalent-but-not-route-identical
(`dialog`, `media-player-panel`), and the per-frame clock string, including the frames whose plate is
covered (`ps-menu`'s black frame keeps the clock, the band and modal frames do not).

## 9. What this plan does not claim

- No frame in this plan exists yet. The three group (a) routes are proven by a 2026-10-26 capture of two of
  them and by a one-keypress-from-a-captured-state argument for the third; none of the three has a
  1920x1080 cell.
- The Tier B numbers section 6 promises are measurements, not passes. Every one of them is expected to
  score below its budget on the current build, which cannot yet draw text: the console frames carry row
  labels, the native ones do not. What the numbers buy is the first honest distance, not a win.
- Group (c) is a statement about **this** rig and **this** fence, not about the firmware. `column-tv` and
  `page-download-management` exist on a signed-in console; `notification` and `volume-hud` exist on a
  console with a PSN account, a disc and a BD remote. If the fence ever widens, 4.1-4.4 are the four
  recipes to revisit first, in that order.
- Nothing here re-opens a budget. If a batch A cell lands below 18 dB / 0.88, the correct response is the
  one sections 6 and 15 already take: report the number, name the region, and go after the row/list
  geometry - not the threshold.
## CORRECTION 2026-10-28: media-player-panel is group (c), not group (b)

The prerequisite in 3.1 was wrong. A rig attempt placed a real 3.46 MB H.264/AAC MP4 (`avc1`+`mp4a`, 20 s)
in `dev_hdd0/video` before boot: the Video column still drew only BD Data Utility / Search for Media
Servers / Video Editor & Uploader. The same file in `dev_usb000/VIDEO` plus a column re-entry showed
nothing, and a JPEG in `dev_hdd0/photo` left the Photo column empty.

Cause, from the 47,749-line RPCS3 log: vsh opens `/dev_hdd0/mms/db/metadata_db_hdd` and its log/index
files, and **`/dev_hdd0/video` appears zero times**; `/dev_usb000` appears only in the VFS mount dump.
The media columns are drawn from the **MMS metadata database**, not a directory scan, so a host-side copy
is invisible until the console's own importer writes a row - and its only sources are devices RPCS3 does
not present (no USB enumeration/hotplug). Media-player-panel therefore joins the do-not-chase list for
this rig. The content was removed and both columns re-grabbed empty; the rig was restored (xRegistry,
input config, live config, dev_hdd0/game/TEST12345 byte-for-byte).
