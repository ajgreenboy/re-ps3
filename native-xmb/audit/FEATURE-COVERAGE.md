# Feature coverage audit: ISC-13/14/15/16/17/20

Date: 2026-10-25. Scope: ISA criteria ISC-13, ISC-14, ISC-15, ISC-16, ISC-17 and
ISC-20 in `ISA.md`. This is a source-reading audit, not a runtime audit: no build
or interactive session was performed for it, and no Git command was run. Runtime
statements below are labelled as records from the existing audit documents, not
as observations made here.

## Method and evidence classes

Read for this audit:

- `ISA.md` (criteria and test strategy) and `audit/FEATURE-CATALOG.md`,
  `audit/FUNCTIONS.md` for the intended surface.
- `src/shell.cpp`, `src/settings_menu.cpp`, `src/host.cpp`, `src/profiles.cpp`,
  `src/profiles.h`, `src/preferences.cpp`, `src/preferences.h`,
  `src/media_library.cpp`, `src/media_library.h`, `src/playback_queue.cpp`,
  `src/photo_viewport.cpp`, `src/photo_viewport.h`, `src/view.cpp` (media,
  wallpaper, input and on-screen keyboard regions), `src/p3t.h`,
  `src/steam_library.cpp`, `src/content_registry.cpp`, parts of `src/assets.cpp`,
  `src/main.cpp`, `src/pad.h`, `src/interaction_audio.h`.
- Existing records: `audit/MEDIA-CONTINUATION.md`, `audit/HANDOFF.md`,
  `audit/README.md`, `audit/STEAM-INTEGRATION.md`.

Evidence classes used in the verdicts:

- **Code-proven**: the operation exists end to end in source (UI entry, state,
  side effect, failure path).
- **Recorded run**: an audit document records a local GUI run. Not reproduced in
  this audit.
- **Cosmetic**: a value is stored but no rendered surface reads it.
- **Placeholder**: the row exists and answers with an explicit unavailable
  message (`Action::Unavailable`, `shell.cpp:680`).

Path correction: this repository has no `src/paf/` directory. The Steam provider
sources are `src/steam_*.cpp` and were read at that location. The only "paf"
code is `View::PafEase` (menu easing) in `src/view.cpp:81-112`.

## Verdict summary

| Criterion | Verdict | One line |
|---|---|---|
| ISC-13 | partial | Create/select and persisted settings are real; no delete/rename/avatar/logout, no user-scoped preferences, no recovery path. |
| ISC-14 | partial | Theme/sound/date-time/power/fullscreen have real backends; display/input/output are read-only or absent, 6 of 16 groups have no working setting, one field is cosmetic. |
| ISC-15 | supported (narrow) | Browse, view, rotate and slideshow all exist end to end; the original Photo panel, EXIF, effects, playlists and device sources are absent. |
| ISC-16 | partial | Playback, queue, repeat/shuffle, seek and volume are real; metadata and playlists are enumerated by the criterion and absent. |
| ISC-17 | partial | Playback, queue, seek and volume are real; subtitles are enumerated by the criterion and absent. |
| ISC-20 | partial | RPCS3/Steam/imported-app launch works as a detached spawn; no configured backend setting and no return lifecycle tracking. |

Counts: 1 supported, 5 partial, 0 unsupported.

The single "supported" is deliberately narrow; see its caveats. If the standard
for ISC-15 is original-panel parity rather than the four enumerated operations,
it drops to partial.

## ISC-13: user/profile creation, selection and persistent settings

**Claim.** User/profile creation, selection and persistent settings work.

**Where implemented.**

- Profile model and file format: `src/profiles.cpp:55-132` (load and validate
  `{version:1, profiles, activeId}`; single default `User` at `:61-66`), atomic
  commit `src/profiles.cpp:147-168` (`QSaveFile`, no direct-write fallback,
  memory updated only after commit), create `:170-186`, select `:188-196`.
- Name rules: 1-16 Unicode scalar values, trimmed, no control/line/paragraph
  separators, duplicate names rejected case-insensitively, 16 profiles maximum
  (`src/profiles.cpp:25-45,170-186`).
- UI: Users category `src/shell.cpp:379-380`; rows rebuilt at
  `src/shell.cpp:842-851`; create/select dispatch `src/shell.cpp:628-630`;
  create/select callbacks `src/shell.cpp:852`; text entry through the native
  on-screen keyboard `src/view.cpp:55-56,1157-1190`.
- Settings persistence: `src/preferences.cpp:48-102`. Version 1 JSON, atomic
  commits, validated values, preserved invalid/newer file (writes disabled,
  error reported), per-key change signal, legacy INI migration for four keys.
- 22 accepted keys, listed by the validation/typing rules in
  `src/preferences.cpp:27-47`; defaults `:13-26`; consumers applied at
  `src/shell.cpp:807-815` and read throughout `src/settings_menu.cpp` and
  `src/view.cpp`.

**What the code proves.**

- Create and select write `profiles.json` under the data directory
  (`src/main.cpp:145`; `src/profiles.cpp:56-57`) and reload it at next launch;
  the active ID is persisted and validated on load (`src/profiles.cpp:125-131`).
- All mutations are synchronous and fail with explicit messages for unwritable
  directories, invalid names, duplicates and the profile limit.
- A corrupt, newer or out-of-schema `profiles.json` sets the service read-only
  (`src/profiles.cpp:67-70`) and every later mutation reports the preserved-file
  error (`:140-145,148,172,190`).
- Preferences are genuinely consumed by the renderer and services:
  appearance colour/background/font/wallpaper/theme (`src/view.cpp:247-301,
  725-731,768-772`; `src/shell.cpp:817-829`), date/time formats and IANA zone
  (`src/shell.cpp:173-184`), audio volume/mute/key tone
  (`src/shell.cpp:808-811`), photo interval/fit/repeat and media
  repeat/shuffle (`src/shell.cpp:733-737,796-803,813-814`), idle exit
  (`src/shell.cpp:562-565`), fullscreen (`src/main.cpp:500-501`), library
  folders (`src/shell.cpp:638-644,692-694`).

**Untested or missing.**

- No rename, delete, avatar/icon editing, console account, logout or automatic
  login control. The service surface is create/select only
  (`src/profiles.h:17-43`); `FUNCTIONS.md:34-37` lists these as required.
- Preferences are global, not user-scoped: `src/profiles.h:15` states it, and
  there is exactly one `Preferences` instance (`src/shell.cpp:112,371-374`).
  Switching users changes the active name and nothing else. `library/photo`,
  `library/music`, `library/video` and every setting are shared.
- The active user is drawn in only one place: a subtitle under the Trophy
  Collection and PS3 saved-data rows (`src/shell.cpp:853-856`,
  `src/view.cpp:887-898`). It is not in the info bar
  (`src/view.cpp:920-924` draws the clock only), so a user switch is nearly
  invisible.
- No in-app recovery for a corrupt `profiles.json`; recovery requires deleting
  or repairing the file outside the app.
- No confirmation/cancel flow, no startup user-selection screen, no
  "login" state; selecting a user is an immediate active-ID write.
- Not observed at runtime in this audit. `audit/HANDOFF.md:49,51` records
  compile/source review only and "no deletion/logout/account or per-user
  preferences"; full persistence/failure workflows are unobserved.
- Minor: window geometry uses a separate non-versioned INI
  (`src/main.cpp:465,497-498`) outside the preferences document.

**Verdict: partial.** A real, atomic local name-and-active-ID service with a
shared, atomically persisted preference document; not a PS3 user session, with
no user-scoped data and no profile lifecycle beyond create/select.

## ISC-14: theme, display, sound, date/time, input and power backends

**Claim.** Theme, display, sound, date/time, input and power options have native
backends.

**Where implemented.** All 16 groups exist in the Settings column
(`src/shell.cpp:381-389`) and are defined in `src/settings_menu.cpp:48-142`.
Original English row labels/explanations are read from `sysconf_plugin` where
present (`src/settings_menu.cpp:50-52,70,127`).

- Theme: `src/settings_menu.cpp:55-61` with choices `:17-25`. Theme file picker
  `src/settings_menu.cpp:191-197`; `.p3t` reader `src/p3t.h:14-46`; load and
  apply `src/shell.cpp:817-829`; icons `src/view.cpp:768-772`; static theme
  background `src/view.cpp:134-138`; theme sounds
  `src/shell.cpp:821,827` and `src/interaction_audio.h:24-26`.
- Display: `src/settings_menu.cpp:103-107`; host display report
  `src/host.cpp:43-51,149`; persisted fullscreen `src/preferences.cpp:39`,
  applied at `src/main.cpp:500-501`; F11 toggle `src/view.cpp:1055`.
- Sound: `src/settings_menu.cpp:84-92`; host endpoint volume/mute via Windows
  Core Audio `src/host.cpp:28-42,151-179`; XMB volume/mute/key tone applied at
  `src/shell.cpp:808-811`; original interaction cues
  `src/interaction_audio.h:10-28`.
- Date/time: `src/settings_menu.cpp:62-67`; date format, hour format and IANA
  zone consumed by the clock `src/shell.cpp:173-184`. The "Date and Time" row is
  a read-only report of the host clock (`src/settings_menu.cpp:64-65`); the host
  clock is never written.
- Power: `src/settings_menu.cpp:108-111`; idle auto-close
  `src/shell.cpp:562-565`.
- Input: no settings rows exist. Accessory Settings is an instructions
  information page plus one unavailable row (`src/settings_menu.cpp:112-116`).
  The actual fixed input layer is XInput plus HID PlayStation controllers
  (`src/view.cpp:1068-1126`, `src/pad.h:4-17`) and the keyboard
  (`src/view.cpp:1009-1063`), with a hard-coded held-key repeat schedule
  (`src/view.cpp:1083-1088`).

**What the code proves.**

- Real persisted backends: theme colour (6 palettes), background mode
  (Original/Classic/Wallpaper), font (3 firmware families,
  `src/assets.cpp:74-92`), user wallpaper, `.p3t` theme with static background,
  icon table and cue sounds; fullscreen; XMB volume/mute/key tone; Windows
  endpoint volume/mute; date/time display formats and time zones; idle
  auto-close; photo and media playback options.
- Read-only host reporting rendered as original-style information pages: system
  information, connection status, audio output device and video output
  (`src/host.cpp:89-149`; information-page detection
  `src/view.cpp:703-710`; two-column rendering `src/view.cpp:945-959`).
- Explicit unavailability instead of fabricated success: 28 of the 58 Settings
  rows answer `Action::Unavailable` with the message at `src/shell.cpp:680`.

**Cosmetic, read-only and dead rows.** Settings-row totals below used the
definitions in `src/settings_menu.cpp:55-140`.

- **Cosmetic (stored, never rendered): System Name** (`system/name`). It is
  written through the on-screen keyboard (`src/settings_menu.cpp:73,198`;
  validation `src/preferences.cpp:43`) and echoed only in its own row
  (`src/settings_menu.cpp:167`). System Information displays
  `QHostInfo::localHostName()` instead (`src/host.cpp:102`), and no other source
  file reads `system/name`.
- **Read-only reports (not settings):** Date and Time (host clock), System
  Information, Video Output Settings, Audio Output Settings, Connection Status
  List, and the Accessory instructions page.
- **Unavailable placeholders:** all of System Update (except the explanation
  row), all of Game Settings (one row), Chat Settings (3), Security (1),
  Remote Play (2), Printer (2), Network configuration/test/browser (4),
  Controller Auto-Off, Manage Bluetooth Devices (1), TV Type (1),
  Audio Multi-Output (1), and 10 of the 12 System Settings rows (System
  Language, Keyboard Type/Entry Method, Media Server Connection, Format Utility,
  Backup Utility, Restore Default Settings, Restore PS3 System, Install Other
  OS, About PlayStation 3). "Update via Internet" is an explanatory information
  page only.
- **Not the original page schema:** Music Settings is Repeat/Shuffle/Volume
  (`src/settings_menu.cpp:93-97`), not CD Import, Crossfade, Output Frequency,
  Dynamic Normalizer or Bitmapping (catalog XMB-039..043). Video Settings is the
  same three rows, not the BD/DVD language, caption, upscaler, HDMI, dynamic
  range, 24 Hz or 50 Hz rows (catalog XMB-017..037). Photo Settings
  (`:98-102`) is the only media page whose rows are native rather than borrowed.
- **Writes the host, not the console:** "System Volume" and "Mute" change the
  Windows default render endpoint (`src/host.cpp:158-179`;
  dispatch `src/shell.cpp:622-626`). That is a real backend, but a demo on
  another machine changes that machine's system volume.
- **Theme scope:** `.p3t` support is memory-only: still background, icon table
  and cue sounds. Dynamic themes' animated scene is explicitly listed but not
  drawn (`src/p3t.h:13`; log line `src/shell.cpp:828`). There is no theme
  install/list/delete library; themes are chosen with a host file dialog.

**Untested or missing.**

- Display backend is one boolean plus host reports: no TV type, output
  resolution/refresh control, RGB range, deep colour, super-white, cross-colour
  filter or screensaver.
- Input settings backend does not exist: no controller assignment, vibration,
  key repeat interval/speed, keyboard type or mouse settings; repeat timing is a
  constant.
- No video/audio output configuration, no multi-output, no device selection.
- Date/time cannot be set; no daylight-saving or automatic-time model.
- Power covers idle app exit only; no controller power, shutdown or restart.
- 6 of the 16 groups contain no working setting (System Update has only an
  explanation row; Chat, Security, Remote Play and Printer are all unavailable;
  Network has one read-only status page and four unavailable rows), and 2 more
  are effectively placeholder (Game, Accessory).
- Silent false-friend risk: `appearance/wallpaper` is ignored while a `.p3t`
  theme with a still background is active, because the theme background is
  checked first (`src/view.cpp:134-138`) while Theme Settings still shows
  "Wallpaper" as the selected background mode. The preference remains stored
  but has no visible effect until the theme is cleared.
- Runtime switching of Windows volume/mute and theme sounds was not observed
  here; `audit/HANDOFF.md:50` records the settings menu as a "host/native
  subset" with many page contents differing from the original.

**Verdict: partial.** Real backends exist for theme, sound, date/time formats,
power idle and fullscreen; display and input are read-only or absent, the
original page schemas are not reproduced, and one stored field is cosmetic.

## ISC-15: local photo browsing, viewing, rotation and slideshow

**Claim.** Local photo browsing, viewing, rotation and slideshow work.

**Where implemented.**

- Category surface: Photo Library, Photo Settings, Choose Media Folder
  (`src/shell.cpp:390`).
- Library selection and persistence: `src/shell.cpp:638-644` (stored
  `library/photo`, else request the picker), picker `src/view.cpp:51`,
  `src/shell.cpp:692-694` (persists then opens), directory page
  `src/shell.cpp:696-703`.
- Scanning: `src/media_library.cpp:64-129` (non-recursive, hidden files skipped,
  directories included, photo extensions `:45`, 5,000-entry cap `:30,:98-101`,
  sorted directories-first then case-insensitive title), asynchronous with
  generation-stamped cancellation and stale-result suppression
  `src/media_library.cpp:145-177`, results folded into the page at
  `src/shell.cpp:533-550`.
- Decode and display: `src/shell.cpp:704-719` (worker-thread `QImageReader`,
  `setAutoTransform` for EXIF orientation, 65536-pixel guard, downscale above
  3840x2160, failure message); frame hand-off `src/view.cpp:45-50`; full-screen
  draw with fit/fill `src/view.cpp:732-738`; fit/fill preference
  `src/preferences.cpp:34`, `src/view.cpp:48,735`.
- Previous/next and rotation: `src/shell.cpp:586-588` (horizontal steps photo),
  `src/shell.cpp:597-599` (vertical rotates by 90 degrees while a photo is open),
  `src/shell.cpp:790-800` (`stepPhoto`, wrapping only when slideshow repeat is
  on), transform at `src/shell.cpp:791`; keyboard shortcuts
  `src/view.cpp:1029-1042`; controller mapping `src/view.cpp:1099-1111`.
- Zoom/pan/reset: geometry in `src/photo_viewport.cpp:16-110`; wiring in
  `src/view.cpp:975-997`; controls `src/view.cpp:1030-1039,1107-1108`.
- Slideshow: timer `src/shell.cpp:551`; start/pause `src/shell.cpp:801-803`;
  interval and repeat settings `src/settings_menu.cpp:100-102`
  (`photo/interval` validation `src/preferences.cpp:36`; `media/repeat` at
  `src/shell.cpp:796`); stop on Back `src/shell.cpp:688`.

**What the code proves.**

- The full path exists: pick folder -> list supported images and subdirectories
  -> open -> decode off the UI thread -> draw -> rotate/zoom/pan -> advance ->
  slideshow start/stop -> Back cancels and clears.
- Photo files are never modified: rotation and zoom are display transforms
  (`src/shell.cpp:791`, `src/photo_viewport.cpp`), matching the "no save" scope.
- Bounded and cancellable: 5,000 entries, 65,536-pixel source guard,
  3,840x2,160 decode cap, stale-result generation check, and slideshow pause
  when the user zooms or pans.
- `audit/MEDIA-CONTINUATION.md:40-47` records five local GUI replays, including
  a photo run (fit 120%, zoom to 200%, pan on both axes, reset, rotation, next
  photo, Back) with saved frames inspected. That is recorded-run evidence, not
  reproduced here.

**Untested or missing.**

- No EXIF display and no other metadata: EXIF orientation is honoured through
  `QImageReader::setAutoTransform` (`src/shell.cpp:715`), but no EXIF value is
  exposed; list detail is only size and modified date (`src/shell.cpp:543`).
- No original control panel (Triangle), no display-mode panel, no change-effect
  transitions; a slideshow frame swap is instantaneous.
- No trimming, copy, delete, print, "set as wallpaper from this photo", Photo
  Gallery, playlists, or PSP playlist export (`FUNCTIONS.md:81-102`).
- No thumbnails/grid, no date/album/format grouping, no recursive media
  database, no device discovery; one folder at a time. Hidden files are
  invisible, and the flat scan has no watch/refresh-on-change.
- Extension list admits formats the codec stack may reject; failures produce a
  dialog but no per-file diagnostic.
- Rotation is not saved to the file (correct as scope, but there is also no
  "save rotated copy" operation).
- Zoom percentages refer to the bounded decoded image, not original resolution
  (`audit/MEDIA-CONTINUATION.md:19-20`).
- Not re-run in this audit; fixtures were synthetic. No evidence for large
  panoramas, orientations 5-8, CMYK/greyscale, or corrupt files beyond the
  recorded failure paths.
- The demo path starts in a Windows folder dialog, not an XMB library screen.

**Verdict: supported (narrow).** All four enumerated operations exist end to end
with code and recorded runs; the Photo family beyond them (panel, EXIF,
transitions, playlists, wallpaper capture, device sources) is not implemented.

## ISC-16: local audio browsing, playback, metadata, playlists and controls

**Claim.** Local audio browsing, playback, metadata, playlists and controls
work.

**Where implemented.**

- Surface: Music Library, Music Settings, Choose Media Folder
  (`src/shell.cpp:391`).
- Browsing: same scanner as Photo with music extensions
  (`src/media_library.cpp:46`); rows are folders/files with size and date
  (`src/shell.cpp:533-550`); rows use a static album icon, not embedded art
  (`src/shell.cpp:541`).
- Queue and playback: `src/shell.cpp:720-729` snapshots the page's file rows
  into an owned `PlaybackQueue`; repeat mode and shuffle restored
  (`src/shell.cpp:733-737,727-728`); `startQueuedMedia` at `:738-749`
  (`QMediaPlayer::setSource`, `play`, selection kept in sync); player outputs to
  `QAudioOutput` at `:530`.
- Queue semantics: `src/playback_queue.cpp:9-121` (shuffle order without
  replacement, repeat one/all, manual previous/next ignoring repeat-one, bounded
  history `:98-101`).
- End/error handling: automatic advance on `EndOfMedia`
  (`src/shell.cpp:552-561`); failures skipped with a bounded failure count and
  no infinite retry through repeat-one (`src/shell.cpp:750-768`); errors from
  the player routed into the same path (`:571-575`).
- Controls: play/pause `:801-806`; previous/next `:769-774`; seek 10-second
  steps while media is open `:584-588`; repeat cycle `:775-777`; shuffle
  `:778-780`; volume/mute `:808-811`; position/duration `:830-831`; overlay text,
  progress bar and clock `src/view.cpp:740-749`; queue summary
  `src/shell.cpp:781-789`.
- Settings: `src/settings_menu.cpp:93-97` (repeat, shuffle, volume).

**What the code proves.**

- A real decoder/mixer path exists, one file at a time, with a stable queue and
  correct repeat/shuffle/previous/next semantics, position and duration,
  seek, volume and error skipping.
- Settings persist (`media/repeatMode`, `media/shuffle`, `audio/volume`) and are
  re-applied without restart (`src/shell.cpp:807-815`).
- Playback is not fabricated: failures surface as dialogs and end the queue
  rather than reporting success.

**Untested or missing.**

- **Metadata: absent.** A scan record's title is the file name
  (`src/media_library.cpp:106-107`, reused as the row title at
  `src/shell.cpp:541`). No artist, album, track number, genre, embedded
  duration or artwork is read anywhere in `src/`. ISC-16 enumerates metadata,
  so the criterion is not met.
- **Playlists: absent.** No playlist model, file format or UI in `src/`.
- No visual player, mini panel, CD import, crossfade/DSP, output frequency,
  dynamic normalizer, bitmapping, music icon change, or media database.
- No background playback: while media is open the shell routes input to the
  player and Back stops it (`src/shell.cpp:581-603,684-691`). No "music keeps
  playing while browsing".
- Controls are host-player grade: no stop command, no accelerated or variable
  fast forward/reverse, no resume position, no sorting/grouping, no queue
  editing.
- Extension list includes formats the host stack may not decode; behavior is
  queue-level skip rather than capability reporting.
- `audit/MEDIA-CONTINUATION.md:40-51` records five GUI replays that verify
  sequencing and failure lifecycle with silent synthetic WAVs; they do not
  verify audibility, real codecs or devices.
- Not re-run in this audit.

**Verdict: partial.** Playback, queue, repeat/shuffle, seek, volume and error
handling are real; the criterion's "metadata" and "playlists" are not
implemented.

## ISC-17: local video browsing, playback, subtitles and controls

**Claim.** Local video browsing, playback, subtitles and controls work.

**Where implemented.**

- Surface: Video Library, Video Settings, Choose Media Folder
  (`src/shell.cpp:392`); video extensions `src/media_library.cpp:47`.
- Playback path: the same queue and player as audio
  (`src/shell.cpp:720-749`), with the `QVideoSink` frame signal forwarded to the
  renderer at `src/shell.cpp:530,569`; the renderer shows frames full-screen with
  letterboxing and draws title, status, progress bar and time
  (`src/view.cpp:732-750`).
- Controls: play/pause `src/shell.cpp:801-806`; seek 10 seconds
  `:584-588`; previous/next `:769-774`; repeat/shuffle `:775-780`; queue
  advance on end `:552-561`; error skip `:750-768`; Back stops and clears
  `:684-691`; fullscreen via `display/fullscreen`.
- Settings: the same three generic rows as Music Settings
  (`src/settings_menu.cpp:93-97`).

**What the code proves.**

- Local file playback works structurally: decode -> video frames -> render,
  with position/duration, seek, queue advance, repeat/shuffle and error paths.
- Same honest failure behavior as audio: rejected files are skipped, and an
  entirely failing queue stops with a message.

**Untested or missing.**

- **Subtitles: absent.** No subtitle or track API use exists in `src/` (no
  `QMediaMetaData`, `setActiveSubtitleTrack`, `setActiveAudioTrack` or
  chapter/timeline model). ISC-17 enumerates subtitles, so the criterion is not
  met.
- No audio-track/angle selection, chapters/scene search, A-B repeat, frame
  step, slow motion, instant replay/advance, time/go-to, screen-mode or aspect
  control, resume position, disc navigation, or protected-content handling.
- No metadata: titles are file names; no thumbnails in the list.
- Catalog drift: `XMB-218` (A-B Repeat) is marked partial but no A-B code
  exists; `XMB-211` (Fast Reverse/Forward) is a fixed 10-second seek, not
  variable scanning; `XMB-210` (Previous return-to-beginning / Next) is marked
  not implemented although the queue provides it.
- Extension list admits formats the host codec stack may reject; no capability
  report per format (`FUNCTIONS.md:140` already notes this).
- Not re-run in this audit; no recorded video run is referenced in
  `audit/MEDIA-CONTINUATION.md` beyond the generic media-player replays.

**Verdict: partial.** Browsing, playback, seek, queue and volume work; subtitles
are explicitly required by the criterion and absent, as is the rest of the
original video control surface.

## ISC-20: game launch backend and return lifecycle

**Claim.** Game launch integrates a configured execution backend and tracks
return lifecycle.

**Where implemented.**

- Installed-title discovery: `src/shell.cpp:65-127`. `PARAM.SFO` parser
  `:40-57`; category mapping `:74-75`; RPCS3 `games.yml` list `:78-94`;
  bootable test `:100-101`; rows become `Action::Launch` with either
  `%RPCS3_GAMEID%:<id>` or the `EBOOT.BIN` path `:109`; otherwise an information
  row with the reason `:106-108`.
- PS3 backend: `src/shell.cpp:59-64` locates `rpcs3.exe` (or `rpcs3`) in the
  directory above `dev_hdd0`; `src/shell.cpp:128-134` calls
  `QProcess::startDetached(emulator, {"--no-gui", arg}, emulatorDir)` and only
  reports failure.
- Steam backend: installed titles are scanned from app manifests with the
  fully-installed flag (`src/steam_library.cpp:494-540`, StateFlags test
  `:520-522`), roots from registry/env (`:421-455`, `XMB_STEAM_ROOT` `:424-425`),
  and launch is `steam.exe -applaunch <appid>`
  (`src/shell.cpp:646-651`).
- Imported-app backend: `Action::LaunchApp` starts the manifest executable with
  its declared argument array and working directory
  (`src/shell.cpp:668-673`; registry validated in
  `src/content_registry.cpp`).
- Configuration inputs: `--dev-flash`, `--dev-hdd0` (`src/main.cpp:82-83,290,315`),
  `--steam-root` / `XMB_STEAM_ROOT` (`src/main.cpp:95,146`), and directory
  layout (RPCS3 sibling of `dev_hdd0`). No backend preference key exists
  (`src/preferences.cpp:27-47`) and no Settings page configures one.

**What the code proves.**

- Launch is real, not a stub: an actual emulator or store client is started with
  the correct title argument, imported apps receive their argv/cwd, and failure
  to start is reported as a message (`src/shell.cpp:131,649,671`).
- Backend selection is deterministic from disk layout/registry/env at scan time;
  RPCS3 titles use its game ID when listed so RPCS3 reuses its licences and
  installed data (`src/shell.cpp:71-73,109`).
- Steam titles are filtered to fully installed manifests, so the list does not
  include incomplete downloads (already recorded as ISC-35 evidence).

**Untested or missing.**

- **Return lifecycle: entirely absent.** All three launch paths use
  `QProcess::startDetached` and keep no handle or signal
  (`src/shell.cpp:130,648,671`; no `QProcess` object is constructed anywhere in
  `src/`). The shell has no "game running" state, no exit notification, no
  return-to-XMB transition, no focus/selection restore, no Quit Game action, and
  no in-game XMB or PS overlay. The player/media state and idle timer are
  unaffected by a launch.
- **Configured backend: not user-configured.** There is no persisted setting
  for an emulator path, Steam path, launch arguments or per-title overrides.
  If `rpcs3.exe` is not beside `dev_hdd0`, every PS3 title silently degrades to
  an information row (`src/shell.cpp:108`) even when RPCS3 is installed
  elsewhere. Steam path is only overridable by environment variable or command
  line.
- No "Quit Game", no game settings consumption (PS upscaler/smoothing absent,
  catalog XMB-014/015), no process priority, no exclusive display/fullscreen
  handoff, no game-running window state.
- Disc copies without an `EBOOT.BIN` and without a `games.yml` entry cannot
  launch (`src/shell.cpp:100-101,109`).
- Idle auto-close keeps running while a game is up: only `mediaOpen_` is exempt
  (`src/shell.cpp:562-565`), so a set auto-off can quit XMB behind a running
  game.
- ISA's own follow-up criteria are unchecked: ISC-37 (real Steam title launches
  and returns with verified lifecycle) remains open in `ISA.md`, and
  `audit/HANDOFF.md:187` (Phase C) lists emulator launch/return services as
  future work.
- Not observed at runtime in this audit; no launch/return evidence was recorded
  for RPCS3 or Steam here.

**Verdict: partial.** A working detached launch path with three real backends,
but the backend is discovered rather than configured in the product, and the
return lifecycle half of the criterion does not exist.

## Settings row ledger (ISC-13/14 support)

Distinct accepted keys: 22 (`src/preferences.cpp:27-47`). Three are media-library
folder paths set by the host folder picker; of the 19 remaining keys exposed as
settings rows, 18 are consumed by a rendered surface or service and 1
(`system/name`) is cosmetic. Two further controls write the Windows audio
endpoint and are intentionally not persisted (`host/volume`, `host/muted`,
`src/settings_menu.cpp:147-148`, `src/shell.cpp:622-626`).

| Group (row count) | Real persisted | Writes host | Read-only report | Unavailable placeholder |
|---|---|---|---|---|
| Theme (6) | 5 keys + theme file picker | - | - | - |
| Date and Time (4) | 3 | - | 1 (host clock) | - |
| System (12) | 1 (cosmetic name) | - | 1 | 10 |
| System Update (2) | - | - | 1 (explanation) | 1 |
| Sound (7) | 3 | 2 | 1 | 1 |
| Music (3) | 3 (generic, not original page) | - | - | - |
| Video (3) | 3 (generic, not original page) | - | - | - |
| Photo (3) | 3 | - | - | - |
| Display (3) | 1 | - | 1 | 1 |
| Power Save (2) | 1 | - | - | 1 |
| Accessory (2) | - | - | 1 (instructions) | 1 |
| Chat (3) | - | - | - | 3 |
| Security (1) | - | - | - | 1 |
| Remote Play (2) | - | - | - | 2 |
| Printer (2) | - | - | - | 2 |
| Network (5) | - | - | 1 | 4 |
| Game Settings (1) | - | - | - | 1 |

Notes: the theme background/wallpaper interaction described under ISC-14 means
`appearance/wallpaper` can be stored and invisible while a static theme
background is active. The "System Volume"/"Mute" rows are real host writes, not
PS3 audio configuration.

## Documentation drift found while auditing

- `audit/FEATURE-CATALOG.md` statuses lag the code: XMB-149 (Zoom In/Out) and
  XMB-177 (Shuffle) are marked not implemented but are implemented
  (`src/view.cpp:975-997`; `src/playback_queue.cpp:58-67`); XMB-170/171
  (Previous/Next) are marked not implemented but the queue provides them
  (`src/shell.cpp:769-774`); XMB-218 (A-B Repeat) is marked partial but no A-B
  code exists.
- `audit/FUNCTIONS.md:99-102` still says photo zoom/pan is absent; it now exists
  (`src/photo_viewport.cpp`, `src/view.cpp:975-997`).
- `audit/FUNCTIONS.md:118-121` describes a boolean repeat only; the three-mode
  repeat plus shuffle is implemented (`src/playback_queue.cpp`).
- This audit was asked to read `paf/steam_*.cpp`; that path does not exist. The
  Steam sources are `src/steam_*.cpp`.

## What a demo would not survive

Ordered by how likely a user probing the app is to hit them.

1. **Settings tour dead ends.** Six groups contain no working setting; most rows
   answer "This feature is not available in the native recreation yet"
   (`src/settings_menu.cpp:68-140`), with System Settings itself at 10 of 12
   rows unavailable. Anyone opening Network, Chat, Security, Remote Play,
   Printer or System Update finds rows that only report or refuse.
2. **Rename the console, then look at System Information.** The new name is
   stored but never shown; the page reports the Windows host name
   (`src/settings_menu.cpp:198` vs `src/host.cpp:102`).
3. **Video Settings is Music Settings.** Three generic rows where the reference
   expects language, captions, upscaler, HDMI, dynamic-range and refresh rows;
   the same for Music Settings vs CD import/crossfade/normalizer/bitmapping.
4. **Second user changes nothing.** No rename/delete/logout, and preferences,
   wallpaper, theme and library folders are shared, so a new profile inherits
   everything and switching users is visually almost invisible
   (`src/profiles.h:15`, `src/shell.cpp:842-856`).
5. **Photo workflow starts in a Windows folder dialog and ends at a static
   viewer.** No Triangle panel, EXIF, thumbnails, effects, trim/copy/print,
   wallpaper-from-photo, or Photo Gallery; unsupported formats fail with a
   single dialog.
6. **Music/video show file names, not media metadata, and have no playlist.**
   The overlay is a host player with a title line and progress bar; MKVs with
   unsupported codecs or subtitles disappoint; seeking is fixed 10 seconds.
7. **Back stops playback.** There is no music-while-browsing model, no mini
   player, no PS overlay, so any demo of "listen while using XMB" fails
   (`src/shell.cpp:684-691`).
8. **Game launch is fire-and-forget.** No Quit Game, no return state, no
   in-game XMB, no configured emulator path (PS3 titles require `rpcs3.exe`
   beside `dev_hdd0`, Steam needs `steam.exe` and a running client), and the
   idle timer can quit XMB behind a running game.
9. **Host side effects.** System Volume/Mute edit the Windows default output
   device; Full Screen persists; the wallpaper picker stores an absolute host
   path. On another machine these are visible side effects on the host, not the
   recreation.
10. **Recovery by file surgery.** Corrupt `profiles.json` or `preferences.json`
    disables writes with no in-app reset
    (`src/profiles.cpp:67-70`, `src/preferences.cpp:52-58`).

## Prioritised next work (5 items)

1. **Launch lifecycle and a configured backend (closes ISC-20, unblocks
   ISC-37).** Add a typed backend preference (emulator path/args, Steam path)
   and a small activity model owning a `QProcess`: running state, exit signal,
   return focus/selection, Quit Game, suspend the idle timer, refresh on return.
   Files: `src/shell.cpp:59-64,128-134,646-673`, `src/shell.h:110-182`,
   `src/preferences.cpp:27-47`, `src/settings_menu.cpp` (new Game/System backend
   row), `src/main.cpp:500-501`.
2. **Per-profile storage and a real user lifecycle (closes most of ISC-13).**
   Move preferences and library paths under the active profile directory,
   migrate the shared document, and add rename/delete plus a startup user
   selection and confirmation flow; handle corrupt-file recovery in the UI.
   Files: `src/profiles.cpp/.h`, `src/shell.cpp:371-374,842-856`,
   `src/preferences.cpp:48`, `src/main.cpp:145`.
3. **Media metadata and the original control panel (raises ISC-15/16/17).**
   Read tags and embedded artwork off-thread into `MediaEntry`, read EXIF for
   photos, and add a Triangle control-panel overlay that owns rotate, zoom,
   slideshow, display mode, subtitles/track selection and set-wallpaper. Files:
   `src/media_library.cpp/.h`, `src/shell.cpp:533-550,704-806`,
   `src/view.cpp` overlay region, `src/settings_menu.cpp`.
4. **Settings schema fidelity and capability binding (raises ISC-14).**
   Replace the borrowed three-row media pages with the original group schemas;
   convert every placeholder row into a typed value with allowed values,
   default, side effects and an explicit unavailable reason; make System
   Information read `system/name`; document which rows write the host. Files:
   `src/settings_menu.cpp:48-142`, `src/preferences.cpp:27-47`,
   `src/host.cpp:89-112`.
5. **Video/audio capability layer and evidence (raises ISC-16/17).** Add
   subtitle/audio-track selection where the backend supports it, a track/chapter
   model, resume position, a real stop, and background playback with a mini
   panel; then record runs over real files rather than synthetic WAVs. Files:
   `src/shell.cpp:720-831`, `src/playback_queue.cpp`, `src/view.cpp:732-750`,
   `audit/MEDIA-CONTINUATION.md`.

## Limits of this audit

- Source reading cannot close workflow criteria. Per `ISA.md` Test Strategy,
  requested window observations are the evidence that does; none were made here.
- Existing runtime records (`audit/MEDIA-CONTINUATION.md`,
  `audit/HANDOFF.md`) are project-recorded and were not re-executed.
- No formal test suite exists or was run; no Git command was used.
- "Supported" in this document means the enumerated operation is implemented
  end to end in code and has no known blocking gap, not that the original panel,
  timing or conditional states are reproduced.
