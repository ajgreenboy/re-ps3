# Native XMB feature status — 2026-11-04

This is a current product-gap summary, cross-checked against source after the older
`FEATURE-COVERAGE.md` audit. That audit is useful historical context, but several
of its statements have since been superseded. This document separates definite
missing features from modern integrations that exist but need lifecycle evidence,
release verification, and exact PS3 fidelity.

## Definite product gaps

1. **Playlist editor (Move, Add All).** Updated 2026-11-07: Music and Photo > Playlists are real. The folder
   lists Create New Playlist (category_music.xml seg_playlist_fixed_items) then the user's playlists with
   playlist_plugin's "%1 Tracks"/"%1 Images"; Create New Playlist asks for the name with msg_input_playlist_name
   and refuses a duplicate with msg_error_same_name_cannot_use; "Add to Playlist" (msg_add_to_playlist) on a local
   music/photo row opens "Select a playlist."; a playlist opens as a page of its files in order and Play queues
   the playlist (scripted run: "Track 1 of 2" from a two-item playlist in a three-file folder); Delete removes a
   row from a playlist or a whole playlist, never a file. Storage is `src/playlists.*`: one atomic JSON file per
   user under `<data>/playlists/`, removed with the user; limits 100 playlists and 1000 items are this build's
   (no firmware value traced). Updated 2026-11-07 (later): the editor's two commands exist as option rows. Inside a playlist an item
   offers Move Up / Move Down (disabled at the ends, focus follows the item; `PlaylistStore::moveItem`), and a
   local music/photo folder with more than one file offers Add All (playlist_plugin `msg_add_all_playlist`) into
   a chosen playlist, skipping files already listed (`PlaylistStore::addAll`, one save). Scripted run: Add All
   added 1 of 3 files to a playlist already holding two of them; Move Down reordered Alpha, Charlie, Bravo to
   Charlie, Alpha, Bravo on disk. Still open: the two-pane editor itself (expage_select_list /
   expage_edit_list, Cross to pick a row up) is not drawn.
2. **Avatar editing.** Updated 2026-11-07: rename and delete are implemented from explore_plugin's own user-row
   menu (`audit/FIDELITY-SURFACES-MAP.md`; `Profiles::rename/remove`, `Shell::renameUser/deleteUser`): Delete runs the
   traced two Yes/No asks and drops that user's settings, Information renames with the firmware's duplicate-name
   error. Avatar editing (user_plugin `msg_icon_image_select`) and the Log Out / Automatic Login rows (listed
   disabled: this build has no logged-out state) remain.
3. **DLNA/UPnP discovery.** Video, Music, and Photo discovery rows are explicitly
   `Action::Unavailable` in `Shell::resolveMediaColumns()` (`src/shell.cpp`).
   Jellyfin is a separate working network-media integration; it does not provide
   general DLNA discovery.
4. **Firmware Photo Gallery application.** The `photo:seg_hakoniwa` row is
   explicitly unavailable in `src/shell.cpp`. The built-in photo library,
   full-screen viewer, slideshow, rotation, and zoom/pan are separate native
   features and do not implement the firmware's 3D Gallery app.
5. **Jellyfin image-based subtitles.** Updated 2026-11-07: text subtitle streams are fetched from the server
   as SRT and drawn like a sidecar (`audit/JELLYFIN-NATIVE.md`, "Server text subtitles"). Image-based streams
   (PGS, VobSub) stay listed and disabled with that reason.
6. **Jellyfin quality/bitrate selection.** Updated 2026-11-07: done. "Streaming Quality" on a Jellyfin video's
   Triangle menu and "Quality" in the player panel pick a ceiling (Maximum 120 Mbps, 40, 20, 10, 8, 4, 2, 1 Mbps,
   720 kbps; default 20 Mbps, the old fixed value), stored as `jellyfin/maxBitrateKbps` and sent as PlaybackInfo's
   MaxStreamingBitrate; changing it while playing re-opens the title at the same position
   (`audit/JELLYFIN-NATIVE.md`, "Streaming quality"). Previously: no quality selector for
   server playback. Direct play and server-negotiated
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
