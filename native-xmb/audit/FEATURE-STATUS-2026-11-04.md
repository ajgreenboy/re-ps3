# Native XMB feature status — 2026-11-04

> Updated 2026-11-07: the items below carry their own dated updates (playlists, users, DLNA, Jellyfin subtitles and quality).
> The plan of record for what to do next is [PHASES-2026-11-07.md](PHASES-2026-11-07.md).

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
   error. The Log Out / Automatic Login rows (listed disabled: this build has no logged-out state) remain.
   Updated 2026-11-08 (P4): avatar editing is implemented. Information on a user row now opens that user's
   Information page with two rows, Icon and User Name (`msg_user_name`; it opens the existing name page). Icon opens a
   page of choices: Default (`explore_plugin_full:tex_Avatar_Default`, the art users had before), the firmware's 27
   local-user icons and From a File. Firmware evidence: the decrypted 4.93 `user_plugin.prx`, `explore_plugin.prx`,
   `explore_category_user.prx` and `np_multisignin_plugin.prx` each carry the literal
   `/dev_flash/vsh/resource/explore/user/000.png`. That local dev_flash folder holds `000.png`..`026.png` (162x162
   RGBA). They are listed and read from dev_flash at run time and are never copied into the repo or the data folder.
   From a File lists the Windows Pictures folder (its top level and one level of subfolders, at most 64 pictures,
   each shown as a bounded thumbnail decode) under user_plugin's `msg_icon_image_select` ("Select an image to use as
   an icon."). A chosen picture (at most 32 MB and 8192 px a side, orientation applied) is centre-cropped to a square,
   scaled to 162x162 and saved atomically to `<data>/avatars/<id>.png`. The choice is stored per profile as
   `profiles.json` `"icon"` (`""` / `"fw:NNN"` / `"file"`; validated on load; written only when set, so a record
   without an icon is unchanged). Deleting a user, or switching away from `"file"`, removes the copied picture. The
   Users column draws the chosen icon in place of `tex_Avatar_Default`.
   Code: `Profiles::setIcon/importPicture/firmwareIcons/iconImage`; `Shell::openUserInfo/openUserIcons/
   openUserIconFiles/chooseUserIcon/refreshUserInfoPages`; `rebuildUsers`.
   **INFERRED:** the page layout. No RCO page lays the choice out: `--rco-tree user_plugin,user_info_plugin` shows only
   `page_create`, `page_create_dialog` and a childless `page_userinfo`. The pages are this build's ordinary row pages,
   and the labels Icon, Default, Icon N and From a File are this build's own.
   Verified with scripted runs on a scratch `--data-dir` (a copy of `out/native-dlna/data`), with `XMB_PICTURES_DIR`
   pointing at a scratch folder. Frames are in `C:\dev\out\p4\f\`: `r1-info.png` (the Information page),
   `r1-icons-focus2.png` (the icon page), `r1-users-after.png` (the Users column with `fw:001`), `r2-users-restart.png`
   (the icon persists after a restart), `r2-pictures.png` (the Pictures page), `r2-users-after.png` and
   `r3-users-restart-file.png` (a wide 1920x1080 picture, centre-cropped, persists after a restart). The saved picture
   is 162x162.
   Not done: the console's own default for a new user appears to be `explore/user/000.png` (the path user_plugin
   carries, and the look of `col-users-r02`), but the build keeps `tex_Avatar_Default` as the default so that existing
   frames do not change. The info-bar avatar slot (-376) is `view.cpp`'s and is still not drawn.
3. **DLNA/UPnP discovery.** Updated 2026-11-07: implemented (`audit/DLNA.md`). Search for Media Servers runs SSDP
   discovery and ContentDirectory browsing (`src/dlna_client.*`, `src/dlna_shell.cpp`); audio/video play through the
   existing player, photos open in the viewer; `<data>/dlna.json` lists servers multicast cannot reach. Verified on a
   local mock; this LAN has no DLNA server (Plex DLNA off, Jellyfin DLNA plugin absent), so a real server is unverified.
   Previously: the discovery rows were explicitly
   `Action::Unavailable` in `Shell::resolveMediaColumns()` (`src/shell.cpp`).
   Jellyfin is a separate working network-media integration; it does not provide
   general DLNA discovery.
4. **Firmware Photo Gallery application.** The `photo:seg_hakoniwa` row is
   explicitly unavailable in `src/shell.cpp`. The built-in photo library,
   full-screen viewer, slideshow, rotation, and zoom/pan are separate native
   features and do not implement the firmware's 3D Gallery app.
5. **Jellyfin image-based subtitles.** Updated 2026-11-07: text subtitle streams are fetched from the server
   as SRT and drawn like a sidecar (`audit/JELLYFIN-NATIVE.md`, "Server text subtitles"). Updated 2026-11-08
   (plan H9): image subtitles play. Local `.sup` (PGS) and `.idx`+`.sub` (VobSub, one track per `.idx`
   language) sidecars, with or without a language tag, are listed, decoded on a worker thread when chosen
   (`src/image_subtitles.*`, 64 MB cap) and drawn over the video at their authored position. A Jellyfin PGS
   stream is fetched as `.sup` and drawn the same way; any other image codec (VobSub/DVB), or a PGS stream the
   server will not deliver, is offered as server burn-in (re-opens the title as a transcode). Evidence:
   `audit/JELLYFIN-NATIVE.md`, "Image subtitles". Not verified on the real server: the `.sup` delivery route
   and the burn-in transcode (no credentials here); both are exercised against the local stub only.
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
- **Artwork:** current artwork sources and local overrides are supported. Updated 2026-11-07: RAWG
  automatic artwork lookup landed in commit d9e2af8 (`src/rawg_client.*`, `src/rawg_artwork.*`, `src/artwork_settings.*`): Game >
  Artwork Settings takes the RAWG key (or `RAWG_API_KEY`), library scans match installed Steam and imported games, matches are
  cached for offline use, and explicit local overrides win. Real-key end-to-end coverage is not recorded in this file.
- **Spotify (Updated 2026-11-07):** Music > Spotify browses the account and controls a Spotify Connect device
  (`src/spotify_client.*`, `src/spotify_shell.cpp`; `audit/SPOTIFY.md`). Verified on a real Premium account; the free-account
  path and the Liked Songs queue on a real device are not.
- **Tailscale (Updated 2026-11-07):** Network > Tailscale (`src/tailscale_client.*`, `src/tailscale_shell.cpp`; `audit/TAILSCALE.md`),
  verified on this PC's real tailnet except turning it off/on and exit-node selection, which were left alone on purpose.
- **Steam activity and actions (Updated 2026-11-07):** play time and last played from `localconfig.vdf` on the Game rows,
  installed titles ordered by last played, and Open in Steam Library / Store Page / Screenshots / Verify Game Files / Delete as
  `steam://` hand-offs (`src/steam_activity.*`, `src/shell.cpp`; commits aff3cb6, 4e64c31).

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
