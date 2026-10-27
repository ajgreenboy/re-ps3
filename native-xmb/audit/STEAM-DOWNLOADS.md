# Steam download state: verified local sources for the XMB download page

Status: `src/steam_downloads.{h,cpp}` implements the scanner and the UI text helper. The module is
**not wired into the shell or CMakeLists yet** - the lead integrates it. Everything below was probed
on this machine on 2026-10-26 (America/Chicago), read-only: no writes to Steam's files or registry, no
network, no Steam API key, no process access.

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

- Add `src/steam_downloads.cpp src/steam_downloads.h` to the `qt_add_executable` list in
  `CMakeLists.txt`. The module needs Qt6::Core only (`QSettings`, `QDir`, `QFile`, `QString`); it
  adds no link libraries, no CLI option and no new thread.
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

## 9. Files

- `src/steam_downloads.h` - `SteamDownload`, `scanSteamDownloads`, `steamDownloadStateText`,
  `logSteamDownloads`.
- `src/steam_downloads.cpp` - bounded VDF reader, library discovery, registry hints, classification.
- This file. No other file was modified; nothing was wired into the application.
