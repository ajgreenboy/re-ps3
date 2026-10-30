# FIDELITY-MEDIA - media columns, players, volume HUD, on-screen keyboard, media dialogs

Audit pass, 2026-10-29. Read-only: no source, tool, budget, threshold or reference was edited; the
only files written are this document and throw-away capture/scratch frames under
`C:\dev\out\fid-media\`.

This domain covers the Video / Music / Photo / TV columns and their players, the volume HUD, the
on-screen keyboard and the media-related dialogs. Sibling passes own the columns' *icons* as
material (ICON-GAP-AUDIT), the settings pages, the dialogs' generic chrome (DIALOGS-FIRMWARE), the
text raster (TEXT-RENDERER) and the subtitles (SUBTITLES); where an element sits on that boundary
this document says so rather than re-litigating it.

Build measured: `C:\dev\xmb\out\native-xmb\re-ps3-native.exe`, 4,257,280 B,
2026-10-29 18:33:37. **The source tree was being edited by another pass while this audit ran**
(`view.cpp` grew from 3,728 to 3,815 lines between two reads of it). Every line number below is
the line at the time of reading and is given with the function name so it survives drift; every
measured number comes from the built exe above, not from the source.

---

## Counts

| verdict | count |
|---|---:|
| elements audited | **74** |
| **match** (within tolerance on every measured property) | **25** |
| **differ** (present, measurable delta) | **27** |
| **absent** (firmware defines it, native draws nothing) | **19** |
| **no-reference** (no console frame and no firmware-side declaration to judge against) | **3** |
| **stale claims found in the existing evidence** | **11** |

Per surface (each row sums to its element count):

| surface | elements | match | differ | absent | no-reference |
|---|---:|---:|---:|---:|---:|
| Video column + video player | 17 | 1 | 8 | 6 | 2 |
| Music column + player | 12 | 5 | 5 | 2 | 0 |
| Photo column + viewer | 8 | 1 | 3 | 4 | 0 |
| TV column | 2 | 0 | 2 | 0 | 0 |
| Volume HUD | 6 | 3 | 3 | 0 | 0 |
| On-screen keyboard | 24 | 15 | 5 | 4 | 0 |
| Media dialogs | 5 | 0 | 1 | 3 | 1 |

---

## 0. Method and evidence classes

| tag | meaning |
|---|---|
| **M** | measured this pass on a 1920x1080 frame (mine under `C:\dev\out\fid-media\`, or a console frame named in place) |
| **FC** | firmware container fact, re-verified this pass by exporting the texture with the current exe (`--export-image`, sizes below) or by reading the object table recorded in the cited audit section |
| **FD/FA** | firmware data / artifact constant, as cited |
| **NS** | native source (read, not run); function name + line at time of reading |
| **NR** | no console reference exists (see section 11) |

Capture recipe (one token per 300 ms, pinned clock, 1920x1080 client area, `--render-clock 20` so
the OSK caret phase is deterministic - 20000/468 = 42, even, lit):

```
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --vsh-modules C:/dev/xmb/fw493/vsh_module
  --data-dir <seed> --render-clock 20 --ui-clock 2026-10-25T12:32:00
  --background-clock 2026-10-25T10:00:00 --focus-time 2000 --script "<tokens>"
```

Seeds used: `out/native-reference/store/jf-smoke` (OSK route via Settings > System Settings >
System Name), a `library/video` seed pointing at `out/native-subs/fixture-media` (Cue Test.mp4 +
sidecar srt), and a `library/music` seed pointing at `out/media-fixtures/music` (Session Probe.wav).

Frames measured: `osk-default`, `osk-default-2`, `osk-key-down`, `page-user-create` (console,
`out/rpcs3-allgraphics-2026-10-28/frames/`), `col-photo-r0*`, `col-music-r0*`, `col2-video-r0*`,
`page3-*` (same batch); mine: `o01`..`o08`, `a-dark`, `a-accent`, `a-palette`, `pv2`, `pv4`, `m4`,
`hud50`, `hb-move`, `hb-early`, `hb-fade`, `h50`, `v0`, `m0`.

---

## 1. Verification of the existing evidence, and what is now stale

The five documents this pass was told to verify are, with three exceptions, accurate to the current
build. The exceptions matter because they are repeated downstream.

| # | claim | where | verdict |
|---|---|---|---|
| S1 | "the boundary segment (the one at the level) is the one drawn with `_focus`" | MEDIA-PANELS-FIRMWARE 11.1 | **FALSE.** `drawVolumeHud` (≈`view.cpp:2631`, segment loop at ≈`:2690`) reads `const QImage& art=i<litCount?lit:(i==litCount-1?focus:dim);` - the `focus` branch is unreachable, because `i<litCount` already took every index `<=litCount-1`. `tex_default_lvmeter_focus` is decoded and then **never drawn**. |
| S2 | "the panel-open cue ... Not done, and why" | MEDIA-PANELS-FIRMWARE 11.7 | **STALE.** `View::toggleMusicPanel` (`view.cpp:2921`) calls `shell_.playCue(InteractionAudio::Cue::Accept)` at `view.cpp:2928`, and `Shell::playCue` exists (`shell.h`). The cue fires; 11.7's "the panel opens silently today" is no longer true. |
| S3 | "the blink is 500 ms from `OskCaret::blinkMilliseconds`" | OSK-FIRMWARE 15.8 (last bullet) | **STALE.** `osk_layout.h:119` is `int blinkMilliseconds = 468;` and `:120` is `bool blinkTraced = true;`. |
| S4 | "`osk_layout.h` carries `blinkMilliseconds = 500` and `blinkTraced = false`, so the measured 936 ms period is not wired" | FIRMWARE-PARITY-GAPS M6 and R17 (also the summary rows at :335 and :363) | **STALE**, same reason. The 936 ms period *is* wired: 468 ms half-phase. |
| S5 | "blink 500 ms (F, not traced)" in the `drawOsk` block | OSK-FIRMWARE 5.2 (line 481) and 10.2 (line 746) | **SUPERSEDED by the same document's 13.1**, which measures 495/504/498 lit and 442/431 dark. Self-flagged, still reads as current at the top of the file. |
| S6 | "the triangle is drawn whether or not the I-beam is lit (5.2)" | OSK-FIRMWARE 15.2, caret row | **FALSE now.** `drawOsk` draws the I-beam *and* the marker inside one `if(osk_.caretLit)` (`view.cpp:3621`). Measured: the marker band 707..721 exists while the I-beam is lit (`o02`) and is **empty** in the dark phase (`a-dark`, `--render-clock 20.4`). |
| S7 | "500 ms is the audit's FIT (OSK-FIRMWARE 5.2 ...)" in the paint loop | `view.cpp:833-834` comment | **STALE comment** in shipping code. `osk_layout.h:119` has been 468 since the capture batch. |
| S8 | "The cursor's 2x36 px, 500 ms on/off toggle" | TEXT-RASTER.md:427 and :463 | **STALE**: 468 ms half-phase, and the mark is a 12x27 I-beam with 12 px serifs, not 2x36. |
| S9 | "OSK plate and key faces ... `view.cpp:3060-3067`, `drawOsk` at `view.cpp:3181-3464`" | UI-ELEMENT-GAP-AUDIT D9 | **DRIFTED**: `drawOsk` is `view.cpp:3528`, `oskFace` `:3445`. Same for M1 (`:1579-1600, 2428-2469` → `:1699-1730, 2772-2819`), M4 (`:1605-1609` → `:1716-1721`), M7 (`:2290-2349` → `:2631-2693`). The verdicts are still right; the anchors are not. |
| S10 | "measured on `hud25`/`hud50`/`hud100`: lit segments 3/5/10" | MEDIA-PANELS-FIRMWARE 11.1 | **TRUE, and reproducible only when something redraws the UI layer.** Reproduced this pass: 5 lit at 50 %, 0 at mute, 20 px pitch, plate 853..1066 x 1001..1053 (`hud50` = pinned + a forced redraw; `live-hud` and `settled-hud` = live runs). Under the *pinned recipe alone* the same state leaves the band untouched (H3). |
| S11 | "the fade-in is 150 ms with 0 delay ... computed in `drawScene` from the change timestamp" | MEDIA-PANELS-FIRMWARE 11.1 / 7.3 | **TRUE, and the reason the capture recipe cannot see it.** The fade amount is *not* in the UI-layer signature (the `" vol %d %d %d "` term at ≈`view.cpp:900` carries `hud.opacity` only), so whether the HUD is ever sampled at `fadeIn > 0.03` depends on unrelated animation. Pinned captures: 0.027 at 4 ms, then nothing (5 of 6 frames show no meter). Live: full strength. Full numbers in H3. |

Unchanged and re-verified (not stale): MEDIA-PANELS 11.2 (the video chrome geometry, the
`_with_icon` private-use gaiji strip, `msg_quit_playback` for Circle), 11.3 (the music panel's
rect, 1.3 scale, 90 px button pitch), 11.5 (every remaining fit), OSK-FIRMWARE 15.2/15.6/15.7 and
GRAPHICS-INVENTORY 3, 11 and 14.

---

## 2. Video column and the video player

Console reference: `col2-video-r00..r03` (column), `page3-video-row0*`, `page2-video-item1*`,
`page-video-item1*` (empty-state pages). **No console frame shows a playing video, the control
panel, the time page or the status page** (GRAPHICS-INVENTORY 14 item 4).

| # | element | firmware (evidence) | native (evidence) | verdict |
|---|---|---|---|---|
| V1 | Video column row set | `BD Data Utility`, `Search for Media Servers`, `Video Editor & Uploader` - 3 rows (M: `col2-video-r00..r02`) | `Continue Watching`, `Video Library`, `Video Settings`, `Choose Media Folder` - 4 rows (NS `shell.cpp` category table; M: `v1`, `v2`, `pv0`) | **differ** - a different row set, not a re-skin |
| V2 | Video column row icons | plain folder / sliders+magnifier / triangle-over-dots (M: same frames) | `video_tex_album_default` for two rows, `tex_video`, `item_tex_plain_folder` (NS) | **differ** |
| V3 | Full-screen backing plane | `videoplayer_full_screen_page` has **0 children** (FC, MEDIA-PANELS 3.1); the only declared full-screen plane in the media set is the *photo* container's `tex_front_screen`, 1x1, pixel (0,0,0) (FC, exported this pass) | `p.fillRect(QRectF(0,0,1920,1080),Qt::black)` then the frame letterboxed (NS `drawScene`, `view.cpp:1699`) | **match** |
| V4 | 25-button control matrix | 25 named buttons, 13 columns x 3 rows of cells, icons 70x70 (98x70 repeat), each with `_shadow` + `_focus` (FC, MEDIA-PANELS 3.3) | **no button is drawn** (NS: no `videoplayer_cp_*` image is referenced anywhere in `src/`) | **absent** |
| V5 | Control-panel plate / `control_panel_base` analogue | `videoplayer_control_panel_page` + `videoplayer_cp_group`, geometry runtime (FC 3.2) | none | **absent** |
| V6 | Focused-button name label | `videoplayer_cp_button_name`, Text, width 400, size 10 (FC 3.2) | the media title is drawn instead at (110,984,1500x38), 27 px (NS `view.cpp:1703`) | **absent** (the label object) |
| V7 | Direct-key help strip | ONE plane `videoplayer_cp_help` + 8 key icons 86x30: circle, cross, l1, l2_left, r1, r2_right, select, start (FC 3.4) | six pairs, icons 86x30 at y 1026, slots 283.33 px from x 110, labels 19 px (NS `drawVideoHelpStrip`, `view.cpp:2772`); icons circle/cross/l2_left/r2_right/l1/r1 - `select`/`start` unused | **differ** |
| V8 | Status planes | `tex_i_play/_pause/_stop/_prev/_next/_jump/_search_b/_search_f/_slow_b/_slow_f/_flash_b/_flash_f/_step/_step_b` 74x74, badges 72x36, `tex_i_repeat` 98x74 (FC 3.4) | none | **absent** |
| V9 | Time-bar track | 3 Progress (`..._bar_base`, `..._bar_playable_time`, `..._bar`) + 1 Scroll (max 100/100) + a jump-marker plane (FC 3.5); the three Progress objects carry **no** image refs (FC, MEDIA-PANELS 11.2 fact 1) | `tex_progress_bar` 30x20 nine-sliced at 8 px + `tex_progress_bar_in` 30x20 fill, in the rect (110,941,1700,20) (NS `drawVideoTimeBar`, `view.cpp:2714`); measured ink rows 944..956 inside that 20 px band (`pv2`) | **differ** (2 arts of the 4-object stack; no Scroll) |
| V10 | Jump / seek indicator | `tex_progress_bar_jump_indi` 20x12 on `..._jump_time_bar_marker` - marks a *jump target* (FC 3.5) | drawn at the played position, because this build has no scrub state (NS + comment at `:2736`); visible at x~57 at 6 % in `pv2` | **differ** (fit: the only position the element can denote here) |
| V11 | Per-cell time readout | 8 cells of 16x16 per group (`..._playing_time_h1..c2`, `..._play_time_h1..c2`) + a 16x16 sign cell (FC 3.5) | one glyph per 16 px cell at 19 px, `MB:SS` digit source, 5 glyphs for "00:01" (NS `drawTimeCells`, `view.cpp:2754`); measured 5 play cells from x 110 and 5 duration cells ending at 1810 (`pv2`) | **differ** |
| V12 | Chapter hit area + chapter number | `videoplayer_display_chapter_button` 200x64 and a 16x16 chapter cell + ItemSpin (FC 3.5) | none | **absent** |
| V13 | Title text | `videoplayer_cp_button_name` width 400 size 10 + `videoplayer_information_string` (FC 3.2/3.6) | 27 px in (110,984,1500x38) (NS `view.cpp:1703`) | **differ** (no size/rect equivalent) |
| V14 | Panel fade / hide timing | not in the container (`videoplayer_plugin.rco` has 0 animations, FC 3.8); `videoplayer_plugin.prx` not decompiled locally | `MediaControls` 3000 ms + 250 ms (NS `media_controls.cpp:8-9`) | **no-reference** |
| V15 | In-player options panel (Triangle) | the console's surface here is the 25-button panel; no separate right-hand band exists in `videoplayer_plugin.rco` (FC) | right-hand band, heading `Playback`, 7 rows + note, drawn over the video and over the hint strip (`pv4`) | **differ** (a native-only shape) |
| V16 | Active sidecar subtitle cue | subtitle rendering is the SUBTITLES domain | drawn over the video below the chrome (`pv2`: "FIRST CUE: the player found this sidecar file.") | **no-reference** (no console frame of playback; defer to SUBTITLES) |
| V17 | Buffering / loading | `videoplayer_buffering_page`, `msg_buffering`, `tex_loading_icon` 90x90, `tex_wait_bar` 90x38 (FC 3.4) | `"Loading…"` 26 px at (110,910,1000x45) (NS `view.cpp:1708`) | **absent** (the icon/bar art is unused) |

---

## 3. Music column and the player

Console reference: `col-music-r00/r01` (column), `page3-music-row0*`, `page2-music-item0*`.
**No console frame shows the XMB music control panel, the audio player or a playing track**
(GRAPHICS-INVENTORY 14 item 4).

| # | element | firmware (evidence) | native (evidence) | verdict |
|---|---|---|---|---|
| M1 | Music column row set | `Search for Media Servers`, `Playlists` - 2 rows (M: `col-music-r00/r01`) | `Music Library`, `Music Settings`, `Choose Media Folder` - 3 rows (NS `shell.cpp`; M: `m0`) | **differ** |
| M2 | Music column row icons | sliders+magnifier / play-triangle-over-dots (M) | `music_tex_album_pic`, `tex_music`, `item_tex_plain_folder` (NS) | **differ** |
| M3 | B1 audio player's own progress page | `play_progress_page` - 8 time cells, `audioplayer_display_time_bar` Progress height 20 max 100, track number, title, artist, codec label (FC 4.1) | flat 4 px track `QRectF(110,941,1700,4)` + one `MM:SS / MM:SS` at 23 px in (1450,963,360x45), plus the ASCII sentence at 21 px and the title at 27 px (NS `view.cpp:1716-1721`, `:1703`, `:1707`) | **differ** |
| M4 | B1 14-button panel | 14 buttons at cells -3..+3, icons 70x70 (98x70 repeat/shuffle), alpha 0.8 template (FC 4.1) | none | **absent** |
| M5 | B2 panel plate | `control_panel_base` + `tex_cp_panel` 72x72 stretchable, scaled 1.3 (FC 4.2 + FD 0x270a54) | nine-sliced at a 20 px inset, plate 850.2x226.2 at (534.9,773.8) (NS `drawMusicPanel`, `view.cpp:2821`); art verified 72x72 this pass, dark centre (20,20,20) with a light rim (152) - which is exactly what `m4` shows | **match** |
| M6 | B2 status planes | 10 planes, all `alphaScale=0` until the plugin publishes one: play/prev/next/pause/stop/searchb/searchf/repeat/1repeat/shuffle, 74x74 / 98x74 / 72x36 (FC 4.2) | play or pause + repeat/1repeat/shuffle only; the five transport planes with no shell state are deliberately not drawn (NS `:2898-2906`; `m4` shows one play triangle) | **differ** |
| M7 | B2 progress bar | `progress_bar_playing_time` width 200 height 6, max 100 (FC 4.2) | 200x6 at the content box's right (NS `:2906`), art `system_plugin/tex_default_progress_base` + `_slider` (a fit, MEDIA-PANELS 11.5 #13) | **match** (geometry FC; material fitted) |
| M8 | B2 `label_playing_time` | Text, size 10, white + the standard (4,-5)/0.25 shadow (FC 4.2) | `00:03 / 00:06` at 19 px, right-aligned on the bar (NS `drawMusicPanel`, label call just above the bar rect at ≈`:2898`) | **differ** |
| M9 | B2 seven 90x60 buttons + shared focus art | 7 buttons, icons 90x60, `image2` = the shared `tex_cp_focus` 90x60 (FC 4.2) | 7 buttons at 90x60, 90 px pitch, `tex_cp_focus` under the focused one (NS ≈`:2912-2919`); measured 7 face runs at y 930 from x 645: the focused first button is **92 px** wide, then six **76 px** faces on a **90 px** pitch (`m4`); art verified 90x60 each | **match** |
| M10 | B2 fade-in | fade 150 / delay 0 (FD 0x270a00/0x270a04); move 150 duration / 100 delay (0x270a58/0x270a5c) | 150 ms fade, plate grows 1/1.3 -> 1.3, 100-unit delay dropped (NS `view.h` `musicPanelFadeMs`, `drawMusicPanel`) | **match** (the traced 100-unit delay is a listed fit, 11.5 #12) |
| M11 | B2 panel-open cue | `explore_plugin_all.c:38936` `snd_system_ok` (FA) | `Cue::Accept` (= `snd_system_ok` in `interaction_audio.h`) at `view.cpp:2928` | **match** (11.7 is stale, S2) |
| M12 | Visualiser / spectrum | `msg_visualizer_with_icon` "Visual Player " button in `audioplayer_plugin` + `soundvisualizer_plugin.rco` (7 images) exist (FC 4.1/2.2) | nothing: no visualiser button, no spectrum, no panel entry | **absent** |

---

## 4. Photo column and the viewer

Console reference: `col-photo-r00..r02` (column), `page3-photo-row0*`, `page2-photo-item1*`.
**No console frame shows the photo viewer** (GRAPHICS-INVENTORY 14 item 4). No photo fixture exists
in any local seed, so the viewer could not be captured this pass either (only `Session Probe.wav`
and video fixtures are present); the verdicts below are code + firmware, with the photo *viewer*
elements that need a frame marked as such.

| # | element | firmware (evidence) | native (evidence) | verdict |
|---|---|---|---|---|
| P1 | Photo column row set | `Search for Media Servers`, `Photo Gallery`, `Playlists` - 3 rows (M: `col-photo-r00..r02`) | `Photo Library`, `Photo Settings`, `Choose Media Folder` (NS `shell.cpp`) | **differ** |
| P2 | Front/back screen planes | `Pv_front_screen_plane` / `Pv_back_screen_plane` 1920x1080 with `tex_front_screen` / `tex_back_screen` 1x1 (FC 6.1); the front pixel is (0,0,0) (exported this pass) | `p.fillRect(QRectF(0,0,1920,1080),Qt::black)` then the image (NS `view.cpp:1699-1705`) | **match** |
| P3 | View control panel (20 buttons + label + hint plane) | `view_controlpanel_group`, 20 buttons on a -7..+7 / step-2 grid, rows y=2 and y=0, icons 70x70, `button_description_label_incpv` width 300 height 9 size 10, `direct_key_icon` (FC 6.2) | none | **absent** |
| P4 | Direct-key hint strip | 19 `tex_dki_*` icons 86x30 (FC 6.4) | one ASCII sentence at 21 px, (110,1020,1700x35): `Circle Back  Cross Slideshow/Pause  L1/R1 or +/- Zoom  Square/0 Reset  Triangle/Q/E Rotate  Arrows Pan when zoomed  [/] Photo` (NS `view.cpp:1705`) | **absent** |
| P5 | Zoom / pan / rotate states | states of the panel's own buttons; no numeric zoom anywhere in the container (FC 6.2, MEDIA-PANELS 7.4) | `PhotoViewport` zoom/pan/reset + shell rotate, keyboard `+ - 0 Q E` and pad (NS `zoomPhoto`/`panPhoto`, `view.cpp:3162-3175`) - the *states* exist, the *surface* does not | **differ** |
| P6 | Numeric zoom readout | none in firmware | `Zoom %1%` at 21 px, (110,895,1700x36) (NS `view.cpp:1710`) | **differ** (native-only text) |
| P7 | Thumbnail frames / IItem strip | 16 IItem rows, `tex_thumbnail_frame` 12x12 nine-slice, `_shadow` 18x18, `tex_thumbnail_shadow` 21x21 (FC 6.4) | none | **absent** |
| P8 | Slideshow panel + status planes | `slideshow_controlpanel_group` 11 children incl. `tex_play_status`/`pause`/`repeat_status` 74x74/98x70 (FC 6.3/6.4) | a `Pause`/`Slideshow` word inside the ASCII sentence only (NS `:1705`) | **absent** |

---

## 5. TV column

The console has **nine** categories and no TV column (GRAPHICS-INVENTORY 3: "No TV / Video Services
column exists on this console", re-confirmed on all nine anchors). The native build has **ten** and
draws TV/Video Services between Video and Game.

| # | element | console | native | verdict |
|---|---|---|---|---|
| T1 | TV / Video Services column | does not exist (`col-*` walks never leave 9 categories; `col-photo-r00` .. `col-friends-r00`) | category id `tv`, title `TV/Video Services`, `qgl/icontex:35n` (NS `shell.cpp` category table); measured: `v0` state `category=tv`, and the settled frame `hud50`/`hb-move` shows the TV icon with its label, one column left of Game | **differ** (a whole extra category) |
| T2 | TV column's item row | - | `My Channels` with `tex_video`, no page behind it (NS; measured `v0` `entry=My Channels`, `hb-move`) | **differ** (native-only row; the console's Video column does not carry it) |

There is no console reference for anything inside this column by construction - it has no
counterpart. Two of the audit's sibling documents already record the column-set difference
(GRAPHICS-INVENTORY 3, REFERENCE-GAPS 4.1); this pass confirms it is still live in the build.

---

## 6. The volume HUD

Console reference: **none.** The console's volume display was never captured (GRAPHICS-INVENTORY 14
item 4). The firmware side is a single paf `PhLevelMeter` whose segment art lives in
`system_plugin.rco` (FC, MEDIA-PANELS 5.1-5.4) - so the *material* and the *absence of any caption,
percentage, plate or border* are checkerboard facts even without a frame.

Art verified this pass by export: `tex_default_lvmeter_on` 20x40, `_off` 20x40, `_focus` 20x40,
`_bg` 48x48 - and `_on` is a green capsule on a white glow (pixel at the centre (129,154,50)), `_bg`
is a black tile (centre (0,0,0)).

| # | element | firmware | native (measured) | verdict |
|---|---|---|---|---|
| H1 | Backing plate | `tex_default_lvmeter_bg` 48x48, the widget's only backing (FC 5.2) | nine-sliced at inset 16 over the segment row inflated by 8 px; measured plate bbox **853,1002 - 1066,1052 (214x51)** vs the code's rect 852..1068 x 1000..1056 (`hud50`) | **match** |
| H2 | Ten lvmeter segments | segment art 20x40; **count not in the container** (only `unknown24 = 4`), pitch unproven (FC 5.4) | 10 cells at a 20 px pitch; measured green runs at y 1008..1048: **866..874, 886..894, 906..914, 926..934, 946..954 = 5 lit at 50 %**, 0 lit when muted (`hud50`, `hb-move`) | **match** (count is a listed fit, 11.5 #2) |
| H3 | The HUD appearing on a level change | the console shows a meter on every change; fade in 150 ms (0x270a00), delay 0 (0x270a04) (FD 5.4) | **the fade-in is not in the UI-layer signature**, so the HUD is sampled only when unrelated animation forces a rebuild inside its 3 s window. `drawVolumeHud(p,hud.opacity*fadeIn)`, `fadeIn = (now-volumeHudAt)/150`, and the `" vol %d %d %d "` signature term carries only `hud.opacity`, never `fadeIn`. Measured: under the **pinned capture recipe every media audit uses** a bare change leaves the meter band unchanged - 43.39 dB / SSIM 0.9966 against a no-HUD frame - in 5 of 6 captures (traces 0.027 at 4 ms, 0.040 at 6 ms); one extra redraw in the same state gives 12.28 dB / 0.1254; a **live** run (unpinned, early or after 12 s idle) draws it at full strength (segment centre 128,154,50 = the texture's own value) | **differ** (fragile, not always absent - but a still screen can lose it entirely) |
| H4 | Caption / percentage / border / rounded plate | none of these exist on the volume page (FC 5.3) | none: `drawVolumeHud` contains no text call at all; the old 222x58 HUD survives only in the art-missing fallback branch, which cannot trigger (`tex_default_lvmeter_*` all decode) | **match** |
| H5 | 150 ms fade-in, delay 0 | 0x270a00 = 150, 0x270a04 = 0 (FD) | implemented (`volumeMeterFadeMs = 150.` in `view.h`) but only sampled as described in H3 | **differ** |
| H6 | Mute | the volume page has no mute state (FC 5.3) | mute keeps the reported level and lights no segment (NS `:2662-2666`); measured 0 green runs while muted | **differ** (a native-only state, drawn as silence; no second material invented) |

`tex_default_lvmeter_focus` is never drawn - see S1. The focus state is a listed fit
(MEDIA-PANELS 11.5 #3), but the code does not draw *any* focus art, so the fit is unrealised.

---

## 7. The on-screen keyboard

Console reference: **four frames, all 1920x1080**, `osk-default.png`, `osk-default-2.png`,
`osk-key-down.png` (Users > Create New User > Edit) and `page-user-create.png` (the page behind).
The route is reproducible and the panel is centred in all three OSK frames (left edge 494), so
unlike OSK-FIRMWARE 13.4's burst frames these need no alignment offset.

| # | element | console (M) | native (M) | verdict |
|---|---|---|---|---|
| O1 | Dim behind the panel | 0.29-0.30 multiplier on the already-dimmed page (OSK-FIRMWARE 4.1) | `dimAlpha = 0.70` black over 1920x1080 (`osk_layout.h:153`; NS `drawOsk`) | **match** |
| O2 | Panel rect + plate nine-slice | 493,290 - 1426,789 (`osk-default` full-panel scan; 934x500) | **493,290 - 1426,789 (934x500)** exactly (measured bbox, luma>110; `o01`); plate 157,154,157 under the RCO `oskpanel_base*` nine-slice | **match** |
| O3 | Title bar, pencil glyph, title | bar (142,143,141) at (600,300); `User Name` at 27 px (ink 503,298-656,321) | (141,143,140) at (600,300); U+270E + `System Name` at 27 px | **match** (Δ1/255) |
| O4 | Entry field | 86 -> 47 vertical gradient, text origin +20 px (M: profile at x=700, 342:85 .. 390:47) | 85 -> 48 at the same x (profile 336:85 .. 390:48); 22 px text, 20 px inset, clipped, scrolls (NS) | **match** |
| O5 | I-beam caret | green (130,249,12), 2 px shaft, 12 px serifs, 27 px tall, at the pen advance (M: columns 578..579 in `osk-default`) | same colour/height/serifs; measured 2 px columns 696..697 in `o02` (at a longer string) and 701..702 in `a-accent` | **match** |
| O6 | Cursor marker (hollow triangle) | ~13 px wide, vertical edge at x 594, peak value **215,215,215** (M: `osk-default`); present in all three console frames; OSK-FIRMWARE 5.2 records it present while the I-beam is dark | 14 px wide, **2 px pure white (255)** lines at x 707..721 while lit (`o02`), and **absent** in the dark blink phase (`a-dark`, marker columns 707..721 empty) | **differ** (brighter + it blinks) |
| O7 | Caret blink period | lit 495/504/498 ms, dark 442/431, period ~936 ms, duty ~53 % (OSK-FIRMWARE 13.1) | `blinkMilliseconds = 468`, `blinkTraced = true` (`osk_layout.h:119-120`); phase pinned from `--render-clock` (`view.cpp:837-838`) | **match** (three documents still say 500 - S3/S4/S5/S7/S8) |
| O8 | Key grid: 10x6 cells 72x54, faces 66x50 | 496,406, 72x54 cells, 66x50 faces (M: grid rows 406/460/514/568/622/676) | from the resolved layout; measured face profile at x=505 row 1: 404:159 .. 428:196 .. 458:165 | **match** (grid crop 19.98 dB / 0.7258 reproduces OSK-FIRMWARE 15.6's 19.98/0.7250) |
| O9 | Captions | 24 px character rows / 19 px function rows, ink 30,40,50, glyph centre at cell+53.5 (OSK-FIRMWARE 4.6, 15.2) | same constants (`oskCharacterCaptionCentre = 53.5`); the glyph raster is the project's own, so 1-2 px of ink difference per key and a 9/28 px x residual on Backspace/Space (OSK-FIRMWARE 15.8) | **differ** (small, text-pipeline) |
| O10 | `LABEL_*` badge pills | L1/R1/L2/R2/SQUARE/TRIANGLE/SELECT/START, 74x28 art drawn at 20 px height (M: `osk-default` row 4/5) | from the RCO art at 20 px, a drawn pill as fallback (NS); visible and in the same cells in `o01` | **match** |
| O11 | Focus ring | `SBTN_FOCUS` 84x66, `LBTN_FOCUS` 166x66, at -9/-8 from the face (OSK-FIRMWARE 4.5) | same art, same offsets (`o01` on `g`, `o03` on the grid key) | **match** |
| O12 | Function strip, row 1 (`@#:`, `a/à` L2, `a/A` R2, Backspace, Space) | olive for cols 0-3, blue for Backspace/Space, captions + badges (M: `osk-default`; face mean at (505,628-655) = 166,159,130) | same roles; measured face mean 160,167,133 (Δ <= 7/255). The palette key is drawn on the *mint* family where the console's is olive - inside the same 7/255 band, so not a measurable delta | **match** |
| O13 | Function strip, row 2 (4 arrows, palette opener, Input Mode, Enter, copy/paste) | arrows blue, `Input Mode` mint with SELECT, `Enter` mint with START, clipboard well at 1217/1322 (M: `osk-default`) | identical set and cells; the palette opener draws a 4x3 block of 5 px squares (NS) | **match** |
| O14 | Prediction column frame, header, mode and count cells | list (110,110,110) at 1216,394 203x246; header 33 px with `Prediction`, mode cell (a/A) 27x33, count cell 27x33 (M: `osk-default`) | measured (1300,500) = **110,110,110** vs the console's 111,111,110; header gradient + `Prediction` at 16 px; `a`/`A` and `0`; caps page shows `A` (`o06`) | **match** |
| O15 | Prediction candidates | the console fills the list from `ime.prx` (OSK-FIRMWARE 8.2) - the `s12-type` burst shows `zz` after typing | list drawn, **always empty**, counter always `0` (NS comment at `:3626`; `o02` after typing `Xy` is still empty) | **absent** (a PC build has no IME dictionary; unreproducible without one) |
| O16 | Copy / paste buttons | both faces read **139,167,172** (measured means over 1225-1270 and 1330-1375 x 690-720); only the glyph reads as disabled (OSK-FIRMWARE 4.4) | copy face 102,131,138 / paste 111,142,148, and the whole paste button is drawn at 55 % opacity (NS `p.setOpacity(...?.55)`) | **differ** |
| O17 | Symbol palette (`@#:` popup) | frame 595,398 353x332, 7 columns on a 48 px pitch, 53x53 cells, 32 symbols in the module's list (OSK-FIRMWARE 4.3, 12.3) | same layout values committed; palette region 18.53 dB / 0.6516 unaligned, 17.22 / 0.6386 aligned against `s11`; the committed frame's x is 39 px right of that capture's panel relationship (OSK-FIRMWARE 15.6/15.8). Captured open (`a-palette`) | **differ** |
| O18 | Caps page | `!@#$%^&*()` / `QWERTYUIOP` / `ASDFGHJKL"` / `ZXCVBNM-_/`, mode cell `A` (OSK-FIRMWARE 4.2; `s4b`) | identical four rows and mode cell measured in `o06` | **match** |
| O19 | Accent page (L2 / the `a/à` key) | L2 opens the accent page (OSK-FIRMWARE 6) | the key logs `osk: accent page is not captured; the accent key is inert` (`view.cpp:3517`); no trigger binding in the OSK pad branch | **absent** (content uncollected, so also NR for its layout) |
| O20 | L1 / R1 caret move | L1/R1 move the text caret (badges on the [left]/[right] keys; OSK-FIRMWARE 6) | the **on-screen** [left] key works: measured caret moving one glyph left from after `Xy` to before `y` (`o04` vs `o03`). The **L1/R1 buttons do nothing**: the OSK pad branch (`view.cpp:3340`) handles A/B/X/Y/START and the d-pad only | **differ** (one of the console's two paths is missing) |
| O21 | SELECT input mode | cycles the console's IMEs (OSK-FIRMWARE 8.3) | key drawn, press logs `osk: Input Mode is unavailable (no IME modules; audit 8.3)` | **absent** (no IME modules locally) |
| O22 | Displayed text, clipping and scroll | 514 text origin, one line, clipped, scrolled to keep the caret visible (OSK-FIRMWARE 5.1/5.3) | 22 px, 20 px inset, `IntersectClip` at the field (`view.cpp:3617`), `scroll = max(0, advance(prefix)-(width-4))`; caret sits at the pen advance in `o02`/`o04` | **match** |
| O23 | Panel open / close alpha fade | ~66-96 ms open (Cross to rest ~550 ms, the dialog dims first), ~104 ms close with a bright flash (OSK-FIRMWARE 13.2) | none: the panel appears and disappears in one frame (NS: `drawOsk` has no opacity envelope; confirmed by `o01` -> `o07`) | **absent** |
| O24 | Panel opacity at rest | OSK-FIRMWARE 7 says the calling dialog shows through the title bar at ~12 %; GRAPHICS-INVENTORY 11 says "an opaque light-grey panel" | opaque; **no dialog text is visible through the title bar in `osk-default.png`**, so the 12 % note is not supported by the allgraphics frame and the two audits disagree | **match** (with that caveat recorded) |

Region scores against the console frame (this pass, `work/ab-compare.ts`, `o01` vs
`osk-default.png`): frame 24.74 dB / 0.8712; panel 20.49 / 0.7626; title bar 20.88 / 0.8348; field
20.02 / 0.8110; key grid 19.98 / 0.7258; function rows 17.85 / 0.6153; prediction 20.59 / 0.7520.
The key-grid, function-row and prediction numbers reproduce OSK-FIRMWARE 15.6 (19.98/0.7250,
17.84/0.6144, 20.61/0.7524) to within 0.02 dB, i.e. **the OSK draw has not moved since that
measurement**; the field is lower only because the console shows `User 1` and this build shows the
host name.

---

## 8. Media-related dialogs

| # | element | console | native | verdict |
|---|---|---|---|---|
| D1 | Playback error page | the player's error path is `videoplayer_simple_message_page` / `_playend_menu_page` in the container (FC 3.1); **no frame** of it exists | `showMessage("This file could not be played.", error, MessageKind::Page)` (`shell.cpp:2897`) and a Jellyfin variant (`:2883`) - a firmware-class page, drawn by `drawMessageSurface` | **no-reference** (the page class is right; nothing local to compare its content with) |
| D2 | Busy / loading while a media folder scans | no scan exists on the console; the closest object set is `videoplayer_buffering_page` (FC) | `Shell::busy()` -> `"Loading…"` 26 px at (110,910) (NS `view.cpp:1708`); while busy the media controls are forced into the "paused" material (`view.cpp:1670`) | **differ** |
| D3 | Copy / delete dialog for a media item | the console has a `copy_window` page and a delete confirmation (OSK-FIRMWARE 8.4 names the XMB clipboard; GRAPHICS-INVENTORY 9 captures the *game-item* delete confirm) | none: no media row carries a Copy or Delete action, and `dialogs.cpp` has only the item-delete box | **absent** |
| D4 | Unsaved-changes prompt | no media surface has unsaved state | `unsavedIds_` exists only in `app_editor.cpp` (Steam app editing), never in a media path | **absent** |
| D5 | Delete confirmation reachable from a media column | captured for a game item (`dlg-confirm-1800`) | 0 media rows with a destructive action; the box is unreachable from Video/Music/Photo | **absent** |

---

## 9. Top 12 gaps by visibility

Ordered by how much of the 1920x1080 frame they cost and how likely a viewer is to notice. Rects are
native (what is drawn) against firmware (what should be), with the measured delta.

| # | gap | native rect | firmware / target | measured delta |
|---|---|---|---|---|
| 1 | **The video player's 25-button control matrix is absent** (V4/V5) | 0 buttons; the lower third holds only a bar, a time string, a title and a hint row | 25 buttons, 13x3 cells, 70x70 icons (98x70 repeat), each with shadow + focus, in a panel plate | 25 of 25 missing; the console's whole player surface has no counterpart |
| 2 | **The photo viewer's control panel is absent** (P3) | 0 buttons | 20 buttons on a -7..+7 step-2 grid, 70x70 icons, a 300x9 size-10 label, a hint plane | 20 of 20 missing |
| 3 | **Every media column carries a different row set** (V1/M1/P1) | Video 4 rows, Music 3, Photo 3 (library/config rows) | Video 3, Music 2, Photo 3 (utility rows: Search for Media Servers / Playlists / Photo Gallery / BD Data Utility / Video Editor & Uploader) | 0 of the console's 8 media-column rows exist natively; 10 native-only rows |
| 4 | **A whole TV / Video Services column exists only natively** (T1/T2) | a 10th category between Video and Game, one row `My Channels` | the console has 9 categories and no TV column | +1 category, +1 row, and every icon after Video shifts one step (200 px) |
| 5 | **The OSK cursor marker blinks and is pure white** (O6) | 14x20 at x 707..721 (while lit) | ~13 px hollow triangle at x 582..594, peak 215, present in both blink phases | disappears for 442/431 ms of every 936 ms; ink 255 vs 215 |
| 6 | **The video status planes are absent** (V8) | none | 14 transport planes 74x74 + repeat 98x74 + 5 speed badges 72x36 | all of `tex_i_*` unused |
| 7 | **The video hint strip is a different object set** (V7) | 6 slots, 283.33 px pitch from x 110, icons 86x30, labels 19 px, drawn over the picture | one `videoplayer_cp_help` plane + 8 key icons (86x30) + the focused button's own name | 6 of 8 icons; 3 labels carry this build's bindings, not the control's (`L2` = Fast Reverse, `R2` = Fast Forward, `Circle` = Quit Playback) |
| 8 | **The photo hint strip and slideshow panel are absent** (P4/P8) | one ASCII sentence at 21 px, (110,1020,1700x35) | 19 `tex_dki_*` icons 86x30 + `slideshow_controlpanel_group` 11 children + status planes 74x74/98x70 | 19 icons and 11 panel children missing; a ~1250 px text line stands where the icons go |
| 9 | **The volume HUD can vanish or freeze at ~3-5 % on a still frame** (H3) | meter 860..1060 x 1008..1048 over the plate 852..1068 x 1000..1056 - present only when something else redraws the UI layer inside its 3 s window | a lit segment row fading in over 150 ms | pinned-clock capture: band unchanged at 43.39 dB / 0.9966 (5 of 6 captures); forced redraw 12.28 dB / 0.1254; live run full strength (128,154,50) |
| 10 | **The OSK's accent page and Input Mode are inert** (O19/O21) | two keys that only log | accent page (L2 / `a/à`) and the IME mode cycle (SELECT) | 2 of 2 direct-key actions do nothing |
| 11 | **The audio player's own progress page is not rebuilt** (M3) | 4 px flat track at (110,941,1700x4) + one `MM:SS / MM:SS` at 23 px + an ASCII sentence | `play_progress_page`: a 20 px Progress bar, 8+ time cells, track number, title, artist, codec label | 4 px vs 20 px bar; 1 string vs 8 cells + 3 text objects |
| 12 | **The video time bar and readout** (V9/V11) | `tex_progress_bar` + `_in` in (110,941,1700x20), 5+5 glyphs at 19 px in 16 px cells, jump marker at the played position | 3 stacked Progress + a Scroll + a jump-target marker; 8 cells per group + a sign cell, size 10 | 2 of 4 progress objects; 5 glyphs per group of 8 declared cells |

Runners-up, all measured: the OSK open/close fade (O23, 0 ms vs ~66-104 ms), the music panel's five
undrawn status planes (M6), the absent B1 14-button panel (M4), and the absent thumbnail strip (P7).

---

## 10. Local matches (25)

Recorded so the lead does not re-open them. Each was verified against a console frame or a
firmware fact this pass, not against another audit.

- **Video**: full-screen black backing (V3) - the only declared media screen plane is 1x1 black.
- **Music**: panel plate `tex_cp_panel` 72x72 at 1.3 (M5); 200x6 progress (M7); seven 90x60 buttons on
  a 90 px pitch with the shared `tex_cp_focus` (M9); 150/0 fade + 1.3 grow (M10); `snd_system_ok`
  on open (M11).
- **Photo**: front-screen black plane (P2).
- **Volume**: backing plate 853..1066 x 1002..1052 (H1); ten 20 px segments, 5 lit at 50 % (H2); no
  caption, no percentage, no border (H4).
- **OSK**: dim 0.70 (O1); panel 493,290 934x500 (O2); title bar colour (O3); field gradient (O4);
  I-beam geometry and colour (O5); 468 ms blink now wired (O7); 10x6 grid and 66x50 faces (O8);
  `LABEL_*` badges (O10); `SBTN_FOCUS`/`LBTN_FOCUS` (O11); row-1 function keys within 7/255 (O12);
  row-2 function keys (O13); prediction frame/header/mode/count (O14); caps page (O18); text
  clipping and scroll (O22); panel opaque at rest (O24).

The three numbers worth keeping at hand: panel **493,290 934x500** exactly; key grid **19.98 dB /
0.7258** against the console frame (0.02 dB from the earlier measurement); volume **10 segments at a
20 px pitch, 5 lit at 50 %**.

---

## 11. Elements with no console reference (3)

No frame shows these, so no delta is asserted; the verdict rests on firmware declarations or code
alone.

| element | why there is no reference |
|---|---|
| V14 video panel fade/hide timing | `videoplayer_plugin.rco` has 0 animations and `videoplayer_plugin.prx` has no local decompile or Ghidra program; the console's player was never captured |
| V16 active subtitle cue rendering | no console frame of playback exists; the subtitle pipeline is the SUBTITLES domain |
| D1 playback error page | the console's error page was never captured (GRAPHICS-INVENTORY 14 item 4) |

Beyond those three: **O19 (the accent page) is judged absent** because its key is inert in the
native build, and its *content* is also unreferenced - the accent page was never opened in the OSK
bursts, so not even the firmware-side layout is collected. The photo viewer's panel, hint strip,
thumbnail strip and slideshow panel (P3, P4, P7, P8) and the audio player's 14-button panel (M4) are
likewise marked **absent** on the strength of the RCO object trees rather than a picture.

Additionally **the whole volume HUD, the music player panel, the video player, the photo viewer and
the copy/busy dialog have no console frame** (GRAPHICS-INVENTORY 14 item 4, restating
REFERENCE-GAPS 7). Where those surfaces still produced a verdict above, the verdict rests on the
firmware container (object sizes, texture sizes, text sizes, the absence of any caption/plate/border)
rather than on a picture, and each such row says so. The TV column has no reference *by
construction*: the console does not have one.

---

## 12. Evidence index

| claim group | path |
|---|---|
| console OSK frames (panel rect, title/field/face colours, caret, marker, copy/paste faces, 3 blink-phase stills) | `out/rpcs3-allgraphics-2026-10-28/frames/osk-default.png`, `osk-default-2.png`, `osk-key-down.png`, `page-user-create.png` |
| console media-column rows and pages | same batch: `col2-video-r0*`, `col-music-r0*`, `col-photo-r0*`, `page3-*-row0*`, `page2-*-item*`, `page-video-item1*` |
| console column census (nine, no TV) | same batch: `INVENTORY.md` 3, 13, 14; `native-xmb/audit/GRAPHICS-INVENTORY.md` 3, 14 |
| firmware object trees, texture sizes, timing constants | `native-xmb/audit/MEDIA-PANELS-FIRMWARE.md` 3-6, 11; `OSK-FIRMWARE.md` 2-6, 12-15 (all **FC**/**FD** tables) |
| texture sizes re-verified this pass | `re-ps3-native.exe --export-image <container>:<name>@out`: `tex_cp_panel` 72x72, `tex_cp_shuffle`/`_repeat`/`_focus`/`_cp_prev` 90x60, `tex_play_status`/`_pause_status` 74x74, `tex_front_screen`/`_back_screen` 1x1 (front pixel 0,0,0), `tex_progress_bar`/`_in` 30x20, `tex_cp_help_circle` 86x30, `tex_default_lvmeter_on`/`_off` 20x40, `_bg` 48x48 |
| "SHUF" badge is firmware art, not a native placeholder | `out/fid-media/tex/tex_cp_shuffle.png` (a 90x60 key cap reading `SHUF`) |
| `tex_default_lvmeter_bg` is a black tile / `_on` a green capsule | `out/fid-media/tex/tex_default_lvmeter_bg.png`, `tex_default_lvmeter_on.png` |
| native OSK draw | `native-xmb/src/view.cpp` `drawOsk` (≈3528), `oskPress` (≈3504, accent case 3517), caret block (≈3621), caret phase (≈837); `osk_layout.h` 108-121, 130-153 |
| native volume HUD and its visibility bug | `view.cpp` `drawVolumeHud` (≈2631), segment loop (≈2690), call site and `fadeIn` (≈2371-2390), signature term (≈900) |
| native video chrome | `view.cpp` `drawVideoTimeBar` (≈2714), `drawTimeCells` (≈2754), `drawVideoHelpStrip` (≈2772), chrome band (≈1699-1723) |
| native music panel | `view.cpp` `drawMusicPanel` (≈2821, bar 2898, button loop 2912), `toggleMusicPanel` (≈2921, cue at 2928) |
| native photo viewer | `view.cpp` ≈1699-1710 (screen fill, ASCII sentence, `Zoom %`), `zoomPhoto`/`panPhoto` (≈3162-3175) |
| native column tables (TV column, media rows) | `native-xmb/src/shell.cpp` category table (`tv` at the row starting `{"tv","TV/Video Services"`) |
| native media dialogs | `shell.cpp` `advanceMedia` (2880-2900), `view.cpp` 1708 (busy line), `dialogs.cpp` (item-delete box only) |
| my capture frames | `out/fid-media/`: `o01`-`o08`, `a-dark`, `a-accent`, `a-palette`, `pv2`, `pv4`, `m4`, `hud50`, `hb-move`, `hb-early`, `hb-3s`, `hb-4s`, `hb-fade`, `h50`, `live-hud`, `settled-hud`, `v0`, `m0` |
| measurement tools | `work/ab-compare.ts` (PSNR/SSIM + `decodePng`), plus throw-away probes under `out/fid-media/` (`probe.ts`, `probe2.ts`, `caret.ts`, `hud.ts`, `meter.ts`, `bbox.ts`, `panel.ts`, `faces.ts`, `band.ts`, `band2.ts`) |

Not touched: any `native-xmb/src` file, `work/`, `out/` thresholds or references, the verification
harness, or git state. The only writes are this document and the scratch frames/probes under
`out/fid-media/`.

One host side effect, recorded and reverted: the volume captures drive the real Windows master
volume (`Shell` writes the endpoint through `--script`'s `VOLSET`/`MUTE` tokens). The endpoint was
**25 %, unmuted** before this pass and is **25 %, unmuted** after it (last run:
`volume: restored the start-up state 25% muted 0`).
