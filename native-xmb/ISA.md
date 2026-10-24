---
task: Complete native C++ recreation of PS3 XMB
phase: execute
progress: in-progress
updated: 2026-10-23
---

# Ideal state

A native C++ application reproduces firmware 4.93 XMB's visual presentation,
navigation, timing, sound, local services and lifecycle using original assets
from the user's local firmware. Every visible operation has a real service or
an explicitly recorded unavailable capability. Functional breadth, visual
accuracy and responsiveness are separate completion requirements.

## Constraints

- Preserve the existing firmware runtime and user overlay as reference evidence.
- No firmware, derived assets, console identity or build outputs in Git.
- No success stubs presented as functioning features.
- Use native C++ application components; keep rendering separate from services.
- Keep reference captures and feature evidence per firmware/version and scenario.
- No new test suites without the user's request; use requested build/run evidence.

## Completion criteria

- [x] Native C++ application builds with a reproducible documented toolchain.
- [x] A clean launch runs independently of the firmware executable and overlay.
- [x] Actual firmware font and named icon textures are visibly used.
- [x] Resource failures identify the container, name and unsupported format.
- [ ] Every main category has reference-correct order, icon, labels and focus behavior.
- [ ] Horizontal and vertical movement, repeat timing and transitions match reference captures.
- [ ] Keyboard and controller share navigation/actions without separate behavior.
- [ ] All submenu/overlay transitions preserve selection and return history.
- [ ] Rendering sustains the display refresh rate with measured frame pacing.
- [ ] Background geometry, material, colour and motion match recorded reference.
- [ ] Original interaction sounds have reference-correct timing and mixing.
- [ ] RCO text/localization, layout, animation, image and model resources are supported.
- [ ] User/profile creation, selection and persistent settings work.
- [ ] Theme, display, sound, date/time, input and power options have native backends.
- [ ] Local photo browsing, viewing, rotation and slideshow work.
- [ ] Local audio browsing, playback, metadata, playlists and controls work.
- [ ] Local video browsing, playback, subtitles and controls work.
- [ ] Removable media and library updates appear without blocking rendering.
- [ ] Game data/save/trophy browsing uses real content and safe operations.
- [ ] Game launch integrates a configured execution backend and tracks return lifecycle.
- [ ] Browser, downloads and local network media have native services.
- [ ] Online/friend/store features have documented real backend requirements and availability.
- [ ] Unsupported/discontinued online services never fabricate login, purchases or messages.
- [ ] Native startup, shutdown and restart have consistent state persistence.
- [ ] Settings storage is versioned and writes recover safely after interruption.
- [ ] Asset paths/configuration are portable between installations.
- [ ] A feature inventory covers every menu/query/plugin found in the local reference resources.
- [ ] Each feature has implementation status, reference evidence and remaining gaps.
- [ ] A ten-minute interactive session has no crash, freeze or unbounded resource growth.
- [ ] Side-by-side captures substantiate visual fidelity across startup, categories and dialogs.
- [ ] Native build instructions, limitations and phase handoffs are current.
- [ ] The user can distinguish the completed recreation from an early foundation in the documentation.

## Decisions

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

## Evidence

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
REFERENCE-2026-10-23.md and HANDOFF.md. QRC/DDS build and local70-map sheet
completed; Computer Use observed original Settings shapes and Theme → Color →
Back restoring selection. The broad transition-parity criterion remains unchecked.
