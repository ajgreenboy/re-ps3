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

## Steam friends

`src/steam_friends.*` uses Valve's HTTPS GetFriendList and GetPlayerSummaries endpoints. An optional `STEAM_WEB_API_KEY` is read only from the launch environment. It is never stored in preferences, manifests or logs. Do not paste keys into chat or commit them. Valve documents a 401 response for private friends lists and a 100-player summaries batch limit: [ISteamUser documentation](https://partner.steamgames.com/doc/webapi/ISteamUser).

The list sorts in-game, online and offline friends and displays returned presence/game text using original avatar artwork and a presence indicator. Selecting a friend hands chat off to Steam; XMB does not send messages. **Open Steam Friends** works independently of the embedded list. Refresh is manual/F5, on entering a stale Friends column, and once per minute while that column is selected. Account changes force cancellation/restart so an old response cannot populate the newly selected account.

Requests have deadlines, bounded responses, batched IDs and stale-generation cancellation. Key-bearing requests do not follow redirects. Missing credentials/accounts, privacy and network errors are explicit. No success/friends are fabricated. This is a provisional developer-key backend; native Steamworks authentication/SDK integration with an appropriately registered application, avatar fetching, invites and richer presence remain open. Valve recommends its client API for game-client profile needs; the optional Web API path does not establish that final integration.

## Commands and service apps

Run from the repository root:

```powershell
& .\out\native-xmb\re-ps3-native.exe --steam-report .\out\steam-installed.json
& .\out\native-xmb\re-ps3-native.exe --steam-friends-report .\out\steam-friends.json
& .\out\native-xmb\re-ps3-native.exe --data-dir <native-data> --import-apps <apps.json>
```

The reports/importer do not open a window or require dev_flash. Reports contain local personal/library data and stay outside Git. Friends failures and invalid imports return exit 2. Games launch through the discovered `steam.exe -applaunch <numeric-ID>`; imported games use an executable plus argument array, without a command shell. Accepting a handoff does not prove the game finished starting.

`examples/jellyfin.json` is a source-only starter. Replace localhost with the user's server, then import it. It adds a TV/Video Services app that opens the server in the system browser. It does not discover a Jellyfin server, authenticate, or provide an embedded player. Registry details, custom artwork and executable examples are in [CONTENT-REGISTRY.md](CONTENT-REGISTRY.md). Future tooling should edit this same schema and providers rather than maintain a second menu catalog.

`XMB_STEAM_ARTWORK=0` disables online header retrieval for offline runs. Missing store headers still fall back safely. The store `appdetails` metadata endpoint is undocumented and must remain replaceable; no browser session/cookies or API key are attached. Remote images and caches remain local.

## Verification and remaining work

- The actual installed-library report found 10 titles across the local libraries; a redistributable installer was excluded. Eight had local wide artwork after the expanded cache lookup.
- A disposable manifest probe retained one installed game and rejected an incomplete download, a missing folder and an escaping install path.
- Native CLI imports succeeded into a new data directory. Reimport retained two entries by ID. An invalid `javascript:` service URL returned exit 2 and left the registry SHA-256 unchanged.
- A disposable imported executable launched the native diagnostic with a structured argument array and wrote its expected marker. This verifies imported-process dispatch without starting a user's game.
- Visible scripted Game, Jellyfin service and Friends states exited 0. Capture metadata verifies the actual Steam title IDs and refresh kept the selected title. A manual friends refresh showed the missing-key connection error; live authenticated friend results remain unverified.
- Missing-header retrieval produced five validated native cache files. A subsequent visible replay showed the Battlefield 6 header in its PKG slot. Six DOWN/UP pairs returned to the first title, and F5 retained the next title by ID. A foreground-independent replay-tap fix prevents arrows being discarded when the preview loses focus.
- The verified 1600x900 replay was on a display reporting 143.855 Hz: 143.46 swap callbacks/s, p95 interval 7.36 ms, no intervals above 25 ms in the 5000–7500 ms window. This is callback cadence on that display, not physical presentation proof or a comparison with the earlier 239 Hz monitor.
- Builds, screenshots, state JSON, CLI reports and probe files stay in `out/native-reference/steam-integration/`. The first `fixture-ui.log` capture attempt had malformed PowerShell token concatenation; use `fixture-ui-valid.log` and `real-ui.log` instead.

Next: exercise an actual Steam game launch/return lifecycle, connect a permitted friends account locally, cache real avatars, support per-game ICON0/PIC1 overrides, add an editor for manifests, then implement Jellyfin as a proper authenticated service/player. Large libraries, disconnect/reconnect, controller use and sustained performance still need runtime evidence.
