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
- **No shell changes in this work.** `shell.h`/`shell.cpp` were owned by another session, so the view
  learns "a tracked title is running" from the public `Shell::psMenuItems()` (it contains Quit Game
  exactly when `Shell::gameRunning()` is true, for a tracked QProcess title or a Steam handoff). The
  actual termination still goes through `Shell::psMenuActivate()`. That session later added the
  Steam-stop branch to the same function; the final build includes it, but this verification exercised
  the tracked QProcess path only.
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
- **Another app taking the foreground while the menu is open.** The menu is raised when it opens, not
  continuously, so an unrelated app can still jump in front (the final Run A log shows an EAAntiCheat
  splash doing exactly that). A further PS hold re-raises the still-open menu; a tap closes it.
- **Steam titles.** A Steam handoff counts as running, so the menu offers Quit Game; the shell now asks
  Steam to stop the app through `steamStopApp()` and lets the registry watcher report the return. That
  branch was not exercised here (no Steam title was launched); the tracked/imported QProcess path is the
  one verified end to end.
- **A modal dialog in the XMB** keeps the old rule: PS events are ignored while a modal owns input.
- **The PC is not a console.** The PS press is also visible to the game; the app neither consumes the
  button at the driver nor blocks the game from reading it.

## Verification

All artifacts are local under `C:\dev\out\native-pshook\verify`. A DualSense was connected over USB
during these runs (`VID_054C`, `PID_0CE6`); every injected press logs `hid-sample=1 raw-ps=0`, so the
watcher was reading the real HID stream while another window had the focus. The PS press itself is
injected by the script token into the same state machine (there is no fixture to press the physical
button), which is the only synthetic part of the path.

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

Typical invocation (Run A; the harness minimises the XMB main window while the stand-in holds the
foreground, then the `pshold` token fires):

```powershell
$env:XMB_LOG='C:\dev\out\native-pshook\verify\minimized-final.log'
$env:XMB_PS_LOG='1'
$env:XMB_STEAM_ROOT='C:\dev\out\native-pshook\verify\empty-steam'
$env:XMB_STEAM_ARTWORK='0'
$script='wait,wait,wait,wait,wait,wait,DOWN,wait,ENTER,wait,wait,wait,wait,wait,wait,wait,wait,pshold,wait,wait,wait,wait,grab=C:\dev\out\native-pshook\verify\pshook-minimized-menu.png,wait,wait,wait,wait,ENTER,wait,wait,quit'
Start-Process C:\dev\out\native-pshook\re-ps3-native.exe -ArgumentList @('--dev-flash','C:\dev\rpcs3\bin\dev_flash','--data-dir','C:\dev\out\native-pshook\verify\data-e3','--no-intro','--script',$script)
```

### Run A: hold while the game owns the foreground

After the stand-in took the foreground the XMB window was minimised with `ShowWindow(SW_MINIMIZE)`, which
removes any doubt that the event arrived while the XMB was usable. `pshold` was then sent from the
scripted run. Observed log (`minimized-final.log`):

```text
ps: watcher started: poll=20 ms hold=1000 ms (HID reads are independent of window focus)
launch: C:/dev/out/ps-standin.exe ... (tracked)
ps: script: token pshold (hold)
ps: press source=script hid-sample=1 raw-ps=0
ps: event: press xmbActive=0 xmbForeground=0 titleRunning=1 menuOpen=0 modal=0 foreground=process="ChatGPT.exe" title="ChatGPT"
ps: hold after 1000 ms
ps: event: hold xmbActive=0 xmbForeground=0 titleRunning=1 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front begin reason="PS hold while a tracked title is running" xmbActive=0 xmbForeground=0 minimized=1 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front end xmbActive=1 xmbForeground=1 foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
shell: PS menu opened
ps: event: system menu opened rows=3 quitGameOffered=1 foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
ps: script: PS release injected
ps: release after 1207 ms (hold)
ps: event: release after 1207 ms (hold, no tap)
ps: event: PS menu activate selection=0 quitting=1
returned from PS Stand-In Title exit 0 normal
shell: quit game requested from the PS menu
ps: event: Quit Game confirmed; tracked title terminated=1
ps: focus: XMB already holds the foreground after Quit Game terminated the tracked title
ps: focus: XMB holds the foreground after Quit Game terminated the tracked title
```

The press was logged while the XMB was minimised (`xmbActive=0`) and the title was running
(`titleRunning=1`); the 1 s hold restored the window and opened the three-row menu with Quit Game. The
release is logged as a hold, not a tap. `quitting=1` is computed immediately before the explicit Enter on
the focused Quit Game row, so the title was still running at that point: the hold alone did not quit
anything. `returned from ... exit 0 normal` is the tracked QProcess exiting, followed by the rescan line
`content: Steam installed 0 imported apps 1 warnings 0` after the return, so `refreshContent()` and the
`touch()` idle reset ran exactly as for a normal return.

External checks from the same recipe without the minimise step (the focus evidence is noisier there
because other applications were switching during the run, but the lifecycle checks are the same):

```text
standin alive at menu open=1
standin process now: 0
stand-in log: started / foreground claimed (x5) / WM_CLOSE received / window destroyed / exited
```

`pshook-hold-menu.png` shows the black system menu with **Quit Game** on the focused first row above Turn
Off the System and Turn Off the Controller; `pshook-minimized-menu.png` is the same menu after the restore
from minimised. `pshook-after-quit.png` shows the XMB back with "The game was closed." and the stand-in
process gone.

### Run B: tap while the game owns the foreground

Same launch recipe, but after the stand-in takes the foreground the XMB window was minimised and the `ps`
token sent. This removes any doubt that the event arrived while the XMB was usable: the window was
minimised and not active, the stand-in owned the foreground, and the log records the game process by
name.

```text
xmb exit=0 minimized=True
ps: press source=script hid-sample=1 raw-ps=0
ps: event: press xmbActive=0 xmbForeground=0 titleRunning=1 menuOpen=0 modal=0 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: tap after 120 ms
ps: event: tap 120 ms xmbActive=0 xmbForeground=0 titleRunning=1 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front begin reason="PS tap while a tracked title is running" xmbActive=0 xmbForeground=0 minimized=1 foreground=process="ps-standin.exe" title="PS Stand-In Title"
ps: focus: bring-to-front end xmbActive=1 xmbForeground=1 foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
ps: event: tap handled; no title was quit (titleRunning=1) foreground=process="re-ps3-native.exe" title="re-ps3 - Native XMB"
standin alive after tap=1
```

`pshook-tap-foreground.png` shows the restored XMB with the controller indicator beside the clock and the
stand-in row still selected. The same run then continued with an explicit `pshold` + Enter (while an
unrelated foreground change happened again) and ended with `standin now=0`, so the title was only stopped
by the Quit Game selection, not by the tap. This is the console's "PS shows the XMB".

### No title running: behaviour unchanged

- XMB in front (a first `full` script token put the XMB in front): `hold` opened the ordinary two-row menu
  (`rows=2 quitGameOffered=0`), Escape closed it, `tap` flashed the indicator and left the window in
  front. `pshook-nogame-menu.png` shows Turn Off the System / Turn Off the Controller with no Quit Game
  row.
- XMB minimised, no title tracked: `ShowWindow(SW_MINIMIZE)` left the XMB inactive, then the `pshold` and
  `ps` tokens logged `press not actionable (XMB not in front and no tracked title running)`,
  `hold ignored` and `tap ignored`. No menu opened and the XMB was not restored, so no focus was stolen.
- A modal XMB dialog keeps the old rule too: the watcher checks `activeModalWidget()` and logs the press
  as ignored instead of opening a menu behind the dialog.
- A press that begins in the XMB but loses the foreground before the hold/tap completes is ignored as
  well; the check is repeated at the event, not only at the press.

### Layer gate

The PS menu draws only while open and the watcher adds no drawing in any other state, so ordinary frames
are unchanged. The required regression run was:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work/ab-layers.ts --capture --exe C:\dev\out\native-pshook\re-ps3-native.exe
```

Observed result (`verify/ab-layers-final.log`): every layer within budget, exit 0.

| Layer | PSNR | Baseline | SSIM | Baseline | Gate |
|---|---:|---:|---:|---:|---|
| frame | 18.01 | 18.02 | 0.8301 | 0.8301 | PASS |
| top background | 20.24 | 20.24 | 0.9344 | 0.9344 | PASS |
| category icon row | 20.91 | 21.05 | 0.8947 | 0.8950 | PASS |
| item label row | 11.94 | 11.94 | 0.3910 | 0.3910 | PASS |
| clock text | 13.51 | 13.51 | 0.6131 | 0.6131 | PASS |
| info bar | 17.12 | 17.12 | 0.7046 | 0.7046 | PASS |

`PASS: 6/6 layers within budget`.

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

1. **Steam Quit Game - added during this session, still unverified here.** The earlier shell code killed
   only `game_`, so a Steam handoff's Quit Game closed the menu and claimed success while Steam kept the
   game running. The shell owner then added the `steamGameId_` branch in `psMenuActivate()` (it calls
   `steamStopApp()` and leaves the registry watcher to report the return), and the final build here
   contains it. No Steam title was launched for this verification, so the tracked/imported QProcess path
   is the only Quit Game path proved end to end; please exercise the Steam branch on a real title.
2. **A public predicate.** The view currently derives `titleRunning` from `psMenuItems().size()>2` because
   `gameRunning()` is private and `shell.h` was out of scope here. A public `bool titleRunning() const`
   (and maybe `QString title() const`) would remove that coupling.
3. **Quit Game message loses the title.** In `psMenuActivate()`, `waitForFinished()` lets the QProcess
   `finished` lambda clear `gameTitle_` before the message is built, so the user sees only
   "The game was closed." without the title. Capture the title before terminating, or set the message
   first.
4. **Build collision.** During this work another session edited `shell.cpp`, `steam_library.cpp`,
   `icons.cpp` and `text_renderer.cpp` concurrently; at different moments `steam_library.cpp` (missing
   `QProcess` include), `icons.cpp` (duplicate `ambient`) and `text_renderer.cpp` (missing
   `penPhase` definition / wrong `glyph` arity / const `rasterize`) stopped the shared build. The final
   build, the four scripted runs (`hold/minimised-hold`, `tap/minimised-tap`, `no-game focused`,
   `no-game minimised`) and the layer gate all ran after those fixes, between 20:54 and 21:04. Avoid
   editing and building the same tree in parallel while a verification run is in progress.
