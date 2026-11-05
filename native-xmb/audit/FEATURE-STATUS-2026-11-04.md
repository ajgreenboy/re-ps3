# Native XMB feature status — 2026-11-04

This is a current product-gap summary, cross-checked against source after the older
`FEATURE-COVERAGE.md` audit. That audit is useful historical context, but several
of its statements have since been superseded. This document separates definite
missing features from modern integrations that exist but need lifecycle evidence,
release verification, and exact PS3 fidelity.

## Definite product gaps

1. **Persistent playlists.** Music and Photo Playlists are present as explicit
   unavailable rows. `Shell::resolveMediaColumns()` in `src/shell.cpp` explains
   that current playback queues are built from the source folder; there is no
   stored playlist model or editor.
2. **Profile rename, deletion, and avatar editing.** The current `Profiles` API
   in `src/profiles.h` exposes `create()` and `select()`; `src/profiles.cpp`
   implements those operations and atomic persistence. Rename/delete/avatar
   operations are absent. Preferences are profile-scoped now: `Preferences`
   stores keys under `profile/<id>/...` (`src/preferences.cpp`); do not rely on
   the old audit's claim that all preferences are shared.
3. **DLNA/UPnP discovery.** Video, Music, and Photo discovery rows are explicitly
   `Action::Unavailable` in `Shell::resolveMediaColumns()` (`src/shell.cpp`).
   Jellyfin is a separate working network-media integration; it does not provide
   general DLNA discovery.
4. **Firmware Photo Gallery application.** The `photo:seg_hakoniwa` row is
   explicitly unavailable in `src/shell.cpp`. The built-in photo library,
   full-screen viewer, slideshow, rotation, and zoom/pan are separate native
   features and do not implement the firmware's 3D Gallery app.
5. **Jellyfin server subtitle playback.** `Shell::buildPlayerOptions()`,
   `chooseSubtitleTrack()` and the Jellyfin track mapping in `src/shell.cpp`
   expose the server's subtitle streams honestly, but subtitle rows are disabled:
   the app does not fetch/render server subtitle streams. Local sidecar subtitle
   playback is implemented. See `audit/JELLYFIN-NATIVE.md`.
6. **Jellyfin quality/bitrate selection.** There is no quality selector for
   server playback (`audit/JELLYFIN-NATIVE.md`). Direct play and server-negotiated
   HLS playback work. The page backdrop is fetched and cached, but its draw path
   remains open: `Shell::pageBackdrop()` exists in `src/shell.h` and has no view
   call site yet; see the same audit.
7. **Dynamic P3T theme scenes and font/color selections.** Static theme
   backgrounds, icons, and sounds are implemented. Dynamic RAF scenes and the
   theme `font` / `color` selections remain open (`README.md`,
   `HANDOFF-PHASES.md`, “Phase 5”).
8. **In-game XMB.** The in-game XMB state/workflow is absent; the existing game
   watcher can bring the native XMB forward and open its system menu, but does
   not provide the console's in-game XMB (`HANDOFF-PHASES.md`, “Phase 5”).

## Modern features already implemented

- **Local media:** asynchronous library browsing, photo view/slideshow/rotation/
  zoom and pan, video/music playback, queues, repeat/shuffle, seek/volume,
  local resume/Continue Watching, sidecar SRT/WebVTT subtitles, and the media
  control panels (`src/media_library.*`, `src/playback_queue.*`, `src/shell.cpp`,
  `audit/MEDIA-CONTINUATION.md`). Exact firmware panels remain a separate parity
  question.
- **Jellyfin:** native XMB sign-in page with opt-in DPAPI remembered session,
  browse/search, posters, resume/play-from-beginning, playback progress reports,
  direct play and server-negotiated HLS, plus audio-track selection when the
  playback backend confirms it can switch (`src/jellyfin_client.*`,
  `src/jellyfin_shell.cpp`, `src/shell.cpp`; `audit/JELLYFIN-NATIVE.md`).
- **Profiles and settings:** profile-specific preference storage and profile
  switching are implemented (`src/preferences.*`, `src/profiles.*`,
  `src/shell.cpp`). The lifecycle omissions above concern management operations,
  not preference scoping.
- **Steam:** installed/owned library, collections, Friends, downloads, account
  switching and launch/install handoffs have native surfaces and providers
  (`src/steam_*.cpp`, `src/shell.cpp`; `audit/STEAM-INTEGRATION.md`). They are
  not missing features; active real-client lifecycle coverage is still needed.
- **Artwork:** current artwork sources and local overrides are supported. RAWG
  automatic artwork lookup is being implemented in parallel; provider-key
  configuration and end-to-end verification are pending, so it is not counted
  as complete here.

## Verification still needed

- **Steam lifecycle:** run real-client scenarios through download start/progress/
  completion, friends/account refresh and launch/return where applicable. The
  download audit records that no download was active during its probe
  (`audit/STEAM-DOWNLOADS.md`); synthetic states and static scans are not a
  substitute for that lifecycle evidence.
- **Release evidence:** scripted evidence exists, including a ten-minute
  inactivity scenario and named-state captures. Expand the release run across
  representative media, settings, profile, Jellyfin, Steam, recovery and
  long-session paths. Harness repeatability is not proof of firmware parity
  (`audit/VERIFICATION-HARNESS.md`).
- **Exact firmware fidelity:** the native renderer now ports most major RSX
  shader paths, but remaining fitted/unmeasured layout, timing, text/material,
  state, and reference-capture differences remain. Use matched RPCS3 captures
  and the layer harness to measure each claim (`audit/PIPELINE.md`,
  `HANDOFF-PHASES.md`). This is a fidelity track, distinct from the eight
  missing product features above.

## Suggested next feature phase

Start with persistent playlists and the profile-management lifecycle, then
implement server subtitle playback and Jellyfin backdrop rendering. Decide
whether DLNA discovery and the firmware Photo Gallery are in scope for this
native application before treating their unavailable rows as defects. Keep
dynamic themes and in-game XMB as separately scoped feature slices. Complete
RAWG key configuration and verification after its implementation lands.
