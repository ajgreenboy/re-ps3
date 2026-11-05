# XMB Manager

The XMB Manager is the companion tool for the native PS3 XMB (`re-ps3-native`). It sets up
emulators, scans ROM/ISO libraries, and imports each game "as if it were a PKG": a small
presentation package under the XMB data directory plus one `apps.json` entry that the XMB's
Game column already knows how to show and launch.

This pass owns the core library and the CLI. The Qt UI is written in a parallel pass against
the interface documented at the end of this file.

Nothing here talks to the network, executes a shell, or touches the firmware, the XMB's
source tree, `work/`, audits or references. The manager writes only under its own data
directory (plus `manager.ini` next to its executable, and a temporary icon cache for readable
`.pkg` files).

## Build

`native-xmb/CMakeLists.txt` adds the `xmb-manager` executable. Its sources are this directory
plus `src/content_registry.cpp` (the apps.json schema is shared, not duplicated). It links
Qt6::Widgets, Qt6::Gui and Qt6::Core with the same MSVC flags as the main target.

The Qt UI pass must add `manager/manager_ui.h` and `manager/manager_ui.cpp`; the CMake target
already lists them. Until they exist, a developer build can remove those two entries from a
temporary copy of the CMake list: `manager_main.cpp` uses `__has_include("manager_ui.h")`, so
the core links without the UI and UI mode prints a clear "this build has no UI" error.

```powershell
# The normal build (also builds re-ps3-native and runs windeployqt on it)
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\manager-build
```

`windeployqt` in `build.ps1` is run only on `re-ps3-native.exe`, but both executables live in
the same output directory, so the deployed Qt DLLs and plugins serve `xmb-manager.exe` too.
Do not copy `xmb-manager.exe` alone to a folder without the Qt runtime.

`XMB_MANAGER_BUILD_TESTS` (default OFF) adds `xmb-manager-tests`, the headless fixture/CLI
test runner:

```powershell
cmake -S . -B <dir> -DXMB_MANAGER_BUILD_TESTS=ON ...   # or a temporary copy of the list
cmake --build <dir> --target xmb-manager-tests
& <dir>\xmb-manager-tests.exe C:\dev\out\manager-dev <dir>\xmb-manager.exe
```

The runner creates fixtures under `C:\dev\out\manager-dev` (or its first argument), runs every
CLI verb and writes `cli-transcript.txt` there. It refuses to delete a scratch directory unless
its name ends in `manager-dev`, or it is a `data` directory inside a `manager-*` folder (for
example `C:\dev\out\manager-org\data`). The runner also writes harmless `cmd.exe /c exit 0`
emulator profiles so `--launch-check` is exercised without ever opening a game window.

## Data layout

`<data>` is the data directory: `--data-dir` if given, otherwise the value persisted in
`manager.ini` next to `xmb-manager.exe`, otherwise `<manager exe folder>\data`. A `--data-dir`
passed on the command line is persisted for the next run.

| Path | Contents |
|---|---|
| `<data>\apps.json` | The native XMB content registry, version 1, exactly the schema `ContentRegistry` reads. The manager only ever writes it through `ContentRegistry::upsert` / `remove`. |
| `<data>\pkg\<id>\ICON0.PNG` | Package icon: the discovered art converted to PNG, at most 512x288 (a generated placeholder is used when no art was found). |
| `<data>\pkg\<id>\PARAM.SFO` | Valid SFO v1.1 with `TITLE`, `CATEGORY` (`HG`), `APP_VER` (`01.00`) and `ICON0` (`ICON0.PNG`). |
| `<data>\pkg\<id>\pkg.json` | Manager record, schema `xmb-virtual-pkg/1` (see below). |
| `<data>\emulators.json` | Emulator profiles merged over the built-ins by id. Missing file means built-ins only. |
| `<data>\collections.json` | Optional named collections: `{"version":1,"schema":"xmb-collections/1","collections":[{"name":"...","ids":["pkg-..."]}]}`. |
| `<data>\platform-map.json` | Optional platform -> emulator map, schema `xmb-platform-map/1` (see below). Missing file means the seeded defaults. |
| `<manager exe folder>\manager.ini` | One key, `[manager] dataDirectory=...`, written atomically. |
| `%TEMP%\xmb-manager-pkg-art\` | Cache of icons extracted from readable `.pkg` files. Safe to delete. |
| any `--backup <file.json>` path | Portable `xmb-manager-backup/1` bundle (see below). Not written under `<data>` unless asked. |

`EBOOT.BIN` is deliberately not fabricated: a virtual package is presentation and metadata,
and the emulator executable named in `apps.json` is the launch target.

### Package JSON

```json
{
  "schema": "xmb-virtual-pkg/1",
  "id": "pkg-snes-0123456789abcdef",
  "title": "Chrono Test",
  "platform": "SNES",
  "romPath": "C:/Games/SNES/Chrono Test.sfc",
  "romKind": "sfc",
  "emulatorId": "retroarch",
  "arguments": ["-L", "snes9x_libretro", "C:/Games/SNES/Chrono Test.sfc"],
  "collection": "SNES Classics",
  "art": "C:/XMB/data/pkg/pkg-snes-0123456789abcdef/ICON0.PNG",
  "createdAt": "2026-11-04T23:12:07Z",
  "sourceNote": "file .sfc",
  "tags": ["Classic", "Co-op"],
  "romSize": 3145728,
  "romMtime": "2026-11-04T22:58:11Z",
  "romHash": "9f2c...64 hex characters ...",
  "romHashLabel": "full-sha256-v1"
}
```

`tags` is optional and defaults to `[]` when absent, so records written by the first
delivery still load. Tags are trimmed, non-empty, at most 32 characters, contain no control
characters and are de-duplicated case-insensitively (the first spelling wins). Tags survive
`--replace`; pass `--tag`/`--untag` to change them deliberately.

`romSize`, `romMtime`, `romHash` and `romHashLabel` are also optional; a record without them
is reported by `--validate` (`missing-hash`) rather than rejected. `romHashLabel` is:

- `full-sha256-v1` - sha256 of the whole file (files up to 4 GiB);
- `partial-sha256-v1` - sha256 over the decimal size, the first 4 MiB and the last 4 MiB
  (files larger than 4 GiB), so hashing stays bounded;
- `folder-v1` - sha256 over the sorted relative file paths and sizes of a PS3 package
  folder (no file contents are read).

Hashing streams through a 1 MiB buffer; it is progress-free but memory-bounded.

Package ids are deterministic: `pkg-<platform slug>-<16 hex chars>`, where the hash is FNV-1a
over the canonical, case-folded ROM path. Re-importing the same ROM with `--replace` updates
the same id, directory and registry entry. Duplicate detection compares canonical ROM paths
across manager-created packages (`pkg.json`), not titles. The `pkg-` id prefix (or an artwork
path under `<data>\pkg`) is how `listPackages` recognises a manager-owned registration; a
manually registered app with such an id is treated as manager-owned.

### PARAM.SFO format notes

PARAM.SFO (PSF) is **little-endian**. The writer implements it byte-exactly:

- Header, 20 bytes: magic `0x46535000` (bytes `00 50 53 46`, "\0PSF"), version
  `0x00000101`, key table start (`u32`), data table start (`u32`), entry count (`u32`).
- Index entry, 16 bytes per key: key offset (`u16`), format (`u16`), length (`u32`), max
  length (`u32`), data offset (`u32`).
- Keys are sorted ascending and stored NUL-terminated in the key table; the key table is
  padded to 4 bytes so the data table starts aligned.
- Values are written as format `0x0204` (UTF-8, NUL-terminated); the slot is padded to a
  4-byte boundary and `max length` is the padded slot size.
- `TITLE` is truncated to at most 127 UTF-8 bytes at a character boundary, so it always fits
  the usual 128-byte PS3 slot with its terminator.
- The reader also accepts format `0x0004` (UTF-8 without terminator) and `0x0404` (uint32,
  returned in decimal). It validates magic, version, table bounds, key bounds and UTF-8.

The test runner compares the writer's output against an independently assembled byte layout
and parses it back.

## Emulator profiles

`EmulatorProfile` has `id`, `name`, `executable`, `workingDirectory`, `platforms`,
`argumentTemplate`, `env`, `builtIn`. Built-ins: RPCS3, RetroArch, PCSX2, Dolphin,
DuckStation, PPSSPP, Cemu, Ryujinx, Xenia, MAME, Mednafen, melonDS, DeSmuME. Their executable
is empty until detection or a user override supplies one. `env` is carried for the UI and
future launch support; `apps.json` has no environment field, so the native XMB launch path
does not receive it.

`detectInstalled()` scans, without inventing anything:

- known install roots: `ProgramFiles`, `ProgramFiles(x86)`, `ProgramW6432`, `LOCALAPPDATA`,
  `LOCALAPPDATA\Programs`, `APPDATA`, `%USERPROFILE%\Applications`, `%USERPROFILE%\Emulators`,
  and `Emulators` / `Emulation` / `Games` / `RetroArch-Win64` on existing drives;
- known executable names inside a matching folder or one directory below a root;
- the uninstall registry (`HKCU`, `HKLM`, `WOW6432Node`) matched by DisplayName, then
  `InstallLocation`, `DisplayIcon` and `UninstallString`.

Every detection records its evidence (known-location directory or registry entry) in the
`notes` returned by the CLI. A path is reported only when the executable exists.

Platform tokens in the path (`PS3`, `PS2`, `PSP`, `SNES`, `GBA`, ...) are also used when an
`.iso` has no other marker. ISO9660 system identifiers (`PS3VOLUME`, `PSPGAME`,
`PLAYSTATION`) and GameCube/Wii disc magic are sniffed from the file itself; the fallback
platform is `Disc image`.

### Argument template rule

Templates use `{rom}`, `{title}`, `{id}`, `{dir}` (the ROM's directory) and `{core}`. The
template is lexed **first** and values are substituted into a token afterwards, so a ROM path
or title containing spaces always stays exactly one argv element. Whitespace separates
arguments; single or double quotes group literal text; there are no escape sequences, no
environment expansion (`%VAR%` and `$VAR` are literal), no globbing and no shell. A value
containing a literal double quote cannot be expressed in the template, but it is passed
verbatim to argv when it comes from a placeholder.

`expandArguments()` returns the argv list (`QStringList`). The brief's prose said "split into
an argv list", so a `QString` return could not satisfy it; `formatArguments()` is provided for
display only.

## CLI

No verb means UI. Any verb (or `--help`, `--version`, `--json`) runs headless with
`QGuiApplication` and never constructs `QApplication`.

```
xmb-manager --list-emulators
xmb-manager --detect-emulators
xmb-manager --scan <dir...>

xmb-manager --import <path...> [--emulator <id>] [--collection <name>] [--replace]
xmb-manager --import-pkg <file>

xmb-manager --list [--tag <tag>]
xmb-manager --remove <appId>
xmb-manager --validate
xmb-manager --export-report <file>

xmb-manager --map-platform <platform> <emulatorId>
xmb-manager --list-mappings

xmb-manager --tag <appId> <tag...>
xmb-manager --untag <appId> <tag...>
xmb-manager --tags
xmb-manager --set-collection <appId> <name|"">
xmb-manager --collections
xmb-manager --rename-collection <old> <new>
xmb-manager --remove-collection <name>

xmb-manager --bulk <file.json>

xmb-manager --relink <appId> <newPath> [--force]
xmb-manager --verify-hashes

xmb-manager --launch-check <appId> [--launch-timeout <sec>]
xmb-manager --print-argv <appId>

xmb-manager --backup <file.json> [--with-packaged-art]
xmb-manager --restore <file.json> [--remap <from>=<to> ...] [--replace]
```

Every verb accepts `--json` (one JSON object on stdout; diagnostics stay on stderr) and
`--data-dir <dir>`. Exit codes: `0` success, `1` operation failed (including an inconsistent
`--validate`, a refused `.pkg`, a refused relink, a failed launch, or any failed import in a
batch), `2` usage/configuration error.

`--import` scans the paths, creates one virtual package per game and reports per-game failures.
A batch keeps the successes and exits `1` if anything failed. When `--emulator` is omitted the
emulator is resolved per platform (see below); a platform with no mapping fails that import
with an explicit reason instead of silently choosing a fallback.

## Platform map

`<data>\platform-map.json` is the first-class platform -> emulator mapping:

```json
{
  "schema": "xmb-platform-map/1",
  "version": 1,
  "mappings": [
    { "platform": "SNES", "emulatorId": "retroarch",
      "arguments": ["-L", "snes9x_libretro", "{rom}"] },
    { "platform": "3DS", "emulatorId": "retroarch" }
  ]
}
```

`arguments` is optional. When present it replaces the profile's `argumentTemplate` for that
platform; each array element is exactly one argv element with `{rom}`, `{title}`, `{id}`,
`{dir}` and `{core}` substituted verbatim (no whitespace splitting, no quoting). When absent
the profile template is used. An explicit mapping wins even when the profile does not list the
platform, so `--map-platform 3DS retroarch` can fill a profile gap.

Resolution order (`resolveEmulatorForPlatform`): the platform map first (including the seeded
defaults), then the first profile whose `platforms` contains the platform. A missing map file
still resolves the seeded defaults: PS3 -> rpcs3, PS1 -> duckstation, PS2 -> pcsx2,
PSP -> ppsspp, GameCube/Wii -> dolphin, Switch -> ryujinx, SNES/NES/N64/Genesis/GBA/Game Boy/
Game Boy Color/Dreamcast -> retroarch, Saturn -> mednafen, Arcade -> mame, NDS -> melonds.
`--list-mappings` prints every known platform with its resolved emulator, source
(`platform-map` or `profile`), whether that emulator has an executable and the argument
override.

## Tags

Tags live in `pkg.json` (`tags`, default `[]`). `--tag <appId> <tag...>` adds tags,
`--untag` removes them, and `--tags` lists every tag with its package count. Tags are trimmed,
non-empty, at most 32 characters, contain no control characters and are de-duplicated
case-insensitively. They survive `--replace` and `--relink`. `--list --tag <tag>` filters the
package/registration listing case-insensitively.

## Collections

`--set-collection <appId> <name>` moves a package into one collection (removing it from every
other one) and writes the same name into `pkg.json`; `--set-collection <appId> ""` clears it.
`--collections` lists every collection with its count, `--rename-collection` renames a
collection and rewrites every member's `pkg.json`, and `--remove-collection` deletes it and
clears the members' field. `collections.json` keeps the existing `xmb-collections/1` schema.

## Bulk operations

`--bulk <file.json>` takes one object:

```json
{
  "setCollection": "SNES Classics",
  "setEmulator": "retroarch",
  "addTags": ["Classic"],
  "removeTags": ["unplayed"],
  "remove": false,
  "ids": ["pkg-snes-...", "pkg-ps2-...", "pkg-ps3-..."]
}
```

Every field except `ids` is optional; at least one operation is required. All ids are
validated before anything is written. On an unexpected write failure the touched `pkg.json`
files, `apps.json`, `collections.json` and any staged package directories are restored, so a
failed request never leaves a half-updated library. The command reports a per-id result and
exits `1` if any id failed.

## ROM identity and relink

Every import records `romSize`, `romMtime`, `romHash` and `romHashLabel` in `pkg.json`.
Hashing is streamed through a 1 MiB buffer. Files up to 4 GiB get the full sha256
(`full-sha256-v1`); larger files get `sha256(size + first 4 MiB + last 4 MiB)`
(`partial-sha256-v1`); PS3 package folders get a path+size digest (`folder-v1`).

`--relink <appId> <newPath>` updates the ROM path, recomputes the hash, re-expands the argv
(using the platform-map argument override when it applies) and updates `apps.json`. A different
content hash is refused with both hashes in the error unless `--force` is given. Repointing at
a path already imported as another package is refused.

`--verify-hashes` recomputes every package hash and reports `ok`, `mismatch`, `missing-rom`,
`missing-hash` or `unreadable`, exiting `1` when anything is not `ok`.

## Launch checking

`--print-argv <appId>` prints the exact `apps.json` argv as JSON without launching anything.
`--launch-check <appId>` spawns that executable with that argv through `QProcess` (argument
list, never a shell), prints the executable, argv, pid, and the exit code - or the spawn error.
`--launch-timeout <sec>` defaults to 8. On timeout the process is terminated, then killed if it
does not exit, and the outcome is reported as `timedOut`. A successful launch-check exits `0`
only when the process started and exited with code `0`. This is the proof that a ROM/argv pair
reaches its emulator.

## Backup, restore and migration

`--backup <file.json>` writes `xmb-manager-backup/1`:

```json
{
  "schema": "xmb-manager-backup/1",
  "createdAt": "2026-11-05T00:00:00Z",
  "sourceDataDirectory": "C:/XMB/data",
  "apps": [ /* ContentApp objects, kind game, pkg.json present */ ],
  "packages": [ /* the pkg.json objects, including tags and hashes */ ],
  "tags": [ { "id": "pkg-...", "tags": ["Classic"] } ],
  "collections": [ { "name": "SNES Classics", "ids": ["pkg-..."] } ],
  "platformMap": { /* the effective xmb-platform-map/1 object */ },
  "emulatorOverlay": [ /* user/configured emulator profiles */ ],
  "art": [ { "id": "pkg-...", "base64": "..." } ]
}
```

`--with-packaged-art` embeds `ICON0.PNG` files base64 when each is at most 64 KiB and the
total stays bounded; larger icons are skipped and counted in `artOmitted`.

`--restore <file.json>` validates the whole bundle first. Existing package ids are conflicts
and abort the restore before anything is written unless `--replace` is given. `--remap
<from>=<to>` (repeatable) replaces that prefix in every restored ROM path, argument,
executable, working directory and artwork path, which is the migrate-to-another-machine path.
The restore merges the emulator overlay, writes or merges the platform map, recreates
`ICON0.PNG`, `PARAM.SFO` and `pkg.json` (recomputing hashes when the remapped ROM exists),
upserts the registrations and merges collection membership. `actions` reports every package,
registration, collection and configuration step. Any unexpected failure rolls the files and
directory changes back. A restored registration must still satisfy `ContentRegistry` (its
executable must exist), so use `--remap` when the emulator lives at a different path on the
target machine.

## Real PS3 `.pkg` policy

A retail PS3 `.pkg` is encrypted. `--import-pkg` parses the 0x280-byte big-endian header
(magic `0x7F504B47`, revision, type, metadata offset/count/size, item count, data offset/size,
content ID, digest, AES-128-CTR IV, header digest) and searches the readable metadata/data
windows for a PARAM.SFO and an ICON0 PNG.

- If TITLE and an icon are readable (a debug/non-finalized package or a plaintext container),
  the manager extracts that metadata and creates a virtual package whose `romPath` is the
  `.pkg` file and whose argument is the `.pkg` path handed to the PS3 emulator. Notes state
  that the payload was not decrypted.
- If no PARAM.SFO is readable, the import is **refused**: the error reports the header fields,
  content id, AES IV and header digest as evidence and says nothing was imported. No package
  directory, registry entry or temp file is created, and the `.pkg` is never modified.
- A ZIP file or a non-PKG header is refused with its own reason.

There is no decryption key in this tool and no claim that a retail package was imported.

## Validation and reports

`--validate` checks the registry, package directories, missing ROMs/artwork, the platform map
(parse errors, unknown mapped emulator ids, invalid argument-override placeholders), package
tags, collection membership (ids with no readable package, `pkg.json`/`collections.json`
disagreement) and every recorded content hash. It exits `1` when any issue is found and prints
one `kind | id | message` line per issue (the JSON form lists `issues`).

`--export-report <file>` writes `xmb-manager-report/2`:

```json
{
  "schema": "xmb-manager-report/2",
  "generatedAt": "2026-11-05T00:00:00Z",
  "dataDirectory": "C:/XMB/data",
  "paths": { "registry": "...", "packages": "...", "emulators": "...",
             "collections": "...", "platformMap": "..." },
  "emulators": [ ... ],
  "apps": [ ... ],
  "packages": [ ... ],
  "mappings": [ { "platform": "SNES", "emulatorId": "retroarch", "arguments": [...] } ],
  "tags": [ { "tag": "Classic", "count": 1, "ids": ["pkg-..."] } ],
  "collections": [ { "name": "SNES Classics", "count": 1, "ids": ["pkg-..."] } ],
  "hashState": [ { "id": "pkg-...", "state": "ok", "storedLabel": "full-sha256-v1",
                   "storedHash": "...", "currentHash": "...", "romPath": "..." } ],
  "errors": [],
  "appCount": 1, "packageCount": 1, "emulatorCount": 13,
  "mappingCount": 18, "tagCount": 1, "collectionCount": 1
}
```

`hashState.state` is `ok`, `mismatch`, `missing-rom`, `missing-hash` or `unreadable`.

## Atomicity and safety

- Every file write is QSaveFile-style (temp file + rename): `manager.ini`, `emulators.json`,
  `collections.json`, `platform-map.json`, `pkg.json`, `PARAM.SFO`, `ICON0.PNG`, the report and
  the backup bundle.
- Package creation uses a staging directory inside `<data>\pkg` and an atomic directory rename.
  A failed registry upsert removes the new package and restores the replaced one; a failed
  collection update rolls back both.
- `--bulk` validates every id before writing, snapshots `apps.json`, `collections.json` and the
  affected `pkg.json` files, stages removals, and restores the snapshots and directories if a
  write fails.
- `--restore` validates the bundle and all conflicts before writing, keeps the previous package
  directories until success, snapshots the configuration files, and rolls everything back on an
  unexpected failure.
- `--remove` renames the package directory to a hidden trash sibling, removes the registry
  entry and collection ids, then deletes the trash. Every path is checked to be a direct child
  of `<data>\pkg`; a symlink that escapes that root is refused.
- No launch path in this tool uses a shell. The argv list is written to `apps.json`, and the
  XMB's existing `startTracked(executable, arguments, workingDirectory, title)` path consumes
  it exactly as before. `--launch-check` calls `QProcess::start(program, arguments)` and never
  goes through `cmd.exe` except when the configured emulator executable is itself `cmd.exe`.
- `apps.json` is always written through the shared `ContentRegistry`, so its validation,
  normalization, atomic save and 1 MiB/1000-entry limits still apply.

## UI pass contract

`manager_main.cpp` includes `manager_ui.h` (guarded by `__has_include`) and, in UI mode,
constructs:

```cpp
xmb::manager::MainWindow window(settings);
window.show();
```

`MainWindow` is expected in namespace `xmb::manager` with a constructor taking
`xmb::manager::Settings` (and optionally a `QWidget* parent`). It can call:

- `Settings`, `loadSettings`, `saveSettings`, `setDataDirectory`, `ensureDataLayout`
- `registryPath`, `packageRoot`, `emulatorsPath`, `collectionsPath`, `platformMapPath`
- `loadProfiles` (returns profiles; empty on error), `saveProfiles`, `detectInstalled`
- `loadPlatformMap` / `savePlatformMap`, `defaultPlatformMappings`,
  `resolveEmulatorForPlatform`, `resolvePlatformTarget`
- `expandArguments` / `expandArgumentList` / `formatArguments`, `defaultCoreFor`
- `scanPaths`, `platformForPath`, `defaultExtensions`, `readParamSfo`,
  `computeRomFingerprint`
- `createPackage` (set `PackageInput::replace` for an update), `removePackage`,
  `listPackages`, `importRealPkg`, `readPackageRecord`
- `updatePackageTags`, `normalizeTags`, `setPackageCollection`, `renameCollection`,
  `removeCollection`, `applyBulk`
- `relinkPackage`, `verifyPackageHashes`, `packageArgv`, `launchPackage`
- `writeBackup`, `restoreBackup`, `parsePathRemap`
- `readCollections` / `writeCollections`, `assignToCollection`, `removeFromCollections`
- `logInfo` / `logWarning` / `logError`

`createPackage` takes the profile by value and returns the exact `ContentApp` it wrote through
`outApp`. `PackageRecord` reports `orphaned` (package without registration) and
`missingPackage` (manager-owned registration without a package), plus `issue` for a malformed
`pkg.json`; it also carries `tags`, `romSize`, `romMtime`, `hashLabel` and `hashValue`. The UI
should surface those rather than repair them silently.
