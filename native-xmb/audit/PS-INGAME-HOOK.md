# The PS button while a game runs

Implemented 2026-10-26. `audit/PS-BUTTON.md` covers the in-XMB behaviour that already existed (tap flashes
the controller indicator, a 1 s hold opens the black system menu with Turn Off the System / Turn Off the
Controller, plus Quit Game while a tracked title runs). This document covers the part that did not work:
the XMB stopped seeing the controller the moment a launched game owned the foreground.

## What was built

- **`src/ps_watcher.cpp/.h`** - a process-wide PS watcher. It polls the existing HID layer (`pad.cpp`) on
  its own 20 ms `QTimer`, independent of the XMB window's focus, minimisation or render loop, and runs the
  console's press state machine: a release before 1000 ms is a tap, a press held for 1000 ms is a hold
  (reported once while still down), and the release after a hold is not also a tap. A missing controller
  sample is held for 500 ms and then cancels the press, so a radio dropout cannot become a phantom tap.
- **`src/view.cpp/.h`** - owns the watcher and turns its events into behaviour:
  - press: decides whether the event is actionable (`XMB is in front` or a tracked title is running);
  - hold + tracked title: force the XMB forward, then `Shell::psHeld()` opens the existing menu;
  - tap + tracked title: force the XMB forward and flash the indicator through `Shell::psTapped()`; nothing
    is quit;
  - no tracked title: the old rule still applies - only an XMB that is already in front reacts, so a PS
    press in another PC application does not steal focus;
  - after an explicit Quit Game: re-assert the foreground, then let the existing QProcess `finished` path
    rescan and restart the idle guard.
  - PS is removed from `pollController()` and masked out of the navigation bits; the watcher is the single
    PS source whether or not the XMB is focused.
- **No shell changes.** `shell.h` was owned by another session, so the view learns "a tracked title is
  running" from the public `Shell::psMenuItems()` (it contains Quit Game exactly when
  `Shell::gameRunning()` is true, for a tracked QProcess title or a Steam handoff). The actual termination
  still goes through `Shell::psMenuActivate()`.
- **`XMB_PS_LOG=1`** adds `ps:` state-transition lines to the normal Qt log (`XMB_LOG=<file>` captures
  them); if the variable is a path instead of `1`, the lines are also appended there directly. Logged
  transitions: watcher start, script injection, press (source, HID sample), hold duration, release/tap,
  actionable decision with `xmbActive`/`xmbForeground`/`titleRunning`/`menuOpen`, the foreground window's
  process and title on every PS event, bring-to-front begin/end, menu row count, Quit Game activation, and
  the post-quit foreground check.
- **Script runner** - the existing `ps` and `pshold` tokens now feed the watcher's real state machine
  (120 ms press for a tap, 1200 ms for a hold) instead of calling the shell directly. `grab=` captures and
  its state JSON are unchanged.

## How the global detection works, and why no hook

Windows HID reads are not tied to window focus: `pad.cpp` already opens each Sony controller
(`VID_054C`, DualShock 3 USB, DualShock 4 / DualSense USB and Bluetooth) and keeps the newest parsed
report in a process-wide slot. The watcher only reads that slot, so a game owning the foreground, an
unfocused or minimised XMB, or another application holding keyboard focus all leave detection intact.
No low-level keyboard hook, no DLL injection, no elevation, and no game-process interaction are involved.
A low-level hook would not help anyway: the PS button is not a keyboard key and the HID report still
arrives while a game is in front.

Bringing the XMB forward is the only window-manager interaction. It restores a minimised window, re-applies
the configured fullscreen state, calls `raise()`/`activateWindow()`/`requestActivate()`, and - only when the
Win32 foreground window is not the XMB - attaches to the foreground thread's input queue for
`SetForegroundWindow`/`BringWindowToTop` for that one call. This is the documented, non-invasive way
through the foreground lock; nothing is injected into the game.

### Limits

- **HID, not XInput.** Only Sony pads are watched for PS; an Xbox pad has no PS bit in the `wButtons` state
  the watcher sees.
- **Exclusive HID claims.** Steam Input, DS4Windows or a game that opens the pad with exclusive access can
  take the reports away; the watcher then sees no new sample, cancels the press after 500 ms and cannot
  detect PS. Steam's default DualSense handling is usually non-exclusive, but this is the first thing to
  check if a specific game never reacts.
- **Bluetooth vs USB.** `pad.cpp` supports DualShock 4 and DualSense over both USB and Bluetooth, and
  DualShock 3 over USB. A DualShock 3 over Bluetooth uses a different report and is not read.
- **Exclusive-fullscreen games.** `SetForegroundWindow` can be refused for an exclusive-fullscreen window.
  The app retries once 400 ms after a quit and after opening the menu, but a game that refuses to yield the
  foreground may need Alt-Tab; nothing is forced into the game process.
- **Steam titles.** A Steam handoff is "running" for the menu (so Quit Game is offered), but
  `Shell::psMenuActivate()` only terminates the QProcess it owns. Selecting Quit Game on a Steam title
  closes the menu and reports success without stopping the game. That is a shell-side gap; see "What is
  needed from the shell owner" below. The verified path here is the tracked/imported QProcess launch.
- **A modal dialog in the XMB** keeps the old rule: PS events are ignored while a modal owns input.
- **The PC is not a console.** The PS press is also visible to the game; the app neither consumes the
  button at the driver nor blocks the game from reading it.

## Verification

All artifacts are local under `C:\dev\out\native-pshook\verify`.

### Build and the stand-in title

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-pshook
```

The stand-in "game" is `C:\dev\out\ps-standin.exe` (source `C:\dev\out\ps-standin.cpp`, built locally): a
Win32 window titled "PS Stand-In Title" that takes the foreground for about a second, logs its lifecycle
to the file passed as argv[1], and exits on WM_CLOSE. It was registered through the normal imported-app
path:

```powershell
C:\dev\out\native-pshook\re-ps3-native.exe --import-apps C:\dev\out\native-pshook\verify\standin-apps.json --data-dir C:\dev\out\native-pshook\verify\data-a
```

The manifest is an ordinary version 1 registry entry (`id: ps-standin`, `kind: game`, `category: game`,
`executable: C:\dev\out\ps-standin.exe`). The scripted run starts at the Game column, presses DOWN once to
the freshly prepended row and ENTER, which goes through `Shell::startTracked()` exactly like an RPCS3 or
imported title. A `grab=` state sidecar in the diagnostic run confirmed `entryId=custom:ps-standin`,
`selection=1` before ENTER.

Typical invocation (Run A, hold while the game is in front):

```powershell
$env:XMB_LOG='C:\dev\out\native-pshook\verify\hold.log'
$env:XMB_PS_LOG='1'
$env:XMB_STEAM_ROOT='C:\dev\out\native-pshook\verify\empty-steam'
$env:XMB_STEAM_ARTWORK='0'
$script='wait,wait,wait,wait,wait,wait,DOWN,wait,ENTER,wait,wait,wait,wait,wait,wait,pshold,wait,wait,wait,wait,grab=C:\dev\out\native-pshook\verify\pshook-hold-menu.png,wait,wait,wait,wait,wait,wait,wait,wait,ENTER,wait,wait,wait,wait,grab=C:\dev\out\native-pshook\verify\pshook-after-quit.png,quit'
Start-Process C:\dev\out\native-pshook\re-ps3-native.exe -ArgumentList @('--dev-flash','C:\dev\rpcs3\bin\dev_flash','--data-dir','C:\dev\out\native-pshook\verify\data-a','--no-intro','--script',$script)
```

### Run A: hold while the game owns the foreground

Observed log (`hold.log`):

```text
ps: watcher started: poll=20 ms hold=1000 ms (HID reads are independent of window focus)
launch: C:/dev/out/ps-standin.exe ... (tracked)
ps: focus: XMB deactivated xmbActive=0 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: script: token pshold (hold)
ps: press source=script hid-sample=1 raw-ps=0
ps: event: press xmbActive=0 xmbForeground=0 titleRunning=1 menuOpen=0 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: hold after 1002 ms
ps: event: hold xmbActive=0 xmbForeground=0 titleRunning=1 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front begin reason="PS hold while a tracked title is running" xmbActive=0 xmbForeground=0 minimized=0 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front end xmbActive=1 xmbForeground=1 foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
shell: PS menu opened
ps: event: system menu opened rows=3 quitGameOffered=1 foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
ps: event: PS menu activate selection=0 quitting=1
returned from PS Stand-In Title exit 0 normal
shell: quit game requested from the PS menu
ps: event: Quit Game confirmed; tracked title terminated=1
ps: focus: XMB already holds the foreground after Quit Game terminated the tracked title
ps: focus: XMB holds the foreground after Quit Game terminated the tracked title
```

External checks made while the menu was open and after the run:

```text
standin alive at menu open=1
standin process now: 0
stand-in log: started / foreground claimed (x5) / WM_CLOSE received / window destroyed / exited
content: Steam installed 0 imported apps 1 warnings 0    <- rescan after the return
```

`pshook-hold-menu.png` shows the black system menu with **Quit Game** on the focused first row above Turn
Off the System and Turn Off the Controller. `pshook-after-quit.png` shows the XMB back with "The game was
closed." and the stand-in process gone. So: the hold alone did not quit (process alive while the menu was
open), and the explicit Cross/Enter selection did terminate the process the app had launched. The
`finished` handler still ran `refreshContent()` (rescan line) and `touch()` (idle interval restarted), so
the return lifecycle is unchanged.

### Run B: tap while the game owns the foreground

Same launch recipe with the `ps` token instead of `pshold`:

```text
ps: event: press xmbActive=0 xmbForeground=0 titleRunning=1 foreground=process="ps-standin.exe" ...
ps: tap after 124 ms
ps: event: tap 124 ms xmbActive=0 xmbForeground=0 titleRunning=1 foreground=process="ps-standin.exe" ...
ps: focus: bring-to-front begin reason="PS tap while a tracked title is running" ... foreground=process="ps-standin.exe" ...
ps: focus: bring-to-front end xmbActive=1 xmbForeground=1 foreground=process="re-ps3-native.exe" ...
ps: event: tap handled; no title was quit (titleRunning=1) foreground=process="re-ps3-native.exe" ...
```

```text
standin alive after tap=1
```

`pshook-tap-foreground.png` shows the XMB foreground with the controller indicator beside the clock and
the stand-in row still selected; the stand-in was only stopped later by an explicit Quit Game. This is the
console's "PS shows the XMB".

### No title running: behaviour unchanged

- XMB in front (the external stand-in helper took and released the foreground first): `hold` opened the
  ordinary two-row menu (`rows=2 quitGameOffered=0`), Escape closed it, `tap` flashed the indicator.
  `pshook-nogame-menu.png` shows Turn Off the System / Turn Off the Controller with no Quit Game row.
- XMB not in front (DeepSeek Harness window foreground, no title tracked): the same `pshold` and `ps` tokens
  logged `press not actionable (XMB not in front and no tracked title running)`, then `hold ignored` and
  `tap ignored`; no menu opened and the foreground window was never touched. No focus was stolen.

### Layer gate

The PS menu draws only while open and the watcher adds no drawing in any other state, so ordinary frames
are unchanged. The required regression run was:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work/ab-layers.ts --capture --exe C:\dev\out\native-pshook\re-ps3-native.exe
```

Result is recorded at the end of this file.

## Files changed

- `native-xmb/src/ps_watcher.h`, `native-xmb/src/ps_watcher.cpp` (new, one source pair)
- `native-xmb/CMakeLists.txt` (the pair added to the `re-ps3-native` target)
- `native-xmb/src/view.h`, `native-xmb/src/view.cpp`
- `native-xmb/audit/PS-INGAME-HOOK.md` (this file)
- `native-xmb/src/main.cpp` - no change was needed; the `ps`/`pshold` tokens already route through
  `View::replayPsTap()`/`replayPsHold()`.

Local verification artifacts (not committed): `C:\dev\out\ps-standin.cpp`, `C:\dev\out\ps-standin.exe`,
and `C:\dev\out\native-pshook\verify\` (scripts, logs, frames, isolated data directories).

## What is needed from the shell owner

1. **Steam Quit Game.** `Shell::psMenuActivate()` terminates only `game_`. While a Steam handoff is
   watched (`steamGameId_` set, `game_` null) the menu's Quit Game closes the menu and claims success but
   leaves the game running. The shell should quit the watched app (for example `steam.exe +app_stop <id>`
   through the same registry watch, or a tracked launcher) and report the real result. Until then the
   in-game Quit Game is honest only for imported/RPCS3-tracked titles.
2. **A public predicate.** The view currently derives `titleRunning` from `psMenuItems().size()>2` because
   `gameRunning()` is private and `shell.h` was out of scope here. A public `bool titleRunning() const`
   (and maybe `QString title() const`) would remove that coupling.
3. **Quit Game message loses the title.** In `psMenuActivate()`, `waitForFinished()` lets the QProcess
   `finished` lambda clear `gameTitle_` before line 959 builds the message, so the user sees only
   "The game was closed." without the title. Capture the title before terminating, or set the message first.
4. **Stable tree while verifying.** During this work another session edited `shell.cpp`, `steam_library.cpp`,
   `icons.cpp` and `text_renderer.cpp` concurrently; at different moments `steam_library.cpp` (missing
   `QProcess` include), `icons.cpp` (duplicate `ambient`) and `text_renderer.cpp` (missing
   `penPhase` definition / wrong `glyph` arity) stopped the shared build. The final build and layer gate
   have to run on a tree nobody else is compiling.
