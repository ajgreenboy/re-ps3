# Settings screens: what the firmware actually defines, and what this build draws

Status: 2026-10-27. Analysis only; no source was modified. Scope: the Settings column's 16 rows, the
settings pages behind them, the choice lists, and the confirm/apply flows.

ASCII note: firmware strings that carry a trademark sign are transliterated here (`PlayStation(R)3`,
`PS3(TM)`). The RCO's exact UTF-8 text stays in the local dumps listed in section 8 and is not committed.

Data note: the RCOs, dev_flash trees, capture frames and the full English string table are local-only.
The derived tables in `work/` are scratch and are not part of the deliverable.

## 0. Evidence grades

Every claim below carries one of these, and section 8 lists the file each one comes from.

| grade | meaning |
|---|---|
| **X** | read from `category_sysconf.xml` (the XMBML definition) - a field, a table key, an item order |
| **R** | read from `sysconf_plugin.rco` (object tree or English text table) |
| **F** | measured on a 1920x1080 firmware frame (`out/native-reference/rpcs3-settled.png`) |
| **f** | measured on a frame of the local firmware recording (`out/native-reference/recording-193556`, 1262x800 window capture) - relative geometry only, see 2.4 |
| **U** | unattributed: no local source. Section 8 names the capture that would settle it |

The distinction matters: the *structure* of the settings screens is fully attested (X + R), the
*geometry* is partly attested (one 1080p frame, one recording), and the *motion* is not attested at all.

---

## 1. The Settings column

### 1.1 What the definition says the column is

`vsh/resource/explore/xmb/category_sysconf.xml`, view `root` -> `#seg_fixed_items` (**X**). The view has
18 `<Table>` entries and 18 `<Item>`/`<Query>` rows in document order:

- 15 leaf rows, `class="type:x-xmb/xmlsysconf"`;
- 3 **segment** rows (`game`, `device`, `network`), `class="type:x-xmb/folder-pixmap"`,
  `src="...#seg_game"`, `#seg_device`, `#seg_network`, and a table that says
  `child=segment`, `ch_pos=1`, `fo_lbl2_alpha=0`;
- 1 QA row (`console_qa`, Debug Settings).

Two firmware facts the current build does not model:

1. **Game, Accessory and Network Settings are two-level.** Cross on them opens a *segment view* that
   lists the rows, and each of those rows opens its own page (**X**, `seg_game` / `seg_device` /
   `seg_network` -> `seg_*_fixed_upper|middle|lower` + conditional provider groups). The native build
   pushes a page directly (`shell.cpp:1015-1022`, `Action::SettingsPage`).
2. **`fo_lbl2_alpha=0`** on exactly those three tables: the XMB row's second label line (the info line)
   is suppressed for them, because a segment row is a folder, not a setting. The native prints
   `msg_*_explanation` under all 16 rows (`shell.cpp:1074`).

The fourteen leaf tables also carry `bar_action=none`, i.e. the XMB bar itself does nothing on Cross;
the plugin takes over.

### 1.2 Row -> definition -> page

Retail order is document order (**X**). `edy` (Japan-only) and `console_qa` (QA) are the two rows the
console does not show a retail user; the native already omits them (`MENUS-XMBML.md` 3.1), which is
correct.

| # | key | title_rsc (**R** exact) | icon_rsc | page object in sysconf_plugin.rco | native id / title | native page |
|---|---|---|---|---|---|---|
| 0 | update | System Update | tex_update | *not in this RCO* (software-update plugin) | update / System Update | `update` |
| 1 | game | Game Settings | tex_game | `page_game_config` | game / Game Settings | `game` (1 row) |
| 2 | bd_dvd | Video Settings | tex_bddvd | `page_bdvd_config` | video / Video Settings | `video` (3 rows) |
| 3 | music | Music Settings | tex_music | `page_music_config` | music / Music Settings | `music` (3 rows) |
| 4 | chat | Chat Settings | tex_chat | `page_chat_config` | chat / Chat Settings | `chat` (3 rows) |
| 5 | console | System Settings | tex_console | `page_console_config` | system / System Settings | `system` (12 rows) |
| 6 | theme | Theme Settings | tex_thema | `page_theme_config` | theme / Theme Settings | `theme` (6 rows) |
| 7 | date_time | Date and Time Settings | tex_datetime | `page_date_time_config` | date / Date and Time Settings | `date` (4 rows) |
| 8 | power | Power Save Settings | tex_power | `page_console_auto_power_off_option` | power / Power Save Settings | `power` (2 rows) |
| 9 | device | Accessory Settings | tex_pd | `page_device_config` | device / Accessory Settings | `device` (2 rows) |
| 10 | print | Printer Settings | tex_printer | `page_print_config` | print / Printer Settings | `print` (2 rows) |
| 11 | display | Display Settings | tex_display | `page_display_config` | display / Display Settings | `display` (4 rows) |
| 12 | sound | Sound Settings | tex_sound | `page_sound_config` | sound / Sound Settings | `sound` (7 rows) |
| 13 | security | Security Settings | tex_security | `page_security_config` | security / Security Settings | `security` (1 row) |
| 14 | remote | Remote Play Settings | tex_remote | `page_remote_config` | remote / Remote Play Settings | `remote` (2 rows) |
| 15 | network | Network Settings | tex_network | `page_network_config` | network / Network Settings | `network` (5 rows) |

Icon source (**R**): `explore_category_sysconf.rco` holds exactly 18 GIM pictures, all 128x128:
`tex_sett` (the generic Settings glyph) plus `tex_update`, `tex_game`, `tex_bddvd`, `tex_music`,
`tex_chat`, `tex_console`, `tex_thema`, `tex_datetime`, `tex_power`, `tex_pd`, `tex_printer`,
`tex_display`, `tex_sound`, `tex_security`, `tex_remote`, `tex_network`, `tex_edyviewer`.

The native column reads `title_rsc`, `info_rsc` and `icon_rsc` from these tables at startup
(`shell.cpp:1064-1078`) - that part is faithful - but then **overwrites every icon** with a numeric
`qgl/icontex` id from a hand-written map (`shell.cpp:1082-1087`) and keeps its own hardcoded page list.
So the column's *strings* are the firmware's and its *icons* are not.

---

## 2. Page class A: a settings list page (`page_*_config`)

### 2.1 Object tree (**R**)

```
page_<group>_config                 Page        <- one per settings group
  xlist_<group>_config              XList       <- the list, no geometry of its own
```

That is the whole page object: an `XList` with `posX/posY/width/height = 0` and no children. The RCO
does **not** list the page's rows; the plugin builds them at run time from the row set the page's
own sub-pages and the definition imply. The sub-pages *are* in the RCO and are the evidence for which
settings exist (section 3), and the MItem choice lists under them are the evidence for section 4.

Consequence for a reconstruction: the row order and the row-to-sub-page binding cannot be read from the
RCO. Section 3 gives the order where a frame exists and marks the rest.

### 2.2 Geometry, 1920x1080 (**F**, `out/native-reference/rpcs3-settled.png`, page = Game Settings)

| quantity | measured | note |
|---|---|---|
| focused row centre y | 505 | same as the XMB row focus height |
| focused row mark | disc x 552-579 (28 wide), y 484-525 | brass wrench-in-disc |
| unfocused row mark | disc x 556-573 (18), y 671-688 (18) | same x centre 565 |
| row label left edge | x 625 | glyph ink starts here |
| row label glyph band | y 491-523 (33 tall, cap + descender) | ~30 px font if cap/descender ratios hold |
| value column left edge | x 1301 | all values start here; left-aligned, not right-aligned |
| value glyph band | y 499-514 (16 tall) | ~21-23 px font, same baseline as the label |
| help line 1 | x 626-1805, y 527-546 (glyph 20) | starts 4 px under the label band |
| help line 2 | x 625-654, y 550-564 | line pitch 23 px |
| second row (next below focus) | label y 667-698, value y 675-689 | see 2.4 for the gap |
| selected-row plate | **none** | no plate/BG1 behind the focused row: mean luminance above focus 0.79, focus band 21-37 (text only), below 38.95 (ribbon) |
| back chevron | at x ~400, y ~455 | left of the row marks, above the label line |
| parent column behind the page | icons x 422-469 (47 wide), centres y 655, 729, 809, 881, 953, 1033 (pitch 74-80, mean 75.6) | the Settings category column, drawn icon-only |

The focused row is marked by the *enlarged, brightened row icon plus the label glow*, not by a plate.

### 2.3 Values are text, in a fixed value column

The firmware shows a setting's current value as a left-aligned text string at x 1301 on the row's own
baseline. Frames show `Off`, `Off`, `PS3-354`, `English (United States)`, `English (UK)` (**f**, 46 s).
There is no dot, no bar and no slider on the list page.

The current build already puts the value at x 1300 with a 21 px font (`view.cpp:1416`), so this one
constant is right; the native's label x 622 (`view.cpp:1375`, `kColumnX+57`) is 3 px left of the
firmware's 625.

### 2.4 Row pitch: what the two sources say, and why they disagree

- **f**: on two different pages (Accessory Settings 40 s, System Settings 46 s) consecutive rows are
  **49 px** apart in the 1262x800 capture, and the label glyph band is 29-32 px, i.e.
  pitch / label-height = **1.6**.
- **F**: on the 1080p frame the distance from the focused row to the next row is **175 px** and the
  label glyph band is 33 px, i.e. **5.3**.
- The same page class cannot have both ratios, and the two frames cannot be mapped onto each other:
  in the recording, page rows start at x ~280 while the 1080p frame has them at 625, and no single
  scale factor reproduces both the x origin and the glyph height. The recording is a window capture
  whose XMB viewport size is not recorded, so it cannot be converted to 1080p pixels.

What is firm: **the focused row expands**. In the recording the gap from the focused row to the next
row is 93 px against a 49 px pitch - exactly one extra row slot for its two help lines (**f**). On the
1080p frame the same gap is 175 px with a two-line help text as well (**F**), so the expansion there is
larger than one slot. The next row on that frame is also visibly blurred and dimmed, consistent with a
list still moving.

Untattributed: the exact 1080p pitch and the exact expanded height. Section 8 names the capture.

---

## 3. Page class B: a choice page (`page_*_<setting>`)

### 3.1 Object tree (**R**)

Every choice page has the same three-plane shape:

```
page_<group>_config_<setting>              Page
  plane_<...>                              Plane
    plane_<...>_BG0                        Plane   <- page background
    plane_<...>_BG1                        Plane   <- the single highlight plate
    plane_<...>_LowerMenu                  MList
      item_<...>_<choice>                  MItem   (one per choice, no children)
      ...
```

Long lists add a `Scroll` object (the RCO has 30). Examples: `page_date_time_config_timezone` has
**110** MItems inside one LowerMenu; `page_device_config_keyboard_type` 24; the BD/DVD language pages 18
each; `page_security_config_parental_lock_level` 12; `page_music_config_crossfade_play` 11.

Facts this settles:

- A setting with choices opens a **new page**, not an overlay. There is one page object per choice
  setting; the container is `LowerMenu`, i.e. the list sits in the lower part of the page with the
  page's own text above it.
- There is exactly **one `BG1`** per page, which is the single focus plate that travels between rows -
  the same reading `ITEM-MENU-INFO-FIRMWARE.md` (section A2/A6) established for the item option menu.
- The current value is **not** an RCO child of any MItem: the MItems have no children at all, so any
  check/dot/radio mark is drawn by the plugin's code, not carried in the RCO (**R**, negative evidence).

### 3.2 What the build does instead, and why that is the core error

`Action::SettingChoices` -> `openChoices()` (`settings_menu.cpp:154-171`) pushes a `Page` whose entries
are all `Action::SetSetting`. `View::drawColumns` recognises the "all entries are SetSetting" column
(`view.cpp:1261-1265`) and draws it with `drawOptionsPanel` (`view.cpp:1278-1285`): a translucent strip
from **x 1330** to the right edge, rows 23 px on a **40 px** pitch centred on **y 505**, heading 19 px,
and a 4.5 px radius **dot** at `x0+18` for the current choice (`view.cpp:1129-1181`).

That is the *Triangle item menu* visual language (`OPTIONS-MENU.md` 5), reused for settings choices. The
firmware uses it for the item menu; for a setting choice it opens a full page of the class in 3.1. The
native has no page-class-B renderer at all.

### 3.3 Choice inventories (**R**, item counts from the RCO)

| setting (page object) | items | item names (abridged) |
|---|---|---|
| `page_theme_config_color` | 0 | page exists, empty: colours are built in code (**U**) |
| `page_theme_config_background` | 0 | empty (**U**) |
| `page_theme_config_font` | 0 | empty (**U**) |
| `page_date_time_config_automatic_setting` | 2 | `_0`, `_1` (Set Automatically off/on) |
| `page_date_time_config_date_format` | 3 | `_0`, `_1`, `_2` |
| `page_date_time_config_time_format` | 2 | `_0`, `_1` |
| `page_date_time_config_timezone` | 110 | `item_date_time_config_timezone_*` (one per zone) |
| `page_date_time_config_summer_time` | 2 | `_0`, `_1` |
| `page_display_config_screensaverstartup` | 5 | `5min 10min 20min 30min not_use` |
| `page_display_config_cross_color_filter` | 2 | `off on` |
| `page_display_config_rgb_full_range` | 2 | `limited full` |
| `page_display_config_super_white` | 2 | `off on` |
| `page_display_config_deep_color_output` | 2 | `auto off` |
| `page_sound_config_multi_output` | 2 | `off on` |
| `page_sound_config_operation_sound` | 2 | `off on` |
| `page_security_config_bd_parental_control` | 2 | `off on` |
| `page_security_config_dvd_region_code_control` | 2 | `us other` |
| `page_security_config_dvd_parental_control` | 9 | `off 8 7 6 5 4 3 2 1` |
| `page_security_config_parental_lock_level` | 12 | `off 11..1` |
| `page_security_config_browser` | 2 | Internet Browser Start Control |
| `page_console_system_lang` | 20 | one per console language |
| `page_console_characterset` | 9 | `_0.._8` |
| `page_console_notification` / `_trophy_notification` / `_whats_new` / `_disc_boot_first` / `_atrac_activation` / `_wma_activation` / `_boot_os` / `_auto_update` / `_mtpip_setting` / `_control_for_hdmi` | 2 each | `_0`, `_1` |
| `page_console_use_dictionary_en` / `_cn_t` | 2 each | `_0`, `_1` |
| `page_device_config_keyboard_type` | 24 | one per keyboard layout |
| `page_device_config_key_repeat_start_time` | 3 | `short normal long` |
| `page_device_config_key_repeat_speed` | 3 | `fast norm slow` |
| `page_device_config_pointer_speed` | 3 | `fast norm slow` |
| `page_device_config_mouse_type` | 2 | `right left` |
| `page_device_config_sce_stereo_headset` | 2 | `off on` |
| `page_music_config_codec_select` | 3 | `aac mp3 atrac` |
| `page_music_config_crossfade_play` | 11 | `off 1..10` |
| `page_music_config_output_frequency` | 2 | `0 1` |
| `page_music_config_bitmapping` | 4 | `off type1 type2 type3` |
| `page_music_config_dnormal` | 2 | `off on` |
| `page_chat_config_when_chat_start` | 2 | `send_video send_avatar` |
| `page_bdvd_config_bd_menu_lang` / `_bd_voice_lang` / `_bd_subtitles_lang` | 18 each | one per language + `list` |
| `page_bdvd_config_dvd_*_lang` / `hdd_*_lang` | 18 each | same |
| `page_bdvd_config_dvd_up_convert` | 4 | `off 2x normal full` |
| `page_bdvd_config_bdvd_hdmi_color_space` | 3 | `auto rgb yuv` |
| `page_bdvd_config_bd_1080_24p` / `bdvd_drc` / `bd_sound_format_spdif` | 3 each | |
| `page_bdvd_config_bd_sound_format_hdmi` / `internet_connect` / `closed_captions` / `telecine_type` / `dvd_wide_screen_type` / `sequential_play` / `50hz_video_output` | 2 each | |
| `page_game_config_ps_ps2_upconvert` | 3 | `off normal full` |
| `page_game_config_ps_ps2_smoothing` | 2 | `off on` |
| `page_game_config_pspemu_viewmode` | 2 | `normal full` |
| `page_game_config_pspemu_adhoc_mode_channel` | 4 | `0..3` |
| `page_game_config_pspemu_adhoc_mode_wlan` | 2 | `0..1` |
| `page_remote_config_regist_select` | 4 | `vita psp mobile pc` |
| `page_remote_config_remote_boot` | 2 | `off on` |
| `page_remote_config_audio_config` | 2 | `psp ps3` |
| `page_network_config_internet_connect` | 2 | `_0`, `_1` |
| `page_network_config_mediaserver_connection_config` | 2 | `_0`, `_1` |

### 3.4 Choice-page layout and animation: unattributed

The parsed RCO attributes for `BG0`/`BG1`/`LowerMenu` come back as `posX/posY 0`, `scale 1`, i.e. the
layout transform for these planes is not in the attribute block the inventory decodes. So the plate
size, the list's y origin, the row pitch inside the list and any slide/fade are **U**. Section 8 names
the two captures that settle them.

What can be stated as design intent without a capture: the plate is one object per page, so it must
travel between rows; the list is a `LowerMenu`, so it is bottom-anchored, not a right-hand strip.

---

## 4. Page class C: confirm / apply / result

`sysconf_plugin` has no toast, notification or HUD object for settings. Applying a setting resolves into
one of three firmware shapes, all visible in the page list (**R**):

1. **Result page** - a `page_*_finish` object with a finished-text `Text`: `page_console_disc_utility_finish`,
   `page_console_format_hdd_finish`, `page_console_setting_init_finish`, `page_console_init_ps3_finish`,
   `page_backup_utility_backup_completed`, `page_backup_utility_restore_completed`,
   `page_remote_config_custom_confirm`, plus the `*_activation_finish` pair
   (`page_atrac_activation_finish`, `page_wma_activation_finish`, `page_dtcpip_activation_finish`).
2. **Confirmation chain** - `page_*_top` -> `*_ask` / `*_choice_type` -> `*_proceed` -> `*_finish`
   (`page_console_disc_utility_select|ask|proceed|finish`,
   `page_console_setting_init_top|proceed|finish`, `page_console_init_ps3_top|choice_type|proceed|finish`,
   `page_console_format_hdd_top|area|area_details|choice_type|proceed_hdd|finish|error`).
3. **Nothing** - the value changes on the row and the list redraws. This is what most of the 16 pages do;
   the frames show it directly: Automatic Update is `Off` on the row with no dialog (**f**, 46 s).

Exact confirmation strings (**R**, English table):

| key | text |
|---|---|
| `msg_console_init_ask1` | Are you sure you want to restore the PS3(TM) system? ... all data on the system storage will be deleted ... |
| `msg_console_init_ask2` | If you format, all data will be deleted. This data cannot be restored. Are you sure you want to continue? |
| `msg_disconnect_ask` | If you perform a connection test, the current connection will be terminated and the system will be disconnected. Do you want to continue? |
| `msg_remoteplay_change_manually_need_change_ask` | Remote play settings will be changed manually ... Are you sure you want to change the settings? |
| `msg_remote_start_via_internet_explanation` | If remote start via the Internet is enabled ... Do you want to enable remote start via the Internet? |
| `msg_hdmi_detected1` | An available HDMI device was detected. Do you want to output video and audio using HDMI? |
| `msg_display_resolution_ask` | Can you read this screen? Select [Yes] if you can. ... |
| `msg_display_resolution_wrong_guid` | If the resolution is not correct, the screen will go blank. After %1 seconds the system will go back to the previous screen. |
| `msg_display_test_result` | Resolution: %1 - Currently displaying in the selected resolution. ... |
| `msg_atrac_activated` / `msg_wma_playback_enabled` / `msg_dtcp_ip_enabled` / `msg_registered` | ATRAC has been enabled. / WMA playback has been enabled. / DTCP-IP has been enabled. / Add completed. |

The presentation of those pages is the dialog class already audited in `DIALOGS-FIRMWARE.md` (D3
inline message: two full-width rules at y 199 and y 879, centred body, button hint under the lower
rule). The native's `message_` dialog (`view.cpp:1488-1500`) is that class; the Settings screens
never use it, which is correct for the settings that need no confirmation and wrong for the ones above.

### Sound cues

From `INTERACTION-AUDIO.md` (section "the map"): move = `snd_cursor`, activate a normal item =
`snd_decide`, **open a choice list = `snd_option`**, dismiss a message = `snd_system_ok`, unavailable
item = `snd_system_ng`.

The native already maps `Action::SettingChoices` to `Cue::Options` and everything else to `Cue::Confirm`
(`shell.cpp:1441-1442`). Two gaps:

- the firmware cue for *committing* a value (after a choice is picked) is **U** - `INTERACTION-AUDIO.md`
  lines 277-285 flag exactly this: the shell plays `snd_decide` for every activation, and
  `Cue::Accept`/`snd_system_ok` is unused;
- the native has no cue distinction between "opened a page" and "wrote a value", because both paths
  return through the same `activate()`.

---

## 5. Page-by-page inventory (firmware rows vs native rows)

Row sources per page: **X** = definition table (authoritative for that page), **R** = RCO sub-page /
string present, **f** = seen on a firmware frame. Order is authoritative only where marked X or f.

### 5.1 Game Settings - `page_game_config`, rows from `seg_game` (**X**)

| # | segment | row (exact label) | key | choice page |
|---|---|---|---|---|
| 0 | upper | PS Vita System (view mode) `msg_viewmode_psp_emu` | game_psp_emu_viewmode | `page_game_config_pspemu_viewmode` (2) |
| 1 | upper | 3D Display (PSP emu) `msg_3d_display_psp_emu` | game_psp_emu_3d | `page_game_config_pspemu_*` |
| 2 | middle | Ad-hoc Mode Channel `msg_adhoc_mode_channel_psp_emu` | game_psp_emu_adhoc_ch | `..._adhoc_mode_channel` (4) |
| 3 | middle | Ad-hoc Mode `msg_adhoc_mode_psp_emu` | game_psp_emu_adhoc_mode | `..._adhoc_mode_wlan` (2) |
| 4 | lower | PS - Upscaler `msg_ps_upconvert` | game_ps_emu_upcon | `..._ps_ps2_upconvert` (3) |
| 5 | lower | PS - Smoothing `msg_ps_smoothing` | game_ps_emu_smooth | `..._ps_ps2_smoothing` (2) |

Frames confirm rows 4 and 5 and their labels, values `Off`, and the two-line help under the focused row
(**F**, `rpcs3-settled.png`). Rows 0-3 are conditional on PSP-emulation support.

Native: **1 row**, `msg_game_other_feature` -> "Game/Other Features", which is a *Power Save* label
(`settings_menu.cpp:145`). The entire page is wrong.

### 5.2 Video Settings - `page_bdvd_config` (**R**, 21 sub-pages)

Rows implied by the sub-page set: BD/DVD - Menu Language, BD/DVD - Audio Language, BD/DVD - Subtitle
Language, BD - Internet Connection, DVD - Menu/Audio/Subtitle Language, Closed Captions, Closed Caption
Setting, Telecine Type?, DVD - Wide Display, BD/DVD - Upscaler, BD/DVD - Video Output Format (HDMI),
1080p 24 Hz Output (HDMI), BD/DVD - Dynamic Range Control, BD/DVD - Audio Output Format (HDMI),
BD - Audio Output Format (Optical), HDD audio/subtitle language, Sequential Play, 50 Hz Video Output.
Exact strings exist for each (`msg_bd_menu_lang`, `msg_bd_audio_lang`, `msg_bd_subtitle_lang`,
`msg_dvd_*_lang`, `msg_bd_dvd_upconvert`, `msg_video_output_format_hdmi`,
`msg_bd_video_output_1080_24_hdmi`, `msg_bd_dvd_dynamic_range_compression`,
`msg_bd_audio_output_format_hdmi/_optical`, `msg_bd_internet_connection`) - **R**, exact.

Order and the US-visible subset are **U** (no frame).

Native: **3 rows** - Repeat, Shuffle, Volume (`settings_menu.cpp:103-107`) - none of which is a Video
Settings row. The same three rows are reused verbatim for Music.

### 5.3 Music Settings - `page_music_config` (**R**, 8 sub-pages)

Music CD Import (`.import_musiccd`), Audio CD Output Format (`.codec_select`, 3), Bit Rate
(`.bitrate_select`), (`.setting_save`), Crossfade Play (`.crossfade_play`, 11), Output Frequency
(`.output_frequency`, 2), Bitmapping (`.bitmapping`, 4), Dynamic Normalizer (`.dnormal`, 2).
Strings: `msg_music_dnormal`, `msg_music_dnormal_explanation` etc. - **R**. Order **U**.

Native: Repeat, Shuffle, Volume - wrong.

### 5.4 Chat Settings - `page_chat_config` (**R** + **R** strings)

Voice/Video Chat Start (`msg_av_chat_start`, choice page `..._when_chat_start`, 2: Send Video / Send
Avatar), Audio Device Settings (`msg_audio_device_setting`), Camera Device Settings
(`msg_camera_device_setting`). The native's three rows (`settings_menu.cpp:140`) match this page; the
order is plausible but not attested (**U**).

### 5.5 System Settings - `page_console_config` (**R** 42 sub-pages + **f** frame 46 s)

Rows in the order the frame shows (**f**): Automatic Update (`msg_auto_update`, value `Off`),
System Name (`msg_ps3_name`, value `PS3-354`), System Language (`msg_console_lang`, `English (United
States)`), Character Set (`msg_characterset`), Dictionary Type, Add/Edit Term, Delete Predictive Text
Dictionary, ... The RCO's sub-pages continue the set: Notification Messages (`msg_notification_message`),
Trophy Notifications (`msg_trophy_notification`), Display [What's New] (`msg_your_channel_display`),
Disc Auto-Start (`msg_disc_auto_start`), Control for HDMI (`msg_control_for_hdmi`), Enable DTCP-IP
(`msg_dtcp_ip_enable`), Enable ATRAC (`msg_atrac_activate`), Enable WMA Playback
(`msg_wma_playback_enable`), Install Other OS (`msg_ex_os_install`), Connect PS Vita System Using
Network, List of Registered PS Vita Systems, Delete PS Vita Backup Files, Format Utility
(`msg_disc_utility`), Backup Utility, Data Transfer Utility, Restore Default Settings, Restore PS3(TM)
System, System Information (`msg_console_info`), About PlayStation(R)3 (`msg_aboutps3`).

Native (`settings_menu.cpp:78-87`): System Language, Keyboard Type, Keyboard Entry Method, System Name,
Media Server Connection, Format Utility, Backup Utility, Restore Default Settings, Restore PS3 System,
Install Other OS, System Information, About PS3. Of these, **Keyboard Type** and **Keyboard Entry
Method** belong to Accessory Settings (`seg_device` -> `pd_keyboard_type` / `pd_keyboard_input`) and
**Media Server Connection** belongs to Network Settings (`seg_network` -> `net_media_server`). Ten of the
twelve are `Action::Unavailable`, including Format Utility/Backup Utility which do exist as confirm
chains in the firmware.

### 5.6 Theme Settings - `page_theme_config` (**R** 4 sub-pages + **f** frame 54 s)

Theme (`msg_theme`), Color (`msg_color`), Background (`msg_background`), Font (`msg_font`) - the frame
shows exactly these four, in this order. The four choice pages are **empty in the RCO** (0 items), so
the option lists for Theme/Color/Background/Font are built by code (**U**); the file-picker rows the
native adds have no firmware counterpart.

Native: Theme, Select Theme, Color, Background, Select Wallpaper, Font - two invented rows.

### 5.7 Date and Time Settings - `page_date_time_config` (**R** 6 sub-pages)

Date and Time (`msg_datetime`), Set Automatically (`msg_automatic_datetime_setting`, 2), Date Format
(`msg_date_fmt`, 3), Time Format (`msg_time_fmt`, 2), Time Zone (`msg_timezone`, 110), Summer Time
(`page_date_time_config_summer_time`, 2 - region-dependent, **U** whether US shows it).

Native: Date and Time (an information page showing the clock), Date Format, Time Format, Time Zone.
Missing "Set Automatically" and the summer-time row; the firmware's Date and Time row opens a set page
(`page_date_time_config_set_date_time`), not an info page.

Option labels (**R**, exact): date = `YYYY/MM/DD`, `DD/MM/YYYY`, `MM/DD/YYYY`; time = `24-Hour Clock`,
`12-Hour Clock`; time zone = `GMT-08:00 Pacific Time (US, Canada)` style, 110 strings. The native's
date/time labels are right (`settings_menu.cpp:34-35`) but its time zones are
`QTimeZone::availableTimeZoneIds()` (`settings_menu.cpp:36-43`) - host IANA ids, not the firmware's 110
labelled zones.

### 5.8 Power Save Settings - `page_console_auto_power_off_option` (**R** + **R** strings)

System Auto-Off (`msg_system_auto_power_off`), Controller Auto-Off (`msg_controller_auto_power_off`).
System Auto-Off opens a page of *two* timers - Video/TV/Music/Photo (`msg_video_tv_music_photo`) and
Game/Other Features (`msg_game_other_feature`) - with the page help
`msg_system_auto_power_off_set_time`. Native: two rows, one timer; the second level is missing and
`msg_game_other_feature` is misused as a Game Settings row.

### 5.9 Accessory Settings - `page_device_config` (**R** 10 sub-pages, **X** seg_device, **f** frame 40 s)

Frame order (**f**): Calibrate Motion Controller, Reassign Controllers, Controller Vibration Function,
Register BD Remote Control, Manage Bluetooth Devices, Camera Device Settings - matching `seg_device`'s
`seg_device_fixed_upper` in order (the definition also has Audio Device Settings, the USB-headset row
and the keyboard/mouse group; those are conditional or below the frame's last visible row).
Keyboard Type, Keyboard Entry Method, Key Repeat Start Time, Key Repeat Speed, Mouse Type, Pointer
Speed follow per `seg_device_fixed_middle` / `_lower` (**X**).

Native: Controller Settings (an invented info page about XInput), Manage Bluetooth Devices - 2 rows.

### 5.10 Printer Settings - `page_print_config` (**R** + **R** strings)

Printer Selection (`msg_printer_selection`), Printer Manager (`msg_printer_manager`). Native: the same
two rows, same strings (`settings_menu.cpp:143`) - this page is right.

### 5.11 Display Settings - `page_display_config` (**R** 6 sub-pages)

Video Output Settings (`msg_display_output_setting`, whose text itself carries
`Current Output Resolution: %1`), Screensaver (`msg_screensaver`, 5 options), Cross Color Reduction
Filter (`msg_cross_color_filter`, 2), RGB Full Range (HDMI) (`msg_rgb_full_range_hdmi`, 2),
Y Pb/Cb Pr/Cr Super-White (HDMI) (`msg_ycbcr_super_white_hdmi`, 2), Deep Color Output (HDMI)
(`msg_deep_color_output_hdmi`, 2).

Native: Video Output Settings, TV Type, Screensaver, Full Screen. **TV Type is not a Display Settings
row** - it belongs to the video-output sub-flow (`page_display_output_config_tvtype_select`,
`msg_tvtype_select`) - and **Full Screen** has no firmware counterpart at all. Four firmware rows are
missing.

### 5.12 Sound Settings - `page_sound_config` (**R** 3 sub-pages)

Audio Output Settings (`msg_sound_output_setting`), Audio Multi-Output (`msg_audio_multi_output`, 2),
Key Tone (`msg_sound_key`, `page_sound_config_operation_sound`, 2). Native: those three **plus** System
Volume, Mute, XMB Volume, XMB Mute (`settings_menu.cpp:98-101`) - four invented rows.

### 5.13 Security Settings - `page_security_config` (**R** 5 sub-pages)

Parental Control (`msg_gameetc_pc`, 12 levels), BD - Parental Control (`msg_bd_pc`, 2),
DVD - Parental Control (`msg_dvd_pc`, 9), BD/DVD - Parental Control Region Code
(`msg_bd_dvd_use_pcregion`, 2), Internet Browser Start Control (`msg_pc_browser`, 2). Native: **one**
row (`settings_menu.cpp:141`).

### 5.14 Remote Play Settings - `page_remote_config` (**R** 23 sub-pages)

Remote Play Device (`msg_remoteplay_device_psp`/register flow), Remote Start (`msg_remote_start`, 2),
Audio Output Device (`page_remote_config_audio_config`, 2), plus the registration/list/custom-SSID
chain. Native: two rows (`remoteplay_device_psp`, `remote_start`); the audio row and the registration
chain are missing.

### 5.15 Network Settings - `page_network_config` (**X** seg_network order, **R** 6 sub-pages)

Definition order (**X**, `seg_network_fixed`): Settings and Connection Status List
(`msg_netlist_current`), Internet Connection (`msg_internet_connect`), Internet Connection Settings
(`msg_internet_connect_setting`), Internet Connection Test (`msg_internet_connect_test`); then the
conditional `net_media_server` = Media Server Connection (`msg_mediaserver_connection`).

Native order: Internet Connection, Internet Connection Settings, **Settings and Connection Status List
(inserted at index 2)**, Internet Connection Test, **Internet Browser Settings**. So the status-list row
is out of document order, and "Internet Browser Settings" is not a row of this page (there is no table
for it in `seg_network`; `msg_internetbrowser_setting` exists but no page binds it here).

### 5.16 System Update - not in this RCO (**U**)

The `update` row's page comes from the software-update plugin, not `sysconf_plugin.rco`; the local RCO
set has `autoupdateconf_plugin.rco` (34 objects, the automatic-update config) but no
`software_update_plugin.rco`. Native: two rows built from `software_update_plugin` text
(`settings_menu.cpp:88-93`) - unverified either way.

---

## 6. Precise diff: every native row/screen that differs

Line numbers are this read (2026-10-27 21:0x). "F" = firmware fact with the grade from section 0.
Line numbers: this read is 2026-10-27 21:0x. `view.cpp` and `shell.cpp` were being edited by
another session while this document was written (view.cpp grew by 627 bytes and the cited lines
moved by +21 to +26 between the first and second read), so every citation is also anchored by the
code text it refers to and the numbers below are the second read's.

### 6.1 Column (Settings)

| # | native | firmware | fix |
|---|---|---|---|
| D1 | Icons overwritten with numeric `qgl/icontex` ids, `shell.cpp:1082-1087` | `icon_rsc` per table: `tex_*` from `explore_category_sysconf.rco` (**X**, **R**) | delete the normalMaps pass for settings; keep the resolved `tex_*` |
| D2 | `game`/`device`/`network` open a page directly, `shell.cpp:1015-1022` | they are `child=segment` rows opening `seg_game`/`seg_device`/`seg_network` (**X**) | add a segment page class or resolve `seg_*` views |
| D3 | info line drawn under all 16 rows, `shell.cpp:1074` | `fo_lbl2_alpha=0` on game/device/network (**X**) | honour `fo_lbl2_alpha` |
| D4 | row order/count hardcoded, `shell.cpp:1014-1022` | document order, 18 rows incl. edy/console_qa (**X**) | already resolved from XML for strings; make the list itself the resolved one |

### 6.2 Pages

| # | native | firmware | fix |
|---|---|---|---|
| D5 | Game Settings = 1 row, `settings_menu.cpp:145` | 6 rows from `seg_game` (**X**) | new row set |
| D6 | Video Settings = Repeat/Shuffle/Volume, `settings_menu.cpp:103-107` | 21 sub-pages of BD/DVD rows (**R**) | new row set |
| D7 | Music Settings = same 3 rows, `settings_menu.cpp:103-107` | 8 sub-pages (**R**) | split the pages; no shared list |
| D8 | System Settings = 12 rows, 10 unavailable, `settings_menu.cpp:78-87` | ~26 rows, order from frame 46 s (**f**) + sub-pages (**R**) | new row set + real handlers |
| D9 | Keyboard Type / Keyboard Entry Method filed under System, `settings_menu.cpp:82` | Accessory Settings rows (**X**) | move |
| D10 | Media Server Connection filed under System, `settings_menu.cpp:84` | Network Settings row (**X**) | move |
| D11 | Theme Settings = 6 rows incl. Select Theme / Select Wallpaper, `settings_menu.cpp:64-70` | 4 rows (**R**, **f**) | drop the two pickers from the page (keep them as an action on the row) |
| D12 | Date and Time = 4 rows, first row is an info page, `settings_menu.cpp:71-77` | 6 rows; first row is a set page (**R**) | add Set Automatically / Summer Time; Date and Time opens a set page |
| D13 | Time Zone list = host IANA ids, `settings_menu.cpp:36-43` | 110 firmware zones with `GMT+HH:MM <City>` labels (**R**) | read `item_date_time_config_timezone_*` + `msg_timezone_*` |
| D14 | Accessory Settings = 2 rows, one invented, `settings_menu.cpp:123-127` | 6-8 rows (**X**, **f**) | new row set |
| D15 | Display Settings has TV Type + Full Screen, `settings_menu.cpp:113-118` | TV Type belongs to the output sub-flow; Full Screen does not exist; 4 real rows missing (**R**) | new row set |
| D16 | Sound Settings = 7 rows, 4 invented, `settings_menu.cpp:94-102` | 3 rows (**R**) | drop System Volume/Mute/XMB Volume/XMB Mute |
| D17 | Security Settings = 1 row, `settings_menu.cpp:141` | 5 rows (**R**) | new row set |
| D18 | Remote Play = 2 rows, `settings_menu.cpp:142` | 3+ rows incl. audio device + registration chain (**R**) | new row set |
| D19 | Network Settings order + Internet Browser Settings row, `settings_menu.cpp:144`, `:149-150` | status list first, no browser row, Media Server Connection last (**X**) | reorder, drop the browser row |
| D20 | Power Save = one idle timer, `settings_menu.cpp:121` | System Auto-Off has two timers (**R**) | add the second level |
| D21 | Every row icon = `qgl/icontex` `23n`, `settings_menu.cpp:53`, `view.cpp:1307` | the rows that carry an XMBML table (Game/Accessory/Network segment rows) all declare `icon_rsc=tex_sett` (**X**, **R**); the frames show the same brass wrench disc on rows of pages that have no table (Display/Game), 18 px unfocused / 28 px focused (**F**, **f**) | use `tex_sett` (or the row's resolved `icon_rsc`) instead of the glass bullet; confirm one page's icon id against a capture |
| D22 | current value drawn as a 4.5 px dot in the panel, `view.cpp:1165-1168`; and `U+25CF` in `valueText`, `settings_menu.cpp:177` | list page shows the value as text at x 1301; a choice page marks the current item with the travelling BG1 plate (**R**, **F**) | value = text; choice mark = plate, not a dot |
| D23 | choices open as a right-hand strip at x 1330, 40 px pitch, `view.cpp:1129-1181`, `:1278-1285` | a choice opens a new page: `Plane{BG0, BG1, LowerMenu(MList)}` (**R**) | new page class; keep the strip for Triangle only |
| D24 | row pitch 80 px, help text drawn over the next row, `view.cpp:1294`, `:1422` | pitch ~1.6 label-heights and the focused row expands by at least one slot (**f**), 175 px gap on the 1080p frame (**F**) | expand the focused row; pitch from a still capture |
| D25 | label font 27 px, `view.cpp:1381` | label glyph band 33 px, i.e. ~30 px (**F**) | re-measure with TEXT-RENDERER's method on a settings page |
| D26 | labels elided at 1050 px / 640 px, `view.cpp:1388-1389` | help text wraps at x ~1805 and the value column starts at 1301, so the label field ends ~1290 (**F**) | label limit from the value column, not a magic number |
| D27 | no confirmation for any setting write, `shell.cpp:1437-1449` | Format/Restore/Region/connection-test flows have confirm chains and result pages (**R**) | add the confirmation page class + exact strings |
| D28 | no result page after a write; the page just pops | `page_*_finish` / `msg_*_enabled` / `msg_registered` (**R**) | result page for the flows that have one |
| D29 | cue for a value write = `snd_decide` (`Cue::Confirm`), `shell.cpp:1441-1442` | firmware cue for committing a value is unattributed (`INTERACTION-AUDIO.md` 277-285) | capture; then map Accept/Optimize |

---

## 7. Fix list, with the runtime source to read instead of the constant

The principle: the firmware data is already on disk and already parsed by this build in three places
(`readXmbDocument`, `Assets::text`, and the RCO object inventory). Every item below reads one of those
instead of a literal.

1. **Column rows** - resolve `#seg_fixed_items` (already done for title/info/icon at
   `shell.cpp:1064-1078`) and also read `child`, `ch_pos`, `fo_lbl2_alpha`, `bar_action`
   (`xmbml_view` already parses tables generically). Source: `category_sysconf.xml`.
2. **Segment pages** - resolve views `seg_game`, `seg_device`, `seg_network` (and their
   `sel://localhost/...` provider rows, which the reader already classifies as provider rows).
   Order comes from the view's `<Items>`, labels from each `seg_*_fixed_*` table's `title_rsc`.
3. **Page row sets** - one table per page, keyed by the firmware row id, listing the sub-page object
   names and the `msg_*` keys. The sub-page set is derivable from the RCO inventory: for a page `P`,
   the rows are the objects named `page_<P>_<setting>` plus the shared `page_console_*` pages bound by
   `seg_*` rows. Suggested shape: `Entry{ .titleKey = "msg_...", .page = "page_..." }`.
4. **Row labels and help** - `Assets::text("sysconf_plugin", key)`; the full English table is 1985
   entries. The native currently synthesises keys as `"msg_"+nativeRowId` (`settings_menu.cpp:60-62`),
   which only accidentally matches (`color`, `background`, `font`, `timezone`, `date_fmt`, `time_fmt`
   match; `name`, `controller`, `interval` do not).
5. **Choice lists** - for a bound page, enumerate its `LowerMenu`'s `MItem` children in document order;
   each item's label is `msg_<item suffix>` or the item's own text key. That replaces every hardcoded
   vector in `choices()` (`settings_menu.cpp:23-51`), including the 0-100 % volume ladder, the photo
   intervals and the screensaver ladder.
6. **Current value** - compare the persisted selection against the item index, and draw the value as
   the item's label text in the value column (class A) or as the plate position (class B). Remove the
   `U+25CF` dot from `valueText` (`settings_menu.cpp:174,177`).
7. **Row icon** - use `tex_sett` (or the row's resolved `icon_rsc`) instead of the `qgl/icontex` `23n`
   glass bullet (`settings_menu.cpp:53`, `view.cpp:1307-1308`), and size it from the measured frame
   (18 px unfocused / 28 px focused, not 18/39).
8. **Geometry constants** - label x 625, value x 1301, help top = label bottom + 4, help line pitch 23,
   value font ~21, label font ~30 (all **F**). Keep 505 as the focus row height (already right).
9. **Confirm/apply** - one confirmation page class bound to a table of flow keys; strings from
   `msg_console_init_ask1/2`, `msg_disconnect_ask`, `msg_remoteplay_change_manually_need_change_ask`,
   `msg_display_resolution_ask` etc. Result pages re-use the same class with the `*_finish` string.
10. **Cues** - keep `Cue::Options` for opening a choice page; add `Cue::Accept` on a committed write once
    a capture confirms which sample the firmware uses.

---

## 8. Evidence paths, what is unattributed, and the captures that would settle it

### 8.1 Local evidence used

| id | path | what it gave |
|---|---|---|
| X | `vsh/resource/explore/xmb/category_sysconf.xml` (in the dev_flash tree) | 18 tables, order, `child=segment`, `fo_lbl2_alpha`, `bar_action`, `seg_*` views |
| X-tab | same file, all 47 tables | the complete row inventory of Game/Accessory/Network pages with `title_rsc`/`info_rsc`/`icon_rsc` |
| R | `out/native-reference/rco-behavior/resources.json` | `sysconf_plugin.rco`: 331 pages, 588 planes, 761 MItems, 161 MLists, 30 scrolls, 16 XLists; page/sub-page/MItem trees |
| R-img | `rco-table-dump.ts --images` on `explore_category_sysconf.rco` | the 18 x 128x128 settings icon textures incl. `tex_sett` |
| R-txt | `rco-table-dump.ts --text` on `sysconf_plugin.rco` | 1985 English strings (local: `work/sysconf-strings.txt`) |
| F | `out/native-reference/rpcs3-settled.png` (1920x1080, RPCS3, 25/10 0:58) | the only 1080p firmware settings-page frame: row x positions, fonts, value x, help lines, no plate |
| f | `out/native-reference/recording-193556/frame-00040000.jpg`, `frame-00046000.jpg`, `frame-00054000.jpg` (1262x800) | page row order and values (Accessory, System, Theme), 49 px relative pitch, focused-row expansion |
| f-col | same recording, frames 38/44/56/58/60/62 s | the column at rest: titles, info line under focus, icons |
| A | `native-xmb/audit/ITEM-MENU-INFO-FIRMWARE.md` (A2/A6) | the UpperMenu/LowerMenu + single-BG1 reading of the option menu |
| A | `native-xmb/audit/INTERACTION-AUDIO.md` | cue map and the open question about the commit cue |
| A | `native-xmb/audit/DIALOGS-FIRMWARE.md` | the confirm/status page class (D3/D4) |
| A | `native-xmb/audit/MENUS-XMBML.md` 3.1 | the 18-row resolved table and the edy/console_qa disposition |

Tools used (read-only): `work/rco-tree-dump.ts`, `work/rco-table-dump.ts`, plus the new read-only probes
`work/settings-probe.ts`, `work/page-inventory.ts`, `work/group-inventory.ts`,
`work/dump-sysconf-tables.py`, `work/measure-settings-dog.py`, `work/anchor-frames.py`. Frames were
measured with the bundled Python/Pillow; crops under `work/crops/` are scratch.

### 8.2 Unattributed (needs a capture)

| id | question | the exact capture that settles it |
|---|---|---|
| U1 | 1080p row pitch of a settings list page, and the exact expansion of the focused row | RPCS3, 1920x1080 output, XMB > Settings > Accessory Settings at rest (6 rows), one still after the list settles; the same frame records the label glyph height so the pitch/label ratio can be converted |
| U2 | choice-page layout: plate position/size, list y origin, row pitch inside the LowerMenu | RPCS3 1080p still of Settings > Display Settings > Screensaver, list open and at rest (5 items), plus one frame 100 ms into the open |
| U3 | choice-page open/close animation and the cue | RPCS3 1080p 60 fps burst (12 frames) across Cross on Screensaver, with audio; read with the same `XMB_AUDIO_TRACE` used in INTERACTION-AUDIO |
| U4 | whether the current choice carries a check/dot in the list, or only the plate | same capture as U2 |
| U5 | the cue for committing a value | same capture as U3 |
| U6 | whether the current value on a *list* row is always text (vs a bar/dot for sliders such as volume) | RPCS3 1080p still of Settings > Sound Settings (3 rows) and Settings > Music Settings |
| U7 | row order of the pages with no frame (Video, Music, Display, Sound, Security, Remote Play, Printer, Power Save, Date and Time) | one 1080p still per page, top of list; or one capture scrolling each page |
| U8 | System Update page source | an RPCS3 capture of Settings > System Update, plus the `software_update_plugin` RCO from the same dev_flash |
| U9 | Theme/Color/Background/Font option lists (their RCO pages are empty) | RPCS3 1080p still of each of the four choice pages |
| U10 | whether the US build lists Summer Time on Date and Time | RPCS3 1080p still of Date and Time Settings |

### 8.3 What this changes for the reconstruction

The column is already 90 % firmware-driven and its remaining deltas are small (icons, segment rows,
info-line suppression). The **pages are the real gap**: 13 of 16 native pages have a row set that
differs from the firmware's, four pages (Game, Video, Music, Accessory) are effectively free-hand, and
the entire choice/confirm model is borrowed from the Triangle menu. The highest-value change is not a
constant: it is a second page class - the sysconf list page with a value column and an expanding focused
row - plus the choice page class with its single travelling plate.

No source was edited. No RPCS3 run was made; every frame cited already existed under `C:\dev\out` or
`out/native-reference`.

---

## 9. Dated correction - 2026-10-27/28 capture batch: U1-U10

Evidence added after section 8 was written: one RPCS3 session of the real 4.93 XMB at 1920x1080
(`C:\dev\out\rpcs3-ref-2026-10-28\`, `MANIFEST.md`), with 15 page stills, 4 choice-list bursts and 38
restore frames. Section 8.2 is kept exactly as written; this section gives its status. Nothing above is
deleted. Frame paths are relative to the capture root, and every value is a screen pixel measured on the
named frame (where the batch summary rounds a value, the frame value is used and the difference is
stated).

### 9.1 U1-U10 status

| id | old question (8.2) | status | value and frame |
|---|---|---|---|
| U1 | 1080p row pitch and focused-row expansion | **CLOSED** | focused row centre y = 505 (label ink 490..519); rows below at 682/762/842/922/1002 -> **pitch 80 px**; the focused row expands to a title plus a 2-line help block (ink 528..564) and the next row sits 177 px below it, i.e. +97 px over the normal pitch; rows below keep 80. `settings/accessory-rest.png` (6 full rows + 7th cut), `display-page-rest.png`, `page-video-settings.png`, `page-system-settings.png`, `page-sound-settings.png`. MANIFEST section 6 says ~78; five pages scan at 80 and the item-row vertex constants (680/760/840) agree, so 80 is used |
| U2 | choice-page plate, list origin, item pitch | **CLOSED** | the list opens as a full-height right-hand band over the dimmed parent page; left edge x ~1260 for this page (x ~1330 for the item-menu overlay), fading out by x ~1660; item text left x ~1278; 5 items `After 5 Minutes`, `After 10 Minutes`, `After 20 Minutes` (focused), `After 30 Minutes`, `Do Not Use`; ink tops 426/466/506/546/586 -> **item pitch 40 px**; first item ink top 426. `settings/screensaver-list-rest.png`, `screensaver-open-burst/` (118 frames, 2.179 s, 53.7 fps) |
| U3 | choice open/commit animation and the cue | **PARTIAL** | video: `settings/screensaver-commit-burst/` (139 frames, 2.583 s, 53.4 fps; open at 500 ms, commit at 1800 ms), `screensaver-open-burst/` (118 frames, 2.179 s, 53.7 fps), `dateformat-open-burst/` (120 frames, 2.572 s, 46.3 fps), `timeformat-open-burst/` (103 frames, 2.379 s, 42.9 fps). The commit/close motion is in the frames; the **cue audio was not captured** (MANIFEST section 7 no. 6) |
| U4 | check/dot on the current choice | **CLOSED** | none. The current item is focused (bold/white) but carries no check and no dot; `Down` moves only the focus. `settings/screensaver-list-rest.png`, `screensaver-down-1.png` ... `-5.png`, `display-after-screensaver-cancel.png`. This confirms the RCO negative evidence in 3.1 |
| U5 | the cue for committing a value | **OPEN** | the audio channel was not recorded (MANIFEST section 7 no. 6). `INTERACTION-AUDIO.md` 277-285 stays the open item |
| U6 | value always text, never a bar/dot | **CLOSED** | values are text at x 1301, left-aligned: `page-sound-settings.png` (`Audio Output Settings` has no value, `Audio Multi-Output` `Off`, `Key Tone` `On`), `page-music-settings.png` (`AAC 128 kbps`, `3 seconds`, `48 kHz`, `Type 1`, `Off`), `page-date-time-settings.png`, `page-game-settings.png` (value ink starts x 1301) |
| U7 | row order of the pages with no frame | **CLOSED** for the visible top of every page | 15 stills, one per page (`settings/page-*-settings.png`); row sets in 9.2. Pages longer than the frame (Video, System) are captured at the top only |
| U8 | System Update page source | **PARTIAL** | page: `settings/system-update-page.png`, `-2.png`, `column-system-update.png` (title, body `Select an update method.`, two methods, `X Enter` / `O Cancel`); the `software_update_plugin` RCO was not dumped (MANIFEST section 7 no. 8) |
| U9 | Theme/Color/Background/Font option lists | **PARTIAL** | Theme is **not** empty: `Install`, `Original` (focused), `Classic`, `Air Paint` with a thumbnail (`theme-choice.png`). Color: `Original` + 11 colour chips + overflow arrow (`color-choice.png`). Background and Font are captured (`background-choice.png`, `font-choice.png`) but their contents were not transcribed in this batch |
| U10 | whether the US build lists `Summer Time` | **CLOSED** | no `Summer Time` row; the row is `Daylight Saving`, value `Standard`. `settings/page-date-time-settings.png`, `date-time-page-fresh.png`. The RCO object keeps the internal name `page_date_time_config_summer_time` |

### 9.2 Row sets now measured (top of each page)

Exact labels and the values this session showed. These close the "order U" entries in 5.2, 5.3, 5.8,
5.11, 5.13 and 5.14 (and 5.16 via U8) for the visible rows. ASCII transliteration as in section 0.

| page | rows (top to bottom) | values in this session | frame |
|---|---|---|---|
| Video (`page_bdvd_config`) | `BD - Menu Language`; `BD - Audio Language`; `BD - Subtitle Language`; `BD - Internet Connection`; `DVD - Menu Language`; `DVD - Audio Language`; `DVD - Subtitle Language` (7th cut) | `English` x6, `Confirm` | `page-video-settings.png` |
| Music (`page_music_config`) | `Audio CD Import`; `Crossfade Playback`; `Output Frequency`; `Bitmapping`; `Dynamic Normalizer` | `AAC 128 kbps`; `3 seconds`; `48 kHz`; `Type 1`; `Off` | `page-music-settings.png` |
| Display (`page_display_config`) | `Video Output Settings`; `Screensaver`; `Cross Color Reduction Filter`; `RGB Full Range (HDMI)`; `Y Pb/Cb Pr/Cr Super-White (HDMI)`; `Deep Color Output (HDMI)` | help carries `Current Output Resolution: 1080p`; `After 20 Minutes`; `Off`; `Limited`; `Off`; `Automatic` | `page-display-settings.png`, `display-page-rest.png` |
| Sound (`page_sound_config`) | `Audio Output Settings`; `Audio Multi-Output`; `Key Tone` | (none); `Off`; `On` | `page-sound-settings.png` |
| Security (`page_security_config`) | `Change Password`; `BD - Parental Control`; `BD/DVD - Parental Control Region Code`; `DVD - Parental Control`; `Parental Control`; `Internet Browser Start Control` | (none); `Off`; `United States`; `Off`; `Level 9`; `Off` | `page-security-settings.png` |
| Remote Play (`page_remote_config`) | `Register Device`; `Registered Device List`; `Custom Connection Settings`; `Remote Start`; `Audio Output Device` | (none); (none); (none); `Off`; `Remote Play Device` | `page-remote-play-settings.png` |
| Printer (`page_print_config`) | `Printer Selection`; `Printer Manager` | (none); (none) | `page-printer-settings.png` |
| Power Save (`page_console_auto_power_off_option`) | `System Auto-Off`; `Controller Auto-Off` | (none; opens the two-timer page); `Off` | `page-power-save-settings.png` |
| Date and Time (`page_date_time_config`) | `Date and Time`; `Set Automatically`; `Date Format`; `Time Format`; `Time Zone`; `Daylight Saving` | `27/10/2026 21:43`; `On`; `DD/MM/YYYY`; `24-Hour Clock`; `GMT+13:00 Independent State of Samoa`; `Standard` | `date-time-page-fresh.png`, `page-date-time-settings.png` |
| Accessory (`page_device_config`) | `Calibrate Motion Controller` (focused); `Reassign Controllers`; `Controller Vibration Function`; `Register BD Remote Control`; `Manage Bluetooth(R) Devices`; `Camera Device Settings`; `Audio Device Settings` (cut) | (none); all remaining have no value | `accessory-rest.png`, `accessory-page-fresh.png`, `accessory-register-bd-focused.png` |

Notes that change earlier readings:

- **Music shows 5 rows, not one row per sub-page in 5.3.** The page reads `Audio CD Import`,
  `Crossfade Playback`, `Output Frequency`, `Bitmapping`, `Dynamic Normalizer`, with no sixth row before
  the frame edge; the codec and bit rate are the `Audio CD Import` choice (`AAC 128 kbps` in the focused
  row), so `.codec_select` / `.bitrate_select` / `.setting_save` do not each get a row.
- **Security is 6 rows, not 5.** `Change Password` is a row above the five `page_security_config_*`
  pages; it is a password flow, not a choice list.
- **Remote Play is exactly 5 rows** (`Register Device`, `Registered Device List`, `Custom Connection
  Settings`, `Remote Start`, `Audio Output Device`), with the first three opening their own pages and
  three of them carrying no inline value.
- **Video continues below the fold**: the 7th row (`DVD - Subtitle Language`) is cut at y 1080, so the
  order of the remaining DVD/HDD/upscaler rows is still unmeasured.

### 9.3 Conclusions that the frames correct

**(a) A choice list is a right-hand band, not a bottom-anchored page.** Section 3.1 says a setting with
choices "opens a new page, not an overlay"; section 3.2 draws the consequence, that the native's
right-hand strip is "the Triangle item menu visual language, reused for settings choices" and that the
native has no page-class-B renderer; section 3.4 goes further: the list is a `LowerMenu`, "so it is
bottom-anchored, not a right-hand strip". The frames contradict the screen-position half of all three. On
`settings/screensaver-list-rest.png` the list is a full-height band on the right (left edge x ~1260,
fading out by x ~1660) over the dimmed parent page, its items at 40 px pitch with the focused row on the
505 focus row - i.e. the same visual language the native draws from x 1330. What survives from section
3.1 is the RCO object tree (`Plane{BG0, BG1, LowerMenu(MList)}`); what does not survive is the inference
that `LowerMenu` names a bottom-anchored screen position.

So D23 ("choices open as a right-hand strip at x 1330, 40 px pitch" is native-only) is **half wrong**:
the kind and the 40 px pitch are the firmware's. The measured deltas are: left edge 1260 vs 1330 (70 px
too far right in the native), the firmware band fades out by ~1660 instead of running to 1920, the native
draws a 4.5 px dot (D22) and the firmware does not, and the firmware dims the parent page behind the band
(the native's description in 3.2 records no dim).

**(b) D24 (row pitch).** Firmware pitch is **80 px**, the native's 80 is exact; the only delta is the
focused-row expansion, which the native does not draw (help text overlaps the next row).

**(c) D22 (current value mark).** Resolved by U4: text only, no dot in the panel and no `U+25CF` in the
row. Remove the dot path (`view.cpp:1165-1168`) and the `U+25CF` fallback (`settings_menu.cpp:177`).

**(d) D12 (Date and Time).** Six rows, no Summer Time, and the `Date and Time` row carries the running
clock as its value (`27/10/2026 21:43`) - it is not a bar. What pressing that row opens is still not
captured; the RCO says a set page (`page_date_time_config_set_date_time`), not an info page.

**(e) D26 (label field).** The value column is x 1301 and the help text wraps at ~1805
(`page-display-settings.png`), so the label field ends at ~1290, as D26 says; no new number is needed.

**(f) U9's "Theme is empty" premise is dead.** The RCO pages really are empty (section 3.3), but the
runtime list is not: `Install`, `Original`, `Classic`, `Air Paint`. The lists are built by code, as 3.3
suspected, and Theme is the one whose contents are now known.

**(g) D29 (commit cue)** is still open: the commit burst is video only. Do not map `Cue::Accept` yet.

### 9.4 Restore audit for this batch

The session changed four settings on the way through and put them back:

- Date Format -> `MM/DD/YYYY` and Time Format -> `12-Hour Clock` for the clock frame, then back to
  `DD/MM/YYYY` and `24-Hour Clock`: `settings/restore/restore10-dst-committed.png` (clock `27/10 21:58`).
- `Daylight Saving` -> `Daylight Saving` (a stray commit), then back to `Standard`:
  `settings/restore/restore10-dst-committed.png`.
- `Voice/Video Chat Start` -> `Send Avatar` (a stray commit), then back to `Send Video`:
  `settings/verify-chat-page.png`.
- `Screensaver` was opened and committed at its **current** value only, never changed:
  `settings/display-after-screensaver-commit.png` (`After 20 Minutes`).
- `Controller Vibration Function` was opened while probing, never changed:
  `settings/restore/restore7-vibration-page.png` (`On`).

One residual is stated plainly in `CAPTURE-BATCH-2026-10-28.md` section 7.1: committing those values
made vsh rewrite `C:\dev\rpcs3\bin\dev_flash2\etc\xRegistry.sys` at 21:58:42. The on-screen values are
restored and verified; the file's bytes are not guaranteed identical because no pre-session copy was
taken.

### 9.5 Still open after the batch

- U5 (commit cue audio) and U3's audio half.
- The tail rows of Video and System (below y 1080 in the stills).
- The exact `BG1` focus-plate rectangle inside the band (not separable from the band's gradient in these
  frames).
- Background and Font list contents (captured, not transcribed).
- `software_update_plugin` RCO for U8.
- The exact open/commit duration of a choice list (the bursts are timestamped but the settle frame is not
  published in the manifest).

No source was edited for this section.

---

## 10. Phase 11.1 applied: the settings row sets (2026-10-27/28)

Status: implemented in `src/settings_menu.{h,cpp}` and `src/xmbml_view.{h,cpp}`. The two `shell.cpp`
hunks that install the resolved column are written out in 10.6 and were **not** applied in the
repository (`shell.cpp`/`shell.h` belong to core); a scratch build outside the tree applied them and
its evidence is 10.3. Scope: row sets, row order and label provenance only. No page-class, choice-list,
cue or value-rendering change: `choices()`, `openChoices()` and `valueText()` are byte-identical to the
previous revision.

### 10.1 What the slice changed

| file | change |
|---|---|
| `src/xmbml_view.h` | `XmbmlColumnRow` gains `child`, `chPos`, `foLbl2Alpha`, `barAction` and the `segment()`/`hasSecondLabel()` helpers; `resolveSegmentView()` is declared |
| `src/xmbml_view.cpp` | the table lookup, table-field read and label/fallback resolution are factored into `tableFor`/`readTableAttributes`/`resolveRowLabels` and shared by both readers; `resolveSegmentView()` flattens a segment view one level (its child Queries' item rows, in the segment view's own order) |
| `src/settings_menu.h` | new: declares `settingsColumnEntries(builtin, assets)` |
| `src/settings_menu.cpp` | the Settings column resolver, the three segment pages, the firmware row tables for the one-level pages, the native-only row table, and the `<XMB_DUMP_ENTRIES>.settings-<page>.json` dump hook |

The reader refactor is behaviour-preserving by construction (same code moved) and by evidence: the
Network column still resolves to the same five rows with the same ids, titles and sources in the new
build (`network/manual/Remote Play/Internet Browser/Internet Search/Download Management`).

### 10.2 The Settings column: 16 rows, firmware order, two omitted

`category_sysconf.xml`'s root view names `#seg_fixed_items` (one query, expanded to 18 tables), so the
resolver walks 18 rows and keeps 16. From the hunked build's log:

```
xmbml: settings column omits edy ( the Japan-only Edy Viewer: the sysconf plugin gates it and this build has no page for it )
xmbml: settings column omits console_qa ( Debug Settings: QA consoles only )
xmbml: settings column resolved 16 rows from category_sysconf.xml (queries 1, expanded 1, sha256 2d286e4e1a30): listed 16, omitted 2, rco titles 18 in 3 ms
```

The 16 rows in order, with the definition's own detail (the `fo_lbl2_alpha` effect on the last three):

```
 id        title                     detail
 update    System Update             Updates the system software on this system.
 game      Game Settings             (none - child=segment, fo_lbl2_alpha=0)
 video     Video Settings            Adjusts settings for video.
 music     Music Settings            Adjusts settings for music.
 chat      Chat Settings             Adjusts settings for chat.
 system    System Settings           Adjusts settings for this PS3(TM) system.
 theme     Theme Settings            Adjusts settings related to the appearance of the XMB(TM) screen.
 date      Date and Time Settings    Adjusts date and time settings.
 power     Power Save Settings       Adjusts settings to reduce power usage by this system.
 device    Accessory Settings        (none - child=segment, fo_lbl2_alpha=0)
 print     Printer Settings          Adjusts settings for printers that are connected to this system.
 display   Display Settings          Adjusts settings for video output.
 sound     Sound Settings            Adjusts settings for audio output.
 security  Security Settings         Adjusts parental control settings.
 remote    Remote Play Settings      Adjusts settings for remote play. ...
 network   Network Settings          (none - child=segment, fo_lbl2_alpha=0)
```

Every row's `Entry::source` is now `rco`; before this change the field was empty for the whole column.
The three folder rows keep `Action::SettingsPage` and their existing page values (`game`, `device`,
`network`), which is what makes them open the segment pages in 10.4 without a new action.

**D1 is half a row-set fix and half a rendering fix, and only the first half is applied.** Deleting the
`qgl/icontex` normal-map pass, as D1 asks, is a visible regression with today's renderer: the RCO
`tex_*` icons draw through `View::drawItemGlyph`'s white item-glyph material, while the frame's brass
settings icon is the `icons.cpp` icon program, which addresses `qgl/icontex` by numeric id. Evidence:
`out/native-settings-column-shadow/column-hunk.png` (the hunk without the material bridge: System Update
is a white outline) against `out/native-settings-rows/after-settings-column.png` (the same row in brass).
The resolver does put the definition's icon in `Entry::texture` and the dump reports it, so the data half
is done; the material bridge (the numeric pass) stays, with a comment naming the missing hunk, until the
icon program takes a named RCO texture (Phase 11.4/13).

The info-line suppression is visible and measured: with the resolved column, focusing Game Settings
leaves the row with no second line (`out/native-settings-column-shadow/column-game-focused.png`); the
previous build prints "Adjusts settings for games." there
(`out/native-settings-before/before-column-game-focused.png`).

### 10.3 The three two-level pages

`resolveSegmentView()` reads each segment view's child Queries in document order and then each group
view's items. A row this build cannot act on is listed with the RCO's own explanation (the
"list-unavailable" disposition), so the pages are the console's row sets.

| page | rows (firmware order) | native action |
|---|---|---|
| Game Settings (`seg_game`) | `PSP(TM) Remaster - Screen Mode`, `... 3D Display`, `... Ad Hoc Mode Channel`, `... Ad Hoc Mode`, `PS - Upscaler`, `PS - Smoothing` (6) | all `Unavailable` (no PSP/PS2 emulation) |
| Accessory Settings (`seg_device`) | `Calibrate Motion Controller`, `Reassign Controllers`, `Controller Vibration Function`, `Register BD Remote Control`, `Manage Bluetooth(R) Devices`, `Camera Device Settings`, `Audio Device Settings`, `Voice Changer`, `Keyboard Type`, `Keyboard Entry Method`, `Key Repeat Interval`, `Key Repeat Speed`, `Mouse Type`, `Pointer Speed` (14) | all `Unavailable` (the accessory stack is Windows/HID's) |
| Network Settings (`seg_network`) | `Settings and Connection Status List`, `Internet Connection`, `Internet Connection Settings`, `Internet Connection Test`, `Media Server Connection` (5) | the status list is `Information` (the host's own report); the rest `Unavailable` |

Logged left-outs (Accessory only): `pd_usbhdset_setting` (only exists with a Sony wireless stereo
headset connected), `pd_keybind` (Japanese kana input only), and `pd_keyboard_input` a second time (the
two keyboard-input groups are mutually exclusive, so it is listed once, from `seg_device_kbim_hantai`).

Network is **five** rows, not four: the definition's fixed group is the four the brief counts (status
list first), and `seg_network_dlnaconf` adds the conditional `Media Server Connection` that D10 moves
here from System Settings. Dropping it would delete the row from both pages.

### 10.4 The one-level pages

Measured rows from the 2026-10-28 batch (9.2) are used where they exist; the rest are the RCO sub-page
sets of 5.2-5.16. "Native-only" rows are marked in the code, in the page dump (`nativeOnly: true`) and
here. `Video`/`Music` now carry the firmware rows **and** the three native media settings at the end.

| page | rows | disposition |
|---|---|---|
| Theme | `Theme`, `Color`, `Background`, `Font` (4) | the four firmware rows bind their existing choice lists; `Select Theme` and `Select Wallpaper` stay as native-only rows at the end (D11): folding them into the Theme/Background rows needs the choice page class (11.3) |
| Date and Time | `Date and Time`, `Set Automatically`, `Date Format`, `Time Format`, `Time Zone`, `Daylight Saving` (6) | added `Set Automatically` and `Daylight Saving` (D12); the three format rows keep their settings |
| System | `System Language`, `System Name`, `Format Utility`, `Backup Utility`, `Restore Default Settings`, `Restore PS3(TM) System`, `Install Other OS`, `System Information`, `About PlayStation(R)3 (PS3(TM))` (9) | `Keyboard Type`/`Keyboard Entry Method` moved to Accessory (D9), `Media Server Connection` moved to Network (D10); the missing ~17 rows and the frame order are still D8 |
| Sound | `Audio Output Settings`, `Audio Multi-Output`, `Key Tone` (3) | `System Volume`, `Mute`, `XMB Volume`, `XMB Mute` removed (D16); host/XMB volume is still on the media volume keys (`view.cpp` volume path), so nothing lost a function |
| Display | `Video Output Settings`, `Screensaver`, `Cross Color Reduction Filter`, `RGB Full Range (HDMI)`, `Y Pb/Cb Pr/Cr Super-White (HDMI)`, `Deep Color Output (HDMI)` (6) | `TV Type` and `Full Screen` removed (D15); F11 still toggles fullscreen, so the row was not the only surface |
| Security | `Change Password`, `BD - Parental Control`, `BD/DVD - Parental Control Region Code`, `DVD - Parental Control`, `Parental Control`, `Internet Browser Start Control` (6) | the 9.2 measured set; the four rows below the old single row are added (D17) |
| Remote Play | `Register Device`, `Registered Device List`, `Custom Connection Settings`, `Remote Start`, `Audio Output Device` (5) | the 9.2 measured set (D18) |
| Printer | `Printer Selection`, `Printer Manager` (2) | unchanged |
| Power Save | `System Auto-Off`, `Controller Auto-Off` (2) | unchanged (D20's second level is still open) |
| Chat | `Voice/Video Chat Start`, `Audio Device Settings`, `Camera Device Settings` (3) | unchanged |
| Video | `BD - Menu Language`, `BD - Audio Language`, `BD - Subtitle Language`, `BD - Internet Connection`, `DVD - Menu Language`, `DVD - Audio Language`, `DVD - Subtitle Language` (7 measured) + `Repeat`, `Shuffle`, `Volume` (native-only) | the rows below the seventh are unmeasured (9.2 note 4) and are not invented; the three media settings stay at the end (D6 partly closed) |
| Music | `Audio CD Import`, `Crossfade Playback`, `Output Frequency`, `Bitmapping`, `Dynamic Normalizer` (5 measured) + `Repeat`, `Shuffle`, `Volume` (native-only) | D7 partly closed |
| System Update | unchanged (2 rows, `software_update_plugin`) | U8 still open |
| Photo Settings | unchanged | the page is this build's own; the sysconf inventory has no photo page |

No label is synthesised from the row id any more: every row names its `msg_*` key in a table
(`RowSpec`), and the "msg_"+id helper is gone.

### 10.5 Page dumps

Every settings page opened in a scripted run writes `<XMB_DUMP_ENTRIES>.settings-<page>.json`: page id,
title, the rows (id, title, detail, action, setting, value, texture, source, nativeOnly), the
definition's own row records (`firmware`) and the logged left-outs (`skipped`). Row counts in
`C:\dev\out\native-settings-rows`: column 16 (shadow build), game 6, device 14, network 5, theme 6,
date 6, system 9, sound 3, display 6, security 6, remote 5, print 2, power 2, chat 3, video 10 (7 + 3),
music 8 (5 + 3).

### 10.6 The shell hunks (exact)

`shell.cpp` needs two hunks and `shell.h` none. Both were compiled and captured in a scratch copy at
`C:\dev\out\native-settings-column-shadow`; they are not applied in the repository.

**Hunk 1 - the include**, after `#include "shell.h"`:

```cpp
#include "settings_menu.h"
```

**Hunk 2 - the column**, replacing the block that starts
`// Bind the current native settings projection to original XMBML resource` and ends with the
`normalMaps` loop (`shell.cpp` ~1062-1087), i.e. the old projection *and* the icon override:

```cpp
    // The Settings column is the firmware's own definition (Phase 11.1, audit/SETTINGS-FIRMWARE.md
    // 1.1-1.2): category_sysconf.xml's root view names #seg_fixed_items, whose 18 tables give the
    // column's rows in document order. The 16 rows a retail console shows keep that order, with the
    // RCO's own title, info line and tex_* icon; edy and console_qa are omitted with a logged reason,
    // and the three child=segment rows lose their second label line (fo_lbl2_alpha=0). The resolver
    // keeps each id's native action, value and setting, so every page and theme icon still applies.
    if(const auto rows=settingsColumnEntries(categories_[1].entries,assets_);!rows.isEmpty())
        categories_[1].entries=rows;
    else qWarning().noquote()<<"xmbml: settings column keeps its built-in rows";
    // D1's remaining half is a rendering change, not a row-set one: Entry::texture now carries the
    // definition's tex_* name (the resolver put it there and the entry dump reports it), but that path
    // draws through View::drawItemGlyph's white item-glyph material, while the frame's brass settings
    // icon is the icons.cpp icon program, which addresses qgl/icontex by numeric id. Until the icon
    // program takes a named RCO texture (Phase 11.4 / 13), these ids keep the frame's material; the
    // resolved names stay in the dump for that hunk. Deleting this pass today turns every Settings
    // column icon into a white outline (captured in out/native-settings-column-shadow/column-hunk.png).
    const QMap<QString,int> normalMaps{{"update",9},{"game",14},{"video",10},{"music",12},
        {"system",13},{"theme",24},{"date",15},{"power",57},{"device",16},{"display",17},
        {"sound",18},{"security",19},{"remote",21},{"network",20}};
    for(auto& entry:categories_[1].entries)if(normalMaps.contains(entry.id)) {
        entry.container="qgl/icontex";entry.texture=QString::number(normalMaps[entry.id])+"n";
    }
```

The page push needs **no** hunk: the column rows already carry `Action::SettingsPage` with `game`,
`device` and `network`, and `Shell::openSettings` (in `settings_menu.cpp`) now builds those three as the
segment pages. `shell.h` is untouched.

### 10.7 Verification

Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-settings-rows` (clean at
`/W4 /permissive- /utf-8`; no warning comes from `settings_menu.cpp` or `xmbml_view.cpp`). Binary
`C:\dev\out\native-settings-rows\re-ps3-native.exe`, 3,518,464 bytes, 2026-10-27 22:35.

Column dump: from the hunked shadow build (10.2); the deliverable's own column dump shows the same 16
ids/titles/order with the old detail values, because the hunk is not applied in the repository.

Page dumps: `<XMB_DUMP_ENTRIES>.settings-*` for column, game, device, network and the twelve one-level
pages, opened one per scripted run (`--script "full,<waits>,LEFT x5,DOWN x n,ENTER,wait,grab=...,quit"`).

Captures, clocks pinned at `--render-clock 30 --background-clock 2026-10-25T10:00:00 --ui-clock
2026-10-25T12:32:00 --focus-time 2000`:

- `out/native-settings-before/before-game-page2.png` - the previous build's Game Settings page: one row,
  `Game/Other Features`, no detail.
- `out/native-settings-rows/after-game-page.png` - the new page: six rows, the first
  `PSP(TM) Remaster - Screen Mode` with the RCO's two-line explanation.
- `out/native-settings-rows/after-device-page.png` - Accessory Settings, 14 rows, `Calibrate Motion
  Controller` focused first.
- `out/native-settings-rows/after-network-page.png` - Network Settings, 5 rows, `Settings and Connection
  Status List` first and focused.
- `out/native-settings-before/before-column-game-focused.png` and
  `out/native-settings-column-shadow/column-game-focused.png` - the info line present/absent on the
  Game Settings row (D3).
- `out/native-settings-rows/after-settings-column.png` and
  `out/native-settings-column-shadow/column-hunk.png` - the material difference described in 10.2.

Gate: `bun work\ab-layers.ts --capture --exe C:/dev/out/native-settings-rows/re-ps3-native.exe` ->
`PASS: 6/6 layers within budget` (frame 18.28/.8315, top background 20.25/.9363, category icon row
20.92/.8990, item label row 11.91/.3918, clock text 11.35/.4102, info bar 14.73/.6100). No named state
covers a settings page or the Settings column (`--list-states` has twelve states, all Game/Video/
Network/root), so this slice cannot move a named state. `--states` was run for the record on the same
build: `states: 1/12 pass`, and every failure is outside this slice - the stored self-baselines are stale
against the icon material another session changed (frames 42-45 dB), the dialog state is the in-flight
dialogs pass (14.28 dB), and the Jellyfin sign-in state now reports six rows instead of five (its
`entryCount: expected 5, got 6` assertion, another session's page). Nothing was re-baselined.

Capture note for future scripted runs: while the XMB owns the foreground, the DualSense attached to this
machine reports a held D-pad up, and `View::pollController` turns that into a `vertical(-1)` every
repeat tick (measured: 182 in one run), which cancels every scripted `DOWN`. Handing the foreground to
the shell window with the Alt-key trick before the navigation tokens (`SetForegroundWindow` alone is
refused by the foreground lock) makes navigation deterministic; the scripted keys still reach the view
because `View::replayKeyTap` dispatches straight to `keyPressEvent`.

### 10.8 Still open after this slice

- **D1** material routing for a named RCO icon (10.2) - the one visible regression the hunk avoids.
- **D6/D7** Video's rows below the seventh (unmeasured), and the `Repeat`/`Shuffle`/`Volume` native rows
  that no longer have a firmware row of their own page.
- **D8** System's missing rows and frame order; **D12** the Date and Time row's clock *value* (the
  measured frame shows `27/10/2026 21:43` in the value column; this slice left the value alone).
- **D13** the 110 firmware time zones (the list is still host IANA ids).
- **D20** System Auto-Off's two-timer page; **D21** the settings row icon (`tex_sett` at 18/28 px needs
  the `view.cpp` size hunk); **D22** the current-choice dot (needs 11.3's travelling plate to replace
  it, otherwise the current choice would be unmarked); **D24** row pitch and focused-row expansion;
  **D27-D29** confirm/apply/result pages and the commit cue.
- The choice page class (11.3) - none of this slice binds a new choice list.

