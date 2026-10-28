# Parity phases: the 1:1 track

Companion to [PHASES-2026-10-27.md](PHASES-2026-10-27.md). That plan is the *product* track (finish features,
make it a usable HTPC). This one is the *fidelity* track: what it takes to make the native output match the
PlayStation 3 XMB as closely as is honestly measurable on a PC, screen by screen.

The rule for this track: **no phase closes on "looks right"**. Each one names the screens it covers, the
measurable gate it must pass, and the capture that proves it.

## What "1:1" means here (three tiers, claimed separately)

| Tier | Claim | How it is proven |
|---|---|---|
| **A. Structural** | The same firmware resource drives the same geometry/strings/material, parsed from dev_flash (RCO objects, XMBML views, MNU constants, PA programs) | dump equality: resolved object tree / layout values / app strings vs the RCO's own values; no hardcoded numbers where an RCO value exists |
| **B. Perceptual** | The rendered frame matches a real RPCS3 frame of the same state within a measured budget | per-screen SSIM/PSNR against a reference capture with pinned clocks, per-region gates, and a published scorecard |
| **C. Behavioural** | The same inputs produce the same state transitions, timings and actions | scripted input traces with logged state, frame-counted timings against a captured RPCS3 burst |

A screen is "1:1" only when all three tiers pass for it. Tier B alone is never called parity: the current
register's caution stands (reference captures carry the emulator's own rendering differences, and the dust phase
differs frame to frame).

## P0 - Parity infrastructure (must exist before any screen work )

**Goal**: a capture corpus and a scorecard that make per-screen claims possible.
- P0.1 **State catalogue**: enumerate every reachable XMB state from the firmware's own XMBML (views/queries in
  `explore/xmb/*.xml`, the 206/788 inventory) and mark each reachable/unreachable in the native build. Output:
  a generated table with ids and how each is opened.
- P0.2 **Capture matrix**: every catalogue state × theme (Original + the four presets) × background mode
  (day/night/yoake/higure) × resolution (1080p, 720p), with clocks pinned exactly as the harness does today.
- P0.3 **RPCS3 reference rig**: the documented boot recipe (HANDOFF-PHASES "Capturing the real XMB in RPCS3"),
  scripted capture runs per cell, a machine-readable reference set outside git.
- P0.4 **Scorecard tool** (`work/parity.ts`): runs the matrix, scores each screen per region, writes a
  JSON+markdown scorecard with per-screen tier status; regression mode diffs a scorecard against the previous one.
- P0.5 **Threshold policy**: Tier B budgets per screen class (icon-heavy screens tighter on structure, text-heavy
  screens gated on measured glyph metrics as well), and the rule that a budget changes only with a dated reason.

**Exit**: the catalogue, matrix and scorecard exist; at least the ten most important screens have references; the
scorecard ran once end to end and reports honest numbers.
**Size**: M. **Evidence**: the generated catalogue table, a committed scorecard, and the reference set inventory.

## P1 - Resource and object model fidelity (Tier A foundation)

**Goal**: every visible value comes from the firmware's own data structures.
- P1.1 Parse the RCO object model completely: object types used by the XMB (Xmenu, PhText, PhClock, Plane,
  Button, Panel, List, Table, Anim...), their properties, and the parent/child transforms the console applies.
- P1.2 Parse all XMBML view types: `listview`, `pixmapview`, `table`, `query` providers (`sel://`, `ese://`,
  `xcb://`, static), variable substitution and locale lookup.
- P1.3 Materials from MNU/override files: ICONS.mnu day/night/yoake/higure, PARTICLES.mnu, background presets,
  text material, theme overrides.
- P1.4 Fonts and glyph resources: every face in dev_flash, the PAF glyph cache format, fallback rules.
- P1.5 Texture decode parity: DXT1/3/5, GIM, DDS, mipmaps, swizzle where the RSX expects it.

**Exit**: a dump of each of the above for a chosen screen matches the source RCO byte-for-byte where numeric, and
the native renderer consumes the parsed values (no duplicate hardcoded constants). For each constant that stays
hardcoded, the doc lists it and why.
**Size**: L. **Dependencies**: P0 for the comparison harness.

## P2 - Geometry: every screen laid out from the RCO

**Goal**: positions, sizes, scales and clip boxes equal the console's for every screen.
- P2.1 Boot the real XMB in RPCS3, dump the object tree for a screen, and lay out the native screen from those
  values (the Network column is the template for XMBML-driven layout).
- P2.2 Do it screen by screen, in the priority order P0's catalogue gives (root, each column's page, options
  panels, dialogs, info panel, OSK, player panels, settings submenus, profile screens, trophy/message screens).
- P2.3 Resolve the remaining measured-not-traced values (the open-page recession is now traced at 189/190.5 px;
  find the same for row-y, icon quads at every level, dialog boxes, list paddings).
- P2.4 Resolution scaling: the console renders 1080p and scales by PAF rules; trace the rule and apply it at
  720p/576p instead of scaling the 1920x1080 layout.

**Exit**: for each covered screen, the per-object bounding boxes differ from the RCO-derived values by <= 0.5 px
at 1080p, and the render gate for that screen passes Tier B.
**Size**: L (incremental per screen). **Dependencies**: P1.

## P3 - Text pipeline at PAF exactness

**Goal**: labels are the firmware's text, not a reconstruction that happens to look close.
- P3.1 Metrics authority: advance widths, bearings, line height, kerning; validate against the firmware's own
  text dumps (the clock advance already agrees within 0.06 px - extend to every string used in the XMB).
- P3.2 Glyph rasterisation: coverage source (FreeType with the traced flags), sub-pixel phase (done), coverage
  union (done), the PAF coverage compositing order vs shadow/halo, and the final gamma/colour path.
- P3.3 Glow: the label halo kernel and its breathing/idle curves, traced rather than observed.
- P3.4 Elision, scrolling, cursor/caret and word-wrap rules from the plugin's own Text behaviour.
- P3.5 Localisation: the console's language set; ensure the layout survives longer strings and RTL is declared
  out of scope rather than half-done.

**Exit**: for a set of strings (menu labels, values, messages, OSK), rendered glyph coverage matches a captured
PAF render within the Tier B budget, and advance sums match the firmware dump exactly.
**Size**: M-L. **Dependencies**: P1.4.

## P4 - Icon materials at firmware exactness

**Goal**: category and item icons render through the firmware's own programs with their real inputs.
- P4.1 Port `icons_no_ss_no_shad.fpo` (and its `_ss`/`_cheap` variants if used at other modes) with real inputs:
  the icon normal map, `texenv.dds`, `amb.bmp`, `mask.fpo`, the `ICONS.mnu` constants per preset, and the month
  palette (`.p3t`-independent).
- P4.2 The refraction input: the glass path reads a scene colour surface; identify exactly what the console
  samples (the background scene at what resolution/mip) and provide it in the native renderer.
- P4.3 Item-icon sizes and brightness for every level and page state (77.616/184.8 quads, `_Ambiant.w` 1.0 /
  0.247 / 0.698, the 1.1022x class if a capture proves it) - consumed from the RCO, not fitted.
- P4.4 The reflective/refractive response to the daytime light clock (the material's `_LuminanceParams`).

**Exit**: an icon closeup crop matches the reference capture within the icon-layer budget for every preset, and
the material's constants are read from the RCO rather than typed in.
**Size**: L. **Dependencies**: P1.3, P1.5.

## P5 - Background, wave and HDR exactness

**Goal**: the scene behind everything is the firmware's, not an approximation.
- P5.1 All background presets and their transitions (day/night/yoake/higure, boot presets BG1/BG2, the cold-boot
  ramp), each with its traced constants.
- P5.2 Month texture/lighting variants and the ribbon's preset keyframes (already ported - now verify per preset).
- P5.3 HDR chain: exposure/white point/glare levels per preset, the tone-map curve, the glare mip chain and the
  UI glare question (does the console bloom the UI layer too?).
- P5.4 Particles: spot position, darkness/life bounds, material/blend mode, and the emission volume (the one
  genuinely untraced item; if it cannot be traced from artifacts, say so and fit it with a documented error).
- P5.5 RSX output quantisation: 8-bit rounding, dithering, and the post filter the console applies.

**Exit**: for each preset, the background layer passes Tier B on three pinned clocks (day/night transitions), and
the HDR/particle constants are documented with their source.
**Size**: M-L. **Dependencies**: P1.3.

## P6 - Motion and transition exactness

**Goal**: every animation uses the console's curve and duration, not a fit.
- P6.1 The PAF animation set: enumerate every animation an XMB screen plays (focus, unfocus, page open/close,
  column slide, dialogs, option panels, boot, shutdown), and for each record accel mode + duration from the
  firmware (PhXmBar/PhAnim calls) or a frame-counted RPCS3 burst where the call cannot be found.
- P6.2 Promote each fit to a trace or label it permanently as a fit with its measured frame count.
- P6.3 Held-key repeat, idle fades, breathing glow, and the notification/HUD lifetimes (currently 3 s and 4 s
  fits) - trace or document.
- P6.4 The boot sequence exactness: coldboot RAF scene timing, sound sync, and the handover to the normal scene.

**Exit**: every animation the app plays is either traced (call + mode + duration) or marked fitted with a burst
measurement; the frame-counted timing is within one rendered frame of the measured burst.
**Size**: L. **Dependencies**: P0.3.

## P7 - Screen and behaviour parity, screen by screen

**Goal**: each menu's behaviour matches, not just its picture.
- P7.1 Take the P0 catalogue and close it screen by screen: Users, Settings (all submenus), Photo, Music, Video,
  TV/Video Services, Game, Network, PSN, Friends, plus dialogs, OSK, player control panels, options menus.
- P7.2 For each screen: correct entries and providers, focus/selection rules, Triangle/Circle/Cross semantics,
  dialogs and confirmations, error states, and the "unavailable" wording where the PC cannot do it.
- P7.3 The console's list actions per content type (Play, Information, Delete, Copy, Display All, Sort, Group),
  implemented where a backend exists and absent-with-reason otherwise.
- P7.4 Console state that spans screens: background music/tasks, notifications, profile switching, theme change,
  remote-play-style handoffs.

**Exit**: for every catalogue screen marked reachable: Tier A dump assertion, Tier B gate, Tier C scripted input
trace. Unreachable screens are listed with the exact missing backend.
**Size**: L (largest phase). **Dependencies**: P2, P3, P6.

## P8 - Audio parity

**Goal**: the same cues at the same moments at the same mix.
- P8.1 Cue map and timing from the firmware (nine system cues inventoried; wire the remaining call sites).
- P8.2 BGM: the console's background music behaviour (source, crossfade, ducking, volume routing).
- P8.3 Mix chain: the console's master/per-app volume, the firmware's playback-volume constant, latency budget.
- P8.4 Theme sound roles and boot sound exact volume/timing.

**Exit**: a scripted input trace produces the captured cue sequence with matching peak/RMS per cue, and BGM
behaviour is documented or implemented to the evidence level available.
**Size**: M. **Dependencies**: P7 (call sites).

## P9 - Theme parity

**Goal**: a real PS3 theme renders as it does on the console, or the limitation is stated precisely.
- P9.1 What exists today: still background, icons, five sound roles, colour/font selection parsed.
- P9.2 Wire the colour selection (month palette override) and font selection where the evidence supports it.
- P9.3 Dynamic theme RAF scenes: the audit's verdict is that this is a new subsystem (Edge geometry, skinning,
  VSMX scripting, the full material set, z-sorting). Either fund it as its own multi-phase project or state in the
  UI and docs that dynamic themes are not supported and the still frame is used.
- P9.4 Theme browser: a screen that lists local themes with their preview and metadata (now that the parser reads
  it), with the same layout discipline as other screens.

**Exit**: a chosen theme's still frame and icons pass Tier B; colour/font selections demonstrably change the
render where evidence exists; dynamic themes are either implemented to a measured standard or explicitly
unsupported.
**Size**: M + L (if P9.3 is funded). **Dependencies**: P4, P7.

## P10 - Display and resolution parity

**Goal**: the same picture on the outputs a PC actually uses.
- P10.1 Internal render resolution rules at 1080p/720p/576p and how PAF scales (P2.4).
- P10.2 Overscan/safe area, aspect handling, fullscreen behaviour and the console's output filter.
- P10.3 Frame pacing on 60 and 120 Hz displays, including the console's own 60 Hz cadence.
- P10.4 HDR/metadata is out of scope (the console has none); state it.

**Exit**: at 720p the layout matches the console's 720p rendering (not a scaled 1080p frame) within the screen
budgets; pacing p95 is within one frame interval at both rates.
**Size**: M. **Dependencies**: P2.

## P11 - Parity closure

**Goal**: a defensible answer to "is it 1:1 yet?".
- P11.1 Scorecard at 100 % of reachable screens for Tiers A and B, and Tier C for the interactive ones.
- P11.2 A written list of irreducible differences (emulator-vs-hardware reference error, PC input/audio paths,
  unavailable services) with their measured size.
- P11.3 Regression protection: the parity scorecard runs as the maintained gate; a new screen or change must not
  lower the published score without a dated justification.
- P11.4 A final handoff that states the score, the method and the remaining list - no overclaiming.

**Exit**: published scorecard + irreducible-difference list + green regression run.
**Size**: S-M. **Dependencies**: all of the above.

## Order and honest expectations

1. **P0 first** - without the corpus nothing else can claim parity, and it also protects the product track.
2. **P1** in parallel with P0 (it is resource parsing, independent of captures).
3. Then **P2**, **P3**, **P4**, **P5**, **P6** roughly in that order: geometry and text are what every screen needs;
   materials and the background follow; motion needs the RPCS3 rig.
4. **P7** is where the bulk of the "rest of the XMB" lands; it consumes P2/P3/P6 per screen.
5. **P8**, **P9**, **P10** once their dependencies are in.
6. **P11** last.

Do not read the current overall score as the gap to close: a large part of the register's missing half is P7
screens that exist in firmware but have no native implementation at all, and each screen is its own measurable
unit. The realistic path is to close P0 + P1, then take P7 screen by screen with P2/P3/P6 equipment in place.

## What this track will still not deliver

- Bit-exact RSX output: the reference is an emulator frame, and the console's exact rasterisation (half-pixel
  rules, precision modes) is only partly recoverable. Tier B is a perceptual budget, stated as such.
- Disc, PSN, online services: no backend; they stay honest "unavailable" screens.
- Dynamic theme scenes unless P9.3 is funded as its own project.
- Hardware-specific quirks with no PC equivalent (HDMI/audio handshakes, console power states).
## Dispatch queue (2026-10-27)

Agents run against one shared tree today; `WORKSPACES.md` describes the split that removes the write collisions
(this table is the same mapping expressed per agent). Each row names the files it may write and what it must not touch.
Slots are limited (8 concurrent); this is the order they start as slots free.

| Order | Agent | Phase items | Write fence | Prerequisites |
|---|---|---|---|---|
| 1 | analysis: item options menu + information panel | P2 groundwork for those surfaces | new `audit/ITEM-MENU-INFO-FIRMWARE.md`, `work/` | none (docs/analysis) |
| 2 | analysis: media panels + volume display | P2/P5 groundwork for the player surfaces | new `audit/MEDIA-PANELS-FIRMWARE.md`, `work/` | none |
| 3 | **P0** parity infrastructure | P0.1-P0.5 | new `work/parity.ts`, new `work/parity-catalogue.json`, `audit/PARITY-SCORECARD.md`; NOT `work/ab-layers.ts` | none (but reads the harness) |
| 4 | **P1** resource/object model | P1.1-P1.5 | new `src/rco_objects.{h,cpp}` (+ CMake entry), new `audit/RCO-OBJECT-MODEL.md`; no shell/view edits | P0 catalogue (for what to dump) |
| 5 | **P2** geometry | P2.1-P2.4, one screen family at a time | `src/view.cpp`/`view.h` + new `audit/GEOMETRY-<screen>.md` | P1, OSK/dialog/item-menu/media specs |
| 6 | **P3** text exactness | P3.1-P3.5 | `src/text_renderer.*`, `src/font_raster.*`, `audit/TEXT-RENDERER.md` | P1 fonts |
| 7 | **P4** icon materials exactness | P4.1-P4.4 | `src/icons.*`, `audit/ICON-MATERIAL-CLOCK.md`, `audit/ITEM-ICONS.md` | P1 materials, P5 scene input |
| 8 | **P5** background/wave/HDR/particles | P5.1-P5.5 | `src/hdr.cpp`, `src/ribbon.cpp`, `src/particles.*`, `audit/PARTICLES.md` | P1 materials |
| 9 | **P6** motion traces | P6.1-P6.4 | `audit/PAGE-MOVE.md`, `audit/ANIMATION-SMOOTHNESS.md`, `src/view.cpp` (easing only) | RPCS3 capture rig free |
| 10 | **P7** screens and behaviour | P7.1-P7.4, one column at a time | `src/shell.cpp`/`shell.h` + new per-screen audits | P2, P3, P6 |
| 11 | **P8** audio | P8.1-P8.4 | `src/interaction_audio.*`, `audit/INTERACTION-AUDIO.md` | P7 call sites |
| 12 | **P9** themes | P9.2-P9.4 (P9.3 is a separate funded project) | `src/p3t.*`, `audit/THEMES.md`, theme browser in shell | P4, P7 |
| 13 | **P10** display/resolution | P10.1-P10.4 | `src/view.cpp`, `src/main.cpp`, `audit/` note | P2 |
| 14 | **P11** closure | P11.1-P11.4 | `audit/PARITY-SCORECARD.md`, `audit/FIRMWARE-PARITY-GAPS.md` | all |

Rules for every parity agent: an RCO value must be **read at runtime** or generated from a documented extraction with
its provenance; a fitted constant must be labelled `fit` and carry the measurement that bounds it; a screen is only
"parity" when Tier A dump, Tier B score and Tier C input trace all pass. No agent may edit another's fence; new
source files need a CMake entry owned by that agent only.