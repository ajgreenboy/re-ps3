# HTPC product phase, 2026-11-09

The XMB already covers the media, games and network features a couch needs (see
[PHASES-2026-11-07.md](PHASES-2026-11-07.md)). This phase is about the parts that make it behave like a living-room
appliance on Windows rather than an app you start: it comes up with the PC, it can step aside for the desktop and come
back the way SteamOS switches between Game Mode and Desktop Mode, it survives crashes and updates, and it never
needs a keyboard.

What exists today (read from the tree, 2026-11-09):

- `--kiosk --display N`: frameless fullscreen on one screen, straight to the XMB, Turn Off shuts the PC down.
- `packaging/`: `build-package.ps1` (one folder with exe and Qt runtime), `run-kiosk.ps1`, `watchdog.ps1` (restarts
  after a crash; a clean exit 0 stops it), `install-autostart.ps1` (a per-user logon scheduled task
  `re-ps3-native-kiosk` that runs the watchdog). All are scripts run by hand; nothing in the XMB can turn them on or off.
- No desktop mode, no tray icon, no single-instance guard, no global way back to the XMB once it is closed.

## Workstreams

Ranked by what a person on the couch hits first. Size: S < 1 day, M 1-3 days, L more.

| ID | Title | What done looks like | Size |
|---|---|---|---|
| D1 | Desktop Mode (SteamOS-style switch) | A "Switch to Desktop" row (PS-button power group, beside Turn Off; **INFERRED** placement, the firmware has no such row) hides the XMB: the kiosk window is withdrawn, rendering and the wave stop, music keeps playing only if chosen. The process stays alive so returning is instant. Ways back: the PS/Guide button on any XInput pad, a tray icon ("Return to XMB", "Exit XMB"), a global hotkey, or launching the XMB again. An "Exit to Desktop" row quits with code 0 so the watchdog stops. **Status 2026-11-09: done and verified windowed and `--kiosk`; the rows sit in the Users power group (the PS-hold menu is left as the firmware's); tray click/menu and a real Guide press on a pad are unverified, see [DESKTOP-MODE.md](DESKTOP-MODE.md).** | M |
| D2 | Single instance and commands | A second launch hands its request to the running XMB over a local socket (`--show`, `--desktop`, `--quit`) and exits; autostart plus a manual launch never yields two XMBs. **Status 2026-11-09: done; one XMB per user and data directory, report modes and `--script` excluded, `XMB_SINGLE_INSTANCE=0` opts out, see [DESKTOP-MODE.md](DESKTOP-MODE.md).** | S |
| D3 | Start with Windows, from the XMB | System Settings gains "Start at Sign-in" (on/off) that creates or removes the same per-user scheduled task `install-autostart.ps1` makes, pointing at the packaged watchdog; disabled with a reason when run from a dev build. No administrator rights, no stored passwords. Docs cover Windows automatic sign-in as the user's own step. **Done (wt/d34):** the row runs `install-autostart.ps1` (one definition of the task) and reads the scheduler back; INFERRED row, unavailable with a reason beside no `watchdog.ps1`; `XMB_AUTOSTART_TASK` / `-TaskName` override the name. The script's at-logon trigger now names the current user, because the any-user trigger it had needed administrator rights to register. | S |
| D4 | Boot polish | Time from sign-in to first XMB frame measured and kept low; mouse cursor hidden in kiosk until the mouse moves; Start-menu and desktop shortcuts ("XMB", "Return to XMB") from the package installer; a clean uninstall. **Done (wt/d34):** `boot: first frame after N ms` is logged (about 1.9 s here, 1.4 s of it between QApplication and the window: shell and asset construction, not tuned); cursor hides after 3 s in `--kiosk`; `install-autostart.ps1 -Shortcuts` makes Start-menu "XMB" and "Return to XMB" (`--show`), `-Uninstall` removes them and the task. Open: boot time from real sign-in on the HTPC; shortcuts are Start menu only, no desktop copies. | S-M |
| D5 | Controller-driven desktop | In Desktop Mode a pad can drive the desktop: left stick moves the pointer, Cross clicks, Square right-clicks, right stick scrolls, Start opens the on-screen keyboard (`osk.exe`); toggled by holding PS. The SteamOS desktop controller layout, rebuilt with `SendInput`. | M |
| D6 | Safe start and diagnostics | The watchdog starts the XMB in a safe mode (no network clients, no artwork fetch, default theme) after repeated crashes and says so on screen; Settings > System Information shows the last crash and the log folder; logs rotate. | M |
| D7 | Updater | Check GitHub releases of `ajgreenboy/XMB-Rewritten` (public), download a package, verify its hash, swap the package folder with the previous one kept for rollback, restart through the watchdog. Opt-in, never automatic without a confirmation. | M-L |
| D8 | First-run setup | A controller-only first start: dev_flash location check, library folders, display choice, pad test, Start at Sign-in. Today this is a JSON file and command-line flags. | M |
| D9 | Display and TV control | Per-app resolution / refresh / HDR switching on launch and restore on return; HDMI-CEC TV power and input through a USB-CEC adapter (libcec) when present. Hardware-gated: verify on the HTPC. | M-L |
| D10 | Bluetooth pairing and network page | Pair a controller or headset from the XMB (Windows.Devices.Bluetooth); show and join Wi-Fi networks. | M each |
| D11 | Phone remote | A LAN web page served by the XMB that sends D-pad/Cross/Circle/text input, for typing searches; off by default, LAN only, pairing code. | M |
| D12 | Idle cost | GPU and CPU near zero when nothing animates or the display is off; measured, with the wave and particles throttled while idle. | S-M |

Carried over from the 2026-11-07 plan and still open: V1 release run on the HTPC, in-game XMB step 4 (fullscreen
D3D capture) and step 10 (owner matrix), P6 leftovers (editor field for the web-app window style, a stale browser
profile still holding the lock), Photo Gallery Playlist/Network areas, RAF playback (needs real dynamic themes).

## Order

D1 + D2 together (they share the return path), D3 + D4 alongside (packaging), then D5, D6, D12, D8, D7, D9-D11.

## Rules for this phase

- Nothing changes system-wide settings on this PC during development; registry, scheduled-task and shortcut changes are
  exercised with a scratch task name or a dry run, and the real ones only through the user's own install step.
- Never close processes the XMB did not start.
- Each item lands with a gate scenario where it has a visible surface (V2 pattern) and a section in this file.
