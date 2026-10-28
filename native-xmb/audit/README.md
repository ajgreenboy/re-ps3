# XMB functional and presentation audit

Date: October 24, 2026. Target: the user's local PS3 firmware 4.93 and supplied
RPCS3 VSH recording. Project goal: a complete native C++ recreation.

## Read this set

Current handoff (2026-10-28): [HANDOFF-2026-10-28.md](../HANDOFF-2026-10-28.md) - the tree at `9545047`,
what ships after the firmware-recovery week, the four gate instruments with their last recorded numbers,
and where the work goes next. The plan of record is [PHASES-2026-10-28.md](../PHASES-2026-10-28.md)
(phases 8-16, with the 88-item completion ledger: 15 done / 44 partly / 28 not started / 1 obsolete);
ownership and write fences are in [WORKSPACES.md](../WORKSPACES.md). The previous handoff,
[HANDOFF-2026-10-27.md](../HANDOFF-2026-10-27.md), is a dated snapshot of `0c8faee` and still carries the
operational reference (build/run, config migration, crash reporting, release checklist, ground rules).

Fidelity instrument and register (2026-10-27): [PARITY-SCORECARD.md](PARITY-SCORECARD.md) - the
three-tier method, the 25-state / 600-cell corpus and the first scorecard (0/25 states at A+B+C; Tier A
22, Tier B 0, Tier C 9) - and [FIRMWARE-PARITY-GAPS.md](FIRMWARE-PARITY-GAPS.md), re-scored in its
section 1.1 with section 2.6 listing every surface that moved from free-hand to firmware-derived.

Firmware recoveries (2026-10-27, all with measured specs):
[dialogs and message boxes](DIALOGS-FIRMWARE.md) (`e4e3c23`, implemented in `e171263`),
[item menu and information panel](ITEM-MENU-INFO-FIRMWARE.md) (`969c9ac`),
[media playback panels and the volume display](MEDIA-PANELS-FIRMWARE.md) (`79f377d`),
[the on-screen keyboard](OSK-FIRMWARE.md) (`68134a6`),
[the settings pages](SETTINGS-FIRMWARE.md) (`4bbff6e`),
[root and page geometry](GEOMETRY-ROOT.md) (`4bbff6e`),
[the runtime RCO object model and materials](RCO-OBJECT-MODEL.md) (`6096dbf`) and
[session and power behaviour](SESSION-POWER.md) (`067d6f0`).

Firmware capture batch (2026-10-27/28): [capture batch 2026-10-28](CAPTURE-BATCH-2026-10-28.md) - one
RPCS3 session of the real 4.93 XMB at 1920x1080 (135 stills, 9 timestamped bursts), with the session and
input-config hash record, the wish-item coverage table (frame paths per audit row), the headline
measurements, what could not be captured and why, and the `xRegistry.sys` caveat. It is the evidence
behind the dated correction sections added to [DIALOGS](DIALOGS-FIRMWARE.md),
[SETTINGS](SETTINGS-FIRMWARE.md), [ITEM-MENU/INFO](ITEM-MENU-INFO-FIRMWARE.md), [OSK](OSK-FIRMWARE.md),
[GEOMETRY-ROOT](GEOMETRY-ROOT.md) and [PAGE-MOVE](PAGE-MOVE.md).

Firmware capture batch 2 (2026-10-28): [capture batch 2](CAPTURE-BATCH-2-2026-10-28.md) - a second targeted
RPCS3 batch, one session in two boots (251 stills, 22,149 burst frames in 19 bursts; 1:1 crop bursts at
130.2-234.9 fps; clock scale 100 %, so no timing rescale), aimed at what a settled frame cannot show. It
carries the rig-restoration hashes (input config `e624da0d...ceee`, `config.yml` untouched, `xRegistry.sys`
restored byte-for-byte), the coverage ledger (closed / partial / open with the frame for each), the depth-2
page numbers, the options-menu and choice-band timings, the BG1-at-rest negative result (a focused settings
row has no filled rectangle), the Theme/Color/Background/Font contents, the Video/System page tails, and the
error-surface investigation - one real firmware error page, fully measured, and an explicit account of why a
numeric error code could not be produced on this console. It is the evidence behind the dated correction
sections added to [DIALOGS](DIALOGS-FIRMWARE.md) (section 9), [SETTINGS](SETTINGS-FIRMWARE.md) (section 12),
[ITEM-MENU/INFO](ITEM-MENU-INFO-FIRMWARE.md) (part G), [GEOMETRY-ROOT](GEOMETRY-ROOT.md) (section 10) and
[PAGE-MOVE](PAGE-MOVE.md) (dated note 2).

Latest Friends continuation (2026-10-26): [signed-in Steam client connection](STEAM-CLIENT-FRIENDS.md), including the verified live list and external SDK build input.

Latest continuations (2026-10-25): [Steam and installed-PKG content](STEAM-INTEGRATION.md),
[import registry](CONTENT-REGISTRY.md), [all-theme icon correction](CATEGORY-ICON-FIX.md),
[info bar](INFO-BAR.md) and [animation smoothness](ANIMATION-SMOOTHNESS.md).

Latest continuations (2026-10-26/27): [notifications](NOTIFICATIONS.md), [Triangle options
menu](OPTIONS-MENU.md), [sidecar subtitles and the player panel](SUBTITLES.md), [Continue
Watching](MEDIA-CONTROLS.md), [volume HUD and the couch volume path](HTPC-IDEAS.md), [Download
Management](STEAM-DOWNLOADS.md), [the XMBML-driven Network column](XMBML-NETWORK.md), [Jellyfin
sign-in inside the XMB](JELLYFIN-NATIVE.md) and [the PS button while a game runs](PS-INGAME-HOOK.md).
This index itself is the October 24 set; per-feature status is in [FEATURE-CATALOG.md](FEATURE-CATALOG.md).

1. [Functional analysis](FUNCTIONS.md): behavior, data ownership, state variants
   and implementation requirements across every category. **Historical** since 2026-10-28: its
   "current native gap" column is a 2026-10-24 assessment, kept for the requirement families it
   enumerates, not for status.
2. [Feature catalog](FEATURE-CATALOG.md): 307 indexed guide topics/actions,
   including 277 leaf entries, with links and native coverage status.
3. [Visual and behavioral analysis](VISUAL-BEHAVIOR.md): shading, opacity, icon
   orientation, motion, timings, input repeat, sound and measurement gaps.
4. [Handoff](HANDOFF.md): build/run commands, current source state, evidence,
   known problems and implementation order for the next session.
5. `feature-catalog.json`: machine-readable companion to the catalog.

## What the evidence establishes

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

## Sources and authority

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
- The user supplied the screenshot and recording. Computer Use blocks RPCS3;
  analysis uses those supplied files, without another capture/control route.
  Dated note (2026-10-27/28): the [capture batch](CAPTURE-BATCH-2026-10-28.md) drove RPCS3
  directly with a scripted key/grab/burst runner, so that sentence describes the 2026-10-24 route.
- [RCOMage layouts](https://github.com/kakaroto/RCOMage/blob/master/src/rcofile.h),
  [object definitions](https://github.com/kakaroto/RCOMage/blob/master/data/objattribdef-ps3.ini)
  and [animation definitions](https://github.com/kakaroto/RCOMage/blob/master/data/animattribdef-ps3.ini)
  inform structural interpretation. Unknown fields and time units remain explicit.

Implementation requirements in this audit are engineering conclusions from that
surface. They are not claims that Sony implemented the same internal architecture.
