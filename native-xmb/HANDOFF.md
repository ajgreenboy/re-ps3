# Native XMB recreation handoff

**Current plan to an exact match:** [HANDOFF-PHASES.md](HANDOFF-PHASES.md) — current state and remaining phases.
See [MaXMB integration and handoff review](audit/MAXMB-INTEGRATION.md) for the October 25 continuation.
The checkpoints below are historical; the procedural-wave descriptions no longer describe the current renderer.

Latest broader phase work: [app tooling, playback controls and idle display](audit/PHASE-CONTINUATION.md).

## Active direction

**Latest continuation:** see [October24 full audit and handoff](audit/HANDOFF.md)
for version0.2 settings/media/profiles/audio integration, the icon orientation
fix,307-topic guide catalog,122-file RCO scan and finer animation observations.

User requested a complete native C++ XMB recreation using original XMB assets,
and rejected continuing as a rendering patch over the firmware runtime.

The original runtime is preserved in Git checkpoints `5a6b3db` (Claude's work)
and `e20fdb5` (takeover findings). Its latest bounded guest-only boots still fell
to 1.2–1.4 FPS, with an intermittent SPURS task-state abort. Preserve that track
for comparison; the native application does not execute it.

## First application slice

- CMake/Qt C++20 build and a dedicated launcher.
- Read-only RCO/GIM original texture loading and original firmware Latin font.
- Separate menu/service state and rendering components.
- GPU procedural background and elapsed-time focus/navigation animation.
- Ten main category definitions and initial settings/game/media/network entries.
- Persistent appearance setting and media-library paths in the native data folder.
- Local folder browsing, photo display and Qt audio/video playback integration.
- Keyboard and XInput routes into the same shell actions.

These source implementations must be distinguished from observed behavior.
## Observed checkpoint: October 23,19:33

- Native Release build compiles and links with Visual Studio2022 C++ Build Tools,
  Qt6.12.0 MSVC64 and bundled CMake/Ninja. Build/deployment completed in
  `work/native-reference-build.txt`. No additional SDK was installed.
- Session `20261023-193245-983` loads the original `SCE-PS3 Rodin LATIN` font,
  166 original English strings and resolves14 Settings entries from XMBML/RCO.
- All44 requested current-menu images decode with zero image failures.
  The earlier cyan rectangles were RGBA channel reversal; DXT5 support restores
  the missing Settings, Network and trophy textures.
- Computer Use visibly observed Game, TV/Video, Video, Music, Photo and Settings.
  [Captures](SCREENSHOTS.md) show the original icons and latest theme material.
- Category materials tint source RGB while preserving source alpha. They have
  no whole-image opacity fade. Transparent space around icon shapes is retained;
  the full-colour PSN icon keeps its original colours.
- The user supplied a live RPCS3 Game screenshot. Its layout informed category
  spacing, selected icon size, selected-item position and tile dimensions.
  The native background now has wider ribbons and GPU particles. It is still a
  procedural approximation. Latest native capture uses the user's persisted
  Green theme; the supplied RPCS3 screenshot uses a gold theme.
- Steady logged render-loop rates are approximately62 FPS at the current
  window size on the RTX3070. These are render-loop intervals, not proof of
  display pacing, animation fidelity or a long stability session.
- The C++ inventory reads all28 local `explore/xmb/*.xml` files:206 views,
  788 item/query definitions,445 tables,zero parse errors. Literal ampersands in
  firmware query attributes caused19 initial Qt XML failures; adaptation is
  read-only and original SHA-256 hashes are preserved.
- Submenus now use a page stack and retain selection. Media Back stops playback
  and clears its source; photo direction input selects adjacent photos. These
  paths compile; complete nested/media workflows have not been observed yet.

## Recording-driven checkpoint: October 23,19:52

- The user supplied a64.639-second VSH navigation recording. The native C++ file
  decoder extracted324 timestamped samples and contact sheets locally. See
  [measured reference values](audit/VIDEO-REFERENCES.md).
- New bounded `QrcArchive` reads original QRCF/QRCC archives, including individual
  zlib entries. The DDS decoder reads the original128x128 icon normal maps.
  A local contact sheet confirms all70 numbered maps decode.
- Seven category silhouettes now use inspected original normal maps, including
  the Settings briefcase and Network globe. Fourteen Settings entries use their
  original normal-map silhouettes. The native lighting approximation preserves
  alpha; original reflection/refraction shaders remain unported.
- Settings now resolves16 entries from original XMBML/RCO and includes Accessory
  and Printer Settings. Theme opens Theme/Color/Background/Font; Color opens the
  native colour selector. Other new entries still report unavailable actions.
- Menu columns now animate horizontally, retain a visible parent, restore
  selection on Back and keep a departing child until its return fade completes.
  Computer Use observed Theme → Color → Back restoring Color selection, without
  changing the user's Green preference. It also observed root Settings and the
  original icons. Final spacing was subsequently adjusted to the recorded row
  gaps; see screenshot chronology for which build each capture represents.
- The latest build compiles/links in `work/native-qrc-build.txt`; session
  `20261023-195156-639` is open for continued use. The earlier QRC session
  `20261023-194841-340` logged48 decoded images,zero missing,steady62.5 render-loop
  FPS. This remains a loop measurement, not a presentation-pacing result.
- `icons.qrc` and `lines.qrc` expose original material/line/particle settings,
  textures and RSX/SPU programs. Their existence is recorded for the next phase;
  the procedural wave has not been replaced by that simulation.

## Reference observation constraint

RPCS3 VSH was launched and its window title reported around60 FPS. Computer Use
returns `product policy blocks this app: rpcs3.exe` when capture/control is
attempted. Do not bypass this restriction through another capture/input method.
Use screenshots or recordings attached by the user. Supplied references now
include the October23,19:28 Game screenshot and19:35 navigation recording.
Automated RPCS3 navigation/recording is not established.

## Next phases

1. Establish the first native executable, inspect original assets on screen,
   capture menu/settings states and record sustained frame measurements.
2. Build a complete machine-readable feature inventory from all local XMBML
   queries, RCO objects/text/animations and plugin references. Replace curated
   menu definitions with versioned resource-driven models.
3. Reproduce reference layout, colour, focus, transitions, wave and interaction
   audio. Extend image formats/model resources as the inventory requires.
4. Complete native profiles/settings, asynchronous media index and full photo,
   audio and video control surfaces, storage/device notifications and safe writes.
5. Add game/save/trophy services and emulator/process launch lifecycle. Define
   online/browser/store/friends integrations from real backend capabilities.
6. Measure performance, resilience and fidelity across recorded full workflows.

## Known foundation limitations

The wave is procedural, not a verified copy. Main menu definitions are curated;
Settings icons/text/descriptions bind to original resources. This is not a full
XMBML query/plugin interpreter. Most settings/online/game actions show an
unavailable message. Nested history needs observed workflow evidence; media
metadata/controls and async indexing need expansion. Full photo/audio/video
workflows are not established. GIM support covers first frames of linear
RGBA8888 and DXT1/3/5; other layouts, palettes and RCO models remain incomplete.

See ISA.md for all open completion requirements. Do not report the first slice as
the complete recreation.
