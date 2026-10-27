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
| VIS-31 | Category change | Strip motion, old/new list phase, icon resize, label fade, background continuity | Incoming/outgoing content fade is a 200 ms mode-5 paf ease (`View::paintGL` and its column loop); item phase timing still fitted |
| VIS-32 | Vertical movement | Row trajectories, focus size, label emphasis, descriptions and settle | PafEase mode 5 (200 ms) for the item move (`View::paintGL`); row spacing still approximate |
| VIS-33 | Submenu open | Parent translation, other categories recede, child appears in phases | Category/item page moves have distinct paf eases (200 ms mode 5; page 300 ms mode 4, fitted), but the child phases share one curve |
| VIS-34 | Submenu close | Child dismissal, parent restoration and selected-label return | Generic return; reference appears faster than entry |
| VIS-35 | Context menu | Panel entry/exit, dimming, focus transfer and cancel | Right-hand options panel for settings choices and the item-level Triangle menu: `Shell::buildOptions()` derives `Open`/`Play`/`View`/`Start`, `Information`, `Sort By` (a second level that really reorders the page) and `Close` from the selected entry, `View::drawOptionsPanel` keeps the settings strip's look (200 ms mode-5 slide, `optionsOpen_`/`optionsNested_`) and owns input while open (`src/shell.h` options API, audit/OPTIONS-MENU.md). Firmware panel animation and the rest of the per-item command set (Delete, Copy, Add to Playlist, Eject Disc) remain open |
| VIS-36 | Player control panel | Overlay motion/fade, show/hide and auto-dismiss | Controls fade after 3 s over 250 ms during playback (`src/media_controls.cpp`, `View::paintGL` media overlay); a video player panel on Triangle lists `Subtitles Off`/one row per sidecar, the audio tracks `QMediaPlayer::audioTracks()` reports, Repeat and Shuffle, with a 400 ms read-back before an audio switch is claimed (`Shell::buildPlayerOptions()`, audit/SUBTITLES.md); the firmware panel's projection and its button row are open |
| VIS-37 | Dialogs | Background treatment, panel reveal, button focus and dismissal | Styled dialog: full-frame dim, two rules, heading/body split and a button hint (`View::paintGL` dialog block); multi-button variants and entry/exit animation open |
| VIS-38 | Information panel | Busy spinner, message/news/avatar indicators and transition | Clock text and analog face plus the PS-tap controller HUD (`View::paintGL`); message/news/trophy indicators open |
| VIS-39 | Notifications/trophies | Entry, hold, exit, sound synchronization and overlap policy | Partial, landed 2026-10-26 and captured the same day: `Shell::notify` holds a bounded FIFO queue (one pop-up, 4 s lifetime, 4-entry limit) and `View::drawNotification` composes the plaque from `system_plugin`'s `tex_notification*` pieces with a 200 ms mode-5 slide/fade, fed by two real producers (a tracked title's return, a library rescan) and a `NOTIFY=` script token. Verified: script token, three queued events shown one at a time, both producers, and an info-bar crop proven bit-identical while the pop-up is up (audit/NOTIFICATIONS.md sections 6-8). Open: the notification sound, the firmware overlap policy and its untraced `2100` argument, per-type suppression, trophy/friend triggers, and the three-line variant (the build uses one or two lines) |
| VIS-40 | Playback launch/return | Menu/background to media, control ownership, resume selection | Basic source switch, no reference-matched transition |
| VIS-41 | Photo transitions | Normal, slide and fade; transformed-image continuity | Immediate image replacement only |
| VIS-42 | Artwork previews | Selected title background/cover fade and unload on focus change | Not implemented |
| VIS-43 | Startup/shutdown/login | Logo/wave/particles, user choice, audio and lifecycle | Cold boot is ported (`src/coldboot.cpp`); the PS menu turns the native app off after one confirmation (`Shell::psMenuActivate`); a focus-independent PS watcher (`src/ps_watcher.*`, 20 ms poll, 1 s hold, 500 ms dropout grace) brings the XMB forward from a running title and opens the system menu on hold (audit/PS-INGAME-HOOK.md); the in-game XMB itself, the login/user-select screen and a shutdown animation are open |
| VIS-44 | Rapid/reversed input | Interruptibility, retargeting, selection vs visual state and repeat | Needs dedicated recordings and comparison |
| VIS-45 | Volume HUD | Firmware volume page (`videoplayer_volume_page` / `videoplayer_volume_level_meter`, the same pair in `audioplayer_plugin` and `explore_plugin_full`), level meter, show/hide timing and suppression | Implemented 2026-10-26: `View::pollVolumeRamp` (L2/R2, one 5% step on press then while held, 120 ms repeat shortening to 60 ms past 0.75 pressure) and `View::drawVolumeHud` in the 222x58 band left of the info bar, 3 s + 250 ms fade on `MediaControls`, every change read back from `host::masterVolume()`, and the HUD hidden under the PS menu, message pages, the options panel, the OSK and the intro (audit/HTPC-IDEAS.md section 6). Gap: the page projection is fitted rather than traced, and the firmware page's own per-app mixer is not reproduced |
| VIS-46 | Continue Watching | Row content, ordering, empty and stale states, and the actions on them | Implemented 2026-10-26 as the Video column's first row: `Shell::openContinueWatching()` lists at most 20 records newer than 30 days (`kContinueRowLimit`, `kContinueMaxAgeSeconds`), newest first, stale rows drawn disabled, Triangle offering Play/Information/Remove from Continue Watching (audit/MEDIA-CONTROLS.md). Gap: no firmware reference was captured for this surface, so its styling and placement are native choices |

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

### Current native timing, read from source (revised 2026-10-26)

The block below was accurate for the exponential easing that preceded the PafEase port; the struck
claims are kept as the record and are superseded by the source lines beside them.

- ~~Movement and category/depth fade use `1 - exp(-18 * dt)` smoothing.~~ Superseded: every move is a
  paf animation through `View::PafEase` - category, item and column cross-fade are 200 ms mode 5
  (traced in vsh.elf PhXmBar), the depth/page move is 300 ms mode 4 (fitted, not traced)
  (`src/view.cpp:125-156,588-593`).
- ~~That reaches90%,95% and99% of a step after approximately128,166 and256ms.~~ Superseded by the
  per-frame mode tables: a 200 ms move lands in 12 frames and a 300 ms move in 18.
- ~~A0.001 depth/fade cleanup threshold can retain a transition for about384ms.~~ The 0.001 snap still
  exists (the depth snap in `View::paintGL`), but PafEase reaches the target at its own duration, so no 384 ms
  retention follows.
- ~~Controller repeat begins after300ms, then repeats every100ms.~~ Corrected: repeat begins after
  300 ms, then every 67 ms, counted from the deadline (`View::pollController`); measured on the
  console at 66.7 ms.
- ~~Keyboard autorepeat currently follows host key events; it is not normalized to the controller
  repeat schedule.~~ Corrected: arrow-key OS auto-repeats are dropped (`View::keyPressEvent` /
  `View::keyReleaseEvent`) and go through the controller repeat schedule.
- ~~Rendering requests use a16ms Qt timer.~~ Corrected: the 16 ms timer only polls input; the next
  frame is requested from `QOpenGLWidget::frameSwapped` (`src/view.cpp:95-97,106`). Logged62.5 loop
  FPS does not prove60Hz presentation pacing, low latency or absence of dropped/repeated displayed frames.

Entry and Back still need separate timelines: the recorded Back transition is faster than entry
(roughly70-130 ms against230-330 ms in the frame observations above), while the current depth ease
uses one 300 ms duration in both directions.

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

## 7. Interaction sound (revised 2026-10-26)

The system table now loads all nine `system_plugin.rco` cues - `snd_cursor`,
`snd_decide`, `snd_category_decide`, `snd_cancel`, `snd_option`, `snd_error`,
`snd_system_ok`, `snd_system_ng`, `snd_trophy` - as VAGp version 6, 48 kHz stereo
PS ADPCM. Six native cue slots map onto them: Move=cursor, Confirm=decide,
Back=cancel, Error=system_ng, Options=option, Accept=system_ok, with Options and
Accept falling back to decide (`src/interaction_audio.cpp:32-43,321-345`).
`snd_error` is a real 1.8 ms silent stub and is deliberately not used for Error;
`snd_trophy` and `snd_category_decide` have no native action yet.

Before 2026-10-26 only four cues - cursor, decide, cancel and error - were decoded,
and the error entry then decoded as silence with no substitute.

Audit requirements: exact action-to-cue mapping, boundary/no-op behavior, repeat
suppression, interruption/mixing, volume/key-tone settings, device errors and
audio/visual phase alignment. Native Move has a 45 ms floor and same-cue retriggers
are dropped until the sample ends (`src/interaction_audio.cpp:405-415`).
Audibility and reference-matched timing have not been independently measured.

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
