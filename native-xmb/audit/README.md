# XMB audit index

This directory holds the audits, specifications, run records and plans for the native XMB. Every document is
listed once below, grouped by type. Status is a property of the document, not of its position here: a **run
record** is a dated snapshot, a **firmware specification** is evidence about the console, and only the first
group says what is current. Superseded handoffs and phase plans live in [../archive/](../archive/README.md).

Provenance labels used throughout: **traced** (from a console frame, RCO object, MNU table or decoded program),
**fit** (a native approximation with its removal criterion) and **unprovable** (with the artifact that would
settle it). Local evidence the documents cite (`out/native-reference/`, `fw493/`, RPCS3 captures) is untracked.

## Contents

1. [Start here: current state and plans](#1-start-here-current-state-and-plans)
2. [Phase plans and handoffs](#2-phase-plans-and-handoffs)
3. [Parity and fidelity audits](#3-parity-and-fidelity-audits)
4. [Firmware specifications](#4-firmware-specifications-how-the-console-does-it)
5. [Rendering and layout measurements](#5-rendering-and-layout-measurements)
6. [Feature notes: services and integrations](#6-feature-notes-services-and-integrations)
7. [Feature notes: shell, media and input](#7-feature-notes-shell-media-and-input)
8. [Verification instruments and runbooks](#8-verification-instruments-and-runbooks)
9. [Run records and investigations](#9-run-records-and-investigations)
10. [Evidence base and sources](#10-what-the-evidence-establishes)

## 1. Start here: current state and plans

| Document | What it is |
|---|---|
| [../HANDOFF-2026-11-09.md](../HANDOFF-2026-11-09.md) | The active handoff: state, build/run/verify, gates, what landed, work in flight, open work |
| [PHASES-2026-11-07.md](PHASES-2026-11-07.md) | Plan of record: done work, ranked backlog, parallel workstreams |
| [FEATURE-STATUS-2026-11-04.md](FEATURE-STATUS-2026-11-04.md) | Per-feature status with dated updates |
| [FEATURE-CATALOG.md](FEATURE-CATALOG.md) | 307 indexed Sony guide topics/actions with native coverage status; machine-readable companion `feature-catalog.json` |
| [FIRMWARE-PARITY-GAPS.md](FIRMWARE-PARITY-GAPS.md) | The scored gap register against the 4.93 firmware |
| [../ISA.md](../ISA.md) | Completion criteria with dated verification |

## 2. Phase plans and handoffs

| Document | What it is |
|---|---|
| [PHASES-HTPC-2026-11-09.md](PHASES-HTPC-2026-11-09.md) | The HTPC product phase (D1-D12): behaving like an appliance |
| [PHASES-ZERO-FRICTION-2026-11-11.md](PHASES-ZERO-FRICTION-2026-11-11.md) | Controller-only boot: automatic sign-in, text entry, administrator windows |
| [PHASES-MANIFEST-DOWNLOAD-2026-11-11.md](PHASES-MANIFEST-DOWNLOAD-2026-11-11.md) | Manifest-driven game download and install (self-hosted source) |
| [HANDOFF-PARITY-PHASES.md](HANDOFF-PARITY-PHASES.md) | 2026-11-10 handoff for the parity track and the product track |
| [HTPC-IDEAS.md](HTPC-IDEAS.md) | The 2026-10-26 couch-system brainstorm and its task queue |
| [HTPC-LEFTOVERS-TRIAGE.md](HTPC-LEFTOVERS-TRIAGE.md) | Triage of the P6 and D3 leftovers |
| [../HANDOFF-PHASES.md](../HANDOFF-PHASES.md) | The 2026-10-25/27 phase record and the RPCS3 capture how-to |
| [../WORKSPACES.md](../WORKSPACES.md) | Write fences for parallel agents |

## 3. Parity and fidelity audits

The comparison of the build against the console, surface by surface. [FIDELITY-VERDICT.md](FIDELITY-VERDICT.md)
consolidates the others.

| Document | What it is |
|---|---|
| [FIDELITY-VERDICT.md](FIDELITY-VERDICT.md) | The exhaustive comparison, consolidated (2026-10-29) |
| [FIDELITY-SURFACES-MAP.md](FIDELITY-SURFACES-MAP.md) | Map of every surface and where its audit lives |
| [FIDELITY-ROOT-BAR.md](FIDELITY-ROOT-BAR.md) | The root frame and category bar, element by element |
| [FIDELITY-SURFACES.md](FIDELITY-SURFACES.md) | Rows, open pages, options menus, dialogs and the info/indicator surfaces |
| [FIDELITY-MEDIA.md](FIDELITY-MEDIA.md) | Media columns, players, volume HUD, on-screen keyboard, media dialogs |
| [FIDELITY-BEHAVIOUR.md](FIDELITY-BEHAVIOUR.md) | Behaviour and state-machine fidelity |
| [FIDELITY-MEASURED.md](FIDELITY-MEASURED.md) | The launcher build measured against every firmware reference |
| [PARITY-SCORECARD.md](PARITY-SCORECARD.md) | The three-tier method, the catalogue and its matrix, the text-bearing scorecard |
| [REFERENCE-GAPS.md](REFERENCE-GAPS.md) | Capture plan for the states that still owe a console reference |
| [BEHAVIOUR-GAP-AUDIT.md](BEHAVIOUR-GAP-AUDIT.md) | System-behaviour gaps: what the 4.93 XMB does and what the build does |
| [BEHAVIOUR-GAPS-SPEC.md](BEHAVIOUR-GAPS-SPEC.md) | Implementation-ready spec for the structure and behaviour gaps |
| [UI-ELEMENT-GAP-AUDIT.md](UI-ELEMENT-GAP-AUDIT.md) | The composited visuals of a console frame and what the native build draws |
| [ICON-GAP-AUDIT.md](ICON-GAP-AUDIT.md) | Every visible icon and graphic the firmware draws |
| [ITEM-ICONS.md](ITEM-ICONS.md) | Open-column item-icon parity |
| [FEATURE-COVERAGE.md](FEATURE-COVERAGE.md) | Feature coverage audit (ISC-13 to ISC-20) |
| [SETTINGS-COVERAGE.md](SETTINGS-COVERAGE.md) | Acceptance matrix for the settings pages |
| [TEXTURE-COVERAGE.md](TEXTURE-COVERAGE.md) | Every named image in the 4.93 UI art and what the build draws |
| [GRAPHICS-INVENTORY.md](GRAPHICS-INVENTORY.md) | The visual inventory of the real XMB as a check-list |
| [INGAME-FIDELITY-SWEEP.md](INGAME-FIDELITY-SWEEP.md) | In-game XMB fidelity sweep |
| [FUNCTIONS.md](FUNCTIONS.md) | Functional analysis of every category (**historical**: its status column is a 2026-10-24 assessment) |

## 4. Firmware specifications (how the console does it)

Measured specifications recovered from the firmware's own resources and from console captures, each with the
native diff.

| Document | What it is |
|---|---|
| [DIALOGS-FIRMWARE.md](DIALOGS-FIRMWARE.md) | System dialogs and message boxes |
| [ITEM-MENU-INFO-FIRMWARE.md](ITEM-MENU-INFO-FIRMWARE.md) | Item menu and information panel |
| [MEDIA-PANELS-FIRMWARE.md](MEDIA-PANELS-FIRMWARE.md) | Media playback panels and the volume display |
| [OSK-FIRMWARE.md](OSK-FIRMWARE.md) | The on-screen keyboard |
| [SETTINGS-FIRMWARE.md](SETTINGS-FIRMWARE.md) | Settings screens |
| [GEOMETRY-ROOT.md](GEOMETRY-ROOT.md) | The root category column and an open page |
| [RCO-OBJECT-MODEL.md](RCO-OBJECT-MODEL.md) | The runtime RCO object model and materials |
| [SESSION-POWER.md](SESSION-POWER.md) | Session and power behaviour |
| [PS-BUTTON.md](PS-BUTTON.md) | The PS button: console behaviour and the native equivalent |
| [PAGE-MOVE.md](PAGE-MOVE.md) | Page-move timing and easing |
| [INTERACTION-AUDIO.md](INTERACTION-AUDIO.md) | Interaction cues |
| [MENUS-XMBML.md](MENUS-XMBML.md) | Firmware-driven menus, per-user profiles and list actions |
| [XMBML-NETWORK.md](XMBML-NETWORK.md) | The XMBML-driven Network column |
| [THEMES.md](THEMES.md) | What a `.p3t` contains and the RAF verdict |
| [RAF-THEMES.md](RAF-THEMES.md) | Animated (RAF) theme support |
| [GIM-FORMATS.md](GIM-FORMATS.md) | GIM pixel formats 4/5/6 and the OSK masks |
| [VISUAL-BEHAVIOR.md](VISUAL-BEHAVIOR.md) | Shading, opacity, motion, timings, input repeat, sound |
| [VIDEO-REFERENCES.md](VIDEO-REFERENCES.md) | Measured values from public video references |
| [PLAYER-PANELS-SPEC.md](PLAYER-PANELS-SPEC.md) | Implementation-ready spec for the four player surfaces |
| [SETTINGS-SUBFLOWS-SPEC.md](SETTINGS-SUBFLOWS-SPEC.md) | Settings sub-flows, save data and copy/move/progress |
| [FIRMWARE-EXTRACT.md](FIRMWARE-EXTRACT.md) | Local PUP extraction |
| [FIRMWARE-FETCH.md](FIRMWARE-FETCH.md) | Firmware fetch research |

## 5. Rendering and layout measurements

| Document | What it is |
|---|---|
| [PIPELINE.md](PIPELINE.md) | The firmware source of each render layer and how far the port has got |
| [LAYOUT-TRACE.md](LAYOUT-TRACE.md) | Category-column and item-row constants against firmware data |
| [TEXT-RENDERER.md](TEXT-RENDERER.md) | The native text renderer and the firmware trace |
| [TEXT-RASTER.md](TEXT-RASTER.md) | The firmware text rasteriser: hinting, gamma, overlap, glow, elision |
| [FONT-MATCHING.md](FONT-MATCHING.md) | Firmware font matching |
| [PARTICLES.md](PARTICLES.md) | Particle implementation and the port groundwork |
| [RIBBON-A2.md](RIBBON-A2.md), [RIBBON-A2-PHASE.md](RIBBON-A2-PHASE.md), [RIBBON-A3-CREST.md](RIBBON-A3-CREST.md) | Ribbon remeasurement, phase recheck and crest amplitude |
| [ICON-MATERIAL-CLOCK.md](ICON-MATERIAL-CLOCK.md) | Icon material clock inputs |
| [CATEGORY-ICON-FIX.md](CATEGORY-ICON-FIX.md) | Category icons across all colours |
| [RENDER-FILL-ALPHA.md](RENDER-FILL-ALPHA.md) | Full-window mapping and operation-guide alpha |
| [ANIMATION-SMOOTHNESS.md](ANIMATION-SMOOTHNESS.md) | Animation diagnostics and measured high-refresh results |
| [ICON-ARTWORK-SCROLL-2026-11-04.md](ICON-ARTWORK-SCROLL-2026-11-04.md) | Installed artwork and folder boundaries |
| [INFO-BAR.md](INFO-BAR.md) | The info bar |

## 6. Feature notes: services and integrations

| Document | What it is |
|---|---|
| [STEAM-INTEGRATION.md](STEAM-INTEGRATION.md) | Steam and installed-PKG content |
| [STEAM-ACCOUNTS.md](STEAM-ACCOUNTS.md) | The Steam account switcher |
| [STEAM-CLIENT-FRIENDS.md](STEAM-CLIENT-FRIENDS.md) | Friends through the signed-in Steam client |
| [STEAM-DOWNLOADS.md](STEAM-DOWNLOADS.md) | Steam download state for the XMB download page |
| [STEAM-CLI.md](STEAM-CLI.md) | What the Steam client accepts headless |
| [SERVICES-BREADTH.md](SERVICES-BREADTH.md) | Account switch, launch/quit, avatars, trophies and honest rows |
| [JELLYFIN-NATIVE.md](JELLYFIN-NATIVE.md) | Native Jellyfin playback |
| [SPOTIFY.md](SPOTIFY.md) | Spotify in the Music column |
| [DLNA.md](DLNA.md) | Search for Media Servers (DLNA / UPnP AV) |
| [TAILSCALE.md](TAILSCALE.md) | Network > Tailscale |
| [HOME-SERVICES.md](HOME-SERVICES.md) | Wake on LAN and Home Assistant |
| [WEATHER.md](WEATHER.md) | Network > Weather |
| [CONTENT-REGISTRY.md](CONTENT-REGISTRY.md) | Content registry and import |
| [XMB-TOOLING.md](XMB-TOOLING.md) | In-XMB tooling: owned library, collections, store, import, media apps |
| [APP-EDITOR.md](APP-EDITOR.md) | The app registration editor |
| [WEB-APPS.md](WEB-APPS.md) | Web apps in a tracked kiosk window |
| [MAXMB-INTEGRATION.md](MAXMB-INTEGRATION.md) | MaXMB integration review |

## 7. Feature notes: shell, media and input

| Document | What it is |
|---|---|
| [OPTIONS-MENU.md](OPTIONS-MENU.md) | The Triangle options menu |
| [NOTIFICATIONS.md](NOTIFICATIONS.md) | The top-right notification composite |
| [SUBTITLES.md](SUBTITLES.md) | Sidecar subtitles and audio tracks |
| [MEDIA-CONTROLS.md](MEDIA-CONTROLS.md) | Media controls and Continue Watching |
| [VIDEO-CONTROL-PANEL.md](VIDEO-CONTROL-PANEL.md) | The video control matrix |
| [MUSIC-PLAYER.md](MUSIC-PLAYER.md) | The full-screen music player |
| [PLAYLIST-EDITOR.md](PLAYLIST-EDITOR.md) | The two-pane playlist editor |
| [PHOTO-GALLERY-DESIGN.md](PHOTO-GALLERY-DESIGN.md), [PHOTO-GALLERY.md](PHOTO-GALLERY.md), [PHOTO-GALLERY-PLAYLIST.md](PHOTO-GALLERY-PLAYLIST.md) | The hakoniwa Photo Gallery: research, library area, Playlist area |
| [INPUT.md](INPUT.md) | Keyboard, gamepads, remotes and media keys |
| [TEXT-ENTRY.md](TEXT-ENTRY.md), [ZF-TEXT-ENTRY.md](ZF-TEXT-ENTRY.md) | Shell text entry; text entry into a running title |
| [PS-INGAME-HOOK.md](PS-INGAME-HOOK.md), [INGAME-XMB-DESIGN.md](INGAME-XMB-DESIGN.md) | The PS button and the XMB over a running game |
| [DESKTOP-MODE.md](DESKTOP-MODE.md) | Desktop Mode, single instance, controller-driven desktop |
| [AUTOLOGON.md](AUTOLOGON.md) | Windows automatic sign-in status |
| [BOOT-TIME.md](BOOT-TIME.md) | Boot time |
| [SAFE-MODE.md](SAFE-MODE.md) | Safe start and diagnostics |

## 8. Verification instruments and runbooks

| Document | What it is |
|---|---|
| [VERIFICATION-HARNESS.md](VERIFICATION-HARNESS.md) | Repeatable native captures, layer comparison, named states and the gate |
| [TRANSITION-HARNESS.md](TRANSITION-HARNESS.md) | The frame-by-frame transition instrument |
| [RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md) | The HTPC evidence run (runbook) |
| [DIAGNOSE.md](DIAGNOSE.md) | Design for the diagnostic bundle, including the **log redaction rules** |

## 9. Run records and investigations

Dated evidence. Each states the tree it was taken at; none is current state.

| Document | What it is |
|---|---|
| [RELEASE-RUN-2026-11-12.md](RELEASE-RUN-2026-11-12.md) | The release evidence run on the development PC (the checklist's results) |
| [BREADTH-GATE-2026-11-11.md](BREADTH-GATE-2026-11-11.md) | Breadth gate investigation |
| [PARITY-SWEEP-2-2026-11-12.md](PARITY-SWEEP-2-2026-11-12.md) | Second render-parity sweep: players, menus, dialogs, settings, error states, window sizes; defects fixed and left |
| [D8-HEAD.md](D8-HEAD.md) | Options-structure recheck at HEAD |
| [CAPTURE-BATCH-2026-10-28.md](CAPTURE-BATCH-2026-10-28.md) | Console capture batch 1: 135 stills, 9 bursts |
| [CAPTURE-BATCH-2-2026-10-28.md](CAPTURE-BATCH-2-2026-10-28.md) | Console capture batch 2: 251 stills, 19 bursts, the error-page investigation |
| [INGAME-RENDER-FIX.md](INGAME-RENDER-FIX.md) | In-game rendering, silent quit and aspect-correct fill |
| [INGAME-RUNTIME-FIX.md](INGAME-RUNTIME-FIX.md) | In-game XMB lifecycle verification |

Superseded handoffs, pass records and phase plans (including the October 24 audit handoff and the 2026-10-25/26
media and phase continuations) are in [../archive/](../archive/README.md).

## 10. What the evidence establishes

| Evidence | Coverage | Limits |
|---|---|---|
| Sony guide index | 10 categories plus shared XMB behavior; 307 indexed topics/actions | A topic can contain many values, dialogs and conditional operations |
| Guide page collector | 111 of 179 linked pages downloaded | 68 detail pages returned HTTP 403, including a slower retry; important pages also read with web tools |
| Local XMBML | 28 files, 206 views, 788 item/query definitions, 445 tables | Definitions include dynamic/plugin/device branches; existence does not establish visibility or functionality |
| Local RCO structural scan | 122 files, 8,070 object/tree entries, 165 animation/tree entries, no structural scan errors | 113 animation action entries; raw field interpretation is incomplete |
| Main plugin layout resources | Explore: 87 object/tree entries; sysconf: 2,247; system: 59 | Main Explore/sysconf tables contain no animation entries; menu timing is not all available as RCO animation data |
| QGL icon archive | 70 numbered normal maps plus a `dif` resource | Original material shader is not ported |
| Supplied recording | 64.639 seconds, source size 906×574 | RPCS3 behavior, one theme/session and limited menu paths |
| Detailed Settings extraction | 241 frames from 40,000–48,000 ms, 33/34 ms spacing | Approximately 30 captured frames/s; no input-event timestamps |
| Native build | Version 0.2.0 compiles/links with Qt/MSVC | Source implementation is distinct from observed end-to-end behavior |

The index is broad enough to prevent whole feature families being forgotten. It
does **not** certify that every firmware state, service or timing is understood.
The documents explicitly retain unmeasured and inaccessible reference details.

### Sources and authority

- [Sony PS3 guide](https://manuals.playstation.net/document/en/ps3/current/)
  covers system software 4.80 and later and warns that service changes may be
  absent from the guide.
- [Manual index](https://manuals.playstation.net/document/en/ps3/current/manualindex.html)
  supplies the indexed category/feature surface.
- [Important service notices](https://www.playstation.com/en-us/support/important-notice/)
  supply service lifecycle qualifications; verify again before implementing an
  online integration.
- Local 4.93 XML, RCO and QRC files supply installed resource evidence. They are
  read without modifying the firmware or emulated-console data.
- RPCS3 can be driven and captured directly for reference frames (the 2026-10-27/28 capture batches
  drove it with a scripted key/grab/burst runner); the earlier 2026-10-24 route used user-supplied
  screenshots and recordings.
- [RCOMage layouts](https://github.com/kakaroto/RCOMage/blob/master/src/rcofile.h),
  [object definitions](https://github.com/kakaroto/RCOMage/blob/master/data/objattribdef-ps3.ini)
  and [animation definitions](https://github.com/kakaroto/RCOMage/blob/master/data/animattribdef-ps3.ini)
  inform structural interpretation. Unknown fields and time units remain explicit.

Implementation requirements in this audit are engineering conclusions from that
surface. They are not claims that Sony implemented the same internal architecture.
