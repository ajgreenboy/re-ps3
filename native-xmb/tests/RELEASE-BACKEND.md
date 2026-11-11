# Release backend integration

`release_index.h` exposes the shared normalizers and `parseReleaseIndex(document, &error)`.
The model is `xmb::release_index::Release`, also aliased as `xmb::IndexedRelease`:
`xmb::Release` was already taken by the application updater. `Download` and `MirrorGroup`
live directly in `xmb`. Missing sizes are `-1`; missing dates are invalid `QDateTime`s.
Sizes use binary units and rounded bytes, matching the TypeScript reference. The heuristic
fallback uses Qt's object-key iteration order (Qt JSON objects do not preserve original
insertion order); use a dedicated adapter when ambiguous title/link ordering matters.

Construct `xmb::ReleaseSources(dataDirectory, preferences)` with the existing active-profile
`Preferences` instance. It borrows that instance, so keep it alive throughout each operation.

- `importSource(QUrl, QString*)` returns success and atomically saves normalized releases.
- `listSources(QString*)` returns `ReleaseSource` records: `slug`, `name`, `url`, `releases`.
- `downloadRelease(sourceSlug, title, QString*, progress)` returns the completed local path,
  or an empty string on failure. Names are also accepted; slugs uniquely identify sources.
- The progress callback receives bytes received/total, or `(0, -1)` while waiting for debrid.
- Operations are blocking, bounded event-loop network calls: run them on a worker for UI use.
  Marshal progress to the UI thread and avoid changing the borrowed preferences concurrently.
- Direct links are preferred, including links in mirror groups. With no token they download
  normally. Magnet-only releases fail with `no Real-Debrid token configured` without a token.
- With a token, direct links are unrestricted. Magnets are added, polled, selected (`all`),
  and polled until downloaded. The first returned file link is unrestricted and downloaded.
  This does not assemble multipart mirrors or download every file in a torrent.
- Standalone `.torrent` URLs remain classified but are not submitted as magnets.
- Completed files are atomically written under `downloads/<source-slug>/<safe-title>.<ext>`;
  the extension comes from the resolved URL filename, defaulting to `bin`.

`RealDebrid` implements `unrestrict`, `addMagnet`, `torrentInfo`, `instantAvailability`, and
`selectFiles`. Every request reads `debrid/realDebridToken` from preferences; no logging of
the credential. Availability returns the API's hash/host JSON object unchanged. Authentication
errors and `error_details` are returned to callers; authenticated calls require a user's token.

## Verification

Enable `-DXMB_RELEASE_BUILD_TESTS=ON` in CMake, then build normally. Run:

```powershell
out/native-xmb/xmb-release-tests.exe work/release-reader/fixtures/fitgirl-sample.json
out/native-xmb/re-ps3-native.exe --import-source file:///C:/dev/xmb/work/release-reader/fixtures/fitgirl-sample.json --data-dir <temporary-directory>
```

Observed: **92 checks, 0 failures**, including all HANDOFF size cases, magnets, dates,
mirror ordering, both adapters, normalized persistence, all five missing-token API paths,
magnet orchestration without a token, and local HTTP streaming/progress/direct fallback.
CLI: **Imported 6 releases from Sanitized sample**, exit **0**. Missing local source exits **2**.
Authenticated Real-Debrid resolution/polling was not exercised (no token available).
