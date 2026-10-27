# Steam download state: verified local sources for the XMB download page

Status: `src/steam_downloads.{h,cpp}` implements the scanner and the UI text helper. It is compiled
into the app (`CMakeLists.txt`, main executable and the optional `xmb-steam-client` target) and is
exposed as a report-only CLI in `main.cpp`: `--steam-root <dir>` plus
`--steam-downloads-report <file.json>`, with `XMB_STEAM_DOWNLOADS_LOG=1` for the qInfo dump. It is now
wired into the XMB's Download Management page (section 10): the firmware's `seg_dlctrl` Network row
opens a page listing Steam's queue.
Everything below was probed on this machine on 2026-10-26 (America/Chicago), read-only: no writes to
Steam's files or registry, no network, no Steam API key, no process access.

Machine facts: Windows, NT 10.0.26200 (25H2), Steam installed at `C:\Program Files (x86)\Steam`,
HKLM `Version=1792618285`, `SteamPID=11580` (client running), one logged-in user (`AutoLoginUser` is
set; its value is intentionally not recorded here), 39 `Apps\<appid>` registry subkeys, 5 library
roots in `libraryfolders.vdf` (C:, F:, D:, E: and a G: drive that no longer exists), 11
`appmanifest_*.acf` files. **No download was running during the probe**: every `Apps\<appid>\Updating`
was 0 and every `steamapps\downloading` folder was empty.

## 1. Registry keys, as observed

All values are read through `QSettings(..., QSettings::NativeFormat)` in the module, which is already
how `src/steam_library.cpp` reads them, so no new link dependency is needed.

| Key | Value | Type | Observed |
|---|---|---|---|
| `HKCU\Software\Valve\Steam` | `SteamPath` | REG_SZ | `c:/program files (x86)/steam` (lowercase, forward slashes) |
| `HKCU\Software\Valve\Steam` | `AutoLoginUser` | REG_SZ | a user name is present (value not recorded); not needed for downloads |
| `HKCU\Software\Valve\Steam` | `RunningAppID` | REG_DWORD | `0` (no app running; holds the app id while one runs) |
| `HKCU\Software\Valve\Steam\Apps\<appid>` | `Running` | REG_DWORD | 0 or 1 |
| `HKCU\Software\Valve\Steam\Apps\<appid>` | `Updating` | REG_DWORD | 0 or 1; 0 for all 39 apps at probe time |
| `HKCU\Software\Valve\Steam\Apps\<appid>` | `Installed` | REG_DWORD | 0 or 1 |
| `HKCU\Software\Valve\Steam\Apps\<appid>` | `Name` | REG_SZ | display name, e.g. `Battlefield` + UTF-8 U+2122 + ` 6` |
| `HKCU\Software\Valve\Steam\Apps\<appid>` | `firewall`, `LocFiles*` | REG_DWORD | present on some apps, not used |
| `HKCU\Software\Valve\Steam\ActiveProcess` | `pid`, `ActiveUser`, `SteamClientDll`, `SteamClientDll64`, `Universe` | mixed | not needed for downloads |
| `HKLM\SOFTWARE\WOW6432Node\Valve\Steam` | `InstallPath`, `SteamPID`, `Version`, `Universe`, `BetaName` | mixed | `InstallPath=C:\Program Files (x86)\Steam`; used as a root fallback |

`Updating=1` is documented in `steam_library.cpp` as "1 while a pre-launch update downloads", and the
module uses it as an active hint. It was 0 during this probe, so that transition is not captured here
(see section 6).

## 2. Files, as observed

### 2.1 `steamapps\libraryfolders.vdf`

Current nested format; every library block has `path`, and optionally `label`, `contentid`,
`totalsize`, `update_clean_bytes_tally`, `time_last_update_verified` and an `apps` child block:

```
"libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"apps"		{ "730" "73942306339" ... }
	}
	"1" { "path" "F:\\SteamLibrary" ... }
	...
}
```

The G: entry still exists in the file but the drive is gone; the scanner drops any library whose
`steamapps` directory is missing. The pre-2020 numbered-at-top-level format
(`"1" "D:\\Games"` directly under the block) is also accepted as a fallback.

### 2.2 `appmanifest_<appid>.acf`

Located at `<library>\steamapps\appmanifest_<appid>.acf` (11 files here, 922-2949 bytes each). It is
UTF-8 quoted KeyValues; the verified byte sequence for the trademark sign in
`appmanifest_2807960.acf` name is `... 64 E2 84 A2 20 36 22` ("Battlefield" + U+2122 + " 6"), and
`QString::fromUtf8` reproduces it exactly. Keys seen in this machine's files:

| Key | Notes |
|---|---|
| `appid` | decimal string; matches the file name here |
| `name` | display name (UTF-8) |
| `StateFlags` | decimal string, the state bitmask (section 3) |
| `installdir`, `LauncherPath`, `Universe`, `buildid`, `TargetBuildID`, `LastOwner`, `DownloadType` | install metadata |
| `lastupdated` | lowercase in the files; Unix seconds, e.g. `1792107735` = 2026-10-15 18:42:15 |
| `LastPlayed`, `SizeOnDisk`, `StagingSize` | not used |
| `BytesToDownload`, `BytesDownloaded`, `BytesToStage`, `BytesStaged` | byte counters; **optional** (`appmanifest_228980.acf` has none) |
| `UpdateResult`, `AutoUpdateBehavior`, `AllowOtherDownloadsWhileRunning`, `ScheduledAutoUpdate`, `StagingFolder` | optional; `UpdateResult` was 0 everywhere |
| `InstalledDepots { <id> { manifest, size[, dlcappid] } }`, `InstallScripts`, `SharedDepots`, `UserConfig`, `MountedConfig` | nested blocks; skipped by the scanner |

Important trap verified here: the byte counters **persist after a completed update** with
`BytesDownloaded == BytesToDownload` and `BytesStaged == BytesToStage` (CS2 166265664/166265664,
Overwatch 664535648/664535648, Battlefield 22079410000/22079410000). Their presence is therefore not
evidence of a download; only `StateFlags` (plus the registry `Updating` hint) decides.

### 2.3 Staging folders

- `steamapps\downloading\<appid>\` holds partial and pre-commit content. Steam's own log names it as
  the commit source, e.g. `starting commit from "E:\SteamLibrary\steamapps\downloading\2807960" to
  "F:\SteamLibrary\steamapps\common\Battlefield 6"`. The staging library can therefore differ from
  the install library (the ACF's `StagingFolder` is a library index: 3 for app 2807960).
- `steamapps\temp\<appid>\` exists as a leftover directory (`D:\SteamLibrary\steamapps\temp\3354750`,
  2026-10-25) and is not a state source.
- No `appmanifest_*.acf` copy inside `downloading\<appid>\` was observed. The scanner still reads one
  if it appears (best effort), because the brief anticipated that layout.
- Workshop content (`steamapps\workshop\appworkshop_*.acf`, `workshop\downloads\`) is out of scope for
  this module.

### 2.4 Files that do not exist here

`steam\appcache\downloadstats.vdf` is **absent**. A recursive search for `downloadstats*` under the
Steam root returned nothing; `appcache` holds `appinfo.vdf`, `packageinfo.vdf`, `httpcache\`,
`librarycache\` and `stats\`. No schema was invented and the scanner does not read it. If a future
Steam writes it, it should be captured before being modelled.

`steam\logs\content_log.txt` exists (3.8 MB, 2026-09-04 to 2026-10-26). It is used **only as evidence**
for the state names below; the scanner never reads logs, and a log tail is not state.

## 3. StateFlags values, verified

Two independent local sources: the ACF values themselves, and Steam's own
`scheduler finished : ... state 0x<hex>` lines, which print the same AppState mask in hex next to a
`state changed : <names>` line in `content_log.txt`. These pairs decompose exactly:

| `state changed` names (content_log) | hex | bits |
|---|---|---|
| `Update Required,Fully Installed,Update Queued,` | `0xe` | 2+4+8 |
| `Update Required,Update Queued,Update Started,` | `0x40a` | 2+8+1024 |
| `Update Required,Fully Installed,Update Queued,Update Started,` | `0x40e` | 2+4+8+1024 |
| `Update Required,Update Queued,Update Optional,Update Started,` | `0x41a` | 2+8+16+1024 |
| `Update Required,Fully Installed,Files Corrupt,Update Started,` | `0x48e` | 2+4+8+128+1024 (queued bit still set in that sample) |
| `Update Required,Fully Installed,Update Queued,Files Missing,Files Corrupt,Update Started,` | `0x4ae` | 2+4+8+32+128+1024 |
| `Update Required,Fully Installed,Files Missing,Files Corrupt,Update Paused,Update Started,` | `0x6a6` | 2+4+32+128+512+1024 |
| `Fully Installed,App Running,` | `0x200c` | 4+8+8192 (queued bit still set in that sample) |
| `Fully Installed,Update Queued,` | `0xc` | 4+8 |

The name list can lag one flag change (`0x42e` appears with and without `Update Queued`,
`0x200c` with `Fully Installed,App Running` while the queued bit was still set), so each bit is taken
from the pairs where names and bits correspond one-to-one. ACF cross-checks on this machine:

- `appmanifest_3354750.acf` (skate.) has `StateFlags 6` = 2+4 = Update Required + Fully Installed, and
  the same app's log state around that write is `Update Required,Fully Installed,`.
- `appmanifest_202970.acf` (Call of Duty: Black Ops II) has `StateFlags 68` = 4+64, and its log state
  is `Fully Installed,Shared Only,`, which pins 64 = Shared Only.

Resulting table used by the module (header comment repeats it):

| Bit | Meaning | Confidence |
|---|---|---|
| 2 | Update Required | verified (ACF 6, log pairs) |
| 4 | Fully Installed | verified (nine ACFs, log pairs) |
| 8 | Update Queued | verified (log pairs) |
| 16 | Update Optional | verified (0x41a) |
| 32 | Files Missing | verified (0x42e/0x4ae) |
| 64 | Shared Only | verified (ACF 68 + log names) |
| 128 | Files Corrupt | verified (0x48e/0x4ae/0x6a6) |
| 256 | Update Running | documented only; absent from local masks, used as an extra active hint |
| 512 | Update Paused | verified (0x6a6) |
| 1024 | Update Started | verified (every running-update mask) |
| 8192 | App Running | verified (0x200c) |

Other state names seen in the log but not needed or not mapped: `Uninstalled`, `Terminating`,
`Moving Folder`, `Updated Disabled by app`, `Update Optional` (mapped above), `Shared Only` (mapped
above), plus the suffixes `(Suspended)` and `(Update delayed for N secs)`, which are scheduler
results/notes, not flag bits.

## 4. What paused/queued/updating look like on this machine

**Queued / pending update - live right now.** App 3354750 "skate.",
`D:\SteamLibrary\steamapps\appmanifest_3354750.acf`: `StateFlags 6` (Update Required + Fully
Installed), `BytesToDownload 328909824`, `BytesDownloaded 0`, `BytesToStage 0`, `BytesStaged 0`,
`buildid == TargetBuildID == 25617022`, `lastupdated 1792107735` (2026-10-15 18:42:15),
`ScheduledAutoUpdate 1793096266` (2026-10-27 05:17:46, in the future), no `downloading\3354750`
folder, a leftover `D:\SteamLibrary\steamapps\temp\3354750`. The scanner reports state
`update-pending`, text `Update pending`, fraction 0 - exactly the "waiting for the scheduled update"
case. Note it has no Update Queued bit yet (2|4, not 2|4|8).

**Active update (historical, same machine).** App 2807960 (Battlefield 6), 2026-10-26 17:13:23 to
18:09:31, in order: `Update Required,Fully Installed,Update Queued,Update Started` (`0x40e`) ->
`... Files Missing,Files Corrupt,Update Started` (`0x4ae`) -> `Fully Installed,Update Queued` ->
`Fully Installed`. The update phases are a separate log stream (`App update changed : Running
Update,Reconfiguring/Preallocating/Downloading/Staging/Committing/Stopping`). The `update started` line
reports the counters live: `download 22079410000/22079410000, ... stage
26516699753/147463815111`, i.e. the download phase was already complete while staging was still in
progress, so the two byte pairs track two different phases. That is what the scanner uses to name the
phase (`downloading` while `BytesDownloaded < BytesToDownload`, then `staging` while `BytesStaged <
BytesToStage`); after the commit all four counters are equal.

**Paused (historical, same machine).** Two paused states appear in the log, both after the scheduler
suspended a faulty update: app 2357570 at 2026-09-04 22:28:22 ("Missing update files") and app 2807960
at 2026-09-15 20:19:11 ("Corrupt update files"). The state names are
`Update Required,Fully Installed,Files Missing,Files Corrupt,Update Paused,Update Started` and the
scheduler line prints the same mask, `0x6a6` = 2+4+32+128+512+1024, next to it. A paused ACF
therefore reads `StateFlags 1702`, with the byte counters frozen mid-phase. A user-initiated pause may
set the same 512 bit without the missing/corrupt bits; that variant is not observed here, and the
scanner keys only on bit 512. It was not captured as a live ACF (both apps have since finished), so
the mask is reconstructed from this machine's log pairs, not from a currently paused manifest.

**Queue transient after a commit:** `Fully Installed,Update Queued` (`0xc`) is logged briefly after a
successful update; the scanner classifies it as `queued` only if it is neither active nor paused.

## 5. Mapping to `SteamDownload`

| Field | Source |
|---|---|
| `appId` | `AppState\appid` (fallback: digits of `appmanifest_<id>.acf`), validated as 1-10 digits |
| `title` | `AppState\name` (UTF-8), else `HKCU\...\Apps\<id>\Name`, else the app id |
| `bytesDownloaded` | `BytesDownloaded` (0 if absent) |
| `bytesToDownload` | `BytesToDownload` (0 if absent) |
| `bytesStaged` | `BytesStaged` (0 if absent) |
| `bytesToStage` | `BytesToStage` (0 if absent) |
| `active` | `Update Started` (1024) or `Update Running` (256), or registry `Updating=1`, and not paused; a registry-only app with `Updating=1` and no manifest yet (first install) also counts |
| `paused` | `Update Paused` (512) |
| `queued` | `Update Queued` (8) with neither `active` nor `paused` (Steam keeps 8 set during a running update, so this flag is "waiting to start" only) |
| `state` | derived: `paused`, then active -> `downloading` (`downloaded < toDownload`) / `staging` (`downloaded complete`, `staged < toStage`) / `updating`, then `queued`, then `update-pending` (2/16/32/128), else `idle` |
| `fraction()` | `staged/toStage` while `state == "staging"`, otherwise `downloaded/toDownload`, else `staged/toStage`; clamped to [0,1]; 0 when Steam publishes no counters |
| text | `steamDownloadStateText`: `Paused`, `Queued`, `Downloading N%`, `Staging N%`, `Updating`, `Update pending` (N capped at 99 until the phase ends) |

Only apps with a download or update worth showing are returned: active, paused, queued, or a
pending/required/optional/missing/corrupt update. Fully installed apps and apps that are merely
running are filtered out (for example the nine `StateFlags 4` manifests and the `68`
Fully Installed + Shared Only app here). Results are sorted active -> paused -> queued -> pending,
then by title.

## 6. Unverifiable without an active download

- The exact ACF written during an in-flight download/pause (byte counters mid-phase, and whether the
  mask gains `Update Running`/other documented bits). The paused mask `0x6a6` and the running masks
  come from this machine's log, not from a live manifest.
- Whether `steamapps\downloading\<appid>\` ever contains an `appmanifest_*.acf` copy. Not observed;
  the scanner reads one only if present.
- Whether `HKCU\...\Apps\<id>\Updating` is 1 for the whole download (it is 0 for all 39 apps now; the
  behaviour is asserted by `steam_library.cpp`, not captured by this probe).
- `downloadstats.vdf` and any aggregate/speed/ETA/queue-order data: the file is absent here, so
  nothing is modelled.
- Whether Steam writes a paused state to a different file or restores a paused download after a
  restart; `UpdateResult` (all 0 here) as a failed-update signal; bandwidth limits.
- The workshop download queues (`appworkshop_*.acf`) are not covered.

## 7. Integration notes

- Already listed in `CMakeLists.txt` (main executable and the optional `xmb-steam-client` target). The
  module needs Qt6::Core only (`QSettings`, `QDir`, `QFile`, `QString`); it adds no link libraries, no
  new thread, and its only CLI surface is the report option `main.cpp` provides.
- From the shell, on entering the download management page and at most about once a second while it
  is visible (not per frame):
  `QString error; const QVector<xmb::SteamDownload> downloads = xmb::scanSteamDownloads(steamRoot, &error);`
  - empty result + empty error means nothing is downloading (normal);
  - an error only occurs when no Steam installation with a `steamapps` directory can be found.
- Row content: `download.title`, `steamDownloadStateText(download)` and a progress bar from
  `download.fraction()`. The vector is already sorted for display. A useful extra line is the byte
  counts (`bytesDownloaded`/`bytesToDownload`, or the staged pair while staging).
- `scanSteamDownloads("")` resolves the root from `XMB_STEAM_ROOT`, then `HKCU\...\SteamPath`, then
  the usual install directories. `XMB_STEAM_ROOT` is the same override `steam_library.cpp` honours, so
  tests can point both modules at a recorded tree.
- `XMB_STEAM_DOWNLOADS_LOG=1` plus `xmb::logSteamDownloads(downloads)` prints a self-check line per
  download (app, title, state, flags, byte counters, progress, display text).
- Limits in the scanner: 1 MiB per VDF/ACF file, 4096 pairs per file, 64 libraries, 4096 manifests per
  library, 16384 manifests per scan, 8192 registry apps, 256 returned rows. All reads are read-only
  and bounded.

## 8. Verification performed

- Compiled and linked standalone with the Qt 6.12.0 msvc2022_64 kit:
  `cl /std:c++20 /Zc:__cplusplus /EHsc /W4 /MD /O2 /DQT_CORE_LIB /I<Qt>/include /I<Qt>/include/QtCore
  steam_downloads.cpp driver.cpp /link /LIBPATH:<Qt>/lib Qt6Core.lib` - exit 0, no warnings at /W4.
  The translation unit also compiles clean on its own with the project's exact MSVC flags
  (`/W4 /permissive- /utf-8`), so adding it to `CMakeLists.txt` needs no edits to the file.
- Real machine, auto-discovered root: one row,
  `app 3354750 | skate. | update-pending | 0/328909824 | Update pending` (everything installed is
  filtered out, as intended).
- Synthetic Steam trees (in `C:\dev\out\native-downloads\fake`, not in the repo) exercised every
  branch: `Downloading 43%` (mask 1038, 430000000/1000000000), `Downloading 11%` (staging-copy
  manifest under `downloading\<id>\`), `Staging 25%` (download complete, 500/2000 staged), `Paused`
  (mask 1702), `Queued` (mask 12 in a second library reached through `libraryfolders.vdf`). An
  installed-only app (mask 4), a garbage ACF and an ACF without `StateFlags` were all ignored.
  `XMB_STEAM_ROOT` and the explicit-root argument both resolved; a nonexistent root returned the
  "Steam installation not found" error.
- Full project build with the module compiled in:
  `build.ps1 -OutputDirectory C:\dev\out\native-downloads` - exit 0, ninja compiles
  `steam_downloads.cpp.obj` with no warnings from that file (the warnings in the log are pre-existing
  ones in `main.cpp`, `host.cpp`, `view.cpp` and `interaction_audio.cpp`). The integrated CLI was then
  run three ways: `--steam-root "C:\Program Files (x86)\Steam" --steam-downloads-report
  report-real.json`, the same without `--steam-root` (registry discovery), and `--steam-root <fake>
  --steam-downloads-report report-fake.json`. The JSON matched the standalone driver: 1 entry
  (`skate.`, `update-pending`, 0/328909824) on the real install, and all five synthetic states with
  the expected order (active -> paused -> queued).

## 9. Files

- `src/steam_downloads.h` - `SteamDownload`, `scanSteamDownloads`, `steamDownloadStateText`,
  `logSteamDownloads`.
- `src/steam_downloads.cpp` - bounded VDF reader, library discovery, registry hints, classification.
- This file. No other file was modified by this work; the `CMakeLists.txt` and `main.cpp` integration
  (report CLI, `--steam-root`, env-gated log) was done by the lead on top of these three files.

## 10. Wired into the XMB: the Download Management page

Implemented 2026-10-26. The firmware's Network column row `seg_dlctrl` (title "Download Management",
icon `tex_dl_manage`) is now `Action::SteamDownloads` and opens a page built from
`scanSteamDownloads()`. Nothing was added to the scanner: the page uses its ordering, its state text,
its byte counters and its `fraction()`.

### 10.1 The mapping, end to end

| Stage | Where | What it does |
|---|---|---|
| 1 | `src/xmbml_view.cpp`, `XmbmlRow::kind()` | Returns `RowKind::DownloadControl` when the query's provider is `ese` and its src carries `type=download_control`. This is the only provider query the reader classifies by the query's own type field, because the XMBML defines no `<Item class=...>` for it (there is no local view). |
| 2 | `src/shell.cpp`, `resolveNetworkColumn` / `actionFor` | `RowKind::DownloadControl` -> `Action::SteamDownloads`, with the reason logged: "the console's download plugin lists its queue; this build lists Steam's own download queue from the manifests Steam writes ...". |
| 3 | `src/shell.cpp`, `activate` | `Action::SteamDownloads` -> `openSteamDownloads()`. |
| 4 | `src/shell.h` | `Action::SteamDownloads` appended to the enum (value 30, so no existing action changed value), `Page::downloads`, `steamDownloadRows`, `openSteamDownloads`, `reloadSteamDownloads`. |

The column row itself is unchanged: same id `dlctrl`, same title, same `tex_dl_manage` icon, same
position (5th), still `source=rco` because the label comes from the plugin's RCO string. The one change
in the catalogue dump is that row's action: `0` -> `30`.

`XMB_XMBML_LOG=1` line from the verified build:

```
xmbml: network row dlctrl key=seg_dlctrl class=- title="Download Management" titleSource=rco texture=tex_dl_manage action=30 source=rco reason="the console's download plugin lists its queue; this build lists Steam's own download queue from the manifests Steam writes (src/steam_downloads.cpp, audit/STEAM-DOWNLOADS.md)"
```

The other four Network rows keep their existing `Unavailable` decisions and reasons.

The built-in fallback row (`src/shell.cpp`, the constructor's Network list, id `downloads`) carries
`Action::SteamDownloads` too, so the page still opens when `category_network.xml` cannot be read and
the column keeps its literals. Checked with the scratch dev_flash from the column audit (87-byte
truncated `category_network.xml`): the column reports `keeps its built-in rows: Line 1: Premature end
of document.`, the dump lists `downloads ... action=30 source=fallback`, and the same page opens with
the same row dump.

### 10.2 What the page shows

One row per `SteamDownload`, in exactly the order the scanner returns (active -> paused -> queued ->
pending, then title). Nothing is re-sorted.

- Title: `download.title`.
- Second line under the title, drawn for every row: `steamDownloadStateText(download)` followed by the
  progress figure - `Update pending - 0% (0 B of 313.7 MB)`, `Downloading 43% - 410.1 MB of 953.7 MB`,
  `Paused - 50% (238.4 MB of 476.8 MB)`, `Queued - no progress published`.
  The percentage is `qRound(fraction()*100)`; the byte pair is the scanner's counters for the phase the
  state names (the staged pair while staging, the downloaded pair otherwise). When the scanner
  publishes no counters for the phase the line says `no progress published`: a percentage is never
  invented. When the state text already ends with the percentage ("Downloading 43%") the number is not
  printed twice.
- Icon: the app's cached Steam header (`<data>/cache/steam/<appid>.jpg`), decoded the way the Steam
  library folder rows decode it (bounded to 64 decodes, 512x288, centre-cropped square). No cache ->
  the build's default `game_tex_default_ps3` glyph.
- Row id: `download:<appid>`.

Real machine (the same item the scanner reports, one pending update):

| field | value |
|---|---|
| title | `skate.` |
| state text | `Update pending` |
| detail | `Update pending - 0% (0 B of 313.7 MB)` |
| app / counters | `3354750`, `bytesDownloaded 0`, `bytesToDownload 328909824` (313.7 MiB), staged 0/0 |
| fraction | `0` |
| artwork | cached, `hasArtwork: true` |
| scan time | 6-8 ms |

A synthetic Steam tree (four manifests under `C:\dev\out\dl-scratch\fake`, outside the repo, reached
through `XMB_STEAM_ROOT`) proves the ordering and the figures. The titles are chosen so rank order and
alphabetical order disagree:

| # | title | state | text | detail line | fraction |
|---|---|---|---|---|---|
| 1 | Zulu Active | downloading | Downloading 43% | `Downloading 43% - 410.1 MB of 953.7 MB` | 0.43 |
| 2 | Alpha Paused | paused | Paused | `Paused - 50% (238.4 MB of 476.8 MB)` | 0.50 |
| 3 | Mike Queued | queued | Queued | `Queued - no progress published` | 0 |
| 4 | Bravo Pending | update-pending | Update pending | `Update pending - 0% (0 B of 313.7 MB)` | 0 |

### 10.3 Nothing downloading, and no Steam

Both are one dialog over the Network column, with the page title as its heading, and both still write
the page dump:

- A resolvable Steam root whose `steamapps` holds no interesting manifest: `Nothing is downloading.`
  (that exact sentence). Log: `steam downloads: page empty (nothing is downloading)`; dump `count 0`,
  `error ""`, `message "Nothing is downloading."`.
- No `steamapps` under any root: the scanner's own sentence, `Steam installation not found (no
  steamapps directory under the configured roots).` Log: `steam downloads: page unavailable: ...`;
  dump `count 0` with `error` and `message` both set to that text.

Neither pushes an empty page, the same convention the Steam library folders use ("This folder is
empty."). A refresh that finds the queue empty, or Steam gone, also closes an open page and says so
rather than leaving rows that no longer describe anything.

### 10.4 Refresh

`F5` -> `Shell::refreshDirectory()` -> `reloadSteamDownloads()` when the open page is the download page
(`Page::downloads`). One rescan, and the page's row vector is assigned, never appended, so repeated
presses cannot grow the list. A row that is still present keeps the selection by id. There is no
automatic timer: the page scans when it opens and when F5 is pressed.

Evidence with `--ui-clock 2026-10-25T12:32:00` pinned so the live clock hand cannot differ:

```
steam downloads: scanned 1 row(s) in 6 ms
steam downloads: page opened with 1 row(s)
steam downloads: scanned 1 row(s) in 6 ms
steam downloads: page refreshed to 1 row(s), selection 0
```

`dl-pinned-open.png` and `dl-pinned-refresh.png` are byte-identical (sha256 prefix `DAB7C90BCAF04C83`),
and the state sidecar after the refresh is `depth 1, entryCount 1, entryId download:3354750`.

### 10.5 The page dump

`XMB_DUMP_ENTRIES=<file>` writes `<file>.dl.json` on every scan (page open and each refresh), next to
the existing `.folder.json` and `.store.json` dumps:

```json
{
    "page": "dl",
    "title": "Download Management",
    "count": 1,
    "error": "",
    "message": "",
    "rows": [
        {
            "id": "download:3354750", "appId": "3354750", "title": "skate.",
            "state": "update-pending", "text": "Update pending",
            "detail": "Update pending - 0% (0 B of 313.7 MB)",
            "note": "Pause/resume is not supported: Steam has no per-title pause or resume command.",
            "bytesDownloaded": 0, "bytesToDownload": 328909824,
            "bytesStaged": 0, "bytesToStage": 0,
            "fraction": 0, "active": false, "paused": false, "queued": false,
            "hasArtwork": true
        }
    ]
}
```

The per-row names mirror `--steam-downloads-report` (`appId`, `state`, `text`, the four counters,
`fraction`, `active`/`paused`/`queued`), so a script can compare the page against the report. `error`
and `message` state what the page says, so the empty and error cases are dump-checkable too.

### 10.6 The per-row action: no pause, no resume

Cross on a row opens the row's own information page (`Action::Information`):

```
skate.

App: 3354750
State: Update pending
Progress: 0% (0 B of 313.7 MB)
Bytes: downloaded 0 B of 313.7 MB
Steam state: update-pending
Pause/resume: not supported
Note: Steam has no per-title pause or resume command (audit/STEAM-CLI.md)
```

Triangle on the row lists `View`, `Information`, `Close`; no Start/Pause row is manufactured. The
panel's `Information` shows the same page, because the row's value already is that page. The claim
behind the note is `audit/STEAM-CLI.md`: no `steam://` verb pauses, resumes or throttles a download,
and the only download switch is the global `app_download_enable`, which affects every title and
auto-update, so this build does not offer it either.

### 10.7 Verification

Build (the mandated command, last run after the last source edit; exe 2026-10-26 22:35:37):

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-dlpage
```

Exit 0; `shell.cpp` and `xmbml_view.cpp` compile with no warnings (the warnings in the log are the
pre-existing ones in `view.cpp`, `main.cpp`, `host.cpp` and `interaction_audio.cpp`).

Captures (in `C:\dev\out\dl-scratch\caps`, local, not committed), all with `--dev-flash
C:/dev/rpcs3/bin/dev_flash --data-dir C:/dev/xmb/out/native-reference/store/ui3-data --render-clock 20
--background-clock 2026-10-25T10:00:00 --focus-time 2000` and the route `LEFT x9, RIGHT x7, DOWN x4,
ENTER`. The category index came from a catalogue dump: `network` is index 7 (`game` is 6, the start),
and the Network rows are `manual, premo, browser, kensaku, dlctrl`, so `dlctrl` is row index 4.

| file | shows |
|---|---|
| `dl-open.png` | the page: `skate.` with `Update pending - 0% (0 B of 313.7 MB)` under it |
| `dl-refresh.png`, `dl-refresh2.png` | after one and two F5 presses: same page, still one row (byte-identical to each other) |
| `dl-pinned-open.png`, `dl-pinned-refresh.png` | the same with a pinned UI clock: byte-identical |
| `dl-fake.png` | four rows in scanner order with the figures in the table above |
| `dl-empty.png` | `Download Management` / `Nothing is downloading.` |
| `dl-error.png` | `Download Management` / `Steam installation not found (no steamapps directory under the configured roots).` |
| `dl-opt-panel.png` | Triangle: `View / Information / Close` |
| `dl-opt-info.png`, `dl-row-info.png` | the information page above (byte-identical: panel and Cross reach the same page) |

`dl-open.png` and `dl-refresh.png` differ only in the live clock's minute hand (276 pixels at x
1695-1793, y 94-117), which is why the pinned pair above is the byte-identity evidence.

Layer gate (the mandated command):

```
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-dlpage/re-ps3-native.exe
```

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---:|---:|---:|---|
| frame | 18.29 | 18.02 | +0.27 | .8295 | .8301 | -.0006 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget`.

Named states:

```
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --states --exe C:/dev/out/native-dlpage/re-ps3-native.exe
```

`states: 4/4 pass`, every region `inf / 1.0000` (pixel-identical), every state assertion unchanged
(`steam-game-root`, `steam-all-games`, `steam-store-sections`, `steam-not-installed`).

One honest note on that baseline: while this work was in progress another agent changed the item-glyph
rendering (`view.cpp` and `view.h`, outside this change's write fence) and re-saved
`out/native-reference/layers/*.png` at 22:32-22:33. A run of the same command on the 22:26 build,
against the previous 21:24 references, failed 0/4; that run's pixel difference was confined to
x 251-592, y 469-1055 - the folder glyphs on the All Games page - with no other region changed, so the
delta belonged to that glyph work, not to this page (no download row exists in any named state). No
harness file, threshold or reference was edited by this work.

### 10.8 Limits

- No per-row progress bar: the figure is the row's second line, because `view.cpp` (the draw code that
  owns row layout) is another agent's write fence for this change. A bar is a renderer change, not a
  shell change.
- The scan runs on the UI thread when the page opens or refreshes: 6-8 ms here (11 manifests, 5
  library roots, 39 registry apps). The scanner's own caps bound the worst case; if a large library
  ever makes that visible, the store/folder pattern (`QtConcurrent` plus a page fill) is the next
  step.
- No automatic refresh timer: the page reflects the queue at the moment it was opened or refreshed.
- No pause/resume/start/cancel; see 10.6. Starting an install stays on the store row's
  `steam://install/<id>` path, a different action (`Action::InstallSteam`).
- English only, like the rest of the shell's pages.
- The second line uses the one-line slot the view draws under every row (`Shell::subtitleText`), and
  only for `download:` ids, so no other column changes.

### 10.9 Files touched by this integration

- `src/xmbml_view.h`: `RowKind::DownloadControl`.
- `src/xmbml_view.cpp`: the `ese:// ... type=download_control` classification in `XmbmlRow::kind()`.
- `src/shell.h`: `Action::SteamDownloads`, `Page::downloads`, `steamDownloadRows`,
  `openSteamDownloads`, `reloadSteamDownloads`.
- `src/shell.cpp`: the `actionFor` case and its comment, the built-in fallback row's action, the page
  builder (`steamDownloadRows`), `openSteamDownloads`, `reloadSteamDownloads`, the `activate` case,
  `activateLabel`, `entryKind`, `subtitleText`, `showEntryInformation`'s download branch and
  `refreshDirectory`'s download branch.
- This file (section 10 and the status line above).

Not touched: `view.cpp`/`view.h`, `main.cpp`, `steam_downloads.{h,cpp}`, `CMakeLists.txt`, the harness
(`work/ab-layers.ts`, its thresholds and references), and anything under the firmware.

