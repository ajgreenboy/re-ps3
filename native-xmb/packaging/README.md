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
| `install-autostart.ps1` | registers a logon scheduled task that runs the watchdog |
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
4. Register autostart:
   ```powershell
   & .\install-autostart.ps1 -Display 1      # schedules watchdog.ps1 at logon
   & .\install-autostart.ps1 -Remove         # undo
   ```

## Watchdog behaviour

`watchdog.ps1` runs the app, waits for it to exit, and:

- exits cleanly itself when the app quits with code **0** (a deliberate quit);
- restarts after **3 s** when the app crashes (a non-zero exit that survived past `-MinUptimeSeconds`);
- gives up after **5** consecutive runs that exit with a usage error (code 2) or in under 5 s, so a
  bad `dev_flash` path or missing config never spins.

The log is `watchdog.log` next to the exe. In kiosk mode "Turn Off the System" shuts Windows down, so the
watchdog never sees it - the machine powers off and the loop dies with it.

## Auto-logon (the one step not automated)

`install-autostart.ps1` schedules the watchdog for the *current* account. For a TV that boots straight
into the XMB with no password prompt, enable Windows auto-logon for that account separately - either
`netplwiz` (uncheck "Users must enter a user name and password") or the `AutoAdminLogon` /
`DefaultUserName` / `DefaultPassword` registry values under
`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon`. This is deliberately manual: it stores a
password in the registry, so only the owner should set it.

## Dry-run and safety

- The app's own dry-run switch `XMB_POWER_DRY_RUN=1` logs power actions instead of acting; the watchdog
  does not affect it.
- Test the watchdog loop safely by pointing `xmb-config.json` at a scratch `dataDir` and quitting the app
  (exit 0) to confirm it stops, or by killing the app to confirm it restarts.
