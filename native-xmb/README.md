# Native C++ XMB recreation

This is the active application track following the user's October 23 direction
change. It rebuilds XMB behavior as a native application. The older firmware
runtime under `xmbrt/` remains a reference.

The target is complete behavior and presentation coverage. Version 0.2 is the
first application foundation, **not a completed XMB recreation**.

## Build and run

From the repository root, using the installed Visual Studio C++ tools and a Qt
6.8+ MSVC kit with Widgets, OpenGLWidgets and Multimedia:

```powershell
& .\native-xmb\build.ps1
& .\run-native-xmb.ps1
```

Pass `-QtRoot` to the build script for another installed Qt kit. Pass `-FlashRoot`
to the launcher for another local `dev_flash`. The default launcher uses the
existing sibling RPCS3 firmware directory. Build products and native settings are
under `out/native-xmb/`. No firmware assets are copied into source control.

Font coverage also requires FreeType: CMake uses an installed FreeType package or the Qt SDK's bundled
library/header targets. The recovered glyph-load settings and remaining parity work are in
`audit/FONT-MATCHING.md`.

Keyboard: arrows navigate, Enter activates, Escape or Backspace returns, T opens the item options menu
(Triangle), F11 toggles full screen, Space pauses/resumes media; the volume media keys drive the volume HUD.
XInput D-pad and A/B share the same actions.

Local named-state captures and layer scoring: [verification harness](audit/VERIFICATION-HARNESS.md).
The harness keeps user preferences separate, checks category/page state and reports failures; repeatable native
captures are distinct from firmware-fidelity evidence.

## Application boundaries

| Layer | Responsibility |
|---|---|
| `Assets` / `QrcArchive` | Read-only firmware font, RCO/GIM, QRC/DDS decoding, English strings; named image cache |
| `catalog` | Bounded XMBML parsing, original file hashes and definition inventory |
| `Shell` | Stable menu IDs, navigation state, settings, service actions |
| `Preferences` / `Profiles` | Validated, versioned atomic native settings and local identity stores |
| `MediaLibrary` | Background directory enumeration, bounded results and stale-request cancellation |
| `InteractionAudio` | Original RCO/VAG interaction cues and native audio output |
| `View` | OpenGL background, original icons/text, animation, keyboard/controller input |
| Qt Multimedia | Host audio/video decoding and playback; video frames return to the view |
| `main` | Composition, paths, desktop lifecycle |

The renderer has no PPU, SPU, LV2, firmware callback or guest FIFO dependency.
It redraws from application state. UI timing uses elapsed time rather than
firmware frame pacing. Source assets remain read-only.

The latest service slice includes setting values, original Latin font choices,
wallpaper, clock display formats/time zones, volume/mute/key tone, native local
profile creation/selection, async media browsing and photo decode, rotation and
slideshow. Preferences migrate legacy color/library paths to `preferences.json`;
profiles are stored in `profiles.json`. Preferences remain shared between local
profiles. These source implementations are not complete original workflows;
see the audit's feature-specific gaps and observed-evidence distinctions.

Surfaces added on 2026-10-26/27, in the same presentation:

- **Continue Watching** is the Video column's first row: remembered positions newest first (at most 20, 30 days
  back) with resume, Information and Remove, and rows kept but disabled when the file has gone
  ([audit/MEDIA-CONTROLS.md](audit/MEDIA-CONTROLS.md)).
- Local video reads **sidecar subtitles** (`<name>.srt`, `<name>.<tag>.srt`, `.vtt` beside the file) and draws
  the active cue over the picture; Triangle opens the **player panel** for subtitles, the audio tracks the
  player reports, Repeat and Shuffle ([audit/SUBTITLES.md](audit/SUBTITLES.md)).
- Triangle on a content row opens the **item options menu** - Open/Play/View, Information, Sort By (which really
  reorders the page) and Close ([audit/OPTIONS-MENU.md](audit/OPTIONS-MENU.md)).
- **Notifications** composed from `system_plugin`'s own plaque and glyphs pop up top-right for a tracked launch
  that returns and for a library rescan ([audit/NOTIFICATIONS.md](audit/NOTIFICATIONS.md)).
- The volume media keys drive the real Windows master volume (L2/R2 seek in the player), with a **volume HUD** in the band left of
  the info bar ([audit/HTPC-IDEAS.md](audit/HTPC-IDEAS.md) section 6).
- The Network column's **Download Management** row opens a real page over Steam's own download queue
  ([audit/STEAM-DOWNLOADS.md](audit/STEAM-DOWNLOADS.md) section 10), and Jellyfin **signs in on an XMB page**
  with a masked keyboard instead of a dialog ([audit/JELLYFIN-NATIVE.md](audit/JELLYFIN-NATIVE.md)).

## Original assets

The native C++ asset reader supports PS3 big-endian RCO with plain or zlib tables,
named plain/zlib image entries and the first frame of linear RGBA8888 and
DXT1/DXT3/DXT5 GIM images. RGBA channels preserve original coverage; category icon
materials tint RGB while keeping original alpha and full-colour PSN artwork.
It loads the original Latin firmware font and caches decoded icons by container
and resource name. Unknown formats are logged and remain missing; they are not
silently treated as decoded resources. Other GIM formats, RCO animation/layout
objects, model assets, other languages and RCO VAG sounds remain work; theme packages are covered
under PS3 themes below.
English strings support UTF-8/UTF-16BE/UTF-32BE tables; Settings currently binds
16 entries to the original XMBML title/description keys. The inspected QGL normal
maps supply seven main category silhouettes and fourteen Settings silhouettes.
The native normal-map light is an approximation of the original material.

Sony QGL archives support `QRCF` and zlib `QRCC`, including individually compressed
files. DDS support covers 32-bit masked RGB(A) and DXT1/3/5 first mip levels. Source
normal maps can be inspected locally without writing them into the repository:

```powershell
& .\out\native-xmb\re-ps3-native.exe --dev-flash <local-dev_flash> --icon-sheet .\out\native-xmb\icon-sheet.png
```

## Controllers and installed titles

Input comes from the keyboard, media keys and remotes (WM_APPCOMMAND; [audit/INPUT.md](audit/INPUT.md)), XInput pads 0-3
(the Xbox Guide button acts as the PS button), and PlayStation controllers read directly over HID: DualShock 3 (USB),
DualShock 4 and DualSense (USB or Bluetooth). The left stick works as the D-pad. ✕ confirms and ○ goes back, as
XInput's A and B do.

Titles in `dev_hdd0/game`, and disc copies in RPCS3's `config/games.yml`, start in the RPCS3 that sits next to
dev_hdd0 (`rpcs3.exe --no-gui`). A title RPCS3 lists starts by Title ID (`%RPCS3_GAMEID%:<id>`); any other starts
from its `USRDIR/EBOOT.BIN`. Both pieces follow MaXMB (MIT); see `THIRD-PARTY.md`.

## Steam games, friends and service apps

The Game column lists the games your Steam account **owns**. Installed titles sit inline in the column and launch
straight away; each row's detail carries its Steam collection labels (Multiplayer, Singleplayer, Favorites, ...).
Titles that are owned but not installed go into one **Not Installed** folder row, whose detail shows the combined
count. Row artwork, title position and spacing are the installed-PKG layout, so Steam rows and original PS3 titles
share one presentation. F5 or **Refresh Game Library** rescans.

Steam uses one folder rather than a folder per subset: **Not Installed** holds every owned title that is not on
disk plus any family-shared titles from `<native-data>/family-shared.json` (275 rows here: 272 owned titles that are not installed, plus 3
family-shared, alongside the 10 installed titles inline). The earlier **Installed Games** and **Family Shared**
folder rows are gone. Opening **Not Installed** pushes an ordinary XMB page of rows; OK hands `steam://install/<id>`
to Steam, which performs the download.

Updated 2026-11-07 (`src/shell.cpp`, `src/steam_activity.*`; commit 4e64c31): the Game column is now the installed titles inline (most
recently played first), a **Steam Store** row and one **All Games** folder. The All Games page lists an **All Titles** folder,
one folder per Steam collection (icon: a 2x2 mosaic of its first titles' headers), then **Not Installed** and **Family Shared**
(the latter only when `family-shared.json` has titles). The paragraph above is still right about what Not Installed holds, but
the separate Family Shared folder is back and the folders sit inside All Games. See "Steam play activity and actions" below.

A **Steam Store** row sits above the folder and opens a browse page of live top sellers with prices (public,
keyless endpoint); its rows hand the same `steam://install/<id>` to Steam. Nothing is bought or downloaded inside
the XMB - the store page only lists, and Steam owns purchase, download and launch.

Two different local sources feed this without writing to Steam:

- installed titles, from `libraryfolders.vdf` + `appmanifest_*.acf`;
- collections, from `userdata/<accountid>/config/cloudstorage/cloud-storage-namespace-1.json`
  (`user-collections.*` entries).

The *owned* list itself is Valve's `IPlayerService/GetOwnedGames`, which Valve requires an API key for. Give it
one of these ways (optionally with `XMB_STEAM_ID`):

- `STEAM_WEB_API_KEY` in the launch environment, or
- a one-line file at `%APPDATA%\re-ps3-native\steam_api_key` (user-only, outside the repository and git-ignored).

Without a key the column keeps the installed titles and the XMB states that a key is needed instead of inventing a
library. The key is never written to preferences, reports, manifests or logs.


**Steam Families.** Valve's owned-games endpoint returns only your own purchases; family-shared titles are
licensed to another member, and the family endpoint (`IFamilyGroupsService`) needs an OAuth access token - a Web
API key is rejected with 401 (verified). Family *membership* is read locally from Steam (`FamilyGroup` in
`userdata/<accountid>/config/localconfig.vdf`, e.g. group and 6 members here). To include shared titles now, write
`<native-data>/family-shared.json` (its titles join the **Not Installed** folder):

```json
{ "apps": [ { "id": 620, "title": "Portal 2" } ] }
```

`--steam-family-report` prints the membership and the shared count.

The Friends column prefers the signed-in Steam client through an optional native helper; this works without a Web API key or a profile-privacy change. Build it with `-SteamworksHeaders <sdk/public/steam>`; see [client connection](audit/STEAM-CLIENT-FRIENDS.md). The key is an optional friends fallback and still serves the owned-games provider. **Open Steam Friends** also opens Steam's own friends
window.

 `<native-data>/artwork/`: `<appid>.png` for the wide row,
`<appid>_icon.png` for the square icon and `<appid>_bg.png` for the background (`.jpg/.jpeg/.webp` also work). An
override wins over the cached Steam header and the manifest icon; `XMB_STEAM_ARTWORK_LOG=1` logs which files are used.

Open **Game → Artwork Settings**, enter a [RAWG API key](https://rawg.io/apidocs), then choose **Fetch Artwork**. XMB looks up installed Steam games and imported games during library scans. Matched artwork supplies the game card and focused background. Explicit local overrides take priority. Games without a confident match keep their existing artwork. Cached matches work offline. The key is saved in the user's application settings directory, separate from the library and repository. You can also set `RAWG_API_KEY` before launching XMB.

**Import Games and Apps** reads a versioned JSON manifest. The same tool is available without opening XMB:

```powershell
& .\out\native-xmb\re-ps3-native.exe --data-dir <native-data> --import-apps <manifest.json>
& .\out\native-xmb\re-ps3-native.exe --steam-report .\out\steam-installed.json
& .\out\native-xmb\re-ps3-native.exe --steam-collections-report .\out\steam-collections.json
& .\out\native-xmb\re-ps3-native.exe --owned-games-report .\out\steam-owned.json      # needs STEAM_WEB_API_KEY
& .\out\native-xmb\re-ps3-native.exe --steam-library-report .\out\steam-library.json  # owned + collections
& .\out\native-xmb\re-ps3-native.exe --steam-store-report .\out\steam-store.json [--steam-store-query portal]
```

Edit `examples/jellyfin.json` (or `examples/media-apps.json` for Jellyfin, Plex, YouTube and Spotify) for your
setup before importing it. This adds a TV/Video Services app: Jellyfin is a native service with library/folder
pages, direct play and server-negotiated HLS, and it signs in on an XMB page with a masked keyboard
(audit/JELLYFIN-NATIVE.md); the Plex, YouTube and Spotify entries are URL apps that still open in your browser. Updated 2026-11-07: Spotify also has a native page in the Music column (see "Spotify" below); the example's Spotify entry is only a browser shortcut.
See [content schema](audit/CONTENT-REGISTRY.md) and
[Steam handoff, verification and limits](audit/STEAM-INTEGRATION.md). No firmware or installed game files are changed.

## Network services and media sources (added 2026-11-05 to 11-07)

These are modern rows in the XMB's own presentation, not firmware surfaces.

### Spotify (Music column)

Music > Spotify signs in through the browser (PKCE, no client secret; the refresh token is sealed with DPAPI), lists Liked
Songs and playlists, and plays on a Spotify Connect device (Triangle on a playlist: Play, Shuffle Play, Open). Spotify only
streams to its own clients, so the track plays on the Spotify app on this PC, a phone or a speaker; Now Playing shows it and
has Play/Pause/Next/Previous. Playback control needs Premium. Setup: create an app at the Spotify developer dashboard, add the
redirect URI `http://127.0.0.1:8888/callback`, and put the client id in `<native-data>/spotify.json`:

```json
{ "clientId": "<32 hex characters>", "redirectPort": 8888 }
```

`SPOTIFY_CLIENT_ID` / `SPOTIFY_REDIRECT_PORT` override the file. Without a client id the page shows one "Set Up Spotify" row
with these steps. Code: `src/spotify_client.*`, `src/spotify_shell.cpp`; details, limits and the real-account run:
[audit/SPOTIFY.md](audit/SPOTIFY.md).

### Search for Media Servers (DLNA / UPnP)

The first row of the Video, Music and Photo columns runs a native DLNA/UPnP client (`src/dlna_client.*`,
`src/dlna_shell.cpp`): SSDP discovery, then ContentDirectory browsing. Audio and video play in the normal player (resume and
controls included); photos open in the viewer. A server that multicast cannot reach (a Docker bridge, another subnet) goes in
`<native-data>/dlna.json`:

```json
{ "locations": ["http://192.168.1.10:8200/rootDesc.xml"], "unicast": ["192.168.1.10"] }
```

Only private, link-local or loopback addresses are accepted. Verified against a local mock; no real DLNA server has been tried
yet. Details: [audit/DLNA.md](audit/DLNA.md).

### Tailscale (Network column)

Network > Tailscale shows this PC's tailnet state ("Connected as <host> - <tailnet IP>", "Off", "Sign-in needed" or "Not
installed"). The page switches Tailscale on or off (off asks first), picks an exit node, lists the devices (Cross pings one) and
offers Sign In when the client needs a login. It drives the installed `tailscale.exe` CLI (`src/tailscale_client.*`,
`src/tailscale_shell.cpp`) and never logs in, out or switches accounts. Details and the run on a real tailnet:
[audit/TAILSCALE.md](audit/TAILSCALE.md).

### Steam play activity and actions

An installed Steam title's detail line shows what Steam recorded, read from `userdata/<account>/config/localconfig.vdf`
(`src/steam_activity.*`; "12 h 5 min played - Last played ..." in the date order of the Clock setting), and installed titles
are ordered by last played. Triangle on a Steam title adds Open in Steam Library, Store Page and, for installed titles,
Screenshots, Verify Game Files and Delete; each is a `steam://` hand-off, so Steam performs (and for Delete, confirms) the
action. Row icons for the Steam, Jellyfin, Tailscale and Spotify services come from `src/service_icons.*` (the installed
client's own icon, or a drawn mark). See [audit/STEAM-INTEGRATION.md](audit/STEAM-INTEGRATION.md).

### Playlists and Jellyfin quality

Music and Photo > Playlists are real (Create New Playlist, Add to Playlist, Add All, Move Up/Down, Delete; stored per user under
`<native-data>/playlists/`, `src/playlists.*`): [audit/FEATURE-STATUS-2026-11-04.md](audit/FEATURE-STATUS-2026-11-04.md) item 1.
Jellyfin video has a Streaming Quality ceiling (row menu and player panel) and server text subtitles:
[audit/JELLYFIN-NATIVE.md](audit/JELLYFIN-NATIVE.md).

### Wake on LAN, Home Assistant and Weather (Network column)

- **Wake on LAN** lists the machines in `<native-data>/wol.json` (`{"devices":[{"name":"Server","mac":"AA:BB:CC:DD:EE:FF","host":"192.168.1.10"}]}`)
  with Online/Offline; Cross wakes one and reports when it answers. "Add from This Network" adds a machine Windows has already
  seen. Only private LAN broadcasts are used.
- **Home Assistant** needs a long-lived access token: put `{"url":"http://<HA address>","token":"<token>"}` in
  `<native-data>/homeassistant.json`; on the next start the token moves into a DPAPI-sealed file. Rooms (or kinds) open as
  folders; scenes and scripts run, lights / switches / fans toggle, media players play/pause.
- **Weather** reads `<native-data>/weather.json` (`{"place":"Chicago, IL"}`, `"units":"metric"` for Celsius) and shows Open-Meteo's
  forecast (no key, no location lookup): current conditions on the row, five days on the page.

Details: [audit/HOME-SERVICES.md](audit/HOME-SERVICES.md), [audit/WEATHER.md](audit/WEATHER.md).

### Power, audio output, Delete and System Check

- **Users column**: Restart System and Sleep after Turn Off System, each confirmed. Turn Off the System shuts Windows down only
  with `--kiosk`; otherwise it quits the XMB. `XMB_POWER_DRY_RUN=1` logs instead of acting (`src/system_power.*`).
- **Settings > Sound Settings > Audio Output Settings** chooses the Windows default playback device; the player follows it.
  `XMB_AUDIO_DRY_RUN=1` logs instead of switching.
- **Delete** on a local media file moves it to the Recycle Bin after the firmware's confirmation, only inside the chosen
  library folders (`src/file_ops.*`). Copy offers a destination list (Video Library, Photo Library or This Folder).
- **Settings > System Settings > System Check** runs read-only checks (firmware files, fonts, data folder, free space,
  graphics, network, Jellyfin, Home Assistant, Spotify, Tailscale, Steam libraries) and lists problems first with their fix.
- **Games** launched from the XMB: rendering pauses while the game is in front, and on exit (normal, crash, Alt-F4) the XMB
  returns to the foreground fullscreen with the same row selected (`src/ps_watcher.*` TitleSession).

## PS3 themes

All built-in colours use light, opaque category materials with a theme tint. Original follows the firmware's
monthly palette; its glass shader remains available for diagnosis. Details and remaining reference work are in
`audit/CATEGORY-ICON-FIX.md`.

Settings > Theme Settings > Select Theme opens a PS3 theme file (`.p3t`). The theme is read in memory (`src/p3t.cpp`,
ported from MaXMB, MIT; see `THIRD-PARTY.md`) and nothing from it is written to disk:

- its still background (`bgimage` hd, else sd) becomes the background through the wallpaper mode, ahead of a
  wallpaper of your own, and covers the whole window;
- its icons (`icontable`, GIM) replace the category icons, the Settings, Users, Game, Network, PSN and Friends items
  and the bullets inside settings pages, in their own colours, on the quads the firmware's glass icons take. Media
  libraries and folders keep the system's pictures.

Theme Settings > Theme lists Original and the chosen theme by its own name; going back to Original with no wallpaper
of your own brings the original background back. The preference stores only the file's path.

The theme's sound effects (`setable`, VAG to WAV) replace cursor/decide/cancel/option-menu/message-accept cues;
missing or invalid cues fall back to the system's, and Original restores system sounds. Volume, mute and Key Tone
apply to theme sounds too. Dynamic themes' animated scenes (RAF) are listed, not drawn; their still background is used when there
is one. The firmware's own theme (`dev_flash/vsh/resource/theme/01.p3t`, "Air Paint") is a full example. To see
what a theme holds, write its element tree and decoded pictures and sounds to a local folder:

```powershell
& .\out\native-xmb\re-ps3-native.exe --p3t-dump <theme.p3t>@<local-folder>
```

## Cold-boot intro

The app starts with the firmware's 4.93 cold-boot intro: coldboot.raf (the PS3 logo scene and its script) with
coldboot_stereo.ac3, then the XMB at 5.5 s, as on the console. Any key or pad button skips it; `--no-intro` starts
at the XMB (scripted and measurement runs skip it too). `XMB_INTRO_GRAB=<folder>@<seconds>,...` saves frames at
those intro times for comparison with an RPCS3 capture (audit/PIPELINE.md "Cold boot").

## Animation diagnostics

Animation diagnostics and measured high-refresh results are in
[ANIMATION-SMOOTHNESS.md](audit/ANIMATION-SMOOTHNESS.md). `--motion-report <file.json>` samples the actual menu
eases without opening a window. `XMB_FRAME_TRACE=<file>` records a bounded callback trace on orderly shutdown;
`bun work/frame-report.ts <file> --warmup-ms 6000 --until-ms 10000` summarizes a chosen interval.

## Decrypted modules for the ribbon

dev_flash holds the VSH modules encrypted (`*.sprx`). The ribbon cloth starts from rest tables inside
custom_render_plugin. To use them, pass a local directory of decrypted modules (`*.prx`):

```powershell
& .\out\native-xmb\re-ps3-native.exe --dev-flash <local-dev_flash> --vsh-modules <local-decrypted-vsh_module>
```

Without that directory, the cloth starts flat and settles. The modules stay local and are never committed.

## Inventory original menu definitions

```powershell
& .\out\native-xmb\re-ps3-native.exe --dev-flash <local-dev_flash> --inventory .\out\native-xmb\xmbml-inventory.json
```

The current local firmware yields 28 documents,206 views,788 item/query
definitions and445 attribute tables with zero parse errors. Literal query `&`
characters are adapted in memory for Qt's strict XML parser. Original file hashes
are recorded before adaptation; firmware files are never rewritten. The inventory
retains unmapped status and selector/plugin references. Definitions are not proof
that their services are implemented. The inventory is local derived output.

Format research references: [RCOMage structures](https://github.com/kakaroto/RCOMage/blob/master/src/rcofile.h),
[RCOMage reader](https://github.com/kakaroto/RCOMage/blob/master/src/rcoreader.c),
[GIMSplit format investigation](https://github.com/adeyblue/GIMSplit/blob/master/Program.cs).
The new reader is an independent implementation of the file layouts.

The procedural background is a native renderer foundation. It is not yet a
measured reproduction of the firmware wave's geometry, material or motion.
The main category/menu catalog is currently curated from the reference. A complete
XMBML/resource-driven catalog and localization are required before fidelity can
be claimed.

See the [forward phase plan](archive/PHASES-2026-10-28.md), the [full feature/presentation audit and handoff](audit/README.md),
[handoff](HANDOFF.md) and [completion criteria](ISA.md).

**Plan of record (2026-11-07):** [audit/PHASES-2026-11-07.md](audit/PHASES-2026-11-07.md) - what is done, the ranked backlog and the
parallel workstreams; it supersedes the "Next steps" list in [HANDOFF-PHASES.md](HANDOFF-PHASES.md) and the open-gap list in
[audit/FEATURE-STATUS-2026-11-04.md](audit/FEATURE-STATUS-2026-11-04.md) where they disagree.

**Current handoff (2026-11-09):** [HANDOFF-2026-11-09.md](HANDOFF-2026-11-09.md) - state, build/run/verify, what landed 2026-11-08/09, work in flight, config files, open work and conventions.

**Previous handoff (2026-10-28):** [HANDOFF-2026-10-28.md](archive/HANDOFF-2026-10-28.md) - supersedes [HANDOFF-2026-10-27.md](archive/HANDOFF-2026-10-27.md) (the tree at `0c8faee`),
with build/run/verify, today's gate result, the known gaps by surface, the config migration policy, the
diagnostics design and the release checklist. Read it first; the install guide is [INSTALL.md](INSTALL.md).

**Previous handoff snapshot (2026-10-26, historical):** [HANDOFF-2026-10-26.md](archive/HANDOFF-2026-10-26.md) - the tree at
`937227d`, with what was verified then, the drift it found and its open work. It is no longer the current state; for
current state read the current handoff, [ISA.md](ISA.md), [the feature catalogue](audit/FEATURE-CATALOG.md),
[the verification harness](audit/VERIFICATION-HARNESS.md) and [the parity register](audit/FIRMWARE-PARITY-GAPS.md).
