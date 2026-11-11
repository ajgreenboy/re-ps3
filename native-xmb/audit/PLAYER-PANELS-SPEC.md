# Player panels — implementation-ready specification

Scope: the four player surfaces named in `FIDELITY-VERDICT.md` §4 item C7 (the "thinnest area" of the
recreation). This document is the build recipe for them, not a re-audit. It records, for each surface, the
exact element list with provenance, the traceable geometry/sizes, the material (texture) references, the
current native state, and a ranked implementation plan mapped to the write-fence owners.

One structural fact governs all four surfaces and is stated once so it is not repeated: **no console
reference frame exists for any player surface** (`GRAPHICS-INVENTORY.md` 14 item 4; `REFERENCE-GAPS.md` 7;
re-confirmed in `FIDELITY-MEDIA.md` §11). Every screen rect and pixel pitch is therefore runtime-authored by
the plugin and **not in the container**. Only the object lists, cell grids, texture sizes, strings, and the
three code-derived timing/scale constants are traceable. The plan below does not ask for a frame it cannot
have; it specifies what *is* checkable and labels every position a FIT.

---

## Conventions

Every claim is labelled one of:

| tag | meaning |
|---|---|
| **TRACED** | read directly from the firmware container (`work/media-rco-dump.ts` → `work/*-tree-fields.txt`, `work/*-images.txt`, `work/*-texts.txt`), the decrypted module, or the app's RCO dump; cited by container + offset or file |
| **measured** | taken off a local capture frame or a re-exported texture; cited by the audit that measured it |
| **INFERRED** | a fit or a reading of a traced name; always labelled, never a number to trust |

RCO values in this document were re-read from the containers this session via the already-existing dumps
under `work/` (the dumps carry the object name, type id, stream offset, grid `posX/posY`, and the resolved
`image`/`shadow`/`image2` texture names + pixel sizes). Firmware numbers are never retyped from memory.

---

## 1. Surface A — video player: the 25-button control matrix and 14 status planes

**Firmware container:** `videoplayer_plugin.rco` (412,136 B; 147 objects, 159 images, 160 strings) —
sibling code module `videoplayer_plugin.prx` (not decompiled locally). Sibling string-only container
`videoplayer_util.rco`. Verdict: **absent** (V4/V5/V8 in `FIDELITY-MEDIA.md` §2; gap #1 and #6 in its §9).

### 1.1 Element list (TRACED, `work/vp-tree-fields.txt`)

```
videoplayer_control_panel_page      Page   @0xc094   -> 1 child
  videoplayer_cp_group              Group  @0xc0e0   all geometry 0
    videoplayer_cp_button_group     Group  @0xc160   children = 25
      (25 buttons, table 1.2)
    videoplayer_cp_button_name      Text   @0xd824   width=400 height=0 size=10
    videoplayer_cp_help             Plane  @0xd934   no image in template
videoplayer_status_page             Page   @0x11078  -> 1 child
  videoplayer_status_group          Group  @0x110c4  children = 5
    videoplayer_status_icon         Plane  @0x11144
    videoplayer_status_string_icon  Plane  @0x111d0
    videoplayer_status_text         Text   @0x1125c
    videoplayer_status_repeat_icon  Plane  @0x1136c
    videoplayer_status_repeat_mode_icon  Plane @0x113f8
```

The "14 status planes" of the verdict are **not 14 distinct plane objects**: they are the 14 transport
status *textures* bound to the single `videoplayer_status_icon` plane per state (table 1.3). The status
group is one icon plane + one string-icon plane + one text + two repeat planes.

### 1.2 The 25 buttons (TRACED, `work/vp-tree-fields.txt`; also `MEDIA-PANELS-FIRMWARE.md` 3.3)

Grid cells `(x,y)` are container integers; the pixel pitch is runtime (INFERRED). Every icon is **70x70**,
each with a `_shadow` and a `_focus` variant of the same size, except repeat at **98x70**. Buttons 1-11
carry `redScale=greenScale=blueScale=alphaScale=0` (hidden until the plugin publishes them); the dummy
button has no image.

| # | object | cell (x,y) | icon |
|---:|---|---|---|
| 1 | `videoplayer_cp_dummy_button` | -6, +1 | (none) |
| 2 | `videoplayer_cp_scene_search_button` | -5, +1 | `tex_cp_scene_search` |
| 3 | `videoplayer_cp_goto_button` | -4, +1 | `tex_cp_jump` |
| 4 | `videoplayer_cp_audio_change_button` | -3, +1 | `tex_cp_audio` |
| 5 | `videoplayer_cp_subtitle_button` | -2, +1 | `tex_cp_subtitle` |
| 6 | `videoplayer_cp_volume_control_button` | -1, +1 | `tex_cp_volume` |
| 7 | `videoplayer_cp_av_settings_button` | 0, +1 | `tex_cp_av_settings` |
| 8 | `videoplayer_cp_viewmode_button` | +1, +1 | `tex_cp_screen_mode` |
| 9 | `videoplayer_cp_icon_change_button` | +2, +1 | `tex_cp_change_icon` |
| 10 | `videoplayer_cp_del_button` | +3, +1 | `tex_cp_delete` |
| 11 | `videoplayer_cp_display_display_button` | +4, +1 | `tex_cp_display` |
| 12 | `videoplayer_cp_beginning_button` | -6, 0 | `tex_cp_prev` |
| 13 | `videoplayer_cp_next_button` | -5, 0 | `tex_cp_next` |
| 14 | `videoplayer_cp_fast_r_button` | -4, 0 | `tex_cp_search_b` |
| 15 | `videoplayer_cp_fast_f_button` | -3, 0 | `tex_cp_search_f` |
| 16 | `videoplayer_cp_play_button` | -2, 0 | `tex_cp_play` |
| 17 | `videoplayer_cp_pause_button` | -1, 0 | `tex_cp_pause` |
| 18 | `videoplayer_cp_stop_button` | 0, 0 | `tex_cp_stop` |
| 19 | `videoplayer_cp_flash_r_button` | +1, 0 | `tex_cp_flash_b` |
| 20 | `videoplayer_cp_flash_f_button` | +2, 0 | `tex_cp_flash_f` |
| 21 | `videoplayer_cp_slow_r_button` | +3, 0 | `tex_cp_slow_b` |
| 22 | `videoplayer_cp_slow_f_button` | +4, 0 | `tex_cp_slow_f` |
| 23 | `videoplayer_cp_oneframe_r_button` | +5, 0 | `tex_cp_step_b` |
| 24 | `videoplayer_cp_oneframe_f_button` | +6, 0 | `tex_cp_step` |
| 25 | `videoplayer_cp_repeat_button` | 0, -1 | `tex_cp_repeat` (98x70) |

### 1.3 The 14 status textures + repeat badges (TRACED, `work/vp-images.txt`; `MEDIA-PANELS-FIRMWARE.md` 3.4)

| role | textures | size |
|---|---|---|
| 14 transport status | `tex_i_play`, `tex_i_pause`, `tex_i_stop`, `tex_i_prev`, `tex_i_next`, `tex_i_jump`, `tex_i_search_b`, `tex_i_search_f`, `tex_i_slow_b`, `tex_i_slow_f`, `tex_i_flash_b`, `tex_i_flash_f`, `tex_i_step`, `tex_i_step_b` | **74x74** |
| repeat badges | `tex_i_repeat` **98x74**, `tex_i_title_repeat` **72x36** | |
| speed badges | `tex_i_10`, `tex_i_15`, `tex_i_30`, `tex_i_120`, `tex_i_f15`..`tex_i_f180` | **72x36** |
| aux icons | `tex_i_sound`, `tex_i_subtitle`, `tex_i_screen_mode` | **40x40** |
| codec labels | `tex_vc_*` (8 video), `tex_ac_*` (8 audio) | 110x28 / 120x28 |

### 1.4 Direct-key hint strip (TRACED)

Eight `tex_cp_help_*` icons, all **86x30**: `circle`, `cross`, `l1`, `l2_left`, `r1`, `r2_right`,
`select`, `start`. The firmware's panel hint is one `videoplayer_cp_help` plane + the focused button's own
name (`videoplayer_cp_button_name`, Text width 400 size 10).

### 1.5 Declared geometry in the time page (TRACED — the only non-zero rects in the panel)

`videoplayer_display_time_page` → `videoplayer_display_time_group`:

| object | declared | note |
|---|---|---|
| `videoplayer_display_chapter_button` | **200x64** at x 0 | chapter hit area |
| `videoplayer_display_time_button` | **750x64** at x 250 | time-bar hit area (group-local) |
| `videoplayer_display_chapter_text` / `_num` / `_sign` | 16x16 each | per-glyph cells |
| `videoplayer_display_playing_time_h1..c2` | 16x16 x 8 | one digit per cell |
| `videoplayer_display_play_time_h1..c2` | 16x16 x 8 | duration digits |
| `videoplayer_display_time_bar_base` / `_playable_time` / `_bar` | Progress, max 100, **no image ref** | art published at runtime |
| `videoplayer_display_scroll_bar` | Scroll, max 100/100 | |
| `videoplayer_display_time_jump_time_bar_marker` | Plane, `tex_progress_bar_jump_indi` **20x12** | marks a *jump target* |

Progress/scroll textures: `tex_progress_bar` **30x20**, `tex_progress_bar_in` **30x20**,
`tex_scroll_bar` / `_base` **20x30**, `tex_shuttle_ring` / `_bg` / `_s` / `_e` **96x96**,
`tex_wait_bar` **90x38**, `tex_loading_icon` **90x90**.

### 1.6 Strings (TRACED, `work/vp-texts.txt`)

`msg_play_with_icon` "Play ", `msg_pause_with_icon` "Pause ", `msg_stop` "Stop",
`msg_next_with_icon` "Next ", `msg_prev_with_icon` "Previous ", `msg_beginning_with_icon`
"Return to Beginning ", `msg_fast_f/r_with_icon` "Fast Forward/Reverse ", `msg_flash_f` "Instant Advance",
`msg_flash_r` "Instant Replay", `msg_slow_f/r` "Slow (Forward/Reverse)", `msg_goto` "Go To",
`msg_scene_search` "Scene Search", `msg_chapter` "Chapter", `msg_subtitle_change` "Subtitle Options",
`msg_audio_change` "Audio Options", `msg_av_settings` "AV Settings", `msg_viewmode` "Screen Mode",
`msg_repeat` "Repeat", `msg_repeat_ab` "A-B Repeat", `msg_volume_control` "Volume Control",
`msg_icon_change` "Change Icon", `msg_display_display_with_icon` "Display ", `msg_del` "Delete",
`msg_buffering` "Buffering...", `msg_quit_playback` (Circle's label — this container has **no** Back string).

The `_with_icon` strings end in a private-use gaiji (U+F88x) after the trailing space; the icon is the
hint strip's own 86x30 key texture, so the gaiji is dropped (already done in `drawVideoHelpStrip`).

### 1.7 Timing and sound (TRACED/INFERRED boundary)

- `videoplayer_plugin.rco` has **zero** animation entries (TRACED). The panel fade/hide timing lives in
  `videoplayer_plugin.prx`, which has **no local decompile** (INFERRED / spec-only).
- No `snd_*` string exists in `videoplayer_plugin.prx` (TRACED, negative). The only firmware-documented
  panel-open cue is `snd_system_ok` on the *XMB music* panel (`explore_plugin_all.c:38936`).

### 1.8 Current native state (read from `native-xmb/src/view.cpp`)

| firmware element | native today | verdict |
|---|---|---|
| 25-button matrix (`videoplayer_cp_button_group`) | **no button drawn**; no `videoplayer_cp_*` image is referenced anywhere in `src/` | **absent** |
| control-panel plate / `videoplayer_cp_group` | none | **absent** |
| focused-button name label | media title drawn instead at 27 px (110,984,1500x38) | **absent** |
| 14 status planes + repeat/speed badges | none; `tex_i_*` unused | **absent** |
| time bar | `drawVideoTimeBar` (`view.cpp:3642`): `tex_progress_bar`/`_in` 30x20 nine-sliced in `(110,941,1700,20)` FIT, jump marker at the played position, 2x8 time cells (`drawTimeCells`, 16 px cell, 19 px digit FIT) | **differ** (2 of 4 progress objects, no Scroll) |
| hint strip | `drawVideoHelpStrip` (`view.cpp:3702`): six 86x30 icons at 283.33 px slots, labels 19 px | **differ** (6 of 8 icons) |
| in-player options (Triangle) | native-only right-hand band (subtitles/audio/repeat/shuffle) | **differ** (native-only shape) |

Relevant existing code: the media branch at `view.cpp:2411-2458` (time bar + hint strip), `MediaControls`
(`media_controls.cpp`) for the 3000 ms + 250 ms visible/fade model, `Shell::mediaPosition/Duration/Paused/
RepeatMode/QueueSummary` and the play/pause/skip/seek/repeat/shuffle bindings already in `pollController`.

---

## 2. Surface D — photo viewer: the 19-button panel and hint/slideshow strips

**Firmware container:** `photoviewer_plugin.rco` (249,620 B; 154 objects, 109 images, 125 strings).
Verdict: **absent** (P3/P4/P7/P8 in `FIDELITY-MEDIA.md` §4; gap #2 and #8 in its §9).

### 2.1 Element list — view control panel (TRACED, `work/pv-tree-fields.txt`)

`control_panel_page` → `view_controlpanel_group` @0x95f8, **21 children = 19 buttons + 1 label + 1 hint
plane**. **Note:** the verdict says "20-button panel"; the tree has **19 buttons** — the "20" does not
survive a count (8 at y=2, 8 at y=0, 3 at y=-1). All icons **70x70**, `alphaScale=0.8`.

| row | object (cell) | icon |
|---|---|---|
| y = +2 | `displaymode_button` (-7) | `tex_cp_viewmode` |
| | `effect_change_button` (-5) | `tex_cp_effect_change` |
| | `trimming_button` (-3) | `tex_cp_trimming` |
| | `add_to_playlist_button` (-1) | `tex_cp_playlist` |
| | `print_button` (+1) | `tex_cp_print` |
| | `set_as_wallpaper_button` (+3) | `tex_cp_wpaper` |
| | `del_button` (+5) | `tex_cp_delete` |
| | `display_display_with_icon_button` (+7) | `tex_cp_showinfo` |
| y = 0 | `zoom_in_button` (-7) | `tex_cp_zoom_in` |
| | `zoom_out_button` (-5) | `tex_cp_zoom_out` |
| | `rotate_left_with_icon_button` (-3) | `tex_cp_rotate_left` |
| | `rotate_right_with_icon_button` (-1) | `tex_cp_rotate_right` |
| | `up_button` (+1) | `tex_cp_cursor_up` |
| | `down_button` (+3) | `tex_cp_cursor_down` |
| | `left_button` (+5) | `tex_cp_cursor_left` |
| | `right_button` (+7) | `tex_cp_cursor_right` |
| y = -1 | `prev_with_icon_button` (-1) | `tex_cp_prev` |
| | `next_with_icon_button` (0) | `tex_cp_next` |
| | `slideshow_with_icon_button` (+1) | `tex_cp_play` |
| label | `button_description_label_incpv` (cell 0,-2) | Text, **width=300 height=9 size=10** |
| hint | `direct_key_icon` | Plane, no image in template |

(`MEDIA-PANELS-FIRMWARE.md` 6.2 folded prev/next/slideshow into its "y=0" row; the tree places them at
y=-1, a third row. The cell grid spans x -7..+7 in steps of 2.)

### 2.2 Element list — slideshow control panel (TRACED, `work/pv-tree-fields.txt`)

`controlpanel_slideshow_page` → `slideshow_controlpanel_group` @0xa94c, **11 children = 8 buttons + 1 label
+ 2 hint planes**:

`slideshow_change_button` (`tex_cp_slideshowoption`), `slide_speed_button` (`tex_cp_slide_speed`),
`sld_prev_with_icon_button` (`tex_cp_prev`), `sld_next_with_icon_button` (`tex_cp_next`),
`play_with_icon_button` (`tex_cp_play`), `pause_with_icon_button` (`tex_cp_pause`), `stop_button`
(`tex_cp_stop`), `repeat_button` (`tex_cp_repeat`, **98x70**), `button_description_label_incps` (Text
width=300 size=10), `sld_direct_key_icon`, `sld_direct_key_icon2`. All 70x70 except repeat.

### 2.3 Direct-key and status art (TRACED, `work/pv-images.txt`)

- **19** `tex_dki_*` hint icons, all **86x30**: `circle`, `cross`, `square`, `triangle`, `up`, `down`,
  `left`, `right`, `l1`, `l2`, `l2_left`, `l3`, `l3_nagaoshi`, `r1`, `r2`, `r2_right`, `r3`, `select`,
  `start`.
- Status: `tex_play_status` / `_pause_status` **74x74** (+ focus/shadow 70x70), `tex_repeat_status` **98x70**,
  `tex_indi_Transition` **40x40**, `tex_bar_photo_icon` **46x46**.
- Thumbnails: `tex_thumbnail_frame` **12x12** (nine-slice), `tex_thumbnail_frame_shadow` **18x18**,
  `tex_thumbnail_shadow` **21x21**; **16 `IItem` rows** exist for the strip.

### 2.4 Screen planes (TRACED)

`Pv_front_screen_plane` @0x9448 and `Pv_back_screen_plane` @0x9520 are the only full-screen rects in the
media set: **1920x1080**, with `tex_front_screen` / `tex_back_screen` **1x1** (front pixel (0,0,0),
re-verified by export in `FIDELITY-MEDIA.md` §4).

### 2.5 Current native state (read from `native-xmb/src/view.cpp`)

| firmware element | native today | verdict |
|---|---|---|
| 19-button view panel | **none** | **absent** |
| hint strip | one ASCII sentence at 21 px (110,1020,1700x35): `"Circle Back Cross Slideshow/Pause L1/R1 or +/- Zoom Square/0 Reset Triangle/Q/E Rotate Arrows Pan when zoomed [/] Photo"` | **absent** |
| slideshow control panel + status planes | a `Pause`/`Slideshow` word inside the sentence only | **absent** |
| thumbnail strip (16 IItem) | none | **absent** |
| numeric zoom readout | `"Zoom %1%"` at 21 px (110,895,1700x36) — native-only; **no numeric zoom exists in firmware** | **differ** |
| screen planes | `p.fillRect(QRectF(0,0,1920,1080),Qt::black)` then the image (`view.cpp:2358-2363`) | **match** |
| zoom/pan/rotate/slideshow *state* | `PhotoViewport` (pure geometry, `photo_viewport.h`), `Shell::rotatePhoto/stepPhoto`, `slideshow_` timer | **states exist, surface does not** |

---

## 3. Surface B1 — audio player: the progress page (and its own 14-button panel)

**Firmware container:** `audioplayer_plugin.rco` (271,984 B; 75 objects, 72 images, 60 strings). Two
distinct surfaces: **B1** (this player's own page + 14-button panel) and **B2** (the XMB Triangle panel in
`explore_plugin_full.rco`, already drawn). Verdict: **differ** (M3) — the page is *not* the firmware's.

### 3.1 Element list (TRACED, `work/ap-tree-fields.txt`)

```
full_screen_page               Page   @0x4e24   (empty)
back_screen_page               Page   @0x4e70   -> back_screen_plane  tex_back_screen 1x1, alphaScale 0
control_panel_page             Page   @0x4f48
  audioplayer_cp_group         Group  @0x4f94
    audioplayer_cp_button_group  Group @0x5014  children = 14
    button_description_label   Text   @0x5d0c   width=300 size=10
title_information_page         Page   @0x5e1c   -> plane_jacket_music_cover (tex_track_frame 240x240),
                                    plane_track_number1/2 (tex_track_number0..9 46x68),
                                    text_contents_title_name, text_artist_name (both size 10)
play_progress_page             Page   @0x62ac
  play_progress_group          Group  @0x62f8   children = 6
    text_track_number          Text   @0x6378
    text_current_time          Text   @0x6488
    play_progress_group_play_bar  Group @0x6598 -> 8 x text_h1/h2/c1/m1/m2/c2/s1/s2_time  (time cells)
    audioplayer_display_time_bar   Progress @0x74f4  height=20  max=100
    play_progress_group_str_kind   Group @0x6e98 -> text_ap_num / text_ap_co / text_ap_sp (codec label)
audioplayer_volume_page        Page   @0x7bd8   -> audioplayer_volume_level_meter
```

### 3.2 The 14-button player panel (TRACED)

All icons **70x70** except repeat/shuffle **98x70**; every button carries `alphaScale=0.8` (not 0 like the
video panel). Cells x -3..+3, rows +1/0/-1:

| object | cell | icon |
|---|---|---|
| `msg_volume_control_button` | -2,+1 | `tex_cp_volume` |
| `msg_visualizer_with_icon_button` | -1,+1 | `tex_cp_vislizer` |
| `msg_add_to_playlist_button` | 0,+1 | `tex_cp_playlist` |
| `msg_del_button` | +1,+1 | `tex_cp_delete` |
| `msg_display_display_with_icon_button` | +2,+1 | `tex_cp_showinfo` |
| `msg_prev_with_icon_button` | -3,0 | `tex_cp_prev` |
| `msg_next_with_icon_button` | -2,0 | `tex_cp_next` |
| `msg_fast_r_with_icon_button` | -1,0 | `tex_cp_searchb` |
| `msg_fast_f_with_icon_button` | 0,0 | `tex_cp_searchf` |
| `msg_play_with_icon_button` | +1,0 | `tex_cp_play` |
| `msg_pause_with_icon_button` | +2,0 | `tex_cp_pause` |
| `msg_stop_button` | +3,0 | `tex_cp_stop` |
| `msg_repeat_button` | -1,-1 | `tex_cp_repeat` 98x70 |
| `msg_shuffle_button` | 0,-1 | `tex_cp_shuffle` 98x70 |

Status art (play_status_page, 10 planes): `tex_play/pause/stop/next/prev_status` **74x74**,
`tex_repeat_status` **98x74**, `tex_1repeat_status` **72x36**, `tex_shuffle_status` **98x74**,
`tex_searchb/f_status` **74x74**. Codec labels `tex_mp3/pcm/aac/wma/atrac/cdda` 110x28 / 120x28.

### 3.3 Current native state (read from `native-xmb/src/view.cpp`, `audit/MUSIC-PLAYER.md`)

`drawMusicPage` (`view.cpp:3963`) **already draws** the B1 page: 240x240 frame, 162x162 cover, 46x68
track-number digits, title/artist (album is a native addition — no firmware object), codec label, play/
pause status planes + repeat/shuffle badges, the `msg_track_number_value` / `msg_tracks_count_value` line,
a button guide, and a time bar. What remains:

| firmware element | native today | verdict |
|---|---|---|
| `audioplayer_display_time_bar` (Progress **height 20**) | **not used** — the page borrows the video player's `drawVideoTimeBar` (`tex_progress_bar`, 20 px) at y941 | **differ** (the container's own bar is 20 px but its art is borrowed) |
| 14-button player panel (`control_panel_page`) | **not built** — B2 (the XMB Triangle panel, 7 cells + a Visual Player 8th cell) stands in | **absent** |
| `del_dialog_page`, `err_dialog_page`, `quit_confirm_dialog_page` | not built | **absent** |
| `audioplayer_volume_page` meter | not built | **absent** |

All positions in `drawMusicPage` are INFERRED (the container's objects carry zero position), as
`MUSIC-PLAYER.md` states.

---

## 4. Surface C — the volume HUD

**Firmware:** eight containers define a volume page, each **one page + one `Meter`** (paf `PhLevelMeter`,
class string at `vsh.elf` file offset 6,811,464) and nothing else that draws. Segment art lives in
`system_plugin.rco`. Verdict: **differ** (H3/H5 — the two remaining defects named in C7).

### 4.1 Element list and art (TRACED, `work/media-rco-dump.ts` → `system_plugin` images; `MEDIA-PANELS-FIRMWARE.md` 5.1-5.2)

| container → page → meter | meter offset |
|---|---|
| `videoplayer_plugin` `videoplayer_volume_page` → `videoplayer_volume_level_meter` | 0x11a2c |
| `audioplayer_plugin` `audioplayer_volume_page` → `audioplayer_volume_level_meter` | 0x7c24 |
| `explore_plugin_full` `volume_page` → `volume_level_meter` | 0x39674 |
| `explore_category_music` `volume_page` → `volume_level_meter` | 0x11af4 |
| `sacd_plugin` `sacdplayer_volume_page` → `sacdplayer_volume_level_meter` | 0x8c54 |
| `bdp_plugin` `page_bdpui_audiovolumecontrol` → `bdpui_audiovolumecontrol_level_meter` | 0x1f590 |
| `avc_plugin` (no page) → `volume_level_meter` | 0xe120 |
| `avc2_text_plugin` `avc2_volume_control` → `avc2_control_lv_meter` | 0x916c |

Art (TRACED): `tex_default_lvmeter_on` / `_off` / `_focus` **20x40** each, `tex_default_lvmeter_bg`
**48x48** — the only `lvmeter` textures in the whole 122-container set. No caption, no percentage, no
plate, no border (TRACED, negative; `MEDIA-PANELS-FIRMWARE.md` 5.3).

### 4.2 What is traceable vs not

| property | firmware value | class |
|---|---|---|
| segment art | 20x40 (`on`/`off`/`focus`) | TRACED |
| backing | 48x48 (`tex_default_lvmeter_bg`) | TRACED |
| fade in | **150 ms, delay 0** (`explore_plugin.prx` `0x270a00` / `0x270a04`, read with `work/elf-const.ts`) | FD |
| segment count | **not in the container**; `unknown24 = 4` is the only candidate field | spec-only |
| meter rect / pitch | runtime; not in the container | spec-only |
| which segment `_focus` draws | not traced | spec-only |

### 4.3 Current native state (read from `native-xmb/src/view.cpp`)

`drawVolumeHud` (`view.cpp:3559`):

- Draws **10** segments (`volumeMeterSegments=10`, FIT) at a **20 px** pitch, `on`/`off` art, backing
  `tex_default_lvmeter_bg` nine-sliced at a 16 px inset. Measured (`FIDELITY-MEDIA.md` §6): plate
  853..1066 x 1002..1052, 5 lit at 50 %, no caption/value/border.
- **Focus segment — still never drawn.** `view.cpp:3616`:
  `const QImage& art = i<litCount ? lit : (i==litCount-1 ? focus : dim);`
  The `focus` branch is **unreachable**: `i<litCount` already takes every index `<= litCount-1`, so
  `tex_default_lvmeter_focus` is decoded and then never used. This is `FIDELITY-MEDIA.md` S1 / "fit
  unrealised" (`MEDIA-PANELS-FIRMWARE.md` 11.5 #3), and it is the surviving half of verdict C7's
  volume defect.
- **Fade-in signature — now present.** The verdict (`FIDELITY-VERDICT.md` C7) and
  `FIDELITY-MEDIA.md` H3/S11 recorded that the 150 ms fade amount was *not* in the UI-layer signature
  (`" vol %d %d %d "` carried only `hud.opacity`), which is why a still frame could lose the meter. The
  current tree has closed this: `view.cpp:1156-1158` now writes
  `" vol %d %d %d %d "` with `int(hud.opacity*256.)`, `int(fadeIn*256.)`, `volumeLevel_`, `volumeMuted_`,
  and `drawScene` multiplies `hud.opacity*fadeIn` (`view.cpp:3192`). So the H3 "vanishes on a still frame"
  bug is **fixed in the working tree**; only the focus segment remains.
- `MediaControls` still owns the visible window (3000 ms + 250 ms fade, `media_controls.cpp:8-9`); the
  150 ms appear edge sits on top of it.

---

## 5. Ranked implementation plan

Ordered by visible-yield per unit of work, with the smallest, lowest-risk item first (it closes a recorded
gap and can land while the larger surfaces are still being scoped). File ownership follows the existing
fence: **view.cpp/view.h = View owner, shell.cpp/shell.h = Shell owner, or a new file.**

| # | surface | work | files (owner) | why this order |
|---|---|---|---|---|
| 0 | **Volume HUD focus segment** | make the boundary segment draw `_focus` instead of `on` (fix the dead `focus` branch in `drawVolumeHud`). No new state, no new file. | `view.cpp` (View owner), one ternary | one line; closes the one recorded "fit unrealised" in C7; the fade-in signature is already fixed. |
| 1 | **Video 25-button matrix + 14 status planes** | draw `videoplayer_cp_button_group` as a focusable button row over the video (70x70 icons + shadow + focus), a focused-name label (`videoplayer_cp_button_name`, width 400 size 10), and the status page (one `tex_i_*` plane + repeat/speed badges). Most actions already map to existing `Shell` bindings (play/pause/skip/seek/repeat/shuffle). | `view.cpp` + `view.h` (View owner, draw + focus-nav state); `shell.cpp` only if a button needs a new action | the single largest absent surface (`FIDELITY-MEDIA` §9 gap #1 + #6); all art is present in `videoplayer_plugin.rco`. |
| 2 | **Photo viewer panel + hint/slideshow strips** | draw the 19-button `view_controlpanel_group` (3 rows, 70x70), the 19 `tex_dki_*` 86x30 hint icons, and the 11-child `slideshow_controlpanel_group`; drop the ASCII sentence and the `Zoom %1%` readout. The underlying states already exist (`PhotoViewport`, `Shell::rotatePhoto/stepPhoto`, `slideshow_`). | `view.cpp` + `view.h` (View owner); `shell.cpp` (Shell owner) only to expose slideshow option/speed state | the second largest absent surface; a pure draw job because the state machine already exists. |
| 3 | **Audio B1: own progress bar + 14-button player panel** | replace the borrowed `drawVideoTimeBar` with the container's own `audioplayer_display_time_bar` (height 20) + the 8 time cells; build the 14-button `control_panel_page` (or keep B2 and note the substitution). | `view.cpp` + `view.h` (View owner); `shell.cpp` for any new panel action | completes an already-partial surface (`drawMusicPage` landed 2026-11-08) rather than building from zero. |

Rationale for the four-surface order: steps 1-3 are the *absent* surfaces (ranked by frame area lost);
step 0 is a one-line close of a recorded regression. Video outranks photo because video playback is the
primary HTPC flow and its matrix is the console's whole player surface; photo outranks audio because the
audio page already has a serviceable `drawMusicPage` while the photo viewer has no panel at all.

Cross-cutting constraints that apply to every step:

- **No console frame exists** (GRAPHICS-INVENTORY 14 item 4), so every screen rect and button pitch is a
  FIT. The spec's own precedent is `MEDIA-PANELS-FIRMWARE.md` 11.5: each FIT is listed with its firmware
  status and its reason. A new step must append to that list rather than hide a fitted number.
- **Texture sizes and cell grids are TRACED and binding**: icons at 70x70 (repeat/shuffle 98x70), status at
  74x74/98x74/72x36, hint icons 86x30, the 16x16 time cells, `audioplayer_display_time_bar` height 20.
  These are the values a build must use; only the *placement* of the panel is free.
- **Panel text is white with the standard `(4,-5)` / 0.25 shadow, size 10** (`MEDIA-PANELS-FIRMWARE.md` 2.4);
  the native `mediaPanelText` path already carries that material through the glyph renderer.

### Suggested decomposition for the video matrix (the largest step)

1. A panel-visibility + focus state in `view.h` (mirror of the existing `musicPanelOpen_/musicPanelFocus_`
   block), opened with a key/pad gesture, walked with Left/Right, confirmed with Cross.
2. `drawVideoPanel(p, opacity)` in `view.cpp` that renders the 25-slot grid from the 1.2 table (only the
   populated slots), each button's `tex_cp_*`/`_shadow`/`_focus` triple, and the focused button's name via
   `videoplayer_cp_button_name`'s width-400 label. Grid geometry is FIT (see above); the cell-to-pixel
   pitch is this build's choice and must be logged as a FIT.
3. `drawVideoStatus(p)` that binds one `tex_i_*` plane per transport state plus repeat/speed badges.
4. Keep `drawVideoTimeBar`/`drawVideoHelpStrip`; the matrix is a *peer* surface to the time page, not a
   replacement.

---

## 6. Verification (what "done" is checkable to)

The tier system (`PARITY-SCORECARD.md` §5): **Tier A** = dump/sidecar/log equality; **Tier B** = pixel
parity against a reference frame (provisional budgets, `work/parity-reference.json`); **Tier C** = ordered
log traces. For the player panels **Tier B cannot run today**: no console or RPCS3 frame of any player
surface exists locally (`GRAPHICS-INVENTORY.md` 14 item 4), and 26 console surfaces are unreachable on this
rig by construction. So the verification for these four surfaces is:

1. **Tier A (the binding gate).** A dump/log assertion that the built panel has the exact object list: 25
   video buttons in the 1.2 grid with the correct `tex_cp_*` bindings, 19 photo buttons in the 2.1 grid,
   14 audio buttons in the 3.2 grid, 8 `tex_cp_help_*` / 19 `tex_dki_*` hint icons, and the 14 `tex_i_*`
   status textures. This is structural equality against `work/*-tree-fields.txt`, not pixels.
2. **Firmware-fact measurement (the honest ceiling).** Re-export the textures with the app's own
   `--export-image` and assert the sizes in tables 1.3/2.3/3.2/4.1; the measured panel frames are scored
   against the *declared* sizes (icon 70x70, status 74x74, hint 86x30, cell 16x16) rather than against a
   frame, and every FIT is listed next to its firmware status.
3. **No-regression gate.** The canonical `bun work/ab-layers.ts --capture` stays **6/6 PASS**: the media
   surfaces draw only while media is open, which the canonical root capture does not trigger, so any
   change that moves a root layer is a regression. The named-state suite (`--states`) must stay pass.
4. **Self-consistency.** Pinned-clock (`--render-clock`, `--ui-clock`, `--background-clock`) captures of a
   panel-open state reproduce bit-identically (PSNR infinity / SSIM 1), the same replay gate
   `NOTIFICATIONS.md` / `MEDIA-CONTROLS.md` use. This proves the panel is deterministic, not that it
   matches the console.

To turn any one surface into a real **Tier B** claim, the missing input is a matched reference frame of
that surface (a console capture or a decompile of `videoplayer_plugin.prx` / `photoviewer_plugin.prx` /
`audioplayer_plugin.prx` to recover the runtime projection). Until then the verdict stays
"firmware-fact + measured native", and this document states that ceiling rather than inventing a frame.

---

## 7. Evidence index

| claim group | path |
|---|---|
| video object tree, 25-button grid, status group, time-page rects | `work/vp-tree-fields.txt` (re-read this session); `MEDIA-PANELS-FIRMWARE.md` 3.1-3.7 |
| video images/textures/strings | `work/vp-images.txt`, `work/vp-texts.txt` |
| photo object tree, 19+8 buttons, hint/slideshow groups, 19 `tex_dki_*` | `work/pv-tree-fields.txt`, `work/pv-images.txt` (re-read this session); `MEDIA-PANELS-FIRMWARE.md` 6.1-6.4 |
| audio object tree, 14-button panel, progress page, status art | `work/ap-tree-fields.txt`, `work/ap-images.txt` (re-read this session); `MEDIA-PANELS-FIRMWARE.md` 4.1; `MUSIC-PLAYER.md` |
| XMB panel (B2) + volume timing constants | `work/explore-full-tree.txt`; `explore_plugin_all.c:38787-38821, 38936`; `work/elf-const.ts` at `0x270a00/0x270a04/0x270a54/0x270a58/0x270a5c` |
| volume art + PhLevelMeter | `system_plugin.rco` `tex_default_lvmeter_*`; `paf-range.c:23812` |
| current native draw sites | `native-xmb/src/view.cpp` `drawVolumeHud` (3559), `drawVideoTimeBar` (3642), `drawTimeCells` (3684), `drawVideoHelpStrip` (3702), `drawMusicPanel` (3751), `drawMusicPage` (3963), media branch (2411-2458), signature (1156-1158); `media_controls.cpp`; `photo_viewport.h`; `musicvis.h` |
| verdict and diff source | `FIDELITY-VERDICT.md` §4 C7; `FIDELITY-MEDIA.md` §2/4/6, §9 gaps 1/2/6/7/8/9/11; `MEDIA-PANELS-FIRMWARE.md` §11 |

No firmware or theme content is shipped in this document; object names are quoted only as short
identifiers needed for the spec. No source file was edited, nothing was built, and nothing was committed.

---

status: groundwork only, no code changed, not built
