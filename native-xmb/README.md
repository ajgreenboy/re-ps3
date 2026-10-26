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

Keyboard: arrows navigate, Enter activates, Escape or Backspace returns, F11 toggles full
screen, Space pauses/resumes media. XInput D-pad and A/B share the same actions.

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

Input comes from the keyboard, XInput pad 0, and PlayStation controllers read directly over HID: DualShock 3 (USB),
DualShock 4 and DualSense (USB or Bluetooth). The left stick works as the D-pad. ✕ confirms and ○ goes back, as
XInput's A and B do.

Titles in `dev_hdd0/game`, and disc copies in RPCS3's `config/games.yml`, start in the RPCS3 that sits next to
dev_hdd0 (`rpcs3.exe --no-gui`). A title RPCS3 lists starts by Title ID (`%RPCS3_GAMEID%:<id>`); any other starts
from its `USRDIR/EBOOT.BIN`. Both pieces follow MaXMB (MIT); see `THIRD-PARTY.md`.

## Steam games, friends and service apps

The Game column lists the games your Steam account **owns**, organised by your own Steam library collections
(Multiplayer, Singleplayer, Favorites, ...). A title that is installed launches straight away; one that is owned but
not installed shows an install action that hands `steam://install/<appid>` to Steam, which performs the download.
Row artwork, title position and spacing are the installed-PKG layout, so Steam rows and original PS3 titles share
one presentation. F5 or **Refresh Game Library** rescans.

Steam is kept in folders rather than one long list: **Installed Games**, **Not Installed** (the rest of the owned
library, each row offering Install) and **Family Shared** when a shared list exists. Opening a folder pushes a page
of that subset; OK launches an installed title or hands `steam://install/<id>` to Steam for one that is not installed.

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
`<native-data>/family-shared.json`:

```json
{ "apps": [ { "id": 620, "title": "Portal 2" } ] }
```

`--steam-family-report` prints the membership and the shared count.

The Friends column uses Steam too and needs the same key. **Open Steam Friends** also opens Steam's own friends
window.

 `<native-data>/artwork/`: `<appid>.png` for the wide row,
`<appid>_icon.png` for the square icon and `<appid>_bg.png` for the background (`.jpg/.jpeg/.webp` also work). An
override wins over the cached Steam header and the manifest icon; `XMB_STEAM_ARTWORK_LOG=1` logs which files are used.

**Import Games and Apps** reads a versioned JSON manifest. The same tool is available without opening XMB:

```powershell
& .\out\native-xmb\re-ps3-native.exe --data-dir <native-data> --import-apps <manifest.json>
& .\out\native-xmb\re-ps3-native.exe --steam-report .\out\steam-installed.json
& .\out\native-xmb\re-ps3-native.exe --steam-collections-report .\out\steam-collections.json
& .\out\native-xmb\re-ps3-native.exe --owned-games-report .\out\steam-owned.json      # needs STEAM_WEB_API_KEY
& .\out\native-xmb\re-ps3-native.exe --steam-library-report .\out\steam-library.json  # owned + collections
& .\out\native-xmb\re-ps3-native.exe --steam-store-report .\out\steam-store.json [--steam-store-query portal]
```

Edit `examples/jellyfin.json` (or `examples/media-apps.json` for Jellyfin, Plex, YouTube and Spotify) for your setup before importing it. This adds a TV/Video Services app that opens
in your browser; embedded Jellyfin playback is future work. See [content schema](audit/CONTENT-REGISTRY.md) and
[Steam handoff, verification and limits](audit/STEAM-INTEGRATION.md). No firmware or installed game files are changed.

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

See the [full feature/presentation audit and new handoff](audit/README.md),
[handoff](HANDOFF.md) and [completion criteria](ISA.md).
