# Broader phase continuation — 2026-10-26

## Scope and status

This continuation implements app-registration tooling (phase 6), video-control behavior (phase 6), and a native idle display state (phase 5). These are verified functional slices; the exact-firmware phases remain open.

- Game → Import Games and Apps opens the [native registration editor](APP-EDITOR.md). Add an executable or HTTP(S) service, select a category/artwork, and save. Registrations use stable IDs, latest-registry merge and atomic writes. Removal only changes the registration. Jellyfin is now a native service (sign-in, paged browse, direct/HLS playback); see [JELLYFIN-NATIVE.md](JELLYFIN-NATIVE.md).
- [Media controls](MEDIA-CONTROLS.md) fade after three seconds of video playback over 250 ms, return on input, and remain visible on pause/loading. Music/photo information remains visible. Seek targets clamp safely to the media duration.
- Settings → Display Settings → Screensaver offers 5, 10, 20, 30 minutes or Do Not Use, matching the [official PS3 guide](https://manuals.playstation.net/document/en/ps3/current/settings/screensaver.html). The native default is off, preserving prior app behavior. Persisted key: display/screensaverMinutes.

## Idle rendering boundary

The native implementation adds a black premultiplied overlay after the background, shader icons and UI composite. It reaches 55% opacity over 750 ms. These brightness/timing values, suppression rules and ordinary navigation on wake are native approximations pending firmware evidence. The manual establishes option values only.

Media (including paused playback), tracked launched games, desktop modal dialogs, XMB message overlays and the boot intro suppress dimming. Keyboard/controller/mouse input resets idle. The app does not change host brightness, power or firmware preferences. System Auto-Off now also avoids closing the app while a desktop modal is open. Returning from a tracked game or the app/library/theme/wallpaper dialogs starts a fresh idle interval.

The existing script runner accepts idle=<milliseconds> to set a local diagnostic idle age. It does not alter persisted preferences, host clocks or the real Auto-Off timer. Navigation clears the override. Capture sidecars include media position/state and screensaver/idle values.

## Verification

- Native Release build and Qt deployment passed in out/native-phase-continuation and the regular out/native-xmb output. The regular build smoke exited zero and selected custom:poc-service / Jellyfin POC in TV/Video Services.
- Existing named-state visual gates: 3/3 PASS, pixel-identical Game root, Store sections and Not Installed captures. These are native regression gates, not firmware fidelity scores.
- Disposable C++ component probe: 17/17 checks passed. Covers registry add/update/preserve/remove, URL/credential/NUL rejection, malformed-byte preservation, control visibility and seek/time boundaries.
- Screensaver replay: simulated 301000 ms inactivity at five-minute setting dims the entire composed frame. Mean RGB becomes 0.450688 of awake. UP wakes without moving the first row; awake/wake frames are pixel-identical (PSNR Infinity, SSIM 1).
- Actual Display Settings replay opened five choices and saved After 10 Minutes; preferences.json contains 10.
- Actual local 30-second AVI playback: controls hide while playing and reappear on pause; paused video suppresses dimming; seek reaches 0 then 10000 ms. See MEDIA-CONTROLS.md.
- Computer Use observed the editor and saved an isolated service rename; stable ID and URL survived. No user library data was changed by these probes.

Evidence is local under out/native-reference/phase-continuation (PNGs, sidecars, logs, generated fixture and disposable probe). No raw firmware, credentials, captures or test fixture media are committed.

## Focused review follow-up

Two lower-cost reviewers found and corrected draft-registration removal and XMB-message idle suppression. The editor's six additional native component checks passed. The message-overlay replay has idle/awake mean RGB ratio 1.000007 and an unchanged bottom 1600x250 crop (PSNR Infinity, SSIM 1); small UI animation differences remain elsewhere, so whole-frame identity is not claimed for this message replay.

The review rebuild hit MSVC C1902 in its debug-information server under restricted execution. A temporary _CL_=/Z7 override embedded debug information in objects and completed the verification build; source/build defaults were not changed. Local logs distinguish failed attempts from the successful rebuild.

## Resume work across phases

| Phase | Next concrete slice | Evidence needed before calling it matched |
|---|---|---|
| 3 | Original player controls/layout and remaining item/dialog materials | Saved firmware resource/layout evidence, then crop comparisons |
| 4 | Player overlay reveal/hide, pause and transition curves | Measured reference timings; current 3 s / 250 ms is provisional |
| 5 | Match idle brightness/fade/wake; login/notification/power states | Reference state captures and option-specific replay |
| 6 | Native Jellyfin browsing/auth/playback; richer game registration/artwork lifecycle | Real server flow and launch/return/error replay |
| 7 | Add meaningful named player/settings states to the existing harness | Stable deterministic fixture and approved baseline |

Do not mark broad fidelity criteria complete from these native probes. The rendering clock and existing 200/300 ms navigation animations were preserved. Steam Friends avatars/presence remain a separate later enhancement.
