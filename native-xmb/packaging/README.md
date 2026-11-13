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

For the release evidence run on the HTPC, `htpc-run.ps1` (in this folder of the repository, not in the package) checks
the install and walks the checklist; see [audit/HTPC-RUN-GUIDE.md](../audit/HTPC-RUN-GUIDE.md).

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

## Install with the installer (re-ps3-setup.exe)

Each release also has `re-ps3-setup.exe`, built by the release workflow from `installer/re-ps3.iss` around the same folder
the zip carries. It is the easy path for a first install on a new PC; the portable zip and the steps above still work, and the
in-app updater keeps using the zip either way.

- Installs per user, with no administrator prompt, into `%LOCALAPPDATA%\Programs\RE-PS3`. The updater swaps the whole folder
  for the new release (`apply-update.ps1`), which needs a folder the user can write; Program Files would not work.
- Asks for the `dev_flash` folder (and optionally a decrypted `vsh_module` folder) and writes `xmb-config.json` from them. It
  never copies or downloads firmware. An upgrade keeps the existing `xmb-config.json` and skips that page.
- Asks which display the TV is (saved for the next upgrade).
- Two ticked options run `install-autostart.ps1` for you: the logon task and the Start-menu shortcuts. Unticking one on an
  upgrade removes it.
- Stops a running XMB from that folder (and its watchdog) before replacing files, and can start it again at the end.
- Uninstall (Settings > Apps) removes the logon task, the shortcuts and the program files, including files a later in-app
  update added, and keeps `xmb-config.json` and `data\` so a reinstall picks up where it left off. The uninstaller's own files
  live in `%LOCALAPPDATA%\RE-PS3\uninstall`, outside the folder the updater swaps.

Build it locally with [Inno Setup 6](https://jrsoftware.org/isinfo.php) after `build-package.ps1`:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DAppVersion=0.3.0 native-xmb\packaging\installer\re-ps3.iss   # -> dist\re-ps3-setup.exe
```

## Code signing

Unsigned executables get a Windows SmartScreen "Windows protected your PC" warning on first run. The release workflow signs
`re-ps3-native.exe`, `xmb-manager.exe`, `xmb-updater.exe` (before they are zipped) and `re-ps3-setup.exe` with
[Azure Artifact Signing](https://learn.microsoft.com/azure/artifact-signing/) (formerly Trusted Signing), and skips both
signing steps when the secrets below are missing, so an unsigned build still works.

| Repository secret | Value |
|---|---|
| `AZURE_TENANT_ID` | the Entra tenant ID |
| `AZURE_CLIENT_ID` | an app registration with the "Artifact Signing Certificate Profile Signer" role on the signing account |
| `AZURE_CLIENT_SECRET` | a client secret for that app registration |
| `ARTIFACT_SIGNING_ENDPOINT` | the account's regional endpoint, e.g. `https://eus.codesigning.azure.net/` |
| `ARTIFACT_SIGNING_ACCOUNT` | the signing account name |
| `ARTIFACT_SIGNING_PROFILE` | the certificate profile name (Public Trust) |

The uninstaller Inno Setup generates inside the installer is not signed; Windows doesn't show SmartScreen for it because it is
never downloaded. Another signing provider would replace the two `azure/artifact-signing-action` steps with its own.

## Watchdog behaviour

`watchdog.ps1` runs the app, waits for it to exit, and:

- exits cleanly itself when the app quits with code **0** (a deliberate quit);
- restarts after **3 s** when the app crashes (a non-zero exit that survived past `-MinUptimeSeconds`);
- gives up after **5** consecutive runs that exit with a usage error (code 2) or in under 5 s, so a
  bad `dev_flash` path or missing config never spins;
- starts the XMB with **`--safe`** after **2** crashes in a row within **5 minutes** (`-SafeAfterCrashes`,
  `-CrashWindowSeconds`), and goes back to normal after a safe run that lasts **60 s** or more (`-StableSeconds`; a
  normal run that long also clears the crash streak). A safe start that fails fast counts toward the same
  5-rapid-failures give-up above. See `audit/SAFE-MODE.md`.

The log is `watchdog.log` next to the exe; it rotates at 5 MB keeping 3 old copies (`.1` to `.3`), and every decision
(start mode, exit, crash count, switch to safe mode, return to normal, give-up) is a line in it. The app's own log is
pointed at `<data>\logs\xmb.log` (rotated the same way) unless `XMB_LOG` is already set.

### Safe mode and crash files

`re-ps3-native.exe --safe` skips the network clients (Spotify, Jellyfin, DLNA, Tailscale, Home Assistant, Weather, Steam
Store), the Steam/RAWG artwork and site-icon fetches, the theme and the boot intro, and says "Started in safe mode after
repeated errors" in a notification and on System Information. Nothing stored changes. A crash writes a minidump and a
short text file (time, build, exception code, faulting module and offset) to `<data>\crashes\`, newest 5 kept; nothing is
ever uploaded. Settings > System Settings > Diagnostics shows the last crash, the run mode and the log folder.

In kiosk mode "Turn Off the System" shuts Windows down, so the
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

The XMB only shows the result: Settings > System Settings > Windows automatic sign-in (and a first-run step) reads
`AutoAdminLogon` and `DefaultUserName` from the Winlogon registry key, read-only. It never reads `DefaultPassword` and
never changes the setting; see [audit/AUTOLOGON.md](../audit/AUTOLOGON.md).

## Elevated windows and the controller

Windows does not let a program send keyboard or mouse input to a window that runs at a higher privilege level (UIPI). While a
window running as administrator is in front (an installer, a program started with "Run as administrator", a UAC prompt), a
normally started XMB cannot drive it with the controller, in Desktop Mode or with the in-game "Enter Text" row. A UAC consent
prompt is worse: it appears on Windows' secure desktop, which no program's input reaches, not even an elevated one.

The XMB detects this and says so instead of failing silently. It asks Windows, with no elevation of its own, whether the process
that owns the foreground window runs with an elevated token. If it does, nothing is sent (key and button releases still go out, so
nothing stays pressed), the XMB shows "Administrator window / The controller cannot drive it", and the log gets one line with the
process id and no window title: "The window in front is running as administrator, so the controller cannot drive it." If the
question cannot be answered, the window is treated as not elevated and the XMB tries as before.

There is no way around the limit from inside the XMB, and none is built: an elevated helper that injects input for the XMB would
let any program running as you drive elevated windows. Two options are left to the owner. The XMB implements neither and never
changes a Windows or UAC setting.

- **Run the XMB at highest privilege.** Register your own scheduled task with "Run with highest privileges" (needs an
  administrator once), or start it with "Run as administrator". An elevated XMB can then drive other elevated windows. Trade-offs:
  everything it launches (games, web apps, emulators) also runs elevated and can change the whole system; the Start at Sign-in
  task the XMB registers is not elevated, so you would replace it with your own; and the UAC consent prompt on the secure desktop
  is still out of reach.
- **Change the UAC prompt behaviour in Windows** (Settings, or Local Security Policy for the administrator elevation prompt, for
  example elevating without prompting). The prompt then never appears. Trade-offs: this is a machine-wide security setting; any
  program running as that administrator can elevate silently. Only the owner should decide it, on a machine in a trusted room.

For most HTPCs the better answer is to avoid elevated programs: do installs and updates that need administrator rights once, at
the keyboard.

## Boot time and the cursor

The app logs `boot: first frame after N ms` (process creation to the first buffer swap) to the Qt log; set
`XMB_LOG=<file>` to capture it. In `--kiosk` the mouse cursor hides after 3 s without movement and returns on
the next movement.

`XMB_BOOT_PROFILE=1` adds `boot-profile: <span> N ms` lines for each start-up stage, and `XMB_BOOT_PREFETCH=0` turns the worker
prefetch off for an A/B; the ribbon's settled state is cached in `<data>/cache/ribbon-settle.bin`
(see [audit/BOOT-TIME.md](../audit/BOOT-TIME.md)).

## Dry-run and safety

- The app's own dry-run switch `XMB_POWER_DRY_RUN=1` logs power actions instead of acting; the watchdog
  does not affect it.
- Test the watchdog loop safely by pointing `xmb-config.json` at a scratch `dataDir` and quitting the app
  (exit 0) to confirm it stops, or by killing the app to confirm it restarts.
