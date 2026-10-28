# Steam and native content handoff — 2026-10-25

The Game column now discovers installed Steam titles directly. The Friends column is backed by a separate Steam service. Imported executables and service apps share a versioned native registry. Steam remains the owner of installation, DRM and game launch; Steam data is read-only.

## Installed-PKG presentation

The user explicitly requested that Steam games appear like installed PS3 PKGs. They are direct Game-column entries using the existing wide ICON0 slot, title position, focus scaling, column spacing and menu easing. There is no extra launcher card border, platform badge or "Steam · Installed" subtitle. Original PS3 utilities and existing RPCS3 titles remain accessible below the provider entries.

Cached wide headers are preferred over hero art and portrait covers. User grid overrides are supported when a Steam account can be selected. Missing local artwork can be retrieved asynchronously from Steam's store metadata/header CDN and cached under the native data directory, without a Web API key. Artwork is displayed at its original aspect ratio; custom PKG-style ICON0 artwork and PIC1 backgrounds/game-specific audio are future work. A missing/unavailable header retains the original firmware fallback.

## Discovery and refresh

`src/steam_library.*` reads Steam's registry location, `libraryfolders.vdf`, and `appmanifest_*.acf`. Both old and current library-folder layouts are supported. A title must have the fully-installed bit and an existing installation directory inside its library's `common` tree. Incomplete downloads, missing directories and traversal paths are excluded; Steamworks Common Redistributables is excluded. IDs are deduplicated and titles sorted deterministically.

Parsing bounds cover file size, tokens, nesting, library/manifests and a global 4096-title result cap. The scan and initial artwork decode run on Qt's worker pool. At most 128 images are eagerly decoded per scan, scaled to fit 512×288. This is an initial bounded cache; a large-library visible-row thumbnail cache remains future work.

Account selection prefers a matching Steam installation's active registry account, then MostRecent, a sole valid account, then the newest recorded timestamp. It never reads session tokens or passwords. Ambiguous accounts require `XMB_STEAM_ID`. An explicit `--steam-root` or `XMB_STEAM_ROOT` is exclusive and does not select a different host installation's active account.

F5 at the root, or **Refresh Game Library**, rescans asynchronously and preserves selection by entry ID. **Import Games and Apps** selects a JSON manifest and refreshes after a successful import. Concurrent refresh/import requests are coalesced.

## Owned games and collections

The Game column is the account's owned library, not just its installed titles. The owned
list comes from Valve's `IPlayerService/GetOwnedGames` (`src/steam_owned.*`), which
requires `STEAM_WEB_API_KEY` in the launch environment; without it the column keeps the
installed titles and reports that a key is needed. Installed titles keep launching
(`steam://rungameid/<id>`); owned-but-uninstalled titles offer an install action
(`steam://install/<id>`) so Steam performs the download.

Rows are grouped by the user's own Steam library collections, read locally and keylessly
from `userdata/<accountid>/config/cloudstorage/cloud-storage-namespace-1.json`
(`user-collections.*`, `name` + `added[]`) by `src/steam_collections.*`. A row's detail line
lists its collections, and scan results are ordered by them. The local Steam appinfo cache
(`appcache/appinfo.vdf`, version 41 with a key string table) was probed as an offline name
source and deliberately not shipped, because its entry layout is not fully understood yet.

Reports: `--steam-collections-report`, `--owned-games-report` (needs the key) and
`--steam-library-report` (owned titles grouped by collection). The in-XMB design, including
the store browser and media apps, is in [XMB-TOOLING.md](XMB-TOOLING.md).

## Steam friends

2026-10-26 continuation: the signed-in client helper now supplies 46 verified live friends without changing profile privacy. It is preferred over the Web API path described below; see [STEAM-CLIENT-FRIENDS.md](STEAM-CLIENT-FRIENDS.md) for the build, evidence and limits.

The optional Web fallback uses Valve's HTTPS GetFriendList and GetPlayerSummaries endpoints. Its key comes from the launch environment or the private AppData key file. It is never stored in preferences, manifests or logs. Do not paste keys into chat or commit them. Valve documents a 401 response for private friends lists and a 100-player summaries batch limit: [ISteamUser documentation](https://partner.steamgames.com/doc/webapi/ISteamUser).

The list sorts in-game, online and offline friends and displays returned presence/game text using original avatar artwork and a presence indicator. Selecting a friend hands chat off to Steam; XMB does not send messages. **Open Steam Friends** works independently of the embedded list. Refresh is manual/F5, on entering a stale Friends column, and once per minute while that column is selected. Account changes force cancellation/restart so an old response cannot populate the newly selected account.

Requests have deadlines, bounded responses, batched IDs and stale-generation cancellation. Key-bearing requests do not follow redirects. Missing credentials/accounts, privacy and network errors are explicit. No success/friends are fabricated. The Web API remains a fallback; the optional local-client helper is now verified. Avatar fetching, invites and richer presence remain open. Valve recommends its client API for game-client profile needs; the optional Web API path does not establish that final integration.

## Commands and service apps

Run from the repository root:

```powershell
& .\out\native-xmb\re-ps3-native.exe --steam-report .\out\steam-installed.json
& .\out\native-xmb\re-ps3-native.exe --steam-friends-report .\out\steam-friends.json
& .\out\native-xmb\re-ps3-native.exe --data-dir <native-data> --import-apps <apps.json>
```

The reports/importer do not open a window or require dev_flash. Reports contain local personal/library data and stay outside Git. Friends failures and invalid imports return exit 2. Games launch through the discovered `steam.exe -applaunch <numeric-ID>`; imported games use an executable plus argument array, without a command shell. Accepting a handoff does not prove the game finished starting.

`examples/jellyfin.json` is now a native service starter: replace localhost with your server and import it. It uses kind jellyfin for local sign-in, XMB library browsing and native direct/HLS playback. See [native Jellyfin handoff](JELLYFIN-NATIVE.md). Other web registrations retain browser behavior. Registry details are in [CONTENT-REGISTRY.md](CONTENT-REGISTRY.md).

`XMB_STEAM_ARTWORK=0` disables online header retrieval for offline runs. Missing store headers still fall back safely. The store `appdetails` metadata endpoint is undocumented and must remain replaceable; no browser session/cookies or API key are attached. Remote images and caches remain local.

## Verification and remaining work

- The rebuilt normal executable was reopened with the user's Green preference. Computer Use confirmed light,
  opaque category icons and actual installed game artwork. The live log reports approximately 240 paint frames/s
  on that window's display, with zero missing images. The concurrent Store/owned-library/collection continuation
  is documented in `XMB-TOOLING.md`; those changes and the pre-existing icon diagnostics were preserved.

- The actual installed-library report found 10 titles across the local libraries; a redistributable installer was excluded. Eight had local wide artwork after the expanded cache lookup.
- A disposable manifest probe retained one installed game and rejected an incomplete download, a missing folder and an escaping install path.
- Native CLI imports succeeded into a new data directory. Reimport retained two entries by ID. An invalid `javascript:` service URL returned exit 2 and left the registry SHA-256 unchanged.
- A disposable imported executable launched the native diagnostic with a structured argument array and wrote its expected marker. This verifies imported-process dispatch without starting a user's game.
- Visible scripted Game, Jellyfin service and Friends states exited 0. Capture metadata verifies the actual Steam title IDs and refresh kept the selected title. A manual friends refresh showed the missing-key connection error; live authenticated friend results remain unverified.
- Missing-header retrieval produced five validated native cache files. A subsequent visible replay showed the Battlefield 6 header in its PKG slot. Six DOWN/UP pairs returned to the first title, and F5 retained the next title by ID. A foreground-independent replay-tap fix prevents arrows being discarded when the preview loses focus.
- The verified 1600x900 replay was on a display reporting 143.855 Hz: 143.46 swap callbacks/s, p95 interval 7.36 ms, no intervals above 25 ms in the 5000–7500 ms window. This is callback cadence on that display, not physical presentation proof or a comparison with the earlier 239 Hz monitor.
- Builds, screenshots, state JSON, CLI reports and probe files stay in `out/native-reference/steam-integration/`. The first `fixture-ui.log` capture attempt had malformed PowerShell token concatenation; use `fixture-ui-valid.log` and `real-ui.log` instead.

Next: set a Web API key to exercise the owned/collections path end to end, run an actual install -> launch -> return lifecycle, connect a permitted friends account locally, cache real avatars, support per-game ICON0/PIC1 overrides, add an editor for manifests, then implement Jellyfin as a proper authenticated service/player. Large libraries, disconnect/reconnect, controller use and sustained performance still need runtime evidence.

## 2026-10-27: services breadth (account switch, launch/quit, avatars, trophies)

Phase 6 of [PHASES-2026-10-27.md](../PHASES-2026-10-27.md), in the Steam modules only. All evidence and the
exact `shell.cpp`/`shell.h` hunks are in [SERVICES-BREADTH.md](SERVICES-BREADTH.md); nothing in `shell.*`,
`view.*`, `main.cpp`, `CMakeLists.txt` or the harness was edited by this pass.

- **Account switch UI (6.1).** `steam_accounts.*` gains the UI-facing layer over the existing switch plan:
  `steamSwitchPlanFor()` returns a `SteamSwitchPrompt` with two confirmation steps, the two labels per step,
  the one-line promise `Steam must restart; unsaved XMB state is unaffected.`, an outcome line that never
  claims a sign-in, and a blocked step for the already-active / no-executable / no-account cases.
  `SteamSwitchConfirmation` is the two-step state machine (nothing advances to `Ready` before both steps
  accept, and a cancelled model can never be ready), and `steamSwitchDryRun()` walks it while recording only
  the command lines. A headless driver proved all of it: three prompts, 25 assertions, commands exactly
  `steam.exe -shutdown` then `steam.exe` with `executed=0`. No client was restarted. The shell hunk adds a
  Game-column **Steam Account** page, the two-step confirmation page and the handoff (launch only after both
  steps; the 5 s relaunch delay and its reason are documented).
- **Launch/quit lifecycle (6.1b).** `+app_stop <id>` is now built and validated in one place
  (`steamStopArguments`: decimal, non-zero, at most ten digits, at most `0xffffffff`), and
  `steam_library.*` reads the client's own return channel: `steamConsoleLogTail()` (only
  `<root>/logs/console_log.txt`, 64 KiB default, 1 MiB hard cap, offset -1 = tail, rotation reported) and
  `steamVerifyStop()`, which matches the exact command line after a snapshot offset and treats a following
  `command not found` as a rejection. Against this machine's real 508,319-byte log the reader finds the
  2026-10-26 probe at byte 499,462:
  `ExecCommandLine: ""C:\Program Files (x86)\Steam\steam.exe" +app_stop 999999999"`, with no
  `command not found: app_stop` anywhere in the file. Synthetic logs cover delivery, a different id, a
  snapshot taken after the line, rejection, rotation, a missing log and both size caps (17 assertions).
  **New finding:** on 2026-10-27 the client was signed out (`ActiveProcess\ActiveUser = 0`) and the same
  invocation wrote nothing to the log at all - `+apps_running` (exit 0), `+app_status` and `+app_stop` all
  returned immediately without a line - so the console channel needs a signed-in client, and a missing line
  is not proof of a bad command. The registry stays the primary stop confirmation; the log is secondary and
  is reported, never treated as the outcome. Stopping a real running title, the registry-clear timing, and
  unsaved-progress behaviour still need a live title.
- **Friends avatars (6.2).** `SteamFriendAvatars` (in `steam_friends.*`) turns the SteamIDs the friends list
  already returns into Steam's own public avatar: the public profile XML is read (https,
  `steamcommunity.com` only, 256 KiB, 8 s), the `<avatarMedium>`/`<avatarFull>` URL is accepted only on
  Steam's CDN hosts, the image is fetched bounded (1 MiB, `Content-Type: image/...`), decoded with format and
  dimension caps (1024 px, JPEG/PNG/WebP), scaled to 64x64 and cached as PNG at
  `<data>/cache/steam/avatars/<steamid>.image`; a cached file is reused for 30 days, at most 16 are fetched
  per refresh, two at a time. A missing, private, failed or oversized avatar leaves no file and the row
  keeps the firmware's `tex_Avatar_Default`: the fallback is a real icon, never a placeholder picture. A real
  avatar was downloaded and cached (64x64, an 8,194-byte PNG) and a second refresh did not re-fetch; offline
  cases (oversized, corrupt, missing, `http`, foreign host, private profile) all fall back (16 assertions).
  The shell hunk fills `Entry::artwork` in `rebuildFriends()`; **no view change is needed**, because
  `view.cpp` already draws entry artwork at the friend row's icon quad with the presence dot on top.
- **Trophies (6.3): no backend.** The PS3 trophy model is a per-user
  `dev_hdd0/home/<userid>/trophy/<NPCOMMID>/` directory whose definitions (`TROPCONF.SFM`, a plain XML body
  after a signature header, with `ttype` P/G/S/B) and icons (`ICON0.PNG`, `TROPxxx.PNG`) are readable, while
  the earned state and timestamps live in the encrypted, signed `TROPUSR.DAT`. This machine's RPCS3
  `dev_hdd0` has no `trophy` directory at all, and a definitions-only list would show every trophy as
  unearned - a false claim. PSN sync has no backend, and Steam achievements are a different system. The row
  stays unavailable with wording that says exactly that (in the new audit file).
- **Honest rows (6.4/6.5).** The Network column's manual / browser / search / Remote Play rows already
  decided `Unavailable` but only logged the reason; the PSN column, My Channels and disc titles had no
  user-facing reason at all. Exact sentences for each, plus the hunks that surface `Entry::value` on the
  `Unavailable` path (so the reason reaches both the screen and the catalogue dump), are in
  [SERVICES-BREADTH.md](SERVICES-BREADTH.md) sections 5 and 6. No row asks for a login, invents a purchase,
  or claims a disc drive.

Verification for this pass: `build.ps1 -OutputDirectory C:\dev\out\native-steam6` exit 0 with no warnings
from the three Steam translation units, and `work\ab-layers.ts --all --exe
C:/dev/out/native-steam6/re-ps3-native.exe` = stored pair 6/6, canonical capture 6/6, named states 4/4,
`whole set: PASS`.

