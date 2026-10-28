# Firmware capture batch 2026-10-27/28: session record and coverage

One RPCS3 session of the real PlayStation 3 4.93 XMB, captured to close the unattributed measurements
in the firmware audits. This document is the batch's session record: what the rig was, what it produced,
which audit question each capture answers, the headline numbers, what could not be captured and why, and
the residual state the session left behind.

Source root: `C:\dev\out\rpcs3-ref-2026-10-28\`. The file manifest (`MANIFEST.md`, same root) is the
authority for what each file satisfies and for what could not be captured; it is cited as "MANIFEST"
below. Frame paths in this document are relative to that root unless they start with `C:\`.

Scope: documentation only. No application source, no `work/` script, no harness, no threshold, no
reference image and no firmware file was edited by this pass. All numbers are 1920x1080 screen pixels.

---

## 1. Session facts

| Item | Value |
|---|---|
| Session | one RPCS3 boot, 2026-10-27 21:34:07 to 21:59:5x local (America/Chicago, i.e. 2026-10-28 UTC), pid 9684, started and stopped by the capture agent |
| RPCS3 build | `RPCS3 v0.0.43-0b0fa082 Alpha \| master \| local_build`; git `0b0fa082ae552ab77a70de4fc1f70110d165116b` (2026-10-21); `git describe` `v0.0.43-237-g0b0fa082a` |
| Runtime title | `FPS: 59.99 \| Vulkan \| 0.0.43 \| vsh.self []` |
| Host | Windows 10.0.26200, Ryzen 5 5600X, 47.93 GiB, NVIDIA RTX 3070 (driver 610.62.0.0) |
| Boot | `rpcs3.exe --no-gui --config <scratch>\config\capture-config.yml C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self` |
| Scratch config | `config/capture-config.yml`, a copy of `C:\dev\rpcs3\bin\config\config.yml` with only the changes in MANIFEST section 8 |
| Video | Vulkan, 1920x1080, 16:9, VSync disabled, Exclusive Fullscreen Mode: Disable |
| Window | borderless, client area exactly 1920x1080 |
| Capture, stills | `CopyFromScreen` of the client area (PrintWindow returns stale frames under Vulkan) to PNG |
| Capture, bursts | full-frame `BitBlt` into a DIB, per-frame millisecond timestamp recorded, then one PNG per frame plus `frames.csv`; measured 42.9 to 54.8 fps |
| Session logs | `logs/rpcs3-session.log` (13.1 MB), `logs/rpcs3.out.txt`, `logs/rpcs3.err.txt` |
| Tools | `tools/refcap.ps1` (key/grab/burst plan runner), `tools/series.ps1` (per-frame luma and green metric) |
| Plans | `plans/*.txt`, one replayable key sequence per capture group |
| Firmware writes | none by the agent; one self-write by the console, see section 7.1 |

### 1.1 Input configuration, hash verified

`C:\dev\rpcs3\bin\config\input_configs\global\Default.yml` is the DualSense mapping and is what `vsh.self`
uses (`--input-config` is ignored for it). It was backed up, replaced for the run, and restored.

| Step | Time | sha256 | Bytes |
|---|---|---|---|
| Original backed up to `backup/Default.yml.orig` | 21:33 | `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` | 66 |
| Capture mapping installed (`config/Default.yml.capture`: Keyboard handler, Cross=PgDown, Circle=PgUp, Triangle=Insert, Square=Delete) | 21:34:07 | `40200e9c6b2954ec95d8db1c3fbada210fe469a70b7ae75d7c2881cd3ed24835` | 139 |
| Original restored | 22:00:13 | `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` | 66 |
| Verification | 22:00:13 | **matches the original exactly** | - |

Trace: `logs/input-config-hash.txt`. The global `C:\dev\rpcs3\bin\config\config.yml` was never touched:
it still reads `Resolution: 1280x720`, `Exclusive Fullscreen Mode: Automatic`, `Date Format: ddmmyyyy`,
`Time Format: clock24`. Only the scratch config above carries the capture settings.

Key mapping for the whole session (from MANIFEST section 1): Cross/Enter = `PgDown` (VK 0x22), Circle/Back
= `PgUp` (0x21), Triangle/Options = `Insert` (0x2D), Square = `Delete` (0x2E), directions = arrow keys
(0x25-0x28), Start/Select = `Return`/`Space`. Letters do not survive vsh's keyboard path; arrows and
PgUp/PgDn do, and `Insert` was added for Triangle and works.

---

## 2. Resolution and inventory

Every image is 1920x1080, full client area, decoded PNG. There is **no resolution conversion anywhere in
this batch or in the audit updates**: 1 frame pixel = 1 screen pixel at 1080p.

| Group | Stills | Bursts | Burst frames | Burst rates (fps) |
|---|---:|---:|---:|---|
| `dialogs/` | 13 | 2 | 218 | 54.1 (open), 54.8 (close) |
| `settings/` | 51 | 4 | 480 | 53.7 (list open), 53.4 (commit), 46.3 (date-format open), 42.9 (time-format open) |
| `settings/restore/` | 38 | - | - | - |
| `osk/` | 6 | 3 | 430 | 45.5 (panel open), 53.7 (caret), 47.0 (panel close) |
| `media/` | 3 | - | - | - |
| `nav/` | 24 | - | - | - |
| **Total** | **135** | **9** | **1128** | 42.9 to 54.8 |

1,263 PNGs, 853 MB, all 1920x1080.

Burst naming: one PNG per captured frame named `f_<ms>.png`, where `<ms>` is the frame's millisecond
timestamp in the burst; `frames.csv` repeats the pair (`ms,file`). The fps of a burst is
`frame count / captured span`, as recorded in MANIFEST section 4.

---

## 3. Coverage: wish item -> status -> frame

Status vocabulary: **CLOSED** = the audit question is answered by a frame in this batch; **PARTIAL** = the
capture exists but one named sub-question is still unmeasured; **OPEN** = not captured, with the reason in
section 7.

### 3.1 DIALOGS (batch items 1-6)

| # | Wish item | Status | Frames |
|---:|---|---|---|
| 1 | Options menu open + Down x4 with a grab per step | PARTIAL | `dialogs/options-open-01.png`, `dialogs/options-down-01.png` ... `-04.png`; geometry in section 5 |
| 2 | Options open and close bursts at >= 30 fps | PARTIAL | `dialogs/options-open-burst2/` (108 frames, 1.977 s, 54.1 fps), `dialogs/options-close-burst2/` (110 frames, 1.990 s, 54.8 fps) |
| 3 | Cross on `Information` -> the full `info_list` page | CLOSED | `dialogs/information-page-01.png`, `-02.png`; `dialogs/options-after-info.png` (back) |
| 4 | Signed-in indicators (message, chat, online count, avatar, plus), alphas, crowding | OPEN | none; `dialogs/root-clock-mmdd-12h.png` records the unsigned-in bar |
| 5 | Busy indicator, copy crowding | OPEN | none |
| 6 | `MM/DD/YYYY` + 12-hour clock, plate measured | CLOSED | `dialogs/root-clock-mmdd-12h.png`, `-2.png` |

Item 5 of the DIALOGS numbering is also the ITEM-MENU "which object hides when all five are eligible"
question (MANIFEST section 7 no. 7); it follows from item 4 and is open for the same reason.

### 3.2 SETTINGS U1-U10

| id | Wish item | Status | Frames |
|---|---|---|---|
| U1 | 1080p row pitch and focused-row expansion | CLOSED | `settings/accessory-rest.png`, `display-page-rest.png`, `display-screensaver-focused.png` |
| U2 | Choice-page geometry: band, list origin, item pitch | CLOSED | `settings/screensaver-list-rest.png`, `screensaver-open-burst/` (118 frames, 2.179 s, 53.7 fps) |
| U3 | Choice open/commit animation (and the cue) | PARTIAL (video yes, audio no) | `settings/screensaver-commit-burst/` (139 frames, 2.583 s, 53.4 fps), `dateformat-open-burst/` (120 frames, 2.572 s, 46.3 fps), `timeformat-open-burst/` (103 frames, 2.379 s, 42.9 fps) |
| U4 | Check/dot on the current choice | CLOSED | `settings/screensaver-list-rest.png`, `screensaver-down-1.png` ... `-5.png`, `display-after-screensaver-cancel.png` |
| U5 | The cue for committing a value | OPEN (audio not recorded) | - |
| U6 | Value on a list row is text, not a bar/dot | CLOSED | `settings/page-sound-settings.png`, `page-music-settings.png`, `page-game-settings.png` |
| U7 | Row order of the pages with no frame | CLOSED for the visible top of each page | `settings/page-game-settings.png`, `page-video-settings.png`, `page-music-settings.png`, `page-chat-settings.png`, `page-system-settings.png`, `page-theme-settings.png`, `page-date-time-settings.png`, `page-power-save-settings.png`, `page-accessory-settings.png`, `page-printer-settings.png`, `page-display-settings.png`, `page-sound-settings.png`, `page-security-settings.png`, `page-remote-play-settings.png`, `page-network-settings.png` |
| U8 | System Update page source | PARTIAL (page yes, RCO no) | `settings/system-update-page.png`, `-2.png`, `column-system-update.png` |
| U9 | Theme/Color/Background/Font option lists | PARTIAL | `settings/theme-choice.png`, `color-choice.png`, `background-choice.png`, `font-choice.png` |
| U10 | Whether the US build lists `Summer Time` | CLOSED | `settings/page-date-time-settings.png`, `date-time-page-fresh.png` |

### 3.3 ITEM-MENU-INFO A5 / A6 / A7 / B3

| item | Wish item | Status | Frames |
|---|---|---|---|
| A5 | `info_list` page geometry | CLOSED | `dialogs/information-page-01.png`, `-02.png` |
| A6 | Options menu box, row pitch, first row, clamp | PARTIAL | `dialogs/options-open-01.png`, `options-down-01..04.png`, `options-open-03.png` |
| A7 | Open/close animation and its duration | PARTIAL | `dialogs/options-open-burst2/` (54.1 fps), `options-close-burst2/` (54.8 fps) |
| B3 | Whether the clock plate grows with the date string | CLOSED | `dialogs/root-clock-mmdd-12h.png`, `-2.png` (plus the existing `C:\dev\xmb\out\native-reference\ab-rpcs3-root.png`) |
| A6/D3 #4, #5 | Signed-in indicator set and crowding; busy/copy | OPEN | none |

### 3.4 OSK

| item | Wish item | Status | Frames |
|---|---|---|---|
| OSK 5.2 | Caret blink period and duty at >= 30 fps | CLOSED | `osk/caret-burst/` (172 frames, 3.185 s, 53.7 fps); `logs/caret-series.csv` |
| OSK 7 | Panel open and close fade duration at >= 30 fps | CLOSED | `osk/panel-open-burst/` (136 frames, 2.968 s, 45.5 fps), `osk/panel-close-burst/` (122 frames, 2.576 s, 47.0 fps); `logs/osk-titlebar-luma.csv`, `logs/osk-close-titlebar-luma.csv` |
| OSK 9 | Default focus and prediction cell | CLOSED | `osk/osk-default-focus.png`, `osk/user-create-dialog.png` |
| OSK 5.4 | Masked/password entry | OPEN | `osk/bd-remote-passkey-01.png`, `-02.png` (probe only; no field) |
| OSK 6 | Key-repeat timing | OPEN | none |

### 3.5 Media panels and page move

| item | Wish item | Status | Frames |
|---|---|---|---|
| MEDIA 9 | Video/music player panels, volume OSD, photo viewer | OPEN | `media/video-column-empty.png`, `music-column-empty.png`, `photo-column-empty.png` (empty-state evidence only) |
| PAGE-MOVE 7 | Does the dialog burst bear on the page-move composite question? | NO, and it is stated why | `dialogs/options-open-burst2/f_0000692.png`, `options-close-burst2/f_0000610.png`; answer in `PAGE-MOVE.md` section 11 |

---

## 4. Headline numbers

All values are 1920x1080 pixels. Where a number differs from MANIFEST section 6, the frame-scanned value
is given and the difference is listed in section 6.1; the audits use these values.

### 4.1 Root, info bar and clock

| Quantity | Value | Frame |
|---|---|---|
| info-bar plate | x 1329..1919, y 75..132 (591 x 58 visible; flush to the right screen edge) | `dialogs/root-clock-mmdd-12h.png` |
| plate growth with a longer date string | **none**: identical plate rect in `10/27 9:53 PM` (12-hour, MM/DD) and `25/10 12:32` (24-hour, DD/MM) | `dialogs/root-clock-mmdd-12h.png`, `-2.png`; `C:\dev\xmb\out\native-reference\ab-rpcs3-root.png` |
| clock text right edge | x 1760 in both formats | same frames |
| clock text left edge | x 1562 (`10/27 9:53 PM`), x 1592 (`25/10 12:32`) | same frames |
| analog clock glyph | x 1774..1805, y 88..119 (32x32, centre 1789.5/103.5) | same frames |
| unsigned-in info bar | plate + clock only; no status icons | `dialogs/root-clock-mmdd-12h.png` |

### 4.2 Options menu (Triangle on `Corrupted Data`)

| Quantity | Value | Frame |
|---|---|---|
| menu background | full-height vertical gradient band; left edge x ~1330, fading to transparent by x ~1690; the XMB behind is dimmed | `dialogs/options-open-01.png` |
| rows | `Start`, `Delete`, `Information`; focused first row `Start` | `dialogs/options-open-01.png` |
| row text left edge | x ~1334 | `dialogs/options-open-01.png` |
| row ink bands | y 538..562, 581..598, 621..638; baselines ~560/598/636 -> **pitch 38 px** | `dialogs/options-open-01.png` |
| Down x4 | focus reaches `Information` and clamps (no wrap); the rows do not scroll | `dialogs/options-down-01..04.png` |
| open/close fades | open is mid-alpha ~190 ms after the key; close is still partial ~110 ms after the key | `dialogs/options-open-burst2/f_0000692.png`, `options-close-burst2/f_0000610.png` |

### 4.3 Settings pages

| Quantity | Value | Frame |
|---|---|---|
| focused row | centre y 505; label ink y 490..519, baseline ~516 | `settings/accessory-rest.png` |
| rows below focus | y 682, 762, 842, 922, 1002 -> **pitch 80 px** | `settings/accessory-rest.png`, `page-video-settings.png`, `page-system-settings.png` |
| focused-row expansion | title + 2 help lines (ink 528..564); the next row sits 177 px below the focused row, i.e. +97 px over the 80 px pitch; rows below keep 80 | `settings/accessory-rest.png`, `display-page-rest.png` |
| label column | ink starts x 628 | `settings/page-game-settings.png` |
| value column | ink starts x 1301 (left-aligned text; no dot, bar or slider) | `settings/page-game-settings.png` row 680 |
| page height | 6 full rows plus a 7th cut at 1080p on Accessory Settings | `settings/accessory-rest.png` |

### 4.4 Choice lists (Screensaver, Theme, Color, Background, Font, Date Format, Time Format)

| Quantity | Value | Frame |
|---|---|---|
| shape | full-height right-hand band over the dimmed parent page; left edge x ~1260 for the Screensaver list (the item-menu overlay starts at ~1330), fading out by x ~1660 | `settings/screensaver-list-rest.png`, `theme-choice.png` |
| item text left edge | x ~1278 | `settings/screensaver-list-rest.png` |
| Screensaver items | `After 5 Minutes`, `After 10 Minutes`, `After 20 Minutes` (focused), `After 30 Minutes`, `Do Not Use` | `settings/screensaver-list-rest.png` |
| item ink tops | 426, 466, 506, 546, 586 -> **pitch 40 px** | `settings/screensaver-list-rest.png` |
| current value mark | none: the current item is focused (bold/white) but carries **no check or dot**; `Down` moves only the focus | `settings/screensaver-list-rest.png`, `screensaver-down-1..5.png` |
| Theme list | **not empty**: `Install`, `Original` (focused), `Classic`, `Air Paint` (with a thumbnail) | `settings/theme-choice.png` |
| Color list | `Original` + 11 colour chips + overflow arrow | `settings/color-choice.png` |
| Time Format order | `24-Hour Clock`, then `12-Hour Clock`; Down clamps at the last item, Up selects 12-hour | `settings/timeformat-list-rest.png`, `-12h-focused.png`, `-12h-selected.png` |

### 4.5 Information page

| Quantity | Value | Frame |
|---|---|---|
| rules | y 199 and y 879, 1 px full width (mean luma 176.8 and 182.0 over x 200..1600) | `dialogs/information-page-01.png` |
| icon plate | x 665..985, y ~263..442 | `dialogs/information-page-01.png` |
| rows | `Title`, `Parental Control`, `Created`, `Size`; ink tops 499/539/579/619 -> pitch 40 px | `dialogs/information-page-01.png` |
| label column | right-aligned, last ink x 802-803 | `dialogs/information-page-01.png` |
| value column | left-aligned, first ink x 844-853; `Created` = `22/10/2026 2:14`, `Size` = `0 KB`, the other two `-` | `dialogs/information-page-01.png` |
| back hint | `O Back`, ink x 1155..1240, y 897..916 | `dialogs/information-page-01.png` |
| frame treatment | no dim; the info bar is replaced by the rules; the category band is off-screen | `dialogs/information-page-01.png` |

### 4.6 OSK

| Quantity | Value | Frame |
|---|---|---|
| caret period | ~936 ms (~1.07 Hz); lit 495/504/498 ms, dark 442/431 ms; lit duty ~53% | `osk/caret-burst/`, `logs/caret-series.csv` |
| panel | x 493..1426, y 290..789 (934 x 499) | `osk/osk-default-focus.png` |
| title / field | title `User Name`; field `User 1` with the green I-beam and hollow cursor marker | `osk/osk-default-focus.png` |
| default focus | `g` (row 2, column 5 one-based) | `osk/osk-default-focus.png` |
| prediction cell | `a 0` | `osk/osk-default-focus.png` |
| function rows | `@#: / a/a-grave / a/A / Backspace / Space`; bottom `arrows / Input Mode / Enter / clipboard / keyboard` | `osk/osk-default-focus.png` |
| open alpha ramp | ~66-96 ms (title-bar luma 5.9 at 1166 ms, 71.8 at 1196, 102.1 at 1219, 119.5 at 1241, 120.7 from 1262 ms); panel at rest ~550 ms after Cross (the dialog dims first) | `osk/panel-open-burst/`, `logs/osk-titlebar-luma.csv` |
| close fade | ~104 ms visible (120.6 at 803 ms, 135.5 at 843 flash, 124.2 -> 108.6 -> 88.9 -> 36.3 -> 6.0 at 947 ms) | `osk/panel-close-burst/`, `logs/osk-close-titlebar-luma.csv` |

### 4.7 Date and Time

| Quantity | Value | Frame |
|---|---|---|
| rows | `Date and Time`, `Set Automatically`, `Date Format`, `Time Format`, `Time Zone`, `Daylight Saving` | `settings/date-time-page-fresh.png` |
| `Summer Time` row | **does not exist** in the US build; the row is `Daylight Saving`, value `Standard` | `settings/date-time-page-fresh.png`, `page-date-time-settings.png` |
| values in this session | `27/10/2026 21:43`, `On`, `DD/MM/YYYY`, `24-Hour Clock`, `GMT+13:00 Independent State of Samoa`, `Standard` | `settings/date-time-page-fresh.png` |

---

## 5. What could not be captured, and why

These are the audit rows that stay open. The reasons are the rig's, not the capture agent's effort.

| # | Item | Status | Reason |
|---:|---|---|---|
| 1 | Signed-in indicators (message, chat, online count, avatar, plus), their alphas and crowding; the error-code page that a failing sign-in would produce | not captured | the emulated console is offline: `Net: Internet enabled: Disconnected`, `PSN status: Disconnected`, and no PSN/RPCN account exists on this rig. Signing in is a network plus credential operation and cannot be faked. |
| 2 | Copy / busy indicator and the copy crowding policy; any delete/copy confirmation | not captured | a copy needs a copyable object. `dev_hdd0\{savedata,music,video,photo,theme}` hold no content files (savedata has only an empty `vmc`), `dev_usb000` has only empty folders, and the only Game item is corrupt (`dev_hdd0/game/TEST12345` has no `PARAM.SFO`). No PKG was installed (out of scope). |
| 3 | Video player panel, music player panel, volume display, photo viewer chrome | not captured | same empty VFS: no video, music or photo file exists, so no player can start. The volume OSD is driven by the BD remote's volume keys or hardware IR; there is no pad path and no such control in the XMB menus. |
| 4 | OSK masked/password entry (the WPA key and Bluetooth passkey fields) | not captured | the password `EditBox`es live in `netconf` (WPA/WEP key) and `deviceconf` (Bluetooth passkey). WPA needs a scanned SSID (no network device); `Register BD Remote Control` waits for a physical BD remote (`osk/bd-remote-passkey-02.png`). No password was typed or logged. |
| 5 | A real installed game's option menu (the batch used `Corrupted Data`) | captured on the nearest item | there is no valid installed game. The `Corrupted Data` menu is the same `expage_menu` page (`Start / Delete / Information`); geometry is item-independent, but the row *set* is the corrupt-item set (MANIFEST section 7 no. 5). |
| 6 | The cue **audio** for an open/commit (A7/A5/A3, U3/U5 "with audio") | video only | RPCS3 audio went to the host device and `Audio: Dump to file` was false at boot; no loopback capture was configured. The animation timing is in the bursts; the sound is still open. |
| 7 | Which of the five indicator objects hides when all are eligible | not captured | depends on item 1. |
| 8 | `software_update_plugin` RCO from this dev_flash | page captured, RCO not dumped | no RCO dumping in this pass; the page itself is captured. |
| 9 | The tail rows of pages longer than 7 rows (Video, System) and the OSK key-repeat timing | not captured | the stills are top-of-page only; repeat timing was not exercised. |
| 10 | A two-button confirm box, a message box, the box plate rectangle, the PS menu confirmation | not captured | no reachable console path produced one in this rig state; see the DIALOGS audit. |

Deliberately not attempted, because each would change the rig beyond the capture: mounting a disc image,
installing a PKG, enabling the network, creating an RPCN account.

---

## 6. Numbers re-measured against the frames

The batch's summary in MANIFEST section 6 rounds several values. Every audit update in this pass was
checked by re-scanning the cited frame directly (bundled Python/Pillow; luma =
0.2126 R + 0.7152 G + 0.0722 B; the ROI and threshold are stated in the audit). Six figures differ
materially from the manifest; the frame values are used, because the audit is only as good as the frame.

### 6.1 Differences

| Quantity | MANIFEST section 6 | Frame scan | Frame used |
|---|---|---|---|
| options menu row y | 479 / 516 / 554 | 560 / 598 / 636 (ink 538..562, 581..598, 621..638) | `dialogs/options-open-01.png` |
| settings page row pitch | ~78 px | **80 px** (rows 682/762/842/922/1002) | `settings/accessory-rest.png`, `page-video-settings.png` |
| info-page rules | ~190 and ~915 | **199 and 879** (915 is the `Back` hint row, ink 897..916) | `dialogs/information-page-01.png` |
| clock text right edge | ~1880 | **1760** in both date formats | `dialogs/root-clock-mmdd-12h.png`, `ab-rpcs3-root.png` |
| OSK panel | x 493-1427, y 282-755 | x 493-1426, y **290-789** | `osk/osk-default-focus.png` |
| menu band left edge | ~1180 | ~1330 (options), ~1260 (Screensaver list) | `dialogs/options-open-01.png`, `settings/screensaver-list-rest.png` |

The rows, the pitch and the two rules are internally consistent across the batch's own frames and with
the older measurements in the audits (the item-row vertex constants 680/760/840 support the 80 px pitch;
the D4 frame's 197/877 supports the 199/879 rule pair; `ITEM-MENU-INFO-FIRMWARE.md` B3 already measured
the clock text ending at 1760). The manifest's pitch (37-38 px) for the options menu matches the frame's
38 px; only its absolute y is offset, by 82 px.

### 6.2 What was not re-measured

Alpha ramps, the caret period and the burst-based fade numbers are taken from MANIFEST section 6 and the
per-frame CSVs; they were not re-derived in this pass. The OSK panel's outer rect is measured; its
internal bands (title 46, field 58, grid 324, footer 58 in `OSK-FIRMWARE.md` section 4.1) were not.

---

## 7. Honesty caveats

### 7.1 `xRegistry.sys` was rewritten by the console

Committing the Date/Time and Daylight Saving values through the XMB made vsh rewrite its own settings
store, `C:\dev\rpcs3\bin\dev_flash2\etc\xRegistry.sys`, at 21:58:42 (with the 21:58:43 copy in
`etc\backup\`). That is the console writing its own registry through its normal save path, not a file the
agent edited.

- On-screen values were returned to the originals and are verified on screen:
  `settings/restore/restore10-dst-committed.png` (DD/MM/YYYY, 24-Hour Clock, `Standard`),
  `settings/verify-chat-page.png` (`Voice/Video Chat Start = Send Video`),
  `settings/restore/restore7-vibration-page.png` (`Controller Vibration Function = On`).
- No pre-session copy of `xRegistry.sys` was taken, so its **bytes are not guaranteed identical** to the
  pre-session file and its mtime is new. The values, not the bytes, are what the restore audit proves.
- Everything else under `dev_flash`/`dev_flash2` has pre-session timestamps; no firmware install, no PKG
  install and no network change was made.

### 7.2 The rest of the rig

- The global input config was restored byte-identically (section 1.1); the scratch config is the only
  changed config file and it is not the live one.
- The Date Format, Time Format and Daylight Saving values passed through transient states during the
  session (`MM/DD/YYYY`, `12-Hour Clock`, `Daylight Saving`) and were restored; the restore sequence is
  38 frames under `settings/restore/`.
- `Screensaver` was opened and committed **at its current value** only (`After 20 Minutes`), so it never
  changed (`settings/display-after-screensaver-commit.png`). `Controller Vibration Function` was opened
  while probing and never changed (`settings/restore7-vibration-page.png`).
- The `Game` column contains `PlayStation(R)Store`, `Saved Data Utility (PS3(TM))` and `Corrupted Data`;
  it is not a normal library, so no "one installed game" state exists in this rig.

### 7.3 What this batch does not prove

- It does not prove the *code* behind any animation, and it does not replace the traces in
  `PAGE-MOVE.md`; it measures what the console drew, at the rate it was captured.
- It does not prove the *sound* of any cue (section 5 no. 6).
- It does not prove the *masked* OSK rendering (section 5 no. 4).
- It does not prove signed-in or online states (section 5 no. 1).
- A 54 fps burst cannot resolve a 60 Hz frame count cleanly; where a duration matters, the burst gives a
  lower bound and the settle frame, not a trace.

---

## 8. What remains unattributed after the batch

Across the six audits this batch touches, these items are still without a firmware measurement:

1. Signed-in indicator set, their alphas, the crowding/hiding policy, and the Plus/avatar states
   (DIALOGS item 4, ITEM-MENU B5/B7).
2. Busy indicator and the copy/delete dialogs and their crowding policy (DIALOGS D8, ITEM-MENU D3 #5).
3. The media player panels, volume display and photo viewer chrome (MEDIA-PANELS section 9).
4. The OSK masked/password field: bullet glyph, advance, caret behaviour (OSK 5.4).
5. The cue audio for open/commit/close (DIALOGS, SETTINGS U5, ITEM-MENU A8, OSK sound).
6. OSK key-repeat timing (OSK 6).
7. A real two-button confirm box, a message box, the box plate rect, the error-code page and the PS menu
   confirmation (DIALOGS D1/D2/D9).
8. The `software_update_plugin` RCO (SETTINGS U8).
9. Background and Font choice contents (SETTINGS U9); the tail rows of long pages (U7).
10. The exact options-menu fade duration and the `BG1` focus-plate rect (ITEM-MENU A6/A7).
11. The page-move composite question: 200 ms traced vs the capture's 18-frame count (PAGE-MOVE
    sections 7 and 11). The batch's menu bursts do not answer it.
12. The two-level-deep page geometry (GEOMETRY-ROOT section 8).

---

## 9. How to read this batch

```
C:\dev\out\rpcs3-ref-2026-10-28\
  MANIFEST.md                 the capture manifest (source of truth)
  dialogs\                    13 stills, 2 bursts (options menu, information page, clock)
  settings\                   51 stills, 4 bursts (pages, choice lists, commit)
  settings\restore\           38 stills (the revert audit trail)
  osk\                        6 stills, 3 bursts (panel, caret, masked probe)
  media\                      3 stills (empty columns)
  nav\                        24 stills (boot, routes, row checks)
  plans\*.txt                 the key sequence behind every capture, replayable
  logs\refcap-*.log           timestamped action log per plan
  logs\input-config-hash.txt  the three hashes in section 1.1
  logs\caret-series.csv       per-frame caret metric for the blink numbers
  logs\osk-titlebar-luma.csv  per-frame open-fade metric
  logs\osk-close-titlebar-luma.csv
  tools\refcap.ps1            the plan runner (keys, grabs, full-frame bursts)
  tools\series.ps1            the per-frame luma/green metric
  backup\Default.yml.orig     the original global input config
  config\capture-config.yml   the scratch RPCS3 config used for the session
  config\Default.yml.capture  the temporary Keyboard mapping
```

The measurements in this document were taken by re-scanning the PNGs at their native 1920x1080; no image
was rescaled. The audit updates under `native-xmb/audit/` cite the individual frames.
