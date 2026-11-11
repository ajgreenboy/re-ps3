# In-game XMB lifecycle verification (2026-11-10)

## Findings and scope

The reported freeze/flicker did **not** reproduce with the tracked H5 GDI stub, either through
`--script` or through a normal interactive launch (render gate enabled, Win32 keyboard events,
diagnostic controller reports feeding the global PS watcher). No speculative GL-state or
render-pause bypass was added. The captured backdrop is intentionally a still, not a live game.

Two lifecycle defects were found:

- `View::enterInGame` made the window topmost, but only `returnToGame` removed that band.
  Closing the title while its overlay was open cleared Shell's state/rows but left the native
  window topmost. A Win32 `GetWindowLongPtr(GWL_EXSTYLE)` probe after the tracked title exited
  returned `style=0x8 topmost=8`. Such a window can cover another foreground app without receiving
  its keyboard input. Shell state changes now also clear the view's capture/window state and
  topmost band, including title exit and Steam stop; normal in-game topmost behaviour is retained.
- `Shell::gameRunning()` deliberately includes an unconfirmed Steam launch handoff for up to
  five minutes, to inhibit idle shutdown and duplicate launches. It was also used for overlay
  eligibility and the PS menu's Quit Game item. `titleRunning()` now requires a running QProcess
  or a confirmed, currently-running Steam report; the pending-launch guard remains separate.
  The first stopped report removes overlay eligibility/rows; the existing two-poll handoff
  debounce still governs final launch-session cleanup. Post-game Steam updates cannot preserve
  stale in-game state. Entry rechecks the title after capture before raising the overlay.

The user explicitly requested a native policy different from firmware F6: Quit Game is now
inserted only in the running content's launch column (Game for Steam and the imported game
stub), not all seven firmware columns. The Game manual row follows the same lifetime. The
original firmware findings in `INGAME-XMB-DESIGN.md` remain historical evidence, not this policy.

## Local evidence

All test configuration and captures are isolated beneath
`C:\Users\Albert\AppData\Local\Temp\opencode`, not the real user data directory.

- `ingame-runtime-before`: baseline PS taps, column/row navigation and Circle return. Logs show
  `title: render resumed`, then `idle frames=454 ... paused=0`, `idle frames=480 ... paused=0`;
  grabs move from Game to Network and from Online Instruction Manuals to Remote Play.
- `ingame-live-before2`: non-scripted baseline, actual OS keyboard delivery, synthetic PS hold
  through the watcher. `hold after 1000 ms`, `capture=ok (153 ms)`, foreground transfer to the
  XMB, `render resumed after 7842 ms`, then 538 frames/2249 ms and 479 frames/2000 ms with
  `paused=0`. Circle returns foreground to the stub and rendering pauses again. This disproves
  a universally frozen render loop or universally broken backdrop GL state on this machine.
- `ingame-runtime-after/topmost-end.txt`: reproduced topmost leak after title exit, before the
  view cleanup fix: `After title exit: style=0x8 topmost=8`.
- `ingame-runtime-final`: after cleanup, the identical probe reports
  `After title exit: style=0x0 topmost=0`. PS-tap and Circle returns both leave `inGame=false`
  while the tracked title remains running, hand foreground back to the stub and pause rendering.
  Closing the stub while the hold menu is up logs `cleared window state and topmost band` and
  ends the title session. Subsequent no-game captures have `titleRunning=false inGame=false`.
  Only `row ingame:quit inserted in game` is logged. All ten column grabs were taken: Game
  has 14 rows (12 home rows plus Quit Game/manual), Photo 6, Music 5, Video 6, TV 1, Network 11,
  PSN 3, Settings 16, Users 8 and Friends 9; no other column receives an in-game row.
- `ingame-live-final`: diagnostic HID-layout PS/Right bits (`buttons=0x408`) go through the normal,
  non-scripted watcher and controller poll. After the hold menu is closed, controller Right moves
  Game to Friends (`controller-right.png`), and an OS keyboard Down focuses Players Met. Logs
  show 475, 480 and 479 frames per ~2000 ms with `paused=0`, then Circle's keyboard equivalent
  returns foreground to the stub and pauses again. This tests synthetic controller reports, not
  physical hardware or an actual XInput device.
- `ingame-runtime-lifetime/ended-game.png.json`: after cooperative window close while the
  overlay's hold menu was open, Game returns from 14 to 12 rows, `runningTitle` is empty,
  `titleRunning=false` and `inGame=false`.
- `ingame-live-final2`: repeats the non-scripted controller test and additionally captures an
  OS keyboard Left moving Friends to PlayStation Network (`keyboard-left.png`).
- `ingame-steam-pending-confirmed`: an isolated Steam manifest for app `987654321`, with the
  Windows `where.exe` copied as a harmless scratch `steam.exe`, exercises an unconfirmed launch
  without starting real Steam or writing its registry. Logs show `launch: steam -applaunch
  987654321`, then PS tap with `titleRunning=0`; both tap/hold sidecars stay `inGame=false
  titleRunning=false`, and the hold logs `system menu opened rows=2 quitGameOffered=0`.

Verification scripts are local `ingame-after.ps1`, `ingame-live.ps1`, and
`ingame-steam-pending.ps1` under that scratch directory. The stand-in is the existing
`C:\dev\out\h5-test\stubgame.exe`; it is registered with a version-1 imported-app manifest.
Controller tests use `XMB_PAD_FAKE`, not a claim of physically pressing a controller. Real
RPCS3/Steam titles, exclusive-fullscreen behaviour and the user's original unspecified glitch
still require owner reproduction.
