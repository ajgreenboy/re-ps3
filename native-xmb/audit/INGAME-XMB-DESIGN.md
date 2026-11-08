# In-game XMB (plan item F5): firmware evidence, Windows feasibility, design

Status: research and design only, 2026-11-08. No source was changed for this document. Labels used
throughout:

- **TRACED** - read directly from firmware files (dev_flash 4.93 XMBML/RCO, decrypted `fw493` PRX strings),
  the RPCS3 source tree, this repository's source, or the local Windows SDK headers.
- **INFERRED** - follows from traced evidence but was not observed running (no in-game capture exists).
- **EXTERNAL** - documented Windows/vendor behaviour that was not reproduced on this machine.

Local dumps produced for this document (not committed): `C:\dev\out\f5\` -
`xmb_ingame.txt`, `xmb_plugin_normal.txt`, `game_indicator_plugin.txt` (`--rco-tree`),
`*-text.txt` (`--rco-text` for xmb_ingame, impose_plugin, system_plugin, game_plugin, explore_plugin_full,
explore_plugin_game, explore_category_*), `all-images.txt` (`--rco-images`). Command form:

```powershell
C:\dev\xmb\out\native-dim\re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-tree "xmb_ingame@C:/dev/out/f5/xmb_ingame.txt"
C:\dev\xmb\out\native-dim\re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-text "explore_category_game@C:/dev/out/f5/ecg-text.txt"
```

---

## 1. The firmware's in-game XMB

### 1.1 What it is

| # | Finding | Evidence | Label |
|---|---|---|---|
| F1 | The in-game XMB is a separate VSH module, `xmb_ingame.sprx`, with its own resource `xmb_ingame.rco`. | `fw493/vsh_module/xmb_ingame.prx` (141,728 B); `dev_flash/vsh/resource/xmb_ingame.rco` | TRACED |
| F2 | It drives the *same* explore plugin and category modules as the normal XMB rather than its own menu code: the PRX names `/dev_flash/vsh/module/explore_plugin.sprx`, all ten `explore_category_{user,sysconf,photo,music,video,tv,game,network,psn,friend}.sprx`, their RCOs, `explore_plugin_full.rco`, `game_plugin.rco`, plus `np_trophy_ingame.sprx/.rco`, `impose_plugin`, `system_plugin`. | printable strings of `xmb_ingame.prx` | TRACED |
| F3 | Its object tree is identical in shape to `xmb_plugin_normal.rco`: `page_xmb_bg`, `page_xmb` -> `XMenu xmenu` (posX -395, posY 40, `menus=10`), `page_xmb_indicator` (info bar `indi_base` 560x82, clock, message/chat/online/avatar/plus planes), `page_xmb_fg`. 26 objects each. | `C:\dev\out\f5\xmb_ingame.txt` vs `xmb_plugin_normal.txt` | TRACED |
| F4 | Difference 1 - the ten `XMList`s carry their own icon and label in the in-game RCO (`list_user image=tex_user text msg_user="Users"` ... `list_friend tex_friend "Friends"`), where the normal RCO's lists are `image=null`. The ten 128x128 category icons live in `xmb_ingame.rco` (already used as a `bestImage` source, `audit/ICON-GAP-AUDIT.md` C35). | same dumps; `all-images.txt` | TRACED |
| F5 | **All ten columns exist in-game.** There is no reduced column list at the RCO level; the reduction is per row (F6-F9). The common belief that the in-game XMB "has fewer columns" is not supported by the resource. Whether some columns are hidden at run time by code is not traced. | F3, F4 (`menus=10`, ten XMLists with labels) | TRACED (columns present); run-time hiding INFERRED absent |
| F6 | Every media/content column carries an in-game-only row `seg_gameexit` whose source is the `ingame` selector: `sel://localhost/ingame?path=category_<c>.xml#seg_gameexit&type=<c>`. Present in Game, Music, Video, Photo, Network, PSN and TV; absent from Users, Settings and Friends. | `grep sel://localhost/ingame vsh/resource/explore/xmb/*.xml`; root query order dumped per file | TRACED |
| F7 | The row resolves to `<View id="seg_gameexit"><Item class="type:x-xmb/xmlgameexit" key="gameexit"/></View>` (category_game.xml:391). Its label is `explore_category_game:msg_game_quit2 = "Quit Game"` and its icon is `tex_quit`, which exists in exactly the seven category RCOs that carry the row and in none of sysconf/user/friend. | `ecg-text.txt`; `all-images.txt` | TRACED (strings/texture); exact binding of label to row INFERRED |
| F8 | Row position: Game column - after the save-data/memory-card utilities and `seg_welcome`, before `seg_manual` and the disc/installed titles. Music/Video/Photo - after the scan/playlist utilities, before `gameDir`. Network - after `seg_dlctrl`, last but one. PSN - after `seg_trophy`. TV - after `seg_welcome`. | root query order of each `category_*.xml` | TRACED |
| F9 | A second in-game-only Game row, `seg_manual` (`sel://localhost/ingame?...#seg_manual&type=manual`, `msg_soft_manual`, `game_plugin:manual` module action), opens the running title's manual. Rows can also be disabled in-game by attribute: `<Pair key="ingame"><String>disable</String></Pair>` on Install Package Files (category_game_tool2.xml:375). | category_game.xml:397-409; category_game_tool2.xml:375 | TRACED |
| F10 | Actions that need the game gone are refused with a quit offer, in every category RCO: `msg_need_quit_game_to_operate_ask = "To perform this operation, you must quit the game. Do you want to quit the game now?"` and `msg_need_quit_to_operate_ask = "...you must quit [%1]..."`. | `ec-*-text.txt`, `explore_plugin_full-text.txt` | TRACED (strings); the set of operations that trigger it INFERRED (launching another title, video playback, browser, store) |
| F11 | The in-game help line is `xmb_ingame:msg_help_back_to_game = ": Back to Game Screen"` (Circle), next to the ordinary `msg_help_back = ": Back"`. | `xmb_ingame-text.txt`; PRX strings | TRACED |
| F12 | The PS-hold black menu belongs to `impose_plugin`: `msg_game_quit = "Quit Game"`, `msg_game_quit_ask = "Do you want to quit the game?"`, `msg_power_off_console`, `msg_power_off_controller`, controller settings/reassign, battery level. | `impose_plugin-text.txt` | TRACED (strings); the confirm step after Quit Game INFERRED from `msg_game_quit_ask` |
| F13 | Notifications appear over the game: `system_plugin` owns the notification textures, including `tex_notification_psbutton_insensitive` (the PS button is disabled by the title), which `xmb_ingame.prx` references by name. | `all-images.txt`; PRX strings | TRACED (assets/reference); the trigger INFERRED |
| F14 | The game keeps running and is *told* the menu is open: on PS, the system sends `CELL_SYSUTIL_DRAWING_BEGIN` (0x0121), `CELL_SYSUTIL_SYSTEM_MENU_OPEN` (0x0131) and `BGMPLAYBACK_PLAY`; on close `..._STOP`, `SYSTEM_MENU_CLOSE` (0x0132), `DRAWING_END`. Quit Game sends `CELL_SYSUTIL_REQUEST_EXITGAME` (0x0101) and lets the title exit itself. | RPCS3 `Emu/Cell/Modules/cellSysutil.h:138-142`, `cellSysutil.cpp:324-357` (`send_open_home_menu_cmds`), `Emu/System.cpp:3341` | TRACED in RPCS3's model of the firmware; real-console behaviour INFERRED to match |
| F15 | "Dimmed game behind": `page_xmb_bg` is present but empty in the in-game RCO, so nothing in the resource replaces the game frame with the wave. The darkening and its factor are not in the RCO. | F3 | Darkened live game frame INFERRED; factor unknown (no capture - `audit/BEHAVIOUR-GAP-AUDIT.md` row "In-game XMB over a running title") |
| F16 | `xmb_ingame.prx` also contains `first_ingame`, `FocusFriend`, `FocusMessageBox`, `Bar::onScrollIn/Out` and a handful of title IDs (`NPEB00468`, `NPHA80134`, `NPIA09008`, `NPJA00060`, `NPUP00052`, `NPXS03027`, `NPXS03030`). Their purpose is not traced. | PRX strings | TRACED (presence only) |

### 1.2 Summary of how it differs from the home XMB

1. Same ten columns, same layout constants; rows filtered by the `ingame` selector (adds Quit Game rows,
   Game-column manual) and by `ingame=disable` attributes.
2. Quit Game appears as a row in seven columns and in the PS-hold menu; both end with a confirmation and a
   cooperative exit request to the title.
3. Background: the running game, not the wave/particles (INFERRED darkened). Circle reads "Back to Game Screen".
4. Operations that need the game's resources ask to quit it first.
5. Notifications and the info bar draw over the game.

### 1.3 What the native build has today

| Area | State | Source |
|---|---|---|
| PS detection while a game owns the foreground | Done: 20 ms focus-independent poll of the shared HID slot (DualShock 3 USB, DS4/DualSense USB+BT) plus the XInput Guide bit via `XInputGetStateEx` ordinal 100, merged as the PS bit; console tap/hold state machine. | `src/ps_watcher.*`, `src/pad.h:56-58`, `audit/PS-INGAME-HOOK.md` |
| PS tap while a title runs | Brings the normal XMB window forward (restore, fullscreen, `AttachThreadInput` + `SetForegroundWindow`) and flashes the indicator. Nothing marks the XMB as in-game. | `View::psTappedEvent`, `View::bringXmbForward` (`src/view.cpp` ~4021-4075) |
| PS hold while a title runs | Same raise, then the black system menu with Quit Game / Turn Off the System / Turn Off the Controller. Quit Game has **no confirmation**. | `Shell::psHeld`, `psMenuItems`, `psMenuActivate` (`src/shell.cpp` ~2236-2300) |
| Quit path | Tracked `QProcess`: `terminate()` (Qt posts `WM_CLOSE` to the process's top-level windows), 3 s, then `kill()`. Steam: `steam.exe +app_stop <id>`, registry watcher reports the return. Message loses the title (known bug, PS-INGAME-HOOK "What is needed" 3). | `src/shell.cpp` `psMenuActivate`; `src/steam_library.cpp:669` |
| In-game rows | Deliberately skipped and logged: `row.inGameOnly` (`sel://localhost/ingame`) in the Network builder and the `*:seg_gameexit` entries in the media `skipped` table ("an in-game XMB row"). | `src/shell.cpp:343-345, 441, 452, 461`; `src/xmbml_view.cpp:33,175,204,296` (`RowKind::InGameExit` already classified) |
| Render while a title runs | `TitleSession` stops the frame loop while the XMB is not foreground and restores window/selection on return. | `src/ps_watcher.h` `TitleSession`, `renderPaused()` |
| Launch paths | RPCS3: `rpcs3.exe --no-gui <path>` tracked. Imported apps/emulators: tracked QProcess. Steam: `steam.exe -applaunch <id>` (not our process). | `src/shell.cpp:542-645, 2142` |
| Notifications | `Shell::notify` composite exists but is drawn by the XMB window, so it is invisible while the game is in front. | `audit/NOTIFICATIONS.md` |

Two conflicts found that the current code does not handle (INFERRED - not exercised, no game was run):

- **RPCS3 also reacts to PS.** RPCS3's pad thread opens its own Home Menu overlay on a PS press for any running
  title (`rpcs3/Input/pad_thread.cpp:660-696`), so with a mapped PS button a tap over an RPCS3 title opens
  RPCS3's menu *and* raises the native XMB. Local `bin/config/input_configs/global/Default.yml` here is a
  keyboard profile (`PS Button: Backspace`); the HTPC's mapping is unknown.
- **RPCS3's close is confirmed by default.** `gs_frame::event(QEvent::Close)` shows "Exit Game?" when the GUI
  setting `confirmationBoxExitGame` is true (default `true`, `rpcs3/rpcs3qt/gui_settings.h:169`; close handling `rpcs3qt/gs_frame.cpp:1206-1252`), and leaves fullscreen
  first. Without that box a close runs `Emu.GracefulShutdown`, which sends `CELL_SYSUTIL_REQUEST_EXITGAME` -
  the firmware's own quit protocol. With the box, the current 3 s `terminate()` window expires and
  `kill()` hard-stops the emulator, skipping the title's own save-on-exit. Whether `--no-gui` honours the
  setting is not verified.
- **Steam owns the Guide/PS button for Steam titles** when Steam Input is enabled for the controller ("Guide
  button focuses Steam" opens Steam's overlay). EXTERNAL; a tap would then raise both overlays.

---

## 2. Feasibility on Windows

### 2.1 Building blocks (what Windows actually allows)

| Mechanism | What it gives | Limits / risks | Label |
|---|---|---|---|
| **Foreground swap** (today) | XMB window becomes foreground and covers the game. Works for any game type. | Exclusive-fullscreen (FSE) games lose exclusive mode on focus loss: a display-mode change, black flashes, some titles minimise or crash, a few refuse focus (`audit/PS-INGAME-HOOK.md` "Limits"). No game visible behind. | TRACED (current code), FSE behaviour EXTERNAL |
| **Topmost window over a borderless/windowed game** (`WS_EX_TOPMOST`, Qt `WindowStaysOnTopHint`) | DWM composes the overlay over a flip-model or GDI game window; the game keeps rendering live. | Many borderless games are themselves `HWND_TOPMOST`; z-order among topmost windows follows the last raise/activation, so the overlay must be raised after the PS press. Independent flip / MPO is dropped while anything overlaps - a small latency cost only while the overlay is up. Normal-band windows cannot sit above system bands (Start, Game Bar); irrelevant here. | EXTERNAL |
| **Translucent overlay** (per-pixel alpha: `WS_EX_LAYERED`/`UpdateLayeredWindow`, or a DirectComposition premultiplied-alpha swap chain with `WS_EX_NOREDIRECTIONBITMAP`, `winuser.h:2886`) | Live game visible through a dimmed XMB - the firmware look. | Qt: `WA_TranslucentBackground` must be set before the native window exists, so it means a second top-level window and a second GL context; translucent GL top-levels on Windows are a known performance and correctness risk; must be measured, not assumed. | SDK TRACED; Qt behaviour EXTERNAL |
| **Exclusive fullscreen detection** | `SHQueryUserNotificationState` returns `QUNS_RUNNING_D3D_FULL_SCREEN` (3) for an exclusive-mode D3D app and `QUNS_BUSY` (2) for a full-screen app. Lets the XMB pick a mode before touching focus. | Heuristic; Vulkan/OpenGL FSE and Fullscreen Optimizations (FSO, which presents most legacy "exclusive" DX titles through the compositor on Windows 10/11) make it imperfect. Combine with window-rect == monitor-rect and style checks. | SDK TRACED (`shellapi.h:989-998`); accuracy EXTERNAL |
| **One-frame window capture** (Windows.Graphics.Capture, `GraphicsCaptureItem` for an HWND; `IsBorderRequired=false` on Windows 11) | A still of the game's last frame, taken through DWM without touching the process - usable as the dimmed backdrop even after focus moves or the game minimises. | True FSE bypasses DWM: expect a black or stale frame. Titles that set `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` (`winuser.h:4783`) capture black by design. Needs C++/WinRT + D3D11 (headers present: `Include/10.0.26100.0/cppwinrt/winrt/Windows.Graphics.Capture.h`). Not injection; same API OBS's window capture uses. | SDK TRACED; behaviour EXTERNAL |
| **Shared controller input** | HID opened with `FILE_SHARE_READ|FILE_SHARE_WRITE` (`src/pad.cpp:236`) and XInput are both shared: the XMB reads the pad without focus. | The game reads the same presses. Games that honour focus loss stop (SDL's default ignores background joystick events; XInput guidance is `XInputEnable(FALSE)` on `WM_ACTIVATEAPP`), others keep moving the character. Steam Input switches to its *Desktop layout* when no game is focused, which may turn the pad into mouse/keyboard while the XMB is up. DS4Windows/HidHide or exclusive opens hide the pad from the XMB entirely. | Code TRACED; game/Steam behaviour EXTERNAL |
| **Returning focus** | Record the game HWND at PS press; on Circle/PS tap call the same `AttachThreadInput` + `SetForegroundWindow` path for that HWND. | FSE titles switch display mode back (seconds of black); a title that minimised on focus loss needs `ShowWindow(SW_RESTORE)`; the foreground lock can still refuse - log and leave Alt-Tab as the fallback. | Existing code TRACED; outcomes EXTERNAL |
| **Pausing the game: `NtSuspendProcess`/`DebugActiveProcess`** | Freezes the title like a hard pause. | Undocumented (not in `winternl.h` of the local SDK). Hung window -> DWM "Not Responding" ghost after ~5 s replaces the game frame; audio endpoints underrun or loop; network sessions time out; launcher/game process trees (Steam, EA, Ubisoft) make "which PID" ambiguous; anti-cheat drivers treat external suspension as tampering; protected processes refuse. **Rejected.** | EXTERNAL |
| **Swap-chain hooking / DLL injection** (how Steam's overlay `GameOverlayRenderer64.dll`, RTSS, Discord's legacy overlay and ReShade draw *inside* FSE games) | The only way to draw over a true exclusive-fullscreen frame. | Writes code into another process: anti-cheat (EAC, BattlEye, Vanguard, Ricochet) can flag or ban unknown modules; each graphics API needs its own hook; crashes land in the game. Steam and Microsoft are allow-listed by anti-cheat vendors, this app is not. **Out of scope for safety and anti-cheat reasons.** Xbox Game Bar mostly avoids injection by being a privileged band window and relying on FSO, which a normal app cannot replicate. | EXTERNAL |
| **Emulator-native menu** | RPCS3 already renders a Home Menu inside its own swap chain and sends the firmware's `SYSTEM_MENU_OPEN/CLOSE` to the title, optionally pausing (`Pause Emulation During Home Menu`, `system_config.h:395`; local config `false`). | Not the XMB's look; no IPC to open it from outside besides the PS button and the gs_frame shortcut. Other emulators differ (some expose a command interface); none checked here. | TRACED (RPCS3 source) |

### 2.2 Matrix: approach x game type

Rows are approaches; cells say what the user sees and the main risk. "Safe" means no code enters the game process.

| Approach | Borderless / windowed (flip or GDI) | Exclusive fullscreen (true FSE, FSO off) | RPCS3 `--no-gui` | Steam title (Steam Input on) | Anti-cheat title |
|---|---|---|---|---|---|
| A. Foreground swap, opaque XMB (today) | Works; game hidden; input leak only if the game reads background input. | Works with a mode switch out and back; some titles minimise/crash/refuse. | Works; RPCS3 Home Menu may open too (PS conflict). | Works; Steam overlay may open too; Desktop layout may drive the mouse. | Safe. |
| B. A + captured dimmed backdrop (WGC still) | Firmware-like look with a frozen frame; capture reliable. | Backdrop usually black/stale -> fall back to plain dark. | Capture of the gs_frame works (RPCS3 presents through DWM unless set exclusive). | Works. | Safe; protected titles give a black frame. |
| C. Live translucent topmost overlay, game keeps foreground | Most faithful look, no focus change; **game receives every press** while the XMB is navigated. | Invisible (DWM bypassed) or forces FSE exit. | Live view, but RPCS3 consumes the same presses. | Steam routes presses to the game. | Safe but input leak makes it unusable as a primary mode. |
| C'. Live translucent overlay that *activates* | Live game behind; well-behaved games stop reading input on deactivation (the PC analogue of `SYSTEM_MENU_OPEN`); per-title variance. | Same as A (mode switch, nothing to show through). | As C, plus RPCS3 keeps emulating unless its own pause is set. | Steam Desktop layout risk. | Safe. |
| D. Suspend the process | Frozen frame then "Not Responding" ghost; audio glitches. | Display stays in a dead exclusive mode. | Emulator threads frozen mid-frame; risky for savedata writes. | Wrong PID likely (launchers). | Tampering signal. **No.** |
| E. Hook/inject | Full fidelity. | Full fidelity. | Full fidelity. | Conflicts with Steam's own hook. | Ban risk. **Out of scope.** |
| F. Delegate to the emulator's own menu | n/a | n/a | RPCS3 Home Menu with real `SYSTEM_MENU_OPEN`; not XMB-styled. | n/a | n/a |

---

## 3. Recommendation

**A safe, no-injection in-game XMB built from B (default) plus C' (opt-in for borderless titles), with
firmware rows and semantics on top.** On a PS tap over a running title the XMB records the game's window,
takes a one-frame capture of it through Windows.Graphics.Capture, then comes forward in an *in-game mode*:
no wave or particles, the captured frame darkened behind the ten columns, Quit Game rows from the firmware's
`seg_gameexit` in the seven columns that carry it, the Game-column manual row honestly unavailable,
`ingame=disable` rows hidden, operations that need the game gone answered with `msg_need_quit_game_to_operate_ask`,
and Circle reading "Back to Game Screen", which hands the foreground straight back to the recorded window.

What it **can** do honestly:

- Look like the firmware's in-game XMB over borderless and windowed games (frozen, not live, by default).
- Quit with the firmware's confirmation, preferring a cooperative close (WM_CLOSE -> wait -> terminate; for
  RPCS3 that is `GracefulShutdown` -> `REQUEST_EXITGAME` when the confirm box is off).
- Show in-game notifications over borderless/windowed games through a separate non-activating, click-through
  topmost window, and hold them while an exclusive-fullscreen game is detected.
- Work for every launch path, including Steam titles, because it never needs to own the game process.

What it **cannot** do, and the document should say so in the UI where relevant:

- Draw over a true exclusive-fullscreen frame. FSE titles get the mode switch that Alt-Tab causes, and a dark
  backdrop instead of the game.
- Pause an arbitrary PC game. Activation tells the game it lost focus; whether it pauses or stops reading the
  pad is the game's choice. No suspension, no `SYSTEM_MENU_OPEN` equivalent exists for native PC titles.
- Stop the game from seeing the PS/Guide press. The pad is shared.
- Show a live game behind the XMB by default. The live translucent mode (C') is opt-in, borderless-only, and
  only after its frame-time cost is measured.
- Coexist silently with RPCS3's Home Menu and Steam's Guide handling: those need an owner choice (step 8).

---

## 4. Implementation steps

Each step names its files, a verification with the stub game, and what needs the owner's real games on the
HTPC. Steps 1-3 and 7 are independent of each other; 4 precedes 5; 5 precedes 6 and 9; 8 is configuration
only. Build with `build.ps1 -OutputDirectory C:\dev\out\f5-build`; the H5 harness
(`C:\dev\out\h5-test\harness.ts`, run with `bun`) already measures foreground hand-over with the stub.

Stub game: `C:\dev\out\h5-test\stubgame.exe <mode> <seconds> <log>` is a GDI window that claims the foreground
and logs `foreground=0/1` once a second; `wait` mode stays up until closed. Register it as an imported app the
same way `audit/PS-INGAME-HOOK.md` registered `ps-standin.exe`. Steps 4, 6 and 7 also need a small D3D11
variant, `C:\dev\out\f5\stubgame-d3d.cpp` (flip-model swap chain, modes `windowed`, `borderless`,
`fse` via `SetFullscreenState(TRUE)`, a moving colour bar so captures are checkable, the same log format),
built with `cl` from the vcvars environment `build.ps1` already uses.

### Step 1 - Public in-game state in Shell (independent)
- **Files:** `src/shell.h`, `src/shell.cpp`, `src/view.cpp`.
- **Change:** public `bool titleRunning() const`, `QString runningTitle() const`, `bool inGame() const`
  (title running and the XMB was raised over it by PS), `void setInGame(bool)`; replace the view's
  `psMenuItems().size()>2` inference. Capture the title before terminating so the quit message keeps it.
- **Verify (stub):** `--script` with `DOWN,ENTER` on the stub row, `ps`, `grab=...png` + state sidecar shows
  `titleRunning=1 inGame=1`; `pshold`, `ENTER` shows "<title>: The game was closed."
- **Owner:** none.

### Step 2 - In-game rows from XMBML (independent)
- **Files:** `src/xmbml_view.h/.cpp` (parse the `ingame` attribute pair alongside `inGameOnly`),
  `src/shell.cpp` (stop skipping `inGameOnly`/`*:seg_gameexit` when `inGame()`; build the row from
  `explore_category_<c>:msg_game_quit2` + `tex_quit` of the same container; place it at the XMBML position from
  section 1 F8; Game `seg_manual` -> `Action::Unavailable` with the reason "no manual source for PC titles";
  hide `ingame=disable` rows; route launch/play actions that would start other content to a
  `msg_need_quit_game_to_operate_ask` confirmation).
- **Verify (stub):** with the stub running, `ps`, then `grab=` per column: Quit Game present in Game/Music/
  Video/Photo/Network/PSN/TV, absent in Users/Settings/Friends; `XMB_LOG` shows no "in-game only" skip lines
  while in-game and the old skip lines when no title runs. Selecting another Game row shows the quit-first ask.
  Layer gate (`bun work/ab-layers.ts --capture --exe ...`) unchanged for the home XMB.
- **Owner:** none.

### Step 3 - Quit confirmation and cooperative close (independent)
- **Files:** `src/shell.cpp` (`psMenuActivate`, the new row action), `src/dialogs.cpp` if the Yes/No page is
  reused.
- **Change:** Quit Game (menu or row) -> `impose_plugin:msg_game_quit_ask` Yes/No. Yes -> post `WM_CLOSE` to the
  title's top-level windows, wait up to a configurable 10 s (savedata writes), then `terminate()`/`kill()`;
  Steam path unchanged (`+app_stop`). Log which stage ended the process.
- **Verify (stub):** `stubgame.exe wait` logs `WM_CLOSE received` / `window destroyed` / `exited 0`; a "No"
  leaves `stubgame` alive (`tasklist` check in the harness).
- **Owner:** RPCS3 title: confirm the close is graceful (RPCS3 log "Requesting game to exit...", no forced
  termination) and decide on `confirmationBoxExitGame`; one Steam title to exercise `+app_stop`.

### Step 4 - Game window identity and one-frame capture
- **Files:** new `src/game_capture.h/.cpp` (C++/WinRT, D3D11; `CMakeLists.txt` links `windowsapp`/`d3d11`
  under `if(MSVC)`).
- **Change:** at PS press, record `GetForegroundWindow()` when it is not the XMB and belongs to the tracked
  process tree, or to any process while a Steam title runs; classify it (`SHQueryUserNotificationState`,
  window rect vs monitor, styles) as windowed / borderless / exclusive; capture one frame to a `QImage` with
  `IsBorderRequired=false` where supported; empty image on failure. Log `ingame: window pid=.. mode=.. capture=ok|black|failed`.
- **Verify (stub-d3d):** `windowed` and `borderless` give a non-black capture whose colour bar matches the stub's
  logged frame colour; `fse` logs `mode=exclusive` and a black/failed capture, and the code path does not throw.
- **Owner:** the HTPC's real borderless and FSE titles: record mode detection and capture outcome per title.

### Step 5 - In-game render mode
- **Files:** `src/view.h/.cpp` (paint path), `src/assets.cpp` (`xmb_ingame` icons/strings already loadable).
- **Change:** when `inGame()`: skip wave/particles; draw the captured frame scaled to the window and darkened
  (start at 50 % and mark the factor as unmeasured - no firmware capture exists); plain dark fill when the
  capture is empty; category icons/labels from `xmb_ingame`; help line `msg_help_back_to_game`; Circle at
  column root returns to the game (step 6).
- **Verify (stub-d3d borderless):** `grab=` frames show the stub's bar under the darkened XMB; the home-XMB
  layer gate stays 6/6.
- **Owner:** visual check on the TV; optionally an RPCS3 reference capture of a real in-game XMB would replace
  the 50 % placeholder (needs a game in the local RPCS3).

### Step 6 - Return to the game
- **Files:** `src/view.cpp` (`bringXmbForward` counterpart `returnToGame`), `src/ps_watcher.cpp`
  (`TitleSession` must treat "returned by Circle" like a normal pause, not a title end).
- **Change:** Circle at root or PS tap while in-game: clear `inGame`, restore the recorded HWND
  (`SW_RESTORE` if minimised), `AttachThreadInput` + `SetForegroundWindow`, retry once after 400 ms, then let
  `TitleSession` pause rendering. Log the outcome.
- **Verify (stub):** the stub's own log flips `foreground=0` -> `foreground=1` within 1 s after the Circle
  token; harness measures hand-over time; render pause log resumes.
- **Owner:** one FSE title (mode switch back, time to picture), one borderless title, one title that minimises
  on focus loss.

### Step 7 - Notifications over the game (independent)
- **Files:** new `src/notify_overlay.h/.cpp`; `src/shell.cpp` (`notify` forwards when a title is foreground).
- **Change:** a frameless `WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_LAYERED`
  window drawing only the existing notification composite; never activates, click-through; while
  `QUNS_RUNNING_D3D_FULL_SCREEN` holds, queue and deliver after return instead.
- **Verify (stub + stub-d3d):** a scripted `notify` while the stub is foreground: stub log stays
  `foreground=1`, `grab` of the desktop shows the plaque; with `fse` the notification is queued and shown after
  return.
- **Owner:** borderless titles that are themselves topmost; confirm the plaque sits above them.

### Step 8 - Controller arbitration per launcher (configuration, owner decisions)
- **Files:** `src/preferences.*`, `src/settings_menu.cpp` (one setting), `src/view.cpp` (policy check).
- **Change:** a per-launcher policy: for RPCS3 titles either "XMB owns PS" (owner unmaps PS in RPCS3's pad
  profile, as on a console the title never receives PS) or "RPCS3 Home Menu owns PS" (the XMB reacts only to
  hold); for Steam titles, document turning off "Guide button focuses Steam" and the Desktop layout, or let the
  XMB react only to hold.
- **Verify (stub):** policy toggles change the `ps:` log decision lines for a stub registered as RPCS3/Steam kind.
- **Owner:** the HTPC's RPCS3 pad profile and Steam controller settings; one RPCS3 and one Steam title.

### Step 9 - Optional live translucent overlay for borderless titles (after 5-6)
- **Files:** new top-level overlay window in `src/view.*` or a sibling class; env flag `XMB_INGAME_LIVE=1`.
- **Change:** C' from section 2: translucent topmost window that activates, live game behind, only when step 4
  classified the game borderless/windowed.
- **Verify (stub-d3d borderless):** the bar keeps moving in two `grab`s 1 s apart; XMB frame time and stub
  frame time logged with and without the overlay; drop the mode if either regresses noticeably.
- **Owner:** frame pacing on the HTPC GPU with one demanding borderless title.

### Step 10 - Owner validation matrix on the HTPC
Run once after steps 1-7: one borderless, one FSE, one RPCS3, one Steam-launched and (if installed) one
anti-cheat title. Per title record: PS tap/hold detected, mode detected, backdrop result, input leak into the
game while navigating, return time, Quit Game outcome (cooperative or forced), notification visibility.
Nothing in this design injects into, hooks, or suspends a game process, so the anti-cheat title is a
behaviour check, not a safety risk.
