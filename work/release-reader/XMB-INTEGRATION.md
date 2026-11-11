# XMB integration spec: game release index (import → list → download)

Follows the standalone reader (`work/release-reader/HANDOFF.md`, implemented `e0d770f`). This document is the
spec for the **in-XMB** feature, which the reader deliberately did not include.

## The feature, end to end

1. **Import a source from a link.** In the XMB, paste a URL to a release-index JSON. The app fetches it,
   normalizes it (the reader's logic, ported to C++), and stores the releases locally.
2. **List in the Game column under the source name.** Each imported source becomes a **folder** in the Game
   column, named after the source. Inside, one row per release (title + cover art).
3. **Grab metadata.** For each release, show cover art — from the JSON if it carries artwork/thumbnails, else a
   title match against the existing Steam/RAWG artwork clients, else a neutral placeholder.
4. **Download on click.** Selecting a release downloads whatever the JSON serves for it — **usually `.zip` or
   `.wav` (a soundtrack), but any file type** — from its direct download URI, to a local folder, with visible
   progress and a complete/error state. This is the core action, not an import-time batch.

## Data (from the reader's model)

Each release already has: `title`, `downloads[]` (classified `magnet`/`torrent`/`direct`, with `uri`,
`infohash`, `displayName`, `trackers`), `mirrors[]` (grouped direct parts), `publishedAt`, `sizeBytes`,
`sizeLabel`, and `raw`. The XMB needs the same normalization (title + download URIs + size), in C++.

## Download semantics

- Prefer the **direct** HTTP/HTTPS links (these are the `.zip`/`.wav`/any files). Multiple parts (mirror
  groups, `.part1.rar` etc.) are a possible later concern; the first pass downloads a single direct link.
- Magnet/torrent links are **not resolved** in the first pass (needs a torrent client / DHT). If a release has
  only magnet/torrent links, mark it "no direct download" honestly rather than pretending.
- Download to `<data>/downloads/<source>/<safe-title>.<ext>`, filename from the URI's last path segment
  (or Content-Disposition), sanitized. Reuse the existing download/progress plumbing (the busy page's
  determinate + indeterminate ring, `steam_downloads.cpp`'s fetch, `QNetworkAccessManager`).
- Show progress; a completed release is a local file the user can open/play; a failed download reports the error.

## Scope boundaries (keep honest)

- No torrent resolution, no tracker/DHT, no mirror heuristics, no unzip/extract in the first pass.
- Import stores the normalized release list (JSON in the data dir), so re-listing is offline after import.
- Only commit sanitized fixtures + the C++ source; never the real source JSON or downloaded files.

## Implementation shape (C++)

- `native-xmb/src/release_index.{h,cpp}` — C++ port of the reader's normalizers (`parseMagnet`,
  `classifyUri`, `parseSize`, `parseDate`, `asStringArray`) + the adapter dispatch (FitGirl shape + heuristic
  fallback), reading a fetched JSON into a `QVector<Release>`.
- `native-xmb/src/release_source.{h,cpp}` — the store (list of imported sources + their releases, persisted
  to `<data>/sources/*.json`), the fetch (URL → bytes → JSON → normalize), and the download (URI → file, with
  progress + error).
- Game-column integration in `shell.cpp` — a "Sources"/import row (paste URL via the OSK), one folder per
  source, releases with art; selecting a release triggers the download (or opens the local file if present).
- A CLI flag `--import-source <url>` (or `--sources-list` / `--download <source> <title>`) for headless test.
