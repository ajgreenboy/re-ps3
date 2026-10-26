# In-XMB tooling: owned library, collections, store, import and media apps

This note describes the programs that open **inside** the XMB. It builds on the Steam
library, content registry and friends work (see
[STEAM-INTEGRATION.md](STEAM-INTEGRATION.md) and [CONTENT-REGISTRY.md](CONTENT-REGISTRY.md))
instead of adding a second menu catalog.

## State

| Piece | Where | State |
|---|---|---|
| Installed Steam titles inline in the Game column | `src/steam_library.*` | Done |
| **Owned (purchased) Steam games, with Install** | `src/steam_owned.*` | Done (needs `STEAM_WEB_API_KEY`) |
| **User's own Steam collections as groups** | `src/steam_collections.*` | Done, keyless and local |
| Import external games/apps from a JSON manifest | `src/content_registry.*` | Done |
| Steam friends column | `src/steam_friends.*` | Done (needs the same key) |
| Steam header artwork cache | `src/steam_artwork.*` | Done |
| Browse the public storefront | `src/steam_store.*` | Done |
| Per-game artwork overrides (ICON0/PIC1) | `src/steam_overrides.*` | Done |
| Steam **Not Installed** folder (owned not installed + family-shared) | `src/shell.cpp` (`Action::SteamFolder`) | Done |
| Steam Families membership + shared list | `src/steam_family.*` | Done (OAuth token still open) |

## Owned games: what the Game column shows

The Game column lists what the account **owns**, not only what is installed:

- installed -> OK launches it (`SteamOwned::launchUrl`, i.e. `steam://rungameid/<id>`);
- owned but not installed -> OK asks Steam to install it (`SteamOwned::installUrl`,
  i.e. `steam://install/<id>`). Steam performs the download; the XMB never writes
  into the Steam directory.

Each row's detail line carries its collection names, so the XMB mirrors the user's own
library organisation.

### Sources

| Data | Source | Key needed |
|---|---|---|
| Installed titles | `libraryfolders.vdf`, `appmanifest_*.acf` | no |
| Purchased titles, names, icons, playtime | `IPlayerService/GetOwnedGames` | **yes** |
| Collections (`name`, `added[]`) | `userdata/<accountid>/config/cloudstorage/cloud-storage-namespace-1.json`, entries keyed `user-collections.*` | no |
| Store browsing | `api/storesearch`, `api/featuredcategories` | no |

Valve requires a Web API key for `GetOwnedGames`. Supply it as `STEAM_WEB_API_KEY`
in the launch environment or as a one-line file at `%APPDATA%\re-ps3-native\steam_api_key`
(user-only, outside the repository); the Steam Friends column reads the same source. The key
is never written to preferences, reports or logs and must never be committed. With no key the XMB keeps the installed titles and
says a key is needed - it never fabricates a library.

Note: two offline/public name sources were probed and dropped. Steam's local
`appcache/appinfo.vdf` (version 41) stores member keys as indexes into a string table and
its per-entry header layout is not fully understood yet; Valve also retired the keyless
`ISteamApps/GetAppList` endpoint (404). The supported path is the Web API key.

## Library folders

The Game column shows the locally installed Steam titles inline and keeps the long tail in a single folder:

| Row | Contents | Action on OK |
|---|---|---|
| Installed titles, inline (10 here) | one row per locally installed title, with its collection labels | launch (`steam://rungameid/<id>`) |
| **Not Installed** folder | every owned title that is not installed (272 here) plus family-shared titles from `<data>/family-shared.json` (3 in the seeded run), 275 rows together | install (`steam://install/<id>`) |

There is no separate **Installed Games** or **Family Shared** folder row. The **Not Installed**
row's detail is the combined count (275). `Action::SteamFolder` pushes an ordinary XMB page
(`Shell::Page`), so navigation, easing and Back are the same as every other page. Rows are
built on demand; at most 64 cached thumbnails are decoded when the folder opens and the
rest fall back to the firmware icon before the online header fetch fills them in.

Verified with `XMB_DUMP_ENTRIES` (writes the catalogue, and `<dump>.folder.json` when a folder opens):

- column: the 10 installed titles inline with `action=LaunchSteam`, then `Not Installed` (detail `275`),
  then PS3 utilities;
- installed rows: collection labels (`Multiplayer`, `Singleplayer`), cached artwork;
- Not Installed page: 275 rows, `action=InstallSteam`, labels like `Hidden - Not installed` and
  `Family shared - Not installed`;
- no `Installed Games` or `Family Shared` folder row is emitted.

## Steam Families

Family-shared titles are licensed to another member, so `IPlayerService/GetOwnedGames` never returns them. Valve's
`IFamilyGroupsService/GetSharedLibraryApps` and `GetFamilyGroupForUser` require an OAuth access token: called with a
Web API key they answer **401 Unauthorized** (probed; recorded here rather than retried).

What works without that token:

- membership, read locally from `FamilyGroup` in `userdata/<accountid>/config/localconfig.vdf` - group id, name and
  the member account ids (verified: group `9416`, 6 members);
- an optional shared-titles list at `<data>/family-shared.json`, which adds those titles to the **Not Installed**
  folder and is needed because no local file maps family app ids to names:

```json
{ "apps": [ { "id": 620, "title": "Portal 2" }, { "id": 400, "title": "Portal" } ] }
```

`--steam-family-report` prints membership plus the shared count. A real OAuth flow (device code or client login) is
the remaining step for automatic family libraries; the HTTP layer and folder are already in place for it.

## Per-game artwork overrides

`src/steam_overrides.*` lets an owned or installed title use the user's own pictures instead of
a cached store header, under `<native-data>/artwork/`:

| File | Slot |
|---|---|
| `<appid>.png` | wide row art, also used as the background |
| `<appid>_icon.png` | square ICON0-style icon |
| `<appid>_bg.png` | PIC1-style background |

`.jpg/.jpeg/.webp` are accepted as well. A row/icon/background falls back to the other slots
when its own file is missing, and an override wins over the cached header and the manifest
icon. Files must be under 32 MiB; anything not named `<numeric appid>[_icon|_bg]` is ignored,
so a stray file cannot hijack a row. `XMB_STEAM_ARTWORK_LOG=1` logs which overrides were used,
and `--steam-artwork-report <json>` lists them without opening a window.

## The three in-XMB programs

All three are ordinary Game/Media column entries; none adds new window chrome, and each
reuses the installed-PKG row presentation (wide header art, title position, focus scale,
column easing).

### 1. Owned library + install, and the in-XMB store (Game column)

The primary surface described above. A **Steam Store** row above the Not Installed folder
opens a browse page of live top sellers (`SteamStore::topSellers`) with prices, and each row
hands `steam://install/<id>` to Steam. Verified in a scripted run: page title `Steam Top
Sellers`, 7 rows, prices such as `$69.99` and `$17.99 (-70%)`, every row `action=InstallSteam`.
Opening the store now shows three sections first - Top Sellers, New Releases and Specials - and only requests the
chosen one, so the page is instant; verified as depth 2 with 7 rows. A free-text search page (on-screen keyboard) is
the next step; nothing is bought inside the XMB.

### 2. Import Games and Apps

`Action::ImportApps` already exists. Inside the XMB it becomes a self-describing page:
the manifest fields in plain language, **Choose Manifest...** (file dialog) calling
`content_registry::importFile`, a list of newly added ids, and the registry's own error
text on failure. Reuse the existing `refreshContent` path afterwards.

### 3. Media Apps (Music/Video/TV columns)

Media services stay content-registry entries with `kind: "web"`. `examples/media-apps.json`
seeds Jellyfin, Plex, YouTube and Spotify. OK opens the service in the system browser.
Embedded playback remains future work (it needs an authenticated player per service).

## CLI reports (headless, no window, no dev_flash)

```powershell
re-ps3-native.exe --steam-report            <json>   # installed titles
re-ps3-native.exe --steam-collections-report <json>  # collections + member app ids
re-ps3-native.exe --owned-games-report      <json>   # purchased titles (needs the key)
re-ps3-native.exe --steam-library-report    <json>   # purchased titles grouped by collection
re-ps3-native.exe --steam-family-report     <json>   # family group, members, shared list
re-ps3-native.exe --steam-artwork-report    <json>   # per-game artwork overrides on disk
re-ps3-native.exe --steam-store-report      <json> [--steam-store-query <term>]
```

Verification so far, on the local install:

- collections: 13 groups read keylessly (Singleplayer 299, Multiplayer 210, Hidden 155,
  Canyon Dogshit 131, Lego 21, Valve 20, Fighting 19, Apps 10, Hype 8, HTPC 3,
  Favorites 1, ea/VR empty);
- Steam column layout: see "Library folders" above (10 installed titles inline; the `Not Installed`
  page opens with 275 rows);
- family: membership read for group `9416` with 6 members; shared list from JSON only;
- owned/library reports with the key present: 282 purchased titles, 259 of them grouped into
  the 13 collections and 4 ungrouped; without a key they exit 2 with
  `Set STEAM_WEB_API_KEY to list the games this account owns.` and write no rows;
- artwork overrides: two seeded files (`730.png`, `400_icon.png`) resolved to the right slots by
  `--steam-artwork-report`, and a scripted run logged `artwork override 730 ...` while the column
  kept its installed rows;
- store: top sellers 7 unique rows, `portal` search 10 rows, nonsense query 0 rows + exit 2;
- scripted XMB run with the new column wiring exits 0 and logs
  `steam owned: 282 games in 13 collections` followed by the refreshed
  `content: Steam installed 10`.

## Rules these programs keep

- No fabricated success: a failed search, import, install handoff or launch reports its own error.
- Steam data stays read-only; nothing is written into the Steam directory.
- Requests carry no cookies, session tokens or credentials except the key the user supplies
  in the environment; caches live under the native data directory.
- Steam owns purchase, download, DRM and launch. The XMB only hands off.
- Personal reports, caches and captures stay outside Git.

## Remaining work

1. A registry/manifest editor page, so imports need no hand-written JSON.
3. Embedded media playback (Jellyfin first) with real authentication.
4. Exercise an actual install -> launch -> return lifecycle end to end, with captures.
5. Revisit `appinfo.vdf` only if a fully offline name source becomes necessary.
