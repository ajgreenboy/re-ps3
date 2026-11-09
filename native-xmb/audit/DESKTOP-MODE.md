# Desktop Mode and single instance (PHASES-HTPC D1, D2)

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
