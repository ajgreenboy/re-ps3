# XMB integration spec: game release index (import → list → download via debrid)

Follows the standalone reader (`work/release-reader/HANDOFF.md`, implemented `e0d770f`). This document is the
spec for the **in-XMB** feature, which the reader deliberately did not include.

## The feature, end to end

1. **Import a source from a link.** In the XMB, paste a URL to a release-index JSON. The app fetches it,
   normalizes it (the reader's logic, ported to C++), and stores the releases locally.
2. **List in the Game column under the source name.** Each imported source becomes a **folder** in the Game
   column, named after the source. Inside, one row per release (title + cover art).
3. **Grab metadata.** For each release, show cover art — from the JSON if it carries artwork/thumbnails, else a
   title match against the existing Steam/RAWG artwork clients, else a neutral placeholder.
4. **Download on click.** Selecting a release hands its link to the user's **download manager** (Real-Debrid),
   which resolves it to a direct download that the app then fetches to the computer.

## Data (from the reader's model)

Each release already has: `title`, `downloads[]` (classified `magnet`/`torrent`/`direct`, with `uri`,
`infohash`, `displayName`, `trackers`), `mirrors[]` (grouped direct parts), `publishedAt`, `sizeBytes`,
`sizeLabel`, and `raw`. The XMB needs the same normalization (title + download URIs + size), in C++.

## Download semantics (via Real-Debrid)

The primary path is to hand the link to the debrid manager and then download the result.

- **Real-Debrid token**: a user-configured API token, stored in preferences, entered from a Settings row
  (never hardcoded, never committed). When set, clicking a release:
  - **Direct link** → `POST /unrestrict/link` (`link=<url>`) → returns a debrid `download` link → the app
    downloads that link to the computer.
  - **Magnet/torrent** → `POST /torrents/addMagnet` (`magnet=<magnet>`) → poll `GET /torrents/info/<id>` until
    `status == "downloaded"` (or `"waiting_files_selection"`) → `POST /torrents/selectFiles/<id>` → the info
    endpoint's `links[]` are the download links → the app downloads the chosen file.
  - Optional speed path: `GET /torrents/instantAvailability/<infohash>` to prefer the already-cached copy.
  - API base `https://api.real-debrid.com/rest/1.0/`, `Authorization: Bearer <token>`.
- **No token (or a non-magnet/direct link the debrid cannot take)** → fall back to a direct download of the
  release's direct link; mark magnet/torrent-only releases "no direct download" honestly.
- **Download to disk**: fetch the (debrid) direct link to `<data>/downloads/<source>/<safe-title>.<ext>`,
  filename from the URI's last path segment (or `Content-Disposition`), sanitized. Reuse the existing
  download/progress plumbing (the busy page's determinate + indeterminate ring, `steam_downloads.cpp`'s fetch,
  `QNetworkAccessManager`). Show progress; a completed release is a local file; a debrid error or failed
  download reports the error with the debrid's message.

## Scope boundaries (keep honest)

- The debrid **token is a secret** — read it from preferences at runtime, never log or commit it.
- No torrent client, no DHT, no mirror heuristics; the debrid does the torrent resolution.
- No unzip/extract in the first pass (the downloaded file stays a `.zip`/`.wav`/whatever on disk).
- Import stores the normalized release list (JSON in the data dir), so re-listing is offline after import.
- Only commit sanitized fixtures + the C++ source; never the real source JSON, downloaded files, or a token.

## Implementation shape (C++)

- `native-xmb/src/release_index.{h,cpp}` — C++ port of the reader's normalizers (`parseMagnet`,
  `classifyUri`, `parseSize`, `parseDate`, `asStringArray`) + the adapter dispatch (FitGirl shape + heuristic
  fallback), reading a fetched JSON into a `QVector<Release>`.
- `native-xmb/src/real_debrid.{h,cpp}` — the debrid client: `unrestrict(directUrl)`, `addMagnet(magnet)`,
  `torrentInfo(id)`, `instantAvailability(infohash)`, `selectFiles(id, files)`, returning the download links
  (or an error with the debrid's message).
- `native-xmb/src/release_source.{h,cpp}` — the store (imported sources + releases persisted to
  `<data>/sources/*.json`), the source fetch (URL → bytes → JSON → normalize), and the download orchestration
  (link → debrid → direct link → file, with progress + error).
- Game-column integration in `shell.cpp` — a "Sources"/import row (paste URL via the OSK), one folder per
  source, releases with art; selecting a release triggers the debrid→download (or opens the local file if
  present).
- A Settings row for the Real-Debrid token; a CLI flag `--import-source <url>` (or `--sources-list` /
  `--download <source> <title>`) for headless test.
