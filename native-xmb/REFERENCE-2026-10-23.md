# Recorded VSH reference and next implementation phases

## Evidence

The user supplied `Recording 2026-10-23 193556.mp4`: 64.639 seconds,
906 by 574 source frames, SHA-256
`44ea993ba0b03b44b5e215324f3cdd5dc7abd16c6bcef2dd3ea684026e5444b6`.
The C++ `xmb-reference-frames` helper read this file and produced 324 timestamped
samples at 200 ms intervals, plus contact sheets. These are local, ignored
outputs in `out/native-reference/recording-193556/`. No recording is uploaded.
The decoder never captures or controls RPCS3.

Reproduce from the repository root after building:

```powershell
& .\out\native-xmb\xmb-reference-frames.exe <user-recording.mp4> .\out\native-reference\session
```

The samples establish menu structure and settled geometry. They do not establish
sub-200 ms animation duration, input latency, audio timing or display frame pacing.
Reported RPCS3 FPS in the recording varies; it is reference evidence, not a native
application performance measurement.

## Observed behavior

| Recording position | Observation | Native follow-up |
|---|---|---|
| 0–2 s | Game items scroll through a fixed focus slot; preceding items appear above the category row | Continuous row positions with a large gap above root focus |
| About 2 s | Game changes to Network; category strip slides while content changes | Preserve category movement; outgoing category content cross-fade still pending |
| About 20–23 s | Search for Media Servers opens a separate search surface and returns | Implement a real asynchronous local-network discovery service and its screen |
| About 40–42 s | Accessory and Power Save submenus retain a left parent column; Back restores the root item | Shared page stack and animated columns implemented; those service pages remain unavailable |
| About 42.8–43.2 s | Date and Time page has individual choices and current values | Host/native settings adapters and explicit PS3/host semantic differences remain pending |
| About 44–44.4 s | Theme Settings opens Theme, Color, Background and Font with a small child wrench icon | Native Theme page now has those entries; Color opens the native color choices |
| About 45–48 s | System Settings scrolls independently with descriptions and right-aligned current values | Original sysconf definitions, text and real native backend mapping needed |
| About 60–64.6 s | Trophy Collection opens an empty collection screen and returns to Trophy Collection | Real trophy data model and empty-state screen needed |

The reference normalizes approximately to a 1920 by 1080 presentation coordinate
system: category/focus column near x560, category row y290, selected entry y505.
A submenu shifts the category/selected parent toward x310, places child icons
near x560 and labels near x620. Settings labels at root start nearer x680;
wide Game/media cards use a larger label offset. Child lists have roughly
80-unit regular row spacing with about 175 units around focus. Root lists leave
roughly 380 units between focus and the nearest item above the category band.
These are measurements from scaled recorded images, not recovered layout constants.

## Original QGL resources

Read-only inspection found the actual silhouettes in local
`dev_flash/vsh/resource/qgl/icontex.qrc`. The C++ asset service now reads Sony CXML
`QRCF` and zlib-wrapped `QRCC`, resolves named files, and supports individually
compressed entries with declared expanded lengths. This format is unrelated to
Qt's RCC resource format. The reader bounds archive size, expansion, table and
file ranges, strings, attributes and ID back-references.

All 70 numbered normal-map images decode as DDS, each 128 by 128 pixels with
original alpha. The additional `dif` entry exists but is not used in rendering.
Use `--icon-sheet <local.png>` with `--dev-flash` to inspect the source maps.
Raw maps and the sheet remain local derived outputs, outside Git.

| Original ID | Shape used by native app |
|---|---|
| `2n` | Settings briefcase |
| `3n` | Photo camera |
| `4n` | Music notes |
| `5n` | Video film |
| `6n` | Game controller |
| `7n` | Network globe |
| `8n` | Friends faces |
| `9n`, `10n`, `12n`, `13n`, `14n` | Update, Video, Music, System and Game Settings |
| `15n`–`21n` | Date/Time, Accessory, Display, Sound, Security, Network, Remote Play |
| `23n`, `24n`, `57n` | Child wrench, Theme Settings, Power Save Settings |

The native material shades these normals with a simple light and theme tint,
preserving original alpha. It does not execute the original RSX shader and is
not yet a reproduction of its reflection, refraction, exposure or lighting.
Full-colour PSN artwork remains loaded from its RCO.

`icons.qrc` also contains `ICONS.mnu`, time/boot overrides and RSX programs.
`lines.qrc` contains background/HDR/line/particle `.mnu` parameters, monthly RGB
and night DDS textures, Fresnel lookup images, RSX programs and SPU programs.
Their contents were inspected locally with `work/qrc-inspect.ts`. The current
wave still uses the procedural approximation; finding these resources does not
mean their simulation or shaders have been reconstructed.

## Handoff priorities

1. Port original normal-map material behavior and resolve Users/TV artwork and
   all entry icons from explicit resource evidence. Current mappings remain an
   initial, inspected subset.
2. Decode sysconf page definitions and localization; attach real backend
   capabilities before presenting editable values. Complete Theme, Background,
   Font, date/time and System Settings without fabricated saved values.
3. Record finer frame samples around transitions, measure easing/fades and port
   incoming/outgoing category content. Validate rapid input during transitions.
4. Reconstruct the wave from its actual configuration, textures and simulation
   evidence. Porting a DDS loader alone does not reproduce the wave.
5. Decode original interaction sounds and compare their timing against audio in
   the supplied recording. The frame helper intentionally does not play audio.
6. Complete real media/network/game/trophy workflows and measure presentation
   timing and stability across sustained interactive use.
