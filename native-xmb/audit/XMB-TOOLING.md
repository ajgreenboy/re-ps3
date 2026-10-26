# In-XMB tooling: owned library, collections, store, import and media apps

This note describes the programs that open **inside** the XMB. It builds on the Steam
library, content registry and friends work (see
[STEAM-INTEGRATION.md](STEAM-INTEGRATION.md) and [CONTENT-REGISTRY.md](CONTENT-REGISTRY.md))
instead of adding a second menu catalog.

## State

| Piece | Where | State |
|---|---|---|
| Installed Steam titles in the Game column | `src/steam_library.*` | Done |
| **Owned (purchased) Steam games, with Install** | `src/steam_owned.*` | Done (needs `STEAM_WEB_API_KEY`) |
| **User's own Steam collections as groups** | `src/steam_collections.*` | Done, keyless and local |
| Import external games/apps from a JSON manifest | `src/content_registry.*` | Done |
| Steam friends column | `src/steam_friends.*` | Done (needs the same key) |
| Steam header artwork cache | `src/steam_artwork.*` | Done |
| Browse the public storefront | `src/steam_store.*` | Done |
| Per-game artwork overrides (ICON0/PIC1) | `src/steam_overrides.*` | Done |

## Owned games: what the Game column shows

The Game column lists what the account **owns**, not only what is installed:

- installed -> OK launches it (`SteamOwned::launchUrl`, i.e. `steam://rungameid/<id>`);
- owned but not installed -> OK asks Steam to install it (`SteamOwned::installUrl`,
  i.e. `steam://install/<id>`). Steam performs the download; the XMB never writes
  into the Steam directory.

Each row's detail line carries its collection names, and the row order follows the
collections, so the XMB mirrors the user's own library organisation.

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

### 1. Owned library + install (Game column)

The primary surface described above. **Steam Store** remains available for browsing
titles that are not owned yet: search (on-screen keyboard) or top sellers, then *Install
in Steam* / *Store Page*. Rows already owned render with their owned state so the user is
never told to buy something twice.

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
re-ps3-native.exe --steam-store-report      <json> [--steam-store-query <term>]
```

Verification so far, on the local install:

- collections: 13 groups read keylessly (Singleplayer 299, Multiplayer 210, Hidden 155,
  Canyon Dogshit 131, Lego 21, Valve 20, Fighting 19, Apps 10, Hype 8, HTPC 3,
  Favorites 1, ea/VR empty);
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
