# Desktop Mode, single instance and controller-driven desktop (PHASES-HTPC D1, D2, D5)

Status 2026-11-09: implemented and verified windowed and with `--kiosk` (verification below). Not verified: the tray icon
click and menu (no UI automation was run against the notification area), a real Guide press on a physical pad, and the
hotkey and Guide paths on the HTPC itself.

Source: `src/desktop_mode.{h,cpp}` (both halves), `src/shell.cpp` (rows, confirmations, `setDesktopMode`),
`src/ps_watcher.{h,cpp}` (`bringWindowForward`, `setDesktopRenderPause`, `setPollInterval`), `src/view.cpp`
(`setSuspended`), `src/main.cpp` (options, wiring, script tokens), `src/preferences.cpp` and `src/settings_menu.cpp`
(the two preferences). Tests: `work/desktop-mode-test.ts`.

## D2 Single instance

One XMB per user **per data directory**. The local server name is
`re-ps3-native-xmb-<USERNAME>-<first 10 hex of sha1(data dir)>`, a named pipe on Windows opened for the owning user only
(`QLocalServer::UserAccessOption`). The data directory is part of the identity on purpose: two XMBs on one data directory
would fight over its stores, while a scratch directory (a test, a second profile) is a separate instance.

| Launch | Result |
|---|---|
| plain, or `--show` | running XMB: leaves Desktop Mode if hidden, else comes to the front; this process exits 0. None running: starts normally. |
| `--desktop` | running XMB enters Desktop Mode, exit 0. None running: starts the XMB and enters Desktop Mode right away. |
| `--quit` | running XMB exits with code 0 (so the kiosk watchdog stops), exit 0. None running: does nothing, exit 0. |

- The second process calls `AllowSetForegroundWindow(ASFW_ANY)` before sending, so the running XMB may take the foreground.
- The reply is `ok` or `bad` on receipt. A command that arrives while the XMB is still starting is queued and applied once the
  window exists. A running XMB that does not answer within 10 s makes the second launch exit 1 with a message.
- **Who takes part.** Only real interactive or kiosk runs: the launch must use none of the options outside an allow-list
  (`dev-flash dev-hdd0 vsh-modules data-dir no-intro no-health-warning kiosk display start-windowed steam-root gallery-root
  gallery-sort show desktop quit`), no positional arguments, and no `XMB_INPUT_SCRIPT_ONLY=1`. Every report mode, `--script`, the
  clock and render pins, the reference tools and `--measure-reference` are therefore outside the guard, so tests and the gate
  never meet a running XMB. `--show`, `--desktop` or `--quit` combined with any other option is an error (exit 2).
- **Opt-out:** `XMB_SINGLE_INSTANCE=0` (no server, no handover; the three commands are refused with exit 2).
- Windows removes a named pipe with its process, so a crash leaves nothing stale. A listen failure after a lost start-up race
  tries the running instance once more, otherwise the XMB continues without the guard and logs why.
- Packaging: `watchdog.ps1` and `run-kiosk.ps1` are unchanged. A manual `run-kiosk.ps1` while the XMB runs hands over and
  exits 0, which a watchdog that launched that second copy reads as a clean exit and stops; the first XMB's own watchdog
  keeps running. Autostart plus a manual launch never yields two XMBs.

## D1 Desktop Mode

### Rows (INFERRED)

Users column power group, after Turn Off System, Restart System and Sleep: **Switch to Desktop** and **Exit to Desktop**
(`Action::SwitchToDesktop`, `Action::ExitToDesktop`, the same `showConfirm` pattern as Restart and Sleep: "Do you want to
switch to the desktop?" and "Do you want to exit the XMB to the desktop?"). The firmware has no such rows. The PS-hold system
menu is left exactly as the firmware's (Turn Off the System, Turn Off the Controller, Quit Game): the view draws it from a
fixed two-row shape with a measured envelope, and several places count its rows to detect a running title. The new rows live
where the existing host power rows (Restart, Sleep) live.

### Switch to Desktop

`DesktopMode::enter(source)`:

1. closes the PS menu if open and records fullscreen, maximised or normal geometry, the screen and the selection;
2. media: a photo slideshow and any video pause; music keeps playing unless `desktop/keepMusic` is off (then it pauses);
3. `Shell::setDesktopMode(true)` (the idle Auto-Off will not blank a display the user is using, and the Auto-Off guide timer
   skips), `setDesktopRenderPause(true)` (`renderPaused()` is true, the frameSwapped re-arm stops), and
   `View::setSuspended(true)` (the 16 ms controller timer slows to 250 ms);
4. shows the tray icon (drawn at run time with QPainter: no file and no firmware art), registers the hotkey, then
   `window.hide()` (the window is withdrawn, not minimised);
5. the PS/Guide watcher keeps running at 100 ms instead of 20 ms; the HID and XInput reader threads are untouched.

An idle line is logged every 2 s: `desktop: idle frames=N in last 2000 ms` (frames swapped by the view while hidden).

### Ways back

All end in `DesktopMode::leave(source)`: unregister the hotkey, hide the tray icon, restore the window (kiosk: frameless
fullscreen on the recorded screen, geometry set to that screen first; windowed: fullscreen, maximised or the saved normal
rectangle), un-pause rendering, restore the poll rates, `bringWindowForward()` (the Win32 recipe `TitleSession` uses: raise,
activate, attach to the foreground thread's input queue for one `SetForegroundWindow`), then re-assert at about 150, 500 and
1500 ms until the XMB is foreground and the view has focus. The selection is untouched (it lives in Shell) and its
before/after equality is logged (`kept=1`).

| Way | Notes |
|---|---|
| Guide/PS button, any XInput pad or HID pad | `PsWatcher::pressed`; ignored for 600 ms after entering (the tail of the press that chose the row). |
| tray icon (left click) or its menu "Return to XMB" | the menu also has "Exit XMB". Not verified by automation. |
| global hotkey, default **Ctrl+Alt+Home** | `RegisterHotKey` on the GUI thread, **only while hidden** (it never takes the combination while the XMB is up) and unregistered on return; Windows drops it when the process ends. Setting: Settings > Power Saving > "Desktop Mode - Return Hotkey" (five presets), or any `<Ctrl\|Alt\|Shift\|Win>+...+<Home\|End\|Insert\|Delete\|PageUp\|PageDown\|F1-F24\|A-Z\|0-9>` in `preferences.json` key `desktop/hotkey`. At least one modifier is required. An invalid or already-taken hotkey is logged and the other ways still work. |
| launching the exe again | `--show` or a plain launch (D2). |

### Exit to Desktop

`app.exit(0)`: the process ends with code 0, which `watchdog.ps1` reads as a clean exit and stops. The tray's "Exit XMB" and
a second launch's `--quit` do the same. (Turn Off System is unchanged: shutdown in `--kiosk`, a plain quit otherwise.)

### Preferences (shared keys: they follow the machine, not a user profile)

| Key | Default | Meaning |
|---|---|---|
| `desktop/keepMusic` | `true` | music keeps playing while hidden; video and slideshows always pause |
| `desktop/hotkey` | `Ctrl+Alt+Home` | return hotkey |

Both are native-only rows at the end of the Power Saving page (`--settings-report --settings-page power` lists them).

### Log lines

`desktop: enter via <source> (fullscreen= kiosk= screen= selection= media="...")`, `desktop: hotkey ... registered`,
`desktop: idle frames=...`, `desktop: return via <source> after N ms hidden`, `desktop: window shown ... kept=`,
`desktop: XMB foreground+focus after N ms`, `desktop: exit to desktop ...`, and `instance: ...` for the pipe. Sources:
`Switch to Desktop row`, `second launch`, `second launch --desktop`, `launch --desktop`, `Guide button`, `hotkey`, `tray icon`,
`tray menu`, `script`.

### Script tokens (verification only)

`desktop` enters as the row does; `guide` injects a PS press and release through the watcher's own state machine (the live pad
is withheld in `--script` runs). `--script` runs have no single-instance server, so the pipe path is tested from real second
processes (below).

## Verification, 2026-11-09 (`work/desktop-mode-test.ts`, scratch data under C:\dev\out\d12)

`--mode windowed` and `--mode kiosk` (display 1) start one XMB and drive it from second processes: plain second launch,
`--desktop`, a real Ctrl+Alt+Home through `SendInput`, `--desktop` then `--show`, `--desktop` then `--quit`, and `--quit` with
nothing running. All checks passed in both modes: exit code 0 everywhere, XMB exit code 0 on `--quit`, the kiosk window back
fullscreen on the same display and rectangle, the XMB the foreground pid after each return (60-80 ms), the selection kept, and
no frames swapped while hidden. `--mode script` (and `--mode script --kiosk 1`) exercises the row, confirm, enter and Guide
return path. Also run by hand: the Exit to Desktop row (exit code 0), music across hide and return (position advanced from
5.0 s to 14.4 s over 9.4 s with `music keeps playing`; with `desktop/keepMusic=false` the log says `music paused` and the
player is paused).

Idle CPU (CPU time of the started PID over 10 s windows, one core = 100 %): visible 50-99 %, hidden 0.47-0.94 %. GPU was not
measured separately: a hidden window has no swap activity (0 frames swapped in each 2 s window, apart from a single frame
right after the hide).

## Left

- The tray icon click and menu are verified by construction only; a real Guide press on hardware; entering with a video open
  (the video pauses; not exercised).
- The 0.5-0.9 % hidden CPU is the 250 ms controller tick, the 100 ms watcher, the HID and XInput reader threads and
  Shell's own timers; D12 (Idle cost) can take it further.
- No gate scenario was added for the two new rows.

## D5 Controller-driven desktop

Status 2026-11-09: implemented; the mapper, the SendInput delivery (into a test window of the exe's own), the on-screen
keyboard and the driver's lifecycle are verified (below). Not verified: a real pad (see "Left").

Source: `src/desktop_pad.{h,cpp}` (the pure mapper and `--desktop-pad-test`), `src/desktop_pad_win.cpp` (pad reading, gates,
SendInput, keyboard, the sink and injector used by the tests), `src/desktop_mode.cpp` (starts and stops the driver, tray item),
`src/pad.{h,cpp}` (the XInput state now carries the right stick), `src/preferences.cpp`, `src/settings_menu.cpp`,
`src/shell.{h,cpp}`. Tests: `work/desktop-pad-test.ts`.

### Mapping

Live only between `DesktopMode::enter` and `leave`, with the XMB hidden. The pad is read every 10 ms from the same two sources the
XMB uses (`pad::latest` for PlayStation pads over HID, `pad::xinputLatest` for XInput slots 0-3); buttons are OR'd, and for each
stick the more deflected pad wins. Both read paths already return nothing in a `--script` run.

| Pad | Windows |
|---|---|
| Left stick | pointer (radial deadzone 0.20, curve below) |
| Right stick | scroll: vertical wheel and horizontal wheel (deadzone 0.25 per axis) |
| Cross (A) | left button, down at the press and up at the release, so dragging works |
| Square (X) | right button, same |
| Circle (B) | Escape |
| Triangle (Y) | Enter |
| D-pad | arrow keys; auto-repeat after 450 ms, then every 45 ms |
| RB / LB | window switcher: Alt+Tab / Alt+Shift+Tab (see below) |
| Start | toggle the on-screen keyboard |
| Back / View | Windows key (down at the press, up at the release, so Back + D-pad is Win+arrows and a tap opens Start) |
| Guide / PS | nothing; it returns to the XMB as before |

**Pointer curve.** Past the deadzone `m = (d - 0.20) / 0.80`, speed = `speedMultiplier * (40 m + 1760 m^2.2)` px/s. At speed
Normal: d=0.3 gives 23 px/s, 0.4 gives 93, 0.5 gives 218, 0.6 gives 403, 0.7 gives 651, 0.8 gives 965, 0.9 gives 1347, 1.0 gives
1800. The linear term keeps a barely deflected stick usable for fine placement; the power term makes the far end fast. Sub-pixel
motion is carried between ticks, and one tick moves at most 50 ms worth, so a stalled timer is not a jump. `desktop/pointerSpeed`
1-5 (Slow, Relaxed, Normal, Fast, Very Fast) multiplies the curve by 0.5, 0.75, 1, 1.5, 2.2. The pointer is moved with absolute
`SendInput` coordinates, one pixel target per tick, which bypasses Windows' "Enhance pointer precision": the pointer goes exactly
as far as the mapper asked (checked).

**Scroll.** Wheel units per second = `120 * 12 * m^1.8` (12 notches/s at full deflection). Stick down scrolls down, stick right
scrolls right. Units are batched until 30 are due (a quarter notch) so applications that ignore tiny deltas still scroll.

**Window switcher.** `Alt+Tab` (not `Win+Tab`): it needs no pointer, works the same on Windows 10 and 11 and is what a
keyboard user expects. The first RB press holds Alt and taps Tab; every further RB (Tab) or LB (Shift+Tab) press while Alt is
held steps through the list; Alt is released 1 s after the last LB/RB press, which commits the choice. While the switcher is
open Cross or Triangle commit at once and Circle cancels (Escape, then Alt up); those presses do not also click or press Enter.
The D-pad still sends arrows (they move inside the Alt+Tab list).

**Guide.** While the Guide bit is down nothing else acts and everything the driver holds is released, so a Guide pressed together
with a face button never clicks. A button already held when Desktop Mode starts (the Cross that chose the row) or when a gate
opens is ignored until it is released and pressed again.

**On-screen keyboard (Start).** In order: (1) an `osk.exe` this driver started and that is still running is closed (`WM_CLOSE`,
then `TerminateProcess` on the handle it started, after 1 s); (2) a visible touch keyboard (`IPTip_Main_Window`) is toggled closed
through `ITipInvocation`; (3) otherwise `ITipInvocation::Toggle` is tried (CLSID `4ce576fa-...`), and if no touch keyboard appears
within 800 ms (or the COM class is not registered) `osk.exe` is started through `ShellExecuteEx` (`CreateProcess` fails with
error 740 for it). Only the `osk.exe` process whose handle the driver holds is ever closed; an OSK or touch keyboard the user
opened is never terminated. On the dev machine the touch keyboard class was not registered (0x80040154), so the `osk.exe`
fallback ran: it appeared, was tracked by pid, and was terminated by pid on the second press.

### Gates (the driver injects nothing unless all are open)

| Gate | Why |
|---|---|
| Desktop Mode active | `setActive(true)` at the end of `enter`, `setActive(false)` first thing in `leave`, which releases anything held before the XMB window is shown |
| `desktop/controllerMouse` on | the preference, default on |
| the XMB window is not visible | never inject into the XMB itself |
| `Shell::titleRunning()` false | a tracked title or web app (Steam handoff, RPCS3, Chromium app window) owns the pad |
| no exclusive-fullscreen Direct3D app, no presentation mode (`SHQueryUserNotificationState`) | a game started from the desktop owns the pad; checked every 500 ms |
| no visible window titled "Steam Big Picture..." | Steam's own controller configuration owns the pad there |

The tray tooltip says "controller mouse on / off / paused: reason", and the tray menu has a checkable "Controller mouse and
keyboard" item, so the switch is one click from where Desktop Mode lives. The same switch is Settings > Power Saving > "Desktop
Mode - Controller Mouse" (`desktop/controllerMouse`, default on) and "Desktop Mode - Pointer Speed" (`desktop/pointerSpeed`,
default Normal), listed with the other desktop rows (`--settings-report --settings-page power`). Both keys are shared (machine-wide).
A hold of the PS button does not toggle it (the old phase text suggested that): the existing Guide handling returns on the press,
before a hold could be told from a tap, and changing that would break the tap-to-return behaviour.

### Coexistence with other pad mappers

If another program also maps the pad to the mouse and keyboard, every action happens twice (two pointers' worth of motion, a
double click). Known cases:

- **Steam Input desktop configuration** (Steam > Settings > Controller > Desktop Layout, or Big Picture running): the XMB steps
  aside while a window titled "Steam Big Picture..." is visible. The desktop layout itself is not detectable (it is a Steam
  Input setting with no window or file the XMB can read safely); when `steam.exe` is running the log says so once. If both act,
  turn off "Desktop Mode - Controller Mouse" (or the tray item), or turn off Steam's desktop layout.
- **DS4Windows** (and other XInput emulators such as reWASD, Xbox Accessories remaps): the pad also appears as a virtual Xbox pad
  that the XMB reads, and the program may additionally map it to the mouse. When `DS4Windows.exe` is running the log warns once.
  Use the same switch, or its "Disable mouse/keyboard output" option. Nothing is killed or configured by the XMB.
- **Windows Game Bar / Xbox button**: an Xbox pad's Guide opens the Game Bar on some installs as well as returning the XMB;
  that is Windows' own handler (Settings > Gaming > Xbox Game Bar > "Open Xbox Game Bar using this button on a controller").

### Safety of the tests

`--desktop-pad-sink <log> [seconds]` is a small always-on-top window of the exe's own that logs every mouse and key event it
receives. `--desktop-pad-inject <json-in> <json-out>` runs a scenario through the mapper in real time and calls `SendInput`, but
before every delivery it requires that the foreground window and the window under the pointer both belong to the sink process
(otherwise it sends nothing and exits 3). The pointer is only moved to a start point that is over the sink. Scenarios never use
Win, Alt or Tab (they would leave the sink); the switcher is covered by the pure mapper tests. The injector also counts
`interference`: times the pointer was somewhere other than where it left it (a person or another agent moving the mouse); the
test discards and retries such runs. Only PIDs the tests started are stopped. Residual risk: the ownership check and the
`SendInput` call are not atomic, so a pointer moved by someone else in those microseconds could receive one click.

### Verification, 2026-11-09 (`bun work/desktop-pad-test.ts --exe C:/dev/out/d5-build/re-ps3-native.exe`, scratch data C:\dev\out\d5)

- **map** (`--desktop-pad-test`): the curves (deadzone 0, monotonic, 1800 px/s at full, 23 px/s just past the deadzone, 60 % more
  than 2x the speed of 40 %), pointer distance over 1 s equal to the curve (218 px at 50 %, 1800 px at 100 %, diagonal dx = dy),
  sub-pixel carry, the speed multiplier (2.2x), a stalled clock capped; Cross/Square down at the press and up at the release;
  Circle, Triangle, Back; Start one toggle per press; D-pad first press at once, first repeat at 450 ms, 24 repeats in 1.5 s,
  key up at the release, a short tap does not repeat; scroll in both axes, signs, deadzone, batching; RB/LB sequences, Alt
  released 1 s after the last press, Cross/Triangle/Circle while the switcher is open, D-pad inside it; Guide + Cross, Cross held
  across a Guide press, Guide held with the stick deflected, Guide tap then the pad works again; everything held at the end of a
  run is released, Alt included. 50 checks, all pass.
- **inject** (SendInput into the sink): the pointer moved exactly as far as the mapper asked (15,142 px) and the sink's last move
  event was the pointer's end position; left click and right click arrived with the right buttons, a Cross held at the end of the
  run was released; vertical scroll -561 units and horizontal scroll +432 units arrived equal to the mapper's totals; Circle arrived
  as VK 0x1B, Triangle as VK 0x0D, D-pad Left held 800 ms as 9 Left-arrow key downs (8 flagged auto-repeat) and one real key up;
  no other key reached the sink; an injector aimed at a PID that is not the sink refused (exit 3) and sent nothing. On this
  machine a person was using the mouse during some runs, which is what the retry on `interference` is for (runs were discarded
  and repeated until one was clean).
- **keyboard**: Start opened `osk.exe` (pid tracked, window visible), the second Start closed it (the process was terminated by
  that pid after `WM_CLOSE` was ignored for 1 s), nothing before or after.
- **app**: a real XMB entered Desktop Mode from a second process; the log shows `desktop-pad: active`, then `live` (or `paused, off
  (desktop/controllerMouse)` with the preference false), and after a real Ctrl+Alt+Home `desktop-pad: inactive` before
  `desktop: window shown`; nothing from the driver while the XMB is visible; no SendInput with no pad attached.

### Left

- A real pad: stick feel (deadzone 0.20 / 0.25 and the curve are sensible defaults, not tuned by hand), a DualSense and an Xbox
  pad (the XInput right stick is new in `pad.cpp`; the HID right stick was already parsed), Guide combined with a face button,
  Alt+Tab with a game or an elevated window in front (SendInput cannot drive an elevated window from a normal process: UIPI),
  the touch keyboard path on a machine that has it registered (only the osk.exe fallback was exercised here), the Steam Big Picture
  title match against a live Big Picture window (matched by title prefix only), and the tray tooltip/menu item (verified by
  construction, like the rest of the tray).
