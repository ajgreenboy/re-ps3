# Behaviour and state-machine fidelity: the 4.93 XMB against native-xmb at `07a4fe4`

Read-only analysis. No source, harness, budget, reference or other audit was edited. This document audits
**behaviour and state machines** only: every input on every surface, navigation and focus rules, the timed
state machines, audio cues, the PS system menu, boot, power/session, user and account switching, the
Steam/Jellyfin surfaces, error pages, notifications, and the end-of-list/loading/multi-select behaviours.
Pixel and layout parity is another domain and is not re-scored here.

- **Tree audited:** `C:\dev\xmb` at HEAD `07a4fe4` (2026-10-29 18:33 -0500). The tree was clean when this
  pass started; a concurrent rendering pass then modified `view.cpp`/`view.h` (a nine-slice `sourceInset`
  parameter and draw-order work; the diff was still growing when this file was closed - at that moment
  `view.cpp` +108/-27, `view.h` +3/-1). Every behaviour citation here is to HEAD, and the key view lines
  were re-read from `git show HEAD:` after the other pass landed (e.g. HEAD `view.cpp:1952` is the sink,
  HEAD `view.h:136` is `psHoldBlackMs`); a reader in the dirty tree will find them shifted by that pass's
  insertions.
- **Binary measured:** `C:\dev\xmb\out\native-xmb\re-ps3-native.exe`, built 2026-10-29 18:33 (same commit,
  before the concurrent view edits, so the measured binary is the HEAD build).
- **Method:** every row was checked twice - once against `native-xmb/src/*.cpp|h` at HEAD, and, where a
  script token, log channel or report can see it, once against a run of the current binary. Rows that only
  have a code answer say so; rows whose console behaviour has no local artifact are `unprovable` and are
  listed with the artifact that would settle them (section 14). Nothing is inferred into a `match`.
- **Console evidence** is cited as an audit section, a measured burst/frame, an RCO page, or a traced
  call/constant. Where the only evidence is an earlier audit's reading of a frame, the row says
  "via BEHAVIOUR-GAP-AUDIT <id>" rather than claiming the frame was re-read here.

## Counts

| behaviours audited | match | differ | absent | unprovable locally |
|---:|---:|---:|---:|---:|
| **189** | **99** | **26** | **21** | **43** |

Per group (the ids are the row prefixes used below):

| group | rows | match | differ | absent | unprovable |
|---|---:|---:|---:|---:|---:|
| I - input model, every surface | 81 | 46 | 7 | 6 | 22 |
| N - navigation, focus, scroll, lockouts | 18 | 11 | 2 | 2 | 3 |
| T - timed state machines | 24 | 15 | 1 | 1 | 7 |
| A - audio cues per action | 14 | 9 | 0 | 1 | 4 |
| P - PS system menu | 8 | 3 | 4 | 0 | 1 |
| B - boot / start-up | 6 | 2 | 0 | 3 | 1 |
| S - power and session | 9 | 2 | 4 | 3 | 0 |
| U - users, accounts, Steam, Jellyfin, errors | 17 | 7 | 7 | 2 | 1 |
| O - notification behaviour | 6 | 3 | 0 | 0 | 3 |
| X - end-of-list, loading, multi-select | 6 | 1 | 1 | 3 | 1 |

Why this is larger than the 129-row `BEHAVIOUR-GAP-AUDIT`: every input is now scored **per surface**
(root, page, dialog, message, item menu, player panel, media, photo, OSK, PS menu, settings band, download
page, Jellyfin sign-in) instead of one row per key; the cue map is scored per action instead of one C15 row;
and three surfaces the older audit reduced to one stale row each (Jellyfin sign-in E14, Download Management
E13/D15, the user switch D16) are several rows here. 14 of its rows are also **stale** and are re-scored
below (section 12).

## How to read a row

- `match` - at HEAD the behaviour is what the console does, tied to a code line and/or a measured run.
- `differ` - implemented, but not what the console does: wrong binding, wrong surface, wrong semantics, or a
  native-only replacement.
- `absent` - no handler/action exists at HEAD (checked by grep for the handler/keyword, not by a status
  column).
- `unprovable` - the console behaviour cannot be established from the artifacts on this machine. The row is
  still scored against **what the code does**, and section 14 names the missing artifact.
- Priority: **P0** breaks the core path or the 1:1 claim; **P1** a real console behaviour is missing or
  replaced on a common surface; **P2** narrower surface or a measured-but-open detail; **P3** rare or
  cosmetic. Only 2 rows are P0 (T4, U17); the 1:1 claim's exposure is concentrated in P1.

## 1. Measure this pass ran

All under `C:\dev\out\behaviour-audit\` (scratch evidence, not part of the tree). Every run used
`--dev-flash C:\dev\rpcs3\bin\dev_flash` and an isolated `--data-dir`, and `--script` therefore implies the
scripted-input gate (`main.cpp:949`, log `input: scripted run; live pad events ignored`).

| run | script (abridged) | channels | what it measured |
|---|---|---|---|
| `nav`, `nav2`, `nav3` | clamp sweeps, 11x DOWN, pages, Triangle, F10 | `XMB_LOG`, `XMB_MOTION_LOG`, `XMB_INPUT_TRACE`, `XMB_DIALOG_LOG`, `XMB_AUDIO_LOG` | category clamp (LEFT x10 -> Users idx 0, RIGHT x12 -> Friends idx 9), row clamp, page open/close depth trace, options open/close, PS menu + confirm step |
| `sink`, `sink2` | 11x DOWN then 3x DOWN; `XMB_OVERSCROLL=0/4` pin | `XMB_LOG` | end-of-list sink: selection stays 10/11, ink moves ~113 px over 3 presses (~38 px/press); the pinned projection reproduces the sink |
| `pages` | page open then `burst=close@ESC@40@12`, `burst=cat@RIGHT@30@10` | `XMB_LOG`, `XMB_MOTION_LOG` | page close: first target-0 sample at 8953 ms, untouched through 8976, first step 8985 (-1.0%), 0.0100 by 9281; options burst series |
| `ps` | `pshold`, DOWN, ESC, `ps`, timed grabs | `XMB_PS_LOG`, `XMB_MOTION_LOG` | hold fired 1071 ms, black 1078, text 0->1 1287..1329, bloom 1.0 1848; tap plate present at +0.3 s/+1.5 s, gone at +4.1 s |
| `psconfirm` | `F10`, ENTER, ESC | `XMB_PS_LOG` | Turn Off raises the "Turn Off the System?" line and the "X turns the XMB off; the PC stays on   O cancels" line; ESC from that step returns straight to the XMB (`confirm.png` / `afteresc.png`) |
| `notif` | `NOTIFY=Alpha\|...`, 5 more notifications | `XMB_NOTIFY_LOG` | dwell 4000 ms (`notificationRemainingMs` 3558 at grab), slide 48 px over 200 ms, queue 4 + fifth dropped, FIFO |
| `dialog2` | Continue Watching -> Triangle -> Remove -> box | `XMB_DIALOG_LOG`, `XMB_AUDIO_LOG` | box opens on No; LEFT yes / RIGHT no; ESC cancel; ENTER No commit=0; ENTER Yes commit=1 removes the row |
| `users` | seeded profiles Alpha (vol 20, libA 1 row) and Beta (vol 80, libB 2 rows) | `XMB_LOG` | whole-session switch: `shell: user switched from ... volume= 20 library/video= ...libA to ... volume= 80 library/video= ...libB`; library page then lists 2 rows |
| `osk` | Settings -> System Settings -> System Name -> OSK | `XMB_LOG` | OSK opens prefilled with the host name, typing inserts, caret drawn, ESC closes; the key faces carry the L1/R1/L2/R2/SELECT/START badges |
| `band` | Display Settings -> Screensaver -> choice band | `XMB_LOG`, `XMB_AUDIO_LOG` | band opens on the committed value, 5x DOWN clamps, UP x2 -> "After 20 Minutes", ENTER writes `display/screensaverMinutes=20`, pops depth 2->1, cue `snd_system_ok` |
| `boot` | no `--script`, `XMB_INTRO_GRAB=...@2.5,5.4,5.7,9.9` | `XMB_LOG` | PS3 logo at 2.502 s, XMB already up by 5.821 s (ShowGUI 5.5), no epilepsy notice frame |
| `reports` | `--steam-accounts-report`, `--steam-downloads-report`, `--steam-friends-report` | JSON | 2 remembered Steam accounts with switch plans; downloads 0 rows, `error:""`; friends HTTP 401 error string |

Caveat on timing: at 19:30-19:45 this machine was running several concurrent instances of this same binary
(other audit passes). The PS watcher's 20 ms poll fired the hold at **1071 ms** instead of 1000, which
delays the black onset to 1078 ms; the envelope itself is press-relative, so the *shape* is right and the
offset is scheduler load, not code. A minimised window also throttles `paintGL` to ~1.5 s frames, which is
why the first options run was discarded and re-run with a normal window.

---

## 2. Input model, every surface (81 rows)

Shared plumbing: the left stick is merged into the d-pad bits (`view.cpp:3238-3242`), L2/R2 are read as
analog axes and never merged (`view.cpp:3208-3210`), the PS bit is stripped from navigation
(`view.cpp:3203`). Keyboard and pad both route through `Shell` (`view.cpp:3161-3180`, `view.cpp:3279-3297`),
so a keyboard row and a pad row are one behaviour.

### 2.1 Root / category bar

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I1 | Left/Right moves a column and **clamps**; Game is the leftmost, Friends the right-hand clamp (REFERENCE-GAPS 212, via BEHAVIOUR-GAP-AUDIT A1) | `std::clamp(category_+delta,0,n-1)` `shell.cpp:1446`; measured LEFT x10 -> Users (idx 0), RIGHT x12 -> Friends (idx 9), no wrap | match | P1 |
| I2 | Up/Down moves inside the column; the row end clamps and then sinks the column (GRAPHICS-INVENTORY 13) | `std::clamp(selection()+delta,...)` `shell.cpp:1465`; sink at `shell.cpp:1467`; measured selection 10/11 held | match | P1 |
| I3 | Held direction: first move on the press, second after 300 ms (18 frames), then every 4 frames (66.7 ms), no acceleration (RPCS3 capture 2026-10-25, via BEHAVIOUR-GAP-AUDIT A3) | `repeatDelay=300, repeatInterval=67` counted from the last deadline `view.cpp:3249-3252` | match | P2 |
| I4 | The firmware owns list repeat; an OS repeat must not double-step | auto-repeats dropped, `keyPressed_` carries a tap between polls `view.cpp:3111-3113`, `3183` | match | P3 |
| I5 | Stick deadzone and response curve are not in any local artifact | hard 0.5 per-axis threshold, i.e. a square deadzone (a diagonal needs both axes past 0.5, ~0.71 magnitude, while the console's curve is unknown) `view.cpp:3239-3241` | unprovable | P3 |
| I6 | Cross opens the focused row (category page, folder, file, settings page) (ITEM-MENU-INFO-FIRMWARE A4/A5) | `activate()` dispatch `shell.cpp:1470-1711`; measured page open from System Settings | match | P1 |
| I7 | Circle at the root is a no-op | `back()` acts only on a message, media or page `shell.cpp:2585-2617`; measured ESC at the root changed nothing | match | P2 |
| I8 | Triangle opens the item menu (`expage_menu`), dismissed by Circle, no Close row (ITEM-MENU A2-A4) | `toggleOptions()` `shell.cpp:2004-2047`; measured `shell: options opened` | match | P1 |
| I9 | No root Square binding in any audited artifact | Square reaches shuffle/reset only while media is open `view.cpp:3296` | match | P3 |
| I10 | No root L1/R1 binding | shoulders reach skip/zoom only while media is open `view.cpp:3273`, `3294-3295` | match | P3 |
| I11 | The XMB binds **no volume** to L2/R2; the volume display is a player page (MEDIA-PANELS-FIRMWARE 5.1) | `pollTriggerSeek()` does nothing outside video `view.cpp:2519-2536`; the fix landed after the audit's section 1 (see section 12.1) | match | P1 |
| I12 | No root Start binding is documented or captured | START is read only by the OSK `view.cpp:3261` | unprovable | P3 |
| I13 | SELECT is documented only as the OSK Input Mode badge (OSK-FIRMWARE 6); no capture shows it elsewhere | SELECT is never read; grep finds no pad bit for it (`view.cpp`, `pad.cpp`) | unprovable | P3 |
| I14 | Tap PS: XMB to the front plus a controller plate for 155/66/2001/66 ms (PS-BUTTON 3) | `psHudOpacity` 155/66/2001/66 `view.cpp:2404-2409`; measured plate present at +0.3 s and +1.5 s, absent at +4.1 s | match | P2 |
| I15 | Hold PS: black at press+1052 ms, entries 1279..1326, bloom to ~1840 (PARITY-SCORECARD 16.3; PAGE-MOVE 15.5) | constants `view.h:136-139`; measured black 1078 ms, text 1287..1329, bloom 1.0 at 1848 (26 ms late because the 1 s watcher fired at 1071) | match | P1 |
| I16 | No two-button chord is documented for the 4.93 XMB | none implemented; both triggers = the harder one wins `view.cpp:3208-3209` | unprovable | P3 |
| I17 | Whether a press skips the cold-boot intro was never captured | any key skips (`view.cpp:3079`), any pad edge skips (`view.cpp:3246`) | unprovable | P3 |

### 2.2 An open page (depth 1-2)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I18 | No local artifact shows whether Left on an open page recedes it or is ignored (PAGE-MOVE's close burst was Circle) | early return while a page is open `shell.cpp:1444`; measured LEFT on the System Settings page changed nothing | unprovable | P2 |
| I19 | Up/Down moves the page's row and clamps at the ends (same shell row rule as the root) | `shell.cpp:1465`; measured 2x DOWN -> selection 2, title changes | match | P2 |
| I20 | Cross opens the focused row's target (folder, file, settings page) | `activate()` `shell.cpp:1503-1611` | match | P1 |
| I21 | Circle is one level back; leaving playback stores the resume point | `back()` pops one page and stores/clears the resume record `shell.cpp:2599-2616`; measured depth 1 -> 0 | match | P1 |
| I22 | Triangle on a page opens the item menu | `toggleOptions()`; measured | match | P1 |
| I23 | Start/Select on a page: no documented binding | not read outside the OSK | unprovable | P3 |

### 2.3 The two-answer dialog box (`MessageKind::Box`)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I24 | Cross confirms the focused answer and plays `snd_decide` (TRACED 0x00341870; DIALOGS 11.2) | `Cue::Confirm` then commit `shell.cpp:1476-1491`; measured `dialog: box enter choice= yes commit= 1` and the row was removed | match | P2 |
| I25 | Circle cancels without running the action, whatever the focus is | `clearMessage()` path `shell.cpp:2591-2598`; measured `dialog: box cancel choice= no` and the row survived | match | P2 |
| I26 | The box draws Yes left / No right and focuses No; its D-pad was never captured (DIALOGS 11.1) | one-step wrap `shell.cpp:1429-1435`; measured LEFT -> yes, RIGHT -> no | unprovable | P2 |
| I27 | D-pad Up/Down on the box is uncaptured | ignored (no handler) | unprovable | P3 |
| I28 | The box starts on No (DIALOGS 11.1 frame) | `messageBoxChoice_=BoxChoice::No` `shell.cpp:1399`; measured `choice=no` | match | P2 |

### 2.4 Non-box messages (Page / Info / Inline / ErrorCode)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I29 | Cross dismisses a message page | `Cue::Accept` then `clearMessage()` `shell.cpp:1493`; measured error/unavailable pages dismiss | match | P2 |
| I30 | Circle dismisses every message surface | same path through `back()` `shell.cpp:2591` | match | P2 |
| I31 | A message is modal: the XMB below takes no input | horizontal/vertical/activate/toggleOptions all return while `message_` is set `shell.cpp:1421`, `1464`, `1476`, `2007` | match | P2 |

### 2.5 Item options panel (Triangle)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I32 | Up/Down moves and **clamps**; the menu does not scroll (ITEM-MENU E1, CAPTURE-BATCH options-down-01..04) | `std::clamp` `shell.cpp:2055-2056`; a move that changes nothing is silent | match | P2 |
| I33 | Cross runs the focused row's own handler (per-row function pointer, ITEM-MENU A4) | `optionsActivate` switch `shell.cpp:2076-2213` | match | P1 |
| I34 | Circle steps back from the Sort By level, then closes the whole menu (ITEM-MENU H3) | `optionsNested_` branch then `dismissOptions()` `shell.cpp:2215-2236` | match | P2 |
| I35 | Triangle hides the menu | `toggleOptions()` dismisses when open `shell.cpp:2006` | match | P3 |
| I36 | The item menu has no horizontal move | Left/Right consumed and ignored while the panel is open `view.cpp:3091-3098` | match | P3 |
| I37 | The menu opens focused on the item's own command, list-level rows above it (ITEM-MENU G2) | opens on the first `dividerBefore` row `shell.cpp:2040-2041` | match | P2 |
| I38 | The console has no "listed but disabled" row; rows appear only under their condition (ITEM-MENU A4) | Copy/Delete are listed disabled and Cross reports the reason with `snd_system_ng` `shell.cpp:2069-2074`, `2186-2191` | differ | P2 |

### 2.6 Video player panel (Triangle in playback)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I39 | The video control panel's traversal was never captured; the native panel wraps | wrap `shell.cpp:2055` (playerPanel_) | unprovable | P3 |
| I40 | The console's panel is a single row of 25 buttons with `msg_*_with_icon` names (MEDIA-PANELS 3.3/3.6) | a text panel of Subtitles/Audio/Repeat/Shuffle `shell.cpp:2244-2338` | differ | P3 |
| I41 | Circle closes the console panel; Triangle's behaviour in the panel is uncaptured | Circle closes; Triangle also closes (`toggleOptions` while open) | unprovable | P3 |

### 2.7 Media playback

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I42 | Cross play/pause (the hint strip's cross slot is `msg_play/pause_with_icon`, MEDIA-PANELS 3.2/3.6) | `activate()` -> `togglePlayback()` `shell.cpp:1494` | match | P1 |
| I43 | L2/R2 are the seek pair: `tex_cp_help_l2_left`/`_r2_right` with "Fast Reverse"/"Fast Forward" (MEDIA-PANELS 3.4/3.6/11.5 item 9) | one 10 s step per press through `seekMedia` `view.cpp:2519-2536`; the console's step size is not in any artifact | unprovable | P1 |
| I44 | L1/R1 previous/next (hint art l1/r1) | `skipMedia` `view.cpp:3294-3295` | match | P3 |
| I45 | No Square binding in the video container's hint set (circle, cross, l1, l2, r1, r2, select, start) | Square toggles shuffle during playback `view.cpp:3296` | differ | P3 |
| I46 | Circle quits playback (`msg_quit_playback`, MEDIA-PANELS 3.6) | `back()` ends the session `shell.cpp:2599-2612` | match | P2 |
| I47 | Triangle opens the control surface (video panel / `control_panel_page`, MEDIA-PANELS 4.2) | video panel via `toggleOptions`, music panel via `toggleMusicPanel` `view.cpp:3288-3292` | match | P2 |
| I48 | Start and Select are in the video container's hint set | neither is read during playback | absent | P3 |
| I49 | The video container carries no d-pad hint art | d-pad Left/Right seeks +/-10 s through `horizontal` `shell.cpp:1441` | unprovable | P2 |
| I50 | d-pad Up/Down in the video container is uncaptured | `vertical()` is a no-op while media is open and not a photo `shell.cpp:1453-1463` | unprovable | P3 |

### 2.8 Photo viewer

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I51 | All four d-pad directions are the photo panel's **cursor** buttons; Previous/Next and Rotate have their own panel entries (MEDIA-PANELS 6.2 and the console's "Using the control panel" page) | Left/Right step the list when the image fits `view.cpp:3270`; only Up/Down are pan-only `shell.cpp:1454-1462` | differ | P1 |
| I52 | Up/Down pans an overflowing image, and a fitting image has nothing to pan | `photoViewport_.canPan()` pans first `view.cpp:3266-3268`; `vertical()` logs "nothing to pan" otherwise | match | P2 |
| I53 | Triangle opens the view control panel (20 buttons, `view_controlpanel_group`) | Triangle rotates the photo 90 deg `view.cpp:3274` | absent | P2 |
| I54 | Zoom/rotate are panel functions, not direct keys | L1/R1 zoom `view.cpp:3273` | differ | P3 |
| I55 | L2/R2 in the photo viewer are uncaptured (no photo exists in the local VFS) | nothing while a photo is open (`pollTriggerSeek` requires `videoOpen()`) | unprovable | P3 |
| I56 | Square in the photo viewer is uncaptured | Square resets the native viewport `view.cpp:3274` | unprovable | P3 |
| I57 | Cross in the photo viewer is uncaptured | Cross -> `activate()` -> `togglePlayback()` (slideshow) `shell.cpp:1494` | unprovable | P3 |
| I58 | Circle in the photo viewer is uncaptured (the video string is `msg_quit_playback`) | Circle ends the viewer `shell.cpp:2599` | unprovable | P3 |

### 2.9 On-screen keyboard

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I59 | Cross presses the focused key (badge table, OSK-FIRMWARE 6) | `oskPress()` `view.cpp:3257`; measured typing in the System Name field | match | P1 |
| I60 | Circle is Back: palette first, then the keyboard (OSK 4.3) | `view.cpp:3259` | match | P2 |
| I61 | Square is Backspace (badge) | `oskBackspace()` `view.cpp:3260`; measured after "Test" | match | P2 |
| I62 | Triangle is Space (badge) | `oskInsert(" ")` `view.cpp:3260` | match | P2 |
| I63 | START Accept and close (badge START on Enter) | `oskClose(true)` `view.cpp:3261` | match | P2 |
| I64 | L1/R1 move the text caret (badges on the arrow keys) | not read; caret moves only through the on-screen arrow keys / keyboard `view.cpp:3118` | absent | P2 |
| I65 | L2/R2 open the accent page / caps page (`a/a-grave`, `a/A` badges) | triggers drive seek only in video, never the OSK `view.cpp:2519-2536`; the on-screen a/A key switches page `view.cpp:3167` | absent | P2 |
| I66 | SELECT is Input Mode (badge) | SELECT unread; the on-screen key logs only that Input Mode is unavailable `view.cpp:3175` | absent | P2 |
| I67 | `msg_del_all_shift_backspace_osk` / `msg_line_break_alt_enter_osk` document Shift+Backspace and Alt+Enter | no modifier branches in the OSK key handler `view.cpp:3116-3133` | absent | P3 |
| I68 | The OSK's own repeat was never timed (OSK 537) | repeats with the XMB's 300/67 rule `view.cpp:3262` | unprovable | P3 |
| I69 | Prediction candidates come from an IME module that is not on this machine | the prediction column draws but no IME candidate source exists | unprovable | P3 |

### 2.10 PS menu input

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I70 | The console menu's traversal was never captured | wrap `shell.cpp:1740`; measured DOWN -> selection 1 | unprovable | P3 |
| I71 | Cross runs the focused row; Turn Off asks once (DIALOGS 2.9 D9) | `psMenuActivate` two-step `shell.cpp:1744-1770`; measured the confirm line and the quit path in an earlier SESSION-POWER run | match | P2 |
| I72 | Circle backs out of the menu | `psMenuClose()` `shell.cpp:1777-1781`; measured ESC closed the menu | match | P3 |
| I73 | While the menu is up the XMB receives no input | key and pad handlers return early `view.cpp:3081-3087`, `3211-3218` | match | P2 |
| I74 | A PS tap while the menu is open is uncaptured | tap closes the menu `shell.cpp:1718` | unprovable | P3 |

### 2.11 Settings band, download page, Jellyfin

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| I75 | Cross on a settings choice row opens the firmware's list band (SETTINGS 13/14) | `openChoices`; measured Display -> Screensaver opened the band on the committed value | match | P1 |
| I76 | Up/Down moves the band, scrolls it one row at the window edge, clamps at the ends (SETTINGS 16.2; CAPTURE-BATCH 195/223/224) | `windowStart` clamp `view.cpp:1522-1523`; measured 5x DOWN clamped, UP x2 moved | match | P2 |
| I77 | A write is a confirm chain: `page_*_top` -> `*_ask` -> `*_proceed` -> `*_finish` (SETTINGS 4, D27/D28) | Cross commits and pops the page immediately `shell.cpp:1527-1530`; measured depth 2 -> 1 with no ask/finish | differ | P1 |
| I78 | Download Management offers pause/resume/cancel and priority | rows open information pages; pause/resume is refused with a note because Steam has no per-title command `shell.cpp:743-804` | differ | P2 |
| I79 | Console has no Jellyfin; the surface is native | sign-in page rows edit fields through the OSK, masked password `shell.cpp:1677-1693`, `view.cpp:144-150` | match | P2 |
| I80 | - (native-only) | Submit/Cancel rows, cancel aborts a request in flight `shell.cpp:1692-1693`, `3178` | match | P3 |
| I81 | - (native-only) | "Stay signed in" toggles and deletes the stored session when turned off `shell.cpp:1683-1691` | match | P3 |

---

## 3. Navigation, focus, scroll, lockouts (18 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| N1 | Opening a page recedes the parent column (262 px focused step, 109.46 px others, 2857.2 depth; GEOMETRY-ROOT 8) | `behind` projection and the bar layer `view.cpp:1876-1878`, `1942-1962`; the audit's "not applied" (B4) is stale (section 12.8) | match | P1 |
| N2 | Closing a page returns to the column at the row it left with | `selections_` untouched; measured category selection 5 after page close | match | P2 |
| N3 | Where a page rebuilt from disk returns its focus is uncaptured | each new page starts at row 0 (`Page` default; measured page2 selection 0) | unprovable | P3 |
| N4 | Returning from a title keeps the column's row | nothing mutates the selection; `restoreFocus()` only restarts the idle timer `shell.cpp:476-479` | match | P3 |
| N5 | Each column remembers its own focused row | per-category `selections_` `shell.cpp:1379` | match | P2 |
| N6 | The console shows scroll indicators on long lists (`tex_default_scroll_*`, ICON-GAP-AUDIT C14) | item columns have no window and no scrollbar; every row is projected `view.cpp:1934-1954` | absent | P2 |
| N7 | The item menu does not scroll (no scroll object; ITEM-MENU E1) | no window, `std::clamp` `shell.cpp:2055` | match | P2 |
| N8 | The settings choice band scrolls by exactly one row at the window edge and clamps | `windowStart` clamp `view.cpp:1522-1523`; measured clamp | match | P3 |
| N9 | Settings page rows clamp (no wrap) | `shell.cpp:1465` (the same vertical rule) | match | P3 |
| N10 | A busy console blocks list actions | `vertical`/`activate`/`toggleOptions` return while `loading_` `shell.cpp:1464`, `1495`, `2007` | match | P2 |
| N11 | A busy console does not treat Left/Right as a cancel | Left/Right at the root **cancels the scan** and clears the status line `shell.cpp:1445` | differ | P2 |
| N12 | `page_busy` shows a spinning busy page while a list loads (DIALOGS 2.8, ICON-GAP C11) | no busy page; the only busy cue is the status line ("Reading media folder...") | absent | P1 |
| N13 | A message is modal over the XMB | see I31 | match | P2 |
| N14 | A notification takes no input (NOTIFICATIONS 176) | notifications are drawn outside the input path `view.cpp:2311-2312` | match | P3 |
| N15 | No console folder-depth limit is documented | native refuses past 32 levels with an inline page `shell.cpp:2624` | differ | P3 |
| N16 | End-of-list: DOWN on the last row does not move focus, the column sinks 40 px per press and stays sunk (GRAPHICS-INVENTORY 13) | `columnSink_` incremented on a clamped down move `shell.cpp:1467`, projected at `view.cpp:1952`; measured selection 10 held and ink moved ~38 px/press | match | P1 |
| N17 | Up on the first row: no frame in the corpus shows whether the column sinks the other way | Up does not sink (only `delta>0` increments) `shell.cpp:1467` | unprovable | P3 |
| N18 | Whether a notification expires while the PS menu hides it is uncaptured | it stays queued and expires on the shell clock; the draw is suppressed `view.cpp:2311` | unprovable | P3 |

---

## 4. Timed state machines (24 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| T1 | Category move: 200 ms paf accel mode 5 (PAGE-MOVE 14.1/14.2, tau 72-75 ms) | `moveMilliseconds=200`, mode 5 `view.h:50`, `view.cpp:702` | match | P2 |
| T2 | Item row move: same 200 ms mode-5 curve | `view.cpp:734` | match | P2 |
| T3 | The bar layer's own page-change re-layout is 200 ms, mode 5 (TRACED: `xmb_plugin` state+0x520, PAGE-MOVE 14.1) | `barMilliseconds=200`, `barEase_` `view.h:60`, retarget at `view.cpp:716` | match | P1 |
| T4 | The console's visible page-content channel is 18 frames (~300 ms) at 0.7741/frame (PAGE-MOVE 4c); the burst does not snap at 200 ms either | `pageMilliseconds=300`, mode 4 `view.h:59`; which channel the burst's pixels belong to is not separable at 60 Hz | unprovable | **P0** |
| T5 | Page close: untouched until +37..54 ms, then the open's own 0.762/frame curve (PAGE-MOVE 15.x, 642-frame burst) | `pageCloseDelayMs=49` `view.h:71`; measured: the first depth sample with target 0 is 8953 ms, frames at 8964/8969/8976 are still 1.0000, the first moved sample is 8985 (-1.0%), 0.8437 at 8995 and 0.0100 by 9281 (~330-350 ms after the key; the ease clock starts one paint early) | match | P1 |
| T6 | The incoming category icon is 10.223% larger and offset 210 px, easing to zero (PAGE-MOVE 14.2 decoded frames) | `entryScale`/`ahead*210` `view.cpp:1955-1961` | match | P2 |
| T7 | Item options entrance starts 115..150 ms after the key and ramps 245..255 ms (UI-ELEMENT-GAP-AUDIT O6 [M]) | `optionsOpenDelayMs=130`, `optionsOpenMs=250` `view.h:77-78`; measured: 0.0000 from 5176 to 5277, first rise 5285 (0.0699), 0.9461 at 5523 - a ~110-135 ms hold and a ~245 ms ramp | match | P2 |
| T8 | Options close starts 9 ms after the key and ramps 200 ms (O6) | `optionsCloseDelayMs=9` + `moveMilliseconds` `view.h:79`, `view.cpp:755`; measured a falling series from ~11410 to ~11610 | match | P2 |
| T9 | A notification slides 48 px from the right while its opacity goes 0->1 on the traced paf curve (NOTIFICATIONS 4.2) | 200 ms mode 5 `view.cpp:775`; measured 0.0697 at x 44.65 px falling to 0.9482 at x 2.49 px | match | P2 |
| T10 | The pop-up's dwell is not traced: `DAT_00047fcc = 2100.0` has no established meaning (NOTIFICATIONS 3) | 4000 ms per entry `shell.h:321`; measured expiry at 4000 | unprovable | P2 |
| T11 | The console's queue depth and overflow policy are uncaptured | FIFO, depth 4, the fifth is dropped with a log `shell.cpp:1323-1370`; measured | unprovable | P3 |
| T12 | Tap plate: starts at 155 ms, full 66 ms later, holds 2001 ms, gone 66 ms later (PS-BUTTON 3) | `psHudOpacity` `view.cpp:2404-2409`; measured plate present at +0.3/+1.5 s, gone by +4.1 s | match | P2 |
| T13 | Hold: black at press+1052, text 1279..1326, bloom to ~1840 (SCORECARD 16.3; PAGE-MOVE 15.5) | constants `view.h:136-139`, draw `view.cpp:2323-2382`; measured 1078 / 1287..1329 / 1848 | match | P1 |
| T14 | The tap/hold **threshold** is not in any artifact; 1052 ms is only an upper bound | `kHoldMs=1000` with the rationale at `ps_watcher.cpp:18` | unprovable | P2 |
| T15 | OSK caret: ~936 ms period, lit ~470 / dark ~440 (OSK 13.1) | `blinkMilliseconds=468` half-phase, `blinkTraced=true` `osk_layout.h:118-120`; phase at `view.cpp:838` | match | P2 |
| T16 | The OSK panel ramps open 66-96 ms and fades ~104 ms closed (OSK 13.2) | no ease: the panel appears and disappears in one frame (no OSK `PafEase` in `view.h`) | absent | P2 |
| T17 | Dialog motion: 200 units on the label group (TRACED 0x00341BFC), 1000 on the plate child (0x00344094), 100 per hint group, 150 for the error body | `dialogMilliseconds=200`, `dialogPlateMilliseconds=1000`, `dialogHintMilliseconds=100`, `dialogSettleMilliseconds=150` `view.h:434-437`; eases wired `view.cpp:787-791` | match | P2 |
| T18 | Whether the box animates its answer change is uncaptured | the answer switches instantly (`horizontal` toggles the enum, no ease) | unprovable | P3 |
| T19 | Volume HUD: 150-unit fade, 0 delay (explore_plugin.prx 0x270a00/0x270a04) | `volumeMeterFadeMs=150` `view.h:272`, applied `view.cpp:2302` | match | P3 |
| T20 | The 150-unit visible-time timer's unit is unproven (MEDIA-PANELS 4.3) | 3000 ms visible + 250 ms linear fade `media_controls.cpp:8-9` | unprovable | P2 |
| T21 | The screensaver fades the display; the target level and curve are unmeasured (SETTINGS 9.1 U2) | the whole composite dims by up to 0.55 over 750 ms `view.cpp:938-941` | differ | P2 |
| T22 | The player chrome's hide timing belongs to the videoplayer plugin (no local decompile) | 3000 ms + 250 ms `media_controls.cpp:8-9` | unprovable | P3 |
| T23 | Cold boot: BootBG2 at 0, NormalBG2 at 4.0, ShowGUI 5.5, finish 10 s (PIPELINE 183) | read at run time, `normalBg=4.0, showGui=5.5, finished=10.0` `coldboot.h:32`; measured logo at 2.502 s, XMB up by 5.821 s | match | P3 |
| T24 | The music panel is a 150-unit fade with 0 delay and a 1.0->1.3 scale (0x270a00/0x270a04/0x270a54) | `musicPanelFadeMs` 150, ease and scale `view.cpp:764-770` | match | P3 |

---

## 5. Audio cues per action (14 rows)

The cue map is `interaction_audio.h:31` / `interaction_audio.cpp:343-354`; a scripted run with
`XMB_AUDIO_LOG=1` logs `decision=play source=snd_*` per action.

| id | console cue (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| A1 | Column/row move: `snd_cursor` (INTERACTION-AUDIO 2, 10.3) | `Cue::Move` -> `snd_cursor`; measured on every move | match | P2 |
| A2 | Confirm / opening a page: `snd_decide` | `Cue::Confirm`; measured on ENTER into a page | match | P2 |
| A3 | Back: `snd_cancel` | `Cue::Back`; measured on ESC out of a page | match | P2 |
| A4 | Menu open: `snd_option` | `Cue::Options`; measured on Triangle and on opening a choice band | match | P2 |
| A5 | Module action completion: `snd_system_ok` (INTERACTION-AUDIO 10.2/10.3 H1) | `Cue::Accept` on a committed setting `shell.cpp:1528`; measured `snd_system_ok` at the band commit | match | P2 |
| A6 | Error: `snd_system_ng` (the `snd_error` entry is a silent stub, INTERACTION-AUDIO 3) | `Cue::Error` -> `snd_system_ng` | match | P3 |
| A7 | Box confirm: `snd_decide` (TRACED 0x00341870) | `Cue::Confirm` `shell.cpp:1484`; measured | match | P2 |
| A8 | The cue for a dialog **cancel** is UNPROVEN (INTERACTION-AUDIO 10.3 item 19) | `snd_cancel` on cancel `shell.cpp:2587`; measured | unprovable | P3 |
| A9 | The cue for dismissing a non-box message is UNPROVEN | `snd_system_ok` `shell.cpp:1493` | unprovable | P3 |
| A10 | Trophy notifications carry `snd_trophy` (np_trophy_util) | `Cue::Trophy` `shell.cpp:1359`, reachable only through `promoteNotification`; nothing raises a trophy icon yet | match | P3 |
| A11 | `snd_category_decide` exists as a name but explore_plugin never references it; its PCM equals `snd_cursor` (INTERACTION-AUDIO 10.3) | no Cue and no action | absent | P3 |
| A12 | The OSK's key-press cue is uncaptured | no cue is played by `oskPress/oskBackspace/oskInsert` | unprovable | P2 |
| A13 | The PS menu's move/confirm cues are uncaptured | `psMenuMove`/`psMenuActivate` play no cue | unprovable | P2 |
| A14 | Music panel open: `snd_system_ok` (explore_plugin_all.c:38936) | `playCue(Accept)` `view.cpp:2844` | match | P3 |

---

## 6. PS system menu (8 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| P1 | Two entries, plus Quit Game while a title runs (PS-BUTTON, measured frame `a21-ps-menu-1.png`) | `psMenuItems()` appends Quit Game only while `gameRunning()` `shell.cpp:1730-1735` | match | P1 |
| P2 | Focus starts on the first entry (the frame's brighter row) | `psSelection_=0` `shell.cpp:1726`; measured | match | P2 |
| P3 | Menu traversal at the ends is uncaptured | wraps `shell.cpp:1740` | unprovable | P3 |
| P4 | "Turn Off the System" asks once, then acts (DIALOGS 2.9 D9) | two-step `psConfirmOff_` `shell.cpp:1744-1750`; measured `F10`, ENTER: the "Turn Off the System?" line and the "X turns the XMB off; the PC stays on   O cancels" line are both drawn (`out/behaviour-audit/psconfirm/confirm.png`) | match | P2 |
| P5 | Turn Off removes power | the app quits to the Windows desktop and says so `shell.cpp:1746`, `main.cpp:1021-1024` | differ | P1 |
| P6 | Turn Off the Controller powers the pad off | explains that Windows owns the pad and it stays on `shell.cpp:1771-1775` | differ | P2 |
| P7 | Circle backs out of the confirm step, then closes (A48's own reading) | `psMenuClose()` ignores `psConfirmOff_` and closes the whole menu in one press `shell.cpp:1777-1781`; measured ESC from the confirm step returned straight to the XMB, not to the entries (`out/behaviour-audit/psconfirm/afteresc.png`) | differ | P2 |
| P8 | The measured frame reads "Charge Level -"; a live pad's report carries a level (SESSION-POWER 4.3) | the panel hard-codes the dash and slot 1 red `view.cpp:2364-2374` although `pad::info()` is available (used by `host.cpp:564-567`) | differ | P2 |

---

## 7. Boot / start-up (6 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| B1 | Cold-boot timeline 0 / 4.0 / 5.5 / 10 s (PIPELINE 183) | `coldboot.h:32`; measured logo 2.502 s and XMB 5.821 s (`XMB_INTRO_GRAB`) | match | P2 |
| B2 | Whether a press skips the intro is uncaptured | any key or pad edge skips `view.cpp:3079`, `3246` | unprovable | P3 |
| B3 | The photosensitive-epilepsy notice appears after ShowGUI, before the XMB (PIPELINE 183) | not ported; no string or page (`grep epilepsy` -> none) | absent | P2 |
| B4 | `welcome_1/2` presets on first-time setup (PIPELINE 158-161) | never scheduled (`grep welcome_` finds only a comment in `particles.cpp:386`) | absent | P3 |
| B5 | `gameboot1-5` plays on a title launch (PIPELINE 158-161) | never scheduled (`grep gameboot` -> none); launch goes straight to the process `shell.cpp:481-493` | absent | P2 |
| B6 | A console always boots to the XMB | native always ends at the XMB (either the intro or straight to it); `--kiosk` also starts at the XMB | match | P3 |

---

## 8. Power and session (9 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| S1 | System Auto-Off powers the display down and leaves the system running | `requestQuit` from the idle timer is mapped to `host::requestDisplayOff()` `main.cpp:1020-1029`, `host.cpp:1061-1098`; SESSION-POWER measured `display state is now 0 (off)` with the process alive | match | P1 |
| S2 | Any input ends the period and a later idle period may ask again | `Shell::touch()` only restarts its own timer `shell.cpp:2952`; `host::noteUserActivity()` still has no caller (`host.cpp:1055`). Re-arm happens only through the display-on notification `host.cpp:1013-1015`, so an ignored request repeats on the 60 s window | differ | P3 |
| S3 | Power Save's System Auto-Off opens a page of two timers (system/controller); the console's list is not captured (SETTINGS-COVERAGE AT-29) | a five-item ladder Off/15/30/60/120 min `settings_menu.cpp:159`, `312` | differ | P2 |
| S4 | The display-off request is one per idle period, with a read-back | idempotent per period with a 60 s retry and a 6 s `GUID_CONSOLE_DISPLAY_STATE` read-back `host.cpp:1065-1097` | match | P3 |
| S5 | A console has no kiosk/launcher mode | `--kiosk`/`--display`/`--start-windowed` are host features `main.cpp:1034-1044` (measured in SESSION-POWER 4.2) | differ | P3 |
| S6 | Some 4.93 SKUs offer a 4-digit console lock | design only; there is no PIN storage key, no gate and no `pin.cpp` in `src/` (SESSION-POWER 4.5 is a design hunk) | absent | P3 |
| S7 | No safe-area or UI-scale row exists in the audited Settings pages | not implemented; the transform is still `min(w/1920,h/1080)` `view.cpp:917-923` and there is no `safeArea` or `uiScale` key anywhere in `src/` | absent | P3 |
| S8 | A console has no "start with Windows" concept | documented as a user shortcut, no autostart write (SESSION-POWER 4.2) | differ | P3 |
| S9 | Background music plays while browsing (`music_1`/`welcome_1`, PARITY-GAPS B3) | no BGM source; `grep bgm` -> none, Back stops playback and clears the queue `shell.cpp:2599-2612` | absent | P1 |

---

## 9. Users, accounts, Steam, Jellyfin, errors (17 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| U1 | Selecting a user switches the whole session: settings, libraries, friends, trophies | `profiles_.select()` + `Preferences::setProfile()` + session reset + `refreshContent()` `shell.cpp:1534-1575`; measured volume 20/library libA -> volume 80/library libB with a 1-row page becoming 2 rows | match | P1 |
| U2 | A user switch reloads that user's friends/trophies | PSN-side state cannot switch (services absent); friends/trophies come from the one Steam account | differ | P2 |
| U3 | The console's "Create New User" flow (confirmation/icon) | OSK name entry then `profiles_.create()` `view.cpp:141`, `shell.cpp:3023` | unprovable | P2 |
| U4 | No console analogue (PSN account binding) | the two-step switch prompt exists only in the data layer (`steam_accounts.cpp:597-682`, `main.cpp:475-480`); `shell.cpp` does not include `steam_accounts.h`, so no surface is wired | absent | P1 |
| U5 | Download Management offers pause/resume/cancel and priority | read-only rows that open an information page; the note says Steam has no per-title control `shell.cpp:743-804`; report: 0 rows, `error:""` | differ | P2 |
| U6 | Each friend row shows the friend's avatar | every row is built with `tex_Avatar_Default` `shell.cpp:987` although the cache/CDN path exists (`steam_friends.cpp:351-660`) | differ | P2 |
| U7 | Friend rows show name and presence | presence strings from Steam `shell.cpp:988`; source is native-only but the row behaviour matches | match | P3 |
| U8 | No console analogue for Steam families | family-shared titles join the library and the Not Installed folder `shell.cpp:504-557` | differ | P3 |
| U9 | No console analogue | in-XMB Jellyfin sign-in page (server/user/password rows, masked password) `shell.cpp:1677-1693` | match | P2 |
| U10 | - | DPAPI remembered sign-in with an explicit toggle `shell.cpp:1683-1691` | match | P3 |
| U11 | - | resume point applied on LoadedMedia and playback reporting (`shell.cpp:1231-1247`, `jellyfin_client`) | match | P3 |
| U12 | A failed action shows the measured error page (cause + suggested actions, DIALOGS 9.1/9.2) | `showErrorPage` + `MessageKind::ErrorCode` `shell.cpp:1395-1406`, used for a failed launch with the OS's own cause `shell.cpp:396-419`; the Steam friends failure surfaces as the Open row's detail instead (`shell.cpp:988-992`) | match | P2 |
| U13 | No 800xxxxx code is producible on this console (DIALOGS 9.3) | the page is drawn but never carries a code | absent | P2 |
| U14 | No console row type says "this feature is not in the recreation" | `Action::Unavailable` opens a page with that sentence `shell.cpp:1694-1699` | differ | P3 |
| U15 | Settings and Connection Status List is a read-only table (SETTINGS 12, DIALOGS 12) | `host:network` -> depth-2 table page `shell.cpp:1515`, `1589` | match | P3 |
| U16 | A title boots with `gameboot1-5` and the XMB returns on quit (PIPELINE 158-161) | launches RPCS3 (`--no-gui`) or asks Steam `shell.cpp:481-493`; quit is a 3 s terminate then a page message `shell.cpp:1761-1766`; no boot preset and no return animation | differ | P2 |
| U17 | Tap PS over a running title raises the in-game XMB over the game (PARITY-GAPS 4 item 8) | the app raises its own window and flashes the plate; there is no overlay over the game `view.cpp:2731-2776` | differ | **P0** |

---

## 10. Notification behaviour (6 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| O1 | The pop-up is built from `page_notification`: plaque, icon, one- or three-line text (NOTIFICATIONS 6) | `Notification` model + `drawNotification` `shell.h:126-136`, `view.cpp:2311` | match | P2 |
| O2 | Whether a notification is deferred while the PS menu is up is uncaptured | it is hidden and still expires `view.cpp:2311` | unprovable | P3 |
| O3 | Whether the console raises a pop-up when a title quits is uncaptured | `notify(...,"Returned to the XMB",...)` on a tracked title's return `shell.cpp:408-410`, `471` | unprovable | P3 |
| O4 | A trophy notification carries `snd_trophy` | the cue exists but no trophy icon is ever raised (dormant) | match | P3 |
| O5 | The icon set is `tex_notification_*` in system_plugin | 14 names accepted (base, caution, friend, headset, info, keypad, mediasever, music, psbutton_insensitive, settings, four trophy metals), unknown names fall back `shell.cpp:1326-1340` | match | P3 |
| O6 | Queue overflow behaviour is uncaptured | the fifth is dropped with a log, depth 4 `shell.cpp:1323-1370`; measured | unprovable | P3 |

---

## 11. End-of-list, loading, multi-select (6 rows)

| id | console behaviour (evidence) | native at HEAD (evidence) | verdict | prio |
|---|---|---|---|---|
| X1 | The sunk column returns when focus actually moves | `columnSink_=0` on any horizontal move, a real row move, open and back `shell.cpp:1420`, `1466`; measured selection 10 -> 9 on UP, which is the branch that runs the reset | match | P2 |
| X2 | The sink's curve is not shown by the frames on disk (GRAPHICS-INVENTORY 13 gives the step, not the ease) | a straight 40 px offset with no ease `view.cpp:1947-1953` | unprovable | P2 |
| X3 | Copy Multiple / Delete Multiple pages exist (`msg_copy_plural`, `msg_del_plural`, ITEM-MENU A3/A5) | no multi-select anywhere in `src/` (`grep multiSelect` -> none) | absent | P2 |
| X4 | Display All / Group Content are list-page commands (ITEM-MENU A3) | intentionally omitted with a logged reason `shell.cpp:1879-1880` | absent | P3 |
| X5 | A busy list shows `page_busy`, and list actions are blocked | a status line ("Reading media folder...") instead of the page; actions blocked (N10) | differ | P2 |
| X6 | Delete/Copy Multiple run select-all/deselect-all/decide pages | no select-all or multi-item confirm path | absent | P2 |

---

## 12. Stale claims in the existing record

Each item names what changed and why the older statement cannot be quoted as current. This is the section
the lead should use before copying any older row into the consolidated report.

1. **`BEHAVIOUR-GAP-AUDIT` sections 1-5 vs its own section 10.** The main tables still mark A11 (L2/R2 volume),
   A26 (video seek), A32 (photo rotate) and D16 (user switch) as `differs`, and A2 as unprovable. Section 10's
   hunks landed with the navigation wave (`4076eac`), so at HEAD: `pollTriggerSeek` (`view.cpp:2519`) does
   nothing while browsing, `Shell::vertical` pans/no-ops for photos (`shell.cpp:1453-1462`), `SelectUser` runs
   the full session switch, and the sink exists. Those rows are **match** here (I11, I43, I52, U1, N16).
2. **`TRANSITION-HARNESS` section 7 FAIL verdicts are older than the fix they accuse.** It records page-close
   -62 ms / 55 px ahead, cat-right 150 px and options open/close FAIL, on a source snapshot of 17:28, before
   `4076eac` (page-close hold, category icon, options content latch). The current depth trace shows the 49 ms
   hold then the 300 ms fall (T5), and the code carries the traced 210 px/1.10223 incoming offset (T6). The
   harness must be re-run before those verdicts are repeated; this pass measured the timing, not the pixel
   deltas.
3. **Options timing (audit rows C1/C2 say 200 ms).** The code and the measurement are 130 ms delay + 250 ms
   ramp open, 9 ms + 200 ms close (`view.h:77-79`; UI-ELEMENT-GAP-AUDIT O6), which is what T7/T8 record.
4. **OSK caret 500 ms.** `osk_layout.h:118-120` is 468 ms half-phase with `blinkTraced=true` (already noted in
   the audit's 7.1); other documents still carry 500.
5. **`SETTINGS-COVERAGE` 8.8 item 3 (volume HUD segments never render).** Fixed by `07a4fe4`: the plate and
   the ten segments now share one alpha (`view.cpp:2601-2614`). The 8.8 note is a record of the defect, not
   the current build.
6. **`SESSION-POWER` 4.1 "shell hunk still required".** The behaviour is in `main.cpp:1020-1029`; the shell
   still emits `requestQuit` (`shell.cpp:1275`). 4.3's controller-panel hunk is still unapplied, so P8 stands.
7. **`BEHAVIOUR-GAP-AUDIT` A48 "Circle backs out of the confirm step" is not what the code does.**
   `psMenuClose()` closes the menu from the confirm step in one press; the row's `match` is unsupported at
   HEAD (P7).
8. **`BEHAVIOUR-GAP-AUDIT` B4 "the receding bar is not applied" is stale.** `view.cpp:1876-1878` and
   `1942-1962` implement the parent recede (`109.46 + 152*focus` px) and the 2857.2 depth scale (N1).
9. **`BEHAVIOUR-GAP-AUDIT` E5 reads "re-arm absent".** `noteUserActivity()` is still uncalled, but the re-arm
   does happen through the display-on notification (`host.cpp:1013-1015`); the row is a mechanism difference,
   not an absence (S2).
10. **`JELLYFIN-NATIVE`'s "remembered sign-in is not implemented"** is superseded by its own later section and
    by `shell.cpp:1683-1691`; read that file top-to-bottom, not its first paragraph (U10).
11. **`BEHAVIOUR-GAP-AUDIT`'s `E22` premise is partly wrong.** The measured console frame itself reads
    "Charge Level -" (PS-BUTTON 3); the gap is that a live pad's report is available natively and not drawn,
    so the row is a P2 difference in data use, not a proven console mismatch (P8).
12. **`SETTINGS-COVERAGE` AT-29 records an Auto-Off gap the behaviour audit does not have.** The console's
    Power Save page is a two-timer page; the native page is a five-item ladder (`settings_menu.cpp:159`).
    That is a behaviour-semantics gap and is S3 here.
13. **The build's own `--settings-report` numbers moved.** The settings-phase commit message says 143 rows and
    32/33 tests; `SETTINGS-COVERAGE` 8.5 records 46 acceptance tests with 32 PASS / 6 PARTIAL / 4 NOT MET /
    3 NOT RUN. The 46-test table is the authority (AT-4's own counts in 8.8 are also newer).
14. **`BEHAVIOUR-GAP-AUDIT` count 129 is not comparable to this 189.** Different row granularity (per-surface
    inputs), four extra audio rows, and three surfaces it did not cover (Jellyfin sign-in, download rows,
    user switch).

---

## 13. The top 12 behavioural gaps by impact

Ranked by how much of the console's behaviour a user meets, weighted by whether the console's side is
proven. Each line: what the console does -> what this build does, with the row ids.

1. **Settings writes have no confirmation chain and no result page (I77; C12/C13).** The console runs
   `page_*_top` -> `*_ask`/`*_choice_type` -> `*_proceed` -> `*_finish` for a value write (SETTINGS 4, D27/D28);
   the native write commits and pops the page in one frame (`shell.cpp:1527-1530`). Measured: the Screensaver
   band committed and dropped depth 2 -> 1 with only `snd_system_ok`. This is the largest single behaviour
   difference in the build because it applies to every one of the 26 writable rows, and the console has a
   visible page state this build never shows. The `Restore Default Settings` row is the one exception (it is
   confirm-gated, `shell.cpp:1701-1709`), which proves the box path works - the chain is simply not modelled.
2. **Power semantics of the PS menu (P5/P7/P8, S3).** "Turn Off the System" quits to the Windows desktop
   instead of removing power, Circle at the confirm step closes the whole menu instead of backing out, the
   charge panel hard-codes "-" while `pad::info()` has the level, and Power Save is a five-item ladder where
   the console has a two-timer page. Four related differences on the console's only system surface; the
   deliberate no-power-off is disclosed in PS-BUTTON, the confirm-step and charge-panel differences are not
   recorded anywhere as console mismatches, and the ladder is recorded only in `SETTINGS-COVERAGE` AT-29.
3. **Auto-Off re-arm and semantics (S2/S3).** The display-off request works and was measured (`display state
   is now 0`), but it is re-armed by the display-on notification rather than by input (`noteUserActivity` is
   dead code), and the settings surface does not have the console's two timers. A user who turns the panel
   back on with a key gets the XMB back but the next idle period depends on Windows reporting state 1.
4. **The photo viewer lost the console's control panel (I51-I58).** Up/Down are correctly the pan cursor, but
   Left/Right still step to the next image (the console gives all four directions to the cursor), and Triangle
   rotates instead of opening the 20-button `view_controlpanel_group`. Zoom/reset have no console direct-key
   evidence. A whole panel surface is absent, and two bindings contradict the measured panel page.
5. **The media input set (I43, I45, I48-I50).** The seek pair is now L2/R2 (correct), but the step size is an
   unmeasured 10 s, Square toggles shuffle with no console binding, Start/Select are dead although the video
   container names them, and d-pad Left/Right seek with no console evidence. Playback is the surface where a
   couch user spends the most time and four of its eight keys are wrong or invented.
6. **The OSK's four missing keys and its missing fade (I64-I67, T16).** L1/R1 caret, L2/R2 capital/accent
   pages, SELECT Input Mode and Shift+Backspace/Alt+Enter are all absent, and the panel appears/disappears in
   one frame where the console ramps 66-96 ms in and ~104 ms out. The key faces still draw the L1/L2/R1/R2
   badges, so the UI advertises bindings that do nothing (visible in `out/behaviour-audit/osk/osk.png`).
7. **Game launch/quit has no gameboot and no XMB return (U16; B5).** The title starts as a bare process and
   the quit path is a terminate plus a page message; the console's `gameboot1-5` preset and its return to the
   XMB are absent. This is P2 by surface but it is the transition a console user sees most often.
8. **The in-game XMB is not modelled (U17, P0).** A tap over a running title raises this app's own window and
   the controller plate; the console slides its XMB over the game. This is the only P0 **difference whose
   console side is proven**: the recreation cannot be mistaken for the console here, and the local artifacts
   cannot even show the console's version (PARITY-GAPS 4 item 8).
9. **Multi-select and the file commands (X3/X6, X4).** Copy Multiple / Delete Multiple, select-all and the
   decide page do not exist, and the Copy/Delete rows are listed disabled with an error cue. Every file-level
   management action the console offers on a local row is missing.
10. **The Steam account surface (U4) and download controls (U5).** The two-step switch prompt exists only in
    the data layer (`--steam-accounts-report` names both accounts and a `switchPossible` plan); there is no
    surface in `shell.cpp`. Download rows are read-only facts where the console has pause/resume/cancel.
11. **The user switch is now right, but the state behind it is not (U2/U6).** Settings and libraries switch
    (measured), while friends/trophies cannot, and friend rows still ignore the avatar cache. The console's
    user switch is a total identity change; this one is a preferences-and-paths change.
12. **The busy page and the loading lockout (N11/N12/X5).** There is no `page_busy` spinner, the only busy cue
    is a status line, and Left/Right cancels a running scan - an input the console's busy state does not
    document. On a slow library this is the first thing a user sees, and it is the one place where the build
    invents an escape instead of showing a state.

Also P1 but outside the twelve: no background music (S9), the phonebook of small cue differences (A8/A9),
and the player panel contents (I40).

---

## 14. What cannot be proven locally, and the artifact that would settle it

Nothing here is invented into a `match`; each row keeps its `unprovable` verdict above.

| id | behaviour | why local evidence cannot settle it | artifact that would settle it |
|---|---|---|---|
| I5 | Stick deadzone / response curve | no artifact records the DS3 curve; the native threshold is a stated choice | a controller-capture burst sweeping the stick, or the input mapping in `explore_plugin` |
| I12 | Start at the root | no capture or string maps Start outside the OSK | an RPCS3 input burst with Start at the XMB root |
| I13 | Select outside the OSK | only the OSK badge is documented | an input burst; a firmware key map |
| I16 | Two-button chords | no chord frame or table exists | an input capture pressing L2+R2 / Select+Start |
| I17 | Intro skip by any key | no boot capture with a press during coldboot | an RPCS3 boot capture with a scripted press |
| I18 | Left on an open page | the page-close burst was driven by Circle | one burst pressing Left on an open page |
| I23 | Start/Select on a page | undocumented | an input burst |
| I26 | Left/Right in the dialog box | no burst shows the box's D-pad (DIALOGS 11.1) | one burst with the box open |
| I27 | Up/Down in the dialog box | same | same burst |
| I39 | Player panel traversal | the panel's module has no local decompile and no capture | videoplayer_plugin disassembly or a control-panel capture |
| I41 | Circle/Triangle closing the player panel | same | same |
| I43 | L2/R2 seek step | the hint art proves the pair, not the step | a timed seek capture (frame trace with a trigger press) |
| I49 | Media d-pad Left/Right | no d-pad hint art exists for the container | a video-player input capture |
| I50 | Media d-pad Up/Down | same | same |
| I55 | Photo L2/R2 | no photo exists in the local VFS; no photo burst (CAPTURE-BATCH 3.5, GRAPHICS-INVENTORY 4) | a photo-viewer capture |
| I56 | Photo Square | same | same |
| I57 | Photo Cross | same | same |
| I58 | Photo Circle | same | same |
| I68 | OSK held repeat | the OSK's own repeat was never timed (OSK 537) | an OSK timing capture |
| I69 | OSK prediction candidates | no IME module or dictionary on the machine | ime.prx and an accent/prediction capture |
| I70 | PS menu traversal wrap | no traversal burst | a PS-menu Down-at-last-row capture |
| I74 | PS tap while the menu is open | no capture of that combination | a PS burst with the menu already up |
| N3 | Where a page rebuilt from disk returns its focus | no capture of reopening a page | a two-pass page capture |
| N17 | Up at the first row (sink or not) | GRAPHICS-INVENTORY 13 measured DOWN only | a frame with Up at row 0 |
| N18 | Notification under the PS menu | no capture | a notification raised while the menu is up |
| T4 | Which page-content channel the burst measures | 60 Hz burst, both 200 and 300 ms candidates fit different channels | a 200+ fps page-open burst |
| T10 | Notification dwell | `DAT_00047fcc = 2100.0` has no traced meaning (NOTIFICATIONS 3) | a timed notification capture |
| T11 | Queue depth/overflow | no multi-notification capture | a capture raising five notifications |
| T14 | Tap/hold threshold | no burst releases just before the menu appears | a press-release sweep at 200+ fps |
| T18 | Answer-change animation in the box | no burst of the box's answer row | a box burst pressing Left/Right |
| T20 | Volume display's 150-unit visible-time unit | the paf layout is in vsh.elf outside the local decompile (MEDIA-PANELS 4.3/5.4) | the paf `PhLevelMeter` layout and timer unit |
| T22 | Player chrome hide timing | videoplayer_plugin has no local decompile | decrypted videoplayer_plugin.prx |
| A8 | Dialog cancel cue | no audio capture of a dialog | an audio capture of a console box cancel |
| A9 | Non-box dismiss cue | same | an audio capture of a console message dismissal |
| A12 | OSK key cue | no OSK audio capture | an OSK audio capture |
| A13 | PS menu cues | no PS-menu audio capture | a PS-menu audio capture |
| B2 | Intro skip | no boot capture with input | an RPCS3 boot capture |
| U3 | Create New User flow | no capture of the console's creation flow | a user-creation capture |
| O2 | Notification during the PS menu | no capture | the same |
| O3 | Return-from-title notification | no capture of a title quit | an RPCS3 game-launch/quit capture |
| O6 | Queue overflow policy | no capture | a five-notification capture |
| X2 | Sink curve / ease | the frames on disk give the 40 px step, not its curve (GRAPHICS-INVENTORY 13) | a frame-by-frame overscroll burst |

Out of scope by decision, not merely unproven: console power-off (PS-BUTTON limits), PSN account binding and
its encryption (PARITY-GAPS 4 item 9), Bluetooth pairing/rumble (PARITY-GAPS 4 item 2), and any in-game
capture requiring an installed title (PARITY-GAPS 4 item 8).

---

## 15. What this pass did and did not do

- No source, harness, budget, reference or other audit was modified. The only file written is this one.
- The tree moved under this pass: `native-xmb/src/view.cpp`/`view.h` were clean when the audit started and
  carry another workstream's rendering edits by the end (`git status` at the close). This audit did not touch
  them; its view citations were re-checked against `git show HEAD:` so they name HEAD, not the dirty copy.
- Scratch runs and their logs/sidecars/frames are under `C:\dev\out\behaviour-audit\` (`nav`, `sink`, `pages`,
  `ps`, `psconfirm`, `notif`, `dialog2`, `users`, `osk`, `boot`, `band`, `reports`), plus the seeded data
  directories `data2`/`data3`. They are evidence, not deliverables.
- The pixel parity of these states (the page-move geometry, the plate's colour, the options band) belongs to
  the rendering audits; this document asserts only what a key/log/state/report can show.
- Where a run was made on a loaded machine the load is disclosed in section 1 rather than smoothed over; the
  PS-hold numbers above are the measured ones, not the ideal constants.

Reproduction (one example per class):

```
# input / navigation, with the motion and dialog channels
re-ps3-native.exe --dev-flash <flash> --data-dir <isolated> --start-windowed \
  --script "full,wait,wait,LEFT,... ,grab=C:/tmp/a.png,DOWN,grab=C:/tmp/b.png,quit"
XMB_LOG=<file> XMB_MOTION_LOG=1 XMB_INPUT_TRACE=1 XMB_DIALOG_LOG=1 XMB_AUDIO_LOG=1

# PS tap/hold envelope
XMB_PS_LOG=1 XMB_MOTION_LOG=1 --script "full,wait,wait,pshold,grab=p0,grab=p1,...,ps,grab=t0,pause=1200,grab=t1,pause=2600,grab=t2,quit"

# notifications and the queue
XMB_NOTIFY_LOG=1 --script "NOTIFY=Alpha|detail|tex_notification_info,grab=n0,pause=450,grab=n1,pause=4300,grab=n2,quit"

# boot timeline
XMB_INTRO_GRAB=<dir>@2.5,5.4,5.7,9.9 (no --script; the intro is disabled by --script)
```

The `grab=` sidecar is the state oracle used throughout: `category`, `categoryIndex`, `depth`, `selection`,
`entryTitle`, `entryCount`, `pageKind`, `notificationCount/RemainingMs`, `signInState`, `mediaOpen`,
`displayOffRequested`, `padEventsIgnored`, `windowWidth/Height`.
