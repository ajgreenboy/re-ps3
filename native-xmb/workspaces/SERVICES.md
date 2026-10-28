# Workspace handoff: services

**Mission**: own every module that talks to the outside world or the host machine - Steam, Jellyfin, local media,
subtitles, interaction audio, power/display, controllers and the PS watcher. The rule here is stricter than
elsewhere: **never fabricate a service that does not exist**, and every capability must state whether it is
verified or unproven.

## Owns (write)

`src/steam_accounts.*`, `steam_artwork.*`, `steam_client_helper.cpp`, `steam_collections.*`, `steam_downloads.*`,
`steam_family.*`, `steam_friends.*`, `steam_library.*`, `steam_overrides.*`, `steam_owned.*`, `steam_store.*`,
`src/jellyfin_client.*`, `jellyfin_shell.cpp`, `jellyfin_stream.*`, `src/subtitles.*`,
`src/media_library.*`, `src/interaction_audio.*`, `src/host.*`, `src/pad.*`, `src/ps_watcher.*`,
`src/playback_queue.*`.

## Must not edit

`shell.*` (core), `view.*` (ui), icons/text/scene modules, `main.cpp`, `CMakeLists.txt`, `work/**`. Service shell
wiring comes back as hunks in your report - the existing audits are written that way and should stay that way.

## Boundaries

- Each service keeps its own header public surface (`SteamOwned`, `SteamDownload`, `SteamSwitchPrompt`,
  `JellyfinClient`, `subtitles::scan`, `host::*`, `pad::*`). You own those headers.
- The shell consumes signals/values; if a new signal or accessor is needed, say so in the report.
- `+command`/CLI/network behaviour: no network write without a bounded, cancellable request; never log
  credentials, tokens or the Steam Web API key.

## Build, run and verify

```powershell
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\ws-services
& C:\dev\out\ws-services\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <scratch>
```

Diagnostic CLIs that already exist and are the fastest proof path:

```powershell
re-ps3-native.exe --steam-report <json>                 # installed titles
re-ps3-native.exe --steam-downloads-report <json>       # Steam's own queue
re-ps3-native.exe --steam-accounts-report <json>        # remembered accounts + switch plan
re-ps3-native.exe --steam-friends-report <json>         # local client helper / web fallback
re-ps3-native.exe --steam-library-report <json>         # collections + handoffs
re-ps3-native.exe --pad-report <json>                   # attached pad, transport, battery
re-ps3-native.exe --pad-check <json>                    # synthetic report parse
```

Traces: `XMB_MEDIA_TRACE`, `XMB_SUBTITLE_LOG`, `XMB_AUDIO_LOG`, `XMB_NOTIFY_LOG`, `XMB_PARTICLE_LOG`,
`XMB_STEAM_DOWNLOADS_LOG`, `XMB_PS_LOG`.

For Jellyfin, the local stub pattern (a PowerShell server answering auth/views/items with `UserData`,
`PlaybackInfo`, Range-served media, sessions, images) is documented in
[../audit/JELLYFIN-NATIVE.md](../audit/JELLYFIN-NATIVE.md) and was used for the phase 1 evidence; prefer it over a
real account, and say plainly when only a stub was used.

## Verification: what green means here

- The layer gate must be unchanged: `bun work\ab-layers.ts --capture --exe ...` PASS 6/6 (services rarely change
  pixels; when they do, see "gotchas").
- A service claim needs one of: a report JSON, a scripted run with sidecar fields, or a log line from a real
  trace. "It should work" is not a result.
- Network-facing features must state the failure modes they handle (401, redirect, timeout, oversized body).

## Current state (2026-10-27)

Landed:

- **Steam**: installed titles, owned library (282 owned / 10 installed here), 13 collections, family membership
  (6 members, `family-shared.json` interim), store browse with sections, `steam://install/<id>` handoff, tracked
  launch through Steam's registry flags, `+app_stop` for quit, download queue page (real item "skate."
  update-pending), account list + two-step switch prompt with a confirmation state machine, friend avatars
  (profile XML -> allow-listed CDN -> 64x64 cache), local-client friends (46 real friends).
- **Jellyfin**: native sign-in page (masked OSK), library/folder pages with paging, direct play + negotiated HLS
  through a loopback gateway, playback reporting, resume ("Resume at", seek guarded, Watched on completion),
  posters, search page, server stream tracks listed with honest disabling, opt-in DPAPI remembered sign-in.
- **Media**: local audio/video/photo libraries, playback queue, subtitles (sidecar SRT/WebVTT parser + cue
  rendering + panel), resume records with Continue Watching, media metadata from the player.
- **Audio**: nine original system cues decoded from `system_plugin.rco` (there is no `snd_*.rco`), corrected
  Accept/Options/Error mapping, `snd_error` proven a deliberate silent stub, same-cue retrigger guard.
- **Host/power**: real Windows master volume get/set/mute, display-awake hold, **Auto-Off goes dark** (display-off
  request + read-back; the XMB stays alive), kiosk flags, DualSense/DS4 battery+transport parse.
- **PS watcher**: focus-independent HID PS button (tap = indicator, hold = system menu), with the in-game hook
  verified against a stand-in title while the XMB was minimised.

Open / unproven (state these, do not paper over):

- No real Steam title launch/stop was ever exercised (would launch a game); the `+app_stop` proof is
  command-shape + registry observation + a console-log line from a signed-in client.
- The Steam client is signed out on this machine (2026-10-27): in that state `+command` writes nothing to
  `console_log`, so the log is a **secondary** confirmation only - the registry stays primary.
- Steam account switching has no CLI/URL path: the plan is `-shutdown` then a bare `steam.exe` handoff, and the
  XMB never reports a sign-in. SteamCMD is not a backend for a logged-in client; do not bundle it.
- PSN/trophies/browser/Remote Play/disc: **no backend** (trophy state lives in encrypted `TROPUSR.DAT`; the local
  RPCS3 `dev_hdd0` has no trophy data). Rows must stay `Unavailable` with the exact wording already written.
- Jellyfin: a real Jellyfin 12 server run is still pending (stub only this pass); stream subtitle rendering and
  stream audio switching are as good as the backend's `audioTracks()`; quit-time stop reports are best effort.
- Pad paths are hardware-verified only for the DualSense (USB); DS3/DS4 parsing is synthetic.

## Evidence index

[../audit/STEAM-INTEGRATION.md](../audit/STEAM-INTEGRATION.md), [../audit/STEAM-CLI.md](../audit/STEAM-CLI.md),
[../audit/STEAM-DOWNLOADS.md](../audit/STEAM-DOWNLOADS.md), [../audit/STEAM-ACCOUNTS.md](../audit/STEAM-ACCOUNTS.md),
[../audit/STEAM-CLIENT-FRIENDS.md](../audit/STEAM-CLIENT-FRIENDS.md), [../audit/SERVICES-BREADTH.md](../audit/SERVICES-BREADTH.md),
[../audit/JELLYFIN-NATIVE.md](../audit/JELLYFIN-NATIVE.md), [../audit/SUBTITLES.md](../audit/SUBTITLES.md),
[../audit/MEDIA-CONTROLS.md](../audit/MEDIA-CONTROLS.md), [../audit/INTERACTION-AUDIO.md](../audit/INTERACTION-AUDIO.md),
[../audit/SESSION-POWER.md](../audit/SESSION-POWER.md), [../audit/PS-BUTTON.md](../audit/PS-BUTTON.md),
[../audit/PS-INGAME-HOOK.md](../audit/PS-INGAME-HOOK.md), [../audit/HTPC-IDEAS.md](../audit/HTPC-IDEAS.md).

## Gotchas already paid for

- `pad.cpp` must keep `std::min/max` parenthesised (or add `NOMINMAX`) - `windows.h` breaks it.
- Do not `Stop-Process re-ps3-native` globally: another agent may be measuring a long run; kill by pid.
- The Steam Web API key lives outside the repo (`%APPDATA%\re-ps3-native\steam_api_key`) and a pre-push hook
  blocks any push containing it. Never echo it, never log it.
- Jellyfin tokens must never appear in URLs, logs or sidecars (the loopback gateway exists for that reason).
- A service change that adds UI rows can invalidate a harness state assertion (row counts, catalogue ids) - tell
  tooling.

## First tasks

1. **Steam account-switch UI hunk** (core applies it) and the **`+app_stop` log-evidence hunk**; then a real
   launch/quit test only if the user agrees to launch a game.
2. Friends avatars shell hunk (view already draws `Entry::artwork` - no ui change needed) and cache tests.
3. Jellyfin: backdrops draw hunk (ui), then a disposable real-server re-run for 12.4 (resume, posters, search,
   tracks, sign-in persistence) replacing the stub-only evidence.
4. Subtitles: embedded stream subtitles for Jellyfin/HLS (currently sidecar-only), and an audio-track switch
   verified against a multi-track fixture (needs a generated file or ffmpeg).
5. Honest `Unavailable` wording hunks (network rows, PSN, trophy, store, My Channels, disc) - already written,
   needs the core application.
6. P8 audio: wire the remaining cue call sites (`snd_trophy` needs a trophy event, `snd_system_ok` after a
   successful settings write), and measure the mix against the firmware's single playback-volume constant.

## Report format

Branch/commit, build dir, the exact CLI/report or scripted run with its output (paste the lines), what is
verified vs unproven, gate result, files touched, and the hunks other workspaces must apply.
