# XMB Manager

The XMB Manager is the desktop companion to the native XMB: a Qt tool that finds your emulators,
imports ROMs, disc images and game folders into the XMB's own app library, and presents every
import as a virtual PlayStation 3 package (`<data>/pkg/<id>/` with `ICON0.PNG`, `PARAM.SFO` and
`pkg.json`). It writes the same version-1 `apps.json` the XMB already reads, so an imported title
appears in the Game column as an ICON0 tile and launches through the emulator you chose. It runs as
a window (`xmb-manager.exe`) or as a scriptable CLI; both paths call the same core.

Read this with [INSTALL.md](INSTALL.md) for the XMB itself and, on the registry and the in-XMB
editor this tool is the fuller sibling of, [audit/CONTENT-REGISTRY.md](audit/CONTENT-REGISTRY.md),
[audit/APP-EDITOR.md](audit/APP-EDITOR.md) and [audit/XMB-TOOLING.md](audit/XMB-TOOLING.md). The
core/CLI design notes and the byte-level PARAM.SFO layout are in
[manager/README.md](manager/README.md); the XMB's side of the handoff is in section 9 here.

Status: written 2026-11-04 against `native-xmb/manager/` as it landed, then re-read and re-run
after the core gained tags, collections management, the platform map, ROM fingerprints, launch
checking and backup/restore. The CLI transcript and the XMB load proof in sections 7.4 and 14 are
from this pass; every field name and limit was re-read from `manager_core.h/.cpp`,
`manager_emulators.h/.cpp`, `manager_scanner.h/.cpp`, `manager_package.h/.cpp` and
`manager_cli.cpp` in the same pass. Re-read the source before trusting a row in a later session.

## 1. What it does, and what it refuses to do

What it does:

- Detects installed emulators from `<ID>_DIR`/`XMB_EMULATOR_ROOTS` overrides, portable folders under
  a dev/games/emulators-style parent, the known install folders and the Windows uninstall registry,
  and stores the executable it actually found as a profile.
- Scans files and folders for games (31 extensions plus PS3 package folders, with ISO/folder
  sniffing), cleans file names into titles, and finds sidecar artwork (`ICON0.PNG`, `boxart.png`,
  cover art next to the ROM).
- Imports each selected game as a virtual package under the XMB data directory, writes an
  `apps.json` row that launches the emulator with the ROM as one argument, and optionally records
  the game in a collection. `--emulator` is optional: without it the platform map resolves the
  emulator (section 6.5).
- Keeps an emulator profile catalogue: built-ins for thirteen emulators, overridable one field at
  a time from `emulators.json` or the UI, plus a seeded platform-to-emulator map.
- Organises the library: tags per package, named collections (set, rename, remove, list with
  counts), and a transactional bulk edit over many packages at once (section 10).
- Fingerprints every ROM (SHA-256, size and mtime), verifies the recorded hashes later, and relinks
  a package to a moved or renamed ROM (section 11).
- Checks a registration end to end: prints the exact argv, or launches the registered emulator with
  a timeout and reports pid, exit code and captured output (section 12).
- Backs the whole library up to one portable JSON bundle and restores it, with optional path
  remapping (section 13).
- Lists, validates and removes packages, and exports one JSON report of the whole library.

What it deliberately does not do:

- **It never fabricates `EBOOT.BIN`.** A virtual package is presentation and metadata: the launch
  target in `apps.json` is the emulator executable, not a disc image or an eboot.
- **It does not decrypt, patch or strip NPDRM.** A retail encrypted PS3 package is refused with
  its header evidence (section 8); nothing is copied or modified.
- **It never launches a shell.** The argument template is lexed into an argv list and handed to the
  XMB (and, in the UI's test launch, to `QProcess` with that list). There is no `system()`, no
  `cmd.exe`, and no shell interpolation anywhere in the manager, so a ROM path with spaces or
  quotes stays one argument.
- **It uses no network.** No module in `manager/` includes Qt Network; detection is local files and
  the registry, and everything it writes stays under the data directory.
- **It does not touch the firmware.** `dev_flash` is not needed to build or run the manager.
- **It does not invent installs.** Detection reports only executables that exist; a profile with no
  executable says "(not configured)" rather than guessing a path.

## 2. Build and install

The manager is a second executable in the same CMake project as the XMB:

```powershell
# Builds re-ps3-native.exe, xmb-reference-frames.exe and xmb-manager.exe into one folder.
& .\native-xmb\build.ps1

# A side build, to keep a manager test away from a running XMB
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\manager-build
```

Requirements are the same as the XMB's (section 1 of [INSTALL.md](INSTALL.md)): Windows x64, Visual
Studio 2022 with the C++ workload, and a Qt 6.8+ MSVC kit with Widgets, Gui and Core. The manager
does **not** need OpenGL, Multimedia, Network, FreeType or `dev_flash`. Because the target builds
into the same output folder as the XMB, `windeployqt` on `re-ps3-native.exe` deploys the Qt runtime
that both executables share; `xmb-manager.exe` uses `Qt6Core`, `Qt6Gui` and `Qt6Widgets` from that
folder. Copy the whole folder, not the executable alone.

Optional, off by default: `-DXMB_MANAGER_BUILD_TESTS=ON` builds `xmb-manager-tests.exe`, the
headless core/CLI test runner under `manager/tests/`. It is not part of a normal build.

## 3. First run

The manager opens on **Library** with five tabs: **Library**, **Import**, **Emulators**,
**Diagnostics** and **Settings**.

1. **Set the data folder.** The header shows the active data directory and which emulator was
   detected (or that none was); the Library tab counts the packages. Default: `<manager exe
   folder>\data` - the same folder `re-ps3-native.exe` uses by default when both executables sit in
   one build folder. If the XMB was launched with a different `--data-dir`, or through
   `run-native-xmb.ps1`, point the manager at that folder with **Browse...** or by editing the path.
   **Use this folder** adopts `<manager exe folder>\data` when the manager sits beside the app.
   Changing the folder never moves existing packages: the old folder keeps them (section 15 has the
   mismatch symptom).
2. **Find emulators.** Open **Emulators**, press **Detect installed**. The manager checks the
   `<ID>_DIR`/`XMB_EMULATOR_ROOTS` overrides, portable folders, the known install folders and the
   uninstall registry (section 6.4), writes `emulators.json` when it finds something, and shows the
   evidence line for each hit. Nothing is invented: emulators that are not installed stay listed
   with no executable.
3. **Add or fix a profile.** Select a row and fill in the executable, working directory, platforms
   and argument template, or **Add profile** for a custom one. The template preview and
   **Test launch** run the same expansion the import uses. Put the ROM path in as `{rom}`; section 6
   explains the placeholders and quoting.
4. **Scan.** Open **Import**, drop files or a folder on the drop zone (or use **Add files...** /
   **Add folder...**). The scan table shows title, platform, kind, size, matched emulator and art.
5. **Import.** Tick the rows, choose a collection if you want one, and press **Import selected**
   (or **Import real .pkg...** for a readable PS3 package). Each
   imported row becomes `pkg/<id>/` plus an `apps.json` entry. Failures are listed with the core's
   exact message; a duplicate is reported as a duplicate, not silently overwritten. Re-importing
   with `--replace` (or **Re-import** / Save in the edit dialog) updates the package; if the new
   import carries no collection, the old collection membership is cleared, so the record and
   `collections.json` stay in step. **Edit** on a selected library row changes its title,
   collection, emulator and extra arguments.
6. **Refresh the XMB.** Start (or return to) the XMB and press **F5**, or use the **Refresh Game
   Library** row. The XMB loads `apps.json` at refresh and at startup; it does not watch the file
   (section 15).

The UI covers the whole workflow: **Library** (search, tag chips, collection panel, grouping by
collection/platform/emulator, tags, artwork, test launch with the exact argv, hash verify, relink,
edit, remove), **Import** (drag-and-drop, per-row emulator defaulted from the platform map,
per-row results), **Emulators** (profiles, detect installed, live argument preview, sample launch),
**Platform map** (platform -> emulator table, per-platform argument overrides, unmapped warnings),
**Diagnostics** (validate, verify hashes, mapping gaps, tag/collection checks, report path) and
**Settings** (data folder, per-platform defaults, sample ROM path, UI reset). Backup and restore
live in the File menu (Ctrl+B / Ctrl+Shift+B). Every UI action has a headless CLI equivalent
(section 7 and sections 10-13); the CLI is what the transcripts in this document use.

**Settings** holds the data folder, per-platform default emulators (used to preselect the emulator
in the Import table), a sample ROM path for template previews, and a reset for UI preferences.
The UI's own preferences - window layout, default emulators, collection field, sample ROM - live in
the same `manager.ini` next to the executable, under UI-owned keys; resetting them never touches
packages, registrations or emulator profiles. **Diagnostics** prints the resolved paths, the
registry and package counts, every package state and the problems `validate` would report, and can
copy that report to the clipboard.

The CLI covers the same core without a window (section 7): useful for scripted imports and for
checking a data folder without opening either application.

## 4. Where the manager writes

`<data>` is the active data directory. `manager.ini` is the exception: it always sits next to the
manager executable, because it is how the executable remembers which data directory to use.

| Path | Written by | What it is |
|---|---|---|
| `<manager exe folder>\manager.ini` | `saveSettings`, the UI | `[manager]` / `dataDirectory=<absolute path>`. Written whenever `--data-dir` is passed (CLI and UI) or the folder is changed in Settings. A missing file means the default `<exe folder>\data`. |
| `<data>\apps.json` | import, remove (`ContentRegistry` upsert/remove) | The XMB's version-1 registry (section 5). Validated by the XMB's own reader: at most 1 MiB, at most 1000 entries, unique ids. |
| `<data>\pkg\<id>\ICON0.PNG` | import | The tile artwork: the discovered art scaled to at most 512x288, or a generated placeholder when no art was found. |
| `<data>\pkg\<id>\PARAM.SFO` | import | A real SFO v1.1: `TITLE`, `CATEGORY` (`HG`), `APP_VER` (`01.00`), `ICON0` (`ICON0.PNG`). |
| `<data>\pkg\<id>\pkg.json` | import | The manager's own record, schema `xmb-virtual-pkg/1` (section 5.1). |
| `<data>\emulators.json` | detect-emulators, the UI | User emulator profiles, version 1 (section 6.3). A missing file means "built-ins only". |
| `<data>\platform-map.json` | `--map-platform`, the UI's platform defaults are UI-only | The effective platform -> emulator map, schema `xmb-platform-map/1` (section 6.5). A missing file means the seeded defaults in code. |
| `<data>\collections.json` | import with `--collection`, `--set-collection`, rename/remove, bulk | Collection membership, schema `xmb-collections/1`. **The XMB does not read this file today** (section 9.4). |
| `<file you name>` | `--export-report`, `--backup` | The JSON report and the portable backup bundle. Deliberately wherever you point them, not implicitly in the data folder. |
| `<data>\pkg\.staging-*`, `<data>\pkg\.backup-*`, `<data>\pkg\.trash-*` | import, remove | Short-lived transaction directories. A crash can leave one behind; it is safe to delete. |
| `%TEMP%\xmb-manager-pkg-art\` | scan, import-pkg | The icon extracted from a readable `.pkg` while it is being scanned. Safe to delete; it is re-extracted. |

Other applications that share the folder keep their own files: `preferences.json`,
`profiles.json`, `artwork\`, `cache\`, `jellyfin\` and so on. The manager reads and writes none of
them. It never opens `dev_flash`, the Steam directory or a ROM for writing.

## 5. The registry row and the virtual package

An imported game is one `apps.json` entry plus one package directory. This is the shape the manager
writes (an illustration with shortened paths; the rows this pass captured are in section 14.1):

```json
{
  "id": "pkg-snes-5d1f0f6f0f6b0c21",
  "title": "Chrono Test",
  "kind": "game",
  "category": "game",
  "executable": "C:/Emulators/RetroArch/retroarch.exe",
  "workingDirectory": "C:/Emulators/RetroArch",
  "artwork": "C:/dev/out/manager-e2e/data/pkg/pkg-snes-5d1f0f6f0f6b0c21/ICON0.PNG",
  "arguments": ["-L", "snes9x_libretro", "C:/Games/SNES/Chrono Test (Europe).sfc"]
}
```

Notes that matter when reading or hand-editing it:

- `kind` is always `game` and `category` `game`, so the XMB puts the row in the Game column.
- `executable` is the **emulator**, never the ROM; `arguments` carries the expanded template and
  therefore the ROM path. The XMB's writer (the importer) requires the executable to exist and, on
  Windows, to end in `.exe`; its reader only normalizes the path, so a row whose emulator moved is
  reported by `--validate` instead of invalidating the whole registry.
- `artwork` is the package's own `ICON0.PNG`, so deleting the package directory also loses the
  tile art (the row then falls back to the default Game icon).
- Paths are absolute (the XMB's reader resolves relative paths against `apps.json`, but the manager
  always writes absolute ones).
- The XMB validates the row: unknown fields are rejected, `id` is `[A-Za-z0-9_.-]`, `arguments`
  must be an array of strings. Hand-editing a key the schema does not know makes the XMB ignore the
  **whole registry** with an error page, so prefer the manager or the in-XMB App Editor.

### 5.1 `pkg.json` (`xmb-virtual-pkg/1`)

```json
{
  "schema": "xmb-virtual-pkg/1",
  "id": "pkg-snes-5d1f0f6f0f6b0c21",
  "title": "Chrono Test",
  "platform": "SNES",
  "romPath": "C:/Games/SNES/Chrono Test (Europe).sfc",
  "romKind": "sfc",
  "emulatorId": "retroarch",
  "arguments": ["-L", "snes9x_libretro", "C:/Games/SNES/Chrono Test (Europe).sfc"],
  "collection": "SNES Classics",
  "tags": ["favorite"],
  "art": "C:/dev/out/manager-e2e/data/pkg/pkg-snes-5d1f0f6f0f6b0c21/ICON0.PNG",
  "romSize": 4194304,
  "romMtime": "2026-11-04T23:10:41Z",
  "romHash": "9f2c...64 hex characters ...",
  "romHashLabel": "full-sha256-v1",
  "createdAt": "2026-11-04T23:12:07Z",
  "sourceNote": "sidecar artwork: Chrono Test (Europe).png"
}
```

`id` is derived, not random: `pkg-<platform slug>-<16 hex digits>` where the digits are FNV-1a over
the canonical, case-folded absolute ROM path. Re-importing the same ROM therefore targets the same
id - the normal path refuses it as a duplicate, and `--replace` updates it in place. `romKind` is
the lower-case extension for a file and `folder` for a PS3 package folder; a real readable `.pkg`
imports with `romKind` `pkg` and `platform` `PS3`. `sourceNote` records where the title, the art or
the package metadata came from, so a generated icon is distinguishable from real artwork. `tags`
is the package's own tag list (at most 32 characters per tag, deduplicated case-insensitively).
`romSize`, `romMtime` and `romHash`/`romHashLabel` are the ROM identity record (section 11); all of
them are optional in a hand-written `pkg.json` and simply report "missing hash" when absent.

The package set is round-tripped: `--list`, `--validate` and `--export-report` read `pkg.json`, and
a malformed one is reported per package (`invalid`) instead of breaking the rest of the library.

## 6. Emulator profiles

### 6.1 Built-ins

Thirteen built-ins ship in `manager_emulators.cpp`. Their executable and working directory stay
empty until detection or a user edit supplies them; the platform list and template are always
present.

| id | Name | Platforms | Template |
|---|---|---|---|
| `rpcs3` | RPCS3 | PS3 | `{rom}` |
| `retroarch` | RetroArch | PS1, PS2, PSP, GameCube/Wii, SNES, NES, N64, Genesis, GBA, Game Boy, Game Boy Color, Saturn, Dreamcast, Arcade, NDS | `-L {core} {rom}` |
| `pcsx2` | PCSX2 | PS2 | `{rom}` |
| `dolphin` | Dolphin | GameCube/Wii | `-e {rom}` |
| `duckstation` | DuckStation | PS1 | `{rom}` |
| `ppsspp` | PPSSPP | PSP | `{rom}` |
| `cemu` | Cemu | Wii U | `-g {rom}` |
| `ryujinx` | Ryujinx | Switch | `{rom}` |
| `xenia` | Xenia | Xbox 360 | `{rom}` |
| `mame` | MAME | Arcade | `{rom}` |
| `mednafen` | Mednafen | PS1, Saturn, NES, SNES, Genesis, Game Boy, Game Boy Color | `{rom}` |
| `melonds` | melonDS | NDS | `{rom}` |
| `desmume` | DeSmuME | NDS | `{rom}` |

Import fails when the scanned platform is not in the chosen profile's `platforms`. An `.iso` is
classified from the ISO9660 system identifier first (PS3/PSP/PS2/GameCube-Wii), then from a
platform token in its path (`PS2`, `SNES`, ...), and only then falls back to `Disc image`. An
empty or marker-less `.iso` in a neutral folder therefore needs a profile that lists `Disc image`;
the same file under `...\roms\PS2\` classifies as PS2 (verified both ways, sections 7.4 and 14.1).

### 6.2 Placeholders and argv lexing

| Placeholder | Value |
|---|---|
| `{rom}` | absolute canonical ROM (or `.pkg`) path |
| `{title}` | the display title written to the registry |
| `{id}` | the derived package id |
| `{dir}` | the ROM's directory |
| `{core}` | a libretro core name for RetroArch (`defaultCoreFor`), empty for other emulators or an unmapped platform |

The template is lexed into an argv list; values are substituted **after** lexing. A whitespace-
separated token is one argument, a ROM path with spaces stays one argument, and single or double
quotes group literal text. There are no escape sequences and no shell interpretation. An unknown
placeholder, an unterminated quote, an unmatched `}` or a token longer than 4096 characters is an
error - the import fails and writes nothing. `{core}` expands to an **empty argv element** for a
non-RetroArch emulator or an unmapped platform (it is not an error; RetroArch will complain itself
about a missing core). The mapping is: PS1 `mednafen_psx_libretro`, PS2
`pcsx2_libretro`, PSP `ppsspp_libretro`, GameCube/Wii `dolphin_libretro`, SNES `snes9x_libretro`,
NES `mesen_libretro`, N64 `mupen64plus_next_libretro`, Genesis `genesis_plus_gx_libretro`, GBA
`mgba_libretro`, Game Boy and Game Boy Color `gambatte_libretro`, Saturn
`mednafen_saturn_libretro`, Dreamcast `flycast_libretro`, Arcade `mame_libretro`, NDS
`melonds_libretro`.

### 6.3 `emulators.json`

```json
{
  "version": 1,
  "emulators": [
    {
      "id": "rpcs3",
      "name": "RPCS3",
      "executable": "C:/dev/rpcs3/bin/rpcs3.exe",
      "workingDirectory": "C:/dev/rpcs3/bin",
      "platforms": ["PS3"],
      "argumentTemplate": "{rom}",
      "env": {},
      "builtIn": true
    },
    {
      "id": "mysnes",
      "name": "Snes9x",
      "executable": "C:/Emulators/Snes9x/snes9x-x64.exe",
      "workingDirectory": "C:/Emulators/Snes9x",
      "platforms": ["SNES"],
      "argumentTemplate": "{rom}",
      "env": {"SDL_VIDEODRIVER": "windows"}
    }
  ]
}
```

The field is `argumentTemplate`, not `template`. A user entry with a built-in id replaces that
built-in row field for field; a new id is appended. The file is limited to 128 profiles, ids are
`[A-Za-z0-9_.-]` up to 64 characters, and `builtIn` is documentation of origin - a known built-in
id still loads as built-in. `env` is handed to the emulator when the manager test-launches it;
the XMB's launch path does not apply it (the XMB starts the executable with the arguments and
working directory only).

### 6.4 Detection

`--detect-emulators` (and the UI's **Detect installed**) resolves each built-in in this order:

1. **Explicit overrides.** `<ID>_DIR` (for example `RPCS3_DIR`), then the semicolon-separated
   `XMB_EMULATOR_ROOTS` list. Each is scanned for the emulator's executable names directly, in a
   `bin\` child, and one level down; the evidence line says `environment RPCS3_DIR` or
   `XMB_EMULATOR_ROOTS <path>`.
2. **A portable folder under a drive-root parent.** For each drive, a directory whose name contains
   `dev`, `emulation`, `emulators`, `emulator`, `games`, `retro`, `apps`, `tools`, `portable` or
   `programs` is searched for a child directory whose name contains the emulator id. This is the
   rule that finds this machine's `C:\dev\rpcs3\bin\rpcs3.exe` (evidence: `portable folder
   C:/dev/rpcs3`).
3. **Known install roots.** `%ProgramFiles%`, `%ProgramFiles(x86)%`, `%ProgramW6432%`,
   `%LOCALAPPDATA%`, `%APPDATA%`, `%LOCALAPPDATA%\Programs`, `%USERPROFILE%\Applications`,
   `%USERPROFILE%\Emulators`, and `<drive>:\Emulators`, `<drive>:\Emulation`, `<drive>:\Games`,
   `<drive>:\RetroArch-Win64` for each drive, each checked for the emulator's folder names and
   executable names.
4. **The uninstall registry.** HKCU, HKLM and WOW6432Node, matched on `DisplayName`, then a usable
   `InstallLocation`, `DisplayIcon` or `UninstallString`.

Only an existing executable is ever reported. Detection never overwrites a profile the user already
configured for an emulator it did not find, and it writes `emulators.json` only when at least one
install was found.

One sharp edge remains: **the executable name is part of the rule** - a folder called `rpcs3` with
no `rpcs3.exe` in it or in `bin\` is not reported. Detection is a convenience, not a requirement.

> Fixed 2026-11-04 during final grading: a root passed without a trailing separator used to be
> scanned one level *up* (`QFileInfo(directory).dir()` is the parent), so `RPCS3_DIR=C:\dev` missed
> `C:\dev\rpcs3\bin\rpcs3.exe` while `C:\dev\` found it. The scan now lists the directory itself;
> verified with `RPCS3_DIR=C:\dev` reporting `C:/dev/rpcs3/bin/rpcs3.exe` with evidence
> `environment RPCS3_DIR`.

Detection is a convenience, not a requirement: set the executable by hand in `emulators.json` or
the Emulators tab for anything it misses.

### 6.5 The platform map

A scanner platform has to become an emulator somehow. The manager resolves it in this order:

1. `<data>\platform-map.json` (schema `xmb-platform-map/1`, section 6.5.1), including the seeded
   defaults that ship in code;
2. the first profile whose `platforms` list names the platform, case-insensitively.

An explicit map entry wins even when the mapped profile does not list the platform (the map is the
user's override), and that is what lets a synthetic `Disc image` import work: map it to an emulator
and `--import` will use it without a per-profile platform list. `--list-mappings` prints the whole
table with the source of each row (`platform-map` or `profile`) and whether its executable is
configured:

```text
> xmb-manager.exe --data-dir <data> --list-mappings
Platform mappings (20 mapped, 1 unmapped):
3DS -> (unmapped) (none, not configured)
Arcade -> mame (platform-map, not configured)
Disc image -> discimage (platform-map, configured)
...
```

The seeded defaults (17 rows) are: `Arcade` -> mame, `Dreamcast` -> retroarch, `Game Boy` ->
retroarch, `Game Boy Color` -> retroarch, `GameCube/Wii` -> dolphin, `GBA` -> retroarch, `Genesis`
-> retroarch, `N64` -> retroarch, `NDS` -> melonds, `NES` -> retroarch, `PS1` -> duckstation,
`PS2` -> pcsx2, `PS3` -> rpcs3, `PSP` -> ppsspp, `Saturn` -> mednafen, `SNES` -> retroarch,
`Switch` -> ryujinx. Everything else resolves through a profile that lists the platform (`Wii U` ->
cemu, `Xbox 360` -> xenia, and a custom `Disc image` profile if you keep one). `3DS` is the
documented gap (no built-in emulator lists it).

`--import` without `--emulator` uses this resolution and logs `(emulator <id>, platform map)`; with
`--emulator` the explicit profile wins and the map is not consulted. If the mapped emulator has no
usable executable the import fails with the profile's own message, so a seeded map never turns into
a silent mislaunch.

#### 6.5.1 `platform-map.json`

```json
{
  "version": 1,
  "schema": "xmb-platform-map/1",
  "mappings": [
    { "platform": "SNES", "emulatorId": "retroarch" },
    { "platform": "PS3", "emulatorId": "rpcs3" },
    { "platform": "Disc image", "emulatorId": "discimage" }
  ]
}
```

`--map-platform <platform> <emulatorId>` writes the **effective** map (the seeded rows plus your
change) to this file in one atomic write; re-mapping an existing platform prints `(replaced)`.
There is no CLI to delete a single mapping: delete the row from the JSON, or remove the file to
return to the seeded defaults. The map is intentionally separate from `emulators.json` so a
profile can be renamed or replaced without editing platform rules, and vice versa.

## 7. Command-line reference

`xmb-manager.exe` with no verb opens the UI. Any verb runs headless (no window), prints human
readable text on stdout, and `--json` swaps stdout for one JSON object. Diagnostics always go to
stderr so `--json` stays parseable.

### 7.1 Verbs

Emulators and scanning:

| Verb | Arguments | What it does |
|---|---|---|
| `--list-emulators` | - | Print the merged catalogue: id, name, executable (or `(not configured)`), platforms. |
| `--detect-emulators` | - | Scan for installed emulators, merge hits into `emulators.json`, print notes with the evidence. Writes nothing when nothing is found. |
| `--scan` | `<path...>` | Read-only scan of files or folders; prints title, platform, kind, size and art per game, plus warnings. Needs no data directory. |
| `--import` | `<path...>` | Scan the paths and import every game found. `--emulator <id>` is optional: omitted, the platform map resolves it (section 6.5). Also takes `--collection <name>` and `--replace`. Exits 1 if any game failed. |
| `--import-pkg` | `<file>` | Probe a real PS3 package; import it when its metadata is readable, refuse an encrypted retail package with the evidence (section 8). |

Library:

| Verb | Arguments | What it does |
|---|---|---|
| `--list` | `[--tag <tag>]` | Print `apps.json` rows and every package with its state (`ok`, `orphaned-package`, `missing-package`, `package-only`, `invalid`). `--tag` filters both lists and prints `Filtered by tag '<tag>'`. |
| `--remove` | `<appId>` | Remove the package directory (only a direct child of `<data>\pkg`) and its registration; also drops it from collections. |
| `--validate` | - | Check registry, package set, ROMs, art, `PARAM.SFO`/`ICON0.PNG`, emulator executables, the platform map and tags, and recompute every recorded ROM hash. Exits 1 on any issue (including a missing hash or a changed ROM). |
| `--export-report` | `<file>` | Write the full JSON report (schema `xmb-manager-report/2`: paths, emulators, apps, packages, collections, tags, mappings and hash state). |

Platform map:

| Verb | Arguments | What it does |
|---|---|---|
| `--map-platform` | `<platform> <emulatorId>` | Write the effective platform -> emulator map to `platform-map.json`; re-mapping prints `(replaced)`. |
| `--list-mappings` | - | Print the resolved table with each row's source (`platform-map` or `profile`) and whether its executable is configured. |

Tags and collections:

| Verb | Arguments | What it does |
|---|---|---|
| `--tag` | `<appId> <tag...>` | Add one or more tags (trimmed, <= 32 characters, deduplicated case-insensitively). |
| `--untag` | `<appId> <tag...>` | Remove one or more tags. |
| `--tags` | - | List every tag with its package count. |
| `--set-collection` | `<appId> <name>` | Set the package's collection; an empty name clears it (and removes the membership). |
| `--collections` | - | List collections with counts. |
| `--rename-collection` | `<old> <new>` | Rename a collection and update every member package's `pkg.json`. |
| `--remove-collection` | `<name>` | Remove a collection; members keep their package but lose the membership. |

Bulk, identity, launch and backup:

| Verb | Arguments | What it does |
|---|---|---|
| `--bulk` | `<file.json>` | Apply `{ids, setCollection, setEmulator, addTags, removeTags, remove}` to many packages transactionally. Every id is read and validated first; a bad id aborts the whole request. Exits 1 if any entry failed. |
| `--relink` | `<appId> <newPath>` `[--force]` | Move a package to a new ROM path, re-expanding the argv. A different content hash is refused unless `--force` (section 11). |
| `--verify-hashes` | - | Recompute every recorded ROM hash. Exits 1 on a mismatch or a missing ROM. |
| `--launch-check` | `<appId>` `[--launch-timeout <sec>]` | Launch through the registered emulator with an argument list (never a shell), wait up to the timeout, report executable, argv, pid and exit code. Exits 1 when the process fails or times out. |
| `--print-argv` | `<appId>` | Print the exact argv the registration would launch, without launching. |
| `--backup` | `<file.json>` `[--with-packaged-art]` | Write one portable bundle (schema `xmb-manager-backup/1`): apps, packages, tags, collections, the effective platform map and the emulator overlay, optionally the `ICON0.PNG` files. |
| `--restore` | `<file.json>` `[--remap <from>=<to>]` `[--replace]` | Restore a bundle, remapping every path field and argv element. Existing package ids abort the restore before anything is written unless `--replace`. |

### 7.2 Options

| Option | Applies to | Notes |
|---|---|---|
| `--data-dir <dir>` | every verb except `--scan` | **Must be absolute.** Saved to `manager.ini` on the spot, so later verbs without the flag use it. A relative path exits 2. |
| `--emulator <id>` | `--import` only | Case-insensitive profile id; overrides the platform map. A missing or unknown id exits 2 or 1 with the known ids listed. |
| `--collection <name>` | `--import` only | 1-64 characters, no control characters; creates or extends the collection. |
| `--replace` | `--import`, `--restore` | Import: update an existing package for the same ROM instead of refusing it. Restore: overwrite existing package ids instead of aborting on conflict. |
| `--force` | `--relink` only | Accept a ROM whose content hash differs from the recorded one (the summary says it was accepted with `--force`). |
| `--tag <tag>` | `--list` only | Filter the library by one tag. |
| `--launch-timeout <sec>` | `--launch-check` only | Whole seconds; the default is 8. On expiry the process is terminated, then killed. |
| `--with-packaged-art` | `--backup` only | Include each package's `ICON0.PNG` in the bundle. |
| `--remap <from>=<to>` | `--restore` only | Repeatable; applied to every path field and argv element. Beware: it remaps the emulator executable too when that lives under `<from>` (section 13). |
| `--json` | every verb | One JSON object on stdout (schema `xmb-manager/1`; report `xmb-manager-report/2`). Failures still emit `{"ok":false,"error":...}`. |
| `--ui` | none | Force the window; cannot be combined with a verb. |
| `-h`, `--help`; `--version` | - | Usage text (`0`), version string (`xmb-manager 0.1.0`). |

Options take `--name value` or `--name=value`; a repeated option (except `--remap`), an unknown
option once a command is present, two commands, or a stray positional argument is a usage error.
`--` ends option parsing. A command line that carries only unknown options (a typo such as
`--list-emulator` singular) is a usage error: the process prints the verb list and exits 2 instead of
opening the window. **`--ui`** still forces the window, and `--data-dir` is valid in either mode.

### 7.3 Exit codes

| Code | Meaning |
|---|---|
| `0` | The command succeeded. |
| `1` | The operation failed: an import that could not complete, a refused package, a duplicate, an inconsistent `--validate`, a hash mismatch, a failed or timed-out launch, a restore conflict, a bulk request with any failure, an unreadable registry. |
| `2` | Usage or configuration error: an unknown/malformed option once a command is present, a relative `--data-dir`, an unreadable `apps.json` version, `--json` with no command, a missing required argument such as an empty `--set-collection` name. Usage text is printed to stderr. |

### 7.4 Worked example

A scratch data folder, one SNES ROM and one PS2 ISO with spaces in its name:

```powershell
$m = "C:\dev\out\manager-build\xmb-manager.exe"
$d = "C:\dev\out\manager-e2e\data"

# 1. What can detection see? (nothing here: RPCS3 lives outside the scanned roots)
& $m --data-dir $d --detect-emulators

# 2. Profiles, and which platform resolves to which emulator
& $m --data-dir $d --list-emulators
& $m --data-dir $d --list-mappings

# 3. Read-only scan
& $m --data-dir $d --scan C:\dev\out\manager-e2e\fixtures\roms

# 4. Import with an explicit {rom} profile, or let the platform map choose
& $m --data-dir $d --import "C:\dev\out\manager-e2e\fixtures\roms\SNES\Chrono Test (Europe).sfc" `
      --emulator snes9x --collection "SNES Classics"

# 5. Organise, check, identify
& $m --data-dir $d --tag pkg-snes-<id> favorite
& $m --data-dir $d --collections
& $m --data-dir $d --validate
& $m --data-dir $d --verify-hashes
& $m --data-dir $d --print-argv pkg-snes-<id>

# 6. Report, remove, back up
& $m --data-dir $d --export-report C:\dev\out\manager-e2e\report.json
& $m --data-dir $d --remove pkg-snes-<id>
& $m --data-dir $d --backup C:\dev\out\manager-e2e\backup.json --with-packaged-art
```

The exact transcripts this pass captured, including the `--json` shapes and the XMB load proof, are
in section 14. The worked command sequence in section 3 of [INSTALL.md](INSTALL.md) is the same
flow without the scratch paths.

## 8. Real `.pkg` files

The manager reads a PS3 package **header** and any metadata that is readable without decryption:
magic `0x7F504B47`, revision (`0x8000` finalized/retail, `0x0000` non-finalized/debug), type,
metadata offsets, item count, sizes, content id, IV and header digest. It does **not** contain
NPDRM keys and does not attempt decryption.

- **Retail encrypted package** (finalized header, `PARAM.SFO` not readable): refused, exit 1, with
  the header/encryption evidence in the message and `pkg.json`/`apps.json` untouched. This is the
  honest answer, not a bug to route around.
- **Unencrypted or debug package** (readable `PARAM.SFO`): imported as a virtual package with
  `platform` `PS3`, `romKind` `pkg`, the SFO's `TITLE` and its `ICON0.PNG`, launched through the
  configured PS3 emulator (RPCS3 preferred, then any usable profile whose platforms include PS3 or
  whose platform list is empty). The payload is never decrypted; the emulator is handed the `.pkg`
  path exactly like a ROM.
- **A ZIP archive with a `.pkg` name**: recognised and refused with "unpack it and scan the folder
  instead".
- **Readable SFO but no usable PS3 emulator**: refused with "run --detect-emulators or set the
  RPCS3 executable", because the row would otherwise point at nothing.

Files are never modified, moved or copied by this path: the package stays where it is, and only
the virtual package is created under `<data>\pkg`.

## 9. How the XMB consumes it

### 9.1 `refreshContent()`

`Shell::refreshContent()` (`src/shell.cpp`) loads `<data>\apps.json` with `ContentRegistry::load`
on a worker thread, together with the Steam scan. Every app whose `category` matches a column is
added as an entry with id `custom:<app id>`; a `kind: "game"` entry gets `Action::LaunchApp`, the
`game_tex_default_ps3` fallback texture, `e.gameTile = true`, and its artwork decoded from the
`artwork` path (scaled to at most 512x288, the same size the manager writes). The Game column
places imported titles after the installed Steam titles and the Steam Store / All Games provider
rows.

The XMB re-runs this on startup, on **F5** / the **Refresh Game Library** row, after an in-XMB
import, and after an install handoff. It does not watch the file: an import made while the XMB is
open appears after a refresh.

### 9.2 Launch

`Action::LaunchApp` resolves the entry by id from the loaded registry, refuses with a page if the
`executable` is missing, and otherwise calls `startTracked(executable, arguments,
workingDirectory, title)`. The emulator is started with the argv list the manager wrote -
`{rom}` and friends are already expanded, so nothing re-parses a command line.

### 9.3 The in-XMB App Editor is the simpler sibling

The XMB owns a small editor (`Action::ImportApps`, `src/app_editor.cpp`) that edits the same
`apps.json` by hand: one game or web app at a time, a manifest import, working directory,
arguments as one-per-line text. It does not scan, does not detect emulators, does not create
packages or artwork, and cannot validate the whole set. The manager is the full tool for building
the library; the editor remains the in-console fix-up surface. Both write the same registry, so a
row added in one shows up in the other after a refresh.

### 9.4 What the XMB does *not* read

`emulators.json`, `collections.json` and `pkg.json` are manager files. The XMB reads only
`apps.json` (plus its own `artwork\` overrides for Steam). In particular **a collection in
`collections.json` does not group anything in the XMB**: the column shows the imported title as a
plain tile, and the collection name is not drawn anywhere. Collection membership is kept for the
manager (and for a future XMB surface), not for the current column.

## 10. Tags, collections and bulk edits

**Tags** are free-form labels stored in each package's `pkg.json`
(`"tags": ["favorite", "rpg"]`). They are trimmed, at most 32 characters, deduplicated
case-insensitively (the first spelling wins) and never drawn by the XMB (the registry row has no tag
field). `--tag` adds, `--untag` removes, `--tags` lists every tag with its count, and
`--list --tag <tag>` filters the library case-insensitively. A missing `tags` array means "no
tags". The `--tag`/`--untag` confirmation line prints the **resulting** tag set
(`Removed tags for pkg-...: favorite` means "favorite is what remains"), so read it that way. Tags
survive `--replace` and `--relink`; change them deliberately with `--tag`/`--untag`.

**Collections** are named groups. The membership list lives in `<data>\collections.json`
(schema `xmb-collections/1`) and each member's `pkg.json` carries the same `collection` string; the
two are kept in step by `--set-collection`, `--rename-collection`, `--remove-collection`,
`--import --collection`, `--replace` and `--remove`. `--set-collection` moves a package into exactly
one collection (removing it from every other), and an import without `--collection` or
`--set-collection <id> ""` clears the membership. Collections are manager-side organisation only:
section 9.4 explains why the XMB column does not group by them.

**Bulk** applies one edit to many packages in one transaction. The request file is JSON:

```json
{
  "ids": ["pkg-snes-60131a9324f01852", "pkg-ps2-5575ad2b17a05841"],
  "addTags": ["batch"],
  "removeTags": [],
  "setCollection": "Batch",
  "setEmulator": "",
  "remove": false
}
```

Every id is read and validated before anything is written: if one id is unreadable, nothing is
applied and every result row says `Not applied: the bulk request failed validation` (exit 1).
`ids` is the only required field, but at least one operation must be present or the request is
rejected. A successful run reports one line per package
(`ok | <id> | tags=[...]; collection-=Old; collection=New`) and `Bulk applied N, failed 0`.
`setEmulator` re-expands the argv through that profile's template; `remove` deletes the package and
registration for every id (still confined to `<data>\pkg`).

Observed output from a valid request:

```text
> xmb-manager.exe --data-dir <data> --bulk bulk.json
ok | pkg-snes-60131a9324f01852 | tags=[favorite,batch]; collection-=Super Nintendo; collection=Batch
ok | pkg-ps2-5575ad2b17a05841 | tags=[batch]; collection=Batch
Bulk applied 2, failed 0
[exit 0]
```

## 11. ROM identity: hashes, verification and relinking

Every import records a bounded fingerprint in `pkg.json`, so a moved, replaced or tampered ROM is
detectable without keeping a copy:

| Field | Value |
|---|---|
| `romSize` | File size in bytes (0 for a folder package). |
| `romMtime` | ISO-8601 UTC modification time at import. |
| `romHashLabel` | `full-sha256-v1` (files up to 4 GiB), `partial-sha256-v1` (larger files: SHA-256 over decimal size + first 4 MiB + last 4 MiB), `folder-v1` (a PS3 package folder: SHA-256 over the sorted relative file paths and sizes). |
| `romHash` | Lower-case hex digest for that label. |

Memory is bounded by a 1 MiB read buffer, so hashing a large ISO is a streaming pass, not a load.
The fingerprint is change detection, not proof of a good dump; it cannot tell a clean dump from a
bad one, only whether the file changed.

`--verify-hashes` recomputes each recorded hash and prints one state per package:

```text
ok | pkg-ps2-5575ad2b17a05841 |
mismatch | pkg-snes-60131a9324f01852 | Content hash does not match the recorded full-sha256-v1;
use --relink pkg-snes-60131a9324f01852 <path> (--force) to update it
Verify-hashes: 1 checked, 0 ok, 1 failed
```

States are `ok`, `mismatch`, `missing-rom`, `missing-hash` (a hand-written `pkg.json` with no hash
fields) and `unreadable`. Any non-ok state exits 1, including `missing-hash`: the check reports a
gap rather than certifying it.

`--relink <appId> <newPath>` moves a package to a new ROM path, recomputes the fingerprint,
re-expands the profile's argument template (or the recorded platform-map override) and rewrites
`pkg.json` and the `apps.json` row in one transaction. It refuses a different content hash unless
`--force`, and refuses a path already imported as another package:

```text
> xmb-manager.exe --data-dir <data> --relink pkg-snes-... "D:\Roms\Chrono Test (Europe).sfc"
xmb-manager: error: The content hash of 'D:\Roms\...' (full-sha256-v1 95b0...) does not match the
recorded hash of package 'pkg-snes-...' (full-sha256-v1 e3b0...). Use --force to relink anyway.
[exit 1]

> xmb-manager.exe --data-dir <data> --relink pkg-snes-... "D:\Roms\Chrono Test (Europe).sfc" --force
Relinked pkg-snes-... -> D:/Roms/Chrono Test (Europe).sfc (full-sha256-v1 95b0...) with 1 argument
    (accepted with --force)
[exit 0]
```

The package id does not change on relink: identity is the package, not the current path. A relink
to a missing file is refused; a relink to a path whose platform the profile does not support is
refused by the same platform rule as import.
## 12. Launch check and print-argv

`--print-argv <appId>` prints the exact argv the registration would launch, one rendered argument
per line, and with `--json` returns `executable`, `workingDirectory`, `arguments`, `argv` and
`rendered`. It never launches anything.

`--launch-check <appId> [--launch-timeout <sec>]` starts the registered emulator with that argv
through `QProcess` (an argument list, no shell), waits up to the timeout (default 8 s), and reports
the outcome. It exits 0 only when the process started and exited with code 0; a start failure, a
non-zero exit and a timeout all exit 1. On timeout the process is terminated, then killed if it
does not exit. `--json` reports `started`, `finished`, `timedOut`, `terminated`, `killed`, `pid`,
`exitCode`, `exitStatus` (`normal`, `crash`, `timeout`, `spawn-error`), `stdout`, `stderr` and
`spawnError`.

This is the fastest way to prove a profile end to end without opening the XMB: it exercises the
same argv the XMB will hand to `startTracked`. A stand-in emulator that exits non-zero is reported
honestly (the verification run used one and got `exit code: 2`, exit 1 overall).

## 13. Backup and restore

`--backup <file.json> [--with-packaged-art]` writes one portable bundle with schema
`xmb-manager-backup/1`: the manager-owned registrations and their `pkg.json` records, tags,
collections, the effective platform map, the emulator overlay, and (with `--with-packaged-art`) the
`ICON0.PNG` files base64 - each icon only up to 64 KiB, with anything larger counted in
`artOmitted` instead of bloating the file. The summary reports the counts, for example
`Wrote backup.json (4 apps, 4 packages, 1 collections, 18 mappings, 4 art files)`.

`--restore <file.json> [--remap <from>=<to>] [--replace]` restores that bundle into the active data
folder:

- **Merge by default.** A package id that already exists in the target is a conflict; the restore
  reports every conflict and aborts **before writing anything** (`N package(s) already exist ...
  use --replace`), exit 1. With `--replace` the existing package and registration are overwritten.
- **Path remapping.** Each `--remap from=to` (repeatable) rewrites every path field and every argv
  element whose value starts with `from`. This is how a bundle moves between machines, but the
  rule is literal: remapping a prefix that also contains the emulator executable remaps that path
  too, and the restore then fails with `Executable path does not name an existing file` and rolls
  everything back. Remap the ROM prefix, not the whole fixtures tree.
- **Transactional.** Registrations, packages and collections are written through the same atomic
  helpers as import; a failed registration removes what the restore created and prints
  `The restore was rolled back.` Art is restored only for packages whose bundle carried it, and a
  remapped ROM that exists on this machine gets its hash recomputed rather than carried over.

A restore prints one action per row (`emulators | | merged`, `package | <id> | created`,
`registration | <id> | restored`, `collection | | merged`, `summary | |`) and ends with
`Restored <file>` and exit 0.

## 14. Verification this pass ran

Build: `native-xmb/build.ps1`-equivalent CMake configure plus `--target xmb-manager` into
`C:\dev\out\manager-build`, then `windeployqt` on `xmb-manager.exe`. The snapshot verified here is
`native-xmb/manager/` with every file last written at or before 18:39:09 on 2026-11-04 (the CLI/UI
passes stopped there); `xmb-manager.exe` is 18:41:07, 1,316,352 bytes, SHA-256
`13adf60b37ce87cbb5f9f6984de026cb08309933981170f076c9065d633b3123`. The executable ran every verb
below. The complete transcripts, the `apps.json` load proof and the Game-column capture are recorded
here; they live under `C:\dev\out\manager-e2e\`.

### 14.1 CLI transcript

Scratch data folder `C:\dev\out\manager-e2e\data`, synthetic fixtures under
`C:\dev\out\manager-e2e\fixtures\roms` (empty `.sfc`, `.gba` and `.iso` files, a sidecar PNG, and
a PS3-style folder with a real SFO v1.1 `PARAM.SFO` and a `PS3_GAME\ICON0.PNG`). The stand-in
emulator executable in the transcript is a copy of a system tool; the profile shapes are the point,
not the binary. The full transcript is `C:\dev\out\manager-e2e\cli-transcript.txt`; this is the
part that matters.

```text
# 1. Detect installed emulators (known install roots, portable folders, registry)
> xmb-manager.exe --data-dir C:\dev\out\manager-e2e\data --detect-emulators
rpcs3: C:/dev/rpcs3/bin/rpcs3.exe (evidence: portable folder C:/dev/rpcs3)
retroarch: not detected
...
Detected 1 emulator; saved to emulators.json
[exit 0]

# 2. Emulator catalogue after detection
> xmb-manager.exe --data-dir C:\dev\out\manager-e2e\data --list-emulators
Emulator profiles (13):
rpcs3 | RPCS3 | C:/dev/rpcs3/bin/rpcs3.exe | PS3
retroarch | RetroArch | (not configured) | PS1, PS2, PSP, GameCube/Wii, SNES, NES, N64, ...
...
[exit 0]

# 3. emulators.json edited by hand to add two stand-in profiles (the detected rpcs3 is kept)
rpcs3     -> C:/dev/rpcs3/bin/rpcs3.exe                 PS3                 {rom}
snes9x    -> C:/dev/out/manager-e2e/fixtures/emu/...exe  SNES, GBA           {rom}
discimage -> C:/dev/out/manager-e2e/fixtures/emu/...exe  Disc image, PS2     {rom}

# 4. Catalogue after the edit
> xmb-manager.exe --data-dir C:\dev\out\manager-e2e\data --list-emulators
Emulator profiles (15):
rpcs3 | RPCS3 | C:/dev/rpcs3/bin/rpcs3.exe | PS3
...
snes9x | Snes9x | C:/dev/out/manager-e2e/fixtures/emu/standin-emulator.exe | SNES, GBA
discimage | Disc image (stand-in) | C:/dev/out/manager-e2e/fixtures/emu/standin-emulator.exe | Disc image, PS2
[exit 0]

# 5. Read-only scan of the fixture library
> xmb-manager.exe --data-dir C:\dev\out\manager-e2e\data --scan C:\dev\out\manager-e2e\fixtures\roms
Scan found 4 games:
[SNES] Chrono Test
    C:/dev/out/manager-e2e/fixtures/roms/SNES/Chrono Test (Europe).sfc
    sfc, 0 B, art C:/dev/out/manager-e2e/fixtures/roms/SNES/Chrono Test (Europe).png
    note: file .sfc
[GBA] Pocket Quest
    C:/dev/out/manager-e2e/fixtures/roms/GBA/Pocket Quest (USA).gba
    gba, 0 B
    note: file .gba
[PS2] Space Game
    C:/dev/out/manager-e2e/fixtures/roms/PS2/Space Game (USA).iso
    iso, 0 B
    note: file .iso (no platform marker read)
[PS3] Synthetic PS3 Folder
    C:/dev/out/manager-e2e/fixtures/roms/PS3 Folders/Test PS3 Folder
    folder, 0 B, art C:/dev/out/manager-e2e/fixtures/roms/PS3 Folders/Test PS3 Folder/PS3_GAME/ICON0.PNG
    note: PS3 package folder (PS3_GAME/PARAM.SFO)
[exit 0]

# 6. Import a SNES ROM with a {rom} template
> xmb-manager.exe --data-dir ... --import ...\SNES\Chrono Test (Europe).sfc --emulator snes9x --collection "SNES Classics"
xmb-manager: Imported 'Chrono Test' as pkg-snes-60131a9324f01852 (emulator snes9x)
Imported 1 of 1 game
    pkg-snes-60131a9324f01852 | Chrono Test | C:/dev/out/manager-e2e/data/pkg/pkg-snes-60131a9324f01852
[exit 0]

# 7. The same ROM again: duplicate detection refuses it
> xmb-manager.exe --data-dir ... --import ...\SNES\Chrono Test (Europe).sfc --emulator snes9x
xmb-manager: error: Chrono Test: This ROM is already imported as 'pkg-snes-60131a9324f01852'
(Chrono Test). Use --replace to update it.
Imported 0 of 1 game
[exit 1]

# 8. --replace updates the same package id
> xmb-manager.exe --data-dir ... --import ...\SNES\Chrono Test (Europe).sfc --emulator snes9x --replace
xmb-manager: Imported 'Chrono Test' as pkg-snes-60131a9324f01852 (emulator snes9x)
Imported 1 of 1 game
[exit 0]

# 9. Import a GBA ROM
> xmb-manager.exe --data-dir ... --import ...\GBA\Pocket Quest (USA).gba --emulator snes9x
xmb-manager: Imported 'Pocket Quest' as pkg-gba-90f1344a89e77377 (emulator snes9x)
Imported 1 of 1 game
[exit 0]

# 10. Import an ISO whose path contains spaces (the ROM stays one argv element)
> xmb-manager.exe --data-dir ... --import "...\PS2\Space Game (USA).iso" --emulator discimage
Imported 1 of 1 game
    pkg-ps2-5575ad2b17a05841 | Space Game | .../pkg-ps2-5575ad2b17a05841
[exit 0]
apps.json arguments: ["C:/dev/out/manager-e2e/fixtures/roms/PS2/Space Game (USA).iso"]

# 11. Import a PS3 package folder (PARAM.SFO title, ICON0 art)
> xmb-manager.exe --data-dir ... --import "...\PS3 Folders\Test PS3 Folder" --emulator rpcs3
xmb-manager: Imported 'Synthetic PS3 Folder' as pkg-ps3-4b8cac104c092a94 (emulator rpcs3)
Imported 1 of 1 game
[exit 0]

# 12. Import a PS3 .pkg whose metadata is readable
> xmb-manager.exe --data-dir ... --import-pkg ...\fixtures\pkg\readable.pkg
xmb-manager: pkg header: magic 0x7F504B47; revision 0x0000 (non-finalized/debug); type 1; ...
readable PARAM.SFO keys: APP_VER,CATEGORY,TITLE
Imported a virtual package from ...\readable.pkg
xmb-manager: pkg: imported 'Readable Test Package' with emulator 'rpcs3'
xmb-manager: pkg: TITLE and ICON0 came from readable data; the payload itself was not decrypted
[exit 0]

# 13. Refuse an encrypted retail .pkg
> xmb-manager.exe --data-dir ... --import-pkg ...\fixtures\pkg\retail.pkg
xmb-manager: pkg header: magic 0x7F504B47; revision 0x8000 (finalized retail); ...
xmb-manager: pkg: refused (metadata encrypted)
xmb-manager: error: Encrypted retail PS3 package: the header is readable but no PARAM.SFO is
readable without decryption, so no title or icon can be trusted. Nothing was imported and the
file was not modified. Evidence: ...
[exit 1]

# 14. Library after the imports
> xmb-manager.exe --data-dir ... --list
Registrations (5):
pkg-snes-60131a9324f01852 | Chrono Test | game | C:/dev/out/manager-e2e/fixtures/emu/standin-emulator.exe
pkg-gba-90f1344a89e77377 | Pocket Quest | game | C:/dev/out/manager-e2e/fixtures/emu/standin-emulator.exe
pkg-ps2-5575ad2b17a05841 | Space Game | game | C:/dev/out/manager-e2e/fixtures/emu/standin-emulator.exe
pkg-ps3-4b8cac104c092a94 | Synthetic PS3 Folder | game | C:/dev/rpcs3/bin/rpcs3.exe
pkg-ps3-505780ec619a4a0d | Readable Test Package | game | C:/dev/rpcs3/bin/rpcs3.exe
Packages (5, orphaned 0, missing 0):
ok | pkg-snes-60131a9324f01852 | Chrono Test | ...\Chrono Test (Europe).sfc
ok | pkg-gba-90f1344a89e77377 | Pocket Quest | ...\Pocket Quest (USA).gba
ok | pkg-ps3-505780ec619a4a0d | Readable Test Package | ...\readable.pkg
ok | pkg-ps2-5575ad2b17a05841 | Space Game | ...\Space Game (USA).iso
ok | pkg-ps3-4b8cac104c092a94 | Synthetic PS3 Folder | ...\Test PS3 Folder
[exit 0]

# 15. Consistency check
> xmb-manager.exe --data-dir ... --validate
Registry and packages are consistent (0 issues).
[exit 0]

# 16. Remove the GBA package
> xmb-manager.exe --data-dir ... --remove pkg-gba-90f1344a89e77377
Removed package and registration 'pkg-gba-90f1344a89e77377'
[exit 0]

# 17. Consistency check after the removal
> xmb-manager.exe --data-dir ... --validate
Registry and packages are consistent (0 issues).
[exit 0]

# 18. Export the JSON report
> xmb-manager.exe --data-dir ... --export-report C:\dev\out\manager-e2e\report.json
Wrote report to C:\dev\out\manager-e2e\report.json
[exit 0]

# 19. The same library as JSON
> xmb-manager.exe --data-dir ... --list --json
{
    "appCount": 4,
    "apps": [ ... one row per registration, with kind/category/arguments/artwork ... ],
    "command": "list",
    "missingPackages": 0,
    "ok": true,
    "orphanedPackages": 0,
    "packageCount": 4,
    "packages": [ ... state, romPath, romKind, emulatorId, hash fields, tags ... ],
    "schema": "xmb-manager/1"
}
[exit 0]
```

Exit codes seen: 0 on success, 1 on the duplicate and on the refused retail package, 2 on a
relative `--data-dir`, on `--ui` combined with a command, and on `--json` without a command (the
usage text goes to stderr; with no argument at all the window opens instead). `--help` and
`--version` exit 0. The JSON
shapes were checked too: `--list --json` is one object with `schema`, `apps`, `packages`,
`appCount`, `packageCount`, `orphanedPackages`, `missingPackages`; a failed `--remove --json` is
`{"ok":false,"error":...}` with exit 1; the exported report is `xmb-manager-report/2` with the
resolved paths, all fifteen profiles, four apps, four packages, the tag list, the collection list
and the mapping table.

The platform map was exercised on the final build: `--import` without `--emulator` resolved SNES
through the seeded map to `retroarch`, failed honestly because that profile has no executable, then
succeeded after `--map-platform SNES snes9x` (log: `Imported 'Chrono Test' as
pkg-snes-60131a9324f01852 (emulator snes9x, platform map)`).

A second script (`cli-advanced.txt`) exercised the organisation, identity, launch and backup verbs
on the same library. Condensed:

```text
> --list-mappings
Platform mappings (20 mapped, 1 unmapped):
3DS -> (unmapped) (none, not configured)
Arcade -> mame (platform-map, not configured)
Disc image -> discimage (profile, configured)
...
PS3 -> rpcs3 (platform-map, configured)
[exit 0]

> --map-platform Disc image discimage
Mapped platform 'Disc image' to emulator 'discimage'
> --map-platform SNES snes9x
Mapped platform 'SNES' to emulator 'snes9x' (replaced)
[exit 0]

> --tag pkg-snes-... favorite rpg          -> Added tags for ...: favorite, rpg
> --untag pkg-snes-... rpg                 -> Removed tags for ...: favorite
> --tags                                   -> Tags (1): favorite | 1 package
> --set-collection pkg-snes-... "SNES Classics"
> --set-collection pkg-ps2-...  "PS2 Classics"
> --collections                            -> PS2 Classics | 1 package, SNES Classics | 1 package
> --rename-collection "SNES Classics" "Super Nintendo"
Renamed collection 'SNES Classics' to 'Super Nintendo' (1 member updated)
> --remove-collection "PS2 Classics"
Removed collection 'PS2 Classics' (1 member)
[all exit 0]

> --print-argv pkg-snes-...
"C:/dev/out/manager-e2e/fixtures/roms/SNES/Chrono Test (Europe).sfc"
> --launch-check pkg-snes-... --launch-timeout 5
executable: .../standin-emulator.exe
argv: "C:/dev/out/manager-e2e/fixtures/roms/SNES/Chrono Test (Europe).sfc"
pid: 24024
exit code: 2
[exit 1]   (the stand-in exits 2; the manager reports it, it does not fake success)

> --verify-hashes
ok | pkg-ps2-... |
ok | pkg-ps3-... |
Verify-hashes: 4 checked, 4 ok, 0 failed
[exit 0]

> --relink pkg-snes-... "C:/.../Chrono Test Relinked (Europe).sfc"
Relinked pkg-snes-... -> ... (full-sha256-v1 e3b0...) with 1 argument
[exit 0]
> --print-argv pkg-snes-...   (now names the relinked path)

> --backup C:\dev\out\manager-e2e\backup.json --with-packaged-art
Wrote backup.json (4 apps, 4 packages, 1 collections, 18 mappings, 4 art files)
> --restore (into a fresh data folder)
package | pkg-snes-... | created
registration | pkg-snes-... | restored
...
Restored C:\dev\out\manager-e2e\backup.json
[exit 0]
> --validate (restored folder)
Registry and packages are consistent (0 issues).

> --bulk bulk.json   (ids: snes + ps2, addTags: batch, setCollection: Batch)
ok | pkg-snes-... | tags=[favorite,batch]; collection-=Super Nintendo; collection=Batch
ok | pkg-ps2-... | tags=[batch]; collection=Batch
Bulk applied 2, failed 0
[exit 0]
```

Failure paths were exercised as well, each with the documented code:

- `--relink` to a file with a different hash: refused, exit 1, with both hashes and "Use --force".
  `--force` relinked it and printed `(accepted with --force)`.
- A `--bulk` request naming one bad id: the whole request is rejected (`Not applied: the bulk
  request failed validation` for the valid id, `No readable package with this id` for the bad one),
  exit 1.
- `--restore` into a folder that already has the bundle's ids: four `conflict` lines and exit 1,
  before writing anything.
- `--restore --remap C:\...\fixtures=C:\...\elsewhere`: the emulator executable lived under that
  prefix, so `Executable path does not name an existing file` and `The restore was rolled back.`
  (exit 1) - the remap rule is literal, section 13.

Side findings from the runs:

- `--replace` without `--collection` cleared the old collection membership (`collections.json`
  returned to an empty list). Re-importing with the collection keeps it.
- Detection found this machine's portable RPCS3 (`C:\dev\rpcs3`, evidence `portable folder`) and
  wrote `emulators.json`; the step-3 edit then replaced that file with the three profiles the
  transcript uses. `<data>\pkg` was created by the first mutating verb.
- `--data-dir` was persisted on the first run: `C:\dev\out\manager-build\manager.ini` now holds
  `[manager]` / `dataDirectory=C:/dev/out/manager-e2e/data`, and the later verbs in the transcript
  would work without the flag.
- The final `pkg.json` records `romSize`, `romMtime`, `romHash` (`full-sha256-v1`) and `tags`;
  the empty fixture ROMs hash to the well-known SHA-256 of the empty string
  (`e3b0c442...b855`), which is why the hash lines are identical across them.
- No stray `.staging-*`, `.backup-*` or `.trash-*` directory was left behind by any import,
  replacement, relink or removal.

### 14.2 `apps.json` load proof

The XMB's own reader, not a reimplementation, is what proves the file. Two scripted runs against
the scratch data folder (`re-ps3-native.exe --dev-flash ... --data-dir C:/dev/out/manager-e2e/data
--no-intro --render-clock 30 --background-clock 2026-11-04T12:00:00 --ui-clock
2026-11-04T12:04:00 --focus-time 2000`) with `XMB_DUMP_ENTRIES` set and a `--script` list.

`refreshContent()` calls `ContentRegistry::load(<data>/apps.json, ...)`; with `XMB_DUMP_ENTRIES`
pointed at `C:\dev\out\manager-e2e\xmb\entries-final.json`, the Game category of that dump
contained (this capture is after the advanced run, so the SNES package had already been relinked
and tagged):

```json
[
  {"id": "steam-store", "title": "Steam Store", "action": 18, "hasArtwork": false},
  {"id": "steam-folder-all-games", "title": "All Games", "action": 17, "hasArtwork": false},
  {"id": "custom:pkg-snes-60131a9324f01852", "title": "Chrono Test", "action": 20,
   "hasArtwork": true, "container": "explore_plugin_full", "texture": "game_tex_default_ps3"},
  {"id": "custom:pkg-ps2-5575ad2b17a05841", "title": "Space Game", "action": 20,
   "hasArtwork": true, "container": "explore_plugin_full", "texture": "game_tex_default_ps3"},
  {"id": "custom:pkg-ps3-4b8cac104c092a94", "title": "Synthetic PS3 Folder", "action": 20,
   "hasArtwork": true, "container": "explore_plugin_full", "texture": "game_tex_default_ps3"},
  {"id": "custom:pkg-ps3-505780ec619a4a0d", "title": "Readable Test Package", "action": 20,
   "hasArtwork": true, "container": "explore_plugin_full", "texture": "game_tex_default_ps3"}
]
```

`action` 20 is `Action::LaunchApp`, and `hasArtwork: true` is the loader having decoded the
`artwork` path the manager wrote. The GBA row is absent because the removal step deleted it before
this capture - the dump tracked the registry, not a stale file. Tags and collection names are
absent too: `apps.json` has no field for them (section 9.4).

### 14.3 XMB Game-column capture

`C:\dev\out\manager-e2e\xmb\game-column.png` (1920x1065, windowed; the script resized the window
with `size=1920x1080`). Script:
`size=1920x1080,pause=800,DOWN,DOWN,pause=300,grab=...\game-column.png,quit`.

The focused row is the imported SNES title: the state sidecar
(`game-column.png.json`) records `"category": "game"`, `"selection": 2`,
`"entryId": "custom:pkg-snes-60131a9324f01852"`, `"entryTitle": "Chrono Test"`,
`"focusedRow": "custom:pkg-snes-60131a9324f01852"`, `"entryCount": 15`. The frame shows the tile
with the SNES fixture's artwork (the blue card from `ICON0.PNG`), then `Space Game`,
`Synthetic PS3 Folder` and `Readable Test Package` below it; two of those show the manager's
generated placeholder icons because their sources had no readable art. The XMB's Game column
therefore renders the manager's package exactly as it renders an installed title's ICON0.


## 15. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| **`--import-pkg` says the package is encrypted** | A retail NPDRM package. Its metadata cannot be read without keys the manager does not have; nothing was imported. Use an unencrypted/debug package, or import the game's ROM with an emulator profile instead. A ZIP renamed `.pkg` gets its own message: unpack and `--scan` the folder. |
| **The tile shows a dark generated card instead of box art** | No readable sidecar art was found for that ROM. The manager names `ICON0.PNG`, `icon0.png`, `boxart.png`, `cover.png`, and `<rom base>.png/.jpg/.jpeg/.webp` next to the ROM or in the PS3 folder; add one of those and `--import --replace`, or put `ICON0.PNG` in the package folder by hand. The `sourceNote` in `pkg.json` says whether the icon was real or generated. |
| **A ROM path with spaces launches the emulator with the wrong argument** | It should not: the template is lexed first and the value substituted second, so `C:/Games/My Game (USA).iso` is one argv element. Check `arguments` in `apps.json`; if it is split, the profile template quoted or concatenated the value incorrectly. Quote the placeholder in the template (`"{rom}"`) only when the emulator needs the quotes to be part of the argument. |
| **The import says "This ROM is already imported"** | Duplicate detection by canonical ROM path. Re-run with `--replace` (or edit and save the row in the UI) to update it. Re-importing a different platform for the same ROM is refused; remove the old package first. |
| **`--validate` reports `unconfigured-emulator`** | The package's `emulatorId` has no executable on this machine. Run `--detect-emulators`, or set the executable in `emulators.json` / the Emulators tab. The package and the XMB row stay valid; only the launch would fail. |
| **`--validate` reports `orphaned-package` / `missing-package`** | The two halves of a virtual package disagree: a `pkg.json` with no registry row, or a manager-owned registry row with no package directory. `--remove` the id to clean up, or restore the missing half. |
| **The XMB shows no new tile after an import** | The XMB only reloads on startup, F5, or the Refresh Game Library row. Press F5 in the XMB. |
| **The XMB shows the old library even after F5** | The XMB is reading a different data folder than the manager wrote. Compare the data directory in the manager's header with the XMB's `--data-dir` (the launcher's `out\<build>\data`, by default). `--data-dir` is persisted in `manager.ini` next to the manager executable, so a wrong value survives restarts until it is changed. |
| **The manager cannot switch folders** | The new path must be absolute and writable; the UI asks for confirmation because packages stay in the old folder. If `manager.ini` holds a path with control characters or one that is not a directory, the error names the file; delete `manager.ini` to fall back to `<exe folder>\data`. |
| **Detection finds no emulator** | Detection looks at `<ID>_DIR`, `XMB_EMULATOR_ROOTS`, portable folders under a dev/games/emulators-style parent, the known install roots and the uninstall registry (section 6.4). A portable install elsewhere is not found: set the executable in the Emulators tab or `emulators.json`, or point `RPCS3_DIR` (etc.) at the emulator's own folder. A root passed without a trailing separator is scanned one level up, so prefer `<ID>_DIR=C:\path\to\emulator`. |
| **`--import` exits 2 with "Data directory must be an absolute path"** | Relative `--data-dir` values are rejected by design so the persisted `manager.ini` cannot drift with the current directory. Pass an absolute path. |
| **`--import` exits 1 with "does not list platform"** | The scanned platform (often `Disc image` for an `.iso` with no marker) is not in that profile's `platforms`. Pick another emulator or add the platform to the profile. |
| **`--verify-hashes` reports `mismatch`** | The file changed since import (or the path now points at different content). If the change is intended, `--relink <appId> <path> --force`; if not, restore the original file. `missing-hash` means a hand-written `pkg.json` never recorded a hash: re-import with `--replace` to record one. `--validate` reports the same conditions as `package-hash` / `missing-hash`. |
| **`--relink` refuses with "does not match the recorded hash"** | Deliberate: a different ROM is a different game. Pass `--force` only when the new file really is the same title (a re-dump), and note the summary says `(accepted with --force)`. |
| **`--import` says the platform is unmapped or the mapped emulator has no executable** | `--list-mappings` shows the resolution and its source. Map the platform (`--map-platform "Disc image" discimage`), or set the executable on the profile the map names. The seeded map routes most platforms to RetroArch until you configure it. |
| **`--restore` lists `conflict` lines and exits 1** | The target already has those package ids; nothing was written. Add `--replace` to overwrite, or restore into an empty data folder. |
| **`--restore` says `Executable path does not name an existing file` and rolls back** | A `--remap` prefix covered the emulator executable as well as the ROMs. Remap only the ROM/art prefix, or fix the profile after restoring. |
| **`--bulk` fails every row with "Not applied"** | One id in the request was unreadable, so the transaction was rejected before writing. Remove/fix that id and re-run; the per-row message says which one. |
| **`--launch-check` exits 1 although the argv looks right** | The check reports the process, not the game: a non-zero emulator exit, a spawn error or a timeout is a failure by definition. Read `exitCode`/`exitStatus`/`stderr`; the argv is printed so a profile problem is distinguishable from an emulator problem. |
| **`--list` or `--validate` fails on `apps.json`** | The XMB's reader rejected the file (unknown field, bad version, duplicate id, over 1 MiB). The message names the field; edit the file or the in-XMB App Editor, or restore a backup. The manager itself never writes an invalid registry: it validates through the same `ContentRegistry` code before committing. |

## 16. Security and limits

- **No network, no telemetry.** Nothing in the manager opens a socket; there is no HTTP client in
  the target. Scans and imports are local file operations.
- **No shell.** Argument templates are lexed to argv, never concatenated into a command line. The
  UI's Test launch starts the emulator with `QProcess` and an argument list; the XMB's own launch
  path does the same (`startTracked`), so nothing re-parses a command line at either end.
- **Atomic writes.** Every JSON, the SFO and the icon go through `QSaveFile` (write to a sibling
  temporary, then rename). A package is staged in `<data>\pkg\.staging-*`, committed by rename, and
  the previous directory is kept as `.backup-*` until the registry update succeeds; a failed
  registry or collection update rolls the package back (and restores the previous one when it was
  a `--replace`). Removal renames to `.trash-*` and restores the directory if the registry removal
  fails.
- **Removal is confined.** `--remove` accepts only a safe id (`[A-Za-z0-9_.-]`, not `.`/`..`) and
  removes only a direct child of `<data>\pkg`, re-checked with a canonical path containment test;
  the ROM, the emulator and anything else on disk are untouched.
- **Path validation.** An imported row must have an existing absolute `.exe`, an existing working
  directory and an existing artwork file; the XMB's importer repeats those checks (its reader only
  normalizes paths, so a file that moves later does not invalidate the whole registry). A missing
  file later is reported by `--validate`, not repaired silently.
- **Size limits.** `apps.json` keeps the XMB's 1 MiB / 1000-entry ceiling; manager JSON files are
  limited to 4 MiB; `emulators.json` to 128 profiles; tags to 32 characters and collection names to
  64, both trimmed and control-character free; a `PARAM.SFO` read is limited to 1 MiB; a package
  probe reads at most 8 MiB; a scan visits at most 250,000 files, 32 directory levels deep, and
  reports when it stops; ROM hashing streams with a 1 MiB buffer (full SHA-256 up to 4 GiB, a
  size+ends digest above that, a sorted file list for folders).
- **Launch checks are local.** `--launch-check` starts the registered executable with `QProcess`
  and an argument list, captures its output and enforces a timeout; it is the only manager path
  that runs another program, and it runs exactly what `apps.json` names - no environment beyond the
  manager's own and no shell.
- **No fabricated console data.** No `EBOOT.BIN`, no fake `PARAM.SFO` claims beyond the fields the
  manager writes, no decrypted payload, no invented emulator install. A refusal is reported as a
  refusal.
- **Honest limits.** Package ids are stable but not human-readable; collections and tags are not
  drawn by the XMB yet; the UI's artwork policy row is informational (the core stores exactly one
  `ICON0.PNG` per package and has no shared cache to size); `env` from a profile is applied only by
  the manager's own test launch, not by the XMB launch path; a content hash detects change, it does
  not certify a clean dump; the platform map has no single-entry delete (edit the JSON or remove
  the file); `--remap` is a literal prefix rule that can hit the emulator path; restoring a bundle
  from another machine requires the emulator executable and ROM prefixes to exist there; and on
  Windows the manager relies on the XMB's `.exe`-only game rule, so a non-Windows emulator launcher
  is out of scope.
