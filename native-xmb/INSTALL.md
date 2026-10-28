# Install, build and run

This is the one-page install and run guide for the native C++ XMB recreation. It covers a Windows
machine that has never built the project, the output-folder convention, where state lives, how to
point the app at Steam and Jellyfin, the input bindings and the failures people actually hit.

The application is a Windows x64 desktop program built with Qt 6, OpenGL 3.3 core and the MSVC
toolchain. It is not a firmware replacement and it ships no Sony assets: it reads a local PS3 4.93
`dev_flash` directory that you supply, and it never writes to it.

Current handoff and gate state: [HANDOFF-2026-10-27.md](HANDOFF-2026-10-27.md).

## 1. What must exist on the machine

| Requirement | Notes |
|---|---|
| Windows 10 or 11, x64 | The build and the crash handler are Windows-specific. |
| Visual Studio 2022 with **Desktop development with C++** | `build.ps1` finds it through `vswhere` and requires the x64 VC tools. CMake and Ninja come from the same Visual Studio install. |
| Qt 6.8 or newer, MSVC 2022 64-bit kit | Needs Widgets, OpenGLWidgets, Multimedia, Concurrent and Network, plus `windeployqt`. `build.ps1` looks for `<drive>:\Qt\<version>\msvc2022_64`, newest first; pass `-QtRoot` for another kit. |
| FreeType | Either an installed FreeType CMake package or the Qt SDK's bundled FreeType targets (`Qt6BundledFreetype` / `Qt6FreetypePrivate`). CMake prefers an installed package. |
| Microsoft Visual C++ 2015-2022 x64 Redistributable | The build deploys Qt with `--no-compiler-runtime`, so the target machine supplies the CRT. |
| A GPU and driver with OpenGL 3.3 core | The app requests a 3.3 core profile with 4x MSAA. Software rasterizers, some remote sessions and very old drivers will fail to show a window. |
| A local PS3 4.93 `dev_flash` directory | Read-only. The launcher checks for `vsh\resource\explore_plugin_full.rco`; the app needs `data\font`, `vsh\resource` and the XMBML files there. Keep it outside the repository. |
| Optional: `dev_hdd0` | Installed titles and saves. Defaults to the directory next to `dev_flash`; override with `--dev-hdd0`. |
| Optional: decrypted VSH modules (`*.prx`) | Only needed for the ribbon's rest tables (`--vsh-modules`). Without them the cloth starts flat and settles. |
| Optional: Bun 1.x | Only for the verification harness (`work/*.ts`). Not needed to build or run. |
| Optional: Steam client | For installed titles, collections, downloads, accounts and the friends helper. |
| Optional: Steamworks SDK public headers | Only to build the signed-in Steam friends helper (`-SteamworksHeaders <sdk/public/steam>`). The SDK itself is not redistributed. |
| Optional: a Jellyfin server | Only for the Jellyfin service. The app is the client. |

Nothing in this list is downloaded by the build. Firmware assets are never copied into source control.

## 2. Get the source and build

The project lives in the private `ajgreenboy/re-ps3` repository under `native-xmb/`.

```powershell
git clone https://github.com/ajgreenboy/re-ps3.git C:\dev\xmb
cd C:\dev\xmb

# Stop any running re-ps3-native.exe first: the linker fails with LNK1104 while it runs.
& .\native-xmb\build.ps1
```

The default output directory is `out\native-xmb` (relative to the repository root). The script
configures CMake, builds with Ninja, then runs `windeployqt` on
`out\native-xmb\re-ps3-native.exe`, so the Qt DLLs and plugins are copied next to the executable in
the same folder. The folder is the portable release shape: copy it, and keep the firmware outside it.

Useful parameters:

```powershell
# Another Qt kit
& .\native-xmb\build.ps1 -QtRoot C:\Qt\6.8.3\msvc2022_64

# A separate output directory (parallel sessions and side builds)
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-xmb-scratch

# Build the optional Steam client friends helper
& .\native-xmb\build.ps1 -SteamworksHeaders C:\path\to\steamworks\public\steam
```

Output-folder convention, in one line: **one build, one folder** - `out\native-xmb` for the main
build, any absolute or relative path passed to `-OutputDirectory` for a side build, and the Qt
runtime, the data directory and the per-session logs all live under that folder. Do not mix a side
build's executable with the main folder's Qt DLLs.

A build needs the CMake and C++ pieces above; it does not need `dev_flash` (that is a runtime
requirement) and it does not need Bun.

## 3. Run

`--dev-flash` is required. Without it the app prints its usage and exits with code 2.

```powershell
# Launcher: defaults -FlashRoot to the sibling rpcs3\bin\dev_flash and writes session logs
& .\run-native-xmb.ps1 -FlashRoot C:\path\to\dev_flash

# Same thing directly
& .\out\native-xmb\re-ps3-native.exe `
    --dev-flash C:\path\to\dev_flash `
    --data-dir  C:\path\to\xmb-data

# Skip the cold-boot intro (useful when debugging)
& .\out\native-xmb\re-ps3-native.exe --dev-flash C:\path\to\dev_flash --data-dir C:\path\to\xmb-data --no-intro
```

- `--dev-flash` - the local firmware directory (read-only).
- `--dev-hdd0` - installed-title data; defaults to the directory next to `dev_flash`.
- `--data-dir` - writable state; defaults to `<exe folder>\data`. The launcher uses
  `out\native-xmb\data`.
- `--vsh-modules` - local directory of decrypted `*.prx` modules for the ribbon rest tables.
- `--no-intro` - start at the XMB instead of the 4.93 cold boot.

Run the executable from its own folder (or use the launcher) so Qt finds the deployed platform and
image plugins. The first launch creates the data directory and the default preferences.

## 4. Where preferences, logs, caches and artwork live

`<data>` below is the `--data-dir` value (default `<exe folder>\data`).

| Path | What it is |
|---|---|
| `<data>\preferences.json` | Versioned settings, `{"version":1,"values":{...}}`, including the `media/resume` records. Written atomically. |
| `<data>\profiles.json` | Versioned local profiles (`version`, `profiles`, `activeId`). Preferences are still shared between local profiles. |
| `<data>\settings.ini` | Legacy preferences from the first native foundation. Read once and deliberately preserved. |
| `<data>\apps.json` | The imported app/service registry written by `--import-apps` (version 1). |
| `<data>\family-shared.json` | Hand-written list of Steam Family titles to add to the library (`{"apps":[{"id":620,"title":"Portal 2"}]}`). |
| `<data>\artwork\` | Per-game artwork overrides: `<appid>.png` (wide row), `<appid>_icon.png`, `<appid>_bg.png`; `.jpg`, `.jpeg` and `.webp` also work. |
| `<data>\cache\steam\` | Cached Steam header images (`<appid>.image`). Safe to delete; it is refetched. |
| `%APPDATA%\re-ps3-native\steam_api_key` | Optional one-line Steam Web API key. Outside the repository and outside the data directory. |
| `<exe folder>\crash.log` | Crash stack (exception code plus symbolized frames) when `XMB_LOG` is not set. The `.pdb` next to the executable gives the symbols. |
| `<data>\diagnostics\` | Where a future `--diagnose` bundle would be written; see [audit/DIAGNOSE.md](audit/DIAGNOSE.md). |
| `out\native-xmb\sessions\<timestamp>\native.stdout.txt` and `native.stderr.txt` | Per-run output from the launcher. |

Logs are opt-in environment variables, not files written by default:

| Variable | Effect |
|---|---|
| `XMB_LOG=<file>` | Append every Qt message to that file. Also redirects the crash stack there. |
| `XMB_FRAME_TRACE=<file>` | Write a bounded frame trace on orderly shutdown; summarise with `bun work/frame-report.ts <file>`. |
| `XMB_STEAM_ARTWORK_LOG=1`, `XMB_STEAM_DOWNLOADS_LOG`, `XMB_SUBTITLE_LOG`, `XMB_VOLUME_LOG`, `XMB_NOTIFY_LOG`, `XMB_AUDIO_LOG`, `XMB_PS_LOG`, `XMB_XMBML_LOG`, `XMB_MOTION_LOG` | Service-specific diagnostic lines. The owning audit document lists each one. |

Never put a credential in a log. The Steam key is never written to preferences, reports, manifests or
logs; the Jellyfin session token stays in memory and stream requests go through the loopback gateway.

## 5. Point it at Steam

Installed titles and collections are read locally; the owned list needs Valve's Web API key.

- **Steam install discovery**: the default Steam root is found on the machine. Use
  `--steam-root <dir>` if you have more than one install or a non-standard layout.
- **Owned titles** (the full library, including titles that are not installed) use
  `IPlayerService/GetOwnedGames`, which requires an API key. Supply it in one of two ways:
  - `STEAM_WEB_API_KEY` in the launch environment, or
  - a one-line file at `%APPDATA%\re-ps3-native\steam_api_key`.
  Optionally set `XMB_STEAM_ID` when the local account is ambiguous.
- **Without a key** the Game column keeps the installed titles and tells you a key is needed. It does
  not invent a library.
- **Collections** come from
  `userdata\<accountid>\config\cloudstorage\cloud-storage-namespace-1.json`, no key needed.
- **Family-shared titles** are not returned by the owned-games endpoint; Valve's family endpoint
  needs an OAuth access token, and a Web API key is rejected with 401. Add shared titles by hand to
  `<data>\family-shared.json` (`{"apps":[{"id":620,"title":"Portal 2"}]}`).
- **Artwork overrides**: drop `<appid>.png` (or `.jpg`, `.jpeg`, `.webp`), `<appid>_icon.png` and
  `<appid>_bg.png` into `<data>\artwork\`. An override wins over the cached Steam header and the
  manifest icon.
- **Rescan**: F5, or the **Refresh Game Library** row.
- **Steam Friends**: the Friends column prefers the signed-in Steam client through the optional
  helper built with `-SteamworksHeaders`. The Web API fallback is limited and can return 401 on the
  friend list; that is Valve's endpoint, not a privacy setting to change.
- **Headless reports** (no window): `--steam-report`, `--owned-games-report`,
  `--steam-collections-report`, `--steam-library-report`, `--steam-store-report`,
  `--steam-friends-report`, `--steam-downloads-report`, `--steam-accounts-report`,
  `--steam-family-report`, `--steam-artwork-report`. All take an output JSON path and exit.

Purchases, downloads and launches stay in Steam: the XMB hands `steam://install/<id>` to the client,
starts installed titles with `steam.exe -applaunch <id>` and never writes to Steam's files.

## 6. Point it at Jellyfin

Jellyfin is a native service (library pages, direct play, server-negotiated HLS, in-XMB sign-in), not
a browser shortcut.

1. Copy `examples\jellyfin.json` and set your server's base URL:

   ```json
   {
     "version": 1,
     "apps": [
       { "id": "jellyfin", "title": "Jellyfin", "kind": "jellyfin", "category": "tv",
         "url": "http://192.168.1.10:8096" }
     ]
   }
   ```

   The URL needs a scheme and a host and must not contain credentials, a query or a fragment.
   `category` may be `tv` or `video` (the example uses `tv`).

2. Import it. Reimporting the same `id` updates the entry instead of duplicating it:

   ```powershell
   & .\out\native-xmb\re-ps3-native.exe --data-dir <data> --import-apps .\examples\jellyfin.json
   ```

3. Start the app, open the TV/Video Services area and select the Jellyfin row. Sign-in is an ordinary
   XMB page: server URL, username, password and Sign In. The on-screen keyboard masks the password.
   The session token is kept in memory only; sign out to drop it.

`examples\media-apps.json` adds Jellyfin plus Plex, YouTube and Spotify as browser URL apps; only the
Jellyfin entry is native. The registry schema and limits are in
[audit/CONTENT-REGISTRY.md](audit/CONTENT-REGISTRY.md); the service behaviour and verification are in
[audit/JELLYFIN-NATIVE.md](audit/JELLYFIN-NATIVE.md).

## 7. Controller and keyboard bindings

Input comes from the keyboard, XInput pad 0, and PlayStation controllers over HID (DualShock 3 on
USB; DualShock 4 and DualSense on USB or Bluetooth). The left stick works as the D-pad. Cross
confirms and Circle goes back, exactly as XInput A and B do.

| Action | Keyboard | Controller |
|---|---|---|
| Navigate columns and rows | Arrow keys; PageUp/PageDown move 10 rows | D-pad or left stick |
| Confirm | Enter | Cross / XInput A |
| Back / cancel | Escape or Backspace | Circle / XInput B |
| Item options (Triangle) | T | Triangle / Y |
| Full screen | F11 | - |
| Refresh library / folder | F5 | Refresh Game Library row |
| Play / pause media | Space | - |
| Previous / next media | `[` / `]` | - |
| Shuffle / repeat | M / R | - |
| Master volume | Volume media keys | L2 / R2 pressure ramp |
| PS system menu | - | PS button, held |
| Photo previous / next, zoom, reset | Q / E, `+` / `-`, `0` | - |
| Skip the cold-boot intro | Any key | Any button |

The PS button watcher is focus-independent: a hold (about one second) brings the XMB forward from a
running title and opens the system menu. Controller pairing, rumble and battery reporting belong to
Windows/HID, not the app. There is no remapping screen yet.

## 8. Troubleshooting

| Symptom | Check and fix |
|---|---|
| **Black window, or it exits immediately** | Run the executable from its output folder or through the launcher, so Qt finds `platforms\qwindows.dll`. Confirm the GPU driver supports OpenGL 3.3 core, and avoid remote-desktop/VM sessions. Start with `XMB_LOG=<file>` and read the last lines. Try `--no-intro`. Check that `--dev-flash` points at the firmware root, not the RPCS3 install root. |
| **Launcher says "Pass -FlashRoot pointing to your local PS3 dev_flash directory"** | The path is missing `vsh\resource\explore_plugin_full.rco`. Point at the `dev_flash` folder itself. |
| **Missing fonts, boxes or tofu instead of labels** | Confirm `dev_flash\data\font` exists and the path is right. FreeType must be deployable: use the Qt SDK's bundled targets or an installed FreeType package, and make sure the matching DLL was deployed. Check the log for font/resource load failures; the `--font-report` probe prints "Cannot load firmware font faces" when the directory is wrong. Compare with [audit/FONT-MATCHING.md](audit/FONT-MATCHING.md). |
| **No Steam library** | Installed titles need a valid Steam root (`--steam-root` if not standard). Owned titles also need `STEAM_WEB_API_KEY` or `%APPDATA%\re-ps3-native\steam_api_key`; without it the column states that a key is needed. Check `--steam-report` and `--owned-games-report`; use `XMB_STEAM_ID` if the account is ambiguous. A key is optional for collections, downloads and accounts. |
| **No audio** | Check the Windows default output device and its volume, the in-app volume (default 60) and mute, and the file's codec. Qt Multimedia needs a working Windows audio backend. Interaction cues additionally need the decoded RCO sounds from `dev_flash`. Set `XMB_AUDIO_LOG` for details. |
| **LNK1104: cannot open `re-ps3-native.exe`** | A previous instance is still running. Stop `re-ps3-native.exe` (Task Manager or `Stop-Process -Name re-ps3-native`) and build again. For parallel sessions use a separate `-OutputDirectory`; antivirus can also briefly hold a just-written executable. |
| **`--dev-flash` exits with code 2** | The option is required. Pass the local firmware directory. |
| **Build cannot find Qt** | Pass `-QtRoot` to a Qt 6.8+ `msvc2022_64` kit that has Widgets, OpenGLWidgets, Multimedia, Concurrent and Network. |
| **Build cannot find Visual Studio** | Install Visual Studio 2022 with the Desktop C++ workload; `vswhere` must find the x64 VC tools. |
| **Jellyfin will not sign in** | The base URL must be reachable from this machine and contain no credentials, query or fragment. Check the server URL, account, certificate and server version; errors are shown on the sign-in page. |
| **Settings stopped saving** | A newer-version or corrupt `preferences.json`/`profiles.json` is preserved read-only instead of being overwritten. Read the message, back up the file, and follow the migration policy in [HANDOFF-2026-10-27.md](HANDOFF-2026-10-27.md) section 6. |
| **The gate cannot find references** | `out\native-reference\` is local, gitignored evidence and is absent on a clean clone. The gate needs the stored RPCS3 frames and named-state references; see HANDOFF section 8. |

## 9. Verify the install

At minimum: build succeeds, the launcher starts the app, the cold boot reaches the XMB, a category
move works and the clock is live. For a full regression check on a machine that already has the local
reference frames and `dev_flash`:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --all
& .\work\release-check.ps1
```

Both are documented in [HANDOFF-2026-10-27.md](HANDOFF-2026-10-27.md): the gate covers six layer
crops, a pinned canonical capture and four named states, and the release check adds the key scan,
the firmware/data scan and the documentation-link check. A clean clone without those local artifacts
can still build and run, but it cannot reproduce the gate.
