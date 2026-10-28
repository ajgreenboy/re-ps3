# Firmware capture batch 2, 2026-10-28: session record, coverage and the error-page investigation

This is the second targeted capture batch against the real PS3 4.93 XMB in the local RPCS3 rig. Batch 1
(`CAPTURE-BATCH-2026-10-28.md`, root `C:\dev\out\rpcs3-ref-2026-10-28\`) proved that the rig could drive the
console and grab settled frames. This batch was aimed at the things a settled frame cannot show: the
depth-2 page, the menu and choice-band transitions at a frame rate high enough to resolve them, the BG1
plate at rest, the option-list contents, and - the priority - a firmware *error* surface.

It closed most of that and it did not close the error code. The batch manifest is
`C:\dev\out\rpcs3-ref2-2026-10-28\MANIFEST.md` (418 lines); this document is the session record, the
coverage ledger and the honest limit, and it is the evidence behind the dated correction sections added to
[DIALOGS](DIALOGS-FIRMWARE.md) (section 9), [SETTINGS](SETTINGS-FIRMWARE.md) (section 12),
[ITEM-MENU/INFO](ITEM-MENU-INFO-FIRMWARE.md) (section G), [GEOMETRY-ROOT](GEOMETRY-ROOT.md) (section 10) and
[PAGE-MOVE](PAGE-MOVE.md) (dated note 2).

No source, threshold, reference or harness file was touched. The session wrote nothing under `C:\dev\xmb`
and ran no git command. Every frame stays under the git-ignored capture root.

## 1. Session facts

| Item | Value |
|---|---|
| Capture root | `C:\dev\out\rpcs3-ref2-2026-10-28\` (5.92 GB) |
| Session shape | **one session, two RPCS3 boots of `vsh.self`** - boot 1 22:39:07 to 22:47:5x (ended by an emulator fault on the System Update path), boot 2 22:50:31 to 23:52 (stopped by the capturing agent). `Get-Process rpcs3` empty afterwards |
| RPCS3 build | `RPCS3 v0.0.43-0b0fa082 Alpha \| master \| local_build` (git `0b0fa082`), the same build as batch 1 |
| Runtime title | `FPS: 60.03 \| Vulkan \| 0.0.43 \| vsh.self []` |
| Host | Windows 10.0.26200, Ryzen 5 5600X, 47.93 GiB, NVIDIA RTX 3070 (driver 610.62.0.0) |
| Boot command | `rpcs3.exe --no-gui --config <root>\config\capture-config.yml C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self` |
| Scratch config | `config/capture-config.yml`, a copy of the live `config.yml` with only `Resolution: 1920x1080` and `Vulkan > Exclusive Fullscreen Mode: Disable` changed |
| Video | Vulkan, 1920x1080, 16:9, VSync Disabled, Exclusive Fullscreen Disable, borderless client area exactly 1920x1080 |
| **Clock scale** | **100 %** for the whole session (`Clocks scale: 100` in the scratch config) - no clock scaling, so no timing rescale is applied to any number in this batch |
| Resolutions | every still is 1920x1080; every burst frame is either 1920x1080 or a documented 1:1 crop of it. Nothing anywhere was rescaled |
| Volume | **22,400 PNGs**: 251 stills + 22,149 burst frames in **19 bursts** |
| Burst rates | 42.0-63.7 fps full frame; **130.2-234.9 fps** on 1:1 crops |
| Stills | `CopyFromScreen` of the client area (`tools/refcap2.ps1`, `grab`) |
| Bursts | raw BGRA frames with a per-frame millisecond timestamp (`frames.csv`), crop declared in `rect.txt` |
| Tools | `tools/refcap2.ps1` (plan runner, keys dispatched on a background thread so a key hold never gaps the capture loop), `analyze.py`, `alpha.py`, `measure.py`, `diff.py`, `sheet.py`, `pick.py` |
| Plans | `plans/*.txt`, every key sequence replayable |

Key mapping (the global input config was replaced for the session and restored afterwards, section 2):
Cross/Enter `PgDown` (VK 0x22), Circle/Back `PgUp` (0x21), Triangle/Options `Insert` (0x2D), Square
`Delete` (0x2E), directions arrows (0x25-0x28), Start/Select `Return`/`Space`.

**Why crop bursts.** A full-frame `BitBlt` costs 13-18 ms (53.9 fps measured), so a full-frame burst cannot
exceed about 54-64 fps and misses frames of a 60 Hz animation. A 1:1 pixel crop of the animated region
reaches 130-235 fps and resolves every rendered frame. Because the crop is 1:1 and never scaled, a crop
frame maps exactly onto the full-frame stills - that is what makes the timing numbers in section 4 usable
without a rescale.

## 2. Rig restoration

| Item | State | Proof |
|---|---|---|
| Global input config | original restored, 66 B, sha256 `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` (the required target), after a temporary 139 B capture mapping `40200e9c...4835` | `logs/input-config-hash.txt`, MANIFEST section 2 |
| Live `config.yml` | **untouched**, sha256 `22d8ffe22461284c6756fc80ab0de2b7e76415981299fdcc6621a697971e4adb` at open and at close | MANIFEST section 2 |
| `dev_flash2/etc/xRegistry.sys` | **restored byte-for-byte** from the 22:36:11 pre-session backup: sha256 `4de4dcdcaecda324093a9246888cc337a5c872103b779047c2a52387fc57d80d`, **262,144 B**, mtime reset to the original `2026-10-27T21:58:42.6042257-05:00` | `logs/restore-audit.txt` |
| RPCS3 processes | only the two this agent started (13568, 32092) were stopped; `Get-Process rpcs3` empty at the end | `logs/pid.txt`, `logs/restore-audit.txt` |
| Firmware / PKG | none installed, no network change, `dev_flash` untouched | directory mtime scan |
| Console settings | **nothing committed**: every list was opened and cancelled with Circle; Theme/Color/Background/Font and Screensaver still read `Original` / `After 20 Minutes` | `lists/theme-page-after*.png`, `bands/display-after-screensaver-band.png` |
| Repo | no file written under `C:\dev\xmb`, no git command | - |

This is the improvement over batch 1, which had no pre-session registry copy and therefore could not prove
a byte-identical console state afterwards (batch 1, section 7.1). The residual is stated plainly: the
console rewrote its own registry twice (boot 1 at 22:39:31, sha `8adf21fd...`; boot 2 exit at about 23:52,
sha `25a28272...`). Those are the console's own writes through its save path, not edits by the agent, and
the file was restored afterwards to the exact pre-session bytes and mtime - the sha256 above matches the
backup taken before any console setting was touched.

Session events that shaped the batch, because they changed what could be captured:

| Time | Event |
|---|---|
| 22:36:11 | `xRegistry.sys` copied to `backup/xRegistry.sys.orig` before any setting was touched |
| 22:39:07 | boot 1 (pid 13568) |
| 22:41-22:42 | Store launch experiment: `PlayStation(R)Store` produced the `Internet Connection Settings` message page, no code |
| 22:47 | **vsh died** on `Settings > System Update > Update via Internet`: RPCS3 fatal, `ScePafJob` thread, `fs::windows_file::write ... Access is denied (0x5)` (`sys_fs_write`). Console frozen on `Checking... Please wait.`, host overlay drawn over the frame |
| 22:50:31 | boot 2 (pid 32092), same scratch config and input mapping |
| 23:52 | boot 2 stopped; registry and input config restored and re-hashed (section 2) |

## 3. Coverage

Frame paths are relative to the capture root. "CLOSED" means the question is answered with a named frame;
"PARTIAL" means part of it is answered; "OPEN" means the batch could not reach it.

### 3.1 Closed

| Item | Question it answers | Frame / burst | Where it lands |
|---|---|---|---|
| Depth-2 page geometry | title rule, row pitch, label and value columns, back hint, band state | `depth2/statuslist-rest.png` | SETTINGS 12.2, GEOMETRY-ROOT 10.2 |
| Second depth-2 page type | a depth-2 *message* page exists (title bar + rule + body + OK + Enter/Back) | `depth2/ics-page-01.png`, `-down1/-down2/-up1.png` | SETTINGS 12.2 |
| Options-menu timing | start, ramp and rest for open and close | `menus/menu-open-crop2/` (135.2 fps), `menus/menu-close-crop2/` (175.9 fps) | ITEM-MENU G1 |
| Choice-band timing | start and ramp for open and close | `bands/theme-band-open2/` (158.9 fps), `bands/theme-band-close2/` (164.7 fps), `bands/screensaver-band-close/` (151.2 fps) | SETTINGS 12.3, ITEM-MENU G1 |
| Row dims before the plate | on close the row ink drops below luma 180 within one frame, only the plate fades | `menus/menu-close-crop2/` | ITEM-MENU G3 |
| Band geometry at rest | left border, gradient peak and geometric zero, full height, item text inset | `bands/screensaver-band-rest.png`, `bands/theme-band-rest2.png` | SETTINGS 12.3, GEOMETRY-ROOT 10.3 |
| BG1 at rest on a settings page | **no filled rectangle**; brighter text plus a soft gradient confined to the text block | `bands/theme-page-ready.png`, `bands/theme-plate-rest-color.png` | SETTINGS 12.6, GEOMETRY-ROOT 10.1 |
| XMB item plate | x 405-731, y about 420-560, soft vertical gradient | `error/column-corrupted-focused.png` | GEOMETRY-ROOT 10.1 |
| Theme list | 4 items, current/focused without a check, Air Paint with an icon | `lists/theme-list-rest.png` | SETTINGS 12.4 |
| Color list | 12 chips, 40 px pitch, no text labels, label line static, live recolour | `lists/color-list-rest.png`, `-down1..14.png` | SETTINGS 12.4 |
| Background list | 3 items plus the `Brightness >` submenu row | `lists/background-list-rest.png` | SETTINGS 12.4 |
| Font list | 3 items | `lists/font-list-rest.png` | SETTINGS 12.4 |
| Video page tail | 19 rows, last `50 Hz Video Output` (Automatic) | `tails/video-page-top.png`, `video-down1..22.png` | SETTINGS 12.5 |
| System page tail | 25 rows, last `About PlayStation(R)3 (PS3(TM))` | `tails/system-page-top.png`, `system-down1..20.png` | SETTINGS 12.5 |
| The one reachable firmware error page | layout, timing, dismiss path | `error/page-at-rest.png`, `error/ictest5-fail/` | DIALOGS 9 |

### 3.2 Partial

| Item | What is answered | What is not | Frame |
|---|---|---|---|
| Menu and band timing | start offset, ramp length, rest time on both edges at 131-235 fps | the cue audio; the exact curve beyond "linear" | `menus/*-crop2/`, `bands/theme-band-*2/` |
| Error surface | one message page, fully measured, with a measured failure transition | **no numeric code**; no game-launch error page; no System Update failure page | `error/ictest5-fail/` |
| Band row origin | rows and labels measured per list | why the same word `Original` sits at ink 494 on Theme and 537 on Background (section 6.1) | `lists/theme-list-rest.png`, `lists/background-list-rest.png` |
| Color list extent | 12 chips, everything visible at rest except the last, clamps at the last | whether any of the 12 is a per-theme palette for an uninstalled theme | `lists/color-list-rest.png`, `-down14.png` |
| Two-level pages | the read-only status page and the message page | a depth-2 page **with focusable rows** (none exists on this console) | `depth2/*` |

### 3.3 Open

| Item | Why |
|---|---|
| A numeric error code (`800xxxxx`) page or dialog | not reachable on this console; section 5.6 |
| A game-launch error page | `dev_hdd0/game/TEST12345` has no `PARAM.SFO`; the item is listed as `Corrupted Data` and Start is a no-op |
| The System Update failure page | the update path kills vsh; that is an emulator fault, not firmware |
| A depth-2 page with focusable rows | the two depth-2 pages this console reaches are a read-only table and a message page |
| A two-button confirm box plate | no reachable path produces a boxed confirm (same conclusion as batch 1) |
| Cue audio for open / close / commit | `Audio: Dump to file` was false and no loopback was configured; video timing only |

### 3.4 The frame ledger, verified

The manifest's own accounting reproduces exactly against the directories: `error/` 17,146 PNGs (50 stills +
9 bursts of 17,096), `menus/` 2,022 (13 + 2,009), `bands/` 3,058 (14 + 3,044), `lists/` 47, `tails/` 49,
`depth2/` 16, `nav/` 41, `nav2/` 21 - **22,400 PNGs**, plus 163 non-PNG files (burst `frames.csv`/`rect.txt`,
logs, plans, config, backup). The per-burst frame counts in section 4 match the directory counts.

## 4. Headline numbers

Everything below is a screen pixel at 1920x1080, measured on the named frame. Clock scale was 100 %, so the
timings are the firmware's, not scaled.

### 4.1 Depth-2 page geometry

Route: Settings column, row 16 `Network Settings`, Cross, row 1 `Settings and Connection Status List`,
Cross. Frame `depth2/statuslist-rest.png`.

| Quantity | Value |
|---|---|
| Title-bar rule | **y = 199** (1 px) |
| Rows | **13**, ink tops 291 -> 771, **pitch 40 px exactly**, ink height 18 |
| Label column | **right-aligned at x = 939** (measured 938-940 across all 13 rows) |
| Value column | **left-aligned at x = 979-980** |
| Column gutter | 38-41 px |
| Lower rule | **y = 879** (1 px), the same pair as the information page |
| Back hint | `O Back`, ink x 1155-1239, y 897-916 |
| Focus | **none** - Down/Up produce no visible change (mean frame delta 0.82 / 1.31 luma, i.e. the wave only) |
| Category band | **off-frame**: no icon-column elevation (x 240-320 mean luma 11.8, against 25.8 on the depth-1 page) |
| Info bar | **absent** (luma 0 at x 1300/1340/1700/1900 on y 100) |

Rows, in order: `Internet Connection`/`Enabled`, `Connection Method`/`Wired Connection`,
`Speed and Duplex`/`Auto-Detect`, `Address Settings`/`Automatic`, `IP Address`/`Automatic`,
`Subnet Mask`/`Automatic`, `Default Router`/`Automatic`, `Primary DNS`/`Automatic`,
`Secondary DNS`/`Automatic`, `MAC Address`/`-`, `MTU`/`Automatic`, `Proxy Server`/`Do Not Use`,
`UPnP`/`Enabled`.

The **40 px pitch is a depth-2 number**. The depth-1 settings list pitch is 80 px (`SETTINGS 9.1/U1`,
`GEOMETRY-ROOT 9.3`). Two page classes, two grids - one constant cannot serve both.

The second depth-2 type is a message page (`depth2/ics-page-01.png`, `Internet Connection Settings`):
title bar, rule at y 199, a two-line body, an `OK` button and `X Enter` / `O Back` hints. Down/Down/Up move
nothing (three probe stills), so it is a message page, not a list.

### 4.2 Menu and choice-band timing

All four transitions were captured on 1:1 crops at 131-235 fps, so no rescale is involved.

| Transition | Start after key | Ramp | At rest | Frame |
|---|---|---|---|---|
| Options menu **open** | **+115...150 ms** | **245-255 ms**, linear, 0 -> 1 | +360...410 ms | `menus/menu-open-crop2/` (432 f, 135.2 fps) |
| Options menu **close** | **+9 ms** | **200 ms**, linear, 1 -> 0 | +215 ms | `menus/menu-close-crop2/` (563 f, 175.9 fps) |
| Choice band (Theme) **open** | **+62 ms** | **270 ms**, 0 -> 1 by +330 ms | +330 ms | `bands/theme-band-open2/` (539 f, 158.9 fps) |
| Choice band (Theme) **close** | **+11 ms** | **200 ms** | +215 ms | `bands/theme-band-close2/` (560 f, 164.7 fps) |

The metric is the band background's luma over its own region (options: ROI 1520,300-1840,440, 0.46 -> 15.3
open and 18.3 -> 4.2 close; Theme band 0.37 -> 19.7 open and 19.7 -> 0.36 close). A third close, the
Screensaver band at 151.2 fps (`bands/screensaver-band-close/`, 452 f), agrees with the Theme close.

**Row ink dims before the plate.** On close the row text drops below luma 180 **within one frame (<9 ms at
176 fps)** and only the plate fades over the following 200 ms. Text and plate are therefore not one alpha
channel.

### 4.3 Band geometry at rest

| Quantity | Screensaver band | Theme band |
|---|---|---|
| Frame | `bands/screensaver-band-rest.png` | `bands/theme-band-rest2.png` |
| Left border | **x = 1260-1261** (1 px step, 0 -> 31 luma at y 100) | **x = 1324-1325** (1 px step, 0 -> 31) |
| Gradient peak | 56 luma at x about 1320 | 55-56 at x about 1400 |
| Profile | 46 @1280, 34 @1500, 20 @1560, 8 @1600, 2 @1640, 1 @1680, 0 @1720 | 44 @1500, 35 @1560, 27 @1600, 15 @1640, 6 @1680, 1 @1720 |
| Geometric zero | about 1680 (last >=2 luma at 1648) | about 1744 (last >=2 luma at 1744) |
| Vertical extent | full height, y 0-1079 | full height, y 0-1079 |
| Item text left | 1276-1278 (batch 1) | 1341 (border + 17) |
| Parent page | dimmed, visible left of the border and through the gradient | same |

The band never reaches a hard right edge: it fades into the dimmed parent page. **The two left edges differ
by 64 px** (1260 vs 1324) on two pages of the same class, so there is no single band-origin constant in the
firmware; `SETTINGS 11.8`'s one `x0 = 1260` can only be right for the Display page.

### 4.4 BG1: the item plate, and the focused row at rest

| Surface | Measurement | Frame |
|---|---|---|
| XMB item plate (focused item in a column) | **x 405-731**, y about **420-560**, soft vertical gradient, peak about 126 luma at y 520 over the icon | `error/column-corrupted-focused.png` |
| Settings-page focused row at rest | **no filled rectangle.** The row is marked by brighter text plus a soft gradient confined to the text block: 10-18 luma over a 3-4 background inside y 470-500, back to 3.9 below the description by y 545-560 | `bands/theme-page-ready.png`, `bands/theme-plate-rest-color.png` |
| Plate mid-transition | the record exists (`bands/bg1-focus-move/`, 602 f, 143.5 fps, crop `450,380,1000,650`, focus moves `Theme Settings` -> `Date and Time Settings` between 901 and 972 ms) | same |

The second row is the batch's most consequential negative result: a focused settings row is **not** a plate
at rest. Whatever the transition draws, it settles to text brightness plus a text-block-confined gradient.
A view that paints a BG1 rectangle on a settings row is drawing something the console does not.

### 4.5 List contents

| List | Items | Measured detail | Frame |
|---|---|---|---|
| Theme | 4: `Install`, `Original` (current, focused), `Classic`, `Air Paint` | no check, no dot on the current item; `Air Paint` carries a small preview icon; the list cannot scroll | `lists/theme-list-rest.png` |
| Color | **12 chips**: white `Original` + 11 colours | **no text label per chip**; the label line above the column always reads the *current value* (`Original`) and does not follow focus; chips at **40 px pitch**, pitch exact over ten measured chips; 11 chips drawn at rest (first ink top 531) plus a down-chevron at 969-977, the 12th revealed when focus reaches it; the focused chip takes a 1 px outline; focus **previews live** by recolouring the whole screen | `lists/color-list-rest.png`, `-down1..14.png` |
| Background | `Original` (current, focused), `Classic`, `Wallpaper` | plus a fourth row `Brightness >` (submenu chevron) above them; clamps at `Wallpaper` | `lists/background-list-rest.png` |
| Font | 3: `Original` (current, focused), `Rounded`, `Pop` | clamps at `Pop` | `lists/font-list-rest.png` |

### 4.6 Page tails

| Page | Rows | Last row | Frame |
|---|---:|---|---|
| Video Settings | **19** | `50 Hz Video Output` (Automatic) | `tails/video-page-top.png`, `video-down1..22.png` |
| System Settings | **25** | `About PlayStation(R)3 (PS3(TM))` | `tails/system-page-top.png`, `system-down1..20.png` |

Full ordered lists are in `MANIFEST.md` section 4.6; both pages were walked to the last row, so "the tail
rows are below y 1080 in the stills" is no longer true and the two page tails are closed.

## 5. The error surface

### 5.1 What was attempted

| Route | Result | Frame |
|---|---|---|
| Game column, `Corrupted Data`, Cross (no `PARAM.SFO`) | **no reaction at all**: no dialog, no page, no code; the clock advances and the XMB stays interactive. Three attempts: 9 s burst, 13 s burst, three 20 s grabs | `error/launch-burst/` (387 f, 43.0 fps), `error/corrupt-launch-burst/` (740 f, 57.0 fps), `error/retry-t01..t06.png` |
| `PlayStation(R)Store` launch | a *network-required* message page: title `Internet Connection Settings`, body `Adjust settings for connection to the Internet.` / `If making a wired connection, you must have an Ethernet cable connected.`, `OK`, `X Enter` / `O Back`. **No code** | `error/icp-page-rest.png`, `error/retry-t01/t04/t06.png` |
| `Settings > System Update > Update via Internet` | **vsh died** (section 2). The host overlay `The PS3 application has likely crashed, you can close it.` is RPCS3 UI, not firmware | `error/update2-launch-burst/` (660 f), `error/update2-settle-01..04.png` |
| `Settings > Network Settings > Internet Connection Test` | **the one real error page**: a string-only message page after a variable timeout | `error/ictest5-fail/` (10,569 f, 234.9 fps), `error/page-at-rest.png` |

### 5.2 The one reachable error page, measured

Frame `error/page-at-rest.png` (route: Settings, row 16 `Network Settings`, Cross, row 4
`Internet Connection Test`, Cross, wait for the timeout).

| Quantity | Value |
|---|---|
| Title-bar rule | **y = 199**, 1 px |
| Title icon | globe, x about 100-140, y about 160-190 |
| Title ink | x 145-446, y 167-185 (`Internet Connection Test`, 19 px cap height) |
| Lower rule | **y = 879**, 1 px - the D4/D5 rule pair |
| Heading ghost | a dimmed repeat of the title at y about 490-523, **max luma 43 / 45** (about 25 % alpha), i.e. below readable |
| Message ink | **x 753-1163, y 530-546** - `An Ethernet cable is not connected.` |
| Back hint | `O Back`, ink x 1155-1239, y 897-916 (the same rect as the information page) |
| Info bar | present but unfilled: 1 px plate border at x 1329, clock ink x 1592-1799, y 88-119, no plate fill |
| Dim behind | none |
| Focusable controls | **none** - Down and Up change nothing (mean frame delta 0.82 / 1.31 luma) |
| Numeric code | **none**: no `800xxxxx` string, no code field, no `Enter` hint |

### 5.3 The failure moment, at 234.9 fps

Burst `error/ictest5-fail/`, key at about 800 ms, 10,569 frames, crop `620,450,740,170`.

| Phase | Time after Cross |
|---|---|
| `Please wait...` (spinner + `O Back`) | 0.8 s -> 11.9 s |
| `Please wait...` text fades out | 11.90 -> 12.05 s (about 150 ms) |
| Message and heading fade in | 12.05 -> 12.80 s; white text full at **12.283 s**; plate at rest by 12.8 s |
| Whole transition | about **750-900 ms**, cross-fade style |

This is the highest frame rate in either batch and the only timestamped record of a firmware *error*
appearing.

### 5.4 The timeout is not deterministic

Three runs of the same test failed at about **12 s, 15 s and 26 s** after the Cross
(`error/ictest5-fail/`, `error/ictest2-burst/`, `error/ictest3-burst/`). The "moment of failure" therefore
has no fixed offset; only its shape is stable (150 ms text fade-out, about 250 ms message fade-in). Any
native implementation that models a fixed error delay is modelling the rig, not the firmware.

### 5.5 Dismiss

Circle returns to the `Network Settings` page with the focus on the `Internet Connection Test` row
(`error/ictest2-dismiss-01..03.png`, `error/page-dismissed.png`, `error/ictest5-row.png`). The error page has
no button of its own to press: the only verb is Back.

### 5.6 Why this is partial - stated as the limit, not as a footnote

**This console cannot produce a numeric error code page.** The batch made four independent attempts
(5.1) and the only error surface it reached is a string-only message page. The reasons are specific and
each is evidenced:

- the corrupted title's `Start` is a **no-op** in this build (`error/corrupt-launch-burst/` plus three
  20 s grabs: nothing happens), so there is no launch failure to report;
- the System Update path **kills vsh** on an emulator fault (`sys_fs_write` -> `Access is denied (0x5)`),
  which is RPCS3's failure, not the firmware's - a firmware page cannot be photographed from a dead
  console;
- the Store path produces the *network-required* page, which is configuration advice, not an error code;
- a genuine `800xxxxx` page needs a failure the firmware itself classifies - a failing PSN sign-in, a
  rejected PKG, a disc read error. This rig has no PSN account, no PKG install and no disc.

Consequence for the audits: `DIALOGS-FIRMWARE.md` section 2.2 (D2, the error-code page) stays **spec-only**
- traced from `msgdialog_plugin` and the `vbox`/`mbox` code, never photographed. What this batch adds is a
measured *sibling*: the string-only message page, which shares the D4 rule pair (199 / 879) and the
`O Back` hint but has no code field, no `Enter` hint and no suggested-actions body. A reimplementation that
draws a code page for a cable-unplugged condition is drawing D2 where the firmware draws D4.

## 6. Re-measurement against the frames

The manifest's numbers were checked against the frames themselves with a luma/ink scan (row bands at a
threshold, column profiles, chroma for the color chips). Most agree exactly. The differences, and the two
frame labels that mislead, are recorded here so the audits can cite the frame value rather than the
summary.

### 6.1 Differences found

| Claim | Manifest / earlier doc | Frame value | Note |
|---|---|---|---|
| Depth-2 lower rule | not given (only 199) | **y = 879** on `depth2/statuslist-rest.png` and on `error/page-at-rest.png` | the D4/D5 rule pair applies to both; new number |
| Error-page info bar | "the info bar is replaced by the rule" | present but **unfilled**: plate border at x 1329, clock ink 1592-1799 at y 88-119 | the frame shows the bar; what is missing is the fill |
| Status-list label/value edges | 939 / 979-980 | 938-940 / 978-980 | agrees |
| Color chips, first chip | "first chip ink y about 491" | **531**; 491 is the label line (`Original`) above the column | the label line is not a chip |
| Color chips visible at rest | 12 | **11** drawn + a down-chevron at 969-977; the 12th appears when focus reaches it | the list has 12 items either way |
| Theme band border | 1325 | 1324-1325 (step at 1324) | agrees within 1 px |
| Theme band zero | 1720-1730 | last >=2 luma at **1744**, 1 luma at 1720 | "visible end" vs "geometric zero" - state both |
| Screensaver band zero | 1680 | last >=2 luma at **1648**, 1 luma at 1680 | same distinction |
| `menus/game-menu-rest.png` | "menu at rest" | scans as the **closed** state (band area luma <=1, identical profile to `game-menu-closed-check.png`); the settled menu is **`menus/game-menu-open-check.png`** (55 luma at x 1400 on y 100) | use the open-check frame for the band |
| Options-menu rows | batch 1 `E2`: only the mid-fade frame shows `Sort By` / `Group Content` | both the settled batch-2 frame **and** batch 1's `dialogs/options-open-01.png` carry five rows: ink 301-323, 341-362, then 539-560, 581-598, 621-638 | the two menu rows are part of the settled menu |
| Band row origin | `first = 505-(n-1)*20` (`SETTINGS 11.8`) | the **same word `Original`** sits at ink top about **494** on `lists/theme-list-rest.png` and about **537** on `lists/background-list-rest.png` | the band's row origin is per-list, not a constant |
| XMB item plate | - | at y 520 the plate reads 136 luma at x 410 and drops to 80 at x 730-750 | agrees with the 405-731 rect |

### 6.2 What was not re-measured

- The four transition numbers in 4.2 are the manifest's, derived from its per-frame metric CSVs
  (`logs/menu-open-band.csv`, `menu-close-band.csv`, `theme-band-*.csv`, `ictest5-text.csv`,
  `ictest5-msg.csv`). I did not re-derive the curves from the raw frames; the frame counts and rates were
  verified against the directories, and the endpoints of the band profile were verified on the stills.
- The 234.9 fps failure timeline in 5.3 is the manifest's, from `logs/ictest5-text.csv` / `ictest5-msg.csv`.
- The item-row texts in 4.1 and 4.6 are transcribed from the frames by the capture agent; the geometry and
  the row counts were re-measured here, the strings were not re-OCR'd.

## 7. Still missing after the batch

| # | Item | Status | Reason |
|---:|---|---|---|
| 1 | A numeric error code page/dialog (`800xxxxx`) | **not reachable** | section 5.6 |
| 2 | A game-launch error page | **does not exist here** | no `PARAM.SFO`; Start is a no-op |
| 3 | The System Update failure page | **blocked by an emulator fault** | `sys_fs_write` -> `Access denied`, console frozen; the overlay is RPCS3 UI |
| 4 | A depth-2 page with focusable rows | **not on this console** | the status page is a read-only table, the other depth-2 page is a message page |
| 5 | A two-button confirm box / message-box plate | **not reached** | no reachable console path produces a boxed confirm; the error page's heading is a dim ghost below luma 45, not a measurable plate |
| 6 | Cue audio (open, close, commit) | **not captured** | no audio dump and no loopback; video timing only |
| 7 | Color list item names | **do not exist** | the firmware draws unlabelled chips; only the current value is named |
| 8 | A fixed offset for the network-test failure | **not deterministic** | 12 / 15 / 26 s across three runs |
| 9 | The band's row-origin rule | **unexplained** | measured, not derived: 494 vs 537 for the same string |
| 10 | Media player panels, masked OSK, signed-in indicators, copy/busy dialogs | **unchanged from batch 1** | empty VFS, offline console, no PSN account |

## 8. How to read this batch

- Frame paths in this document and in the audit corrections are **relative to
  `C:\dev\out\rpcs3-ref2-2026-10-28\`**.
- Crop bursts are 1:1 sub-rectangles of the 1920x1080 client area, declared in each burst's `rect.txt`:
  `menus/*-crop2` = `1150,150,770,700`; `bands/theme-band-*2` = the same; `bands/bg1-focus-move` =
  `450,380,1000,650`; `error/ictest5-fail` = `620,450,740,170`. A crop frame's pixel (0,0) is that rect's
  origin in the full frame; nothing is scaled, so coordinates are directly comparable with the stills.
- Timing is firmware time at clock scale 100 %; the rig's 131-235 fps crop rates are the sampling rate, not
  the animation's.
- Non-ASCII strings from the frames are transliterated as in the other audits: `(R)` and `(TM)` for the
  marks, `O` and `X` for the Circle and Cross glyphs.
- Every number in sections 4 and 5 is a measured screen pixel or a measured duration; where the manifest's
  summary rounds, section 6.1 names the frame value used.
- The rig is restored (section 2). Nothing here needs a rollback.

No source file was edited for this document. The frames stay under `C:\dev\out\`, git-ignored.
