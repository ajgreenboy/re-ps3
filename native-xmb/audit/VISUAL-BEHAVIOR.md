# Visuals, interaction behavior and timing

## Evidence rules

- **Resource:** a value, asset or structure read from the user's local firmware.
- **Observed:** a visible state in the supplied screenshot/recording or native window.
- **Estimated:** a visually identified range bounded by captured frames.
- **Native source:** what the present C++ implementation does; not PS3 truth.
- **Unmeasured:** keep the requirement open; do not replace it with a guessed constant.

The user explicitly requires shading and animation fidelity, including timing.
This document makes those independent requirements rather than treating a usable
menu as proof of visual parity.

## 1. Icon orientation: identified and corrected

The newly adopted QGL normal maps were displayed vertically inverted. The Game
controller's grips pointed upward, Music's note heads were at the top and the
small Settings wrench appeared on the wrong side of the vertical shape.
This was visible in native screenshots and contradicted the user-supplied VSH.

The correction is at asset ingestion: only images from `qgl/icontex` are flipped
vertically after DDS decode, before entering the named image cache. RCO/GIM
icons, photos, wallpaper and unrelated DDS resources keep their own orientation.
This fixes all numbered icon maps together rather than patching each draw call.
Original normal-vector channel data and alpha are retained.

The rebuilt native Game screen was observed with downward grips, matching the
reference silhouette. [Corrected native screenshot](../screenshots/09-icon-orientation-corrected.jpg).
Monthly background texture orientation and exact normal-map light direction need
separate comparison; this fix is not evidence for those surfaces.

## 2. Materials and shading

| ID | Behavior to reproduce | Evidence and current finding | Remaining requirement |
|---|---|---|---|
| VIS-01 | Theme-tinted opaque icon faces | User correction; original normal maps carry silhouette coverage | Preserve coverage and antialiased edges; do not simulate theme tint by making faces translucent |
| VIS-02 | Embossed bevel/specular response | Normal-map RGB and icon shader resources exist | Port or reconstruct original diffuse/specular/environment response |
| VIS-03 | Light positions and material response | `ICONS.mnu` contains two-light angles, diffuse/environment/specular controls and powers | Establish coordinate conventions and compare per-pixel response |
| VIS-04 | Reflection/refraction | Original material settings and RSX programs exist | Current CPU directional light has no equivalent reflection/refraction |
| VIS-05 | Selected/unselected shading | Reference shows different emphasis by focus/context | Separate material parameters from focus size, text opacity and parent-column fading |
| VIS-06 | Full-color artwork | PSN, Store, covers and photos contain actual color | Exclude those assets from monochrome tint rules |
| VIS-07 | Icon glow/shadow | Beveled appearance in reference; shadow/material controls in original data | Measure extent, falloff, offset and overlap; not a generic drop-shadow guess |
| VIS-08 | Theme/time-driven lighting | Icon overrides exist for day/night/dusk/dawn and boot/activity states | Resolve override selection/blending and dependencies on theme/time |
| VIS-09 | Color/alpha blending space | Final appearance depends on source alpha, tone mapping and compositor | Establish linear/sRGB conventions and premultiplication; compare edges against multiple backgrounds |
| VIS-10 | Text material | Original fonts and Text object fields include colors, sizing, spacing and shadows | Reproduce font/style selection, shadow, glow, baseline and active/dim colors |

The current native material uses a cached directional-light approximation over
the original normal maps. It preserves original alpha and uses six native tint
presets. It is **not** the original shader. As one example of resource evidence,
the base icon configuration contains diffuse attenuation 2, environment 0.74,
specular powers 100 and 30 and exposure 0.82. Those isolated values are not enough
to recreate the full pipeline or prove the current material correct.

## 3. Background, wave and particles

| ID | Surface | Evidence | What must be recovered/measured |
|---|---|---|---|
| VIS-11 | Monthly background colors | Twelve RGB and twelve night DDS resources in `lines.qrc` | Orientation, interpolation, time-of-day blending and theme override |
| VIS-12 | Background spatial gradient | Original textures and recorded warm gold scene | Match gradient positions and luminance without fitting a single screenshot only |
| VIS-13 | Wave geometry | Line configuration, spline SPU program and RSX programs | Spline/simulation state, projection, width, folds and depth |
| VIS-14 | Wave material | Fresnel lookup images, line shaders and HDR configuration | Translucency, highlights, Fresnel/falloff and tone mapping |
| VIS-15 | Wave time evolution | Reference shows continuously moving overlapping bands | Phase, velocity, periodicity and continuity during menu changes |
| VIS-16 | Particle emission | Particle configuration, iridescent texture, SPU/RSX resources | Emission distribution/rate, motion, size, depth, lifespan and brightness |
| VIS-17 | Particle response to interaction | UI-specific particle settings include input/wind coefficients | Capture idle, scrolling, rapid input and controller motion separately |
| VIS-18 | Activity/background variants | Boot, gameboot, music and welcome overrides exist | Determine which scenes use which overrides and how they transition |
| VIS-19 | Wallpaper and Classic mode | Original Theme surface documents these options | Current wallpaper works as a host image; current Classic only changes the approximation and is not verified original Classic behavior |

The new native renderer samples the current month's original RGB resource for
the Original color path. Its moving bands and particle field remain procedural
approximations. A texture loader does not reproduce the SPU simulation, original
projection or post-processing. Green/custom colors still use the native gradient.

## 4. Layout and focus geometry

Measurements below are approximate normalized coordinates from the recording,
expressed on a 1920×1080 canvas. They are not claimed recovered firmware constants.

| ID | Element | Observed/estimated geometry | Native status |
|---|---|---|---|
| VIS-20 | Category focus | x about 560, y about 290; horizontal separation about 202 | Implemented approximate positions |
| VIS-21 | Main focus slot | Selected entry near y505 | Implemented |
| VIS-22 | Normal vs wide content | Settings labels nearer x680; wide Game/media cards nearer x756 | Implemented approximate branches |
| VIS-23 | Root preceding items | Items above selection move above the category band | Continuous mapping implemented; exact choreography unverified |
| VIS-24 | Focus spacing | Root preceding gap about 380; following focus gap about175; ordinary row spacing about80 | Implemented approximate focus-gap mapping |
| VIS-25 | Entering a submenu | Category/selected parent shift toward x310; child icons about x560; child labels about x620 | Implemented parent/child columns |
| VIS-26 | Parent context | Selected parent stays large; inactive parent items remain dim | Implemented approximation |
| VIS-27 | Child focus | Larger local gap around focused item, normal spacing farther away | Implemented approximation |
| VIS-28 | Selection restoration | Back restores root item and nested child selection | Theme → Color → Back observed in earlier checkpoint |
| VIS-29 | Long labels and values | Labels, descriptions and right-aligned values occupy different fields | Values/ellipsis added; clipping and localization need observed coverage |
| VIS-30 | Thumbnail shape | Covers, cards, folders and utility icons do not share one size rule | Current texture-name heuristic is incomplete; use explicit presentation metadata |

Record geometry separately at 4:3, 16:9, different resolutions, overscan/display
area settings and text languages. Scaling the entire scene can preserve a
composition while still producing incorrect text size or focus movement.

## 5. Animation families

| ID | Family | State and timing requirements | Current native gap |
|---|---|---|---|
| VIS-31 | Category change | Strip motion, old/new list phase, icon resize, label fade, background continuity | Incoming/outgoing content fade added; timings are still generic |
| VIS-32 | Vertical movement | Row trajectories, focus size, label emphasis, descriptions and settle | Exponential smoothing and approximate spacing |
| VIS-33 | Submenu open | Parent translation, other categories recede, child appears in phases | Same generic easing used for all components |
| VIS-34 | Submenu close | Child dismissal, parent restoration and selected-label return | Generic return; reference appears faster than entry |
| VIS-35 | Context menu | Panel entry/exit, dimming, focus transfer and cancel | Not implemented |
| VIS-36 | Player control panel | Overlay motion/fade, show/hide and auto-dismiss | Not implemented |
| VIS-37 | Dialogs | Background treatment, panel reveal, button focus and dismissal | Static message overlay only |
| VIS-38 | Information panel | Busy spinner, message/news/avatar indicators and transition | Clock only |
| VIS-39 | Notifications/trophies | Entry, hold, exit, sound synchronization and overlap policy | Not implemented |
| VIS-40 | Playback launch/return | Menu/background to media, control ownership, resume selection | Basic source switch, no reference-matched transition |
| VIS-41 | Photo transitions | Normal, slide and fade; transformed-image continuity | Immediate image replacement only |
| VIS-42 | Artwork previews | Selected title background/cover fade and unload on focus change | Not implemented |
| VIS-43 | Startup/shutdown/login | Logo/wave/particles, user choice, audio and lifecycle | Not reconstructed |
| VIS-44 | Rapid/reversed input | Interruptibility, retargeting, selection vs visual state and repeat | Needs dedicated recordings and comparison |

### Frame-based timing observations

The precise sample is 40,000–48,000 ms from the user's recording. It contains
241 frames with gaps of 33 or34 ms (mean33.333ms). PNG output preserves source
resolution. These samples bound visible changes, not controller-to-photon latency.

| Event | Visible change interval in clip | Initial timing conclusion |
|---|---|---|
| Date/Time submenu entry | First clear change around42.77s; child presentation near settled by43.00s | Roughly230–300ms, visual estimate |
| Date/Time Back | Change around43.10s; root restoration near43.17s | Roughly70–130ms, visual estimate |
| Theme submenu entry | First clear change44.10s; child text/focus near settled around44.37–44.40s | Roughly270–330ms, visual estimate |
| Theme Back | Change around44.57s; root presentation near settled44.63–44.67s | Roughly70–130ms, visual estimate |

These are provisional frame inspections, not fitted animation constants. The
recording shows RPCS3 frame-rate variation. Start/settle definitions, opacity
threshold and emulator scheduling can change the range. No claim is made that
these estimates equal original-console timings.

[Frame-by-frame Theme entry/return evidence](../screenshots/reference-timing-theme.jpg)
is extracted from the user-supplied recording, not an automated RPCS3 capture.

### Current native timing, read from source

- Movement and category/depth fade use `1 - exp(-18 * dt)` smoothing.
- That reaches90%,95% and99% of a step after approximately128,166 and256ms.
- A0.001 depth/fade cleanup threshold can retain a transition for about384ms.
- Controller repeat begins after300ms, then repeats every100ms.
- Keyboard autorepeat currently follows host key events; it is not normalized to
  the controller repeat schedule.
- Rendering requests use a16ms Qt timer. Logged62.5 loop FPS does not prove60Hz
  presentation pacing, low latency or absence of dropped/repeated displayed frames.

Entry and Back therefore need separate timelines. Reusing one exponential for
every property cannot establish the recorded phase ordering or timing.

## 6. Resource animation findings

The structural scan covers122 RCO files with zero scan errors:8,070 object/tree
entries and165 animation/tree entries, of which113 are action entries. These
counts include containers and are not counts of distinct user-visible features.

Main `explore_plugin_full.rco` and `sysconf_plugin.rco` contain no animation table.
Their layout objects alone do not encode the whole menu transition system.
`system_plugin.rco` has17 animation/tree entries; its circle/cross hint show/hide
Fade actions contain a raw duration field100 and acceleration mode0. Other
resources include values0,100,174,200,863 and1000. Units, enum meaning and runtime
scheduling must be confirmed before interpreting every value as milliseconds.

The inspector recognizes MoveTo, Recolour, Rotate, Resize, Fade, Delay, FireEvent,
Lock/Unlock and SlideOut layouts using primary RCOMage format definitions. It
retains unknowns. It does not execute animation events or evaluate a PAF timeline.

## 7. Interaction sound

Four original resources now decode from `system_plugin.rco`: cursor, decide,
cancel and error. Observed files are VAGp version6,48kHz stereo with PS ADPCM
blocks. The native service caches decoded PCM and reuses an audio sink. The
original error cue decodes as silence; no substitute is synthesized.

Audit requirements: exact action-to-cue mapping, boundary/no-op behavior, repeat
suppression, interruption/mixing, volume/key-tone settings, device errors and
audio/visual phase alignment. Native Move has a45ms debounce. Audibility and
reference-matched timing have not been independently measured.

## 8. Measurement plan for remaining behaviors

For every animation, retain source clip hash, frame timestamps, firmware/theme,
resolution, input sequence, before/after state and measurement method. Track
separate x/y/scale/opacity curves. Define first-change and settled thresholds,
report dropped-frame gaps, then fit candidate easing curves with residuals.
Capture button event timestamps before making latency claims.

For shading, compare interior pixels and antialiased edges at multiple phases,
backgrounds and themes. Recover light/material parameters before adjusting tint
by eye. Record which values are source constants and which are fitted estimates.

Required new footage includes context/options panels, photo/music/video panels,
dialogs, notifications, sign-in/trophy states, continuous held navigation, rapid
reversal, controller reconnect, media launch/return and boot/shutdown. They are
requirements in this handoff, not behaviors established by the present clip.
