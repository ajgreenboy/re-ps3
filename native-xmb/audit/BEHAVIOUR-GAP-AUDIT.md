# System-behaviour gap audit: what the 4.93 XMB does, and what this build does

Analysis only. No source, harness, reference or other audit was modified for this document. Read at
**HEAD `cb902fa` (2026-10-28 17:12 -0500)**. A concurrent workstream is editing the shared tree
(`src/assets.cpp`, `src/assets.h`, `src/main.cpp` are dirty against HEAD while this document was written);
every cited file (`shell.cpp`, `view.cpp`, `view.h`, `ps_watcher.cpp`, `osk_layout.*`, `host.cpp`,
`profiles.cpp`, `steam_friends.cpp`, `coldboot.h`) was unmodified, and the two `main.cpp` references were
re-read after that file grew, so the line numbers quoted below are the working tree's.

Scope: every *interactive* behaviour of the real XMB - input, navigation, focus, the timed state machines,
the per-surface action sets and the system-level behaviours - checked against `native-xmb/src/`. This is the
behaviour companion to the rendering audits (`GEOMETRY-ROOT`, `ICON-GAP-AUDIT`, `TEXT-RENDERER`): it says
nothing about pixels except where a state machine's timing is only visible as pixels.

## Counts

| behaviours audited | matches | differs | absent | unprovable locally |
|---:|---:|---:|---:|---:|
| 129 | 53 | 35 | 24 | 17 |

- **matches** - the behaviour is implemented and the claim is tied to a named audit section, frame or code
  line. A match is not a pixel claim; the visual parity of these states is scored elsewhere.
- **differs** - implemented, but not what the console does (wrong binding, wrong timing, wrong surface, or a
  stated native choice where the firmware has something else).
- **absent** - no handler/action exists; verified by grep for the handler, not by reading a status column.
- **unprovable locally** - the firmware behaviour cannot be established from the artifacts on this machine
  (no capture, no decompile, no hardware). Section 6 lists these with the missing artifact; nothing is
  invented for them.

Priority: **P0** = the behaviour breaks the 1:1 claim or is visibly wrong on the core path; **P1** = a real
console behaviour is missing or replaced; **P2** = narrower surface or a measured-but-open detail;
**P3** = cosmetic, rare, or native-only.

## Method, and how to read a row

- Firmware evidence is named in each row: an audit section, an RPCS3 frame/burst, an RCO page or object, a
  decompiled call site/constant. `TRACED` means a call, constant or frame establishes it; `FIT` means the
  project inferred it and says so; `UNPROVEN` means no local artifact settles it.
- Every `absent` row was checked by searching for the handler or the feature keyword in `src/` - the grep is
  named in the row. No status column in any earlier document was trusted without that check.
- Where an audit and the code disagree, both are recorded and section 7 states which is newer with the
  commit date (`git log -1 --format=%ci`).
- Code references are `file:line` at `cb902fa`. Shell handlers: [shell.cpp](../src/shell.cpp) `horizontal`
  :1410, `vertical` :1442, `activate` :1449, `back` :2458, `toggleOptions` :1906, `optionsMove` :1950,
  `optionsActivate` :1965, `optionsClose` :2088. Input pump: [view.cpp](../src/view.cpp) `pollController`
  :2927, `keyPressEvent` :2814.

---

## 1. Input model

The left stick is merged into the d-pad bitmask (`view.cpp:2975-2979`), so every d-pad row below also covers
the stick. L2/R2 are read as analog axes and never merged into the navigation bits (`view.cpp:2941-2947`).

| surface | input/trigger | firmware behaviour (evidence) | native status (code reference) | priority |
|---|---|---|---|---|
| A1 root | D-pad/stick Left-Right | Column move clamps at the ends; there is no wrap: Game is the leftmost column and Friends the right-hand clamp (REFERENCE-GAPS 212, SCORECARD 973, SUBTITLES 316 "9 LEFT clamps to 0"). TRACED | **matches** - `std::clamp(category_+delta,0,n-1)` shell.cpp:1437 | P2 |
| A2 root | D-pad/stick Up-Down | Row move inside the focused column. The row-end rule (wrap vs clamp) was never captured: no burst shows Up at row 0 or Down at the last row | **unprovable** - native clamps (shell.cpp:1446); the console's row end is unmeasured | P2 |
| A3 root, page | held direction | First move on the press, second 300 ms (18 frames) later, then one every 4 frames (66.7 ms), no acceleration however long it is held (RPCS3 capture 2026-10-25). TRACED | **matches** - `repeatDelay=300`, `repeatInterval=67`, counted from the last deadline so a 16 ms poll does not stretch it (view.cpp:2984-2989) | P2 |
| A4 all | OS key auto-repeat | The firmware owns list repeat; an OS repeat must not double-step | **matches** - auto-repeats are dropped and the internal timer drives the repeat (view.cpp:2848-2851) | P3 |
| A5 all | left stick | The console's stick deadzone and axis curve are in no local artifact | **unprovable** - native synthesises d-pad bits from a hard 0.5 per-axis threshold, i.e. a square deadzone with diagonal first contact (view.cpp:2977-2978) | P3 |
| A6 root | Cross | Opens the focused item: a category page, a folder, a file, a settings page (A4/A5 confirm chain in ITEM-MENU-INFO-FIRMWARE) | **matches** - `activate()` dispatch shell.cpp:1449-1623 | P1 |
| A7 root | Circle | No-op at the root; Circle dismisses every surface above it | **matches** - `back()` acts only when a page, media or message is up (shell.cpp:2460) | P2 |
| A8 root, page | Triangle | Opens the item menu (explore_plugin `expage_menu`); dismissed with Circle, no Close row (ITEM-MENU A2-A4) | **matches** in trigger - `toggleOptions` shell.cpp:1906; the *contents* differ, section 4 | P1 |
| A9 root | Square | No root binding in any audited artifact | **matches** - Square reaches only the photo reset and the media shuffle (view.cpp:3011, 3033) | P3 |
| A10 root | L1/R1 | No root binding in any audited artifact | **matches** - only photo zoom and media skip (view.cpp:3010, 3031) | P3 |
| A11 all | L2/R2 | The XMB binds no volume key; the volume display is a player page (MEDIA-PANELS-FIRMWARE 5.1: eight containers, each one Meter, no plate/label) | **differs** - L2/R2 always drive the host master volume and raise the meter on every browsing surface, read before any surface branch (view.cpp:2945-2947, 2256-2280); the only guard is `volumeInputBlocked()` for the OSK, PS menu, options panel, a message, the intro and a modal dialog (view.cpp:2187-2189) | P1 |
| A12 root | Start | No root binding is documented or captured | **unprovable** - native ignores START outside the OSK (view.cpp:2998) | P3 |
| A13 root | Select | SELECT is documented only as the OSK's Input Mode badge (OSK-FIRMWARE 6) | **unprovable** - SELECT (pad bit 0x20) is never read: grep finds it only in pad.cpp/pad.h | P3 |
| A14 depth-1/2 | D-pad Left-Right | No local artifact shows whether Left on an open page recedes it or is ignored; the page-close burst in PAGE-MOVE was driven by Circle | **unprovable** - native returns early whenever a page is open (shell.cpp:1435); JELLYFIN-NATIVE 117 records the same native choice | P2 |
| A15 depth-1/2 | Circle | One level of back: nested page -> parent page -> column; leaving playback stores the resume point | **matches** - `back()` pops exactly one page and stores/clears the resume record (shell.cpp:2472-2490) | P1 |
| A16 dialog | Cross/Enter | Confirms the focused answer with `snd_decide` (TRACED 0x00341870) | **matches** - `Cue::Confirm` shell.cpp:1463 | P2 |
| A17 dialog | Circle/Esc | Cancels without running the action, whatever the focused answer is | **matches** - shell.cpp:2464-2471 (cue per C18) | P2 |
| A18 dialog | Left-Right | The box draws Yes (left) / No (right) and focuses No; the D-pad on this box was never captured (DIALOGS-FIRMWARE 11.1) | **unprovable** - native wraps the pair so one step in either direction reaches the other answer (shell.cpp:1420-1426); DIALOGS 11.1 states the wrap as a native decision | P2 |
| A19 dialog (non-box) | Cross | Dismisses the message | **matches** with a guessed cue - `Cue::Accept` (shell.cpp:1472); the firmware cue for a dismissal is UNPROVEN | P3 |
| A20 options menu | Up-Down | Focus moves and CLAMPS at both ends; the list does not scroll - E1 and CAPTURE-BATCH `options-down-01..04` (Down x4 stops on Information) | **matches** - `std::clamp` shell.cpp:1957 | P2 |
| A21 options menu | Cross | Runs the focused row's own handler; each row carries a function pointer, not a command enum (ITEM-MENU A4) | **matches** in effect - `optionsActivate` switch shell.cpp:1965-2086 | P1 |
| A22 options menu | Circle | Steps back from the Sort By level, then closes the whole menu; there is no Cancel/Close row to focus (ITEM-MENU H3) | **matches** - `optionsClose` shell.cpp:2088-2102 | P2 |
| A23 options menu | Triangle | Hides the menu | **matches** - `toggleOptions` dismisses when open (shell.cpp:1908) | P3 |
| A24 options menu | Left-Right | The item menu has no horizontal move; the in-player panel's traversal is unmeasured | **unprovable** - the item menu ignores Left/Right, the player panel wraps (shell.cpp:1957) | P3 |
| A25 media playback | Cross | Play/pause (the video hint strip's cross slot carries `msg_play_with_icon`/`msg_pause_with_icon`, MEDIA-PANELS 3.2/3.6) | **matches** - `activate()` -> `togglePlayback()` (shell.cpp:1473); the strip draws the same strings (view.cpp:2443) | P1 |
| A26 media playback | L2/R2 | Fast reverse / fast forward: the video container's hint art lists `tex_cp_help_l2_left` and `_r2_right` for the seek pair (MEDIA-PANELS 3.4, 3.6, 11.5 item 9) | **differs** - L2/R2 change the host volume; seeking is Left/Right. The code records the mismatch itself (view.cpp:2424-2425) and seeks through `Shell::horizontal` (3016-3017 -> shell.cpp:1432) | P1 |
| A27 media playback | L1/R1 | Previous/next (hint art l1/r1) | **matches** - `skipMedia` (view.cpp:3031-3032) | P3 |
| A28 media playback | Square | No Square binding exists in the video container's hint set (circle, cross, l1, l2, r1, r2, select, start) | **differs** - Square toggles shuffle during any playback (view.cpp:3033) | P3 |
| A29 media playback | Circle | Quit playback; the container's string is `msg_quit_playback` (MEDIA-PANELS 3.6) | **matches** - `back()` ends the session and stores/clears the resume point | P2 |
| A30 media playback | Triangle | Opens the control surface: the video panel for video, `control_panel_page` for music (MEDIA-PANELS 4.2) | **matches** in trigger - video: in-player options panel, music: the XMB music panel (view.cpp:3025-3028) | P2 |
| A31 media playback | Start, Select | Both keys are in the video container's hint set (MEDIA-PANELS 3.4) | **absent** - neither is read during playback (view.cpp:3016-3035) | P3 |
| A32 photo viewer | D-pad | Pans when the image overflows; the photo viewer's own panels own zoom/rotate (MEDIA-PANELS 6.2) | **differs** - up/down route to `Shell::vertical`, which ROTATES a photo 90 degrees (shell.cpp:1444 via view.cpp:3008); only left/right step the list | P1 |
| A33 photo viewer | Triangle | Opens the view control panel (20 buttons, `view_controlpanel_group`) | **absent** - Triangle rotates the photo instead (view.cpp:3011) | P2 |
| A34 photo viewer | L1/R1, L2/R2 | Zoom/rotate are panel functions; no direct-key map is recorded in the corpus | **differs** - L1/R1 zoom (view.cpp:3010) while L2/R2 still drive host volume | P3 |
| A35 photo viewer | Square | No Square binding is recorded for the photo viewer | **differs** - Square resets the native viewport (view.cpp:3011) | P3 |
| A36 OSK | Cross/Circle/Square/Triangle | Press key / back / backspace / space, read from the key badges (OSK-FIRMWARE 6 table) | **matches** - view.cpp:2994-2997 | P1 |
| A37 OSK | START | Accept and close (badge START on the Enter key) | **matches** - view.cpp:2998 | P2 |
| A38 OSK | L1/R1 | Move the text caret left/right without deleting (badges on the arrow keys) | **absent** - L1/R1 are read only for photo zoom and media skip; the on-screen caret keys work (osk_layout.cpp:370-371, view.cpp:3173) | P2 |
| A39 OSK | L2/R2 | Accent page / caps page (badges on a/a-grave and a/A) | **absent** - the triggers are consumed by the volume ramp (view.cpp:2945); the a/A key switches page (view.cpp:3167) but no trigger binding exists | P2 |
| A40 OSK | SELECT | Input Mode (badge SELECT on that key) | **absent** - SELECT is never read; pressing the on-screen key only logs "Input Mode is unavailable" (view.cpp:3175) | P2 |
| A41 OSK | Shift+Backspace, Alt+Enter | Delete all and line break, documented by the panel's own strings `msg_del_all_shift_backspace_osk` / `msg_line_break_alt_enter_osk` (OSK 6) | **absent** - no delete-all or line-break path in the keyboard handler (view.cpp:2853-2870) | P3 |
| A42 OSK | held D-pad | The OSK's own repeat was never timed (OSK 537) | **unprovable** - native repeats with the XMB's 300/67 rule (view.cpp:2999) | P3 |
| A43 PS menu | hold PS | The frame goes fully black 1052 ms after the press and the entries settle ~1970 ms (SCORECARD 16.3, PS-BUTTON measured pass) | **differs** - the watcher fires at 1000 ms and the menu draws fully opaque on the next frame (ps_watcher.cpp:10, 95-98; view.cpp:2095-2140) | P1 |
| A44 PS menu | tap PS | The XMB comes to the front and the controller plate shows for 155/66/2001/66 ms (PS-BUTTON 3) | **matches** - `psHudOpacity` view.cpp:2162-2169 | P2 |
| A45 PS menu | tap PS over a running title | The in-game XMB comes over the game (unavailable natively, FIRMWARE-PARITY-GAPS 4 item 8) | **differs** - the app raises its own window and flashes the plate; there is no overlay over the game (view.cpp:2731, 2742-2776) | P2 |
| A46 PS menu | Up-Down | The console menu is two rows, three with a title running; its traversal was never captured | **unprovable** - native wraps (shell.cpp:1651) | P3 |
| A47 PS menu | Cross | Runs the row; "Turn Off the System" asks once (DIALOGS 2.9 D9) | **matches** - two-step `psConfirmOff_` (shell.cpp:1661) | P2 |
| A48 PS menu | Circle | Backs out of the confirm step, then closes | **matches** - `psMenuClose` shell.cpp:1688-1692 | P3 |
| A49 chords | L2+R2, Select+Start, etc. | No local artifact documents a two-button chord on the 4.93 XMB | **unprovable** - native implements none; holding both triggers makes the harder one win (view.cpp:2261-2267) | P3 |

---

## 2. Navigation and focus rules

| surface | input/trigger | firmware behaviour (evidence) | native status (code reference) | priority |
|---|---|---|---|---|
| B1 column -> page | open/close transition | 18 rendered frames (~300 ms) open and close on the console (PAGE-MOVE 4c, section 5), but the bar driver is disassembled at **200 ms, mode 5** with no mode-4 animate call anywhere (PAGE-MOVE 14.1). TRACED (200/5) vs FIT (300/4) | **differs** - `pageMilliseconds=300` and mode 4 (view.h:49, view.cpp:696), labelled a fit at view.cpp:1633 | P0 |
| B2 category move | Left-Right | 200 ms paf accel mode 5; the incoming set approaches with tau 72-75 ms (PAGE-MOVE 14.2) | **matches** - `moveMilliseconds=200`, mode 5 (view.h:48, view.cpp:694) | P2 |
| B3 item row move | Up-Down | Same 200 ms mode-5 curve as the category move | **matches** - view.cpp:704 | P2 |
| B4 page open | focus geometry | The focused row steps to x 303 and the column recedes (PAGE-MOVE 2, GEOMETRY-ROOT); the receding bar and the incoming column are still open (PHASES 13, PARITY-GAPS 1.4) | **differs** in part - the label/pen and parent icon are measured, the receding bar is not applied (view.cpp:1628-1662) | P1 |
| B5 page close | focus memory | No local artifact records where the column returns to | **unprovable** - each category keeps its own row (`selections_`), but a page rebuilt from disk starts on row 0 | P3 |
| B6 return from a title | focus | The column returns where it was | **matches** - nothing mutates the selection; `restoreFocus()` only restarts the idle timer (shell.cpp:476-479) | P3 |
| B7 long list | scroll indicators | The console shows scroll indicators; `tex_default_scroll_*` decodes but is never drawn (ICON-GAP-AUDIT C14) | **absent** - item columns have no window or scrollbar; every row is projected regardless of count | P2 |
| B8 options menu | rows beyond the band | Do not scroll, clamp (E1) | **matches** - no window, `std::clamp` | P2 |
| B9 settings choice band | long list | Scrolls by exactly one row at the last step and clamps at the ends (SETTINGS 16.2; CAPTURE-BATCH 195/223/224) | **matches** - `windowStart` clamp, view.cpp:1422-1477 | P3 |
| B10 loading | any input | A busy console shows `page_busy`; the exact lockout is not captured | **differs** - `activate`/`vertical`/Triangle are blocked while `loading_` (shell.cpp:1445, 1474, 1909), but Left/Right at the root CANCELS the running scan and clears the status line (shell.cpp:1436) | P2 |
| B11 busy | indicator | `system_plugin.rco page_busy`: `busy_icon` + `busy_icon_anim1` + two 8x36 bars, spinning (DIALOGS 2.8, ICON-GAP C11) | **absent** - the only busy cue is the ASCII "Loading..." inside the media overlay (view.cpp:1592) | P1 |
| B12 message up | any nav | Modal: the XMB below takes no input | **matches** - horizontal/vertical/activate/toggleOptions all return while `message_` is set | P2 |
| B13 notification up | Left-Right | A notification does not take input; Left still moves the category (NOTIFICATIONS 176) | **matches** - notifications are drawn outside the input path | P3 |
| B14 nested folder | depth | No console depth limit is documented | **differs** - native refuses past 32 levels with an inline page (shell.cpp:2497) | P3 |

---

## 3. State machines and timings

| surface | input/trigger | firmware behaviour (evidence) | native status (code reference) | priority |
|---|---|---|---|---|
| C1 options menu | open | Still at partial alpha ~190 ms after the key (ITEM-MENU E2), consistent with a 200 ms ramp | **matches** - 200 ms mode-5 slide (view.cpp:708-711) | P3 |
| C2 options menu | close | A visible fade of at least ~110 ms, not an instant destroy (E2) | **matches** - the same 200 ms ease runs outward | P3 |
| C3 options band | geometry | Full-height band from x1320, 38 px pitch, 237 px group gap, 0.545 content dim (E1, F6; 828518e) | **matches** - `kOptionsBand` view.cpp:1248-1250 (x1320 is the item menu's per-page border from SETTINGS 15.1; E1's ~1330 was the approximate reading) | P3 |
| C4 notification | appear/leave | Slides 48 px from the right while opacity goes 0->1 on the traced paf curve (NOTIFICATIONS 4.2) | **matches** - 200 ms mode 5, `notificationEase_` (view.cpp:727) | P3 |
| C5 notification | dwell | 4000 ms is this build's fit; the firmware's `DAT_00047fcc = 2100.0` argument has no traced meaning (NOTIFICATIONS 3) | **unprovable** - native 4000 ms per entry, FIFO, 4-deep queue | P2 |
| C6 PS tap plate | envelope | Starts 155 ms after the press, full 66 ms later, holds 2001 ms, gone 66 ms later (PS-BUTTON 3) | **matches** - view.cpp:2162-2169 with the plate at 1191,81 (2151-2155) | P2 |
| C7 PS hold menu | envelope | Fully black at 1052 ms; the entries fade in between 1202 ms and ~1970 ms (SCORECARD 16.3) | **absent** - no black hold and no fade-in; the full menu is drawn on the first frame after the 1000 ms rule (ps_watcher.cpp:95) | P1 |
| C8 OSK caret | blink | ~936 ms period, lit ~470 / dark ~440, duty ~53% (OSK 13.1) | **matches** - `OskCaret::blinkMilliseconds = 468` half-phase, `blinkTraced = true` (osk_layout.h:116-120); three audits still say 500 ms (section 7.1) | P2 |
| C9 OSK panel | open/close | Alpha ramp 66-96 ms open (at rest ~550 ms after Cross, because the dialog dims first), visible fade ~104 ms close with a bright flash (OSK 13.2) | **absent** - the panel appears and disappears in one frame (OSK 15.8 last item) | P2 |
| C10 dialogs | motion | 200 units on the label group (TRACED 0x00341BFC), 1000 on the plate child (0x00344094), 100 per hint group (`anim_*_show`), 150 for the error body (0x00340964) | **matches** - view.h:340-348 | P2 |
| C11 dialogs | dim | The plate draws over dimmed page content; the D3/D4/D5 kinds choose their own dim (DIALOGS 2.3-2.5, 10.2) | **matches** in kind - the per-kind dim constants live in dialogs.cpp and the box uses the measured 43/255 wash | P3 |
| C12 settings | value write | A confirmation chain: `page_*_top` -> `*_ask`/`*_choice_type` -> `*_proceed` -> `*_finish` (SETTINGS 4, D27) | **absent** - a write commits and pops the page immediately (shell.cpp:1501-1505) | P1 |
| C13 settings | result | `page_*_finish` with the finished text / `msg_*_enabled` (SETTINGS 4, D28) | **absent** - no result page exists | P1 |
| C14 settings | commit cue | The module-action completion plays `snd_system_ok` (INTERACTION-AUDIO 10.2/10.3 H1) | **matches** - `Cue::Accept` shell.cpp:1502; SETTINGS D29 still says `snd_decide` (section 7.3) | P2 |
| C15 all | cue map | `snd_cursor` move, `snd_decide` confirm, `snd_cancel` back, `snd_option` menu open, `snd_system_ok` accept, `snd_system_ng` error (INTERACTION-AUDIO 2, 10.3) | **matches** - interaction_audio.h:31 and the call sites listed in INTERACTION-AUDIO 10.3 | P2 |
| C16 category | decide cue | `snd_category_decide` exists as a name but explore_plugin never references it; its PCM is identical to `snd_cursor` (INTERACTION-AUDIO 10.3) | **absent** - no Cue and no action; audibly identical to Move | P3 |
| C17 all | error cue | `snd_error` decodes as a silent stub (INTERACTION-AUDIO 3) | **matches** - decoded, reported, never played | P3 |
| C18 dialog | cancel cue | `snd_cancel` for a *dialog* cancel is UNPROVEN (INTERACTION-AUDIO 10.3 item 19) | **differs** - `Cue::Back` (`snd_cancel`) is played on cancel (shell.cpp:2460) | P3 |
| C19 notifications | trophy | `np_trophy_util` sets `sound=snd_trophy` on its notification | **matches** but dormant - `promoteNotification` plays `Cue::Trophy` (shell.cpp:1351); nothing raises a trophy icon yet | P3 |
| C20 music panel | open | `snd_system_ok` right after the panel is built (explore_plugin_all.c:38936) | **matches** - `playCue(Accept)` view.cpp:2584 | P3 |
| C21 volume HUD | fade | 150 ms fade, 0 delay (explore_plugin.prx 0x270a00/0x270a04) | **matches** - the appearing edge is 150 ms | P3 |
| C22 volume HUD | visible time | The 150-unit visible-time timer's unit is unproven (MEDIA-PANELS 4.3) | **differs** - native keeps the 3000 ms visible + 250 ms linear MediaControls model | P2 |
| C23 screensaver | idle | Display Settings offers After 5/10/20/30 and Do Not Use (SETTINGS 9.1 U2); the dim target and curve are unmeasured | **differs** - the whole composite dims by up to 0.55 over 750 ms (view.cpp:869-872); the console fades the display, and no local frame measures to what level | P2 |
| C24 boot | timeline | coldboot.jsx: BootBG2 0, NormalBG2 4.0, ShowGUI 5.5, finish 10 s (PIPELINE 183) | **matches** - the script/MNU timings are read at runtime; `normalBg=4.0, showGui=5.5, finished=10.0` (coldboot.h:32) | P3 |

---

## 4. Per-surface action sets

Row types and their Triangle lists. Firmware rows come from the game builder's own array
(`explore_plugin_all.c:60846-60997`, ITEM-MENU A4) and the settled frame's two groups (G2).

| surface | input/trigger | firmware behaviour (evidence) | native status (code reference) | priority |
|---|---|---|---|---|
| D1 local folder row | Triangle | Firmware list for an owned item: Start (disc titles), Internet Search, Purchase, Copy, Delete, Check for Update, Update History, Health Warning, PS Move Notice, About this Game, IP Notice, Information last (A4); the settled frame groups list-level rows above the actions with a 237 px gap (G2) | **differs** - Sort By, Open, Copy (disabled), Delete (disabled), Information (H2; shell.cpp:1810-1824). The two file commands are listed but cannot run | P1 |
| D2 local file / photo row | Triangle | Same builder: Play/View + Copy + Delete + Information (A3 mapping) | **differs** - the same disabled Copy/Delete rows; no backend exists (grep: no copy/delete handler in shell.cpp) | P1 |
| D3 Delete | menu row | `expage_fdelete_dialog`: select-all / deselect-all / decide, a separate page (A5, D6) | **absent** - `OptionCommand::Delete` only logs and plays the error cue (shell.cpp:2057-2064) | P1 |
| D4 Copy | menu row | `expage_fcopy_dialog`: source picker, wait state, name-exists Rename/Overwrite/Do Not Save, apply-to-all (A5, D7) | **absent** - the Copy row is disabled with a note (shell.cpp:1811-1812) | P1 |
| D5 multi-select | Copy/Delete Multiple | `msg_copy_plural` / `msg_del_plural` exist and the dialogs are list pages (A3, A5) | **absent** - no multi-select anywhere in `src/` | P2 |
| D6 Move | menu row | The recovered game builder has no Move row; the other categories' builders are not fully recovered | **unprovable** - native has no Move action; no artifact says which categories offer it | P3 |
| D7 list page | Display All | A list-page command; nothing on this build's pages is grouped or filtered (F3) | **absent** - correctly omitted, and the reason is written at shell.cpp:1790-1791 | P3 |
| D8 list page | Group Content | `msg_folder_class` plus the `By Title/Album/Artist/...` keys (A3) | **absent** - no grouping store (shell.cpp:1791) | P3 |
| D9 music row | Add to Playlist | `msg_add_to_playlist` and the audioplayer playlist button (A3, MEDIA-PANELS 4.1) | **absent** - no playlist backend (F3) | P2 |
| D10 directory page | Sort By | Four date pairs in the RCO, direction glyph inside the string: Creation, Update, Import, Release Year, Film, Earned (A3) | **differs** - only Creation and Update are offered, because the other four have no source in this build (shell.cpp:2002-2013) | P2 |
| D11 Sort By | nested level | Separate 128x272 `plane_menu1/2` plates (A2); no frame shows an open level 1 (G4) | **differs** - the nested level reuses the full-height band (shell.cpp:1997-2001, H4) | P3 |
| D12 Steam title row | Triangle | The game builder's twelve rows (A4) | **differs** - Start + Information only (H2; shell.cpp:1800-1824); Search/Purchase/Update/History/Health/Move/About/IP are all out because their conditions are not evaluable (F3) | P2 |
| D13 host report rows | Cross | D5 read-only table page: own header, 40 px rows, labels right-aligned to 939, values from 979, Back hint, no focus, no dim (DIALOGS 12, 719aeb8) | **matches** - selected from the row's own `host:*` payload (shell.cpp:1515) | P3 |
| D14 Continue Watching row | Triangle | No firmware row exists for forgetting a resume point (H3) | **differs** - a native-only row with Resume / Play from Beginning / Remove (`msg_eject`), plus a confirm box (shell.cpp:1805-1822) | P3 |
| D15 download row | Triangle | The console's Download Management offers pause/resume/cancel and priority | **differs** - rows open an information page; pause/resume is refused because Steam has no per-title command (shell.cpp:720-741) | P2 |
| D16 user row | Cross | Selecting a user switches the whole session: that user's settings, libraries, friends, trophies | **differs** - `SelectUser` only rewrites `profiles.json`'s activeId (shell.cpp:1508, profiles.cpp:188-196); `Preferences::setProfile` is never called, so settings and libraries do not change | P1 |
| D17 any row | Information | `info_list` page, D5 geometry, Key: value rows (DIALOGS 2.5, ITEM-MENU E3) | **matches** - `showEntryInformation` shell.cpp:2364-2433 | P2 |
| D18 in-player panel | Triangle | The video container's own 25 buttons with `msg_*_with_icon` names (MEDIA-PANELS 3.3, 3.6) | **differs** - a text panel of Subtitles/Audio/Repeat/Shuffle rows (shell.cpp:2117-2210) | P3 |
| D19 settings choice | Up-Down + Cross | RCO list order, committed value focused, no dot, no check (SETTINGS 13, 14; U4 closed) | **matches** - the choice band + `SetSetting` rows | P2 |
| D20 Unavailable row | Cross | No console row type says "this feature is not in the recreation" | **differs** - a native row type that opens a page with that sentence (shell.cpp:1614-1620) | P3 |

---

## 5. System-level behaviours

| surface | input/trigger | firmware behaviour (evidence) | native status (code reference) | priority |
|---|---|---|---|---|
| E1 screenshots | - | The premise looks PS4-era: no frame, RCO page or string in the 122 decoded containers shows an XMB screenshot function, and the Game column's `screenshot` XMBML row is the photo *Screenshots view* (MENUS-XMBML 185, 198) | **unprovable** - native has no capture feature (grep `screenshot` finds only theme previews); with no firmware artifact the feature cannot be claimed either way, and the harness `grab=` token is a dev tool, not a user action | P3 |
| E2 background music | browsing | `music_1`/`welcome_1` presets, the music category icon animation and the music player chrome exist (PARITY-GAPS B3; PARTICLES 362) | **absent** - no BGM source: Back stops playback and clears the queue; grep finds no bgm/music-source code | P1 |
| E3 volume, media keys | L2/R2, Windows media keys | The console's volume display is a player page; Windows media keys have no console equivalent | **differs** - WM_APPCOMMAND/VK_VOLUME_* are taken app-wide and drive the host endpoint (view.cpp:2233-2242, VolumeKeyFilter :75) | P2 |
| E4 Auto-Off | idle `power/idleMinutes` | The console powers the display down and stays on (SESSION-POWER 4.1) | **matches** in behaviour - the `requestQuit` connection at main.cpp:981-996 (Auto-Off block) maps an idle request to `host::requestDisplayOff()` + a display-state read-back; the shell hunk in SESSION-POWER 4.1 was never applied (shell.cpp:1265-1267) | P2 |
| E5 Auto-Off | re-arm on input | Any input ends the period, and a later idle period may ask again (SESSION-POWER 4.1 hunk) | **differs** - `host::noteUserActivity()` has a definition and no caller (host.cpp:271), so the request repeats on the 60 s window instead of re-arming per idle period | P3 |
| E6 kiosk / launcher | startup flags | A console always boots to the XMB; there is no kiosk mode to match | **differs** - `--kiosk` / `--display` / `--start-windowed` are host features (SESSION-POWER 4.2; the kiosk block in main.cpp, header at :924) | P3 |
| E7 PIN gate | boot / user switch | Some 4.93 SKUs offer a 4-digit console lock | **absent** - design only (SESSION-POWER 4.5); grep finds no PIN code in `src/` | P3 |
| E8 safe area / UI scale | settings | No safe-area or UI-scale row exists in the audited Settings pages | **unprovable** - SESSION-POWER 4.4 is a design hunk; grep finds no safeArea/uiScale in `src/` | P3 |
| E9 game launch | Cross on a title | The title boots, `gameboot1-5` plays, and on quit the XMB returns (PIPELINE 158-161); `gameboot1-5` are never scheduled by this build | **differs** - launches RPCS3 (`--no-gui`) or asks Steam; quit is a 3 s terminate then a page message (shell.cpp:1672-1677); no boot preset | P2 |
| E10 Steam title handoff | Quit Game | The console's Quit Game closes the title it started | **differs** in scope - a Steam title is watched through the registry, so Quit Game covers only titles this app launched (PS-BUTTON honest limits) | P3 |
| E11 Steam account switching | account UI | No console analogue (Sony accounts are PSN, unprovable), but the two-step switch prompt is implemented in the data layer (b339422, STEAM-ACCOUNTS) | **absent** - no account-switch surface is wired in the shell (grep "account" finds only Steam-id detection) | P1 |
| E12 Friends rows | Cross on a row | The console shows each friend's avatar | **differs** - the avatar cache and CDN fetch exist (steam_friends.cpp:351-660) but every row is built with `tex_Avatar_Default` (shell.cpp:987) | P2 |
| E13 Download Management | page state | Active/paused/queued states with pause, resume and cancel | **differs** - read-only rows, F5 refresh, honest empty/error pages (shell.cpp:805-851) | P2 |
| E14 Jellyfin playback | sign-in, resume, reporting | No console analogue; the surface is native | **differs** - DPAPI remembered sign-in, resume and playback reporting exist (3e5bcdf); JELLYFIN-NATIVE still says remembered sign-in is not implemented (section 7.8) | P2 |
| E15 network failure | error page | The measured network-test error page is a catalogue state (DIALOGS 9.1, 2d9cd30) | **matches** in kind - `showErrorPage` + `MessageKind::ErrorCode` (shell.cpp:1395) | P2 |
| E16 numeric error code | error page | No 800xxxxx code is producible on this console (DIALOGS 9.3) | **unprovable** - the page is drawn but never carries a code | P2 |
| E17 connection status | Cross | Settings and Connection Status List is a read-only table (SETTINGS 12, DIALOGS 12) | **matches** - `host:network` -> the depth-2 table page (shell.cpp:1515) | P3 |
| E18 power | PS menu Turn Off | The console removes power | **differs** - the app quits to the Windows desktop and says so (PS-BUTTON; FIRMWARE-PARITY-GAPS 4.1) | P2 |
| E19 boot | epilepsy notice | The photosensitive-epilepsy notice appears after ShowGUI, before the XMB (PIPELINE 183) | **absent** - not ported (PARITY-GAPS M9) | P2 |
| E20 boot / launch | gameboot, welcome presets | `gameboot1-5` on a launch, `welcome_1/2` on first-time setup (PIPELINE 158-161) | **absent** - neither flow is scheduled | P2 |
| E21 boot | skip | No local artifact shows whether a press skips the intro | **unprovable** - any key skips (view.cpp:2816) | P3 |
| E22 PS menu controller panel | panel | Slots 1-4, "Controller 1", charge level; the pad's own report carries the level (SESSION-POWER 4.3) | **differs** - the panel always draws "Charge Level -" and slot 1 red (view.cpp:2123-2133) while pad.cpp parses the real battery | P2 |

---

## 6. What the firmware does that cannot be proven locally

Each row names the missing artifact, so no behaviour is invented to fill the gap.

| behaviour | why it cannot be proven here | what would settle it |
|---|---|---|
| In-game XMB over a running title (tap PS over a game, in-game menu, screenshots) | `xmb_ingame.rco`/`xmb_ingame.prx` run inside the firmware over a real title; no game is installed in the local RPCS3 and no capture exists (PARITY-GAPS 4 item 8) | an RPCS3 run with an installed title plus an input capture |
| Screenshot / capture function | no frame, RCO page or string in any of the 122 decoded containers; the XMBML `screenshot` row is the photo view (MENUS-XMBML 198) | a console frame showing the function, or a string table entry |
| PSN sign-in, Store purchase/download, trophy sync, What's New, messages, Remote Play, cloud saves | the firmware-dependent services are disconnected; the ISA forbids fabricating success (PARITY-GAPS 4 item 3) | a live PSN session and a network capture |
| Account switching (PSN user switch) as the console does it | account binding and per-console encryption have no honest PC equivalent (PARITY-GAPS 4 item 9) | out of scope by decision |
| Disc / BD title rows: Start on disc, Remove Disc, health warning, BD-J, AACS | no optical drive and no disc item in any local run (PARITY-GAPS 4 item 6) | a disc image and a drive path |
| Move / Add to Playlist / Group Content / Display All conditions | the per-category option builders are only recovered for the game category (ITEM-MENU A4); the rest are not in the local decompile | the remaining category builders from explore_plugin.prx |
| Row wrap vs clamp at the ends of an item column; Left on an open page | no burst shows Up at row 0, Down at the last row, or Left on a page | four small RPCS3 input bursts |
| D-pad behaviour on the two-button box (wrap vs clamp) | no burst shows the box's D-pad (DIALOGS 11.1) | one burst with the box open |
| PS menu traversal and PS hold envelope at 200+ fps with input timing | the 1052/1202/1970 ms envelope is measured (SCORECARD 16.3) but the fade curve between 1202 and 1970 is not separable at 206 fps | a higher-rate burst |
| Volume display geometry and the 150-unit visible-time unit | the page is a single `PhLevelMeter` with zero geometry; the paf layout lives in `vsh.elf` outside the local decompile range (MEDIA-PANELS 5.4, 4.3) | the paf `PhLevelMeter` layout and the timer unit from vsh.elf |
| Music/photo control panel positions | the RCO gives cells and sizes, not pixel positions; those are the plugin's (MEDIA-PANELS 4.3, 11.5 items 10-11) | videoplayer/audioplayer plugin disassembly (no local Ghidra program) |
| Video panel fade/hide timing and per-control sounds | `videoplayer_plugin.rco` has zero animations and the module has no local decompile and no `snd_*` strings (MEDIA-PANELS 3.8, 3.9) | decrypted videoplayer_plugin.prx |
| OSK accent page, Input Mode (IME), prediction candidates, key repeat | no IME modules, no dictionary, no accent capture (OSK 8.2/8.3, 13.4) | ime.prx plus an OSK accent capture |
| OSK panel as an alpha composite (~12% dialog show-through) | the reference shows the dialog through the panel; the native plate is opaque because the measured band colours are already composited (OSK 15.8) | a clean alpha capture of the panel alone |
| Controller power-off, Bluetooth pairing, vibration, motion calibration | owned by Windows/HID drivers (PARITY-GAPS 4 item 2) | hardware ownership, not software |
| Exact RSX/GCM bit behaviour, Cell/SPU frame coupling | a desktop GL driver cannot reproduce fp16/fixed-point and RSX blend quirks, and the host frame rate is variable (PARITY-GAPS 4 items 4-5) | hardware, or a measured tolerance (which is what the gates do) |
| Console power state (Turn Off removes power) | the PC stays on; a confirmed shutdown is a different, destructive operation (PS-BUTTON limits) | out of scope by decision |

---

## 7. Where an audit and the code disagree

Each item states which is newer; `git log -1 --format=%ci` on the commit that changed the code or the claim.

1. **OSK caret blink.** OSK-FIRMWARE 15.8 and FIRMWARE-PARITY-GAPS R17/M6 say "the blink is 500 ms with
   `blinkTraced = false`". `osk_layout.h:119-120` has `blinkMilliseconds = 468` (half of the measured 936 ms
   period) and `blinkTraced = true`; that value landed in `5dabd43` (2026-10-27 23:10). `view.cpp:783-786`
   still carries the older comment ("500 ms is the audit's FIT ... never traced"), authored in `6ead87c`
   (2026-10-27 22:58). **The code is newer than both audits and than its own comment.** Row C8 records the
   behaviour as matching; the audits need the correction.
2. **Page-move duration and mode.** PAGE-MOVE 14.1 (`3c0c87b`, 2026-10-27 21:56) disassembles the bar driver
   at **200 ms, mode 5** and calls "300 ms mode 4" contradicted as a firmware value. `view.h:49` and
   `view.cpp:696` still ship `pageMilliseconds=300` with mode 4, authored in `dbee2de` (2026-10-25), and the
   call site is labelled a fit (view.cpp:1633). **The audit is newer; the code is knowingly stale**, and the
   phase status (PHASES 14.1) says the re-time is held pending one hop of the call chain. Row B1 is the
   highest-priority timing gap in this document.
3. **Settings commit cue.** SETTINGS-FIRMWARE D29 says the value write plays `snd_decide`. INTERACTION-AUDIO
   10.2/10.3 (H1, `27f1569`, 2026-10-27 22:36) traces a module-action completion to `snd_system_ok`, and
   `shell.cpp:1502` plays `Cue::Accept`. **Code is newer**; D29 is stale (row C14).
4. **OPTIONS-MENU.md versus the shipped item menu.** That audit (2026-10-26) says Sort By opens
   `Name (A-Z)` / `Name (Z-A)` / `Close`, and that Copy/Delete/Group Content/Display All are "not shown rather
   than shown-and-inert". The code now offers four RCO date rows, has no Close row, and lists Copy/Delete as
   disabled rows with their reasons (`shell.cpp:1802-1824`). ITEM-MENU-INFO-FIRMWARE A3/H2/H3 (`828518e`,
   `b760278`, 2026-10-27 22:20-23:20) is newer and matches the code. **OPTIONS-MENU is stale**; it is still
   cited by FIRMWARE-PARITY-GAPS as if current.
5. **ITEM-MENU F3 versus H2.** F3 lists a flat order (`Open`, `Copy`, `Delete`, `Sort By`, `Information`) and
   a `Remove from Continue Watching` label; H2 shows two groups with `Sort By` first and `msg_eject` =
   `Remove`. Both are in the same file; H2 is the later section and matches `buildOptions`. **H2 is newer**
   (rows D1, D14).
6. **ITEM-MENU F6 says the options-band hunk was never applied.** `view.cpp:1248-1250` now defines
   `kOptionsBand` (x1320, pitch 38, group gap 237, 0.545 dim) and `view.cpp:1975` passes it to the panel
   (`3f3a445`, 2026-10-27 23:59). **Code is newer**; F6 is the record of a scratch validation.
7. **MEDIA-PANELS 7.1/7.2 say "native today: none"** for the video chrome and the music panel (no icons, no
   panel, Triangle cycles repeat for music). `320a69e` (2026-10-28 01:05) landed `drawVideoHelpStrip`,
   `drawVideoTimeBar`, `drawTimeCells` and `drawMusicPanel`, and Triangle opens the music panel. **Code is
   newer**; the 7.x tables are the pre-pass diff (rows A25-A30, D18).
8. **JELLYFIN-NATIVE's "remembered sign-in is not implemented" and "playback currently starts at zero".**
   `3e5bcdf` (2026-10-27 20:54) added DPAPI remembered sign-in, and the shell applies a Jellyfin resume point
   (`applyJellyfinResume`, `shell.cpp`). **Code is newer** (row E14).
9. **SESSION-POWER 4.1 says the Auto-Off shell hunk is "still required".** That part is still true
   (`shell.cpp:1267` emits `requestQuit`), but the *behaviour* is implemented in `main.cpp:981-996`, which the
   section does not say plainly; and its 4.3 controller-status hunk is also unapplied, so the PS menu still
   draws "Charge Level -" although pad.cpp parses the battery (row E22). Read those sections as "behaviour
   present, hunk open", not "behaviour missing".
10. **DIALOGS 10.5 versus section 11.** 10.5 states `Shell::activate` accepts a Box unconditionally; section 11
    (`9723cb4`, 2026-10-28 08:21) records the live choice model. Section 11 already says it supersedes 10.5, so
    this is a within-file note, not a live contradiction.

---

## 8. The 25 highest-priority differs-or-absent behaviours

Ranked; each line is what the console does and what this build does instead.

1. **B1 page open/close** - 200 ms mode 5 (traced) against 300 ms mode 4 (fit) in the shipped easing.
2. **C7 PS hold envelope** - black at 1052 ms with a 1202-1970 ms entry fade against a full menu at 1000 ms.
3. **A43 PS hold threshold** - the console's hold fires later and holds the black frame; the watcher fires at 1000 ms.
4. **D3 Delete** - `expage_fdelete_dialog` with select-all/decide; native plays the error cue.
5. **D4 Copy** - a source picker with the name-exists dialog; native lists Copy disabled.
6. **C12 settings confirm chains** - `page_*_top` -> ask -> proceed; native commits immediately.
7. **C13 settings result pages** - `page_*_finish`; native pops the page.
8. **A11 L2/R2** - no XMB volume binding; native changes the host master volume while browsing.
9. **A26 video L2/R2 seek** - the console seeks with the triggers; the native hint strip says so and the triggers do something else.
10. **A32 photo up/down** - pans/next-previous; native rotates the photo in 90-degree steps.
11. **D16 user switch** - switches the whole session; native rewrites activeId only, so settings and libraries never change.
12. **E2 background music** - plays while browsing; native has no BGM source.
13. **E11 Steam account switch UI** - the two-step prompt exists in data, no surface in the shell.
14. **B11 busy indicator** - `page_busy` spinner; native shows an ASCII "Loading..." inside the media overlay only.
15. **B10 loading lockout** - a busy state blocks list actions; native's Left/Right cancels the scan instead.
16. **D1/D2 options lists** - the firmware's own conditional rows; native substitutes disabled Copy/Delete and offers Sort By where the firmware groups differently.
17. **D5 multi-select** - Copy Multiple / Delete Multiple pages; absent.
18. **A38/A39/A40 OSK L1/R1, L2/R2, SELECT** - caret move, caps/accent pages, Input Mode; all absent.
19. **A33 photo control panel** - 20 buttons; Triangle rotates instead.
20. **C9 OSK open/close fade** - 66-96 ms in, ~104 ms out; native is one frame.
21. **E22 controller charge** - the real pad report; native always draws a dash.
22. **E19 epilepsy notice** - shown after ShowGUI; not ported.
23. **E20 gameboot/welcome presets** - scheduled on launch and first setup; never scheduled.
24. **D10 Sort By rows** - six date pairs in the RCO; native offers two.
25. **E9 launch/quit** - gameboot preset and an XMB return; native launches RPCS3/Steam and shows a page message.

Also on the P1 list but outside the top 25: **B4** the receding bar, **D12** Steam title rows, **D15**
download states, **E12** avatars, **E13** download management, **A45** in-game XMB, **C22/C23** volume and
screensaver timing.

---

## 9. Files and reproduction notes

- Every `absent` claim is a grep over `src/*.cpp` and `src/*.h` at `cb902fa` for the handler or feature
  keyword (e.g. `bgm`, `screenshot`, `safeArea`, `noteUserActivity`, `setProfile`, `SELECT`, `l1`/`caret`).
- Behaviour claims that are also visual states are gated elsewhere; this document does not re-score any
  layer. Where a row cites a frame or burst it is the audit's, not a run made for this pass: no build, no
  capture and no gate was run here.
- Timing constants quoted from code: `view.h:48-49` (200/300 ms), `view.cpp:2986` (repeat), `view.cpp:2162`
  (PS tap), `ps_watcher.cpp:10` (hold 1000 ms), `osk_layout.h:119` (caret 468), `view.h:345-348` (dialogs),
  `view.cpp:869` (screensaver dim), `media_controls.cpp` (3000 + 250 ms).
- No source file, no harness, no reference, no other audit and no budget was touched by this pass.
