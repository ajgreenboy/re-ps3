# Packaging: a portable HTPC release

These scripts turn a build into a self-contained folder for the living-room PC, and set it up to run the
XMB full-screen at login with a crash watchdog. They are PowerShell only; the app itself is unchanged.

## What is in a package

`build-package.ps1` copies a built output folder (default `out\integration`) into `dist\re-ps3\` and adds
the four scripts below plus `xmb-config.example.json`. The folder is the portable release shape: the
executables, the deployed Qt runtime (`Qt6*.dll`, `platforms\`, `imageformats\`, ...), the `.pdb` for
crash symbols, and the launcher/watchdog. **Firmware is never copied** - `dev_flash` stays on the target
and is pointed at by `xmb-config.json`.

| File | What it is |
|---|---|
| `re-ps3-native.exe`, `xmb-manager.exe` | the app and the companion manager |
| `run-kiosk.ps1` | the launcher: resolves firmware/data paths and runs the app `--kiosk --display N` |
| `watchdog.ps1` | loops the kiosk app and restarts it after a crash |
| `install-autostart.ps1` | registers the logon scheduled task that runs the watchdog; optionally creates the Start-menu shortcuts; uninstalls both |
| `xmb-config.json` | (you write it) `devFlash`, `dataDir`, `vshModules` paths on the HTPC |

## Build a package

```powershell
# from the repo root, after native-xmb\build.ps1 has produced out\integration
& .\native-xmb\packaging\build-package.ps1                 # -> dist\re-ps3
& .\native-xmb\packaging\build-package.ps1 -Zip            # -> dist\re-ps3.zip
```

## Install on the HTPC

1. Copy the whole `dist\re-ps3\` folder to the HTPC (keep the exe and the Qt plugins together).
2. Put a local `dev_flash` on the machine and write `xmb-config.json` next to the exe:
   ```json
   { "devFlash": "C:\\PS3\\dev_flash", "dataDir": "C:\\PS3\\xmb-data",
     "vshModules": "C:\\PS3\\vsh_module" }
   ```
   `vshModules` is optional (decrypted `*.prx`; the ribbon falls back to its approximate rest state
   without it). `dataDir` is optional (defaults to `<folder>\data`).
3. Test once by hand:
   ```powershell
   & .\run-kiosk.ps1 -Display 1
   ```
   Confirm the TV is `--display 1` (the second screen) in Windows Display settings; change `-Display` if not.
4. Register autostart and the Start-menu shortcuts:
   ```powershell
   & .\install-autostart.ps1 -Display 1               # logon task only: schedules watchdog.ps1 at sign-in
   & .\install-autostart.ps1 -Display 1 -Shortcuts    # ... plus Start menu "XMB" and "Return to XMB"
   & .\install-autostart.ps1 -Remove                  # remove the logon task only
   & .\install-autostart.ps1 -Uninstall               # remove the logon task and the shortcuts (clean uninstall)
   ```
   The task and the shortcuts are per user and need no administrator rights. "XMB" starts the kiosk through the
   watchdog; "Return to XMB" runs `re-ps3-native.exe --show` (the single-instance command that brings the
   running XMB back to the front). `-ShortcutDirectory <folder>` puts the shortcuts somewhere other than the
   Start menu (used for testing); `-NoTask` creates only the shortcuts; `-TaskName <name>` (or the environment
   variable `XMB_AUTOSTART_TASK`) renames the task, so a test never touches the real `re-ps3-native-kiosk`.

### Start at Sign-in, from the XMB

System Settings has a **Start at Sign-in** row (On/Off). It runs this same `install-autostart.ps1` (with an
argument list, never a shell string), so the row and the script agree on what "on" means: a scheduled task
named `re-ps3-native-kiosk` whose action runs the `watchdog.ps1` that sits next to the running exe. The row
reads its state back from the scheduler. In a dev build with no `watchdog.ps1` beside the exe the row is
listed as unavailable and says why. The row is this build's own (the console has no sign-in).

## Watchdog behaviour

`watchdog.ps1` runs the app, waits for it to exit, and:

- exits cleanly itself when the app quits with code **0** (a deliberate quit);
- restarts after **3 s** when the app crashes (a non-zero exit that survived past `-MinUptimeSeconds`);
- gives up after **5** consecutive runs that exit with a usage error (code 2) or in under 5 s, so a
  bad `dev_flash` path or missing config never spins.

The log is `watchdog.log` next to the exe. In kiosk mode "Turn Off the System" shuts Windows down, so the
watchdog never sees it - the machine powers off and the loop dies with it.

## Windows automatic sign-in (your own step)

The logon task only runs once the account is signed in. For a TV that boots straight into the XMB with no
password prompt, turn on Windows automatic sign-in for that account yourself; this package never asks for,
reads or stores a password.

- **netplwiz**: Win+R, `netplwiz`, untick "Users must enter a user name and password to use this computer",
  Apply, and enter the account's password in Windows' own dialog. (On Windows 11 the tickbox may be hidden until
  Settings > Accounts > Sign-in options > "For improved security, only allow Windows Hello sign-in for Microsoft
  accounts" is switched off.)
- **Sysinternals Autologon** (`autologon.exe` from Microsoft Sysinternals): enter the user, domain and password
  once; it stores the password as an LSA secret instead of plain text in the registry.

Both store a credential on this PC, so only the owner should do it, and only on a machine in a trusted room.

## Boot time and the cursor

The app logs `boot: first frame after N ms` (process creation to the first buffer swap) to the Qt log; set
`XMB_LOG=<file>` to capture it. In `--kiosk` the mouse cursor hides after 3 s without movement and returns on
the next movement.

## Dry-run and safety

- The app's own dry-run switch `XMB_POWER_DRY_RUN=1` logs power actions instead of acting; the watchdog
  does not affect it.
- Test the watchdog loop safely by pointing `xmb-config.json` at a scratch `dataDir` and quitting the app
  (exit 0) to confirm it stops, or by killing the app to confirm it restarts.
