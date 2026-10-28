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

---

## 11. Phase 11.3 applied: the choice lists (data half), 2026-10-27/28

Status: the data model and its dump are implemented in `src/settings_menu.{h,cpp}`; this slice edits no
other source file (`view.cpp`, `shell.cpp`, `main.cpp` and `assets.*` are untouched by it). The band
renderer is the later view pass and is written out in 11.8 as hunks, not applied. Pixels are unchanged by
construction (nothing on the draw path reads the new model) and by the gate:
`PASS: 6/6 layers within budget` (11.5).

Scope: for every Settings row whose firmware counterpart is a choice setting, the list the firmware's
own page declares - labels, authored order, item identity, and the page's Scroll object - read from
`sysconf_plugin.rco` at run time, with the current value marked from the preference. Where the RCO page
is empty (the plugin builds the list in code) the fallback is named per list and never presented as the
firmware's.

### 11.1 Where the labels come from

Every `MItem` carries its own label as a text reference: `[MItem]` is defined as `text = text`,
`altText = text`, `unknownRef4 = ref` (`out/native-reference/objattribdef-ps3.ini`), and the existing
`RcoObjects` reader resolves that reference through the container's English table. The object dump shows
it directly (all 24 extra bytes of an MItem are those three references):

```
MItem item_display_config_5min @625552 +24B text=text#882:msg_5min="After 5 Minutes" altText=null unknownRef4=null
MItem item_date_time_config_timezone_samoa_independent ... text=text#...:msg_timezone_samoa_independent="GMT+13:00 Independent State of Samoa"
```

So the list is enumerated as: the page's `Plane` child -> its `MList` child (`LowerMenu`) -> its `MItem`
children in document order, each item's `text` -> English string. No label is synthesised, and no label
is taken from this build's `choices()`. The `text` reference's table index and the object offset are kept
per item in the dump, so every label can be cited to the RCO.

The current-value mark is **not** in the RCO: the MItems have no children (3.1, negative evidence), and
the capture batch found no check/dot on the frame (U4, closed). It is therefore computed here against
the persisted preference, and the model says so (`ChoiceItem::isCurrent` with the source of the item
separate from the source of the mark).

The RCO read is lazy and cached: one `RcoObjects` per process, `sysconf_plugin.rco` read once on the
first choice page (the same reader the `--rco-tree` CLI uses). A run that never opens a settings page
and does not ask for the dump never reads it.

### 11.2 The API (`settings_menu.h`)

```cpp
struct ChoiceItem {
    QString label; QVariant value; bool isCurrent; QString source; bool scrollable;
    QString item; int rcoIndex; quint32 rcoOffset; bool writes;
};
struct ChoiceList {
    QString pageId, rowId, rowTitle, setting, rcoPage; quint32 rcoPageOffset;
    QString container, source, note; bool scrollable; QString scrollObject;
    QVector<ChoiceItem> items; bool fromRco() const; int count() const;
};
QVector<ChoiceItem> settingsChoices(const QString& pageId,const QString& rowId,Assets& assets,
                                    const QVariant& current=QVariant());
QVector<ChoiceList> settingsChoiceLists(const QString& pageId,Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor={});
QVector<ChoiceList> settingsChoiceCatalogue(Assets& assets,
    const std::function<QVariant(const QString&)>& currentFor={});
bool choiceValueMatches(const QVariant& current,const QVariant& value);
```

| field | meaning |
|---|---|
| `label` | the RCO item's own text, in the page's authored order; the fallback's label when the page is empty |
| `value` | exactly what the existing `Action::SetSetting` path stores for that item (11.3); the item's ordinal when `writes` is false |
| `isCurrent` | the persisted value equals this item's `value` (bool/int/string compared through `choiceValueMatches`) |
| `source` | per item: `rco`, `capture` (Theme's captured list) or `native` (this build's `choices()`) |
| `scrollable` | the page carries a `Scroll` object, or the list is longer than the 27 items the measured band holds |
| `item` / `rcoIndex` / `rcoOffset` | the MItem object name, its position in the LowerMenu, and its table-section offset - the citation |
| `writes` | `value` is a real preference value; false for a firmware item this build cannot honour yet |
| `ChoiceList::note` | per list: why a fallback, or which capture attests the labels |
| `ChoiceList::rcoPage` / `rcoPageOffset` | the page object and its offset, or empty when the RCO has no page |

`settingsChoices` accepts the row's native id (`"screensaver"`) or the Preferences key it writes
(`"display/screensaverMinutes"`), which is the shorthand a view pass will want.

### 11.3 Value semantics: a choice still writes what it writes today

The list replaces `choices()` for drawing only in the later pass; the write path is unchanged
(`Action::SetSetting`, `entry.option`, the same Preferences key). Every item's `value` is the value the
current `choices()` returns for the same semantic item, so a view that draws the item and commits it
writes the identical key and value:

| setting | today's `choices()` values | the firmware list's values | identical? |
|---|---|---|---|
| `display/screensaverMinutes` | 5, 10, 20, 30, 0 | 5, 10, 20, 30, 0 (Do Not Use = 0) | yes |
| `clock/dateFormat` | 0 = DD/MM, 1 = MM/DD, 2 = YYYY/MM/DD | authored `YYYY/MM/DD` -> 2, `DD/MM/YYYY` -> 0, `MM/DD/YYYY` -> 1 | yes, by label |
| `clock/hourFormat` | 0 = 24-hour, 1 = 12-hour | authored `12-Hour Clock` -> 1, `24-Hour Clock` -> 0 | yes, by label |
| `audio/keyTone` and every other Off/On page (BD/DVD, browser, remote, network) | `Off` false, `On` true | `Off` false, `On` true (an inert row's items are still false/true, marked `writes=false`) | yes |
| `clock/timeZone` | host IANA ids (`QTimeZone::availableTimeZoneIds`) | the 110 firmware zones (offset zones) | **no** - see below |
| `appearance/theme` | `Original` = `""` | `Original` = `""`; `Install`/`Classic`/`Air Paint` marked `writes=false` | Original yes; the other three are not installable here |

The time zone list is the one value identity that cannot be preserved: the firmware's 110 zones are
GMT-offset zones with a city label (`GMT-08:00 Pacific Time (US, Canada)`), not IANA identities, and the
old list was the host's IANA ids. The stored value is now the zone's own item token
(`date_time_config_timezone_pacific_us_canada`); `Shell::clockDateTime` resolves it through the same
label's `GMT+HH:MM` text to a fixed-offset zone, and **still resolves every IANA id already in the
store first**, so no stored preference is orphaned. The D13 row of section 7 is closed by this, with
that value change stated. A time zone row's value text resolves a firmware token back to its RCO label
(`Shell::valueText`), so the band and the list row show the same string.

The 110-item list's ordering is the RCO's; its first item is `GMT-11:00 American Samoa` and its last
`GMT+13:00 Independent State of Samoa` - the same string the capture batch's session showed as the
console's current value (`settings/date-time-page-fresh.png`), which is an independent check that the
enumeration is the firmware's own.

### 11.4 The choice tables

Enumerated at run time. Counts and sources are from `entries.json.settings-choices.json` produced by
11.5; "offset" is the page object's offset in `sysconf_plugin.rco` (sha256
`6f4d743ad9ad176c400e7e07742835d27d78bf8feb40534259ac24190fa0b5ed`, version 304, 1,440,648 bytes).

| page | row | items | source | RCO page object | offset |
|---|---|---|---:|---|---:|
| Game | `game_psp_emu_viewmode` | 2 | rco | `page_game_config_pspemu_viewmode` | 483940 |
| Game | `game_psp_emu_adhoc_ch` | 4 | rco | `page_game_config_pspemu_adhoc_mode_channel` | 484812 |
| Game | `game_psp_emu_adhoc_mode` | 2 | rco | `page_game_config_pspemu_adhoc_mode_wlan` | 485812 |
| Game | `game_ps_emu_upcon` | 3 | rco | `page_game_config_ps_ps2_upconvert` | 486684 |
| Game | `game_ps_emu_smooth` | 2 | rco | `page_game_config_ps_ps2_smoothing` | 487620 |
| Game | `game_psp_emu_3d` | 0 | none | - | - |
| Video | `bd_menu_lang` / `bd_audio_lang` / `bd_subtitle_lang` | 18 each | rco | `page_bdvd_config_bd_menu_lang` / `_bd_voice_lang` / `_bd_subtitles_lang` | 489244 / 491140 / 493036 |
| Video | `bd_internet_connection` | 2 | rco | `page_bdvd_config_internet_connect` | 494932 |
| Video | `dvd_menu_lang` / `dvd_audio_lang` / `dvd_subtitle_lang` | 18 each | rco | `page_bdvd_config_dvd_menu_lang` / `_dvd_voice_lang` / `_dvd_subtitles_lang` | 495804 / 497700 / 499596 |
| Video | `repeat` / `shuffle` / `volume` | 3 / 2 / 11 | native | - (this build's own rows) | - |
| Music | `import_musiccd` (codec) | 3 | rco | `page_music_config_codec_select` | 515676 |
| Music | `crossfade_play` | 11 | rco | `page_music_config_crossfade_play` | 518008 |
| Music | `output_frequency` | 2 | rco | `page_music_config_output_frequency` | 519456 |
| Music | `bitmapping` | 4 | rco | `page_music_config_bitmapping` | 520328 |
| Music | `dnormal` | 2 | rco | `page_music_config_dnormal` | 521328 |
| Music | `repeat` / `shuffle` / `volume` | 3 / 2 / 11 | native | - | - |
| Chat | `av_chat_start` | 2 | rco | `page_chat_config_when_chat_start` | 522464 |
| System | `console_lang` | 20 | rco | `page_console_system_lang` | 524088 |
| Theme | `theme` | 4 | capture | `page_theme_config_theme` (empty page) | 588460 |
| Theme | `color` | 6 | native | `page_theme_config_color` (empty page) | 588536 |
| Theme | `background` | 3 | native | `page_theme_config_background` (empty page) | 588612 |
| Theme | `font` | 3 | native | `page_theme_config_font` (empty page) | 588688 |
| Date | `autodate` | 2 | rco | `page_date_time_config_automatic_setting` | 590492 |
| Date | `date_fmt` | 3 | rco | `page_date_time_config_date_format` | 591364 |
| Date | `time_fmt` | 2 | rco | `page_date_time_config_time_format` | 592300 |
| Date | `timezone` | **110** | rco | `page_date_time_config_timezone` | 593172 |
| Date | `summertime` | 2 | rco | `page_date_time_config_summer_time` | 600956 |
| Power | `idle` | 5 | native | `page_console_auto_power_off_option` (empty page) | 602804 |
| Power | `controller` | 0 | none | - | - |
| Accessory | `pd_keyboard_type` | **24** | rco | `page_device_config_keyboard_type` | 606956 |
| Accessory | `pd_keyrepeat_start` | 3 | rco | `page_device_config_key_repeat_start_time` | 611916 |
| Accessory | `pd_keyrepeat_speed` | 3 | rco | `page_device_config_key_repeat_speed` | 612852 |
| Accessory | `pd_mouse_type` | 2 | rco | `page_device_config_mouse_type` | 613788 |
| Accessory | `pd_pointer_speed` | 3 | rco | `page_device_config_pointer_speed` | 614660 |
| Accessory | `pd_keyboard_input` / `pd_pad_vibration` | 0 | none | - | - |
| Display | `screensaver` | 5 | rco | `page_display_config_screensaverstartup` | 624808 |
| Display | `cross_color_filter` | 2 | rco | `page_display_config_cross_color_filter` | 625872 |
| Display | `rgb_full_range` | 2 | rco | `page_display_config_rgb_full_range` | 626744 |
| Display | `super_white` | 2 | rco | `page_display_config_super_white` | 627616 |
| Display | `deep_color` | 2 | rco | `page_display_config_deep_color_output` | 628488 |
| Sound | `audio_multi_output` | 2 | rco | `page_sound_config_multi_output` | 635292 |
| Sound | `keytone` | 2 | rco | `page_sound_config_operation_sound` | 636164 |
| Security | `bd_pc` | 2 | rco | `page_security_config_bd_parental_control` | 637300 |
| Security | `bd_dvd_pcregion` | 2 | rco | `page_security_config_dvd_region_code_control` | 638172 |
| Security | `dvd_pc` | 9 | rco | `page_security_config_dvd_parental_control` | 639044 |
| Security | `gameetc_pc` | **12** | rco | `page_security_config_parental_lock_level` | 640364 |
| Security | `pc_browser` | 2 | rco | `page_security_config_browser` | 641876 |
| Remote | `remote_start` | 2 | rco | `page_remote_config_remote_boot` | 661872 |
| Remote | `audio_output_device` | 2 | rco | `page_remote_config_audio_config` | 663868 |
| Network | `net_internet` | 2 | rco | `page_network_config_internet_connect` | 645652 |
| Network | `net_media_server` | 2 | rco | `page_network_config_mediaserver_connection_config` | 646752 |
| Photo | `interval` / `fit` / `repeat` | 4 / 2 / 2 | native | - (this build's own page) | - |

Totals: **63 lists, 434 items** - 45 RCO lists (373 items), 1 captured list (4), 13 native lists (57),
4 rows with no page at all. Printer Settings has no choice row (0).

Two lists whose item counts are the audit's evidence for "long list": the time zone page has 110 MItems
in one `LowerMenu` and the keyboard page 24, both enumerated in full. `scrollable` is true only for the
time zone list: no settings choice page carries a `Scroll` child (the RCO's 30 `Scroll` objects belong to
list pages and to `page_console_characterset`, `page_first_config_lang_select` and
`page_first_config_tz_select`, not to any choice page this build opens), and 110 items exceed the 27 rows
the measured band holds at the 40 px pitch. This is stated as the criterion, not as a firmware child.

Labels verified against the two lists the capture batch measured, exactly and in order:

| list | expected (capture batch 4.4) | enumerated | match |
|---|---|---|---|
| Screensaver (5) | `After 5 Minutes`, `After 10 Minutes`, `After 20 Minutes`, `After 30 Minutes`, `Do Not Use` (`settings/screensaver-list-rest.png`) | identical, in order | yes |
| Theme (4) | `Install`, `Original`, `Classic`, `Air Paint` (`settings/theme-choice.png`) | identical, in order (captured fallback, RCO page empty) | yes |

`System Language` (20) enumerates `Dansk`, `Deutsch`, the two Englishes, then the European languages,
and closes with Japanese, Korean, Simplified Chinese and Traditional Chinese written in their own
scripts; `Parental Control` (12) enumerates `Off`, `Level 11` ... `Level 1`; `DVD - Parental Control` (9)
enumerates `Off`, `Level 8` ... `Level 1`. Every label is the RCO's own (the RCO's items are UTF-8 and
the dump is UTF-8 JSON).

### 11.5 Verification

Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-choices` (clean at
`/W4 /permissive- /utf-8`; no warning from `settings_menu.cpp`). Binary
`C:\dev\out\native-choices\re-ps3-native.exe`, 3,632,128 bytes, 2026-10-27 23:14.

Dump, one scripted run (`XMB_DUMP_ENTRIES=C:/dev/out/native-choices/entries.json`, opens Settings >
Game Settings; the catalogue is written once on the first settings page):

- `C:/dev/out/native-choices/entries.json.settings-choices.json` - 16 pages, 63 ChoiceList records,
  434 items, the container's name/path/version/bytes, and per item `item`, `rcoIndex`, `rcoOffset`,
  `value`, `isCurrent`, `writes`.
- `C:/dev/out/native-choices/entries.json.settings-game.json` - the same run's per-page dump, now with
  its `choices` array (six rows; five RCO lists 2/4/2/3/2 and the 3D Display row with no page).
- Log: `settings: settingsChoices API -> display/screensaver 5 items, date/clock-dateFormat 3 items`
  (the public single-list entry point is exercised by the same dump, by row id and by preference key).

Pixels: `bun work\ab-layers.ts --capture --exe C:/dev/out/native-choices/re-ps3-native.exe` ->
`PASS: 6/6 layers within budget` (frame 18.29/.8342, top background 20.25/.9363, category icon row
20.93/.8990, item label row 11.95/.3925, clock text 11.39/.4500, info bar 14.98/.7639; every delta
positive except the icon row's -0.13 dB, inside the 0.50 dB budget). `--list-states` on the same build
lists twelve states (root, Steam catalogue/folder/store/download, video options/continue/subtitle,
Jellyfin sign-in, notification, volume HUD), none of them a settings page or a choice list, so this
slice cannot move a named state.

### 11.6 Dated corrections to earlier sections

- **4.4's "Time Format order: `24-Hour Clock`, then `12-Hour Clock`" is wrong, and its own frame
  contradicts it.** `settings/timeformat-list-rest.png` shows `12-Hour Clock` at ink y ~461 and
  `24-Hour Clock` (focused, the session's current value) at ~505, i.e. the RCO's authored order
  (`item_date_time_config_time_format_0` = 12-Hour, `_1` = 24-Hour). What 4.4 says after that ("Down
  clamps at the last item, Up selects 12-hour") is consistent with that order. The list enumerates in
  the frame's order.
- **U2's band fade: the measured profile.** At y 100 on `settings/screensaver-list-rest.png` the band's
  luma (the page is black there, so it is the band's own fill) is 0 at x 1255, 34 at 1262, 60 by 1300,
  61 at 1310-1320, 53 at 1400, 37 at 1500, 25 at 1550, 9 at 1600, 2 at 1650, 0 at 1690. So "fading out
  by 1660" is the visible end within ~2 luma; the geometric zero is ~1690, the same end as the item
  menu's band in 6.1. The left edge is a 1-2 px step at 1260, and the item text starts at 1276-1278.
- **D22's dot is not drawn on a settings choice today.** The settings-choice call site
  (`view.cpp:1296`) passes no `rows`, and the 4.5 px dot is inside the `rows` path (`view.cpp:1179-1182`)
  used by the player panel; the item-menu call passes no rows either. The band must keep that: no check
  and no dot (U4). 11.8 keeps the invariant explicit.

### 11.7 Mismatches: no firmware page, and lists built at run time

**(a) A native setting whose firmware counterpart is a choice setting, but with no choice page object
in `sysconf_plugin.rco`.** These are reported as empty lists with a `note`, not invented:

| page / row | row label | what is missing |
|---|---|---|
| Game / `game_psp_emu_3d` | `PSP(TM) Remaster - 3D Display` | no page object named for the 3D Display row |
| Power / `controller` | `Controller Auto-Off` | no page object; only the list page `page_console_auto_power_off_option` exists, and it ships no items |
| Accessory / `pd_keyboard_input` | `Keyboard Entry Method` | no page object |
| Accessory / `pd_pad_vibration` | `Controller Vibration Function` | no page object (the captured page `settings/restore/restore7-vibration-page.png` exists, so the list is built by code or lives in another plugin; `deviceconf_plugin.rco` was not searched) |

**(b) A firmware choice page that ships empty (the plugin builds the list at run time).** Per list the
fallback and its provenance:

| page / row | RCO page | what it ships | fallback | provenance |
|---|---|---|---|---|
| Theme / `theme` | `page_theme_config_theme` | a Page with **no children** | `Install`, `Original`, `Classic`, `Air Paint` | **capture** (`settings/theme-choice.png`); only `Original` is installable here |
| Theme / `color` | `page_theme_config_color` | Plane, no MList items | this build's 6 colours | **native**; the capture has 12 items (`Original` + 11 colour chips, `settings/color-choice.png`) whose labels were not transcribed |
| Theme / `background` | `page_theme_config_background` | Plane, no MList items | this build's 3 | **native**; capture contents not transcribed |
| Theme / `font` | `page_theme_config_font` | Plane, no MList items | this build's font families | **native**; capture contents not transcribed |
| Power / `idle` | `page_console_auto_power_off_option` | Plane, no MList items | this build's 5 timers | **native**; the two-timer list was not captured (D20) |

**(c) Native settings with no firmware choice page at all** (this build's own rows, kept where they are
a feature's only surface; 10.4): Video and Music `Repeat` (3), `Shuffle` (2), `Volume` (11), and Photo
Settings `Slideshow Interval` (4), `Display Mode` (2), `Repeat` (2). Their lists are `choices()`'s,
marked `native`.

**(d) Read but not yet writable.** Every firmware choice row that is `Action::Unavailable` in this build
(Game's five, Video's and Music's firmware rows, Chat's one, System's language, Security's five,
Display's four non-screensaver rows, Remote's two, Network's two, Accessory's five, Date's `Set
Automatically` and `Daylight Saving`) now has its list enumerated and dumped, with `writes=false` on every
item. The rows that already write a preference - Screensaver, Key Tone, Date Format, Time Format, Time
Zone, Power's `System Auto-Off`, and Theme's Color/Background/Font lists - carry the real value on every
item; the Theme row itself writes only `Original` (11.3). Making the rest writable is a row-action change
(a later slice), not a choice-model one; their values are already the ordinal the page authors.

### 11.8 The view hunks for the band (not applied)

Three hunks in `src/view.cpp`. Anchored by code text, because another session edits this file.

**Hunk A - `drawOptionsPanel` gains the firmware band form.** Replace the signature, the `x0`/`first`
block and the fill (the block that starts `const auto drawOptionsPanel=` and ends with the `edge`
`fillRect`):

```cpp
    // `firmwareBand` (Phase 11.3): a settings choice list is the firmware's full-height right-hand band
    // over the dimmed parent page (SETTINGS 9.1/U2, 9.3a; profile re-measured in 11.6: left edge 1260,
    // 60 alpha by 1300, zero by 1690, item text 1276, 40 px pitch, current item on the 505 focus row,
    // no check/dot). The item-menu overlay and the player panel keep their form at x 1330 to the edge.
    const auto drawOptionsPanel=[&](const QStringList& items,double position,double alpha,double ahead,const QString& heading,
                                    const QVector<MenuOption>* rows=nullptr,bool firmwareBand=false) {
        const double x0=(firmwareBand?1260.:1330.)+ahead*600;
        const double fadeEnd=firmwareBand?1690.:1920.;
        const double rowCount=rows?double(rows->size()):double(items.size());
        const double first=505-(rowCount-1.)*20.;
        p.save();p.setOpacity(alpha);
        if(firmwareBand) {
            // The parent page is dimmed behind the band. The dim is not separable from the band's own
            // gradient in the captured frames (SETTINGS 9.5), so this alpha is a re-measure target.
            p.fillRect(QRectF(0,0,1920,1080),QColor(0,0,0,120));
        }
        // Over the XMB's own background the light strip is enough. Over video it is not: white 23 px text
        // on a bright frame is unreadable, so the in-player panel lays the strip on a dark plate (the
        // console's control panel dims the picture behind it for the same reason).
        if(rows) {
            QLinearGradient shade(x0,0,1920,0);
            shade.setColorAt(0,QColor(0,0,0,158));shade.setColorAt(1,QColor(0,0,0,120));
            p.fillRect(QRectF(x0,0,1920-x0,1080),shade);
        }
        QLinearGradient fill(x0,0,fadeEnd,0);
        if(firmwareBand) {
            // The band's own profile, sampled at y 100 on settings/screensaver-list-rest.png, where the
            // page is black so the luma is the fill's alpha: 0 at 1260, 60 at 1300, 61 at 1320, 37 at
            // 1500, 9 at 1600, 0 at 1690 (SETTINGS 11.6).
            fill.setColorAt(0,QColor(255,255,255,0));fill.setColorAt(.005,QColor(255,255,255,60));
            fill.setColorAt(.14,QColor(255,255,255,61));fill.setColorAt(.558,QColor(255,255,255,37));
            fill.setColorAt(.79,QColor(255,255,255,9));fill.setColorAt(1,QColor(255,255,255,0));
        } else {
            fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
        }
        p.fillRect(QRectF(x0,0,fadeEnd-x0,1080),fill);
        QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
        p.fillRect(QRectF(x0-10,0,14,1080),edge);
```

and, in the same lambda, the dot must never be drawn for a firmware band (U4: the firmware marks the
current item by focus only):

```cpp
            if(rows && !firmwareBand && (*rows)[i].active) { // the player panel's current-choice dot
```

**Hunk B - the settings choice column passes the band form** (`view.cpp:1296`):

```cpp
-                    drawOptionsPanel(titles,column.position,columnAlpha,ahead,QString());
+                    // Phase 11.3: a settings choice is the firmware's own band, not the item-menu overlay.
+                    drawOptionsPanel(titles,column.position,columnAlpha,ahead,QString(),nullptr,true);
```

**Hunk C - the info bar is absent while the band is up.** The captured frame has no clock plate and no
analog clock while the band is open (luma 0 at x 1750..1918, y 100 on
`settings/screensaver-list-rest.png`), and the band does not reach there. Insert before the info-bar
block (`if(!messageScreen) {` at `view.cpp:1466`) and use it in that condition:

```cpp
        // A settings choice band replaces the info bar (SETTINGS 11.6): the frame has neither the plate
        // nor the analog clock. The item-menu overlay does not do this.
        const bool choiceBandUp=columns_.size()>=2 && isChoiceColumn(columns_.back())
            && depthPosition_>double(columns_.size()-2)+0.001;
        if(!messageScreen && !choiceBandUp) {
```

Not in these hunks: the `BG1` focus plate inside the band (9.5 still open), the band's item font size
(the 40 px pitch and x 1276 are measured; the glyph height is not), and the open/close animation (U3's
video is captured, its cue is not).

### 11.9 Still open after this slice

- The `BG1` plate rectangle inside the band and the dim alpha (9.5) - Hunk A uses a placeholder dim.
- Background and Font list contents (U9, still untranscribed); Colour's 11 chip labels.
- The two timer option lists behind `System Auto-Off` / `Controller Auto-Off` (D20) - no RCO items, no
  capture of the timer page.
- `Keyboard Entry Method`, `Controller Vibration Function` and `3D Display` page objects: absent from
  `sysconf_plugin.rco`; they may be built in `deviceconf_plugin.rco` or in code, which was not searched.
- The commit cue (U5) and the band's open/close duration (U3's audio/settle frame).
- Wiring the 45 RCO lists to live rows (11.7d) and the choice-page row actions that follow.

---

## 12. Dated correction - 2026-10-28 capture batch 2: depth-2 pages, band geometry, list contents

Evidence added after section 11 was written: a second targeted RPCS3 batch on the same 4.93 XMB, one
session in two boots, at 1920x1080 (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md`): 251 stills,
22,149 burst frames in 19 bursts (crop bursts at 131-235 fps, 1:1, no rescale), **clock scale 100 %** for
the whole session. Nothing above is deleted; sections 9-11 keep their 2026-10-27 text. Frame paths are
relative to the capture root. Where the batch summary rounds a value, the frame value is used and the
difference is named; the numbers marked "(re-measured)" were checked against the cited still with a
luma/ink scan for this section, the rest are the batch's own frame measurements.

### 12.1 Status of 9.5 and 11.9

| old item | status | value and frame |
|---|---|---|
| 9.5: the tail rows of Video and System below y 1080 | **CLOSED** | Video **19 rows**, last `50 Hz Video Output` (Automatic); System **25 rows**, last `About PlayStation(R)3 (PS3(TM))` - 12.5 |
| 9.5: Background and Font list contents | **CLOSED** | Background 3 items + `Brightness >`; Font 3 items - 12.4 |
| 9.5 / 11.9: Colour's 11 chip labels | **CLOSED as a negative** | the chips carry **no labels at all**; only the current value is named above the column - 12.4 |
| 9.5: the exact BG1 focus-plate rectangle inside the band | **PARTIAL, and changed in kind** | inside the band it is still not separable; at rest on a settings page there is **no filled rectangle** at all - 12.6 |
| 9.5 / U3: the exact open and close duration of a choice band | **CLOSED** | Theme band open starts +62 ms, ramp 270 ms, rest by +330 ms; close starts +11 ms, ramp 200 ms - 12.3 |
| 9.5: U5 and U3's audio half (the commit cue) | **OPEN** | no audio was recorded in this batch either (`MANIFEST` section 5 no. 6); `INTERACTION-AUDIO.md` 277-285 stays the open item |
| 9.5: the `software_update_plugin` RCO for U8 | **OPEN** | not part of this batch; the update path kills vsh (`MANIFEST` section 4.1b) |
| 11.9: the two timer lists behind `System Auto-Off` / `Controller Auto-Off` (D20) | **OPEN** | not captured; the Network/Display/Theme pages were the targets instead |
| U9: Colour's item count (11.7b said "12 items, labels not transcribed") | **CLOSED** | 12 chips, 40 px pitch, `Original` + 11 colours, no labels, live preview - 12.4 |

New in this batch and not on any earlier list: a **depth-2 page class** with its own grid (12.2), and the
band's row origin turning out to be per-list rather than a constant (12.4c).

### 12.2 A page class the audit did not have: the depth-2 read-only page

Route: Settings column, row 16 `Network Settings`, Cross, row 1 `Settings and Connection Status List`,
Cross. Frame `depth2/statuslist-rest.png` (re-measured for this section).

| quantity | value | note |
|---|---|---|
| Title-bar rule | **y = 199**, 1 px | same as every D4/D5 page |
| Rows | **13**, ink tops **291 -> 771**, **pitch 40 px exactly**, ink height 18 | all 13 re-measured; pitch identical on every step |
| Label column | **right-aligned at x = 939** | re-measured 938-940 across all 13 rows |
| Value column | **left-aligned at x = 979-980** | re-measured 978-980 |
| Column gutter | 38-41 px | new |
| Lower rule | **y = 879**, 1 px | new: the page uses the D4 rule pair, not one rule |
| Back hint | `O Back`, ink x 1155-1239, y 897-916 | same rect as the information page |
| Focus | **none** | `Down`/`Up` produce no change (mean frame delta 0.82 / 1.31 luma = the wave) |
| Category band | **off-frame** | no icon-column elevation: x 240-320 mean luma 11.8 against 25.8 on the depth-1 parent page |
| Info bar | **absent** | luma 0 at x 1300 / 1340 / 1700 / 1900 on y 100 |

**The 40 px pitch is a depth-2 number.** D24 and U1 measured **80 px** on the depth-1 list pages and
`GEOMETRY-ROOT.md` 9.3 confirmed the item-row grid at 80 there. Two page classes, two grids: a single row
pitch constant cannot serve both, and 12.2's 40 is *not* a correction of D24 - it is a second class.

The second depth-2 type is a **message page** (`depth2/ics-page-01.png`, `Internet Connection Settings`):
title bar, rule at y 199, a two-line body, an `OK` button, `X Enter` / `O Back` hints; `Down`/`Down`/`Up`
move nothing (`depth2/ics-page-down1/down2/up1.png`). So a depth-2 page inherits one of the two shapes this
audit already knows - the read-only table or the D4 message page - and neither has focusable rows.

### 12.3 Band geometry and timing, per page

**Timing** (1:1 crop bursts, 131-235 fps, clock 100 %; no rescale anywhere):

| transition | start after key | ramp | at rest | frame |
|---|---|---|---|---|
| Choice band (Theme) **open** | **+62 ms** | **270 ms** (0 -> 1 by +330 ms) | +330 ms | `bands/theme-band-open2/` (539 f, 158.9 fps) |
| Choice band (Theme) **close** | **+11 ms** | **200 ms** | +215 ms | `bands/theme-band-close2/` (560 f, 164.7 fps) |
| Choice band (Screensaver) **close** | +11 ms class | 200 ms class | - | `bands/screensaver-band-close/` (452 f, 151.2 fps), an independent repeat |

The metric is the band background's luma inside its own region (Theme 0.37 -> 19.7 open, 19.7 -> 0.36
close). This closes U3's "exact open/commit duration" for the video half and confirms it is **not** the
item menu's timing (that surface starts +115...150 ms and ramps 245-255 ms, `ITEM-MENU-INFO-FIRMWARE.md`
G1): the two overlays share a look, not a clock.

**Geometry at rest** (re-measured on the stills):

| quantity | Screensaver band | Theme band |
|---|---|---|
| frame | `bands/screensaver-band-rest.png` | `bands/theme-band-rest2.png` |
| left border | **x = 1260-1261** (1 px step, 0 -> 31 luma at y 100) | **x = 1324-1325** (1 px step, 0 -> 31) |
| gradient peak | 56 luma at x about 1320 | 55-56 at x about 1400 |
| profile | 46 @1280, 34 @1500, 20 @1560, 8 @1600, 2 @1640, 1 @1680, 0 @1720 | 44 @1500, 35 @1560, 27 @1600, 15 @1640, 6 @1680, 1 @1720 |
| geometric zero | about 1680 (last >=2 luma at 1648) | about 1744 (last >=2 luma at 1744) |
| vertical extent | full height y 0-1079 | full height y 0-1079 |
| item text left | 1276-1278 (11.6) | 1341 = border + 17 |

Two corrections to 11.6, and they do not cancel:

- **(a) There is no single left edge.** 11.6 generalised the Display page's 1260 to "the band". The Theme
  band's border is at **1324/1325**, i.e. **64 px further right**, on a page of the same class. 11.8's hunk
  hardcodes `x0 = 1260` for the firmware band and is therefore correct for the Screensaver band only. A
  per-page origin is needed, and the frames do not say what sets it.
- **(b) "Fading out by 1660" and "zero by 1690" are two different thresholds, and the batch's own numbers
  are the geometric one.** Re-measured: the Screensaver band is visually gone by about 1660-1680 (>1 luma
  left) and geometrically zero at about 1680; the Theme band at about 1720-1744. State which is meant.

The parent page behind the band is dimmed and visible left of the border and through the gradient
(`bands/theme-band-rest2.png`, `bands/screensaver-band-rest.png`), so 11.8's `fillRect` dim is the right
kind of thing; its alpha is still a re-measure target.

### 12.4 List contents: Theme, Color, Background, Font

All four lists were opened and cancelled with Circle, so nothing was committed (`lists/theme-page-after*.png`).

**(a) Theme - 4 items, no overflow.** `lists/theme-list-rest.png`: `Install`, `Original` (current and
focused), `Classic`, `Air Paint`. The current item carries **no check and no dot** - it is only brighter -
which closes U4 for this page as well. `Air Paint` carries a small preview icon on its row. The list has
exactly 4 items and cannot scroll (`MANIFEST` section 5 no. 8).

**(b) Color - 12 chips, unlabelled, live preview.** `lists/color-list-rest.png` and `-down1..14.png`:

| quantity | value |
|---|---|
| items | **12**: a white `Original` chip plus 11 colour chips |
| labels per chip | **none**, anywhere; there is no text column in the list |
| chip pitch | **40 px**, exact over the ten chips measured (571, 611, 651, ... 931) |
| chips drawn at rest | 11, first ink top **531**, last ink bottom **957**, plus a down-chevron at y 969-977 |
| the 12th chip | appears when focus reaches it (`-down14.png`); the focused chip takes a **1 px outline** the others do not |
| label line above the column | ink y **494-523**, and it reads the **current** value (`Original`) - it does **not** follow focus |
| preview | focus **recolours the whole screen live** (`-down14.png` is red end to end) |

Correction to U9's "Color: `Original` + 11 colour chips + overflow arrow": the arrow is drawn while the
column is not at its end, and the list is 12 items, not 12 labels. Correction to 11.7b ("this build's 6
colours"): the firmware's list is **12 unlabelled chips** whose only identity is the colour itself, so a
native list cannot be built from labels at all - it needs 12 rendered chips.

**(c) Background - 3 items plus a submenu row.** `lists/background-list-rest.png`: `Brightness` (first
row, with a right-hand chevron: a submenu, not a value), then `Original` (current and focused), `Classic`,
`Wallpaper`. Clamps at `Wallpaper`. Row ink tops measured 381 / 537 / 581 / 621.

**(d) Font - 3 items.** `lists/font-list-rest.png`: `Original` (current and focused), `Rounded`, `Pop`. Clamps
at `Pop`.

**(e) The row origin is per-list, and this is a correction to 11.8.** The same string - `Original` - sits at
ink top about **494** on the Theme list and about **537** on the Background list: two pages, one word, one
font, 43 px apart. So the band does not place its rows from a single origin, and 11.8's
`first = 505-(rows-1)*20` (and 11.8 Hunk A's `first`/`rowZero` pair) cannot reproduce both. Measured row
tops: Theme 261 / 494 / 532 / 576, Background 381 / 537 / 581 / 621, Font 491 / 539 / 578, Color label line
494 then chips from 531. The 40 px pitch holds inside a list; the origin does not hold across lists. The
frames do not explain the 43 px difference - recorded as an open item, not a rule.

### 12.5 Video and System page tails

The two pages that were cut at y 1080 in batch 1 were walked to the last row.

| page | rows | last row | frame |
|---|---:|---|---|
| Video (`page_bdvd_config`) | **19** | `50 Hz Video Output` (Automatic) | `tails/video-page-top.png`, `video-down1..22.png` |
| System (`page_console_config`) | **25** | `About PlayStation(R)3 (PS3(TM))` | `tails/system-page-top.png`, `system-down1..20.png` |

This closes 9.2's "Video continues below the fold" note and 9.5's tail item, and makes the full ordered
lists in `MANIFEST` section 4.6 the authoritative row sets for both pages (19 and 25 rows, one row per
frame of the walk). Neither page scrolled past its last row.

### 12.6 BG1 at rest: the focused settings row has no filled rectangle

This is the batch's most consequential negative result for this document. Measured on
`bands/theme-page-ready.png` (Theme Settings page, `Color` row focused) and `bands/theme-plate-rest-color.png`:

- **no filled rectangle** anywhere on the row: at y 470-560 the page background is 3-5 luma from x 405 to
  x 1300, i.e. the same value as the rows above and below. There is no plate edge, no rounded corner, no
  fill;
- the row is marked by **brighter text** plus a **soft gradient confined to the text block**: 10-18 luma
  over the 3-4 background inside y 470-500, back to 3.9 by y 545-560;
- the description line under the row is unaffected.

So the depth-1 settings page marks focus with light, not with a plate - in contrast to the XMB column's
item plate (x 405-731, y about 420-560, peak about 126 luma; `error/column-corrupted-focused.png`,
`GEOMETRY-ROOT.md` 10.1). Two surfaces, two focus languages, on the same 505/80 grid.

What this does to the rows above:

| old line | what stands |
|---|---|
| 9.5 "the exact BG1 focus-plate rectangle inside the band (not separable from the band's gradient)" | still not separable **inside the band**, but on the page at rest there is nothing to separate - there is no plate to measure |
| 11.8 Hunk A's placeholder dim and its note "the BG1 focus plate inside the band (9.5 still open)" | the dim stays a re-measure target; the "focus plate" language should not become a `fillRect` on a settings row |
| 3.2's description of the native's settings focus | the native's soft text glow is the right *kind*; a plate would be wrong |

### 12.7 What this batch does not change

- **D24 (row pitch 80)** stands for depth-1 pages; 12.2 adds a 40 px depth-2 class beside it, not instead
  of it.
- **U1's focused-row expansion** (+177 px first step on a depth-1 page) is untouched by this batch.
- **U2's band profile** (11.6) stands as the Display page's profile; 12.3 corrects its generalisation, not
  its numbers.
- **The 4-item Theme list, the 5-item Screensaver list and the 110-item time zone list** in 11.4 are
  unaffected: this batch confirms Theme (4) and does not re-open the RCO enumerations.
- **D22 (no dot), U4 (no check)** are confirmed again on the Theme list.
- The choice data model in 11.x needs **no value change** for Color/Background/Font (they write no
  preference yet); what changes is 11.7b's note *why* Color cannot be labelled.

### 12.8 Still open after batch 2

- U5 / U3 audio half (the commit cue) - still no audio channel.
- The `software_update_plugin` RCO (U8) and the update failure page (the update path kills vsh).
- A depth-2 page **with focusable rows** - does not exist on this console (12.2).
- The two timer lists behind `System Auto-Off` / `Controller Auto-Off` (D20).
- What sets a band's left edge (1260 vs 1324) and its row origin (494 vs 537) - measured, not derived.
- The band's rendered font size and cap height - the 40 px pitch and x 1276 / x 1341 are measured, the
  glyph height is not.
- The dim alpha behind a band.

No source was edited for this section; no RCO, threshold or reference was touched.



---

## 13. Phase 11.3 applied: the choice band (draw half), 2026-10-27/28

Status: implemented in `src/view.cpp` and `src/view.h` only. The data half (section 11) and the second
capture batch (section 12) are the evidence this pass draws from; no other source file was edited. The
band is one drawing with two measured parameter sets - the settings choice list and the item options menu
- and the page behind it is dimmed by the page's own content factor, not by a plate over the frame
(13.2). Section 11.8's hunks are applied in shape; where the frames disagree with their numbers the
frame value is used and the correction is dated below (13.3).

### 13.1 What the slice changed

| file | change |
|---|---|
| `src/view.h` | `View::ChoiceBand` (the committed model's labels, values, writability, provenance and scroll flag, cached by Preferences key), the resolver declarations and the focus rule |
| `src/view.cpp` | `PanelBand`/`BandStop` parameter sets for the two measured bands; `drawOptionsPanel` gains the band form (measured fill, fixed or scrolling rows, no dot); the settings choice column resolves its firmware list and draws the band; the item menu draws its own band with its own numbers; the page under a band is dimmed; the info bar is suppressed while a band is up |

No draw path reads the .rco for a list until a settings choice actually opens: the committed
`settingsChoiceCatalogue()` is called once, lazily, on the first choice band (one line in the log), and
the per-frame cost afterwards is a hash lookup plus at most 110 value comparisons.

### 13.2 The dim is the page's, not a plate over the frame, and its factor is a fit

Section 11.8's hunk A dims the whole screen (0,0,1920,1080 black at alpha 120). The frames do not show
that. Measured on the same page with and without the band:

| element | no band | with band | ratio | frames |
|---|---:|---:|---:|---|
| the parent's white help text (255 at rest) | 255 | **136** | 0.533 | `settings/display-page-rest.png` vs `settings/screensaver-list-rest.png` (batch 1) |
| the same, batch 2 Screensaver band | 255 | **104** | 0.408 | `bands/display-page-rest.png` vs `bands/screensaver-band-rest.png` |
| the same, batch 2 Theme band (the `Theme` label) | 255 | **131** | 0.513 | `bands/theme-page-ready.png` vs `lists/theme-list-rest.png` |
| the category band's briefcase icon | 123.1 | **123.2** | 1.000 | batch 2, same pair |
| the background at x 1200-1250, y 1000-1080 | 29.8 | 29.8 | 1.000 | batch 1, same pair |
| the item menu's page label | 255 | **139** | 0.545 | `dialogs/options-state-before.png` vs `dialogs/options-open-01.png` |

Two facts follow. The category band and the background are untouched - the briefcase icon is identical
to within 0.1 luma and the background region is bit-identical - so a 120-alpha plate over the whole frame
would darken both by 47 %, which the frames contradict. What dims is the page's own content, uniformly. Three
readings of the page text give 0.408, 0.513 and 0.533; **0.52 is a fit inside that bracket** (0.545 for
the item menu's single measurement), and the batch 2 Screensaver still is the outlier. The value is
applied as a factor on the page's column (icons, labels, help text, value column all scale together), and
it eases in with the band's own open amount. This entry replaces 9.5's "the dim alpha behind a band" as a
measured bracket with a stated fit, not as an attested constant.

The category band and background staying lit also means the native's `drawScene` needs no new pass: the
factor is applied where the page's rows are drawn (`drawColumns`), outside the category loop and the
background.

### 13.3 Two parameter sets, from the frames

| quantity | settings choice band | item options menu band | source |
|---|---:|---:|---|
| left edge (settled) | **1260** | **1320** | batch 1 y 100: first lit column 1260 / 1320, a 1 px step above the fill |
| 2 px left step | 15/32/34 at 1260-1262 | 15/34 at 1320-1322 | same |
| fill profile (alpha) | 23 at 1263, 37 at 1270, 50 at 1280, 60 at 1300, 61 at 1303-1330, 53 at 1400, 45 at 1450, 37 at 1500, 25 at 1550, 9 at 1600, 2 at 1650 | 25 at 1324, 38 at 1330, 51 at 1340, 61 at 1360, 62 at 1366-1388, 58 at 1420, 55 at 1450, 48 at 1500, 39 at 1550, 28 at 1600, 12 at 1650, 4 at 1690, 2 at 1700 | measured column by column at y 100, where the page behind is black so the luma is the alpha |
| geometric zero | **1690** | **1742** | last column at luma 0 |
| pitch | **40 px** | **38 px** | 11.6 / ITEM-MENU E1 |
| rows | the focused row on the 505 focus row; the list scrolls with focus | fixed at 552 + 38 i (baselines 560/598/636); does not scroll | 11.6; E1 |
| row text left | **1276** | **1334** | 11.6 (ink 1276-1278); E1 (ink 1337) |
| dot / check | none | none | U4; 12.4a |
| page behind | 0.52 of its own brightness (fit) | 0.545 | 13.2 |

Three corrections to the committed numbers, all measured:

- **The fill's left stops.** 11.8 encodes 0/.005 = 0/60, i.e. 60 luma at x 1262 and 61 by x 1320. The
  frame's own profile is 15/32/34 at 1260-1262, 23 at 1263 and 60 only at 1300, so the committed stops
  over-brighten x 1263-1299 by up to 35 luma. The drawn gradient uses the measured samples (the table
  above); rendered against `settings/screensaver-list-rest.png` at y 100 the drawn profile is within 2
  luma of the frame at every sample, and the 2 px left step comes out at 31 luma against the frame's 34.
- **The item band's edge and end.** E1's "left edge x ~1330, fading to nothing by x ~1690" is approximate.
  The frame's first lit column is 1320 (a 2 px step, 15 at 1320 and 34 at 1322) and its last is 1741 (4
  luma at 1690, 2 at 1700, 0 at 1742). The drawn band uses the measured 1320/1742; rendered against
  `dialogs/options-open-01.png` at y 100 it is within 1-3 luma at every sample.
- **The item band's rows.** E1's baselines 560/598/636 are ink baselines; the draw rect centres the em box,
  whose baseline sits 8 px below the rect centre at 23 px, so the rows are centred on 552 + 38 i.

What the frames also show and this pass does **not** reproduce, recorded as open rather than guessed:

- **The band's left edge is per-list** (12.3a, 12.4e): the Theme band's border is 1324/1325, not 1260, with
  the same fill shape. This pass draws 1260 for every settings choice list, which is the number the
  Screensaver/Display list measures; a per-list origin needs a rule the frames do not give.
- **The Theme list separates its first item**: `Install` sits at ink top ~261, while `Original` (the
  current, focused item) is at ~494 and `Classic`/`Air Paint` follow at ~532/~576, i.e. a 233 px gap after
  the first row and the measured 40 px pitch inside the sublist. The drawn band keeps one 505-centred
  column, so its `Install` row lands ~204 px below the frame's. The same pattern appears on Background
  (`Brightness` at 381, then `Original`/`Classic`/`Wallpaper` at 537/581/621).
- **The item menu's rows are two groups on the frame** (`Sort By`/`Group Content` at 318/356, then
  `Start`/`Delete`/`Information` at 560/598/636). The drawn band keeps the committed single top-anchored
  block; the grouping is a row-set and order question for the shell half, which does not build that row
  set.

### 13.4 Rows: labels from the committed model, focus by value, no dot, scrolling

The band's labels and their authored order come from `settingsChoiceCatalogue()` (11.4), not from
`choices()`: the drawn list is the RCO's own MItems in document order, so `Date Format` draws
`YYYY/MM/DD`, `DD/MM/YYYY`, `MM/DD/YYYY` and `Time Format` draws `12-Hour Clock`, `24-Hour Clock` -
the orders `choices()` does not have (11.3, 11.6).

Not every row gets the band. A row draws the firmware band only when the committed model holds a
firmware list for it, i.e. `source` is `rco` (the RCO's own page) or `capture` (Theme, whose page ships
empty and whose four labels are the 2026-10-28 capture's). A row whose list is this build's own
(`source` `native`, `rco-page-empty` or `none` - the 13 native lists of 11.7b/c and the four rows with no
page at all) keeps today's options panel at x 1330 and logs one line naming the reason, so no function is
lost:

```
view: settings choice band display/screensaverMinutes -> 5 items from rco (page_display_config_screensaverstartup), current row 2, fits the band
view: settings choice band appearance/theme -> 4 items from capture (page_theme_config_theme), current row 1, fits the band
view: settings choice band clock/dateFormat -> 3 items from rco (page_date_time_config_date_format), current row 2, fits the band
view: settings choice band clock/timeZone -> 110 items from rco (page_date_time_config_timezone), current row 0, scrolls
view: settings choice panel appearance/colour keeps this build's options panel: the firmware has no list of its own for this row (the Page has no LowerMenu items; the plugin builds the colour chips at run time (12 captured, labels not transcribed))
```

**Focus is the drawn row that owns the model's selection**, in this order: the item whose value the model
would commit (`choiceValueMatches` against the selected entry's `option`), then the item the row's own
displayed value names, then the model's position. The first rule is what keeps the focused label and the
committed value paired when the RCO's order differs from `choices()`'s: on `Date Format` the drawn focus
follows the value, not the index, so the highlighted row is always the one a Cross would write. The second
rule exists for the time zone, whose firmware items are GMT-offset tokens no model value can match
(13.6a). No dot and no check is drawn on a band (U4, 12.4a): the player panel's dot stays inside the
`rows` path the in-player panel owns.

The settings list scrolls with the focused row, as 11.8's `first`/`rowZero` pair does: row i sits at
505 + (i - position) * 40 and rows outside the frame are skipped, so a 110-item list holds about 27 rows on
screen (the measured band capacity, 11.4) and moves under the fixed 505 focus row. The item menu's list
does not scroll (no Scroll object, E1): its rows are anchored at 552 + 38 i.

### 13.5 Verification

Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-band` (clean at `/W4
/permissive- /utf-8`; no warning from the new code). Binary
`C:\dev\out\native-band\re-ps3-native.exe`.

Captures: the settings chain is five Lefts into the Settings column, eleven Downs to `Display Settings`,
Enter, one Down to `Screensaver`, Enter, with `display/screensaverMinutes = 20` in the data directory so
the current value is the frame's `After 20 Minutes`; the time zone chain is the same column, seven Downs to
`Date and Time Settings`, Enter, four Downs to `Time Zone`, Enter. All captures pin `--render-clock 20
--background-clock 2026-10-27T21:43:00 --ui-clock 2026-10-27T21:43:00 --focus-time 2000`.

**Band fill against the frames** (added luma over the frame's own background, y 100):

| x | 1260 | 1261 | 1262 | 1263 | 1270 | 1280 | 1300 | 1320 | 1340 | 1400 | 1450 | 1500 | 1550 | 1600 | 1650 | 1690 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| frame | 15.2 | 32.2 | 34.1 | 23.1 | 37.1 | 50.4 | 60.3 | 61.3 | 60.3 | 53.3 | 45.4 | 37.1 | 25.1 | 9.0 | 2.0 | 0.0 |
| drawn | 20.1 | 31.0 | 27.0 | 22.1 | 36.0 | 48.1 | 58.1 | 59.1 | 58.1 | 51.1 | 43.8 | 36.0 | 24.0 | 9.0 | 2.0 | 0.0 |

and for the item menu, added luma over its own background:

| x | 1320 | 1322 | 1324 | 1330 | 1340 | 1360 | 1380 | 1400 | 1450 | 1500 | 1550 | 1600 | 1650 | 1690 | 1700 | 1742 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| frame | 15.2 | 34.1 | 25.4 | 38.1 | 51.3 | 61.2 | 62.4 | 60.5 | 55.5 | 47.6 | 39.4 | 28.1 | 11.9 | 3.9 | 2.0 | 0.0 |
| drawn | 18.1 | 31.1 | 25.0 | 37.0 | 49.1 | 59.1 | 59.1 | 58.1 | 53.1 | 46.1 | 38.0 | 27.0 | 12.0 | 4.0 | 2.0 | 0.0 |

**Region scores, before and after** (`work/ab-compare.ts`, the same reference frame and crop for both; the
"before" binary is `C:\dev\out\native-choices\re-ps3-native.exe`, the data-half build):

| crop | settings band, before | after | delta |
|---|---|---|---|
| band `1240,0,470,1080` | 16.21 dB / 0.6219 | **19.08 dB / 0.7538** | +2.87 dB / +0.132 |
| band interior below the rows `1260,620,430,460` | 20.22 / 0.8246 | **22.19 / 0.8992** | +1.97 / +0.075 |
| page left of the band `0,0,1240,1080` | 18.05 / 0.4345 | **18.64 / 0.4380** | +0.59 / +0.004 |
| info bar `1700,72,220,64` | 11.37 / 0.0038 | **30.34 / 0.0929** | +18.97 dB |

| crop | item menu, before | after | delta |
|---|---|---|---|
| band `1310,0,450,1080` | 17.82 / 0.7144 | **20.19 / 0.8016** | +2.37 / +0.087 |
| band interior `1310,700,430,380` | 21.37 / 0.8701 | **22.10 / 0.9087** | +0.73 / +0.039 |
| page `0,0,1310,1080` | 17.16 / 0.4014 | **18.19 / 0.4239** | +1.03 / +0.023 |
| info bar `1742,72,178,64` | 11.93 / 0.0042 | **30.33 / 0.0929** | +18.40 dB |

The label text is missing from every capture of the deliverable build (13.6b), so the crops above measure
the fill, the edges and the page. The rows were measured two ways instead, both on scratch builds under
`out/` that keep this pass's `view.cpp` verbatim:

- `out/native-band-gate`: the repository tree with `text_renderer.{cpp,h}` and `font_raster.{cpp,h}` taken
  from the 23:05 snapshot (`out/phase122-src/`), because the tree's own copies were mid-edit by another
  pass. This is the real GL glyph pipeline and gives the numbers below.
- `out/native-band-fallback`: the same `view.cpp` with `textRenderer_.draw` bypassed so the existing
  QPainter fallback draws the same strings in the same rects; it crosses the first build's ink positions
  within 1-2 px.

| measurement | frame | drawn |
|---|---|---|
| Screensaver rows, ink tops (x 1270-1660, thr 140) | 418 / 458 / 495 / 538 / 578 | 415 / 455 / 496 / 534 / 576 |
| Screensaver row ink left | 1275-1277 | 1276-1278 |
| item menu row ink | 539-560 / 581-598 / 621-638, ink left 1336-1337 | 542-565 / 580-598, ink left 1335-1336 |
| item menu row baselines | 560 / 598 / 636 | 561 / 599 |

The rows sit 2-3 px above the frame's, uniformly: `AlignVCenter` puts the cap ink about 1.5 px above the
rect centre while the frame's ink sits about 1.5 px below it. The audit's own 9.1 puts the focused row's
centre at y 505, which is the value the hunk uses and this pass keeps; the residual is a fit-level detail,
not a geometry error.

**The current value's row is the focused one, and there is no dot.** The Screensaver band draws
`After 5 Minutes`, `After 10 Minutes`, `After 20 Minutes` (brighter, on the focus row), `After 30
Minutes`, `Do Not Use` with `display/screensaverMinutes = 20`: the focused row is the persisted value,
put there by the value rule of 13.4 and not by the model's index. The Theme band draws its 4 items with
`Original` (the current theme) on the focus row, and the Date Format band its 3 items with `MM/DD/YYYY`
(the store's default) on the focus row - both from the log line's `current row`. No dot: the dot is gated
on `rows && !firmwareBand`, the settings band draws with `rows` null and the item menu with
`firmwareBand` set, so the only bright ink on the focus row is the glyph run. The marker column confirms
it: at the item menu x 1332-1336 carries 6 ink rows (the `O` of `Open`) where a 9 px dot would carry 23.

**Scrolling, on the 110-item time list** (`out/native-band-gate`, positions from the sidecars 0 / 60 / 110;
row ink tops over x 1270-1660, the page's own text at 350-390 in every frame):

| state | visible row ink tops |
|---|---|
| position 0 | **505 (focused)** then 545, 585, 625, 665, 705, 745, 785, 825, 865 - nothing above the focus row |
| position 60 | 425, 465, **505 (focused)**, 545, 585, 625, 665, 705, 745, 785, 825, 865 - the list has scrolled under the fixed focus row |
| end (drawn row 109) | 345, 385, 425, 465, **505 (focused)**, nothing below it - the last of the 110 items is on the focus row |

The focus row is at 505 in all three states, ink appears above it only once the list scrolls, and at the
end the column stops below the focus row: 110 items, 27 of them on screen at once, all drawn in the RCO's
authored order.

**The info bar is absent while a band is up.** Right of the band, where the plate and the analog clock sit
(rect 1329..1919 x 75..133, clock centred 1791,104), the frame is 0.00 mean luma; the before capture reads
48.59 mean / 255 max with 2263 pixels over 60 luma; the after capture reads 8.00 / 17.1 with none, which
is the background's own 8.1. The item menu is the same: 46.29 / 255 before, 8.00 / 17.1 after, 0.00 in
the frame. The item menu had to be suppressed too even though 11.8's hunk C says it does not: the frame
shows no plate on either surface.

**Determinism.** Two runs of the screensaver chain at `--render-clock 20` are byte-identical:
`6CB59C9CB171070930CC4E362DC82A1565E550DD86A178D07F495E5DA3397556` for both
`cap\det-1.png` and `cap\det-2.png` (1,056,231 bytes each).

**The gate.** `bun work\ab-layers.ts --capture --exe C:/dev/out/native-band/re-ps3-native.exe` scores
**5/6** on the deliverable build, failing `item label row` at 12.04 dB / 0.3860 against a baseline of
11.94 / 0.3910 and a budget of baseline - 0.0040 SSIM. That build has no text (13.6b). The same gate on
`out/native-band-gate` - this pass's `view.cpp` with the tree's own files, only the text renderer taken
from the pre-regression 23:05 snapshot - scores **PASS: 6/6**, layer for layer identical to the data-half
build's run:

| layer | data-half build (23:14) | this pass's view.cpp + working text | baseline |
|---|---|---|---|
| frame | 18.29 / 0.8342 | 18.29 / 0.8342 | 18.02 / 0.8301 |
| top background | 20.25 / 0.9363 | 20.25 / 0.9363 | 20.24 / 0.9344 |
| category icon row | 20.93 / 0.8990 | 20.93 / 0.8990 | 21.05 / 0.8950 |
| item label row | 11.95 / 0.3925 | 11.95 / 0.3925 | 11.94 / 0.3910 |
| clock text | 11.39 / 0.4500 | 11.39 / 0.4500 | 11.39 / 0.4105 |
| info bar | 14.98 / 0.7639 | 14.98 / 0.7639 | 14.78 / 0.6117 |

The canonical frame is the XMB root and has no band, so the band code cannot move it; the layer numbers
being identical to four decimals is the evidence that it does not. The deliverable build's 5/6 is the text
regression, and the gate must be re-run on it once that pass lands.

### 13.6 Findings outside this pass's write fence

**(a) The time zone's stored value cannot be a firmware token.** 11.3 says the stored value "is now the
zone's own item token". `Preferences::validBare` accepts only `System` or an id `QTimeZone` knows, so a
token is dropped on load, and `Shell::valueText`'s token-to-RCO-label branch (11.3) cannot be reached by
any value the store will keep. Consequence for this pass: the 110-item time zone band can never mark its
current row, because the current value is never one of the firmware's 110 GMT items. The band draws the
firmware's list and scrolls it; the current-row mark is verified on the Screensaver, Theme and Date Format
lists, whose values do correspond.

**(b) The tree draws no text at all in builds made after 23:34.** `src/text_renderer.cpp`,
`src/text_renderer.h` and `src/font_raster.cpp` were being edited by another pass while this one built;
every build from that window renders zero glyphs (clock text, item labels, band rows), deterministically
across runs, while the 23:14 data-half binary renders text. The renderer logs its frame work ("frame ...
in 40 draw(s), 572 glyph quads") and its mask composite ("union coverage active"), so it believes it drew.
This is not caused by the band (the root frame has no band and loses its text too) and it blocks the
row-label half of this section's pixel verification.

**(c) The item menu's row groups and the Theme band's separated `Install` row** are measured in 13.3 and
belong to the shell's row set, not to the drawing.

### 13.7 Still open after this slice

- The per-list band origin (1260 for Screensaver/Display, 1324 for Theme) and the Theme/Background lists'
  separated first row - 12.3a/12.4e, not derived and not invented here.
- The band's row text size and cap height: the 40 px pitch and x 1276 are measured, the glyph height is not
  (12.8). The drawn rows use 23 px, the panel's existing size, which is a fit.
- The dim's exact alpha: 0.52 is a fit inside a 0.408-0.535 bracket (13.2).
- The item menu's two row groups and its Sort By nesting.
- 11.9's list: the BG1 plate inside the band, the colour chips' rendered form, the two timer lists, the
  three missing page objects, the commit cue, and wiring the 45 RCO lists to live rows.
- The gate's 6/6 once the text renderer pass lands (13.5).

---

## 14. Phase 11.3 data fix: the time zone token is a stored value, 2026-10-28

Status: this closes **13.6a** and the parenthetical in **11.3** that said the stored value "is now the
zone's own item token". Edited source: `src/preferences.{h,cpp}` and `src/settings_menu.{h,cpp}`. No hunk
was needed in `src/shell.cpp` - this tree keeps `Shell::valueText`, `Shell::clockDateTime` and the
Settings page open in `settings_menu.cpp` - and `view.*`, `main.cpp`, `assets.*`, the harness and CMake are
untouched, as is every other choice list's values (11.3 stays true for all of them; `choices()` is
byte-identical). Pixels are unchanged by construction and by the gate (14.6).

### 14.1 The bug

`Preferences::validBare` accepted only `System` or an id `QTimeZone` knows. Both load paths - the version-2
load and the version-1 migration - go through `validBare`, so the token the choice model's Zone values are
was dropped on load and could never be saved. Three consequences, all fixed here:

- the stored value could never be one of the firmware's 110 items, so the band's current-row rule (13.4)
  could never mark a row on the time zone list;
- `Shell::valueText`'s token branch (11.3) was unreachable from a stored value, so the Time Zone row's
  value column could never show the RCO label;
- `Shell::clockDateTime`'s token branch was reachable only for a value set in memory, never for a saved one.

### 14.2 The accepted forms, and why the bound is where it is

`clock/timeZone` now accepts exactly four forms, and nothing else:

| form | example | accepted by |
|---|---|---|
| the default | `System` | since before this pass |
| the empty value | `` | new; behaves like `System` |
| an IANA id | `America/Los_Angeles` | since before this pass; the host list still produces these and every older file holds one |
| a firmware zone token | `date_time_config_timezone_pacific_us_canada` | new |

The token's bound is one rule, shared by the store and the choice model
(`Preferences::firmwareZoneTokenPrefix()` / `Preferences::isFirmwareZoneToken()`):

    "date_time_config_timezone_" + 1..48 x [a-z0-9_], first character a letter, no trailing "_"

That is the shape of the 110 MItem names without their `item_` namespace, which is exactly what
`firmwareChoiceValue(ChoiceValues::Zone, ...)` produces. The store validates the *form* rather than
membership because `Preferences` holds no firmware tree; membership is exact where it is used - the token is
resolved through `sysconf_plugin`'s English table, and a well-formed name this firmware does not ship
resolves to no label and no offset, so the row shows no value and the clock follows the host (logged once),
never a guessed zone. All 110 values the model produces pass the same test (14.6), so every item the band
draws can be written back and reloaded.

Refused, with the stored value left alone: an empty name, a name starting with a digit, a trailing
underscore, an upper-case name, a name with a space, the label instead of the token, a non-IANA path
(`Europe/Nowhere`), lower-case `system`, and a name longer than the 48-character bound. The version-1
migration keeps working unchanged: the shared time zone stays a bare key, the per-user keys still move
under `profile/local/`, and the file is still rewritten as version 2 (14.6).

### 14.3 The current row, reported by the model

`ChoiceList` gained `currentRow`: the `rcoIndex` of the item the persisted value selects, or -1 when
nothing in the list is current. `markCurrentRow()` derives it from the items' own `isCurrent`, so the list's
current row and the item's mark cannot disagree, and it logs the time zone's verdict once per stored value:

```
settings: clock/timeZone date_time_config_timezone_pacific_us_canada is firmware zone item 4 of 110 (GMT-08:00 Pacific Time (US, Canada))
settings: clock/timeZone America/Los_Angeles is not one of the firmware's 110 zone items; no current row (the band marks no item as current)
```

The band focuses by the rule it already had (13.4, rule 2: the drawn row whose label the row's displayed
value names). `Shell::valueText` now returns that label, so the band's own log line reports the item:

```
view: settings choice band clock/timeZone -> 110 items from rco (page_date_time_config_timezone), current row 4, scrolls
```

On the deliverable build the Date and Time page's dump carries the same verdict:
`entries-final.json.settings-date.json` -> `"currentRow": 4`, item 4
`GMT-08:00 Pacific Time (US, Canada)`, `isCurrent: true`.

**One residual, stated rather than papered over.** For a stored IANA id the *model* reports no current row
(`currentRow: -1`, logged above), but the drawn band still rests its focus on a row, because
`View::choiceBandFocus`'s third rule (`src/view.cpp:1227`) clamps to the model's position when no item
matches. `view.*` is outside this pass's write fence, so that drawn position is unchanged from 13.6a (the
band's line still reads `current row 109` for `America/Los_Angeles`). The model claims no row, `valueText`
names no item, and the log says so; closing the drawn half is a three-line change in the draw pass (return
-1 and draw no focus row when no item matches).

A second, pre-existing mismatch is visible here and belongs to the draw/shell halves, not to storage: the
options model for this row is still `choices()`'s host list (`System` plus every IANA id, ~600 entries)
while the band draws the firmware's 110, so moving the cursor past the drawn list clamps the focus. The
shell/choice-model half of "wire the 45 RCO lists to live rows" (11.7d) is what closes it; this pass does
not change the list, as required.

### 14.4 valueText and the clock

| stored `clock/timeZone` | Time Zone row's value column | clock |
|---|---|---|
| `date_time_config_timezone_pacific_us_canada` | `GMT-08:00 Pacific Time (US, Canada)` - the RCO item's own text, through `firmwareZoneFor()` | fixed `-08:00` from the label's `GMT-08:00` |
| `America/Los_Angeles` | `America/Los_Angeles` - the host list's label, as before | `QTimeZone("America/Los_Angeles")`, DST honoured |
| `System` / empty | `System` / empty | host clock |
| a well-formed token this firmware does not ship | empty, logged once | host clock, logged once |

`firmwareZoneFor(Assets&, token)` replaces `firmwareZoneOffsetSeconds()`: it returns the label as well as the
offset, is cached per token, and refuses anything the token test does not accept, so `valueText` and
`clockDateTime` read one resolution rather than two.

### 14.5 The diagnostic (`XMB_TZ_SELFCHECK`)

Set `XMB_TZ_SELFCHECK=<dir>` on any run that opens a settings page: on the first page the shell runs the
check once and writes `<dir>/tz-selfcheck.json`. It stores a token through the shell's own `Preferences`,
reloads the file through a second `Preferences` (the same read path a restart takes), migrates a
hand-written version-1 file, refuses the nine malformed values, then reports the band's current row, the
band's focus rule and the row's `valueText` for the token and for an IANA id, and puts the store back. It is
not a second implementation of the rules: it calls `Preferences`, `settingsChoiceLists()` and
`Shell::valueText()` and applies the band's own label rule, so what it reports is what the app does.

### 14.6 Verification

Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-tz` -> clean at `/W4
/permissive- /utf-8`, no warning from `preferences.cpp` or `settings_menu.cpp`; binary
`C:\dev\out\native-tz\re-ps3-native.exe`, 3,708,416 bytes, 2026-10-28 00:46.

**(a)-(d) in one run**: `--data-dir <token seeded> --render-clock 20 --ui-clock 2026-10-27T21:43:00
--background-clock 2026-10-27T21:43:00 --focus-time 2000`, script `full,wait,LEFT x5,DOWN x7,ENTER,wait,wait,...`,
with `XMB_TZ_SELFCHECK=C:/dev/out/native-tz/cap/selfcheck` and
`XMB_DUMP_ENTRIES=C:/dev/out/native-tz/cap/entries-final.json`. The JSON's four cases:

```
load      : accepted true, reloaded "date_time_config_timezone_pacific_us_canada", kept true, fileVersion 2,
            storedUnderSharedKey true
migration : from "version 1", reloaded "date_time_config_timezone_pacific_us_canada", kept true, fileVersion 2,
            volume 30 under profile/local/audio/volume, bare audio/volume gone, shared key stayed bare
tokenCase : accepted true, items 110, currentRow 4, bandFocus 4, bandFocusLabel "GMT-08:00 Pacific Time (US, Canada)",
            valueText "GMT-08:00 Pacific Time (US, Canada)", clock "10/27 6:43 PM",
            tokenItemsAcceptedByStore 110, focusNamesTheCurrentRow true
ianaCase  : accepted true, items 110, currentRow -1, bandFocus -1, valueText "America/Los_Angeles",
            clock "10/27 7:43 PM", focusNamesTheCurrentRow false
refused   : 9 of 9
```

Log lines from the same run (verbatim, 14.3 above): the token is item 4 of 110; the IANA id is no item;
`stores=1 reloads=1 migrates=1 (v1->v2, 2) -> band current row 4 of 110, valueText "GMT-08:00 Pacific Time
(US, Canada)", clock "10/27 6:43 PM"`; `iana ... current row -1 (band focus -1), valueText
"America/Los_Angeles", clock "10/27 7:43 PM"`.

The clock values are the fixed-offset resolution at work: the pinned UI clock is 21:43 at the host's
`America/Chicago` (-05:00), so `-08:00` is 18:43 and the IANA zone is 19:43.

**Capture with the stored token**, same pinned clocks, script `LEFT x5, DOWN x7, ENTER, wait,wait,wait,
grab (page), DOWN x4, ENTER, wait,wait,wait,wait, grab (band)`. The deliverable binary renders text in this
capture (the renderer regression of 13.6b was reverted in the tree by the pass that owns it before this
build):

| file | what it shows |
|---|---|
| `out/native-tz/cap/tz-token-page.png` | Date and Time page: `Time Zone` value column `GMT-08:00 Pacific Time (US, Canada)`, info-bar clock `10/27 6:43 PM` |
| `out/native-tz/cap/tz-token-band.png` | the 110-item band: `GMT-11:00 American Samoa`, `GMT-11:00 Midway Islands`, `GMT-10:00 Hawaii`, `GMT-09:00 Alaska`, then **`GMT-08:00 Pacific Time (US, Canada)`** glowing on the 505 focus row, `GMT-08:00 Tijuana`, `GMT-07:00 Mountain Time (US, Canada)` below |
| `out/native-tz/cap/tz-iana-page.png` | the IANA case's page: value column `America/Los_Angeles`, clock `10/27 7:43 PM` |
| `out/native-tz/cap/tz-iana-band.png` | the IANA case's band: no row is current; the drawn focus is the model-position fallback of 14.3 (the last rows), which is stated as open, not as a current-row mark |

Determinism of the band capture: two runs of the token chain give frames differing in 61 of 2,073,600
pixels (max 11 luma, one glyph's antialiased edge on row 1); the band's fill, rows and focus are identical.

**Gate.** `bun work\ab-layers.ts --capture --exe C:/dev/out/native-tz/re-ps3-native.exe` ->
`PASS: 6/6 layers within budget`, layer for layer identical to the stored baselines: frame 18.29 / 0.8342,
top background 20.25 / 0.9363, category icon row 20.93 / 0.8990, item label row 11.95 / 0.3925, clock text
11.39 / 0.4500, info bar 14.98 / 0.7639 (every delta +0.00 dB / +0.0000). The canonical frame has no band
and no time zone, so this is the expected result and the evidence that nothing on that path moved.

**Named states.** `--states` currently reports 0/12 on **any** text-drawing build of this tree, and that is
not this pass: the twelve stored state references were re-saved at 00:02:20 while the text renderer was
broken (the reference for `steam-game-root` has no labels and no clock digits), and the renderer was
restored at 00:42:59. The pre-change binary `out/native-final/re-ps3-native.exe` (00:01, textless) scores
12/12 only because it is textless. A control build of this tree whose single behaviour-affecting line
(`validBare`'s token branch) is reverted scores exactly the same as the deliverable on three states -
`steam-game-root` 27.59 / 0.9758, `subtitle-cue` 23.04 / 0.9597, `xmb-notification` 26.96 / 0.9725, to four
decimals - so the named states are unchanged by this pass; their references are stale and belong to the
text-renderer pass to refresh.

### 14.7 What stays approximate

- **A token has no DST rule.** The firmware's 110 zones are GMT-offset zones whose own label states the
  offset, so a token resolves to that fixed offset all year; the console applies "Daylight Saving" as a
  separate setting (`msg_summertime`, still `Action::Unavailable` in this build). An IANA id keeps DST
  through `QTimeZone`, which is why stored IANA ids still "work as they do today". So the same wall clock
  can differ by an hour between a token and the IANA id for the same city, and the token is the one the
  firmware's list names.
- **The drawn focus for a value that names no firmware item** is still the model-position fallback of
  `View::choiceBandFocus` (14.3): the model says no current row, the drawing rests on a row.
- **Membership is checked at resolution, not in the store**: a well-formed token this firmware does not
  ship is storable, and then resolves to nothing with one log line. `Preferences` has no firmware tree, and
  the alternative - duplicating the 110 names in the store - is the "typed-in firmware data" 11.1 refuses.
- **The options model is still the host IANA list** (`choices()` unchanged, requirement 5): the band draws
  110 items the model does not contain, so cursor moves past the drawn list clamp. Unchanged from 13.6a;
  wiring the row to the firmware list is the 11.7d slice.
- The band's row text size, the per-list origin and the dim factor stay as 13.3/13.7 left them; this pass
  draws nothing.

---

## 15. Dated measurement pass - 2026-10-28: what sets the band's left edge, its row origin, its type size, and the dim

Read-only pass on the two existing firmware batches, no new capture, no RPCS3 run, no source edit. It closes
four of the seven items on 12.8/13.7's list with numbers, and states which one is not closable from local
artifacts.

Tooling (helpers outside the repo, `C:\dev\work\`): `m.py` (RGB -> Rec.709 luma, then column/row mean
profiles and ink-run detection), `align.py` (two frames' profiles aligned on a chosen x), `dim.py` (a
two-frame luma ratio over a fixed ROI with a threshold mask), `series.py` (a fixed ROI across a burst of
crop frames, using each burst's own `rect.txt` origin). Python is the bundled runtime
(`C:\Users\Albert\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`, Pillow 12.3.0).
Frame paths are relative to `C:\dev\out\rpcs3-ref2-2026-10-28\` (`b2`) or `C:\dev\out\rpcs3-ref-2026-10-28\`
(`b1`); every image is 1920x1080.

### 15.1 The left edge is per page, and the page's RCO structure splits it exactly

Command (per frame): `m.py profx <frame> 1200 1420 95 105 3` - the 10-row mean luma per column at y 95..105,
where the page behind is black, so the first lit column is the band's own edge. The border is a 1 px step in
every frame (background 0.0-0.7 luma, then 14-15, then 31-34).

| list (parent page) | items | left border x | frame |
|---|---:|---|---|
| Screensaver (Display) | 5 | **1260-1261** | `bands/screensaver-band-rest.png` |
| Screensaver (Display), second session | 5 | **1260-1261** | `b1/settings/screensaver-list-rest.png` |
| Date Format (Date and Time) | 3 | **1260-1261** | `b1/settings/dateformat-list-rest.png` |
| Time Format (Date and Time) | 2 | **1260-1261** | `b1/settings/timeformat-list-rest.png` |
| Theme (Theme) | 4 | **1324-1325** | `lists/theme-list-rest.png` |
| Theme (Theme), second session | 4 | **1324-1325** | `b1/settings/theme-choice.png` |
| Background (Theme) | 4 rows | **1324-1325** | `lists/background-list-rest.png` |
| Background (Theme), second session | 4 rows | **1324-1325** | `b1/settings/background-choice.png` |
| Font (Theme) | 3 | **1324-1325** | `lists/font-list-rest.png` |
| Font (Theme), second session | 3 | **1324-1325** | `b1/settings/font-choice.png` |
| Color (Theme) | 12 chips | **1324-1325** | `lists/color-list-rest.png` |
| Color (Theme), second session | 12 chips | **1324-1325** | `b1/settings/color-choice.png` |
| Options menu (Triangle) | 3-5 rows | **1320** (b1 and b2) | `b1/dialogs/options-open-01.png`, `menus/game-menu-open-check.png` |

So the item menu's band is 4 px left of the Theme family's, not 6 px right of it: 10.3's 1330 was measured
on `menus/game-menu-open-check.png` with the fill threshold, and the 1 px border of that frame is at 1320
(0.0 -> 14.1 -> 31.2 at 1319-1321, fill 50.2 by 1344). `menus/game-menu-rest.png` is a mid-close frame (only
a 1 px edge at 1329 survives, peak 89) and must not be used as a rest measurement.

**The two values are one shape, translated.** `align.py` on `bands/screensaver-band-rest.png` at x0 1260
against `lists/theme-list-rest.png` at x0 1324, y 90..110:

| k (px from the border) | 0 | 30 | 60 | 90 | 120 | 180 | 240 | 300 | 360 | 420 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Screensaver luma | 14.1 | 52.3 | 56.1 | 54.3 | 51.0 | 43.2 | 34.2 | 20.1 | 4.1 | 1.0 |
| Theme luma | 14.6 | 53.3 | 56.6 | 54.8 | 51.5 | 43.6 | 34.7 | 20.6 | 5.0 | 1.2 |
| delta | -0.5 | -0.9 | -0.5 | -0.6 | -0.4 | -0.3 | -0.5 | -0.5 | -1.0 | -0.2 |

The whole 420 px profile, the gradient peak (56.1/56.6 at k 60) and the geometric zero agree within 1 luma
after a **64 px** shift. So the band is one authored shape whose left border is a per-page constant; the
right-hand fade is not independent data.

**What the frames rule out** (all with more than two samples):

- **Item count.** 5, 3 and 2 items all sit at 1260; 3, 4 and 12 items all sit at 1324. Count does not move it.
- **Row text extent / content width.** The two bands are the same profile under a pure translation while
  their longest labels differ by ~40 px ("After 20 Minutes" 1275-1450 vs "Classic"/"Air Paint" 1340-1417).
  A content-sized band would change width; this one does not.
- **The parent page's value column.** It is at x 1295-1301 on both pages (`m.py inkcol` on
  `bands/theme-page-ready.png` and `bands/display-page-rest.png`), so it cannot explain a 64 px difference.
- **Scroll position or focus index.** See 15.2: the rows never move with focus.
- **The session's theme or wallpaper.** Same session, same background: Screensaver 1260, Theme 1324.

**What it does correlate with, 1:1 and without exception: the page object's own child list in
`sysconf_plugin.rco`.**

| page object | children in the RCO | band x |
|---|---|---|
| `page_display_config_screensaverstartup` | `plane_*` + `_BG0` + `_BG1` + `_LowerMenu` + 5 `MItem` | 1260 |
| `page_date_time_config_date_format` | same shape, 3 `MItem` | 1260 |
| `page_date_time_config_time_format` | same shape, 2 `MItem` | 1260 |
| `page_theme_config_theme` | **none** - the page entry has no children at all | 1324 |
| `page_theme_config_color` | **none** | 1324 |
| `page_theme_config_background` | **none** | 1324 |
| `page_theme_config_font` | **none** | 1324 |

(`C:\dev\out\rco-dump\sysconf.txt`, `page_theme_config*` at lines 844-849, `page_display_config_screensaverstartup`
at 1178-1184, `page_date_time_config_date_format` at 869.) The four Theme pages ship as empty pages because
their lists are runtime data (colours, wallpapers, fonts, installed themes), which is why 11.7b could not
build them from RCO labels; the band for them is built by a different path and carries a different anchor.

**Where the number is not.** It is not authored data that can be read: a scan of all 2247 objects in
`sysconf.txt` for any non-zero `posX`/`posY` returns **zero hits** (`Select-String -Pattern 'posX=(-?[1-9]|-)'`).
Every page, plane and MList in that container carries `posX=0 posY=0 width=0 height=0`, and the MLists'
`unknown29/unknown30` pair is 0x37f/0x380 on 123 of them - including every Display and Date-and-Time choice
page checked - so no field distinguishes a 1260 page from a 1324 page.

**Proven:** the band is one fixed shape; its left border takes exactly two values inside `sysconf_plugin`
(1260-1261 and 1324-1325), and the split follows the page's RCO structure with no exception over 7 lists and
2 sessions. **Not proven, and not provable locally:** which code path computes it. There is no
`sysconf_plugin.prx` decompile in the tree (`out/native-reference/` holds `explore_plugin_all.c`,
`xmb_plugin_all.c` and `paf-range.c` only), so the step from "empty page object" to "1324" is unmeasured.
The missing artifact is a `sysconf_plugin.prx` decompile of the choice-page open path (the function that
creates the page and places its list), or a band capture on a *structured* page whose list is built at run
time for another reason. Until then 15.1's rule is a classification, not a derivation, and 11.8's
`x0 = 1260` stays correct for the Display/Date-and-Time pages only.

### 15.2 The row origin is per list; the rows never move

Command (per frame): `m.py inkrow <frame> <textLeft> <textLeft+30> 200 1000 140` - the ink runs of the
first glyph of each row, in a 30 px window at the row's own text column (1276 for a 1260 band, 1341 for a
1324 band). Text peak is 229 (unfocused) / 255 (the current row, which carries a glow); the band fill is
< 60 and the dimmed parent page behind the band is below 140 in this window, so the runs are rows.

| list | rows (ink tops, px) | pitch | first row | current row | frame |
|---|---|---|---:|---:|---|
| Screensaver | 418, 458, **498**, 538, 578 | 40 | 418 | 498 | `bands/screensaver-band-rest.png` |
| Screensaver, b1 | 418, 458, **498**, 538, 578 | 40 | 418 | 498 | `b1/settings/screensaver-list-rest.png` |
| Date Format | 458, **498**, 538 | 40 | 458 | 498 | `b1/settings/dateformat-list-rest.png` |
| Time Format | 458, **498** | 40 | 458 | 498 | `b1/settings/timeformat-list-rest.png` |
| Font | **498**, 538, 578 | 40 | 498 | 498 | `lists/font-list-rest.png` |
| Theme | 261, **501**, 541, 581 | 40 | 261 (`Install`) | 501 (`Original`) | `lists/theme-list-rest.png` |
| Background | 381, **541**, 581, 621 | 40 | 381 (`Brightness`) | 541 (`Original`) | `lists/background-list-rest.png` |
| Color | label line 494-523, chips 531, 571, 611, ... 931 | 40 | 531 | 531 | `lists/color-list-rest.png` |

Corrections and answers:

- **Not a scroll offset.** The list is a fixed grid and the focus plate travels: `lists/background-list-down1`
  through `-down9` all show rows at 381/541/581/621 (`m.py inkrow ... 1341 1371`), and the highlight moves
  541 -> 579 -> 619 and clamps at `Wallpaper`; `lists/theme-list-down1` through `-down6` all show
  261/501/541/577-581. No row moves by even 1 px across nine focus steps.
- **Not a heading offset.** No heading is drawn in any band. The Theme list's `Install` and the Background
  list's `Brightness` are **rows** on the same 40 px grid (both carry the right-hand chevron of a submenu
  row), separated from the value block by a whole number of pitches: **6 x 40 = 240 px** (261 -> 501) for
  Theme, **4 x 40 = 160 px** (381 -> 541) for Background. 12.4e's "~233 px" and "~43 px" are the same two
  gaps measured off the *glow-inclusive* tops of the current row (494 and 537); the grid gaps are 240 and
  40 px.
- **A current-value label line exists on exactly one list.** Color draws the current value above the column
  (ink 494..523, reading `Original`, which does not follow focus) and starts its chips at 531. No other
  captured list draws one, so it is not the general origin rule.
- **The origin is per-list authored spacing.** Six different origins (418, 458, 498, 261/501, 381/541,
  531). Five of the seven lists put the *current* row's ink top at 498-501, which is why 11.8's
  `first = 505-(rows-1)*20` looked right; the Background list breaks it by exactly one pitch (541) and the
  Color list by 33. A single formula cannot reproduce the set, and the frames contain nothing that varies
  with the 43 px - the difference is between two lists of the *same page*, same item count (4 rows) and
  same submenu-row shape.

**Proven:** rows are a per-list fixed grid at a 40 px pitch, no heading, no scroll, no single origin.
**Open:** what places the grid (a per-list authored y in the plugin's own table, or a computation over the
row count). Missing artifact: the same `sysconf_plugin.prx` decompile as 15.1, or a capture of a list whose
length is just past the band's capacity so that the vertical centring rule would reveal itself.

**Consequence for the tree's current parameter sets.** `src/view.cpp` now carries the per-list origins this
section measures (`kThemeBand` `rowCentre=505` with `groupGap=239`, `groupBreakRow=1`; the Background set
`rowCentre=548`). Two of the three numbers below are off, and the anchor itself is fine:

| parameter | tree | measured | basis |
|---|---:|---:|---|
| Theme value block -> Background value block | 548 - 505 = **43** | **40** (exactly one pitch: 501 -> 541) | both lists' non-focused rows share one 40 px grid phase: `Install` 261 and `Brightness` 381 are both 21 mod 40, and Theme's 541/581 and Background's 581/621 agree exactly, so a one-pitch difference cannot be 43 |
| `groupGap` (Theme's submenu row to the value block) | 239 | **240** (6 x 40, 261 -> 501) | `lists/theme-list-rest.png` |
| `rowCentre` (the band's row anchor) | 505 | the current row's cap centre measures 506.5 (Screensaver, ink 498..515) and 507.5 (Font, ink 498..517), so the anchor is within ~2 px | `bands/screensaver-band-rest.png`, `lists/font-list-rest.png` |

So the pitch and the group gap are confirmed by the frames, and the Theme/Background offset should be **545**,
not 548, if `rowCentre` is kept as the em-box centre. The one number the frames cannot explain is the 3 px
phase between the two page families' grids (Display/Date-and-Time ink tops are 18 mod 40, the Theme
family's 21 mod 40): it is either an authored per-page y or a metric difference between the two drawing
paths, and it is what makes the Display band's first row land at 418 and the Theme band's at 381.

### 15.3 The band row's type size: cap 18 px, em about 24 px, and no RCO style selects it

Command: `m.py inkrow <frame> 1276 1300 570 600 120` (and the same on the Theme band at x 1341).

| quantity | value | frame |
|---|---|---|
| cap height | **18 px** (`Do Not Use`, D at 578..595; `Classic`, C at 541..558; `After 5 Minutes`, A at 418..435) | `bands/screensaver-band-rest.png`, `lists/theme-list-rest.png` |
| ascender height | 18 px (`Classic`, 'l' cap-to-baseline; the face's ascender and cap are equal here) | `lists/theme-list-rest.png` |
| ink width, `After 5 Minutes` | **176 px** (1275..1450, 16 chars) -> 11.0 px/char | `bands/screensaver-band-rest.png` |
| ink width, `After 10 Minutes` | 192 px (1275..1466) | same |
| ink width, `Do Not Use` | 130 px (1277..1406, 10 chars) -> 13.0 px/char | same |
| ink width, `Classic` | 78 px (1340..1417, 7 chars) -> 11.1 px/char | `lists/theme-list-rest.png` |
| inter-word ink gap | 11-12 px (o->N at 1306..1318, t->U at 1355..1366) | same |

Two independent size estimates agree on **about 24 px em**: the inter-word gap plus the bearings puts the
space advance at 12-13 px, and the firmware's own space is 13.4 px at 27 px (`audit/TEXT-RENDERER.md` 5,
PAF 26.6 advance), giving em 24-26; the 16-character string sums to about 7.3 em in the face's own
advances, giving em 24.1 from 176 px. Cap/em is then 0.75, which is a normal cap ratio. So the band row is
**em ~24 px, cap 18 px**, and the native's 23 px (`SETTINGS 3.2`, `13.3`) is about 1 px small - its cap
lands at ~16.6 px and `After 5 Minutes` comes out ~8 px short.

**No RCO text style matches, because no style is attached to a band row at all.** A choice page's rows are
`MItem` objects and an `MItem` carries only a text reference:

```
MItem item_display_config_5min @625552 +24B text=text#882:msg_5min="After 5 Minutes" altText=null
```

(`C:\dev\out\rco-dump\sysconf.txt:1183`; the whole row block is 1183-1187.) There is no font, size, colour,
spacing or shadow field on it.
The only named list-text material in the local tree is `Text fullkey_list_text`
(`oskfullkeypanel_plugin`, `+232B`), and it carries the *same* block as every other Text object:
`size=10 topRed=1 ... shadowX=4 shadowY=-5 shadowRed=0.25` - identical to the 1304-of-1305 default
recorded in `src/text_renderer.cpp:855`. So the RCO cannot distinguish a "fullkey list" style from a menu
style: **the band row's px size is a module constant, not authored data**, and the number to reproduce is
15.3's em 24 / cap 18.

**The repository's own raster, measured.** The built tree's text self-check runs headlessly and prints the
raster's advance at every UI size:

```
XMB_TEXT_CHECK=1 re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --measure-reference <out>
text check: PASS metric-size-27 "Game Data Utility"  PAF 234.92 px vs shaped 234.84 px
text check: PASS metric-size-23 "Display All"        PAF 119.55 px vs shaped 119.42 px
text check: PASS metric-size-22 "System Information" PAF 211.44 px vs shaped 211.28 px
text check: PASS metric-size-19 "Sort By"            PAF  69.73 px vs shaped  69.66 px
text check: PASS metric-size-16 "This build cannot play it" PAF 191.13 px vs shaped 191.05 px
```

(binary `out/native-textrevert/re-ps3-native.exe`, 2026-10-27 23:58; the PAF column is the firmware's
unhinted 26.6 advance sum, so this is the raster's advance *authority*, not Qt's). At 23 px it shapes
10.87 px/char on an 11-character string, against the firmware band's 11.0 px/char on `After 5 Minutes` - the
same order, which is why 23 px looked plausible, but it is not a same-string check. **That check is still
missing:** the self-check samples a fixed string list (no arbitrary string), and every band-capable build
after 2026-10-27 11:58 renders no glyphs - `out/native-band/cap/band-screensaver.png` has **zero ink above
120 luma** anywhere in the band's row region, which is the glyphless-tree regression H6/H5 already record.
The missing artifact is a text-bearing build of the current tree (band code plus a working compositor), or a
one-line mode that shapes an arbitrary string.

### 15.4 The dim: one alpha, about 0.40, and the spread is capture timing

Command: `m.py mean <frame> <labelX0> <labelX1> 490 524` for the luma peak of the parent page's surviving
row, and the same on `1020 1200 490 524` for the background behind it; `dim.py` for the masked ratios.
When a band is up, the parent page keeps exactly one row - the one that was activated, at its old y - and
that row's **label and description** stay while its **value is suppressed** (max luma 2.9 at x 1290..1322
against 255 in the closed frame, on all four Theme lists).

| sample (band over page) | label peak | /255 | background patch mean | frame |
|---|---:|---:|---:|---|
| Screensaver over Display, b2 | **104.0** | **0.408** | 2.5 | `bands/screensaver-band-rest.png` |
| Date Format over Date and Time, b1 | **103.1** | **0.404** | 1.5 | `b1/settings/dateformat-list-rest.png` |
| Theme over Theme, b1 | **107.3** | **0.421** | 2.1 | `b1/settings/theme-choice.png` |
| Time Format over Date and Time, b1 | 121.8 | 0.478 | 34.8 | `b1/settings/timeformat-list-rest.png` |
| Color over Theme, b2 | 121.6 | 0.477 | 2.3 | `lists/color-list-rest.png` |
| Screensaver over Display, b1 | 136.6 | 0.536 | 6.4 | `b1/settings/screensaver-list-rest.png` |
| Background over Theme, b2 | 152.6 | 0.599 | 2.5 | `lists/background-list-rest.png` |
| Font over Theme, b2 | 153.5 | 0.602 | 3.3 | `lists/font-list-rest.png` |
| Theme over Theme, b2 (same page and band as 0.421) | 250.0 | **0.980** | 2.3 | `lists/theme-list-rest.png` |
| Options menu over the Game column, b1 | 178.6 | 0.700 | 22.7 | `b1/dialogs/options-open-01.png` |

The last-but-one row is the key: **the same page with the same band reads 0.42 in one session and 0.98 in
another**, so the difference between samples cannot be per-page. It is *when the still was grabbed*: the
band and its dim do not arrive together. `series.py` on `bands/theme-band-open2` (539 crop frames at
158.9 fps, crop origin 1150,150) shows the parent page's content decaying after the band is already
drawn - value-column peak 255 at f_0790, then 200.9 (f_0798), 191.3, 171.4, 148.6, 121.9, 108.1, 99.8,
66.1 (f_0949) and flat from there - a ramp of about 150 ms that starts *after* the band's own 62 ms start
and 270 ms ramp (12.3). A still taken inside that window reads high; one taken before it reads 0.98.

**Conclusion: a single constant is defensible, and it is the minimum over the settled samples - 0.404 /
0.408 / 0.421, i.e. alpha 0.40 +/- 0.01.** The 0.48-0.60 samples are mid-ramp stills, not per-page values.
The native's `contentDim = 0.52` and 13.2's 0.408-0.535 fit are both too bright; 0.40 is the number, and a
future capture must sample at least 1 s after the key to see it. The mechanism is a page-layer alpha, not a
plate over the frame: the wallpaper and the category band do not move (13.2), and the surviving row's white
text lands at 104 = 0.408 x 255 over a black page.

### 15.5 Status after this pass

| item | status |
|---|---|
| 12.8 "what sets a band's left edge (1260 vs 1324)" | **CLASSIFIED** (15.1): one shape, two per-page constants, split 1:1 with the page's RCO structure over 7 lists; the computing code is not in any local artifact |
| 12.8 "the band's row origin (494 vs 537)" | **CLOSED as per-list** (15.2): fixed grid, 40 px pitch, six origins, the 43 px is one pitch; no rule reproduces the set from local data |
| 12.8 "the band's rendered font size and cap height" | **CLOSED** (15.3): cap 18 px, em about 24 px; no RCO style is attached to a row at all |
| 12.8 "the dim alpha behind a band" | **CLOSED** (15.4): alpha 0.40 +/- 0.01, single constant; the spread is the 150 ms ramp |
| 13.2's `contentDim = 0.52` | superseded by 15.4's 0.40 |
| 13.3's row metrics for the band | unchanged (x 1276 / 1341, pitch 40); the type size it drew at (23 px) is 1 px small |
| 15.2's "no single origin" | stands; 11.8's `first = 505-(rows-1)*20` must not come back |

Not opened here: the U5/U3 commit cue audio, the timer lists behind System Auto-Off / Controller Auto-Off,
and the `software_update_plugin` RCO - all unchanged from 12.8.

No source file was edited for this section; no RCO, threshold or reference was touched.
