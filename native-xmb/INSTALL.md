# Install, build and run

One-page install and run guide for the native C++ XMB recreation: a Windows machine that has never
built the project, the output-folder convention, every command-line option, the `--script` token
language, the data directory, the environment switches, Steam and Jellyfin, and the failures people
actually hit.

Status: read on 2026-10-28 against the tree at `f45cb5c`. The option list and the `--script` tokens in
sections 4 and 5 were checked against `src/main.cpp` and the modules named in each row; the data layout
in section 6 against `src/preferences.cpp`, `src/profiles.cpp`, `src/jellyfin_client.cpp` and
`src/steam_owned.cpp`. Re-read the source before trusting a row in a later session.

Current handoff and gate state: [HANDOFF-2026-10-28.md](HANDOFF-2026-10-28.md).

The application is a Windows x64 desktop program built with Qt 6, OpenGL 3.3 core and the MSVC
toolchain. It is not a firmware replacement and it ships no Sony assets: it reads a local PS3 4.93
`dev_flash` directory that you supply, and it never writes to it.

## 1. What must exist on the machine

| Requirement | Notes |
|---|---|
| Windows 10 or 11, x64 | The build is Windows-only: the crash handler, XInput/HID input, DPAPI, `WM_APPCOMMAND` volume keys and the display-state read-back are Win32. There is no Linux or macOS target. |
| Visual Studio 2022 with **Desktop development with C++** | `build.ps1` finds it through `vswhere` and requires the x64 VC tools. CMake and Ninja come from the same Visual Studio install. The tree is built with MSVC 14.4x. |
| Qt 6.12, MSVC 2022 64-bit kit | The tree is built and verified with **Qt 6.12.0** (`C:\Qt\6.12.0\msvc2022_64`). `CMakeLists.txt` accepts Qt 6.8 or newer, but 6.12.0 is the only version this tree has been run on, so an older kit is untested. Needs Widgets, OpenGLWidgets, Multimedia, Concurrent and Network, plus `windeployqt`. `build.ps1` looks for `<drive>:\Qt\<version>\msvc2022_64`, newest first; pass `-QtRoot` for another kit. |
| FreeType | Either an installed FreeType CMake package or the Qt SDK's bundled FreeType targets (`Qt6BundledFreetype` / `Qt6FreetypePrivate`). CMake prefers an installed package. |
| Microsoft Visual C++ 2015-2022 x64 Redistributable | The build deploys Qt with `--no-compiler-runtime`, so the target machine supplies the CRT. |
| A GPU and driver with OpenGL 3.3 core | The app requests a 3.3 core profile with 4x MSAA. Software rasterizers, some remote sessions and very old drivers will fail to show a window. |
| A local PS3 4.93 `dev_flash` directory | Read-only, and the one thing the build never supplies. Take it from your own console, or from an RPCS3 install that already has the firmware installed (`<rpcs3>\bin\dev_flash`); `run-native-xmb.ps1` defaults `-FlashRoot` to `..\rpcs3\bin\dev_flash`, a sibling of this repository. The app reads `data\font`, `vsh\resource\*.rco`, `vsh\resource\qgl\*.qrc` and `vsh\resource\explore\xmb\*.xml` there; the launcher checks for `vsh\resource\explore_plugin_full.rco`. Keep it outside the repository. |
| Optional: `dev_hdd0` | Installed titles and saves. Defaults to the directory next to `dev_flash`; override with `--dev-hdd0`. |
| Optional: decrypted VSH modules (`*.prx`) | Only needed for tables the plugins carry (ribbon rest tables, OSK layout). `dev_flash` holds them encrypted (`*.sprx`). Without them the cloth starts flat and settles, and the OSK falls back to its committed table. |
| Optional: Bun 1.x | Only for the verification harness (`work/*.ts`). Not needed to build or run. |
| Optional: Steam client | For installed titles, collections, downloads, accounts and the friends helper. |
| Optional: Steamworks SDK public headers | Only to build the signed-in Steam friends helper (`-SteamworksHeaders <sdk/public/steam>`). The SDK itself is not redistributed. |
| Optional: a Jellyfin server | Only for the Jellyfin service. The app is the client. |

Nothing in this list is downloaded by the build, and nothing derived from the firmware is copied into
source control. A clone without `dev_flash` can still build; it cannot run.

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

Products in the output folder:

| File | What it is |
|---|---|
| `re-ps3-native.exe` | The application, plus the deployed Qt runtime. `/DEBUG` is on and `re-ps3-native.pdb` sits beside it for the crash handler's symbolized stack. |
| `xmb-reference-frames.exe` | Helper that samples timestamped frames and a contact sheet from a recording you already have (`xmb-reference-frames recording.mp4 <out-dir> [interval-ms] [start-ms] [end-ms]`). Local evidence only; it is not part of the app. |
| `xmb-steam-client.exe` | Only with `-SteamworksHeaders`. The signed-in friends helper the app launches; no Steam DLL is redistributed. |

Useful parameters:

```powershell
# Another Qt kit
& .\native-xmb\build.ps1 -QtRoot C:\Qt\6.12.0\msvc2022_64

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

`--dev-flash` is required by anything that opens a window or reads firmware. Without it the app
prints its usage and exits with code 2. The options that do not need it (the Steam reports,
`--pad-report`, `--pad-check`, `--p3t-dump`, `--import-apps`) are listed in section 4.7.

```powershell
# Launcher: defaults -FlashRoot to the sibling rpcs3\bin\dev_flash and writes session logs
& .\run-native-xmb.ps1 -FlashRoot C:\path\to\dev_flash

# Same thing directly
& .\out\native-xmb\re-ps3-native.exe `
    --dev-flash C:\path\to\dev_flash `
    --data-dir  C:\path\to\xmb-data

# Skip the cold-boot intro (useful when debugging)
& .\out\native-xmb\re-ps3-native.exe --dev-flash C:\path\to\dev_flash --data-dir C:\path\to\xmb-data --no-intro

# Appliance mode: frameless fullscreen on the second screen, straight to the XMB
& .\out\native-xmb\re-ps3-native.exe --dev-flash C:\path\to\dev_flash --kiosk --display 1
```

- `--dev-flash` - the local firmware directory (read-only).
- `--dev-hdd0` - installed-title data; defaults to the directory next to `dev_flash`.
- `--data-dir` - writable state; defaults to `<exe folder>\data`. The launcher uses
  `out\native-xmb\data`.
- `--vsh-modules` - local directory of decrypted `*.prx` modules for data tables the plugins carry.
- `--no-intro` - start at the XMB instead of the 4.93 cold boot.

Run the executable from its own folder (or use the launcher) so Qt finds the deployed platform and
image plugins. The first launch creates the data directory and the default preferences.

## 4. Command-line reference

Every option `src/main.cpp` registers is listed here (41 at `f45cb5c`, plus `--help`/`--version`);
`--help` prints the same set with the same one-line descriptions. A row marked **exits** writes its
file and quits without opening a window.

### 4.1 Paths and data

| Option | Argument | What it does |
|---|---|---|
| `--dev-flash` | directory | The local 4.93 firmware root (read-only). Required by anything that opens a window or reads firmware; section 4.7 is the exact split. |
| `--dev-hdd0` | directory | Installed-title data. Default: `..\dev_hdd0` next to `dev_flash`. |
| `--vsh-modules` | directory | Decrypted `*.prx` modules for tables the RCOs do not carry. |
| `--data-dir` | directory | Writable state (section 6). Default: `<exe folder>\data`. |

### 4.2 Window and session

| Option | Argument | What it does |
|---|---|---|
| `--kiosk` | - | Appliance mode: one frameless fullscreen window on the chosen display, straight to the XMB (the cold boot is skipped). It writes no window state, so leaving the flag restores the stored window. |
| `--display` | index | Screen index for `--kiosk`, `0` = first screen. Needs `--kiosk`; an index outside `0..screens-1` exits 2. Mutually exclusive with `--start-windowed`. |
| `--start-windowed` | - | Start in a window even when the stored preference is fullscreen. Local testing; ignores the stored state for this run only. |
| `--no-intro` | - | Start at the XMB instead of the 4.93 cold boot. Also implied by `--script`, `--background-clock`, `--measure-reference` and `--kiosk`. |

### 4.3 Deterministic capture

These exist so a frame can be reproduced and scored; a normal launch never sets them.

| Option | Argument | What it does |
|---|---|---|
| `--render-clock` | seconds | Fixes the background, ribbon, particle and icon-material animation time. Non-negative number; anything else exits 2. |
| `--background-clock` | ISO date/time | Fixes the background and icon-material time through the scene clock while the UI clock stays live. Invalid ISO exits 2. |
| `--ui-clock` | ISO date/time | Fixes the displayed date/time. It never changes the host clock. Invalid ISO exits 2. |
| `--focus-time` | milliseconds | Fixes the focused label's age for glow and idle-fade captures. Non-negative integer; anything else exits 2. |
| `--script` | tokens | Replays input and saves framebuffer grabs. Section 5. |

### 4.4 Controllers

| Option | Argument | What it does |
|---|---|---|
| `--pad-report` | json-file | **exits.** Opens the HID watcher for 2.5 s and writes the attached PlayStation controller's kind, connection, battery percent and charging state. Exits 0 when a controller reported, 1 when none did. Hardware path. |
| `--pad-check` | json-file | **exits.** Runs five documented synthetic DualSense/DualShock 4 reports (USB, Bluetooth and a deliberately truncated one) through the exact parser the watcher uses, and writes the results with `verifiedHardware: false`. Needs no hardware. |
| `--motion-report` | json-file | **exits.** Samples the PAF easing curves at 60/120/240 Hz for the two animation modes and writes them. Needs `--dev-flash`. |

### 4.5 Steam reports

Each writes JSON and exits. None needs `--dev-flash`. `--steam-root` overrides local discovery for all
of them; `XMB_STEAM_ID` selects an account (section 7). The owned-games report, and the owned half of
the library report, want a Web API key (section 8).

| Option | Argument | What it writes |
|---|---|---|
| `--steam-root` | directory | Not a report: overrides the Steam installation directory for this run. |
| `--steam-report` | json-file | Discovered installed Steam titles with their install directories and artwork paths. |
| `--steam-library-report` | json-file | The owned library organised by the account's own collections, with install and launch handoffs. |
| `--steam-collections-report` | json-file | The account's collections and their app ids. |
| `--owned-games-report` | json-file | Games the account owns, installed or not, with playtime and artwork URLs. |
| `--steam-store-report` | json-file | The public store catalogue (top sellers by default; see `--steam-store-query`). |
| `--steam-store-query` | term | Free-text store search for `--steam-store-report`. |
| `--steam-friends-report` | json-file | Friends and presence, or an explicit connection error. |
| `--steam-downloads-report` | json-file | Steam's download and update queue with byte counts and states. |
| `--steam-accounts-report` | json-file | Remembered local accounts, the active one, and the switch plan the app would follow. |
| `--steam-family-report` | json-file | Steam Families membership plus the local `family-shared.json` list. |
| `--steam-artwork-report` | json-file | Per-game artwork overrides found under `<data>\artwork`. |

### 4.6 Firmware and theme readers

Local analysis, all **exits**. Everything they write is firmware-derived or theme-derived: keep the
output outside the repository, exactly like `dev_flash`.

| Option | Argument | What it does |
|---|---|---|
| `--inventory` | json-file | Every XMBML document, view and item the local firmware defines, with parse errors. Exits 1 when a document failed to parse. |
| `--rco-tree` | spec | The RCO object model (objects, animations, typed properties) of the named containers: `<name>[,<name>...][@file]`, or `*` for every `vsh\resource` RCO. Without `@file` it prints to the log. |
| `--rco-text` | spec | The English strings of the named containers: `<name>[,<name>...]@<file>`. |
| `--rco-images` | text-file | The image names of every `vsh\resource` RCO, one container per line. |
| `--export-image` | spec | Exports images as PNG: `container:name[,container:name...]@directory`. |
| `--p3t-dump` | spec | `theme.p3t@directory`: lists the theme's elements in `elements.txt` and writes its decoded background, preview, icons and sounds. Needs no `--dev-flash`. |
| `--font-report` | text-file | Which firmware font face renders each sample string's glyphs. Exits 2 with "Cannot load firmware font faces" when `data\font` is wrong. |
| `--icon-sheet` | image-file | A contact sheet of the original QGL normal maps. |
| `--month-sheet` | image-file | A contact sheet of the `qgl/lines` month day/night textures. |
| `--measure-reference` | text-file | Renders the background offscreen and writes statistics comparable to [audit/VIDEO-REFERENCES.md](audit/VIDEO-REFERENCES.md), then exits. |
| `--decode-fp` | spec | Decodes captured RSX fragment programs into named parameter values: `archive:lib/x.fpo@directory`. |
| `--dump-cg` | directory | Translates the original Cg programs to GLSL in a local folder. |
| `--import-apps` | json-file | Validates and merges a games/service-app manifest into `<data>\apps.json`, then exits. Reimporting the same `id` updates the row. Needs no `--dev-flash`. |

### 4.7 What needs `--dev-flash`

Needs it: the window, `--inventory`, `--motion-report`, `--export-image`, `--rco-text`,
`--rco-images`, `--rco-tree`, `--month-sheet`, `--icon-sheet`, `--font-report`, `--decode-fp`,
`--dump-cg`, `--measure-reference`.

Does not need it: `--help`, `--version`, `--pad-report`, `--pad-check`, `--p3t-dump`, `--import-apps`
and every report in section 4.5. `--data-dir` is only needed by the ones that write under the data
directory (`--import-apps`, and the artwork/family reports that read it).

## 5. `--script` tokens

`--script` replays a comma-separated token list into the real input path, so a capture exercises the
same handlers a controller does. One token is applied every 300 ms, starting 2.5 s after launch;
commas are the separator, so no token value can contain one. A scripted run also gates live controller
input (the same gate as `XMB_INPUT_SCRIPT_ONLY=1`), so the script - not a resting stick - decides what
the shell sees.

```powershell
# A pinned capture of the PS system menu, then the volume HUD and a notification
& .\out\native-xmb\re-ps3-native.exe --dev-flash C:\path\to\dev_flash `
    --render-clock 30 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00 `
    --focus-time 2000 --script "pshold,pause=1200,grab=C:/tmp/psmenu.png,quit"
& .\out\native-xmb\re-ps3-native.exe --dev-flash C:\path\to\dev_flash `
    --render-clock 30 --script "NOTIFY=Hello|Detail,VOLSET=35,pause=1000,grab=C:/tmp/hud.png,quit"
```

### 5.1 Keys

`LEFT`, `RIGHT`, `UP`, `DOWN`, `ENTER`, `ESC`, `PGUP`, `PGDN`, `BACKSPACE`, `PREV`, `NEXT`,
`SHUFFLE`, `REPEAT`, `ZOOMIN`, `ZOOMOUT`, `RESETVIEW`, `ROTATELEFT`, `ROTATERIGHT`, `REFRESH`,
`TRIANGLE`, `F10`.

- `F10` is the keyboard PS button: it opens the PS system menu, exactly like holding PS on a pad.
- `TRIANGLE` (the T key) opens or closes the selected item's options panel.
- `ps` and `pshold` are separate tokens for the PS button tap (controller indicator) and hold (system
  menu), because a key event cannot express a hold.

### 5.2 Actions

| Token | Effect |
|---|---|
| `full` | Go fullscreen through the shell's own path. |
| `size=WxH` | Leave fullscreen and resize the window. `W` 800-7680, `H` 450-4320; anything else logs "invalid size" and is skipped. |
| `idle=ms` | Set the idle age the Auto-Off logic sees, for a capture of the dark-display state. |
| `pause=ms` | Hold the script clock for `ms` without consuming tokens, so real time can pass inside a run (an idle Auto-Off, a media timeout). Whole milliseconds, 0-600000; a larger value is rejected with "invalid pause token". |
| `grab=<path>` | Save the framebuffer as PNG and write `<path>.json`, the state sidecar (category, depth, selection, entry, media, subtitles, page kind, notification, display-off request, window state, pad-gate counters). |
| `burst=<prefix>[@KEY]` | Press `KEY` (optional) and save the next 12 frames at 12 ms as `<prefix>_NN_<ms>ms.png`. |
| `type=<chars>` | Send each character as a key press (OSK text entry). |
| `NOTIFY=<title>[\|<detail>[\|<icon>]]` | Raise a notification through the same path a real event uses. The icon is a `tex_notification_<name>` suffix (default `info`) or the full texture name. |
| `VOLUP`, `VOLDOWN`, `MUTE` | The couch volume path: the same handler a Windows media key reaches. |
| `VOLSET=<percent>` | Set the master volume directly, whole percent 0-100. Any other value logs "invalid VOLSET token" and is skipped. |
| `VOLKEY=<up\|down\|mute\|vkup\|vkdown\|vkmute>` | Windows only: post the real `WM_APPCOMMAND` or `VK_VOLUME_*` message to the window, so the native filter is exercised rather than the Qt handler. |
| `VOLRESTORE` | Restore the volume the run started with. |
| `quit` | Quit the application. |

## 6. Where preferences, logs, caches and artwork live

`<data>` below is the `--data-dir` value (default `<exe folder>\data`).

| Path | What it is |
|---|---|
| `<data>\preferences.json` | Versioned settings, **document version 2**: `{"version":2,"active":"<profile id>","values":{...}}`. Per-user keys are stored under `profile/<id>/`; console-wide keys stay bare. A version-1 file is migrated once, in memory, to the reserved legacy profile and rewritten as version 2. Written atomically (`QSaveFile`). |
| `<data>\profiles.json` | Local users, version 1: `{"version":1,"profiles":[{"id","name"}],"activeId":"..."}`. Ids are lower-case UUIDs without braces, names are unique and at most 16 characters, and at most 16 profiles can store settings. Preferences are per profile; the app reports rather than repairs a corrupt file. |
| `<data>\settings.ini` | Two jobs. The window geometry (`window/geometry`) is written here on exit unless `--kiosk` is running. The first native foundation's preference keys (`appearance/colour`, `library/photo`, `library/music`, `library/video`) are read from it once, into `preferences.json`; the file itself is deliberately left on disk. |
| `<data>\apps.json` | The imported app/service registry written by `--import-apps` (version 1). |
| `<data>\family-shared.json` | Hand-written list of Steam Family titles to add to the library (`{"apps":[{"id":620,"title":"Portal 2"}]}`). |
| `<data>\artwork\` | Per-game artwork overrides: `<appid>.png` (wide row), `<appid>_icon.png`, `<appid>_bg.png`; `.jpg`, `.jpeg` and `.webp` also work. |
| `<data>\cache\steam\` | Cached Steam header images (`<appid>.image`). Safe to delete; it is refetched. |
| `<data>\jellyfin\images\` | Jellyfin poster and backdrop cache, keyed by the server's own image tag. Capped at 400 files; the oldest are trimmed. Safe to delete; it is refetched. |
| `<data>\jellyfin\session.bin` | The remembered Jellyfin session, **only if you chose Remember on the sign-in page**. It is the access token encrypted with Windows DPAPI for this user; the password is never stored. Delete the file (or sign out) to forget it. |
| `%APPDATA%\re-ps3-native\steam_api_key` | Optional one-line Steam Web API key. Outside the repository and outside the data directory. |
| `<exe folder>\crash.log` | Crash stack (exception code plus symbolized frames) when `XMB_LOG` is not set. The `.pdb` next to the executable gives the symbols. |
| `<data>\diagnostics\` | Reserved for a diagnostic bundle. **No flag writes it today**: the bundle is a design in [audit/DIAGNOSE.md](audit/DIAGNOSE.md), not an implemented option. |
| `out\native-xmb\sessions\<timestamp>\native.stdout.txt` and `native.stderr.txt` | Per-run output from the launcher. |

Preferences keys an uninstall or a backup should keep: `media/resume` holds one record per title
(`path`, `ms`, `dur`, `title`, `at`), so deleting `preferences.json` loses resume points. Deleting
`profiles.json` loses the local user list.

Never put a credential in a log. The Steam key is never written to preferences, reports, manifests or
logs; the Jellyfin token is either in memory or DPAPI-sealed in `jellyfin\session.bin`, and stream
requests go through the loopback gateway.

## 7. Environment switches

Switches are read from the launch environment. A boolean is on when it is set and not `0`, except
where the row says only `1` is accepted.

### 7.1 Input and determinism

| Variable | Effect |
|---|---|
| `XMB_INPUT_SCRIPT_ONLY=1` | Withhold every live controller sample for the whole run, exactly as `--script` does. Use it when a capture needs determinism but the input comes from elsewhere. |
| `XMB_INPUT_TRACE=1` | One line per input decision: `source=script\|pad\|keyboard action=<name> accepted=<0\|1>`, plus the gate's own line. This is how a capture proves which source moved the cursor. |
| `XMB_PAD_FAKE=<tokens>` | Pretend a controller is held, so the live path and the scripted gate can be proved without hardware. Comma-separated: `up`, `down`, `left`, `right`, `cross`, `circle`, `square`, `triangle`, `l1`, `r1`, `start`, `select`, `l3`, `r3`, `ps`, and `lx=`, `ly=`, `rx=`, `ry=`, `l2=`, `r2=` with floats (`+ly` is down). A scripted run ignores it too, which is the point. |
| `XMB_INSTALL_REFRESH_MS=<ms>` | Shorten the install-refresh delay for scripted checks. |

### 7.2 Logs and traces

| Variable | Effect |
|---|---|
| `XMB_LOG=<file>` | Append every Qt message to that file. Also receives the crash stack; without it the stack goes to `crash.log`. |
| `XMB_FRAME_TRACE=<file>` | Write a bounded frame trace on orderly shutdown; summarize with `bun work/frame-report.ts <file>`. |
| `XMB_DUMP_ENTRIES=<prefix>` | Write the harness's structural dumps: `<prefix>`, and `<prefix>.folder.json`, `.dl.json`, `.store.json`, `.options.json`, `.settings-<page>.json`, `.settings-choices.json`. This is the named-state oracle. |
| `XMB_INTRO_GRAB=<dir>@<seconds,...>` | Save the frame as drawn the first time the intro clock passes each listed time, as `<dir>\intro_<seconds>.png`. |
| `XMB_BG_SCENE=<png>` | Save the composed background scene once. |
| `XMB_MOTION_LOG=1` | Log the focused column's and the depth's eased positions each frame while they move. |
| `XMB_NOTIFY_LOG=1` | Log the notification composite's own curve each frame while it runs. |
| `XMB_MEDIA_TRACE=1` | Log media queue decisions and photo-viewport zoom/pan. |
| `XMB_RIBBON_FFD_SWEEP=<file>` | Write the day deformation lattice for `_Time` 0..100 in 0.01 steps. |
| `XMB_LABEL_LIMIT=<px>` | Override the row label elision width (default 1050 when the row has no value column, 640 when it has one). |

### 7.3 Rendering A/B and module traces

| Variable | Effect |
|---|---|
| `XMB_TEXT_MATERIAL=1` | Trace the text material: the firmware `Text` block (shadow 4/-5 at RGB .25, alpha 1) and the shadow blend the renderer applies. |
| `XMB_TEXT_PHASE=0` | Restore whole-output-pixel glyph origins instead of per-phase tiles. |
| `XMB_TEXT_MASK=0\|1` | Force the per-glyph alpha-over fast path (`0`) or the coverage-union mask for every string (`1`). Default is auto. |
| `XMB_TEXT_SHADOW=premultiplied` | Read the shadow as premultiplied colour/alpha instead of the shipped attenuated multiply. |
| `XMB_TEXT_QT=1` | Bypass the FreeType rasterizer and use Qt's glyph path. |
| `XMB_TEXT_CHECK=1` | Run the text renderer's self-check and log every line. |
| `XMB_GIM_TRACE=1` | Print the GIM header, palette and decode result for every picture. |
| `XMB_BESTIMAGE_TRACE=1\|all\|<name>[,<name>]` | Trace the `bestImage` fallback: which container supplied the picture and whether the same-art allow-list accepted it. |
| `XMB_ICON_PRESET_LINEAR=1` | Restore the earlier single day/night icon-material schedule instead of the traced clock keyframes. |
| `XMB_ICON_DEBUG=0..3` | Draw that texture unit at the icon's coordinates instead of the result. |
| `XMB_ICON_FIRMWARE=1` | Force the firmware glass path for every icon. |
| `XMB_ICON_UCODE=<capture>` | Use the firmware's own constants from an RSX capture for everything except the refraction input. |
| `XMB_ICON_LOG_MATERIAL=1`, `XMB_ICON_LOG_CONSTANTS=1` | Log the resolved material and the vertex constants. |
| `XMB_UI_GLARE=<scale>` | Add the UI layer to the glare pass at that scale (the measured-then-rejected experiment). |
| `XMB_PARTICLE_LOG=1` | Log the particle pool state and its FNV-1a hash for determinism checks. |
| `XMB_OSK_LAYOUT_LOG=1` | Trace the OSK layout loader: panel geometry, cells, key objects, palette and the fallback decision. |
| `XMB_P3T_TRACE=1` | Print the parsed theme/scene facts through the `--p3t-dump` path. |

### 7.4 Services

| Variable | Effect |
|---|---|
| `STEAM_WEB_API_KEY` | The Steam Web API key for this run (alternative to the key file). |
| `XMB_STEAM_ROOT` | Steam installation directory (the `--steam-root` option sets this). |
| `XMB_STEAM_ID` | Select a numeric Steam ID when the local account is ambiguous. |
| `XMB_STEAM_WEB_ONLY=1` | Skip the signed-in client helper and use the Web API only. |
| `XMB_STEAM_ARTWORK=0` | Do not fetch Steam header artwork. |
| `XMB_STEAM_ARTWORK_LOG=1`, `XMB_STEAM_DOWNLOADS_LOG=1`, `XMB_SUBTITLE_LOG=1`, `XMB_VOLUME_LOG=1`, `XMB_NOTIFY_LOG=1`, `XMB_AUDIO_LOG=1`, `XMB_AUDIO_TRACE=1`, `XMB_PS_LOG=1`, `XMB_XMBML_LOG=1` | Service-specific diagnostic lines: artwork (including overrides), downloads, sidecar subtitles, volume (including swallowed duplicates), notifications, audio (the `_TRACE` alias accepts only `1`), the PS-button watcher, and XMBML resolution. |
| `XMB_DEV_FLASH` | Fallback firmware path for the particle layer when it is initialized without the assets service. The `--dev-flash` argument is the normal source; do not set both. |

## 8. Point it at Steam

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
- **Headless reports** (no window): section 4.5. All take an output JSON path and exit.

Purchases, downloads and launches stay in Steam: the XMB hands `steam://install/<id>` to the client,
starts installed titles with `steam.exe -applaunch <id>` and never writes to Steam's files.

## 9. Point it at Jellyfin

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

4. **Remember** is opt-in. Off, the session token lives in memory only and ends with the process. On,
   the token (never the password) is sealed with Windows DPAPI for the current user in
   `<data>\jellyfin\session.bin`, and the next launch reuses it until the server refuses it or you
   sign out. A refused remembered session is forgotten and the sign-in page returns.

Posters and backdrops are fetched through the authenticated image endpoint into
`<data>\jellyfin\images\`, keyed by the server's tag and capped at 400 files.

`examples\media-apps.json` adds Jellyfin plus Plex, YouTube and Spotify as browser URL apps; only the
Jellyfin entry is native. The registry schema and limits are in
[audit/CONTENT-REGISTRY.md](audit/CONTENT-REGISTRY.md); the service behaviour and verification are in
[audit/JELLYFIN-NATIVE.md](audit/JELLYFIN-NATIVE.md).

## 10. Controller and keyboard bindings

Input comes from the keyboard, XInput pad 0, and PlayStation controllers over HID (DualShock 3 on
USB; DualShock 4 and DualSense on USB or Bluetooth). The left stick works as the D-pad. Cross
confirms and Circle goes back, exactly as XInput A and B do.

| Action | Keyboard | Controller |
|---|---|---|
| Navigate columns and rows | Arrow keys; PageUp/PageDown move 10 rows | D-pad or left stick |
| Confirm | Enter | Cross / XInput A |
| Back / cancel | Escape or Backspace | Circle / XInput B |
| Item options (Triangle) | T | Triangle / Y |
| PS system menu | F10 | PS button, held |
| Full screen | F11 | - |
| Refresh library / folder | F5 | Refresh Game Library row |
| Play / pause media | Space | - |
| Previous / next media | `[` / `]` | - |
| Shuffle / repeat | M / R | - |
| Master volume | Volume media keys | L2 / R2 pressure ramp |
| Photo previous / next, zoom, reset | Q / E, `+` / `-`, `0` | - |
| Skip the cold-boot intro | Any key | Any button |

The PS button watcher is focus-independent: a hold (about one second) brings the XMB forward from a
running title and opens the system menu. Controller pairing, rumble and battery reporting belong to
Windows/HID, not the app; the battery and transport the app parses are reported by `--pad-report`.
There is no remapping screen yet.

## 11. Troubleshooting

| Symptom | Check and fix |
|---|---|
| **Black window, or it exits immediately** | Run the executable from its output folder or through the launcher, so Qt finds `platforms\qwindows.dll`. Confirm the GPU driver supports OpenGL 3.3 core, and avoid remote-desktop/VM sessions. Start with `XMB_LOG=<file>` and read the last lines. Try `--no-intro`. Check that `--dev-flash` points at the firmware root, not the RPCS3 install root. |
| **Launcher says "Pass -FlashRoot pointing to your local PS3 dev_flash directory"** | The path is missing `vsh\resource\explore_plugin_full.rco`. Point at the `dev_flash` folder itself. |
| **Missing fonts, boxes or tofu instead of labels** | Confirm `dev_flash\data\font` exists and the path is right. FreeType must be deployable: use the Qt SDK's bundled targets or an installed FreeType package, and make sure the matching DLL was deployed. `--font-report <file>` exits 2 with "Cannot load firmware font faces" when the directory is wrong. Compare with [audit/FONT-MATCHING.md](audit/FONT-MATCHING.md). |
| **No Steam library** | Installed titles need a valid Steam root (`--steam-root` if not standard). Owned titles also need `STEAM_WEB_API_KEY` or `%APPDATA%\re-ps3-native\steam_api_key`; without it the column states that a key is needed. Check `--steam-report` and `--owned-games-report`; use `XMB_STEAM_ID` if the account is ambiguous. A key is optional for collections, downloads and accounts. |
| **No audio** | Check the Windows default output device and its volume, the in-app volume (default 60) and mute, and the file's codec. Qt Multimedia needs a working Windows audio backend. Interaction cues additionally need the decoded RCO sounds from `dev_flash`. Set `XMB_AUDIO_LOG=1` for details. |
| **LNK1104: cannot open `re-ps3-native.exe`** | A previous instance is still running. Stop `re-ps3-native.exe` (Task Manager or `Stop-Process -Name re-ps3-native`) and build again. For parallel sessions use a separate `-OutputDirectory`; antivirus can also briefly hold a just-written executable. |
| **`--dev-flash` exits with code 2** | The option is required for anything that opens a window or reads firmware. Pass the local firmware directory, or use one of the options section 4.7 lists as not needing it. |
| **`--kiosk` exits with code 2** | `--display` needs `--kiosk`, the index must be a real screen, and `--kiosk` and `--start-windowed` cannot be combined. The console message names which rule failed. |
| **Build cannot find Qt** | Pass `-QtRoot` to a Qt 6.12 `msvc2022_64` kit that has Widgets, OpenGLWidgets, Multimedia, Concurrent and Network. |
| **Build cannot find Visual Studio** | Install Visual Studio 2022 with the Desktop C++ workload; `vswhere` must find the x64 VC tools. |
| **Jellyfin will not sign in** | The base URL must be reachable from this machine and contain no credentials, query or fragment. Check the server URL, account, certificate and server version; errors are shown on the sign-in page. A remembered session the server no longer accepts is dropped and the page returns. |
| **Settings stopped saving** | A newer-version or corrupt `preferences.json`/`profiles.json` is preserved read-only instead of being overwritten. Read the message, back up the file, and follow the migration policy in [HANDOFF-2026-10-27.md](HANDOFF-2026-10-27.md) section 6 (still the reference; the code is `src/preferences.cpp` and `src/profiles.cpp`). |
| **The gate cannot find references** | `out\native-reference\` is local, gitignored evidence and is absent on a clean clone. The gate needs the stored RPCS3 frames, the named-state references and a build the thresholds file points at; see [HANDOFF-2026-10-27.md](HANDOFF-2026-10-27.md) section 8 and [HANDOFF-2026-10-28.md](HANDOFF-2026-10-28.md) section 4. |
| **Only one screen or the wrong screen in kiosk** | `--display 0` is the first screen in Qt's own order, which can differ from the Windows numbering when a display is cloned or disabled. `--kiosk` logs the chosen display and its size. |

## 12. Verify the install

At minimum: build succeeds, the launcher starts the app, the cold boot reaches the XMB, a category
move works and the clock is live. For the full release gate on a machine that already has the local
reference frames and `dev_flash`:

```powershell
# Product gate: stored pair + canonical capture + named states. Exits non-zero on a layer failure.
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --all

# Fidelity corpus report: which cells still have no matched reference. Report only.
& "$env:USERPROFILE\.bun\bin\bun.exe" work\parity.ts --references-missing

# The release check: the gate above plus the credential, firmware-data, dump, link and stale-commit scans.
& .\work\release-check.ps1

# Report mode on a docs-only tree or a fresh clone (skips the two harness checks that need a build):
& .\work\release-check.ps1 -SkipBuild
```

`work\release-check.ps1` runs the layer gate as a gate (a non-zero exit fails the run), treats
`parity.ts --references-missing` as a report that never fails the run, and fails on any credential,
firmware-data, dump, broken-link or dead-commit finding. `-WhatIf` prints the plan and the exact
harness commands without running them. A clean clone without the local artifacts can still build and
run, but it cannot reproduce the gate. [HANDOFF-2026-10-28.md](HANDOFF-2026-10-28.md) section 9 is the
release and tagging checklist.
