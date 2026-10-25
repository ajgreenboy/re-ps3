# XMB recreation handoff — October 24, 2026

## Objective and latest user direction

Build a complete native C++ recreation of PS3 XMB using the user's original local
assets. The firmware-execution experiment is preserved for reference. The user
also requested an in-depth review of **every feature**, including shading,
animation behavior and timing, and a handoff for future phases.

Read [the audit overview](README.md), [functional analysis](FUNCTIONS.md),
[catalog](FEATURE-CATALOG.md) and [visual/timing findings](VISUAL-BEHAVIOR.md).
Do not report this checkpoint as full XMB functionality or visual parity.

## Workspace and quick start

Repository: `C:\dev\xmb`, branch `main`, private remote
[ajgreenboy/re-ps3](https://github.com/ajgreenboy/re-ps3).
Native sources: `native-xmb/`. Executable: `out/native-xmb/re-ps3-native.exe`.
Original assets: sibling `rpcs3/bin/dev_flash` by default, configurable.

From the repository root:

```powershell
& .\native-xmb\build.ps1
& .\run-native-xmb.ps1
```

Toolchain observed: Visual Studio2022 Build Tools/MSVC19.44, bundled CMake/Ninja,
Qt6.12.0 MSVC64, C++20 and OpenGL3.3. Qt Widgets, OpenGLWidgets, Multimedia and
Concurrent are required. The build script accepts `-QtRoot`; launcher accepts
`-FlashRoot`. No new SDK was installed for this checkpoint.

Close only the native executable before relinking. Preserve the RPCS3 reference
session and the user's preferences. Build/launch logs are under ignored `work/`
and `out/native-xmb/sessions/`.

The older `native-xmb/REFERENCE-2026-10-23.md` was removed (its recording-derived
values now live in `audit/VIDEO-REFERENCES.md`); the file's previous contents
remain in Git history.

## What was added since commit876f94b

| Component | Implementation | Evidence/limits |
|---|---|---|
| `Preferences` | Typed key validation, version1 JSON, atomic QSaveFile commits, legacy INI migration, preserve invalid/newer file | Compiled; source-reviewed. Complete persistence/failure workflows not observed |
| Settings menu | Theme/color/background/font, name/info, date/time display formats and IANA zones, volume/mute/key tone, media/photo options, app full screen, idle exit | Host/native subset. Many page contents differ from original pages; catalog records gaps |
| `Profiles` | Stable local UUIDs, default User, create/select, bounds/duplicate validation, atomic JSON | Compiled; no deletion/logout/account or per-user preferences |
| `MediaLibrary` | Background flat directory scan, cancellation and stale-result suppression,5,000 matching-entry bound, metadata and errors | No recursive index, watcher, persistent media database or complete workflow evidence |
| Photos | Async bounded decode, previous/next,90-degree rotation, slideshow, repeat and fit/fill | Original panel/zoom/EXIF/playlist/transform save absent |
| Media player | Volume/mute, repeat-current, playback progress/duration and seek routes | Full queue, metadata/panels, subtitle/disc capabilities incomplete |
| `InteractionAudio` | Original cursor/decide/cancel/error from system RCO; VAGp v6/PS ADPCM to cached48k stereo PCM | Device audibility and precise sync not measured; original error cue is silent |
| Renderer | Setting values, three original Latin fonts, async wallpaper, category old/new fade, original monthly RGB texture on Original color path | Original material/wave/timeline parity remains incomplete |
| Icon correction | Vertical orientation normalized only for `qgl/icontex` before image cache | Native Game screenshot visibly shows corrected downward grips |
| Reference decoder | Optional interval/start/end, source-size PNGs for precise samples, explicit truncation failure |241 frames captured for40–48s;33/34ms cadence |
| Audit tooling | Guide catalog and read-only RCO object/animation structural inventory |307 indexed topics;122 RCO files scanned; accessibility/semantic limits retained |

The native version is0.2.0. The existing firmware runtime under `xmbrt/` was not
modified by this phase. Source checkpoints `5a6b3db` and `e20fdb5` preserve its
prior investigation.

## Latest observed evidence

- `work/native-services-build.txt`: all new service/menu sources compile and link.
- `work/native-orientation-build.txt`: corrected asset ingestion and updated
  decoder compile and link. Nonfatal Qt deprecation warnings remain in the
  earlier services build; missing optional Vulkan headers do not block OpenGL.
- Session `20261024-002645-174` launched the corrected native app.
- The preceding service session `20261024-002358-819` logged52 decoded images,
  zero missing, the three original font families and four original decoded cues.
- Steady service-session logs were around62.5 render-loop FPS. Do not call that
  measured display pacing or a ten-minute interactive stability result.
- Computer Use observed the corrected Game category; screenshot09 is the new
  orientation reference. Earlier screenshots05–08 preserve intermediate layouts
  and contain the previous icon orientation.
- Prior Theme → Color → Back observation establishes one return-history flow,
  not all current settings/media/profile workflows.
- No formal test suite was added or run. The user authorized build/run and visual
  observation. Do not invent test results.

## Local data and migration

`out/native-xmb/data/preferences.json` stores versioned shared application
preferences. Legacy color/library paths migrate from `settings.ini`, which is
preserved for window geometry. The user's Green theme must remain selected.

`profiles.json` stores native local names/UUIDs and active profile. Preferences
are shared across profiles in this phase. No PSN identity, password, console ID
or authentication token is copied. A local profile must not imply a signed-in
Sony account.

The preferences service writes candidate values atomically and updates memory
after commit. Invalid/newer existing files disable writes and are preserved.
Wallpaper selection stores a host path; source images remain unchanged.
Power-save settings close the native app only and default to Off.

## Input map

| Input | Action |
|---|---|
| Arrows / XInput D-pad | Navigate; photo Left/Right changes photo, Up/Down rotates |
| Enter / XInput A | Activate; during media toggles pause/slideshow |
| Backspace or Escape / XInput B | Back |
| Space | Pause/resume media or slideshow |
| Page Up / Down | Move ten entries |
| F5 | Refresh current media directory |
| F11 | Toggle native app full screen through persisted preference |

XInput only responds while the native window is active and a modal is absent.
Triangle panels and PS-button multitasking overlay are still missing. Keyboard
and controller repeat schedules are not yet normalized.

## Reference and research outputs

User recording: `Recording 2026-10-23 193556.mp4`,64.639s,906×574. SHA-256:
`44ea993ba0b03b44b5e215324f3cdd5dc7abd16c6bcef2dd3ea684026e5444b6`.

Ignored local evidence:

- `out/native-reference/recording-193556/`: coarse200ms frames/sheets.
- `out/native-reference/timing-40000-48000/`: precise241-frame PNG sequence,
  manifest and contact sheets. Every frame interval is33 or34ms.
- `out/native-reference/guide-audit/`: cached primary HTML, headings/tables and
  collector manifest.68 of179 detail fetches returned403 after retry.
- `out/native-reference/rco-behavior/resources.json`:122 resource structural
  inventories,8,070 object/tree entries,165 animation/tree entries and113 actions.
- `out/native-xmb/icon-sheet.png`: original map inspection sheet; regenerate for
  current orientation because the initial sheet preceded the correction.

Reproduction tools:

```powershell
bun work/xmb-guide-audit.ts
bun work/write-xmb-audit.ts
bun work/rco-behavior-audit.ts <local-dev_flash>
bun work/qrc-inspect.ts <local-qrc> [name-filter]
& .\out\native-xmb\xmb-reference-frames.exe <recording.mp4> <output-folder> 0 40000 48000
```

The RCO behavior tool needs the primary RCOMage PS3 object/animation definition
files cached in `out/native-reference/`; see its documented source URLs in the
audit overview. Do not mistake a raw duration float for a verified time unit.

## Highest-priority findings

1. **Shell state is broader than a page stack.** Implement context panels,
   player panels, PS overlay and concurrent foreground/background activities.
2. **Generic settings pages are not original feature parity.** Build exact
   original page schemas and bind each value to a real capability. Keep native
   extras distinguishable from reproduced original options.
3. **Presentation needs independent timelines.** Recorded entry appears roughly
  270–330ms and Back70–130ms in selected examples; estimates are frame-bounded.
   Current uniform exponential easing is not a verified match.
4. **Original normals are now upright; original lighting is still absent.** Port
   material/reflection/refraction behavior rather than altering face opacity.
5. **Main Explore/sysconf timing is not in their RCO animation tables.** Runtime
   analysis and further recordings are needed for the menu engine.
6. **Media needs databases and owned queues.** A flat directory and one player
   are insufficient for grouping, playlists, background playback and content operations.
7. **Online availability is feature-specific.** Use current official notices;
   never fabricate sign-in, trophies, commerce, messages or server responses.

## Next implementation phases

### Phase A — audited shell and timeline foundation

Introduce typed presentation metadata instead of texture-name sizing heuristics;
separate action/capability/state from panels; add original Triangle/context and
player-panel behavior, PS overlay, busy/notification queues and exact input-repeat
state. Fit independent entry/exit/scroll/focus curves against timestamped footage.

### Phase B — original presentation resources

Resolve all remaining category/item artwork, original normal-map lighting,
monthly/day-night colors, fonts/text shadows, wave simulation/material and
particle behavior. Keep raw/fitted/observed values distinguished. Capture each
state in the audit matrix rather than declaring fidelity from the Game screen.

### Phase C — local functional completeness

User-scoped storage/session lifecycle; original settings schemas and transactions;
async media database, metadata/artwork, playlists/queue, complete controls,
storage notifications and safe copy/delete/trim/backup operations. Expand game,
save, trophy and emulator launch/return services using actual local data.

### Phase D — network and external capabilities

Browser, download manager, local media-server discovery and remote-play session
model. Define authenticated online adapters only for available legitimate
capabilities, with accurate unavailable/failed states and current service notices.

### Phase E — evidence and sustained use

Measure presentation frame intervals, CPU/GPU/resource growth, cancellation and
long interactive sessions. Record full workflows for every catalog leaf and its
conditional variants. Unchecked project criteria remain open until observed.

## Operational constraints

- Use Bun/TypeScript for scripting and native C++ for application components.
- Do not launch nested Codex subprocesses. The helper agent reached its usage
  limit after writing its component files; continue directly if needed.
- Preserve other contributors' changes; inspect Git status before editing.
- No new formal test suite unless the user asks.
- Never upload firmware/raw extracted assets, console identities, recordings or
  private runtime state. Source/docs and approved UI screenshots can be committed.
- Computer Use blocks RPCS3. Do not bypass it using another capture/input route.
- Physical Escape can stop Computer Use; prefer Backspace for native Back.

## Definition of completion

The catalog, functional variants and VIS behavior rows are the coverage map.
The native project ISA remains the completion checklist. Build success, original
assets and a smooth root menu establish a foundation, not a finished recreation.
