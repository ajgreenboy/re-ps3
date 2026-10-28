# Media playback panels: what the firmware defines, and the native diff

Analysis pass, 2026-10-27. No source file was edited. No RPCS3 run was started; no git command was run.
Every firmware number below was read out of the local `dev_flash` containers or out of the local
decrypted plugin data, both read-only.

The criticism this answers - "several UI surfaces were designed free-hand rather than rebuilt from the
firmware" - is **correct for all four media surfaces**, and this document says exactly which numbers
were invented and which were merely fitted.

Contents

1. Evidence classes and tools
2. Shared facts about the four player containers
3. Surface A - video player control panel
4. Surface B - music playback panel
5. Surface C - the volume display (the fitted 222x58 HUD)
6. Surface D - photo viewer chrome
7. Native vs firmware diff per surface, with the exact changes
8. Runtime-read sources that should replace hardcoded numbers
9. What is spec-only, and why
10. Evidence index

---

## 1. Evidence classes and tools

Each claim below is tagged with one of these:

| tag | meaning | how it was obtained |
|---|---|---|
| **FC** | firmware container | object/image/text tables read directly from `C:/dev/rpcs3/bin/dev_flash/vsh/resource/<name>.rco` |
| **FA** | firmware artifact | a local decompile (`out/native-reference/*.c`) or a local CSV under `rea/` |
| **FD** | firmware data | a constant read out of a local decrypted module's data segment with `work/elf-const.ts` |
| **CR** | container-relative | an inference from the container layout, not from code; always labelled |
| **NS** | native source | read, never run |
| **SO** | spec-only | no local evidence exists; needs a reference frame or a new decompile |

Tooling written for this pass (both under `work/`, both read-only):

- `work/media-rco-dump.ts` - RCO object-tree / image-table / text-table dumper. It resolves the
  8-byte `image` fields to image-table entries (so a button's three icon textures are named, not just
  numbered), decodes GIM/PNG/DDS headers for real pixel sizes, and prints every attribute with its
  table offset. This is what produced every FC table here.
- `work/elf-const.ts` - maps a load address in a decrypted PRX/ELF to a file offset and prints the
  words there as floats or ints. This is how the explore_plugin timing constants (**FD**) were read.

Raw dumps kept for re-checking, not committed as firmware data: `work/vp-tree-fields.txt`
(videoplayer_plugin, all attributes), `work/ap-tree-fields.txt`, `work/pv-tree-fields.txt`,
`work/explore-full-tree.txt`, `work/vp-images.txt`, `work/ap-images.txt`, `work/pv-images.txt`,
`work/vp-texts.txt`, `work/ap-texts.txt`, `work/pv-texts.txt`.

Reproduce any table below with:

    bun work/media-rco-dump.ts --rco videoplayer_plugin --tree --fields
    bun work/media-rco-dump.ts --rco videoplayer_plugin --images
    bun work/media-rco-dump.ts --rco videoplayer_plugin --texts
    bun work/elf-const.ts --elf fw493/vsh_module/explore_plugin.prx --at 0x270a00 --count 40 --type float

---

## 2. Shared facts about the four player containers

| container | bytes | objects | images | strings (English) |
|---|---:|---:|---:|---:|
| `videoplayer_plugin.rco` | 412,136 | 147 | 159 | 160 |
| `audioplayer_plugin.rco` | 271,984 | 75 | 72 | 60 |
| `photoviewer_plugin.rco` | 249,620 | 154 | 109 | 125 |
| `explore_plugin_full.rco` (XMB music panel) | 2,421,192 | 87 | 245 | 861 |

Object-type census, from the same tool:

| type | video | audio | photo | explore_full |
|---|---:|---:|---:|---:|
| Page | 15 | 11 | 22 | 15 |
| Plane | 24 | 18 | 34 | 36 |
| Group | 17 | 9 | 16 | - |
| Button | 30 | 14 | 41 | 19 |
| Text | 43 | 19 | 23 | 6 |
| Progress | 6 | 1 | - | 1 |
| Scroll | 1 | - | - | 1 |
| Meter | 1 | 1 | - | 1 |
| ItemSpin / Spin | 4 / 2 | - | - | - |
| MList / XList / IList | 2 / 1 / - | - / - / 1 | - / - / 1 | 6 / - / - |
| IItem | - | - | 16 | - |
| ObjectTree | 1 | 1 | 1 | 1 |

Four structural facts hold for **all** of them, and they are the reason the surfaces cannot be
rebuilt from the container alone:

1. **Every panel-anchoring rect is zero.** The page and group that own a control panel carry
   `posX=posY=width=height=0`; the plugin (or paf) writes the projection at runtime. **FC**
   Example: `videoplayer_control_panel_page` @0xc094 and `videoplayer_cp_group` @0xc0e0 are all zero;
   `control_panel_base` in `explore_plugin_full.rco` @0x38694 is all zero and carries
   `image=tex_cp_panel 72x72`.
2. **Button positions are grid cells, not pixels.** `posX`/`posY` on the control-panel buttons are
   small integers that index a grid inside the group. Video uses columns -6..+6 and rows +1..-1;
   audio uses -3..+3 and +1..-1; photo uses -7..+7 in steps of 2 and rows 2 and 0. **FC**. The
   pixel pitch of one cell is written by the plugin and is not in the container. **SO**
3. **Sizes that are declared are real.** Hit-area rects, progress-bar heights, icon sizes and text
   `size` values are present and are the firmware's own numbers. Those are what this document
   substitutes for the native guesses.
4. **Every player-panel text is white with the same shadow.** `topRGB=bottomRGB=(1,1,1)`,
   `shadowX=4`, `shadowY=-5`, `shadowRGB=(0.25,0.25,0.25)`, `shadowAlpha=1`, `spacing=0`,
   `size=10` for every label and every time digit. **FC** (uniform across all four containers).
   The unit of `size` is **not** established locally: `audit/TEXT-RENDERER.md:37-38` records that paf
   font setup is 72 DPI with a 7.02 fallback and that this "does not establish the final screen size";
   no local frame has been measured against an RCO `size`. So `size=10` must be read as a firmware
   *declaration*, not as 10 px. **SO** for the pixel equivalent.

---

## 3. Surface A - video player control panel

Defined by `videoplayer_plugin.rco` (the panel, time bar, status, volume page) and the sibling code
module `videoplayer_plugin.prx`. `videoplayer_util.rco` (6 objects) carries only strings.

### 3.1 Pages (FC, offsets are into the RCO tables stream)

| page | offset | children |
|---|---|---:|
| `videoplayer_full_screen_page` | 0xc048 | 0 |
| `videoplayer_control_panel_page` | 0xc094 | 1 (`videoplayer_cp_group`) |
| `videoplayer_display_infobar_page` | 0xd9c0 | 1 |
| `videoplayer_scene_search_page` | 0xe96c | 1 |
| `videoplayer_display_time_page` | 0xecf8 | 1 (`videoplayer_display_time_group`) |
| `videoplayer_status_page` | 0x11078 | 1 |
| `videoplayer_information_page` | 0x11484 | 1 |
| `videoplayer_change_icon_page` | 0x11888 | 1 |
| `videoplayer_volume_page` | 0x119e0 | 1 |
| `videoplayer_logo_page` | 0x11ae0 | 1 |
| `videoplayer_buffering_page` | 0x11bb8 | 1 |
| `videoplayer_simple_message_page` | 0x122a4 | 1 |
| `videoplayer_av_settings_page` | 0x12400 | 1 |
| `videoplayer_playend_menu_page` | 0x1292c | 2 |
| `videoplayer_navigate_store_page` | 0x12cf0 | 1 |

### 3.2 The control panel object tree

```
videoplayer_control_panel_page             Page    @0xc094
  videoplayer_cp_group                     Group   @0xc0e0   all geometry 0
    videoplayer_cp_button_group            Group   @0xc160   children = 25
      (25 buttons, table below)
    videoplayer_cp_button_name             Text    @0xd824   width=400 height=0 size=10
    videoplayer_cp_help                    Plane   @0xd934   no image in template
```

`videoplayer_cp_button_name` is one Text line for the focused button - width 400 declared, size 10,
white with the standard shadow (**CR**: the object name and the single instance say "the button's
name", but the code that fills it is not locally decompiled). `videoplayer_cp_help` is the hint-icon
plane; its images are the `tex_cp_help_*` set (table 3.4).

### 3.3 The 25 buttons (FC)

Each button is 228 bytes; `image`, `shadow`, `image2` are the normal, shadow and focus textures.
All icon textures are **70x70**, except `tex_cp_repeat*` which is **98x70**. Every button template
carries `alphaScale=0` (hidden) except where noted; the plugin raises them per availability.

| # | object | offset | cell (x,y) | icon | size |
|---|---|---|---:|---|---|
| 1 | `videoplayer_cp_dummy_button` | 0xc1e0 | -6, 1 | (none) | - |
| 2 | `videoplayer_cp_scene_search_button` | 0xc2c4 | -5, 1 | `tex_cp_scene_search` | 70x70 |
| 3 | `videoplayer_cp_goto_button` | 0xc3a8 | -4, 1 | `tex_cp_jump` | 70x70 |
| 4 | `videoplayer_cp_audio_change_button` | 0xc48c | -3, 1 | `tex_cp_audio` | 70x70 |
| 5 | `videoplayer_cp_subtitle_button` | 0xc570 | -2, 1 | `tex_cp_subtitle` | 70x70 |
| 6 | `videoplayer_cp_volume_control_button` | 0xc654 | -1, 1 | `tex_cp_volume` | 70x70 |
| 7 | `videoplayer_cp_av_settings_button` | 0xc738 | 0, 1 | `tex_cp_av_settings` | 70x70 |
| 8 | `videoplayer_cp_viewmode_button` | 0xc81c | 1, 1 | `tex_cp_screen_mode` | 70x70 |
| 9 | `videoplayer_cp_icon_change_button` | 0xc900 | 2, 1 | `tex_cp_change_icon` | 70x70 |
| 10 | `videoplayer_cp_del_button` | 0xc9e4 | 3, 1 | `tex_cp_delete` | 70x70 |
| 11 | `videoplayer_cp_display_display_button` | 0xcac8 | 4, 1 | `tex_cp_display` | 70x70 |
| 12 | `videoplayer_cp_beginning_button` | 0xcbac | -6, 0 | `tex_cp_prev` | 70x70 |
| 13 | `videoplayer_cp_next_button` | 0xcc90 | -5, 0 | `tex_cp_next` | 70x70 |
| 14 | `videoplayer_cp_fast_r_button` | 0xcd74 | -4, 0 | `tex_cp_search_b` | 70x70 |
| 15 | `videoplayer_cp_fast_f_button` | 0xce58 | -3, 0 | `tex_cp_search_f` | 70x70 |
| 16 | `videoplayer_cp_play_button` | 0xcf3c | -2, 0 | `tex_cp_play` | 70x70 |
| 17 | `videoplayer_cp_pause_button` | 0xd020 | -1, 0 | `tex_cp_pause` | 70x70 |
| 18 | `videoplayer_cp_stop_button` | 0xd104 | 0, 0 | `tex_cp_stop` | 70x70 |
| 19 | `videoplayer_cp_flash_r_button` | 0xd1e8 | 1, 0 | `tex_cp_flash_b` | 70x70 |
| 20 | `videoplayer_cp_flash_f_button` | 0xd2cc | 2, 0 | `tex_cp_flash_f` | 70x70 |
| 21 | `videoplayer_cp_slow_r_button` | 0xd3b0 | 3, 0 | `tex_cp_slow_b` | 70x70 |
| 22 | `videoplayer_cp_slow_f_button` | 0xd494 | 4, 0 | `tex_cp_slow_f` | 70x70 |
| 23 | `videoplayer_cp_oneframe_r_button` | 0xd578 | 5, 0 | `tex_cp_step_b` | 70x70 |
| 24 | `videoplayer_cp_oneframe_f_button` | 0xd65c | 6, 0 | `tex_cp_step` | 70x70 |
| 25 | `videoplayer_cp_repeat_button` | 0xd740 | 0, -1 | `tex_cp_repeat` | 98x70 |

The grid is 13 columns wide (x -6..+6) and 3 rows (+1, 0, -1), i.e. the real firmware panel is a
25-slot matrix in which only some slots are populated. **CR** (the grid is the declared cell
coordinates; the pixel pitch is runtime).

### 3.4 Textures (FC, all GIM 32bpp unless noted)

Control-panel icons, each with `_shadow` and `_focus` variants of the same size:

`tex_cp_scene_search`, `tex_cp_jump`, `tex_cp_audio`, `tex_cp_subtitle`, `tex_cp_volume`,
`tex_cp_av_settings`, `tex_cp_screen_mode`, `tex_cp_change_icon`, `tex_cp_delete`,
`tex_cp_display`, `tex_cp_prev`, `tex_cp_next`, `tex_cp_search_b`, `tex_cp_search_f`,
`tex_cp_play`, `tex_cp_pause`, `tex_cp_stop`, `tex_cp_flash_b`, `tex_cp_flash_f`,
`tex_cp_slow_b`, `tex_cp_slow_f`, `tex_cp_step_b`, `tex_cp_step`, `tex_cp_help` = **70x70**
`tex_cp_repeat` = **98x70**

Direct-key hint strip, all **86x30**:
`tex_cp_help_circle`, `tex_cp_help_cross`, `tex_cp_help_l1`, `tex_cp_help_l2_left`,
`tex_cp_help_r1`, `tex_cp_help_r2_right`, `tex_cp_help_select`, `tex_cp_help_start`

Progress and seek:
| texture | size | used by |
|---|---|---|
| `tex_progress_bar` | 30x20 | `videoplayer_display_time_bar_base` / `..._bar` |
| `tex_progress_bar_in` | 30x20 | fill of the bar |
| `tex_progress_bar_jump_indi` | 20x12 | `videoplayer_display_time_jump_time_bar_marker` @0x10c3c |
| `tex_scroll_bar` | 20x30 | `videoplayer_display_scroll_bar` |
| `tex_scroll_bar_base` | 20x30 | base |
| `tex_shuttle_ring` / `_bg` / `_s` / `_e` | 96x96 | seek shuttle ring |
| `tex_i_upper_bar` | 1x200 | `videoplayer_display_upper_base` @0xda8c |
| `tex_i_under_bar` | 1x200 | `videoplayer_display_under_base` @0xedc4 |
| `tex_i_carpet` | 34x46 | codec bg, information bg, scene-search bg, playend bg |
| `tex_wait_bar` | 90x38 | buffering |
| `tex_wait_bar_shadow` | 90x48 | buffering |
| `tex_loading_icon` | 90x90 | loading |
| `tex_store` | 160x160 | playend / navigate-store icon |
| `tex_divx_logo` | 173x72 | logo page |

Status and badge art:
| texture | size |
|---|---|
| `tex_i_play`, `tex_i_pause`, `tex_i_stop`, `tex_i_prev`, `tex_i_next`, `tex_i_jump`, `tex_i_search_b`, `tex_i_search_f`, `tex_i_slow_b`, `tex_i_slow_f`, `tex_i_flash_b`, `tex_i_flash_f`, `tex_i_step`, `tex_i_step_b` | 74x74 |
| `tex_i_repeat`, `tex_i_title_repeat` | 98x74 / 72x36 |
| `tex_i_10`, `tex_i_15`, `tex_i_30`, `tex_i_120`, `tex_i_f15`..`tex_i_f180` | 72x36 (speed badges) |
| `tex_i_sound`, `tex_i_subtitle`, `tex_i_screen_mode` | 40x40 |
| `tex_icon_focus` | 27x27 |
| `tex_vc_*` (codec labels: none/mpeg1/mpeg2/mpeg4/mpeg4avc/motionjpeg/divx/vc_1) | 110x28 or 120x28 |
| `tex_ac_*` (audio codec labels: aac/atrac/dd/lpcm/mp3/mpeg/nonlinearpcm/wma) | 110x28 or 120x28 |

### 3.5 Declared geometry in the time bar page (FC - the only non-zero rects in the panel)

| object | offset | posX | posY | width | height | note |
|---|---|---:|---:|---:|---:|---|
| `videoplayer_display_chapter_button` | 0xeed0 | 0 | 0 | **200** | **64** | chapter hit area |
| `videoplayer_display_time_button` | 0xf34c | **250** | 0 | **750** | **64** | time-bar hit area |
| `videoplayer_display_chapter_text` | 0xefb4 | 0 | 0 | 16 | 16 | "Chapter" label cell |
| `videoplayer_display_chapter_num` | 0xf0c4 | 0 | 0 | 16 | 16 | chapter number cell |
| `videoplayer_display_chapter_num_spin` | 0xf1d4 | 0 | 0 | 16 | 20 | ItemSpin |
| `videoplayer_display_sign` | 0xf430 | 0 | 0 | 16 | 16 | sign cell |
| `videoplayer_display_playing_time_h1..c2` (8 cells) | 0xf540..0xfcb0 | 0 | 0 | 16 | 16 | one digit per cell, step 0x110 |
| `videoplayer_display_play_time_h1..c2` (8 cells) | 0xfdc0..0x10530 | 0 | 0 | 16 | 16 | duration digits |
| `videoplayer_display_time_jump_hour/minute/second` | 0x10640/0x10738/0x10830 | 0 | 0 | 0 | 0 | ItemSpin, `unknownFloat28=32` |
| `videoplayer_display_time_bar_base` | 0x109a8 | 0 | 0 | 0 | 0 | Progress, max `unknownFloat23=100` |
| `videoplayer_display_time_bar_playable_time` | 0x10a48 | 0 | 0 | 0 | 0 | Progress, max 100 |
| `videoplayer_display_time_bar` | 0x10ae8 | 0 | 0 | 0 | 0 | Progress, max 100 |
| `videoplayer_display_scroll_bar` | 0x10b88 | 0 | 0 | 0 | 0 | Scroll, max 100/100 |
| `videoplayer_display_time_jump_time_bar_marker` | 0x10c3c | 0 | 0 | 0 | 0 | Plane, `tex_progress_bar_jump_indi` 20x12 |

Reading of this table (**CR**, labelled): the time scrub area is declared 750x64 anchored at
x=250 inside its group, and the chapter area 200x64 at x=0; the 16x16 digit cells are the
firmware's per-glyph layout unit for the time readout, i.e. the real time display is a row of
16x16 cells, not one right-aligned string. The group/page transform is runtime, so 250/750 are
group-local, not screen coordinates. **SO** for the screen position.

### 3.6 Strings (FC, `--rco-texts`)

| id | text |
|---|---|
| `msg_play_with_icon` | `Play ` |
| `msg_pause_with_icon` | `Pause ` |
| `msg_stop` | `Stop` |
| `msg_next_with_icon` | `Next ` |
| `msg_prev_with_icon` | `Previous ` |
| `msg_beginning_with_icon` | `Return to Beginning ` |
| `msg_fast_f_with_icon` | `Fast Forward ` |
| `msg_fast_r_with_icon` | `Fast Reverse ` |
| `msg_flash_f` | `Instant Advance` |
| `msg_flash_r` | `Instant Replay` |
| `msg_slow_f` | `Slow (Forward)` |
| `msg_slow_r` | `Slow (Reverse)` |
| `msg_goto` | `Go To` |
| `msg_scene_search` | `Scene Search` |
| `msg_chapter` / `msg_chapter2` | `Chapter` |
| `msg_subtitle_change` | `Subtitle Options` |
| `msg_audio_change` | `Audio Options` |
| `msg_av_settings` | `AV Settings` (`msg_audio_video_settings`) |
| `msg_viewmode` | `Screen Mode` |
| `msg_repeat` | `Repeat` |
| `msg_repeat_ab` | `A-B Repeat` |
| `msg_repeat_off` / `msg_repeat_on` / `msg_repeat_title` | `Repeat Off` / `Repeat On` / `Title Repeat` |
| `msg_volume_control` | `Volume Control` |
| `msg_percent` | `%1%%` |
| `msg_icon_change` | `Change Icon` |
| `msg_display_display_with_icon` | `Display ` |
| `msg_del` | `Delete` |
| `msg_buffering` | `Buffering...` |
| `msg_ch_type` | `%1 Ch.` |
| `msg_datetime_num1` / `num2` | `%1/%2/%3  %4:%5` / `%1/%2/%3  %4:%5:%6` |

The trailing space in the `_with_icon` strings is firmware data: those strings are followed by a
glyph, so the label is "Play" + icon, not a bare word.

### 3.7 Colours and alpha (FC)

- Panel text: white `(1,1,1)` top and bottom gradient, shadow `(4,-5)` at `(0.25,0.25,0.25)` with
  `shadowAlpha=1`. Uniform for all 43 Text objects in this container.
- Buttons: `redScale=greenScale=blueScale=alphaScale=0` in the template (fully invisible until the
  plugin publishes them); `scaleWidth/Height/Depth=1`.
- `videoplayer_cp_volume_control_button` is the one button whose template shows the tint-off state
  (`alphaScale=0`) while the equivalent audio button uses `alphaScale=0.8` - so the video panel
  publishes visibility per-button at runtime. **FC**
- Planes: same shape; e.g. `videoplayer_display_upper_base` `alphaScale=0` and `tex_i_upper_bar`.

### 3.8 Fade / hide timing

**Not in the container.** `videoplayer_plugin.rco` has **zero** animation entries (`animations=0`
in `resources.json` for that resource), so no Fade/MoveTo/Delay object carries a panel timing.
**FC**

What is traceable in code is the XMB music panel, not the video panel: see section 4.3 (fade 150 ms,
delay 0). For the video panel the timing lives in `videoplayer_plugin.prx`, which has **no local
decompile and no local Ghidra program** (`ghidra_proj/vsh493.rep` holds vsh.elf, raf, qglbase,
mms*, paf_* and a few x3_* modules - not videoplayer_plugin). **SO**

### 3.9 Sound per control

No control-panel sound name exists in `videoplayer_plugin.prx`: an ASCII string scan of the local
decrypted module finds **no** `snd_*` names at all (the same scan finds none in
`audioplayer_plugin.prx` or `photoviewer_plugin.prx`). **FC/FA**

The only firmware-documented panel-open sound is on the XMB music panel:
`explore_plugin_all.c:38936` calls `FUN_001fdf4c("snd_system_ok")` immediately after the control
panel is built. `explore_plugin` uses `snd_cursor` (9245, 9318), `snd_system_ok` (22617),
`snd_cancel` (26546) and `snd_decide` (33256 and others) for generic navigation. **FA**

So: a per-control sound map for the video panel is **SO**; what the firmware does prove is that the
XMB owns one cue set (`snd_cursor`, `snd_decide`, `snd_cancel`, `snd_system_ok`) and that opening the
music control panel plays `snd_system_ok`.

---

## 4. Surface B - music playback panel

Two different surfaces exist in the firmware, and they are not the same object set:

- **B1, the player's own panel**: `audioplayer_plugin.rco` - 14 buttons, a title page, a progress page.
- **B2, the XMB panel** (what the console draws while music plays in the XMB):
  `explore_plugin_full.rco` `control_panel_page` - a 72x72 panel plane, 7 buttons, one 200x6
  progress bar, one label.

### 4.1 B1 - `audioplayer_plugin.rco` (FC)

```
full_screen_page            Page   @0x4e24
back_screen_page            Page   @0x4e70   -> back_screen_plane @0x4ebc  image tex_back_screen 1x1, alphaScale=0
control_panel_page          Page   @0x4f48
  audioplayer_cp_group      Group  @0x4f94
    audioplayer_cp_button_group  Group @0x5014  children=14
    button_description_label     Text  @0x5d0c  posY=-2  width=300  size=10
title_information_page      Page   @0x5e1c  -> title_information_group @0x5e68 (5 children)
  plane_jacket_music_cover  Plane  @0x5ee8
  plane_track_number1/2     Plane  @0x5f74 / @0x6000
  text_contents_title_name  Text   @0x608c  size=10
  text_artist_name          Text   @0x619c  size=10
play_progress_page          Page   @0x62ac
  play_progress_group       Group  @0x62f8  children=6
    text_track_number       Text   @0x6378
    text_current_time       Text   @0x6488
    play_progress_group_play_bar  Group @0x6598  children=8 -> text_h1/h2/c1/m1/m2/c2/s1/s2_time
    audioplayer_display_time_bar  Progress @0x74f4  height=20, max=100
    play_progress_group_str_kind  Group @0x6e98 -> text_ap_num / text_ap_co / text_ap_sp
audioplayer_volume_page     Page   @0x7bd8  -> audioplayer_volume_level_meter @0x7c24
```

The 14 buttons (FC), all icons 70x70 except repeat/shuffle at 98x70:

| object | offset | cell | icon |
|---|---|---|---|
| `msg_volume_control_button` | 0x5094 | -2, 1 | `tex_cp_volume` |
| `msg_visualizer_with_icon_button` | 0x5178 | -1, 1 | `tex_cp_vislizer` |
| `msg_add_to_playlist_button` | 0x525c | 0, 1 | `tex_cp_playlist` |
| `msg_del_button` | 0x5340 | 1, 1 | `tex_cp_delete` |
| `msg_display_display_with_icon_button` | 0x5424 | 2, 1 | `tex_cp_showinfo` |
| `msg_prev_with_icon_button` | 0x5508 | -3, 0 | `tex_cp_prev` |
| `msg_next_with_icon_button` | 0x55ec | -2, 0 | `tex_cp_next` |
| `msg_fast_r_with_icon_button` | 0x56d0 | -1, 0 | `tex_cp_searchb` |
| `msg_fast_f_with_icon_button` | 0x57b4 | 0, 0 | `tex_cp_searchf` |
| `msg_play_with_icon_button` | 0x5898 | 1, 0 | `tex_cp_play` |
| `msg_pause_with_icon_button` | 0x597c | 2, 0 | `tex_cp_pause` |
| `msg_stop_button` | 0x5a60 | 3, 0 | `tex_cp_stop` |
| `msg_repeat_button` | 0x5b44 | -1, -1 | `tex_cp_repeat` 98x70 |
| `msg_shuffle_button` | 0x5c28 | 0, -1 | `tex_cp_shuffle` 98x70 |

Declaration detail (FC): every audio button carries `alphaScale=0.8` (not 0 like the video panel),
so the audio panel is always faintly present in the template. Icons: `tex_cp_volume`,
`tex_cp_vislizer`, `tex_cp_playlist`, `tex_cp_delete`, `tex_cp_showinfo`, `tex_cp_prev`,
`tex_cp_next`, `tex_cp_searchb`, `tex_cp_searchf`, `tex_cp_play`, `tex_cp_pause`, `tex_cp_stop`
70x70; `tex_cp_repeat`, `tex_cp_shuffle` 98x70.

Music-only textures (FC): `tex_track_frame` 240x240, `tex_track_number0..9` 46x68,
`tex_default_music_cover` 162x162, `tex_back_screen` 1x1, status art `tex_play_status` 74x74,
`tex_pause_status` 74x74, `tex_stop_status` 74x74, `tex_next_status` 74x74, `tex_prev_status` 74x74,
`tex_repeat_status` 98x74, `tex_1repeat_status` 72x36, `tex_shuffle_status` 98x74,
`tex_searchb_status` 74x74, `tex_searchf_status` 74x74, codec labels `tex_mp3`/`tex_pcm`/`tex_aac`/
`tex_wma`/`tex_atrac`/`tex_cdda` 110x28 or 120x28.

### 4.2 B2 - the XMB panel, `explore_plugin_full.rco` (FC)

```
control_panel_page           Page   @0x38648
  control_panel_base         Plane  @0x38694   image tex_cp_panel 72x72   (stretchable plate)
    control_play_status_group   Plane @0x38720  children=10  (status icons, alphaScale=0 each)
      plane_play_status        Plane @0x387ac  tex_play_status 74x74
      plane_prev_status        Plane @0x38838  tex_prev_status 74x74
      plane_next_status        Plane @0x388c4  tex_next_status 74x74
      plane_pause_status       Plane @0x38950  tex_pause_status 74x74
      plane_stop_status        Plane @0x389dc  tex_stop_status 74x74
      plane_searchb_status     Plane @0x38a68  tex_searchb_status 74x74
      plane_searchf_status     Plane @0x38af4  tex_searchf_status 74x74
      plane_repeat_status      Plane @0x38b80  tex_repeat_status 98x74
      plane_1repeat_status     Plane @0x38c0c  tex_1repeat_status 72x36
      plane_shuffle_status     Plane @0x38c98  tex_shuffle_status 98x74
    control_progress_bar_group Plane @0x38d24  children=2
      progress_bar_playing_time   Progress @0x38db0  width=200 height=6  max=100
      label_playing_time          Text     @0x38e50  size=10
    control_button_group      Plane  @0x38f60  children=7
      msg_prev_with_icon_button    Button @0x38fec  cell 0,0   tex_cp_prev 90x60 / tex_cp_focus 90x60
      msg_next_with_icon_button    Button @0x390d0  cell -1,0  tex_cp_next
      msg_pause_button             Button @0x391b4  cell -2,0  tex_cp_pause
      msg_stop_button              Button @0x39298  cell -3,0  tex_cp_stop
      msg_repeat_button            Button @0x3937c  cell -4,0  tex_cp_repeat
      msg_shuffle_button           Button @0x39460  cell -5,0  tex_cp_shuffle
      msg_volume_control_button    Button @0x39544  cell -6,0  tex_cp_volume
notification_page -> notification_text  Text @0x39774  size=10
volume_page @0x39628 -> volume_level_meter @0x39674
```

Concrete firmware numbers available for B2 (**FC**):

- panel plate: `tex_cp_panel` 72x72, a stretchable plane (its declared w/h are 0, so paf sizes it
  from the image and the plugin then scales it);
- buttons: 7, icons 90x60, `image2` is the shared `tex_cp_focus` (not a per-button focus texture);
- progress bar: **200 x 6**, Progress max 100;
- labels: `label_playing_time` and `notification_text`, both `size=10`, white with the standard
  shadow;
- status art: 74x74 (98x74 for repeat/shuffle, 72x36 for the "1" repeat badge).

### 4.3 B2 timing, scale and sound - recovered from code (FA + FD)

`explore_plugin_all.c:38787-38821` (`FUN_0006278c`) builds the panel:

| line | what it does |
|---|---|
| 38796 | `paf_paf_3CEC3833(page,"control_panel_page",0,0,0x30,0,0)` - page setup |
| 38798 | `DAT_002c94f4 = paf_paf_11E195B3(page,"control_panel_page")` |
| 38800 | `FUN_00010314(0x270a68, page)` - page value then play event `0x1000003` |
| 38801-38811 | reads three floats of `control_panel_base` at +0x140/+0x144/+0x148, multiplies each by `0x270a54`, calls `FUN_00060a68` (set scale vector, fire `0x1000005`) |
| 38812-38814 | `FUN_00027c64(0x270a58, 0x270a5c, w, h, d, 0x270a50, base, 0x1000005)` - the move/size animation |
| 38816 | `FUN_00027c14(0x270a58, 0x270a5c, base, 0x1000003)` - the fade |

`FUN_00027c14(duration, delay, object, event)` forwards to the paf vtable slot at +0x13c, i.e. paf's
animation-start helper; `FUN_00060a68` builds a scale vector and fires `0x1000005`.

Constants read from the module's own data segment with `work/elf-const.ts` (**FD**):

| address | value | role |
|---|---:|---|
| 0x270a00 | **150** | fade duration passed for the volume meter and the notification text |
| 0x270a04 | **0** | fade delay |
| 0x270a1c | 200 | (used elsewhere in the same table) |
| 0x270a30 | **150** | value handed to the panel's visible-time helper (`FUN_000612f4`) |
| 0x270a34 | 480 | a reference size inside the child-measurement loop of `FUN_00061a88` (`:38545-38560`), compared against an object's projected extent |
| 0x270a38 | **1920** | screen width |
| 0x270a3c | **1080** | screen height |
| 0x270a50 | 1 | alpha argument of the move animation |
| 0x270a54 | **1.3** | scale factor applied to the panel base's own w/h |
| 0x270a58 | **150** | duration of the panel move/fade |
| 0x270a5c | **100** | delay of the panel move/fade |
| 0x270a60 | 0.8333 | (same table) |
| 0x270a64 | 0.25 | (same table) |
| 0x270a68 | 0 | page value written before the fade |

So the firmware's panel presentation is: **fade in over 150 ms with 0 delay**, panel scaled by
**1.3** against its own size, then a 150-unit visible-time timer, in a 1920x1080 design space.

Caveat, stated plainly: 1920/1080 are certainly the screen bounds, but the *unit* of 150 is not
proven to be milliseconds. `FUN_000612f4` (`:38405-38433`) hangs a helper and a callback on the value
150 and is fired with the same constant that edges the animation, so 150 is at minimum an animation
length; a 150 ms *visible* time would be far too short for a panel, and a 150-frame time at 60 Hz is
2.5 s, which is in the right order. **SO** for the visible-time unit; 150 ms for the *fade* is the
consistent reading of it being passed to the animation helper beside 0 delay.

Sound: `:38936` `FUN_001fdf4c("snd_system_ok")` right after the panel is constructed (**FA**).

### 4.4 B2 strings (FC)

`msg_prev_with_icon` `Previous `, `msg_next_with_icon` `Next `, `msg_pause_with_icon` `Pause `,
`msg_play_with_icon` `Play `, `msg_stop` `Stop`, `msg_repeat` `Repeat`, `msg_shuffle` `Shuffle`,
`msg_volume_control` `Volume Control`, `msg_visualizer_with_icon` `Visual Player `,
`msg_add_to_playlist` `Add to Playlist`, `msg_del` `Delete`, `msg_track_number_value` `Track %1`,
`msg_track_count_value` `%1 Track`, `msg_tracks_count_value` `%1 Tracks`,
`msg_clock_volume`/`msg_chat_volume` exist in this container too (used by chat, not the player).

### 4.5 What B does *not* contain

- No percentage string, no numeric volume label, and no volume caption anywhere on the volume page
  or the volume panel (see section 5). **FC**
- No animation entries at all in `explore_plugin_full.rco` for these pages, so the 150 ms is code
  data, not container data. **FC**

---

## 5. Surface C - the volume display

This is the surface the app replaced with a fitted 222x58 HUD. The firmware answer is short and it
is not flattering to the HUD.

### 5.1 What exists (FC)

Eight containers define a volume page. Every one of them holds a `Meter` (typeId 0x81e, 180 bytes)
and **nothing else that draws**: no label, no value text, no plate, no border, no icon.

| container | page | offset | meter | offset | meter parent |
|---|---|---|---|---:|---|
| `videoplayer_plugin.rco` | `videoplayer_volume_page` | 0x119e0 | `videoplayer_volume_level_meter` | 0x11a2c | page |
| `audioplayer_plugin.rco` | `audioplayer_volume_page` | 0x7bd8 | `audioplayer_volume_level_meter` | 0x7c24 | page |
| `explore_plugin_full.rco` | `volume_page` | 0x39628 | `volume_level_meter` | 0x39674 | page |
| `explore_category_music.rco` | `volume_page` | 0x11aa8 | `volume_level_meter` | 0x11af4 | page |
| `sacd_plugin.rco` | `sacdplayer_volume_page` | 0x8c08 | `sacdplayer_volume_level_meter` | 0x8c54 | page |
| `bdp_plugin.rco` | `page_bdpui_audiovolumecontrol` | 0x1f354 | `bdpui_audiovolumecontrol_level_meter` | 0x1f590 | plane `bdpui_audiovolumecontrol_all` |
| `avc_plugin.rco` | - (no page) | - | `volume_level_meter` | 0xe120 | `plane_ctl_list` |
| `avc2_text_plugin.rco` | `avc2_volume_control` | 0x9120 | `avc2_control_lv_meter` | 0x916c | page |

The `bdp_plugin` page is the only one that wraps the meter in anything: page -> group @0x1f3a0 ->
plane `bdpui_audiovolumecontrol_all` @0x1f420 (2 children: an alpha-0 dummy button @0x1f4ac and the
meter). It still contains no text. **FC**

Each page's child count is 1; each meter's `posX/posY/posZ/width/height/depth` is 0 in the template.
**So the RCO defines the volume display as a single level meter whose rect is written at runtime.
There is no panel rect in firmware data at all.** **FC**

### 5.2 The meter is a paf primitive, and its art is in `system_plugin.rco`

`paf-range.c:23812` and `:23821` return `PTR_s_PhLevelMeter_006fb220`; `:24679` compares against it.
The Meter widget class is therefore paf's `PhLevelMeter`, implemented in `vsh.elf` (the class-name
string is present at vsh.elf file offset 6,811,464). **FA**

The segment art is the console's default level-meter set, in `system_plugin.rco` (**FC**):

| texture | size | role |
|---|---|---|
| `tex_default_lvmeter_on` | **20x40** | a lit segment |
| `tex_default_lvmeter_off` | **20x40** | an unlit segment |
| `tex_default_lvmeter_focus` | **20x40** | focused segment |
| `tex_default_lvmeter_bg` | 48x48 | the meter's backing plate |

Those four names are the only `lvmeter` textures in the whole 122-container set, and
`system_plugin.rco` also holds the only `tex_default_lvmeter_*` labels. **This is the real meter:
a row of 20x40 segments.** The meter's own images in the player containers are unset
(`unknownRef28..34 = 0xffff0000ffffffff`), which is consistent with paf applying the defaults.

Template field detail (FC), identical shape in the player containers:

    Meter: unknown23 = -4, unknown24 = 4, unknown25 = 0, unknown26 = 0x04000000,
           unknown27 = <label offset>, then 4 unset refs

(`avc_plugin` differs: 0, 7, 3.) Treat `-4 / 4` as declared integer fields of the meter whose
meaning is not established; do not turn them into pixel numbers. **SO**

### 5.3 What the firmware volume display does *not* have

- No caption. `msg_volume_control` ("Volume Control") exists in `videoplayer_plugin`,
  `audioplayer_plugin`, `sacd_plugin`, `bdp_plugin`, `avc_plugin`, `avc2_text_plugin`,
  `explore_plugin_full` and `explore_category_music`, but it is the label of the volume **button**
  (`msg_volume_control_button` / `videoplayer_cp_volume_control_button`), i.e. the button's hint
  text in the control panel - not a caption on the volume page. The volume page has no Text child.
  **FC**
- No percentage readout. The only percent format string in the player set is
  `msg_percent = "%1%%"` in `videoplayer_plugin.rco`, and no Text object on any volume page
  references it. **FC**
- No plate, no border, no background fill: the page's only child is the meter; whatever backing
  exists is `tex_default_lvmeter_bg` inside the widget. **FC**
- No per-container animation, so no fade in the container. **FC**

### 5.4 The real volume-display geometry, stated as far as it goes

| property | firmware value | class |
|---|---|---|
| structure | 1 page + 1 `PhLevelMeter`, nothing else that draws (bdp adds an alpha-0 dummy button) | FC |
| segment art | `tex_default_lvmeter_on/off/focus`, 20x40 each | FC |
| backing art | `tex_default_lvmeter_bg`, 48x48 | FC |
| segment pitch | 20 px (the segment texture width) if segments abut; not proven | SO |
| segment count | not in the container; `unknown24 = 4` is the only candidate field | SO |
| meter rect | runtime; not in the container | SO |
| label / value | none | FC |
| font size | none (no text object) | FC |
| fade in | 150 ms, delay 0 (`0x270a00`, `0x270a04`) through the paf animation helper | FA + FD |
| visible time | 150 (`0x270a30`); unit unproven | SO |
| sound | `snd_system_ok` on the XMB panel open (`:38936`) | FA |

**Headline for section C: the firmware's XMB volume display is a bare row of 20x40 level segments
with a 48x48 backing, faded in over 150 ms. It has no caption, no numeric value, no rounded plate
and no 1 px border.** The native 222x58 HUD - label at 15 px, value at 19 px, 196x6 track, 20-alpha
plate, `tex_infobar` at 0.12, 1 px `(255,255,255,90)` border, 3000 ms + 250 ms fade - is invented
end to end, and the section-6.8 note in `audit/HTPC-IDEAS.md` that says only the *rect* is fitted is
too generous: the contents and the material are fitted as well.

---

## 6. Surface D - photo viewer chrome

`photoviewer_plugin.rco` defines a substantial chrome that the native build does not draw at all.
**FC**

### 6.1 Pages

| page | offset | children |
|---|---|---:|
| `PvPage1` | 0x93b0 | 0 |
| `Pv_front_screen_page` | 0x93fc | 1 (`Pv_front_screen_plane`) |
| `Pv_back_screen_page` | 0x94d4 | 1 (`Pv_back_screen_plane`) |
| `control_panel_page` | 0x95ac | 1 (`view_controlpanel_group`) |
| `controlpanel_slideshow_page` | 0xa900 | 1 (`slideshow_controlpanel_group`) |

`Pv_front_screen_plane` @0x9448 and `Pv_back_screen_plane` @0x9520 are the only objects in the whole
media set that declare a **full-screen** rect: `width=1920`, `height=1080`, with `tex_front_screen`
and `tex_back_screen` **1x1** (stretchable). **FC**

### 6.2 View control panel - 20 buttons + label + hint plane

`view_controlpanel_group` @0x95f8 (21 children). Grid: columns -7..+7 in **steps of 2**, rows
`y=2` and `y=0`. All icons **70x70**. **FC**

| row | objects (cell) |
|---|---|
| y=2 | `displaymode_button` (-7) `tex_cp_viewmode`; `effect_change_button` (-5) `tex_cp_effect_change`; `trimming_button` (-3) `tex_cp_trimming`; `add_to_playlist_button` (-1) `tex_cp_playlist`; `print_button` (1) `tex_cp_print`; `set_as_wallpaper_button` (3) `tex_cp_wpaper`; `del_button` (5) `tex_cp_delete`; `display_display_with_icon_button` (7) `tex_cp_showinfo` |
| y=0 | `zoom_in_button` (-7) `tex_cp_zoom_in`; `zoom_out_button` (-5) `tex_cp_zoom_out`; `rotate_left_with_icon_button` (-3) `tex_cp_rotate_left`; `rotate_right_with_icon_button` (-1) `tex_cp_rotate_right`; `up_button` (1) `tex_cp_cursor_up`; `down_button` (3) `tex_cp_cursor_down`; `left_button` (5) `tex_cp_cursor_left`; `right_button` (7) `tex_cp_cursor_right`; `prev_with_icon_button`; `next_with_icon_button`; `slideshow_with_icon_button` (`tex_cp_play`) |

Offsets for the first eight (FC): 0x9678, 0x975c, 0x9840, 0x9924, 0x9a08, 0x9aec, 0x9bd0, 0x9cb4;
then 0x9d98, 0x9e7c, 0x9f60, 0xa044, 0xa128, 0xa20c, 0xa2f0, 0xa3d4 (stride 0xe4).

`button_description_label_incpv` @0xa764: Text, `width=300`, `height=9`, `size=10`.
`direct_key_icon` @0xa874: Plane, no image in template.

### 6.3 Slideshow control panel - 11 children

`slideshow_controlpanel_group` @0xa94c: `slideshow_change_button` @0xa9cc (`tex_cp_slideshowoption`),
`slide_speed_button` @0xaab0 (`tex_cp_slide_speed`), `sld_prev_with_icon_button` @0xab94
(`tex_cp_prev`), `sld_next_with_icon_button` @0xac78 (`tex_cp_next`), `play_with_icon_button` @0xad5c
(`tex_cp_play`), `pause_with_icon_button` @0xae40 (`tex_cp_pause`), `stop_button` @0xaf24
(`tex_cp_stop`), `repeat_button` @0xb008 (`tex_cp_repeat`, **98x70**),
`button_description_label_incps` @0xb0ec (Text, `width=300`, `size=10`), `sld_direct_key_icon` @0xb1fc.
All icons 70x70 except repeat 98x70. **FC**

### 6.4 Indicator and direct-key art (FC)

- Direct-key hint icons, all **86x30**: `tex_dki_circle`, `tex_dki_cross`, `tex_dki_square`,
  `tex_dki_triangle`, `tex_dki_up`, `tex_dki_down`, `tex_dki_left`, `tex_dki_right`, `tex_dki_l1`,
  `tex_dki_l2`, `tex_dki_l2_left`, `tex_dki_l3`, `tex_dki_l3_nagaoshi`, `tex_dki_r1`, `tex_dki_r2`,
  `tex_dki_r2_right`, `tex_dki_r3`, `tex_dki_select`, `tex_dki_start`.
- Status: `tex_play_status` 74x74 (+`_focus`,`_shadow` 70x70), `tex_pause_status` 74x74,
  `tex_repeat_status` 98x70 (+focus/shadow), `tex_indi_Transition` 40x40, `tex_bar_photo_icon` 46x46.
- Thumbnails: `tex_thumbnail_frame` 12x12 (nine-slice frame), `tex_thumbnail_frame_shadow` 18x18,
  `tex_thumbnail_shadow` 21x21. 16 `IItem` rows exist in the container for the strip.
- `multiangle_icon` is the only PNG in the container (224x80).

### 6.5 Strings (FC)

`msg_zoom`, `msg_rotate`-family labels exist as button labels; the panel's text objects are dynamic.
`msg_repeat`, `msg_slideshow`-family and `msg_effects` strings are present in this container's table
(125 entries). The panel labels are the same white text with the standard `(4,-5)` / `0.25` shadow
and `size=10`.

### 6.6 What D does not contain

No animation entries in `photoviewer_plugin.rco` (**FC**), so zoom/rotate/slideshow indicators have no
container-side timing; and no panel rect outside the two 1920x1080 screen planes. There is also no
`snd_*` string anywhere in the local `photoviewer_plugin.prx` (**FC**), so no per-control sound can be
quoted for this surface either; the console's cue set (`snd_cursor`, `snd_decide`, `snd_cancel`,
`snd_system_ok`) is the only sound vocabulary the local artifacts support (`explore_plugin_all.c:9245,
9318, 26546, 33256`).

---

## 7. Native vs firmware diff per surface

Native line numbers are from the tree as it stands (read, not run). "Firmware" columns quote the
sections above.

### 7.1 Surface A - video player control panel

| aspect | firmware | native today | change |
|---|---|---|---|
| structure | 25-slot button matrix + separate time-bar page + separate status page (`videoplayer_plugin.rco`) | one text overlay: title, queue line, hint line, 4 px bar, one time string (`view.cpp:1189-1198`) | draw the panel as a button row from `videoplayer_cp_button_group`; keep text overlay only for the error/buffering path |
| buttons | 25 named buttons, each `tex_cp_*` 70x70 with `_shadow` + `_focus` | none - no icon is drawn | draw `tex_cp_play`/`_pause`/`_stop`/`_prev`/`_next`/`tex_cp_volume`/`tex_cp_subtitle`/`tex_cp_audio`/`tex_cp_display`/`tex_cp_repeat` with their shadow/focus triple |
| play/pause | `videoplayer_cp_play_button` @0xcf3c / `videoplayer_cp_pause_button` @0xd020 | one key binding, no visible control (`view.cpp:1943-1947`) | a state-driven play/pause button; same input, drawn surface |
| progress bar | 3 stacked Progress widgets (base, playable, playing) + Scroll + jump marker (`tex_progress_bar` 30x20 nine-slice, `_in` fill 30x20, `tex_progress_bar_jump_indi` 20x12) | flat 4 px rect, `QColor(255,255,255,60)` track, white fill (`view.cpp:1196`) | nine-slice `tex_progress_bar` + `_in` fill + jump marker plane |
| time text | 8 + 8 digit cells of 16x16, `size=10`, sign cell, `msg_percent` available | single 23 px right-aligned `MM:SS / MM:SS` at `{1450,963,360,45}` (`view.cpp:1198`) | per-cell digits at 16x16 with the firmware size; keep `MediaControls::formatTime` only as the digit source |
| title | `videoplayer_cp_button_name` (width 400, size 10) plus `videoplayer_information_string` | 27 px white at `{110,970,1500,50}` (`view.cpp:1189`) | move to the panel's own label object size and rect |
| button hint strip | `tex_cp_help_*` 86x30 per key (`circle`, `cross`, `l1`, `l2_left`, `r1`, `r2_right`, `select`, `start`) | one long ASCII hint sentence at 21 px (`view.cpp:1190`) | draw the 86x30 hint icons instead of spelling keys out |
| status icons | `tex_i_play/_pause/_stop/_prev/_next/_repeat` 74x74, speed badges 72x36 | none | status page icons |
| fade/hide | 3000 ms visible + 250 ms linear fade (`media_controls.cpp:8-9`), not firmware-derived | same | **do not change until the video panel's timing is decompiled** (section 3.8, SO) |
| sound | no per-control name in the plugin; system cue set only | `InteractionAudio::Cue::Options/Move` from `system_plugin.rco` (`interaction_audio.cpp:32-43`) | unchanged; per-control sound is SO |

### 7.2 Surface B - music playback panel

| aspect | firmware | native today | change |
|---|---|---|---|
| existence | full panel: 14 buttons (player) / 7 buttons + 200x6 bar + status icons (XMB) | **none**. `view.cpp:2044-2050` states it outright: "Music, which has no player panel, keeps Triangle as ... cycle repeat" | build the XMB variant B2: `control_panel_page`, `control_panel_base` + `tex_cp_panel`, 7 buttons at `tex_cp_*` 90x60 with shared `tex_cp_focus`, `progress_bar_playing_time` 200x6 |
| panel plate | `tex_cp_panel` 72x72 stretchable, scaled 1.3 (`0x270a54`) | none | nine-slice `tex_cp_panel` at the panel's declared size x 1.3 |
| progress bar | 200x6, max 100 (`explore_plugin_full.rco` @0x38db0) | none | 200x6 bar; position from the panel rect |
| now playing text | `label_playing_time` size 10, `notification_text` size 10 | `mediaQueueSummary()` printed at 21 px `{110,895,1700,36}` (`view.cpp:1192`) | use the panel's label object and its declared size; keep the queue summary as the string source |
| status icons | 10 planes: play/prev/next/pause/stop/searchb/searchf/repeat/1repeat/shuffle, 74x74 and 98x74 | none | draw one status plane per state instead of a text suffix ("Paused", "Repeat All") |
| fade | fade in 150 ms delay 0; panel move 150/100; visible-time 150 (`0x270a30`) | n/a | use 150 ms on appear; do not invent a hide time until the timer unit is settled |
| sound | `snd_system_ok` on open (`explore_plugin_all.c:38936`) | `Cue::Options` for the options panel only | `Cue::Confirm` (= `snd_system_ok` in the native cue table) on panel open |
| input | panel buttons are focusable, direct keys per `tex_cp_help_*` | Triangle cycles repeat for music (`view.cpp:2049`), L1/R1/shuffle/repeat keys (`view.cpp:1944-1947`) | keep the direct keys; make Triangle open B2 |

### 7.3 Surface C - the volume display

| aspect | firmware | native today (`view.cpp:1657-1691`) | change |
|---|---|---|---|
| rect | runtime; no rect in the container | `QRectF panel(1000,75,222,58)` | replace with the meter's own runtime rect: a row of 20x40 segments; the 222x58 plate goes away |
| plate | `tex_default_lvmeter_bg` 48x48 behind the segments | `QColor(0,0,0,20)` rounded rect, radius 4, plus `tex_infobar` at `0.12` opacity | draw `system_plugin/tex_default_lvmeter_bg` only |
| border | none | `QPen(QColor(255,255,255,90),1)` rounded rect | remove |
| caption | none on the volume page | `assets_.text("videoplayer_plugin","msg_volume_control",...)` at 15 px | remove from the HUD; the string belongs to the volume button's hint, see 7.1 |
| value | none | `"%1%"` or `"Mute"` at 19 px, right aligned | remove; the level is shown by segment count |
| meter | segments of 20x40, on/off/focus + 48x48 bg | one 196x6 track plus a white fill, `QColor(255,255,255,55)` track | segmented meter from `tex_default_lvmeter_on/off` (and `_focus` for the current segment), 20 px pitch |
| fade | 150 ms in, 0 delay (`0x270a00`/`0x270a04`); visible-time 150 (`0x270a30`, unit SO) | `MediaControls` 3000 ms + 250 ms linear (`media_controls.cpp:8-9`) | 150 ms fade on every change; keep the current visible time until the timer unit is settled |
| sound | `snd_system_ok` (`:38936`) | none | optional: `Cue::Confirm` on the panel open, not per step |

### 7.4 Surface D - photo viewer chrome

| aspect | firmware | native today | change |
|---|---|---|---|
| screen planes | `Pv_front_screen_plane` 1920x1080, `tex_front_screen` 1x1 | whole-frame `p.fillRect(...,Qt::black)` then the image (`view.cpp:1174-1177`) | draw the 1920x1080 plane from `tex_front_screen` as the backing |
| panel | `view_controlpanel_group` 20 buttons + `button_description_label_incpv` (width 300, size 10) + `direct_key_icon` | none: zoom/rotate are keyboard-only, feedback is a text line | draw the panel; 70x70 `tex_cp_zoom_in/_out`, `tex_cp_rotate_left/_right`, `tex_cp_viewmode`, `tex_cp_slideshowoption`, `tex_cp_slide_speed`, `tex_cp_play/pause/stop/prev/next/repeat` |
| zoom indicator | no numeric zoom in firmware: the zoom is a state of the panel buttons | `"Zoom %1%"` at 21 px (`view.cpp:1193`) | remove the percentage text; show the state on the panel |
| slideshow indicators | `slideshow_controlpanel_group` 11 children (play/pause/stop/repeat + option + speed), `tex_play_status`/`tex_pause_status`/`tex_repeat_status` 74x74/98x70 | text suffix "Pause"/"Slideshow" in the hint line (`view.cpp:1190`) | draw the slideshow panel's status planes |
| keyboard hints | 19 `tex_dki_*` icons, 86x30 | one ASCII sentence | draw the `tex_dki_*` icons that match the bindings the native build actually has |
| thumbnails | 16 `IItem` rows, `tex_thumbnail_frame` 12x12 nine-slice, 18x18 shadow, 21x21 thumb shadow | none | optional; the strip is a separate feature |

---

## 8. Runtime-read sources that should replace hardcoded numbers

Every row is an RCO object or string that already exists locally; reading it removes a literal.

| today (native) | where | source to read instead |
|---|---|---|
| video overlay title 27 px at `{110,970,1500,50}` | `view.cpp:1189` | `videoplayer_plugin/videoplayer_cp_button_name` (Text, `width=400`, `size`, white+shadow) and `videoplayer_information_string` |
| hint sentence 21 px | `view.cpp:1190` | `tex_cp_help_circle/cross/l1/l2_left/r1/r2_right/select/start` (86x30) + `msg_*_with_icon` strings |
| progress track `QRectF(110,941,1700,4)` | `view.cpp:1196` | `videoplayer_display_time_bar*` (Progress, max 100) with `tex_progress_bar` / `tex_progress_bar_in` (30x20) |
| time string 23 px at `{1450,963,360,45}` | `view.cpp:1198` | the 16 `videoplayer_display_*_time_*` Text cells (16x16, `size=10`) + `MediaControls::formatTime` for the digits |
| the loading line (source spells it with U+2026) 26 px | `view.cpp:1191` | `videoplayer_buffering_text` + `msg_buffering` + `tex_loading_icon` (90x90) |
| `"Track %1 of %2"` 21 px | `view.cpp:1192`, `shell.cpp:2423` | `audioplayer_plugin/msg_track_number_value` (`Track %1`), `msg_track_count_value` (`%1 Track`), executed into `label_playing_time` (size 10) |
| `"Zoom %1%"` | `view.cpp:1193` | none: the firmware has no numeric zoom. Drop it or move it to the photo panel's state |
| volume HUD rect `(1000,75,222,58)` | `view.cpp:1660` | no firmware rect exists (section 5.4). Size it from the meter: N x 20 px segments at 40 px height |
| volume HUD plate `(0,0,0,20)` + `tex_infobar` 0.12 | `view.cpp:1663-1664` | `system_plugin/tex_default_lvmeter_bg` (48x48) |
| volume HUD border `(255,255,255,90)` 1 px | `view.cpp:1665-1666` | nothing - remove |
| volume label 15 px from `msg_volume_control` | `view.cpp:1678-1679` | nothing on the volume page; the string is the volume **button's** hint |
| volume value 19 px | `view.cpp:1683-1685` | nothing - the firmware shows level by segments |
| volume track 196x6 | `view.cpp:1686-1689` | `system_plugin/tex_default_lvmeter_on` + `_off` (20x40), `_focus` |
| HUD fade 3000 + 250 ms | `media_controls.cpp:8-9` | `explore_plugin.prx` 0x270a00 = 150 ms fade (FD); the visible time's unit is SO |
| music panel (absent) | `view.cpp:2044-2050` | `explore_plugin_full/control_panel_base` + `tex_cp_panel` (72x72), `control_button_group` (7 x 90x60 + `tex_cp_focus`), `progress_bar_playing_time` (200x6), `control_play_status_group` (74x74 / 98x74 / 72x36), `label_playing_time` and `notification_text` (size 10), constants 0x270a54 = 1.3, 0x270a58/0x270a5c = 150/100 |
| photo panel (absent) | `view.cpp:1168-1200` | `photoviewer_plugin/view_controlpanel_group` (20 x 70x70), `slideshow_controlpanel_group` (11 children), `Pv_front_screen_plane` 1920x1080, `tex_dki_*` 86x30, `tex_thumbnail_frame` 12x12 |

---

## 9. What is spec-only, and why

Stated explicitly, because these are the places where a reader must not treat a number above as
measured.

1. **The video control panel's screen rect and button pitch.** The container declares cell indices
   and three hit rects (750x64 at x=250, 200x64) but no screen transform, and `videoplayer_plugin.prx`
   has no local decompile and is not in the local Ghidra project. Recovering it needs
   `analyzeHeadless ... -process videoplayer_plugin.prx -postScript DecompileRange.java` (the recipe
   is in `audit/INFO-BAR.md` section 4) or an RPCS3 frame. **No local frame shows this surface.**
2. **The music panel's own rect.** Same reason for `audioplayer_plugin.prx`. For the XMB variant
   (B2) the *sizes* are container facts and the scale/timing are code facts, but the panel's screen
   anchor is still runtime.
3. **The volume meter's rect, segment count and total width.** The page is a single meter with zero
   geometry; the widget is paf's `PhLevelMeter` inside `vsh.elf`, and its layout code is not in the
   local decompile range (`paf-range.c` only proves the class identity). Segment *art* size (20x40)
   is a container fact; the *number* of segments is not.
4. **The unit of the 150 visible-time constant** (`0x270a30`). 150 ms would be too short; 150 frames
   is plausible but unproven. The 150 ms *fade* is a different constant (`0x270a00`) with 0 delay and
   is passed straight to the animation helper.
5. **The pixel size of RCO `size=10`.** `audit/TEXT-RENDERER.md:37-38` says the DPI and fallback are
   known but the final screen size is not.
6. **Sound per control** for the three players: no `snd_*` string exists in any of the three
   decrypted plugin modules, so no per-control mapping can be quoted from firmware.
7. **Fade/hide timing for the video and photo surfaces**: both containers have zero animations and
   neither plugin is decompiled locally.
8. **No RPCS3 reference frame of any player surface exists locally.** Checked: every `.png.json`
   sidecar under `out/native-reference/` and every media directory
   (`video-frames/` empty, `resume-*`, `volume/`, `media-continuation/`) is a native capture
   (`mediaOpen`, `mediaPosition`, `mediaProvider: Local` fields come from the native sidecar writer).
   The only RPCS3 frames are XMB root/page frames (`ab-rpcs3-root.png`, `rpcs3-ref-20261025.png`).

---

## 10. Evidence index

| claim group | path |
|---|---|
| object trees, offsets, image refs, texture sizes, strings (all FC tables) | `work/media-rco-dump.ts` against `C:/dev/rpcs3/bin/dev_flash/vsh/resource/*.rco` |
| same data, pre-digested (122 resources, 8,070 objects, 0 errors) | `out/native-reference/rco-behavior/resources.json`; per-container listing via `work/rco-tree-dump.ts` |
| image-name inventory as produced by the app's own analysis flag | `out/native-reference/rco-images.txt` (`re-ps3-native --rco-images`), regenerable with `--dev-flash` |
| English string tables | `out/native-reference/rco-text.txt` (`re-ps3-native --rco-text`), plus `work/vp-texts.txt`, `work/ap-texts.txt`, `work/pv-texts.txt` |
| Meter = paf `PhLevelMeter` | `out/native-reference/paf-range.c:23812, 23821, 24679` |
| XMB panel build, fade, scale, timer, sound | `out/native-reference/explore_plugin_all.c:38171-38208` (fade-in), `:38249-38277` (meter state), `:38405-38433` (visible-time helper), `:38787-38821` (panel setup), `:38936` (`snd_system_ok`) |
| numeric constants 0x270a00..0x270a68 | `work/elf-const.ts --elf fw493/vsh_module/explore_plugin.prx --at 0x2709f0 --count 40 --type float` |
| `tex_default_lvmeter_*` sizes | `work/media-rco-dump.ts --rco system_plugin --images` |
| native media overlay | `native-xmb/src/view.cpp:1168-1207` |
| native volume HUD | `native-xmb/src/view.cpp:1657-1691`, draw/suppression at `:1476-1490`, input gate at `:1544-1557` |
| native fade model | `native-xmb/src/media_controls.cpp:8-9, 43-64` |
| native media state | `native-xmb/src/shell.cpp:2411, 2423, 2432, 2452-2457` |
| native player options | `native-xmb/src/shell.cpp:1651-1667`; drawn at `view.cpp:1204-1207` |
| native cue set | `native-xmb/src/interaction_audio.cpp:32-43` |
| prior honest statement of the fit | `native-xmb/audit/HTPC-IDEAS.md` section 6.2 and 6.8 |
| prior media overlay status | `native-xmb/audit/MEDIA-CONTROLS.md` (timings are "this native proof-of-concept model") |

Not touched: any `native-xmb/src` file, `out/`, the verification harness, its thresholds or its
references, and any git state.

---

# 11. Implementation pass, 2026-10-28 (appended)

This section records what the native build now draws for the volume display (section 5), the video player
control surface (section 3) and the XMB music control panel (section 4.2), every number that is still a
**fit**, and the measurements taken from the built artefact. It was written from the code and the captures,
after the code.

Files touched: `native-xmb/src/view.cpp`, `native-xmb/src/view.h`, and this appendix. Nothing else -
`shell.*`, `main.cpp`, `assets.*`, `dialogs.cpp`, `media_controls.*`, the harness, its thresholds and its
stored references are unchanged. The build is `C:\dev\out\native-media\re-ps3-native.exe`.

## 11.1 Surface C - the volume display

`View::drawVolumeHud` (`view.h`: `volumeMeterSegments`, `volumeMeterFadeMs`). Firmware: one page, one paf
`PhLevelMeter`, segment art `system_plugin:tex_default_lvmeter_on` / `_off` / `_focus` (20x40 each) over
`tex_default_lvmeter_bg` (48x48), no caption, no percentage, no plate of its own, no border, fade 150 / delay
0 (5.2-5.4).

Implemented:

- a row of ten 20x40 segments; lit = on, unlit = off, and the boundary segment (the one at the level) is the
  one drawn with `_focus`;
- the backing is `tex_default_lvmeter_bg`, nine-sliced at a 16 px inset, drawn over the segment row inflated
  by 8 px;
- everything the old HUD invented is gone: no `tex_infobar` at 0.12, no `QColor(0,0,0,20)` rounded plate, no
  1 px `(255,255,255,90)` border, no `msg_volume_control` caption at 15 px, no `"%1%"` value at 19 px, no
  196x6 track;
- the fade-in is 150 ms with 0 delay (0x270a00 / 0x270a04), computed in `drawScene` from the change
  timestamp; `MediaControls` still owns the visible window under it. That is the "150 ms fade on every
  change; keep the current visible time" reading of 7.3, not a new timing scheme;
- mute is a native-only state (the volume page has none, 5.3): the reported level is kept but no segment is
  lit, so silence is drawn as silence instead of a second material;
- if `tex_default_lvmeter_on` / `_off` / `_focus` / `_bg` do not decode, the exact pre-change 222x58 HUD is
  drawn with one `qWarning`.

Measured on `hud25.png` / `hud50.png` / `hud100.png`: lit segments **3 / 5 / 10** at 25 / 50 / 100 %; ten
cells on a 20 px pitch at x 860..1060, ink rows 1011..1044 inside the 40 px band 1008..1048; backing
footprint 853..1067 x 1001..1053 (the 48x48 tile nine-sliced; the code's rect is 852..1068 x 1000..1056).
Diffed against the old reference, the meter band changes by 11,366 px and the old plate's band
(1001..1221 x 76..131) changes by exactly 3,178 px - i.e. the invented plate is gone where it was.

## 11.2 Surface A - the video player control surface

`View::drawVideoTimeBar`, `View::drawTimeCells`, `View::drawVideoHelpStrip`.

- progress: `tex_progress_bar` (30x20) nine-sliced as the track and `tex_progress_bar_in` as the played
  portion, in `videoplayer_display_time_bar`'s rect, plus `tex_progress_bar_jump_indi` (20x12);
- time: two groups of 16x16 cells (`videoplayer_display_playing_time_h1..c2` and `play_time_h1..c2`), one
  glyph per cell, digits from `MediaControls::formatTime`; the playing group starts at the bar's left edge,
  the duration group is right-aligned on the bar's right edge;
- hint strip: six pairs, each an 86x30 `tex_cp_help_*` key icon plus the container's own string -
  `msg_quit_playback`, `msg_play_with_icon` / `msg_pause_with_icon` (state), `msg_fast_r_with_icon`,
  `msg_fast_f_with_icon`, `msg_prev_with_icon`, `msg_next_with_icon`;
- the ASCII advice sentence no longer draws on the video player. It survives verbatim on the photo viewer
  (surface D is out of this change) and on the audio player, whose 4 px bar and single time string are also
  unchanged;
- falling back, one logged line each: `tex_progress_bar`/`_in` missing -> the old flat 4 px bar plus the
  single 23 px `MM:SS / MM:SS`; `tex_cp_help_*` missing -> the old sentence.

Three fidelity facts found while wiring it:

1. the three Progress objects (`..._bar_base`, `..._bar_playable_time`, `..._bar`) carry **no** image refs
   (`unknownRef25/27/29 = 0xffff0000ffffffff`); `tex_progress_bar`/`_in` are container art the plugin
   publishes at run time, so 3.4's mapping to those objects is the audit's CR reading, not an object field.
2. the `_with_icon` strings end in a private-use gaiji after a space (`msg_play_with_icon` = "Play " U+F88F,
   `msg_fast_r_with_icon` = "Fast Reverse " U+F893, `msg_next_with_icon` = "Next " U+F88B, ...). The strip
   draws the key icon itself, so the trailing gaiji and space are stripped; drawn as-is they render as a
   missing-glyph box.
3. this container has no Back/Cancel string (3.6), so Circle's label is `msg_quit_playback` - which is what
   Circle does on this player (it leaves playback).

Measured on `player-chrome.png`: bar ink rows 944..956 across x 110..1809 (the 20 px band is 941..961);
playing-time glyph cells at x 110/126/142/158/174 (16 px pitch) and the duration group's cells ending on
1810 (ink 1732..1806); hint icons on rows 1026..1055, six slots at a 283.33 px pitch from x 110. Diffed
against the stored (text-free) reference, the whole change is confined to y 815..1056; the band 500..640 is
pixel-identical, so the video frame and the rest of the screen are untouched.

## 11.3 Surface B2 - the XMB music control panel

`View::drawMusicPanel` plus the panel state in `view.h` (`musicPanelOpen_`, `musicPanelFocus_`,
`musicPanelEase_`, `musicPanelButtons`, `musicPanelFadeMs`, `musicPanelScale`).

- plate: `tex_cp_panel` 72x72 nine-sliced at a 20 px inset, sized from the content box and scaled **1.3**
  (0x270a54);
- status planes (`control_play_status_group`): `tex_play_status` / `tex_pause_status` for the transport and
  `tex_repeat_status` / `tex_1repeat_status` for repeat mode 2 / 1, `tex_shuffle_status` for shuffle. The
  transport planes the shell has no state for (prev, next, searchb, searchf, stop) are not drawn rather than
  fabricated - the template itself keeps all ten at `alphaScale=0` until the plugin publishes one (3.7);
- `progress_bar_playing_time` 200x6, and `label_playing_time` at the size-10 fit, showing position /
  duration;
- seven buttons at 90x60 in the firmware's table order (`prev`, `next`, `pause`, `stop`, `repeat`, `shuffle`,
  `volume`), each icon from `explore_plugin_full`, with the shared `tex_cp_focus` under the focused button;
- open on Triangle over a local audio item; fade **150 ms, delay 0**; the close uses the same 150 because the
  container carries no animation at all (4.5);
- the traced 150/100 move/size animation on the base (0x270a58 / 0x270a5c, alpha 1 at 0x270a50) is drawn as
  the plate growing from its own size to 1.3x, upward from its resting bottom edge. The 100-unit delay is
  dropped: `PafEase` has no delay parameter and the unit is unproven (9.4);
- while the panel is up, the audio player's own chrome (queue line, title, sentence, 4 px bar, time string)
  fades out on the same ease, so the panel is not read through another surface's progress bar;
- if the plate or any of the seven icons does not decode, the panel is not drawn and one `qWarning` says so.

Measured on `music-panel.png` / `music-closed.png`: plate footprint 534..1384 x 774..999 (851x226; the code's
rect is 850.2x226.2 at 534.9, 773.8); the status plane's rows 834..866; `label_playing_time` ink right-aligned
to x 1275 on rows 838..860; the 200x6 bar on rows 868..874; the button row 901..961 with a 90 px pitch (the
edge period). The fade trace (`XMB_MEDIA_TRACE=1`) runs 0.065 ... 0.949 over its 150 ms.

## 11.4 Input changes (chrome only; behaviour kept)

- **Triangle on a local audio item now opens/closes the panel** instead of cycling Repeat. This is the change
  7.2 lists; Repeat keeps `R` and gains the panel's own Repeat button, so no binding was lost. Photo keeps
  the old Triangle binding (it did nothing), video keeps the in-player options panel.
- while open: Left/Right walk the seven buttons (pad and keyboard), Cross/Enter runs the focused one,
  Circle/Escape and Triangle close it;
- the buttons run the bindings the player already had: prev/next = `Shell::skipMedia`, pause =
  `Shell::togglePlayback`, stop = `Shell::back()` (ends the session, which is what Stop does at the player
  level), repeat = `Shell::cycleMediaRepeat`, shuffle = `Shell::toggleMediaShuffle`, volume = raise the
  volume meter at the current level (no level change).

## 11.5 Every number that is still a fit, and why

| # | fitted value | firmware status | why it cannot be traced |
|---|---|---|---|
| 1 | volume meter rect: ten segments, 20 px pitch, x 860..1060, y 1008..1048 | rect and count are runtime (5.4) | the page is a single meter with zero geometry; paf's `PhLevelMeter` layout is in `vsh.elf` and not in the local decompile range (9.3) |
| 2 | volume segment count **10** | not in the container; only `unknown24 = 4` is a candidate | 5.4, spec-only |
| 3 | `_focus` = the boundary segment | which segment paf focuses is untraced | 5.4; the art set exists but the state assignment does not |
| 4 | mute = no lit segment | the volume page has no mute state (5.3) | native-only state; drawn as silence, no new material |
| 5 | video time-bar rect (110, 941, 1700, 20) | the Progress objects are zero-rect; 750x64 at x=250 is group-local | 3.5, 9.1; keeps this build's band at the texture's own 20 px height |
| 6 | time digit size **19 px** inside the 16x16 cell | RCO `size=10` has no established px equivalent | 9.5; 19 px keeps the digit cap (about 0.82 x size - the 27 px clock measures a 22 px cap) inside a 16 px cell |
| 7 | jump indicator at the played position | the object marks a jump/seek target | this build seeks instantaneously and has no scrub state, so the played position is the only position the bar can mark (3.5) |
| 8 | hint strip: six 283.33 px slots from x 110, labels 19 px | object sizes and strings are FC, positions runtime | 3.2/3.4; the panel's own hint is one icon + the focused control's name |
| 9 | L2/R2 icons carry the Left/Right seek bindings; Circle carries `msg_quit_playback` | the firmware seeks with L2/R2 and this container has no Back string | 3.4/3.6; the video container has no d-pad hint art |
| 10 | music plate: content 630x150 + 12 px padding, x1.3, centred on x 960, bottom edge y 1000, content centred in the plate | plate/base size is runtime (4.2, 9.2) | paf sizes the base from its 72x72 image and the plugin scales it 1.3; neither the base size nor the anchor is in the container |
| 11 | music panel internal layout: status left, label + bar right, buttons below | the tree gives sizes and cells, no positions | 4.2; the button cells are indices, the pixel pitch is the plugin's |
| 12 | music panel's 100-unit move delay dropped | 0x270a5c = 100 | `PafEase` has no delay parameter and the unit is unproven (9.4) |
| 13 | music progress art = `system_plugin:tex_default_progress_base` / `_slider` (24x20) | `progress_bar_playing_time` declares no image | same "paf's defaults live in system_plugin" rule as the level meter (5.2) |
| 14 | shuffle badge derived from `Shell::mediaQueueSummary().contains("Shuffle")` | Shell state | `Shell` has no shuffle accessor and `shell.*` is outside this change's write fence |
| 15 | music panel close fade 150 ms | the container has no animations (4.5) | no close time exists to trace |

## 11.6 Verification

Build: `native-xmb/build.ps1 -OutputDirectory C:\dev\out\native-media` - exit 0, no new warnings (the three
C4996/C4189/C4456 warnings are pre-existing, at untouched lines 349/489/624).

Canonical gate, run twice:

    bun work/ab-layers.ts --capture --exe C:/dev/out/native-media/re-ps3-native.exe
    frame 18.29/0.8342, top background 20.25/0.9363, category icon row 20.93/0.8990,
    item label row 11.95/0.3925, clock text 11.39/0.4500, info bar 14.98/0.7639
    PASS: 6/6 layers within budget   (every delta +0.00, both runs)

Captures (pinned clocks, one token per 300 ms; data dirs under `C:\dev\out`):

    volume  --script full,wait,wait,wait,VOLSET=25,wait,wait,grab=hud25.png,VOLSET=50,...  (25/50/100)
    video   --data-dir <phase5/subtitle-data> --script full,wait x10,LEFT,LEFT,wait,wait,UP x18,wait,
            DOWN,ENTER,wait,wait,ENTER,wait x6,ENTER,wait,LEFT,wait,wait,[TRIANGLE,wait,wait,]grab=...
    music   --data-dir <library/music seed> --script full,wait x6,LEFT,wait,LEFT,wait,LEFT,wait,wait,
            ENTER,wait x4,ENTER,wait x8,[TRIANGLE,wait,wait,]grab=...

The video capture is paused at 0 ms of 20000 with sidecar cue 1 (`subtitleCue=1`,
`subtitleFile=Cue Test.srt`); the music capture plays a 6000 ms local WAV at 3622 ms with the panel open
(`shell: playback` / `media: music control panel opened`).

Media-panel spec checks (geometry / size table above, measured on those frames):

| surface | expected (firmware / code) | measured |
|---|---|---|
| volume segments | 10 x 20x40, lit 3/5/10 at 25/50/100 | 10 cells, 20 px pitch, lit 3 / 5 / 10 |
| volume backing | 48x48 tile nine-sliced to 216x56 | 853..1067 x 1001..1053 |
| volume caption/value/border/plate | none (5.3) | none; old plate band now empty |
| video bar | 30x20 art in a 1700x20 band at 110,941 | ink 110..1809 x 944..956 |
| video digit cells | 16x16 pitch, 5 glyphs per group | x 110/126/142/158/174 and 1732/1748/1764/1781/1796 |
| video hint strip | 6 x 86x30 icons + labels, 283.33 pitch | icons on rows 1026..1055, ink from x 138 |
| music plate | 850.2 x 226.2 at 534.9, 773.8 (1.3 scale) | 534..1384 x 774..999 |
| music bar | 200x6 | rows 868..874, x 1075..1275 |
| music buttons | 7 x 90x60, 90 px pitch | row 901..961, 90 px pitch |
| music label | size-10 fit, right-aligned on the bar | ink 1135..1273 on rows 838..860 |

Named states, `bun work/ab-layers.ts --states --exe C:/dev/out/native-media/re-ps3-native.exe`:
**0/12 pass**, and that is not this change. A shared text-renderer change (src/text_renderer.cpp and
src/font_raster.cpp, 2026-10-28 00:42) makes captures contain glyphs again, while every stored state
reference under `out/native-reference/layers/` was saved between 00:03 and 00:06, i.e. text-free. The
proof is `steam-game-root`, a state with no media surface: it now fails its frame at 27.59/0.9758 and its
item column at 24.41/0.9581, and the 19,582 changed pixels sit on the clock and the label rows (630 touched
columns, no other region). Before that text change the same suite scored **9/12**, and the three failures
were: `xmb-volume-hud` (this surface: frame 33.64/0.9931, hud region 20.84/0.6585 - the reference holds the
222x58 plate), `subtitle-cue` (chrome only: frame 28.83/0.9820, cue band inf/1.0000 - the subtitle drawing
itself is untouched), and `video-options-panel`, which fails its own assertions on the two older builds as
well (pre-existing: on `out/native-all` its panel log line never appears; on `out/native-phase3` it reaches
depth 0 instead of 1 and expects the row order 'Sort By|Open|Copy|Delete|Information'). After the text change
`subtitle-cue`'s cue band also differs (17.93/0.9321) because the reference contains no cue glyphs.

The two media states' references therefore need the deliberate refresh the thresholds file describes
(`--states --save-reference`), together with the other ten that the text change moved. **No reference was
edited here**, as instructed.

## 11.7 Not done, and why

- **The panel-open cue.** 7.2 wants `Cue::Confirm` on open (firmware: `snd_system_ok`,
  `explore_plugin_all.c:38936`). Two obstacles, both outside this change's write fence:
  `InteractionAudio` is a private member of `Shell` with no cue entry point, and the mission's cue table maps
  `Cue::Confirm` to `snd_decide` while `snd_system_ok` is `Cue::Accept` (`interaction_audio.h:12-15`), so
  7.2's parenthetical "(= snd_system_ok in the native cue table)" is wrong. The one-line patch is a public
  `void Shell::playCue(InteractionAudio::Cue cue) { interactionAudio_.play(cue); }` plus
  `if(shell_.mediaAudioOnly()) shell_.playCue(InteractionAudio::Cue::Accept);` in `View::toggleMusicPanel()`.
  The panel opens silently today and says so in its log line.
- Surface D (photo viewer chrome), audio player B1 and the video control panel's 25-button matrix are not
  part of this pass; the video player keeps the existing Triangle options panel for subtitles, audio tracks,
  repeat and shuffle.
- The audio player's own chrome (4 px bar, single time string, ASCII sentence) is deliberately unchanged, so
  the only audio progress display the firmware provides here is the panel's `progress_bar_playing_time`.
- Pre-existing, untouched: `Shell::mediaQueueSummary()` writes its separators as "A-circumflex middot"
  (mojibake) - visible in the queue line of the video capture; and white hint labels over a bright video
  frame have only the firmware's shadow to separate them from the picture.
