# Zero-friction boot (controller-only HTPC), 2026-11-11

The goal: a person on the couch never reaches for a keyboard or mouse. The XMB is the only thing the TV
shows from power-on to power-off, and the pad drives everything that can appear on screen. This is a
plan of record for the *remaining* keyboard moments, written against the code as it stands on 2026-11-11.
It is not a new feature area; it threads together work that already exists (D3/D4, watchdog, D5, D6,
P6) and names the gaps between them.

Read with [packaging/README.md](../packaging/README.md) (autostart, watchdog, safe mode, auto-logon) and
[audit/DESKTOP-MODE.md](DESKTOP-MODE.md) (controller-driven desktop). Collisions with in-flight work are
section 3. Everything marked **INFERRED** is reasoning that has not yet been checked against `src/`.

## 1. Where it stands

Already in place, verified in the tree:

- **Start at Sign-in** (D3/D4): `autostart.cpp` (`kDefaultTask` = `re-ps3-native-kiosk`,
  `src/autostart.cpp:18`) and `packaging/install-autostart.ps1` register a per-user logon task that runs
  the watchdog; the row reads its state back from the scheduler. No admin rights needed.
- **Watchdog + safe mode** (D6, in flight): `watchdog.ps1` restarts after a crash, gives up on rapid
  failures, switches to `--safe` after 2 crashes in 5 minutes, rotates its log, and `--safe` skips the
  network clients and boot intro. Minidumps land in `<data>\crashes\` (newest 5, never uploaded).
- **Kiosk cursor hide**: the mouse cursor hides after 3 s without movement in `--kiosk` and returns on
  movement (`packaging/README.md` §"Boot time and the cursor").
- **Controller-driven desktop** (D5, in flight): a pad drives the desktop via SendInput, and starts
  Windows' own `osk.exe` on demand — including the elevation-manifest workaround
  (`src/desktop_pad_win.cpp:132-141`, `ERROR_ELEVATION_REQUIRED`/740).
- **Boot-time instrumentation**: the app logs `boot: first frame after N ms` (process creation to first
  buffer swap); capture it with `XMB_LOG=<file>`.

What is still a keyboard moment (the gaps this doc ranks):

1. **An elevated window kills the pad.** `src/desktop_pad_win.cpp:220` logs "SendInput delivered nothing
   ... the foreground window may be elevated." A UAC prompt, an installer, or any elevated app cannot be
   driven by the pad's SendInput, and the user must reach for a mouse. This is the single largest
   remaining break.
2. **Text entry outside the XMB.** The XMB's own OSK only works inside the XMB. The P6 web apps
   (Plex/YouTube/etc. in app/kiosk/browser windows) and any launched emulator's own menus need text, and
   there is no controller path to type into them.
3. **Auto-logon is a manual step.** The package deliberately never reads or stores a password
   (`packaging/README.md` §"Windows automatic sign-in"); the user must do `netplwiz` or Sysinternals
   Autologon themselves, and nothing confirms it worked before they sit down.
4. **~1.4 s of avoidable startup.** The handoff records ~1.4 s of shell + asset construction at startup. (D3's
   ~1 s PowerShell hitch on the GUI thread was fixed in `511c4ed`, which runs the toggle on a worker thread.) Not a
   keyboard moment, but it is the "instant-on" feel.
5. **Updates/reboots can land on a desktop.** With auto-logon + autostart set, a Windows update that
   reboots at 2 a.m. should come back to the XMB, not a login or desktop screen; nothing today asserts
   this end to end.
6. **Return-to-XMB from any launched app is not fully proven.** H5 (game launch/return) and P6 (web apps)
   are partly landed; P6's open item is a stale browser profile making the XMB think an app closed at once
   (`HANDOFF-2026-11-09.md` §5.3).

## 2. Ranked backlog

Size: S under a day, M one to three days, L a week or more. "New-only" = new files plus a one-time
registration (a `CMakeLists.txt` line and a row/hook), the pattern used by `tailscale_shell.cpp` /
`dlna_shell.cpp`. "Hybrid" = real edits inside `shell.cpp`, `view.cpp`, `host.cpp` or the packaging
scripts.

| ID | Title | Why it matters | Evidence | Size | Depends on | Files |
|---|---|---|---|---|---|---|
| ZF1 | Elevated-window input path | The pad dies at any UAC prompt or elevated installer; this is the last hard keyboard dependency | `src/desktop_pad_win.cpp:220` (SendInput fails on an elevated foreground window); `:132-141` (osk.exe already raised via the shell) | M | D5 (desktop pad) | Hybrid: a small elevated input-bridge helper (new) + a `host.cpp`/`settings_menu.cpp` row. **Status 2026-11-11: safe half done** (elevation detection `src/elevation.*`, the notice in Desktop Mode and the text delivery, owner options in `packaging/README.md`); the helper is deliberately not built (section 6). Real elevated window: device test pending. See [TEXT-ENTRY.md](ZF-TEXT-ENTRY.md). |
| ZF2 | Text entry into P6 web apps and emulators | The XMB OSK stops at the XMB boundary; a controller user cannot type a search box in Plex/YouTube or an emulator menu | P6 `content_registry.h:19` (`window` app/kiosk/browser); `src/desktop_pad_win.cpp:132` (osk.exe start path already exists) | S-M | D5 | `desktop_pad_win.cpp`, the web-app launch path in `shell.cpp`, a controller gesture to raise the OSK. **Status 2026-11-11: done** as the in-game "Enter Text" row (`src/text_entry.*`, XMB OSK, `KEYEVENTF_UNICODE`); verified against a sink window and end to end through the XMB, a real web app is still to be tried. See [TEXT-ENTRY.md](ZF-TEXT-ENTRY.md). |
| ZF3 | Guided auto-logon + first-run check | Auto-logon is the manual prerequisite for "boots straight to the XMB"; nothing confirms it before the couch test | `packaging/README.md` §"Windows automatic sign-in" (netplwiz / Autologon, password never stored) | M | D8 (first-run setup) | `packaging/` scripts + a first-run page in `shell.cpp`. **Built 2026-11-11 (branch wt/zf3), read-only status only:** `src/autologon.*`, a System Settings row, a first-run step after Start at Sign-in, one boot log line; see [AUTOLOGON.md](AUTOLOGON.md). Only a note in `packaging/README.md` changed; no script. |
| ZF4 | Boot-time tuning (**done 2026-11-11**: first frame 1694 ms to 982 ms median, [BOOT-TIME.md](BOOT-TIME.md)) | ~1.4 s shell/asset construction before the couch sees a frame (the PowerShell hitch is already off the GUI thread, `511c4ed`) | `HANDOFF-2026-11-09.md` §5.3 (~1.4 s startup) | M | none | `main.cpp`/`shell.cpp` startup |
| ZF5 | Update/reboot containment | A controller-only box must come back to the XMB after any reboot, not a desktop or login screen | `packaging/README.md` (auto-logon + logon task), D7 updater in plan | S-M | ZF3, D7 | `packaging/` + `update_check.*` (D7) |
| ZF6 | Return-to-XMB from every launched app | The daily loop (launch → play → back to XMB) must be lossless for web apps and emulators as it is for Steam | H5/P6 partly landed; `HANDOFF-2026-11-09.md` §5.3 (P6 stale-profile edge) | M | H5, P6 | `ps_watcher.*`, web-app launch path in `shell.cpp` |

## 3. Collisions and in-flight work

- **ZF1** is the elevated-window half of the D5 controller-desktop workstream (`C:\dev\xmb-wt\d5`).
  It touches `desktop_pad_win.cpp`, which D5 owns. Coordinate before editing; the input-bridge helper is
  new-only and can be built alongside.
- **ZF2** also lands in `desktop_pad_win.cpp` and the P6 web-app path — two workstreams' files. Hand the
  `shell.cpp` hunk to the shell owner.
- **ZF3** overlaps D8 (first-run setup) and D3/D4 (already done). The auto-logon *check* is new; the
  *flow* belongs in D8.
- **ZF5** depends on D7 (updater) and ZF3. Don't start it until the updater lands.
- **ZF4** and **ZF6** touch `main.cpp`/`shell.cpp`/`ps_watcher.*`, which D6 and D12 also touch. One owner
  per file, per the `PHASES-2026-11-07.md` collision map.

## 4. Verification each item needs

| Item | Verification |
|---|---|
| ZF1 | A stub elevated window (not a real UAC prompt) that the pad can reach: SendInput delivers to it, and the bridge is exercised with a dry-run switch. Real UAC behaviour is a device test on the HTPC. |
| ZF2 | Scripted run: open a P6 web app, raise the OSK from the pad, type into a search box. No real credentials; a local page is enough. |
| ZF3 | Scripted run of the first-run page on a scratch account; the real `netplwiz`/Autologon step stays manual and user-run (the package still never reads a password). |
| ZF4 | `XMB_LOG=<file>` capture of `boot: first frame after N ms` before/after; assert the PowerShell toggle no longer blocks the GUI thread (no ~1 s freeze). |
| ZF5 | Scripted run that simulates a reboot path; one real scheduled-update reboot on the HTPC to confirm it lands back in the XMB. |
| ZF6 | Real device: launch a Steam title, an emulator, and a P6 web app; crash one, close one normally, Alt-F4 one; confirm XMB focus and selection each time. |

Gate for every item: build passes (`native-xmb/build.ps1`), scripted run exits 0, no new `qWarning`
lines, and the affected audit file is updated with date, command and result.

## 5. The line this must not cross

Zero-friction boot is about removing keyboard moments for software the user already owns and launches
through legitimate channels. It must not grow a storefront-adjacent downloader or any path that fetches,
installs or launches content the user does not already own. Auto-logon guidance stays exactly as it is
today: the package never asks for, reads or stores a password.

## 6. Decisions and design notes (2026-11-11, plan taken over)

The first wave is ZF1 (detection and guidance only), ZF2, ZF3 and ZF4. ZF5 waits for the updater and ZF3; ZF6 needs a real
device. Work lands on branches in separate worktrees and is merged one at a time.

### ZF1: why the "input-bridge helper" is not built as drafted

- **Windows blocks it on purpose.** UIPI stops a lower-integrity process from sending input to a higher-integrity
  window, so `SendInput` from the XMB reaches nothing while an elevated window has focus.
- **A helper does not fix the common case.** A UAC consent prompt runs on the secure desktop, which no `SendInput`
  reaches, even from an elevated process.
- **A helper would be a privilege-escalation primitive.** An elevated process that injects whatever input an
  unelevated process asks for lets any program running as this user drive elevated windows. It would need to
  authenticate its caller properly, and that is the kind of boundary this project should not weaken quietly.
- **What is built:** detect that the foreground window belongs to an elevated process (`GetTokenInformation`,
  `TokenElevation`, no elevation needed to ask) and say so on screen, instead of the silent log line at
  `desktop_pad_win.cpp:220`: "The window in front is running as administrator, so the controller cannot drive it."
- **Left to the owner, documented in `packaging/README.md` with the trade-offs, not implemented:** (a) run the XMB at
  highest privilege through a scheduled task (needs administrator once to register; everything the XMB launches
  then runs elevated), (b) change Windows' UAC prompt behaviour (a system setting that is the user's to change, which
  the XMB never does).

### ZF2: type into any running title through the XMB's own keyboard

Not Windows' `osk.exe`. While a tracked title or web app runs, the in-game XMB gets an "Enter Text" action. It opens the
XMB's OSK (the text-field mode the phone remote already feeds), and on confirm hands the foreground back to the recorded
window and delivers the text with `SendInput` (`KEYEVENTF_UNICODE`, so any layout works). Never log the typed text.
Elevated targets fall under ZF1's message.

### ZF3: read-only auto-logon status

Built; design, registry values and the never-list are in [AUTOLOGON.md](AUTOLOGON.md). Show "Windows automatic sign-in: On for <user> / Off" in first-run setup and in Settings, with the manual steps. Read
only `AutoAdminLogon` and `DefaultUserName` under `HKLM\...\Winlogon`. Never read, write or test `DefaultPassword`, and
never change the setting.

### Verification notes

The gate is for the XMB's pixels. These items add no new gate scenario unless they add a visible page (ZF3's status row
does, ZF1's message may). Verify behaviour with the scripted runs in section 4, against a stub window the test starts.
