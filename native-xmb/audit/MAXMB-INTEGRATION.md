# MaXMB integration and current handoff review

Updated 2026-10-25. Active native plan: ../HANDOFF-PHASES.md.

## Repository checkpoint reviewed

The checkout was clean at `e4286d0` when this review started, three commits ahead of origin/main. The latest
three commits add deterministic animation time, PSNR/SSIM comparison and `work/ab-shot.ts` capture/scoring.
That clean snapshot did not prove DeepSeek was idle: `src/view.cpp` changed during this review and was left alone.
No other contributor's staged changes are included in the audio integration.

Latest documented baseline: 18.20 dB / .8388 SSIM for a settled root frame, category row 20.13 / .9022,
top background 19.03 / .9248. These are the previous session's baseline, not measurements rerun by this review.
The latest handoff records the brightness mismatch as an earlier clock mismatch, while the remaining fitted
day curve is a separate issue. The current background edits belong to the other active session.

Reviewed handoffs: root HANDOFF.md, AGENT-HANDOFF.md, docs/README.md and its Phase 4/5/6 handoffs,
docs/HANDOFF-TEMPLATE.md, native-xmb/HANDOFF.md, native-xmb/audit/HANDOFF.md and HANDOFF-PHASES.md.
Older files preserve historical evidence but contain obsolete active-state descriptions, especially the
procedural ribbon and unported category material. Their entry links now point to the current phase document.

## MaXMB source and reuse

Source: https://github.com/MaxiBono/MaXMB, inspected local commit
`a746f77906d11390f6a3e12e4f2310a4c45fd399`. MIT license and attribution are retained in THIRD-PARTY.md.
Its implementation is a reference for porting behavior into native C++; its observations alone do not prove
PS3 firmware fidelity.

| Component | Current native state |
|---|---|
| DualShock/DualSense HID layouts and connection handling | Already ported to pad.cpp; physical-controller evidence remains open |
| RPCS3 installed-title launch selection | Already ported to shell.cpp |
| P3T tree/images/sounds and icon-role mapping | Reader and image/icon application already ported; reader ancestry credited to DeckyXMB |
| Focus glow breathing / idle label fade | Already applied from MaXMB observations; still needs reference confirmation |
| Theme interaction sounds | Implemented in this continuation, described below |
| Dynamic P3T RAF/VSMX scenes | Still open; MaXMB's scene/render/VSMX modules are useful next sources |
| Media queues, music playlists and photo zoom | Still open; adapt to the native media service and original controls |
| Local fabricated PSN/friend state | Excluded from this project's online behavior; no fabricated Sony session |

The current ribbon uses the traced firmware path. MaXMB/linkev's reconstruction must not replace that verified
work. The sparkles currently use linkev's MIT implementation and are explicitly not a firmware particle port.

## Theme sound implementation

MaXMB `src/js/xmb.js` `_soundFiles` / `_applyThemeSounds` and `src/js/audio.js` `setThemeSounds` define five
PS3-theme sound roles. Native maps the same IDs:

| P3T id | Native action | Missing entry |
|---|---|---|
| se_cursor | Move | Original cursor cue |
| se_decide | Confirm | Original decide cue |
| se_cancel | Back | Original cancel cue |
| se_optionmenu | Open a setting's options | Silent |
| se_system_ok | Accept a message | Original decide cue |

`P3t::sounds()` already decodes VAG to WAV. InteractionAudio now validates PCM WAV chunk bounds, format,
channel count, duration and size; converts accepted mono/stereo PCM16 to cached 48 kHz stereo; and uses the
theme clip ahead of the system clip for each role. Unknown, absent or rejected entries preserve fallback.
Original theme selection or a failed theme load clears all previous overrides and stops any playing old cue.
Mute, key-tone enable, volume and cursor rate limiting still apply. Error sounds retain the system cue.

This adds sound behavior to existing settings/dialog actions. It does not implement the full Triangle menu,
sound-effect packs or theme startup music. Runtime diagnostics can set `XMB_AUDIO_TRACE=1` alongside XMB_LOG
to record the selected cue source; no decoded theme files are written by normal theme selection.

## Validation and next steps

Isolated Release build: out/native-audio, log out/native-reference/theme-audio-build.log. Existing Qt/MSVC
warnings remain; no new formal test suite was added. Firmware-derived diagnostic files stay under ignored out/,
never in Git.

Local playback run exited zero. A diagnostic sound-only P3T reused one original local firmware cue for all
five roles; logs confirmed theme selection for Move/Confirm/Back/Options/Accept, system Error fallback, then
system Move/Back after choosing Original. A second run rejected a cursor cue with an invalid sample rate,
kept the other four overrides and played the system cursor cue. It also exited zero. These are source-routing
and device-start observations, not speaker-audibility or theme-fidelity measurements. The firmware's bundled
Air Paint theme contains no sound entries, so a diagnostic container exercised the sound path.
Logs: out/native-reference/theme-audio/playback.log and rejected-cursor.log.

While reviewing, the other session committed background work as `7ef277a`; it is preserved separately.

Next useful MaXMB adaptations: dynamic theme scene evaluation, owned media queues/playlists, and photo zoom.
Agree on file ownership with the other active session before changing View or the main render pipeline.
