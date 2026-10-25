---
task: Complete native C++ recreation of PS3 XMB
phase: execute
progress: in-progress
updated: 2026-10-25
---

# Native XMB ideal state

## Problem

The native application has original assets and basic navigation but incomplete original services and presentation behavior. The audit identifies broad gaps that a smooth root menu cannot establish as complete.

## Vision

A native C++ application reproduces firmware 4.93 XMB's visual presentation,
navigation, timing, sound, local services and lifecycle using original assets
from the user's local firmware. Every visible operation has a real service or
an explicitly recorded unavailable capability. Functional breadth, visual
accuracy and responsiveness are separate completion requirements.

## Out of Scope

Firmware execution is preserved as a reference track. This native application does not implement Sony's private online services by displaying fabricated successful operations.

## Principles

- Preserve the relationship between visible operations and actual capabilities.
- Keep presentation fidelity separate from functional correctness.
- Record measured, resource-derived and unknown behavior distinctly.

## Constraints

- Preserve the existing firmware runtime and user overlay as reference evidence.
- No firmware, raw extracted assets, console identity or build outputs in Git. Approved UI evidence screenshots are retained separately from raw assets.
- No success stubs presented as functioning features.
- Use native C++ application components; keep rendering separate from services.
- Keep reference captures and feature evidence per firmware/version and scenario.
- No new test suites without the user's request; use requested build/run evidence.

## Goal

Recreate the stock firmware4.93 XMB feature surface and presentation with native C++ services and original local assets. Completion requires evidence for original workflows and their conditional states, including timing and shading.

## Criteria

- [x] ISC-1: Native C++ application builds with a reproducible documented toolchain.
- [x] ISC-2: A clean launch runs independently of the firmware executable and overlay.
- [x] ISC-3: Actual firmware font and named icon textures are visibly used.
- [x] ISC-4: Resource failures identify the container, name and unsupported format.
- [ ] ISC-5: Every main category has reference-correct order, icon, labels and focus behavior.
- [ ] ISC-6: Horizontal and vertical movement, repeat timing and transitions match reference captures.
- [ ] ISC-7: Keyboard and controller share navigation/actions without separate behavior.
- [ ] ISC-8: All submenu/overlay transitions preserve selection and return history.
- [ ] ISC-9: Rendering sustains the display refresh rate with measured frame pacing.
- [ ] ISC-10: Background geometry, material, colour and motion match recorded reference.
- [ ] ISC-11: Original interaction sounds have reference-correct timing and mixing.
- [ ] ISC-12: RCO text/localization, layout, animation, image and model resources are supported.
- [ ] ISC-13: User/profile creation, selection and persistent settings work.
- [ ] ISC-14: Theme, display, sound, date/time, input and power options have native backends.
- [ ] ISC-15: Local photo browsing, viewing, rotation and slideshow work.
- [ ] ISC-16: Local audio browsing, playback, metadata, playlists and controls work.
- [ ] ISC-17: Local video browsing, playback, subtitles and controls work.
- [ ] ISC-18: Removable media and library updates appear without blocking rendering.
- [ ] ISC-19: Game data/save/trophy browsing uses real content and safe operations.
- [ ] ISC-20: Game launch integrates a configured execution backend and tracks return lifecycle.
- [ ] ISC-21: Browser, downloads and local network media have native services.
- [ ] ISC-22: Online/friend/store features have documented real backend requirements and availability.
- [ ] ISC-23: Unsupported/discontinued online services never fabricate login, purchases or messages.
- [ ] ISC-24: Native startup, shutdown and restart have consistent state persistence.
- [ ] ISC-25: Settings storage is versioned and writes recover safely after interruption.
- [ ] ISC-26: Asset paths/configuration are portable between installations.
- [ ] ISC-27: A feature inventory covers every menu/query/plugin found in the local reference resources.
- [ ] ISC-28: Each feature has implementation status, reference evidence and remaining gaps.
- [ ] ISC-29: A ten-minute interactive session has no crash, freeze or unbounded resource growth.
- [ ] ISC-30: Side-by-side captures substantiate visual fidelity across startup, categories and dialogs.
- [ ] ISC-31: Native build instructions, limitations and phase handoffs are current.
- [ ] ISC-32: The user can distinguish the completed recreation from an early foundation in the documentation.
- [ ] ISC-33: Anti: tracked deliverables contain no raw firmware or private console identity.
- [ ] ISC-34: Antecedent: visual claims identify their reference firmware, theme and recording evidence.

## Test Strategy

Build logs establish compilation. Requested window observations establish specific visible states; source inspection alone does not close workflow criteria. Feature-specific runtime evidence is recorded in audit/HANDOFF.md and audit/VISUAL-BEHAVIOR.md; no new test suite is introduced.

## Features

| Area | Source boundary | Remaining completion work |
|---|---|---|
| Shell | Shell/View | Original panels, activity model and matched timelines |
| Assets | Assets/QrcArchive | Original material, wave and complete resource semantics |
| Native services | Preferences/Profiles/MediaLibrary/InteractionAudio | Complete original workflows and capability bindings |
| Feature audit | audit/ | Per-feature state variants and missing reference evidence |

## Decisions

- 2026-10-25: Fixed-colour category icons use an opaque original-normal-map material after reproducing dark
  interiors on Green. Original retains the firmware glass path. This resolves the reported appearance defect,
  not the broader material-parity criterion; evidence and limits are in audit/CATEGORY-ICON-FIX.md.
- 2026-10-25: Added an owned folder playback queue and photo viewport geometry using scoped lower-cost agents,
  followed by parent integration. See audit/MEDIA-CONTINUATION.md for observed sequencing/error/zoom behavior.

- 2026-10-25: The ribbon is ported from the firmware end to end (custom_render_plugin cloth, ffd_shader1 lattice,
  spline.elf sheet), with each stage checked against RPCS3 captures. The LINE1 preset schedule follows the plugin's
  clock keyframes and boot sequence. The boot blend curves are measured, because the qgl_base player is not traced.
  gameboot/welcome are not scheduled, since the native app has no game launch or first-time setup flow.

- 2026-10-25: User directed work from Phase 2 onwards while another session completed the category shader port.
  Text now uses a native-resolution GL atlas with Qt shaping/coverage; this does not close firmware rasterizer
  parity. Recovered PAF/RCO evidence and open requirements are in audit/TEXT-RENDERER.md.

- 2026-10-24: refined: original unnumbered criteria now have stable ISC IDs; the audit retains source, observed and unmeasured status separately.
- 2026-10-24: full audit maps307 indexed topics and44 presentation requirements; catalog breadth does not establish complete runtime coverage.

- October 23: user changed the objective from executing firmware to a complete
  native recreation, explicitly in C++ and using original XMB assets.
- Qt supplies desktop/text/media integration; C++ OpenGL supplies the presentation
  surface. This avoids guest scheduling and synchronization in the rendering path.
- Local firmware resource access is read-only. Derived textures stay in memory.
- First slice establishes architecture and visible original assets. It does not
  satisfy unimplemented OS, media, online or visual fidelity requirements.
- The user-supplied19:35 recording provides structural/settled-layout evidence.
  Sampling at200ms does not establish exact transition timing or input latency.
- Original QGL normal maps replace incorrect fallback silhouettes at ingestion.
  Root/child focus gaps and parent columns now use recorded geometry; original
  RSX material, wave simulation and many service pages remain open work.

## Changelog

- 2026-10-25 | conjectured: the ribbon was a flat sheet displaced by ffd_shader1, placed on screen by fitted constants
  refuted by: RPCS3 captures of the lines1 vertex buffer (a rolled 128x128 sheet written by SPU spline.elf)
  learned: geometry comes from a PPU mass-spring cloth plus an SPU B-spline FFD; ffd_shader1 only deforms the lattice
  criterion now: ribbon geometry is verified per stage against captures; the visual match is a side-by-side screenshot

- 2026-10-24 | conjectured: directly painting QGL decoded rows preserved icon orientation
  refuted by: native controller grips pointed upward against the supplied VSH screenshot
  learned: icon texture coordinates need normalization at ingestion
  criterion now: original icon fidelity remains open; the specific orientation correction has a native screenshot

## Verification

2026-10-25 continuation: default and isolated Release builds succeeded. Computer Use inspected the rebuilt
Green-theme window with light opaque icons, left open. Five media GUI replays exited zero, covering automatic
advance with a bad file, shuffle/Repeat All, Repeat One, bounded all-invalid failure, and photo zoom/pan/reset.
Physical-controller and sustained-media behavior, plus matched fixed-colour icon reference parity, remain open.

2026-10-25 Ribbon: cloth boot frame 1.6e-7; one-step x about 1e-4; FFD lattice 5e-5..1.9e-4 over 83 s; spline FFD
4.8e-4; sheet positions/normals 1e-5; boot preset schedule (SCALE2) within 2.2e-3. Native 1080p grabs match the
RPCS3 reference's band placement. A side-by-side boot recording against RPCS3 remains open.

2026-10-25 Phase 2: isolated Release build in out/native-phase2 succeeded. Scripted root, category transition
and dialog captures rendered via the glyph atlas and exited zero. A lost-glyph regression was reproduced and
corrected by resetting GL unpack skipped rows/pixels. Foreground Computer Use confirmed readable labels;
one 960x540 foreground five-second report was 52.7 fps. Hidden-window observations are not performance evidence.
Exact reference crops, rasterizer hinting/gamma and native glow parameters remain unverified.

Native Release build: work/native-reference-build.txt. Session193245-983 loads
the original font,44 images with no failures,166 English strings and14 resource
bindings. Computer Use captures show original icons/font. Initial resource
failures named each container/texture and format; fixes rebuilt successfully.
The native launch does not execute the firmware runtime or access its VFS overlay.
No clean-machine build has been performed; documented installed toolchain was used.

Build and run observations are recorded in HANDOFF.md. Unchecked criteria remain
open until the specific behavior has been observed. Definition inventory is
complete for28 local XMBML files; wider plugin/RCO coverage remains pending.

Recording evidence and observed nested return are detailed in
HANDOFF.md. QRC/DDS build and local70-map sheet
completed; Computer Use observed original Settings shapes and Theme → Color →
Back restoring selection. The broad transition-parity criterion remains unchecked.
