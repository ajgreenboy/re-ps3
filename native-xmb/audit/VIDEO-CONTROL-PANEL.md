# Video control matrix (2026-11-11)

Scope: `view.cpp`, `view.h`, the timing comment in `media_controls.cpp`. This extends the player, not the photo/music panels. Firmware stays in the user's local dev_flash.

## Firmware evidence

Re-read the installed firmware with the app's diagnostics:

```
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-tree videoplayer_plugin@C:/dev/xmb/out/video-panel-tree.txt
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-text videoplayer_plugin@C:/dev/xmb/out/video-panel-text.txt
```

Tree header: version 0x130, compression 1, file 412136 bytes, tables 104412 bytes, **147 objects, zero animations**. The player is an RCO/plugin surface, not an explore-category XMBML row list. See also `MEDIA-PANELS-FIRMWARE.md` 3 and `FIDELITY-MEDIA.md` V4–V17.

`videoplayer_control_panel_page` @0xc094 -> `videoplayer_cp_group` -> `videoplayer_cp_button_group` @0xc160 contains 25 slots:

| RCO row | x cells | controls / texture suffixes (`tex_cp_`) |
|---|---|---|
| +1 | -6..4 | dummy (no image), scene_search, jump, audio, subtitle, volume, av_settings, screen_mode, change_icon, delete, display |
| 0 | -6..6 | prev, next, search_b, search_f, play, pause, stop, flash_b, flash_f, slow_b, slow_f, step_b, step |
| -1 | 0 | repeat |

Buttons @0xc1e0..0xd740 use normal/shadow/focus images; 70x70 except repeat 98x70. Thus **24 visible icons, not 25**: drawing a dummy icon would invent firmware art. `videoplayer_cp_button_name` @0xd824 has width 400, size 10; `videoplayer_cp_help` @0xd934 has no template image. Eight help textures are circle/cross/l1/l2_left/r1/r2_right/select/start (86x30).

The status audit's “14 planes” means **14 transport texture variants**, not 14 simultaneous status widgets. Tree @0x11078 has `status_icon`, `status_string_icon`, `status_text`, `status_repeat_icon`, `status_repeat_mode_icon`, initially unpublished. The 74x74 `tex_i_` variants are play/pause/stop/prev/next/jump/search_b/search_f/slow_b/slow_f/flash_b/flash_f/step/step_b; repeat is 98x74 and title-repeat/speed badges 72x36. The time page @0xecf8 contains the progress stack, Scroll, jump marker, chapter selector and two eight-cell 16x16 time groups. No chapter data is available from this backend, so no invented chapter number appears.

Labels are read at runtime through `Assets::text("videoplayer_plugin", id)`, including `msg_scene_search`, `msg_goto`, `msg_audio_change`, `msg_subtitle_change`, `msg_volume_control`, **`msg_audio_video_settings`**, `msg_viewmode`, `msg_icon_change`, `msg_del`, `msg_display_display_with_icon`, `msg_beginning_with_icon`, `msg_next_with_icon`, `msg_fast_r_with_icon`, `msg_fast_f_with_icon`, `msg_play_with_icon`, `msg_pause_with_icon`, `msg_stop`, `msg_flash_r`, `msg_flash_f`, `msg_slow_r`, `msg_slow_f`, **`msg_oneframe_r/f`**, `msg_repeat`. The trailing private-use key glyph is omitted from the focused name because the key strip draws the firmware art. Missing strings show their resource ID, not invented English. Buffering uses `msg_buffering`, `tex_loading_icon`, `tex_wait_bar`.

## Presentation and real actions

Triangle/T, START/S, and a remote Info key toggle the matrix. It shares the existing controls' visibility envelope. Arrows/d-pad navigate the sparse grid geometrically; Cross/Enter executes. Circle/Back closes the matrix first; the next Circle retains the player's existing leave/resume behavior. When closed, Left/Right still seek and Cross still toggles playback. Space, media keys, triggers, shoulder skip, repeat and Display bindings remain intact. Audio/subtitle selectors keep the existing options surface, selected at the corresponding item; they are not new decoders or fake PS3 selectors.

| control | implementation |
|---|---|
| Play / Pause | idempotent calls to existing `togglePlayback`; Play closes the matrix, leaving the transient strip |
| Stop | existing `back`, ending playback and remembering the resume point |
| Beginning / Next | existing `seekMedia(-position)` returns to zero / `skipMedia(1)` advances the queue, not PS3 chapters; previous-item skip remains on L1 / `[` |
| Fast Reverse/Forward, Instant Replay/Advance | existing bounded -/+10000 ms `seekMedia`; focused note explicitly says 10-second seek, **not continuous speed scanning** |
| Audio / Subtitle | existing `openMediaOption("audio"/"subtitle")` |
| Volume | existing Windows endpoint HUD; no fabricated per-video audio gain |
| Display / Repeat | existing `toggleMediaDisplay` / `cycleMediaRepeat` |

Status shows actual play/pause, repeat/title-repeat, and real panel skip/jump/instant-seek events. No search-speed, slow-motion or frame-step state is fabricated. Stop leaves the player, so there is no persistent stop overlay. A source that is not seekable and an absent Windows endpoint refuse the corresponding action.

Unavailable controls are dimmed; their focus remains reachable and shows the specific reason. Activation classifies the refusal as `Action::Unavailable` and logs the same reason:

- scene search: no scene-thumbnail/search implementation;
- Go To: no time-entry/chapter selector (relative seeking still works);
- AV Settings: no PS3 AV output backend;
- Screen Mode: video only has aspect-preserving letterboxing;
- Change Icon: no PS3 video-icon database;
- Delete: media library exposes no deletion;
- Slow Reverse/Forward: no implemented reverse/rate transport;
- Frame Reverse/Advance: Qt seeking cannot promise frame-accurate stepping.

The dummy is neither drawn nor focusable. This change never deletes a media file.

## Fits, bounds, and limitations

- Timing **fit**, not traced: installed RCO has zero animations. Preserve native full opacity through 3000 ms, 0.5 at 3125, hidden at >=3250 (250 ms fade); activity returns controls, pause/loading pins them, resume restarts the hold. Display still pins controls. Existing boundary-probe evidence is in `MEDIA-CONTROLS.md`. Action-status hold reuses 3000 ms. No firmware timing claim is made.
- Grid pitch **fit** 100x70 px: 70x70 art has 30 px horizontal separation; 13 cells span 1270 px, inside the existing 1700 px chrome band. Center x=960; top y=650; three rows end at 860, leaving 81 px before the y=941 bar. Label y=864; reason y=897. Firmware only provides cell coordinates, not screen projection.
- Status anchor **fit** (110,530), 74x74, ends at y=604 with 6 px before the panel backing; repeat begins at x=194, leaving 10 px after the transport icon. Unsupported icons reuse the native disabled-row 0.5 alpha, not a measured firmware availability alpha.
- Backing **fit**: the video's own `tex_i_under_bar` stretched in (110,610,1700,320). No video `tex_cp_panel` exists; the music plate is deliberately not borrowed. This is not a console-verified plate rectangle.
- Text size **fit** 19 px for size-10/16px-cell labels (existing cap-height bound ~0.82x19=15.6); focused name uses an 800px screen box (RCO width 400 is not a proven screen projection). Native explanatory reasons use 17 px, and are explicitly not firmware strings.
- Expanded help uses all eight 86x30 textures, evenly distributed in 110..1810. Runtime help composition is unknown; this is a fit, not evidence that firmware displays all eight simultaneously. The legacy six captioned pairs remain when the matrix is closed.
- Known video times now occupy eight HH:MM:SS cells; legacy unknown-time handling and saturating seek helper stay unchanged. No chapter or playable/buffered range is invented; the existing progress implementation remains, rather than claiming to implement all three firmware Progress objects or a real Scroll.
- The time bar and minimal help icons now receive the inherited controls opacity explicitly (their draw helpers override QPainter opacity). Without that, those materials could survive the shared hide envelope.

## Verification

- Standalone MSVC/Qt probe (`out/video-panel/probe.cpp`, disposable): **13 assertions pass** for 3000/3125/3250 ms visibility, activity return, pause/loading-equivalent pinning, resume, backward-clock clamping, lower/upper seek clamping, extreme signed seek deltas and unknown/hour clock formatting.
- Installed-firmware label check: **24/24 visible button IDs** exist in the exported English string table. Sparse-grid graph check: **24/24 visible buttons reachable from Play**, dummy excluded.

- Scripted playback through the actual Qt backend, 1920x1080: `out/video-panel/capture.ps1` / `check.ps1`, `capture-final.log`, seven PNGs and JSON sidecars. **PASS**:

| capture | media open | paused | position / duration (ms) |
|---|---|---|---|
| matrix | yes | yes | 1344 / 20000 |
| unavailable (Scene Search) | yes | yes | 1344 / 20000 |
| seek-zero | yes | yes | 0 / 20000 |
| seek-ten | yes | yes | 10000 / 20000 |
| hidden after Play + 4 s wait | yes | no | 14634 / 20000 |
| returned on input, then paused | yes | yes | 5205 / 20000 |
| stopped | no | yes | 0 / 0 |

The captures show the actual matrix/focus/name/status, unavailable reason and eight key images; the hidden capture contains only the video picture. Logs confirm `Action::Unavailable Scene Search`, seeks 1344 -> 0 -> 10000, and actual Play, Pause, Stop dispatch. Buffering and controller hardware were not induced by this capture. The current tree starts on Game, not Users, and has six Video rows; the disposable script therefore clamps Left to Users, moves Right four times, and selects Video Library at index 4. Earlier navigation-only attempts did not reach the player and are not counted as verification.

- Required release command passed and printed **`Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe`**. Early shared-tree attempts hit another session's incomplete `settingsSubpageRows` implementation or the build script's source-change guard; no other session's source was edited or reverted to work around them.

Test media, extracted firmware tables, executables, captures, and probes remain uncommitted under `out/`. No console playback reference is available, so these tests establish functionality and firmware-material/layout provenance, not a pixel-match claim.
