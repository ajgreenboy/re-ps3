# In-XMB tooling: store, import and media apps

This note designs the programs that open **inside** the XMB instead of requiring a
CLI. It builds on the existing Steam library, content registry and friends work
(see [STEAM-INTEGRATION.md](STEAM-INTEGRATION.md) and
[CONTENT-REGISTRY.md](CONTENT-REGISTRY.md)) rather than adding a second menu
catalog.

## What already exists

| Piece | State |
|---|---|
| Installed Steam titles in the Game column | Done: `src/steam_library.*` |
| Import external games/apps from a JSON manifest | Done: `src/content_registry.*`, `--import-apps` |
| Steam friends column | Done (needs a permitted key): `src/steam_friends.*` |
| Steam header artwork cache | Done: `src/steam_artwork.*` |
| **Browse/download games that are not installed yet** | **New: `src/steam_store.*`** |

`src/steam_store.*` reads Valve's public, keyless storefront endpoints:

- `GET https://store.steampowered.com/api/storesearch/?term=<q>&l=en&cc=US`
- `GET https://store.steampowered.com/api/featuredcategories?cc=US&l=en`
  (`top_sellers.items[]`)

It parses at most 60 rows, de-duplicates by app id, bounds the response to 4 MiB,
uses a 12 s deadline, refuses redirects, sends no cookies or credentials, and
cancels in flight work with a generation counter (the same discipline as
`steam_friends.cpp`). It never buys, downloads or launches: it only lists rows and
builds handoffs.

```
--steam-store-report <json-file> [--steam-store-query <term>]
    Query the public store (top sellers when no term) and write JSON, no window.
```

Each row carries `appId`, `title`, `image`, `price`, `discounted`, `windows`,
`install` (`steam://install/<id>`) and `storePage`. Verified against the live
store: search `portal` returned 10 rows (`Portal 2`, `Portal`, …) and top sellers
returned 7 unique rows after de-duplication.

## Design: three in-XMB programs

All three are ordinary Game/Media column entries; none adds a new window chrome.
They reuse the existing PKG-style row presentation (wide header art, title
position, focus scale, column easing) so a store row is visually indistinguishable
from an installed title.

### 1. Steam Store (Games → Steam Store)

Opens as a **page** in the Game column, not a modal:

- First row is a search field: OK opens the existing on-screen keyboard
  (`Osk` in `view.h`), submitting calls `SteamStore::search(term)`.
- Rows below are the results (title, price, artwork). The focused row scales like
  an item icon.
- OK on a row opens a small detail panel: **Install in Steam**, **Store Page**,
  **Back**.
  - *Install in Steam* → `SteamStore::installUrl()` → the shell's existing
    `OpenUrl`/`LaunchSteam` path (`steam://install/<id>`), so Steam shows its own
    install dialog. Steam remains the owner of download, DRM and updates.
  - *Store Page* → `storeUrl()` in the system browser.
- Empty search, network failure, timeout and oversized response each show an
  explicit message row instead of a fabricated list.
- Rows whose title is already installed (matched by app id against
  `scanSteam().games`) render as **Installed** and OK launches the game instead of
  prompting an install.

### 2. Import Games and Apps (Games → Import)

The import action already exists (`Action::ImportApps`, F5-adjacent). Make it
self-describing inside the XMB:

- A page listing the manifest schema fields in plain language, plus **Choose
  Manifest…** which opens the file dialog and calls `content_registry::importFile`
  exactly as the CLI does.
- After a successful import the page lists the newly added ids, and the column
  refreshes through the existing `refreshContent` path.
- A failed import shows the registry's own error text (container, entry id,
  reason), matching the resource-failure rule in the ISA.

### 3. Media Apps (Music/Video/TV columns)

Media services are **content-registry entries** with `kind: "web"`, not a new
mechanism. `examples/media-apps.json` seeds Jellyfin, Plex, YouTube and Spotify.

- OK on a web row opens the service in the system browser (`OpenUrl`).
- The row uses the provider's favicon/artwork file when the manifest supplies
  `artwork`, otherwise the firmware fallback icon.
- Embedded playback is explicitly out of scope here: it needs an authenticated
  player per service and is tracked as future work in STEAM-INTEGRATION.md.

## Rules these programs keep

- No fabricated success: a failed search, import or launch reports its own error.
- Steam data stays read-only; nothing is written into the Steam directory.
- Network requests carry no cookies, session tokens, API keys or credentials, and
  store caches stay under the native data directory.
- Steam owns purchase/download/DRM/launch. The XMB only hands off.
- Personal reports, caches and captures stay outside Git.

## Remaining work, in order

1. Wire `SteamStore` into `Shell` (search page, detail panel, installed matching)
   and add the **Steam Store** entry to the Game column.
2. Import page text + post-import listing in the XMB.
3. Per-game ICON0/PIC1 overrides for installed Steam titles (custom artwork).
4. A registry/manifest editor page so imports do not need hand-written JSON.
5. Jellyfin as an authenticated service with an embedded player.
6. Verify an actual Steam install → launch → return lifecycle end to end.

Steps 1-2 touch `shell.cpp` / `view.cpp`, which another session is editing; the
model and CLI are landed first so the wiring is a small, additive change.
