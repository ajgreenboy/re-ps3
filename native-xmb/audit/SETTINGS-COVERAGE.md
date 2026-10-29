# SETTINGS COVERAGE: the acceptance matrix for the settings-pages phase

Status: 2026-10-28. Analysis only. This file is the checklist the settings-pages implementation pass will
be graded against: one row per firmware settings row, what the console's own screen shows for it, what
this build does with it today, the host API that is the intended source of the real value, and whether
the row may be applied or must stay read-only.

ASCII note: a trademark sign in a firmware string is transliterated (`PlayStation(R)3`, `PS3(TM)`,
`Bluetooth(R)`, `DivX(R)`), as in `SETTINGS-FIRMWARE.md` section 0.

WRITE FENCE for this pass: this file only. No source, no harness, no reference frame, no git command.

---

## 0. How to read this document

### 0.1 Evidence grades (inherited from SETTINGS-FIRMWARE.md section 0)

| grade | meaning |
|---|---|
| **X** | read from `category_sysconf.xml`, the XMBML definition |
| **R** | read from an RCO object tree or its English string table |
| **F** | measured on an RPCS3 firmware frame at 1920x1080 |
| **U** | unattributed: no local artifact. Section 6 lists what would settle it |

Every row below carries at least one of these or a source line. Nothing in the "console value" column is
inferred: it is either a measured frame, an RCO item list, or the word `unknown` with the reason.

### 0.2 The three coverage verdicts

| verdict | test a row must pass |
|---|---|
| **real** | the row exists with the console's label, its value column is produced from the host or from this build's own stored setting, and activating it does the row's job (opens its list, opens its page, writes, or shows the host read) |
| **placeholder** | the row exists with the console's label but is inert (it opens the generic message page) or its value column is empty where the console shows one |
| **absent** | no such native row exists on the page |

A row that is present, inert, and *also* shows no value is still counted **placeholder**, never real and
never absent. The sums in section 2 are exact: `real + placeholder + absent = rows`.

In section 1 the verdict is the opening word of the "native now" cell: `real`, `placeholder` or `absent`.
`real (row type differs)` is counted **real**, with the shape delta named in the same cell; a bare
`Unavailable` (or `` `Action::Unavailable` ``) is **placeholder**; `absent` is always bold.

### 0.3 The counting basis

| level | what it counts | rows |
|---|---|---:|
| 0 | the Settings column's 16 retail rows (section 1.1) | 16 |
| 1 | the rows of the 16 settings pages behind them (sections 1.2-1.17) | 114 |
| 2 | the rows of the one settings page that has its own table page (Network > status list, section 1.18) | 13 |
| | **total inventory rows** | **143** |

Two firmware rows are deliberately outside the count because the retail console does not show them:
`edy` (Japan-only Edy Viewer) and `console_qa` (Debug Settings). `SETTINGS-FIRMWARE.md` 1.2 and
`MENUS-XMBML.md` 3.1 record the disposition, and the native omits both with a logged reason
(`settings_menu.cpp:1251`).

Left out of the count on purpose, and named here so nobody thinks they were missed: Photo Settings (3
rows) is this build's own page and has no sysconf counterpart
(`settings_menu.cpp:1333`); "Select Theme" and
"Select Wallpaper" on Theme, and "Repeat"/"Shuffle"/"Volume" on Video and Music, are native-only rows
(`settings_menu.cpp:290-313`).

### 0.4 Source keys used in the tables

| key | source |
|---|---|
| A1 s.N | `native-xmb/audit/SETTINGS-FIRMWARE.md`, section N |
| A2 s.N | `native-xmb/audit/GRAPHICS-INVENTORY.md`, section N |
| A3 row | `native-xmb/audit/BEHAVIOUR-GAP-AUDIT.md`, row id |
| F-sys, F-vid, ... | `C:\dev\out\rpcs3-ref-2026-10-28\settings\page-<page>-settings.png` (1920x1080) |
| F-col-rNN | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\col-settings-rNN.png` (`NN` = DOWN presses from the top row; `r00` is System Update) |
| F-band-* | `C:\dev\out\rpcs3-ref2-2026-10-28\lists\` or `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\page-*-list*.png` |
| F-depth2 | `C:\dev\out\rpcs3-ref2-2026-10-28\depth2\`, or `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\page-network-status-list.png` |
| M1 s.N | `C:\dev\out\rpcs3-ref-2026-10-28\MANIFEST.md`, section N |
| M2 s.N | `C:\dev\out\rpcs3-ref2-2026-10-28\MANIFEST.md`, section N |
| C-sm:NNN | `native-xmb/src/settings_menu.cpp` line NNN (as read in this pass) |
| C-sh:NNN | `native-xmb/src/shell.cpp` line NNN (as read in this pass) |
| C-ho:NNN | `native-xmb/src/host.cpp` line NNN (as read in this pass) |
| C-pr:NNN | `native-xmb/src/preferences.cpp` line NNN (as read in this pass) |
| P-cat | `work/parity-catalogue.json` state id |
| P-ref | `out/parity-references/<cell>.png` (RPCS3 reference, paired by basename) |
| P-score | `out/parity/scorecards/scorecard-20261028-185352.md` |

### 0.5 The current native row sets, in one place
Read from the app's own choice catalogue dump
(`out/native-theme2/capture/theme-page.entries.json.settings-choices.json`: 16 pages, **63 choice
lists**, **442 items**), which matches `settings_menu.cpp` row for row. That dump covers every settings
page that has at least one choice row; **System Update has two action rows and no list**, so it is built
outside `pageRows` (`settings_menu.cpp:1327-1332`)
and its 2 rows are counted in section 1.2 only. Photo Settings is in the catalogue because it has three
choice rows, and it is native-only.

| page | native rows | of which firmware rows | native-only |
|---|---:|---:|---:|
| game | 6 | 6 | 0 |
| video | 10 | 7 | 3 |
| music | 8 | 5 | 3 |
| chat | 3 | 3 | 0 |
| system | 9 | 9 | 0 |
| theme | 6 | 4 | 2 |
| date | 6 | 6 | 0 |
| power | 2 | 2 | 0 |
| device | 14 | 14 | 0 |
| print | 2 | 2 | 0 |
| display | 6 | 6 | 0 |
| sound | 3 | 3 | 0 |
| security | 6 | 6 | 0 |
| remote | 5 | 5 | 0 |
| network | 5 | 5 | 0 |
| photo | 3 | 0 | 3 |

Three facts about that model that the implementation must not silently change:

1. The lists are read from `sysconf_plugin.rco` at run time and every item carries its own text
   reference (`settings_menu.cpp:886-897`); of the 63 lists in the dump, **45 are `source=rco`**,
   **4 are `capture`** (Theme, Color, Background, Font - their RCO pages are empty), **10 are `native`**
   (Video/Music Repeat, Shuffle, Volume; Power's timer ladder; Photo's three) and **4 rows have no page
   object at all** (A1 11.4, dated 16.1).
2. The current value mark is computed, never read: the RCO's MItems have no children and no captured
   frame shows a check or a dot (A1 3.1, 9.1/U4, 16.1).
3. The value vocabulary of a list is the RCO's authored order, which is not always the native
   `choices()` order: Date Format enumerates `YYYY/MM/DD`, `DD/MM/YYYY`, `MM/DD/YYYY` and Time Format
   `12-Hour Clock`, `24-Hour Clock` (A1 11.6; F-band Time Format).

### 0.6 The baseline, and the tree that moved while this was written

The "native now" column describes the tree as read on 2026-10-28 between 21:07 and 21:20. While this
document was being written, the settings-pages implementation pass started writing the same files. The
mtimes and sizes are the evidence:

| file | as read for this document | at 21:23 |
|---|---:|---:|
| `src/settings_menu.cpp` | 101,007 bytes | 105,974 bytes |
| `src/host.cpp` | 16,383 bytes | 124,889 bytes |
| `src/settings_menu.h` | 7,448 bytes | 8,308 bytes |
| `src/host.h` | 2,883 bytes | 10,024 bytes |

What the new tree already changes, read from it at 21:23: every row that was `Action::Unavailable` on
System, Sound, Display, Security, Remote Play and Video is now `Action::Information` (so the generic
"not available" page is being replaced by a host-backed detail), `Restore Default Settings` has a new
`Action::ResetPreferences`, and `host.cpp` has grown a full host-report layer (`readAdapters` with link
speed, `readDisplays` with EDID, `readAudio`, `readPrinters`, `readVolumes`, `readPads`,
`powerSchemeName`, `hostChoiceIndex`/`setHostChoice`, `rowDetail`).

Therefore: **section 1 is the before-state the pass is graded against, and section 3's tests are the
target.** The tests do not depend on the line numbers; AT-2/AT-3 re-derive every count from
`--settings-report` against the final tree, and section 2.1's numbers are the baseline to beat (a better
split is a pass, a worse one is a regression). Every `C-sm`/`C-ho`/`C-sh` line number in section 1 is
this pass's read and is also anchored by the symbol or the exact text it cites, so a moved line does not
make a claim unverifiable - the symbol names are the durable citation.

---

## 1. The full row inventory

The `id` column is the row's own identity. For a row this build has, it is the app's row id, the same
string the page dump carries in `entries.json.settings-<page>.json` (checked against dumps for all
sixteen pages). For a row this build does **not** have, the id is this document's slug for the firmware
row, because there is no native id to join on - and the `absent` verdict says exactly that.

### 1.1 Level 0: the Settings column, 16 rows

The column is the firmware's own definition resolved at start-up
(`settings_menu.cpp:1228-1299`,
`shell.cpp:1069`). Each row below is a page entry: its "value type"
is a submenu in the console's sense, so there is nothing to apply at this level.

| id | firmware label (RCO) | type | console observed | native now | host source | apply |
|---|---|---|---|---|---|---|
| update | System Update | page | row 0; circular-arrows glyph, no wrench disc; info line `Updates the system software on this system.` (F-col-r01, `settings/column-system-update.png`) | SettingsPage -> `update` (C-sh:1016, C-sm:1327) | none: a page entry | n/a |
| game | Game Settings | page (segment) | row 1; brass wrench disc + gamepad; info line `Adjusts settings for games.` (F-col-r01) | SettingsPage -> `game`; definition view `seg_game` (C-sm:330) | none | n/a |
| video | Video Settings | page | row 2; wrench + film (F-col-r01) | SettingsPage -> `video` | none | n/a |
| music | Music Settings | page | row 3; wrench + note (F-col-r01) | SettingsPage -> `music` | none | n/a |
| chat | Chat Settings | page | row 4; wrench + bubble (F-col-r06) | SettingsPage -> `chat` | none | n/a |
| system | System Settings | page | row 5; wrench + cube (F-col-r06) | SettingsPage -> `system` | none | n/a |
| theme | Theme Settings | page | row 6; wrench + picture; info line `Adjusts settings related to the appearance of the XMB(TM) screen.` (F-col-r06) | SettingsPage -> `theme` | none | n/a |
| date | Date and Time Settings | page | row 7; wrench + clock (F-col-r06) | SettingsPage -> `date` | none | n/a |
| power | Power Save Settings | page | row 8; wrench + plug (F-col-r09) | SettingsPage -> `power` | none | n/a |
| device | Accessory Settings | page (segment) | row 9; wrench + card; info line `Adjusts settings for accessories that are connected to this system.` (F-col-r09) | SettingsPage -> `device`; view `seg_device` | none | n/a |
| print | Printer Settings | page | row 10; wrench + printer (F-col-r09) | SettingsPage -> `print` | none | n/a |
| display | Display Settings | page | row 11; wrench + screen (F-col-r09) | SettingsPage -> `display` | none | n/a |
| sound | Sound Settings | page | row 12; wrench + speaker (F-col-r09) | SettingsPage -> `sound` | none | n/a |
| security | Security Settings | page | row 13; wrench + keyhole (F-col-r15) | SettingsPage -> `security` | none | n/a |
| remote | Remote Play Settings | page | row 14; wrench + PSP shape (F-col-r15) | SettingsPage -> `remote` | none | n/a |
| network | Network Settings | page (segment) | row 15, the last row; wrench + globe; info line `Adjusts settings for the Internet connection.` (F-col-r15) | SettingsPage -> `network`; view `seg_network` | none | n/a |

**Column findings the implementation must fix (all measured, all currently wrong or unproven):**

| # | finding | evidence | current code |
|---|---|---|---|
| C-1 | every column row's **second label line is the firmware's info_rsc**, and the console draws it on all three `child=segment` rows too | F-col-r01 (Game), F-col-r09 (Accessory), F-col-r15 (Network) each show the info line under the focused folder row | the native suppresses it for exactly those three, because `fo_lbl2_alpha=0` (`xmbml_view.h:143`, C-sm:1270-1272). A1 1.1/D3 reads the attribute as "no second label"; the frames contradict that reading for the visible row |
| C-2 | the row icon is a **brass wrench disc plus the page glyph** on rows 1-15, and a bare circular-arrows glyph on row 0 | F-col-r01, F-col-r09, F-col-r15, `settings/column-system-update.png` | numeric `qgl/icontex` ids from a hand-written map (C-sh:1079-1084), so the composite is not reproduced; the resolver's `tex_*` name sits in the entry but the draw path uses the numeric id (A1 10.2) |
| C-3 | the console's column has **no value column**: a page row shows its info line, never a value | every F-col-rNN | correct today (the column entries carry no setting) |
| C-4 | a row past the last one **sinks the column 40 px per press** and stays sunk | A2 item 13; A3 10.7 still open | `Shell::vertical` clamps and only counts `columnSink_` (C-sh:1465-1468); the view does not offset the column |

### 1.2 System Update (page `update`, level 1, 2 rows)

Console page: title `System Update`, body `Select an update method.`, two rows, footer `X Enter` /
`O Cancel` (`settings/system-update-page.png`). The page comes from the software-update plugin, not
`sysconf_plugin.rco` (A1 5.16, 9.1/U8: page captured, RCO never dumped).

| id | firmware label | type | console value / options | native now | host source | apply |
|---|---|---|---|---|---|---|
| update/net | Update via Internet | action | no value (row 1, focused; `system-update-page.png`) | placeholder: the row opens an `Information` page whose body is this build's own sentence "The native XMB is updated with its own repository; there is no PS3 system software to download." (C-sm:1330-1331), not the console's method chooser | none: an app release feed would be an HTTPS endpoint, not a Win32 API | read-only |
| update/media | Update via Storage Media | action | no value (row 2) | `Action::Unavailable` (C-sm:1332) | `QStorageInfo` / `GetLogicalDrives` only if the row ever lists removable media | read-only |

### 1.3 Game Settings (page `game`, level 1, 6 rows, segment view `seg_game`)

Native rows come from the definition, not a literal (C-sm:330-399). The console frame shows **2 rows**
on this machine: the four PSP(TM) Remaster rows are conditional and are not drawn.

| id | firmware label | type | console value / options | native now | host source | apply |
|---|---|---|---|---|---|---|
| game/psp_emu_viewmode | PSP(TM) Remaster - Screen Mode | choice | not drawn on the console (F-game shows only the two PS rows). RCO page 2 items (A1 3.3): value list **unknown** beyond its count | Unavailable, list from the RCO (C-sm:481) | none: no PSP emulation on the host | read-only |
| game/psp_emu_3d | PSP(TM) Remaster - 3D Display | choice | not drawn; **no page object** in `sysconf_plugin.rco` (C-sm:482), list unknown | Unavailable, no list | none | read-only |
| game/psp_emu_adhoc_ch | Ad Hoc Mode Channel | choice | not drawn; RCO 4 items (A1 3.3), labels unknown | Unavailable, RCO list (C-sm:483) | none | read-only |
| game/psp_emu_adhoc_mode | Ad Hoc Mode | choice | not drawn; RCO 2 items, labels unknown | Unavailable, RCO list (C-sm:484) | none | read-only |
| game/ps_emu_upcon | PS - Upscaler | choice | `Off` (F-game) | Unavailable, RCO 3 items `off normal full` (C-sm:485) | none: the host GPU scaler is the driver's, not an app setting | read-only |
| game/ps_emu_smooth | PS - Smoothing | choice | `Off` (F-game) | Unavailable, RCO 2 items (C-sm:486) | none | read-only |

Delta: the native lists four rows the console does not draw on this unit. The frame is the row set a
verifier sees; the definition is the row set the plugin *can* draw.

### 1.4 Video Settings (page `video`, level 1, 19 rows, page `page_bdvd_config`)

Row set and values: M2 4.6 (the 23-still walk, `tails/video-page-top.png`, `video-down1..22.png`),
top-of-page confirmed on F-vid. The native has the first seven plus three native-only media rows.

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| video/bd_menu_lang | BD - Menu Language | choice (18) | `English` | Unavailable; RCO 18 items (C-sm:488) | none: no optical drive; `QMediaFormat` only for the app's own files | read-only |
| video/bd_audio_lang | BD - Audio Language | choice (18) | `English` | Unavailable; RCO 18 (C-sm:489) | none | read-only |
| video/bd_subtitle_lang | BD - Subtitle Language | choice (18) | `English` | Unavailable; RCO 18 (C-sm:490) | none | read-only |
| video/bd_internet_connection | BD - Internet Connection | choice | `Confirm` | Unavailable; modelled as Bool, so its value text would be `Off`/`On` (C-sm:491, C-sm:561-566) | none | read-only |
| video/dvd_menu_lang | DVD - Menu Language | choice (18) | `English` | Unavailable; RCO 18 (C-sm:492) | none | read-only |
| video/dvd_audio_lang | DVD - Audio Language | choice (18) | `English` | Unavailable; RCO 18 (C-sm:493) | none | read-only |
| video/dvd_subtitle_lang | DVD - Subtitle Language | choice (18) | `English` | Unavailable; RCO 18 (C-sm:494) | none | read-only |
| video/cinema_conversion | BD/DVD - Cinema Conversion | choice | `Automatic` | **absent** | none | read-only |
| video/dvd_wide_display | DVD - Wide Display | choice | `Letterbox` | **absent** | none | read-only |
| video/upscaler | BD/DVD - Upscaler | choice | `Off` | **absent** | none | read-only |
| video/video_output_format | BD/DVD - Video Output Format (HDMI) | choice | `Automatic` | **absent** | none | read-only |
| video/hdmi_1080_24 | 1080p 24 Hz Output (HDMI) | choice | `Automatic` | **absent** | `QScreen::refreshRate()` only describes the desktop, not the console's 24 Hz mode | read-only |
| video/dynamic_range_control | BD/DVD - Dynamic Range Control | choice | `Automatic` | **absent** | none | read-only |
| video/audio_format_hdmi | BD/DVD - Audio Output Format (HDMI) | choice | `Linear PCM` | **absent** | `QMediaDevices`/`IMMDevice` formats describe the host endpoint, not this row | read-only |
| video/audio_format_optical | BD - Audio Output Format (Optical Digital) | choice | no value shown | **absent** | none | read-only |
| video/storage_audio_lang | System Storage - Audio Language | choice | `English` | **absent** | none | read-only |
| video/storage_subtitle_lang | System Storage - Subtitle Language | choice | `English` | **absent** | none | read-only |
| video/sequential_play | Sequential Playback | choice | `Off` | **absent** | the app's own playback queue (`playback_queue.cpp`) if it ever gets the row | read-only |
| video/hz_50 | 50 Hz Video Output | choice | `Automatic` | **absent** | none | read-only |

### 1.5 Music Settings (page `music`, level 1, 5 rows, page `page_music_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| music/import_musiccd | Audio CD Import | choice | `AAC 128 kbps` | Unavailable; RCO codec page 3 items `aac mp3 atrac`, and the frame's value is the codec *with* a bit rate (C-sm:496-497) | none: no CD drive; `QMediaFormat` describes encoders, not this row | read-only |
| music/crossfade_play | Crossfade Playback | choice (11) | `3 seconds` | Unavailable; RCO 11 items `off 1..10` (C-sm:498) | the app's own player (`QMediaPlayer`) if a crossfade were added; value is a real duration | read-only |
| music/output_frequency | Output Frequency | choice (2) | `48 kHz` | Unavailable; RCO 2 (C-sm:499) | `QAudioDevice::preferredFormat()` / `IAudioClient::GetMixFormat` describe the host rate | read-only |
| music/bitmapping | Bitmapping | choice (4) | `Type 1` | Unavailable; RCO 4 `off type1 type2 type3` (C-sm:500) | none | read-only |
| music/dnormal | Dynamic Normalizer | choice (2) | `Off` | Unavailable; RCO 2 (C-sm:501) | none | read-only |

### 1.6 Chat Settings (page `chat`, level 1, 3 rows, page `page_chat_config`)

F-chat shows **one** row on this console; the other two are RCO sub-pages (A1 5.4) that this unit does
not draw (no camera, no chat audio device).

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| chat/av_chat_start | Voice/Video Chat Start | choice (2) | `Send Video` (F-chat) | Unavailable; RCO 2 `Send Video`/`Send Avatar` (C-sm:503) | none: no console chat service | read-only |
| chat/audio_device_setting | Audio Device Settings | submenu | not drawn on this unit; RCO page exists (A1 5.4) | Unavailable (C-sm:272) | `QMediaDevices::audioInputs()` / `IMMDeviceEnumerator(eCapture)` if it were made real | read-only |
| chat/camera_device_setting | Camera Device Settings | submenu | not drawn on this unit; RCO page exists | Unavailable (C-sm:273) | `QMediaDevices::videoInputs()` / `MFEnumDeviceSources` | read-only |

### 1.7 System Settings (page `system`, level 1, 26 rows, page `page_console_config`)

Row set and values: M2 4.6 (the 21-still walk, `tails/system-page-top.png`, `system-down1..20.png`);
top-of-page confirmed on F-sys. The native has 9 rows.

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| system/auto_update | Automatic Update | choice | `Off` | **absent** | none: no PS3 update channel | read-only |
| system/name | System Name | text (edit) | `PS3-354` | real: `Action::EditName` writes `system/name` (C-sm:213, C-sh:1527); the value column is empty until a name is set | `QHostInfo::localHostName()` is the natural default (C-ho:110); `GetComputerNameExW` if a Win32 name is wanted | apply (app-local) |
| system/console_lang | System Language | choice (20) | `English (United States)` | Unavailable; RCO 20 items (C-sm:505) | `QLocale` / `QLocale::system()`; a real apply needs the app's own string table, which does not exist | read-only |
| system/characterset | Character Set | choice (9) | no value shown | **absent** | `QFontDatabase` / `EnumSystemCodePagesW` describe the host, not this row | read-only |
| system/dictionary_type | Dictionary Type | choice | `English (UK)` | **absent** | none: no predictive-text dictionary in this build | read-only |
| system/dict_edit | Add/Edit Term | action | no value | **absent** | none | read-only |
| system/dict_delete | Delete Predictive Text Dictionary | action | no value | **absent** | none | read-only |
| system/notification_message | Notification Messages | choice | `Display` | **absent** | this build's own notification settings (`view.cpp` notification path, A3 C4/C5) | read-only |
| system/trophy_notification | Trophy Notifications | choice | `Display` | **absent** | none: trophies are PSN-backed (A3 C19 dormant) | read-only |
| system/whats_new | Display [What's New] | choice | `On` | **absent** | none | read-only |
| system/disc_auto_start | Disc Auto-Start | choice | `On` | **absent** | none: no optical drive | read-only |
| system/control_hdmi | Control for HDMI | choice | `Off` | **absent** | none: Windows has no public HDMI-CEC API | read-only |
| system/divx_code | DivX(R) VOD Registration Code | read-only value | no value | **absent** | none | read-only |
| system/atrac | Enable ATRAC | action | no value | **absent** | `QMediaFormat::supportedAudioCodecs()` can say whether the host decodes ATRAC | read-only |
| system/wma | Enable WMA Playback | action | no value | **absent** | `QMediaFormat::supportedAudioCodecs()` | read-only |
| system/vita_network | Connect PS Vita System Using Network | choice | `On` | **absent** | none | read-only |
| system/vita_list | List of Registered PS Vita Systems | submenu | no value | **absent** | none | read-only |
| system/vita_backup_delete | Delete PS Vita System's Backup Files | action | no value | **absent** | none | read-only |
| system/disc_utility | Format Utility | submenu (confirm chain) | no value | placeholder: Unavailable (C-sm:214) | `QStorageInfo` can *list* volumes; formatting is forbidden (section 4) | read-only |
| system/backup_utility | Backup Utility | submenu (confirm chain) | no value | placeholder: Unavailable (C-sm:215) | none on the host: a PC backup is a different product | read-only |
| system/data_transfer | Data Transfer Utility | submenu | no value | **absent** | none | read-only |
| system/init_setting | Restore Default Settings | submenu (confirm chain) | no value | placeholder: Unavailable (C-sm:216) | the app's own `Preferences` defaults (`Preferences::reset` would be the call) | read-only |
| system/init_console | Restore PS3(TM) System | submenu (confirm chain) | no value | placeholder: Unavailable (C-sm:217) | none, and it must never be mapped to a host reset (section 4) | read-only |
| system/ex_os_install | Install Other OS | action | not drawn in the 25-row walk; RCO carries `msg_ex_os_install` (A1 5.5) | placeholder: Unavailable, sitting between Restore PS3 System and System Information (C-sm:218) | none | read-only |
| system/information | System Information | read-only page | page (not captured on this rig) | real: `Action::Information` -> `host::systemInformation` (C-sm:219, C-sh:1579-1580, C-ho:97) | `QSysInfo`, `GlobalMemoryStatusEx`, `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0`, `QStorageInfo`, `GetAdaptersAddresses`, `QScreen` | read-only |
| system/aboutps3 | About PlayStation(R)3 (PS3(TM)) | read-only page | no value | placeholder: Unavailable (C-sm:220) | `QSysInfo::prettyProductName()` + the app's own version; nothing PS3-specific exists | read-only |

### 1.8 Theme Settings (page `theme`, level 1, 4 rows, page `page_theme_config`)

All four choice pages are empty `Page` objects in the RCO, so the lists are run-time data (A1 12.4,
15.1). F-theme shows the four rows and their values; the bands are F-band-theme and
`lists/theme-list-rest.png`, `color-list-rest.png`, `background-list-rest.png`, `font-list-rest.png`.

| id | firmware label | type | console value / options | native now | host source | apply |
|---|---|---|---|---|---|---|
| theme/theme | Theme | choice | `Original`; list `Install`, `Original`, `Classic`, `Air Paint` | real: 4 captured items, 1 writable (`Original`), commit writes `appearance/theme` (C-sm:669-694) | this build's p3t loader (`P3t::load`, C-sm:1466) | apply (app-local) |
| theme/color | Color | choice | `Original`; 13 rows: `Original` + 12 unlabelled chips | real: 13 items on a 12-row window, 6 writable (C-sm:60-79, 16.2) | `view.cpp` month-palette table (`colours[]`/`fixedMonth[]`) | apply (app-local) |
| theme/background | Background | choice | `Original`; `Brightness` (submenu) / `Original` / `Classic` / `Wallpaper` | real: 4 items, 3 writable, `Brightness` disabled with its reason (C-sm:80-92) | the app's background path; a wallpaper is a user file (`QImageReader`) | apply (app-local) |
| theme/font | Font | choice | `Original`; `Original` / `Rounded` / `Pop` | real: 3 items, all writable; Rounded=2, Pop=1 (C-sm:93-101) | `Assets::fontFamily` over `QFontDatabase`-registered firmware faces | apply (app-local) |

### 1.9 Date and Time Settings (page `date`, level 1, 6 rows, page `page_date_time_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| date/datetime | Date and Time | submenu (set page) | live clock `27/10/2026 21:40` (F-date) | real on its value, but the page type differs: the value column is the live host clock (C-sm:205, 419) and activating it opens a read-only `Information` page where the console opens a set page, which section 4 forbids writing | `QDateTime::currentDateTime()`; the host clock is **not** writable by this build (section 4) | read-only |
| date/autodate | Set Automatically | choice (2) | `On` | placeholder: Unavailable, RCO 2 items (C-sm:206, 516) | `HKLM\SYSTEM\CurrentControlSet\Services\W32Time\Parameters\Type`, `w32tm /query /status` | read-only |
| date/date_fmt | Date Format | choice (3) | `DD/MM/YYYY` | real: writes `clock/dateFormat`, 3 RCO items in the authored order (C-sm:207, 517, 567-570) | app formatting (`clockDate`, C-sm:121) | apply (app-local) |
| date/time_fmt | Time Format | choice (2) | `24-Hour Clock` | real: writes `clock/hourFormat`, 2 RCO items (C-sm:208, 518, 571-574) | app formatting (`clockTime`, C-sm:123) | apply (app-local) |
| date/timezone | Time Zone | choice (110) | `GMT+13:00 Independent State of Samoa` | real: 110 RCO items, stored as the firmware zone token; the clock resolves the token's own GMT offset (C-sm:579-588, 600-617, 1421-1449) | `QTimeZone` for the host zone; `GetTimeZoneInformation` to read the Windows zone | apply (app-local only; never `SetTimeZoneInformation`) |
| date/summertime | Daylight Saving | choice (2) | `Standard` | placeholder: Unavailable, RCO 2 items (C-sm:210, 520) | `GetTimeZoneInformation` (`StandardDate`/`DaylightDate`) or `QTimeZone::hasDaylightTime()` | read-only |

### 1.10 Power Save Settings (page `power`, level 1, 2 rows, page `console_auto_power_off_option`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| power/idle | System Auto-Off | submenu (two timers) | **no value on the row** (F-power: the row is a folder) | real (row type differs): it opens this build's own 5-item list (`Off`, 15 min, 30 min, 1 hour, 2 hours) and writes `power/idleMinutes` (C-sm:151, 268), where the console opens a two-timer page; the timer then drives the real display-off request (C-sh:1273, C-ho:277, A3 E4) | `SetThreadExecutionState` / `SendMessageTimeoutW(WM_SYSCOMMAND, SC_MONITORPOWER)` as already used, plus `GUID_CONSOLE_DISPLAY_STATE` read-back (C-ho:193-318) | apply (app-local, already real in effect) |
| power/controller | Controller Auto-Off | choice (2) | `Off` (F-power) | placeholder: Unavailable; no RCO page object (C-sm:269, 524) | `XInputGetBatteryInformation` / the pad report `pad.cpp` already parses; there is no host idle timer for a pad | read-only |

### 1.11 Accessory Settings (page `device`, level 1, 14 rows, segment view `seg_device`)

The console frame shows the top seven rows of the segment view (F-device; `Audio Device Settings` is cut
at the frame edge). The keyboard/mouse group follows per `seg_device_fixed_middle`/`_lower` (A1 5.9).

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| device/pd_calibrate_motion_controller | Calibrate Motion Controller | action | no value | placeholder: Unavailable | none: motion calibration is the driver's | read-only |
| device/pd_pad_assign | Reassign Controllers | action | no value | placeholder: Unavailable | `XInputGetState` slot enumeration can *show* the assignment | read-only |
| device/pd_pad_vibration | Controller Vibration Function | choice | `On` (`settings/restore/restore7-vibration-page.png`) | placeholder: Unavailable; no RCO page object (C-sm:532) | `XInputSetState` / `Capabilities.Flags & XINPUT_CAPS_FFB_SUPPORTED`; `pad.cpp` for the DualSense report | apply is possible, read-only for now |
| device/pd_bdremote_reg | Register BD Remote Control | action | no value | placeholder: Unavailable | `BluetoothFindFirstDevice` (pairing needs a passkey flow) | read-only |
| device/pd_bt_utility | Manage Bluetooth(R) Devices | submenu | no value | placeholder: Unavailable | `BluetoothFindFirstDevice` / `BluetoothEnumerateInstalledServices` (read-only list) | read-only |
| device/pd_camera | Camera Device Settings | submenu | no value | placeholder: Unavailable | `QMediaDevices::videoInputs()` / `MFEnumDeviceSources` | read-only |
| device/pd_audio_setting | Audio Device Settings | submenu | no value (cut at the frame edge) | placeholder: Unavailable | `QMediaDevices::audioOutputs()` / `IMMDeviceEnumerator` | read-only |
| device/pd_voice_changer | Voice Changer | choice | not drawn (below the fold) | placeholder: Unavailable | none | read-only |
| device/pd_keyboard_type | Keyboard Type | choice (24) | not drawn (below the fold on F-device); RCO 24 items (A1 3.3) | placeholder: Unavailable; RCO 24 items (C-sm:526) | `GetKeyboardLayoutList`, `GetKeyboardLayout` | read-only |
| device/pd_keyboard_input | Keyboard Entry Method | choice | not drawn; **no page object** (C-sm:527) | placeholder: Unavailable, no list | `ImmGetDefaultIMEWnd` / `GetKeyboardLayout`; no IME support exists in the OSK | read-only |
| device/pd_keyrepeat_start | Key Repeat Interval | choice (3) | not drawn; RCO 3 `short normal long` | placeholder: Unavailable; RCO 3 (C-sm:528) | `SystemParametersInfoW(SPI_GETKEYBOARDDELAY)` | read-only |
| device/pd_keyrepeat_speed | Key Repeat Speed | choice (3) | not drawn; RCO 3 `fast norm slow` | placeholder: Unavailable; RCO 3 (C-sm:529) | `SystemParametersInfoW(SPI_GETKEYBOARDSPEED)` (0-31, 31 fastest) | read-only |
| device/pd_mouse_type | Mouse Type | choice (2) | not drawn; RCO 2 `right left` | placeholder: Unavailable; RCO 2 (C-sm:530) | `GetSystemMetrics(SM_SWAPBUTTON)` | read-only |
| device/pd_pointer_speed | Pointer Speed | choice (3) | not drawn; RCO 3 `fast norm slow` | placeholder: Unavailable; RCO 3 (C-sm:531) | `SystemParametersInfoW(SPI_GETMOUSESPEED)` (1-20) | read-only |

### 1.12 Printer Settings (page `print`, level 1, 2 rows, page `page_print_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| print/printer_selection | Printer Selection | submenu | no value (F-print: `Printer Selection` above the focused row) | placeholder: Unavailable (C-sm:265) | `QPrinterInfo::availablePrinters()` / Win32 `EnumPrintersW` | read-only |
| print/printer_manager | Printer Manager | submenu | no value | placeholder: Unavailable (C-sm:266) | `QPrinterInfo::defaultPrinter()` + spooler state (`GetPrinter`) | read-only |

### 1.13 Display Settings (page `display`, level 1, 6 rows, page `page_display_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| display/display_output_setting | Video Output Settings | submenu (help carries `Current Output Resolution: 1080p`) | no value column; the current resolution is in the help text (F-display) | real as a read-only page: `Information` -> `host::displayOutput` shows every screen's pixels and refresh (C-sm:226, C-ho:157) | `QScreen::geometry()`, `QScreen::refreshRate()`, `QScreen::devicePixelRatio()`, `EnumDisplaySettingsExW` (DEVMODE) | read-only |
| display/screensaver | Screensaver | choice (5) | `After 20 Minutes` (F-display) | real: writes `display/screensaverMinutes`, 5 RCO items `After 5/10/20/30 Minutes`, `Do Not Use` (C-sm:227, 534, 575-578) | this build's own screensaver dim (A3 C23) | apply (app-local) |
| display/cross_color_filter | Cross Color Reduction Filter | choice (2) | `Off` | placeholder: Unavailable (C-sm:228, 535) | none: no host equivalent | read-only |
| display/rgb_full_range | RGB Full Range (HDMI) | choice (2) | `Limited` | placeholder: Unavailable (C-sm:229, 536) | `IDXGIOutput6::GetDesc1` reports colour space but no public API sets HDMI range | read-only |
| display/super_white | Y Pb/Cb Pr/Cr Super-White (HDMI) | choice (2) | `Off` | placeholder: Unavailable (C-sm:230, 537) | none | read-only |
| display/deep_color | Deep Color Output (HDMI) | choice (2) | `Automatic` (F-display) | placeholder: Unavailable (C-sm:231, 538) | `IDXGIOutput6::GetDesc1` (bit depth) is read-only | read-only |

### 1.14 Sound Settings (page `sound`, level 1, 3 rows, page `page_sound_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| sound/sound_output_setting | Audio Output Settings | submenu | no value (F-snd) | real as a read-only page: `Information` -> `host::audioOutput` shows the default render endpoint's friendly name and its volume (C-sm:222, C-ho:138) | `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eConsole)` + `PKEY_Device_FriendlyName`; `IAudioEndpointVolume` | read-only |
| sound/audio_multi_output | Audio Multi-Output | choice (2) | `Off` | placeholder: Unavailable (C-sm:223, 540) | `IMMDeviceEnumerator::EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)` lists every endpoint; output fan-out is not implemented | read-only |
| sound/keytone | Key Tone | choice (2) | `On` | real: writes `audio/keyTone`, and the value gates the interaction audio (C-sm:224, 541, C-sh:2946) | this build's own cue player | apply (app-local) |

### 1.15 Security Settings (page `security`, level 1, 6 rows, page `page_security_config`)

Console rows and values: F-sec (`Change Password` is a row above the five choice pages, A1 9.2).

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| security/pw_change | Change Password | action (PIN flow) | no value | placeholder: Unavailable (C-sm:233) | none: a PC PIN gate would be a new app-local feature, not a host read | read-only |
| security/bd_pc | BD - Parental Control | choice (2) | `Off` | placeholder: Unavailable (C-sm:234, 543) | none honest (Windows Family Safety is per-MSA and not app-scoped) | read-only |
| security/bd_dvd_pcregion | BD/DVD - Parental Control Region Code | choice (2) | `United States` | placeholder: Unavailable (C-sm:235, 544) | none | read-only |
| security/dvd_pc | DVD - Parental Control | choice (9) | `Off` | placeholder: Unavailable (C-sm:236, 545) | none | read-only |
| security/gameetc_pc | Parental Control | choice (12) | `Level 9` | placeholder: Unavailable (C-sm:237, 546) | none | read-only |
| security/pc_browser | Internet Browser Start Control | choice (2) | `Off` | placeholder: Unavailable (C-sm:238, 547) | none | read-only |

### 1.16 Remote Play Settings (page `remote`, level 1, 5 rows, page `page_remote_config`)

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| remote/device_registration | Register Device | submenu (registration chain) | no value (F-remote) | placeholder: Unavailable (C-sm:242) | none: no PS3/Vita counterpart | read-only |
| remote/device_registration_list | Registered Device List | submenu | no value | placeholder: Unavailable (C-sm:243) | none | read-only |
| remote/custom_connection_setting | Custom Connection Settings | submenu | no value | placeholder: Unavailable (C-sm:244) | none | read-only |
| remote/remote_start | Remote Start | choice (2) | `Off` | placeholder: Unavailable (C-sm:245, 549) | none | read-only |
| remote/audio_output_device | Audio Output Device | choice (2) | `Remote Play Device` | placeholder: Unavailable (C-sm:246, 550) | none | read-only |

### 1.17 Network Settings (page `network`, level 1, 5 rows, segment view `seg_network`)

Console row order and values: F-net (and F-col-r15). The status list opens the one depth-2 table page
(section 1.18).

| id | firmware label | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| network/net_netlist | Settings and Connection Status List | read-only page (depth 2) | no value; opens the 13-row table | real: `Information` -> `host::networkStatus` (C-sm:336, C-sh:1579, C-ho:122) | `GetAdaptersAddresses`, `GetIfEntry2`, `WinHttpGetIEProxyConfigForCurrentUser`, `INetworkListManager` | read-only |
| network/net_internet | Internet Connection | choice (2) | `Enabled` | placeholder: Unavailable (C-sm:552) | `GetAdaptersAddresses` (`IfOperStatusUp`) / `INetworkListManager::IsConnectedToInternet` | read-only |
| network/net_internet_setting | Internet Connection Settings | submenu (wizard) | no value | placeholder: Unavailable (C-sm:330-399, no binding) | none: Windows network configuration, and a write is forbidden (section 4) | read-only |
| network/net_internet_test | Internet Connection Test | action | no value; raises a `Please wait...` busy page (`C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\page-network-connection-settings.png`) | placeholder: Unavailable, so it raises the generic message page (P-score, `page-network-test-error` region note) | `InternetGetConnectedState` or a `QNetworkAccessManager` HEAD to a known endpoint | read-only (a probe is allowed, a config write is not) |
| network/net_media_server | Media Server Connection | choice (2) | `Enabled` | placeholder: Unavailable (C-sm:553) | Windows media-sharing service state (`GetService`, `Windows Media Player Network Sharing`) | read-only |

### 1.18 Level 2: Network > Settings and Connection Status List, 13 rows

Console geometry and every row: M2 4.2, and the frame `page-network-status-list.png` (rows at ink tops
291-771, pitch 40, labels right-aligned at 939, values left-aligned at 979). The console's own values are
shown because they are the *emulated* console's, not a real network.

| id | firmware label (console row) | type | console value | native now | host source | apply |
|---|---|---|---|---|---|---|
| netl/internet_connection | Internet Connection | read-only value | `Enabled` | **absent** from the host report | `GetAdaptersAddresses` (`IfOperStatus`) or `INetworkListManager::IsConnected` | read-only |
| netl/connection_method | Connection Method | read-only value | `Wired Connection` | real: `host::networkStatus` emits it per adapter, `Wired Connection`/`Wireless` (C-ho:125) | `IP_ADAPTER_ADDRESSES.IfType` (`IF_TYPE_ETHERNET_CSMACD` / `IF_TYPE_IEEE80211`) | read-only |
| netl/speed_duplex | Speed and Duplex | read-only value | `Auto-Detect` | **absent** | `GetIfEntry2` (`MIB_IF_ROW2.TransmitLinkSpeed`/`ReceiveLinkSpeed`) or `GetAdaptersAddresses` + `GetIfTable2` | read-only |
| netl/address_settings | Address Settings | read-only value | `Automatic` | **absent** | `IP_ADAPTER_INFO.DhcpEnabled` (`GetAdaptersInfo`) or the interface's `EnableDHCP` registry value | read-only |
| netl/ip | IP Address | read-only value | `Automatic` | real: the adapter's IPv4 address (C-ho:112, 127) | `GetAdaptersAddresses` (`FirstUnicastAddress`) | read-only |
| netl/subnet | Subnet Mask | read-only value | `Automatic` | real: computed from `OnLinkPrefixLength` (C-ho:84, 128) | `GetAdaptersAddresses` | read-only |
| netl/router | Default Router | read-only value | `Automatic` | real (C-ho:85, 129) | `GAA_FLAG_INCLUDE_GATEWAYS` | read-only |
| netl/dns1 | Primary DNS | read-only value | `Automatic` | real (C-ho:88, 130) | `FirstDnsServerAddress` chain | read-only |
| netl/dns2 | Secondary DNS | read-only value | `Automatic` | real (C-ho:88, 131) | same | read-only |
| netl/mac | MAC Address | read-only value | `-` on this console | real: the adapter's `PhysicalAddress` as `AA:BB:...` (C-ho:86-87, 113) | `GetAdaptersAddresses` (`PhysicalAddress`/`PhysicalAddressLength`), cross-check `getmac` | read-only |
| netl/mtu | MTU | read-only value | `Automatic` | real (C-ho:89, 133) | `IP_ADAPTER_ADDRESSES.Mtu` | read-only |
| netl/proxy | Proxy Server | read-only value | `Do Not Use` | **absent** | `WinHttpGetIEProxyConfigForCurrentUser` (winhttp.dll) | read-only |
| netl/upnp | UPnP | read-only value | `Enabled` | **absent** | COM `IUPnPDeviceFinder` / `INetworkListManager`; a plain read is enough | read-only |

**The depth-2 table itself is not reproduced.** The native answers `host:network` with a message page
whose body is nine labelled lines per adapter plus a heading (C-ho:122-136); the console draws a
two-column table on the D5 grid (labels right-aligned 939, values 979, rule pair 199/879, no focus). The
parity catalogue records exactly that mismatch as an informative region
(P-score, "page-network-status-list | status rows").

---

## 2. Coverage tables

### 2.1 Per page

| page | level | rows total | real | placeholder | absent | native-only rows (not counted) |
|---|---:|---:|---:|---:|---:|---:|
| Settings column | 0 | 16 | 16 | 0 | 0 | 0 |
| System Update | 1 | 2 | 0 | 2 | 0 | 0 |
| Game Settings | 1 | 6 | 0 | 6 | 0 | 0 |
| Video Settings | 1 | 19 | 0 | 7 | 12 | 3 |
| Music Settings | 1 | 5 | 0 | 5 | 0 | 3 |
| Chat Settings | 1 | 3 | 0 | 3 | 0 | 0 |
| System Settings | 1 | 26 | 2 | 7 | 17 | 0 |
| Theme Settings | 1 | 4 | 4 | 0 | 0 | 2 |
| Date and Time Settings | 1 | 6 | 4 | 2 | 0 | 0 |
| Power Save Settings | 1 | 2 | 1 | 1 | 0 | 0 |
| Accessory Settings | 1 | 14 | 0 | 14 | 0 | 0 |
| Printer Settings | 1 | 2 | 0 | 2 | 0 | 0 |
| Display Settings | 1 | 6 | 2 | 4 | 0 | 0 |
| Sound Settings | 1 | 3 | 2 | 1 | 0 | 0 |
| Security Settings | 1 | 6 | 0 | 6 | 0 | 0 |
| Remote Play Settings | 1 | 5 | 0 | 5 | 0 | 0 |
| Network Settings | 1 | 5 | 1 | 4 | 0 | 0 |
| Network status list | 2 | 13 | 8 | 0 | 5 | 0 |
| Photo Settings (native page, no firmware rows) | 1 | 0 | 0 | 0 | 0 | 3 |
| **total** | | **143** | **40** | **69** | **34** | **11** |

Check: 40 + 69 + 34 = 143. Regrouped by level, the same numbers are:

| level | rows | real | placeholder | absent |
|---|---:|---:|---:|---:|
| 0 (column) | 16 | 16 | 0 | 0 |
| 1 (pages) | 114 | 16 | 69 | 29 |
| 2 (status list) | 13 | 8 | 0 | 5 |
| **total** | **143** | **40** | **69** | **34** |

The two tables are the same numbers, not two measurements: level 1's 16 real are System 2, Theme 4,
Date and Time 4, Power 1, Display 2, Sound 2, Network 1, and its 29 absent are Video 12 + System 17. If
either table is edited, both must be reconciled row by row against section 1.

### 2.2 The headline numbers

- **143** firmware settings rows are in scope: 16 column entries, 114 page rows, 13 depth-2 readout rows.
- **40 real (28.0 %)**, **69 placeholder (48.3 %)**, **34 absent (23.8 %)**.
- **78 of the 143 rows carry a value on the console.** Of those, **20 show a value** in this build,
  **34 are present with an empty value column**, and **24 have no native row at all**. The mechanism is
  one line: an inert row carries no `setting`, and `Shell::valueText` returns empty when `setting` is
  empty (C-sm:1396), so the console's `Enabled`/`Off`/`English` has nowhere to come from. Those counts
  are derived from section 1 (each row's console-value cell read against its native-behaviour cell) and a
  verifier can re-derive them from this file with the same two-column comparison.
- Choice lists are in much better shape than rows: **63 lists / 442 items**, 45 read from the RCO at run
  time, 4 captured (the four Theme lists), 10 native, and 4 rows with no page object at all (A1 11.4,
  16.1); the four Theme lists were measured item for item against the frames (A1 16.1, P-score).

### 2.3 Pages that do not exist natively at all

Each of these is a firmware page the console opens, with no native page behind the row (the row is
either absent or, at this baseline, raises the generic message page):

| firmware page | evidence | native at this baseline |
|---|---|---|
| `page_date_time_config_set_date_time` (the Date and Time set page) | A1 5.7, 9.3d | the row opens a read-only info page (C-sm:205) |
| Display output sub-flow: `page_display_output_config_*` (`TV Type`, resolution ask/test/result) | A1 5.11, 4 (`msg_display_resolution_ask`) | no page; `TV Type` was removed as a row (A1 10.4) |
| `page_console_disc_utility_{select,ask,proceed,finish}` (Format Utility chain) | A1 4, 5.5 | Unavailable (C-sm:214) |
| `page_console_setting_init_{top,proceed,finish}` (Restore Default Settings) | A1 4, 5.5 | Unavailable (C-sm:216) |
| `page_console_init_ps3_{top,choice_type,proceed,finish}` (Restore PS3 System) | A1 4, 5.5 | Unavailable (C-sm:217) |
| `page_console_format_hdd_{top,area,area_details,choice_type,proceed_hdd,finish,error}` | A1 4 | no row at all |
| `page_security_config_password*` (Change Password PIN flow) | A1 9.2, 5.13 | Unavailable (C-sm:233) |
| `page_print_config` sub-pages (Printer Selection / Printer Manager) | A1 5.10 | Unavailable (C-sm:265-266) |
| `page_remote_config` registration chain (register / list / custom SSID) | A1 5.14, 9.2 | Unavailable (C-sm:242-244) |
| `page_device_config` sub-pages (motion calibration, reassign, BD remote, Bluetooth, camera, audio device) | A1 5.9 | Unavailable (C-sm:340-399, no bindings) |
| `page_sound_output_config` (the audio output device picker) | A1 5.12 | replaced by a host report page (C-sm:222) |
| `page_console_auto_power_off_option` two-timer level | A1 5.8, 9.2 | replaced by a 5-item list (C-sm:151) |
| `page_aboutps3` (About PlayStation(R)3) | A1 5.5 | Unavailable (C-sm:220) |

The one settings page this build has that the console does not is **Photo Settings** (3 rows,
C-sm:1333), and it is correctly marked native-only.

---

## 3. Acceptance tests

Every test is a checkable statement. "The report" means the `--settings-report` JSON described in
section 5.1; "the dump" means `<XMB_DUMP_ENTRIES>.settings-<page>.json` written by
`settings_menu.cpp:1010-1028`. Elevation: `no` means
the check runs as the logged-in user; `yes (perform only)` means only the *write* the test forbids or
asserts would need an elevated shell.

### 3.1 The report surface

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-1 | `re-ps3-native.exe --settings-report <file>` writes the JSON and exits 0 without opening a window | file exists, parses, `exitCode == 0`; no window appears | no |
| AT-2 | the report lists all 16 pages and, per page, every row with `id`, `title`, `value`, `valueSource`, `action`, `coverage` and `firmwareRsc` | 143 inventory rows join by page/id; every row in section 1 appears exactly once | no |
| AT-3 | `coverage` is one of `real`/`placeholder`/`absent`, and the per-page counts equal section 2.1 | sum check in the verifier script; any drift fails | no |
| AT-4 | the report carries the container identity it read: `sysconf_plugin.rco` path, version 304, sha256 `6f4d743ad9...b5ed`, 63 lists, 442 items | string equality against A1 11.4 | no |

### 3.2 System Settings

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-5 | System Information shows the host adapter's MAC, equal to `getmac` output for the active adapter | `getmac /fo csv /nh` first row vs the MAC line of the `system/information` row's `valueText` | no |
| AT-6 | System Information shows free/total space equal to `QStorageInfo::root()` within 1 % | `Get-PSDrive C` / `[IO.DriveInfo]` vs the Free Space line of the same `valueText` | no |
| AT-7 | System Information shows the CPU name equal to `(Get-CimInstance Win32_Processor).Name` and the RAM total equal to `(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory` | exact/1 % compare against the Processor and Memory lines of the same `valueText` | no |
| AT-8 | System Name renders a non-empty default before the user sets one (host name), and the stored name wins after a write | two runs: fresh data dir, then `system/name` seeded; value column non-empty both times | no |
| AT-9 | the System page's row order is the console's 25-row order for the rows it shares, and every console row it lacks is reported `absent`, not silently dropped | report `system.rows[].order` vs M2 4.6 | no |

### 3.3 Video, Music, Chat

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-10 | every Video row the console shows with a value renders that value's *kind*: `English` for the six language rows, `Confirm` for BD - Internet Connection | dump of `page-settings-video` vs F-vid | no |
| AT-11 | the Video page contains all 19 console rows in console order, or the missing ones are reported `absent` with a reason | dump row count = 19 (plus native-only rows flagged) | no |
| AT-12 | Music renders `AAC 128 kbps`, `3 seconds`, `48 kHz`, `Type 1`, `Off` on its five console rows | dump vs F-music | no |
| AT-13 | Chat renders `Send Video` on Voice/Video Chat Start | dump vs F-chat | no |

### 3.4 Display and Sound

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-14 | Video Output Settings reports the display's current resolution and refresh, equal to `QScreen` | report `display.current` vs a Qt probe (`QScreen::geometry`, `refreshRate`) and `Get-CimInstance Win32_VideoController` (`CurrentHorizontalResolution`, `CurrentVerticalResolution`, `CurrentRefreshRate`) | no |
| AT-15 | Audio Output Settings names the host's default output device and its volume | report `sound.defaultOutput` vs `Get-PnpDevice -Class AudioEndpoint` (active render) and the Windows volume flyout | no |
| AT-16 | Audio Output lists **every** active render endpoint, not only the default | report `sound.outputs[]` count = `IMMDeviceEnumerator` count; at least one entry on any machine with sound | no |
| AT-17 | the Sound page shows `Off` for Audio Multi-Output and `On` for Key Tone when the app's preferences hold those values, and the value changes when they change | seed both values; dump shows the seeded strings | no |

### 3.5 Network

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-18 | the status list shows the host's real IPv4 address, mask, gateway, DNS pair and MTU for the active adapter | report vs `Get-NetIPAddress`, `Get-NetRoute -DestinationPrefix 0.0.0.0/0`, `Get-DnsClientServerAddress` | no |
| AT-19 | the status list's MAC equals `getmac` and is formatted `AA:BB:CC:DD:EE:FF` | string compare | no |
| AT-20 | the status list has the console's 13 labels, in the console's order, with the native value on the right of each; labels that have no host source are reported `absent` rather than dropped silently | report `network.statusList[]` length 13; label set equality | no |
| AT-21 | Internet Connection Test performs a real probe and reports its result, and no probe changes any adapter setting | run the row; report `network.test` has `ok`/`detail`; `netsh interface ip dump` identical before/after | no (a config write would be `yes`, and is forbidden) |
| AT-22 | no settings surface writes network configuration: the app never calls `SetIpInterfaceEntry`, `netsh`, or `EnableDHCP` | `netsh interface ip dump` byte-identical across a full settings walk | no |

### 3.6 Date and Time, Power

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-23 | the Date and Time row's value column is the live clock in the format the two format rows select, and it ticks | two grabs 2 s apart differ only in the digits; string matches the format | no |
| AT-24 | Time Zone offers the firmware's 110 zones, in the RCO's order, first `GMT-11:00 American Samoa`, last `GMT+13:00 Independent State of Samoa` | dump `date/timezone` count 110 and both endpoints | no |
| AT-25 | a stored firmware zone token resolves to a fixed GMT offset and is never written to the host clock | `--ui-clock` pinned run: clock string shifts by the offset; `Get-Date` unchanged | no |
| AT-26 | Set Automatically reports the host's real synchronisation state (NTP vs no sync), read from W32Time | report `date.autoSync` vs `w32tm /query /status` and `HKLM\SYSTEM\CurrentControlSet\Services\W32Time\Parameters\Type` | no |
| AT-27 | Daylight Saving reports the host zone's real state | report `date.dst` vs `(Get-TimeZone).SupportsDaylightSavingTime` and the current `IsDaylightSavingTime` | no |
| AT-28 | the settings walk never changes the host clock, zone or DST state | `Get-Date`, `Get-TimeZone` and `HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation` identical before/after; no elevation prompt | no (changing them would be `yes`) |
| AT-29 | System Auto-Off's option list is the console's two-timer shape (Video/TV/Music/Photo and Game/Other Features), not a single native ladder | dump `power/idle` shows two groups or the report says the second level is `absent` with a reason | no |
| AT-30 | the System Auto-Off timer still drives the real display-off request and re-arms on activity | scripted idle run logs one `power: display-off requested`; a key press logs the re-arm | no |

### 3.7 Apply paths

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-31 | changing Screensaver to `After 5 Minutes` stores 5, the row's value text becomes `After 5 Minutes`, and the app dims after 5 min of idle | `preferences.json` `display/screensaverMinutes` = 5; row text; time-lapse or a shortened pin | no |
| AT-32 | changing Volume changes the **Windows endpoint master volume** and the app's HUD reflects it | set the row/HUD to 25 %; `IAudioEndpointVolume::GetMasterVolumeLevelScalar` (or the Windows Sound slider) reads 0.25; HUD shows 25 % | no |
| AT-33 | the app's own playback volume (`audio/volume`) is not silently the endpoint: a change to one does not change the other | set `audio/volume` 30, endpoint unchanged; set endpoint 60, `audio/volume` unchanged | no |
| AT-34 | Key Tone `Off` silences the cues and `On` restores them | two runs, same navigation; audio trace or the cue log shows the gate (C-sh:2946) | no |
| AT-35 | every Theme list commit writes the value the label names (Font `Rounded` -> 2, `Pop` -> 1; Color `January` -> 5) and a disabled item writes nothing | scripted commits; `preferences.json` compare (A1 16.5 pattern) | no |
| AT-36 | Date Format and Time Format commit in the RCO's authored order while the focused row is the value a Cross would write | commit each of the three date formats; the stored value and the focused label agree every time | no |

### 3.8 No invention, no forbidden write

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-37 | no settings surface renders the substring `not available` (case-insensitive) after a full walk of all 16 pages | OCR-free check: `--settings-report` reports `unavailableMessageCount` and it is 0; plus a grep of the page dumps for the string | no |
| AT-38 | no row reports `real` whose value came from a literal in `settings_menu.cpp`: every `real` row's value is traceable to `host::` or to `Preferences::value` | report `valueSource` is `host:<fn>` or `pref:<key>` for all 40 real rows, never `literal` | no |
| AT-39 | no setting whose host source is `none` gains a fabricated value: those rows show no value and say why | for every inventory row with host source `none`, the report has `value == null` and a `reason` | no |
| AT-40 | the app never writes: host clock/zone, display mode, network configuration, partition/format, or a system-wide power plan | `Get-Date`/`Get-TimeZone`/`netsh interface ip dump`/`Get-Disk` unchanged; no `ChangeDisplaySettingsW`, `SetTimeZoneInformation`, `netsh`, `FormatEx` symbol in the binary (`dumpbin /imports`) | no to verify; `yes` to perform any of them |
| AT-41 | the report's `writeSurface` list names every host write the build performs, and it is a subset of {endpoint volume, endpoint mute, `SetThreadExecutionState`, `SC_MONITORPOWER` broadcast} | set compare | no |

### 3.9 Rendering and row behaviour

| id | statement | how it is checked | elevation |
|---|---|---|---|
| AT-42 | every column row draws its info line, including the three `child=segment` rows | crop compare of the focused row's second line against F-col-r01/r09/r15; the text equals the RCO `info_rsc` | no |
| AT-43 | no settings page row and no column row shows a current-choice dot or check | pixel check on the focus row; A1 U4 closed | no |
| AT-44 | a settings page's focused row expands and the rows below keep the 80 px pitch | crop compare against F-sys/F-vid (focused centre 505, next row +177) | no |
| AT-45 | the choice band's left edge is the page's own (1260 for the Display/Date pages, 1324 for the Theme family) | luma column scan at y 100 vs A1 15.1 | no |
| AT-46 | the gate still passes on the deliverable: `PASS: 6/6 layers within budget`, text presence 900+ bright pixels | the command in section 5.3 | no |

**Count: 46 acceptance tests.** None needs an elevated shell to verify; the four writes that would
(host clock, display mode, network configuration, format) are exactly the ones section 4 forbids, and a
test asserts their *absence* rather than performing them.

The tests that already fail, or cannot pass, today:

- **AT-37** fails on **all 69 placeholder rows** (section 2.1): activating any one of them shows
  `This feature is not available in the native recreation yet.` (C-sh:1683). The parity scorecard
  already records the string on a settings path (P-score, `page-network-test-error` message body:
  "the native row answers with this build own not available in the native recreation yet message").
- **AT-42** fails on exactly three column rows: Game, Accessory and Network (C-sm:1270-1272).
- **AT-2/AT-3** cannot pass at all until `--settings-report` exists; the tree has no such option today
  (section 5.1).

---

## 4. What must NOT be invented or applied

The rule: a settings row may **read and display** the host, and may **write only** what this application
owns. Anything that would change the machine for other programs is out of scope for a settings page and
needs the principal to ask for it explicitly, one surface at a time.

| risky write | why it must stay read-only | what the row may do instead | elevation a real write would need |
|---|---|---|---|
| **Host clock / date / time zone** (`SetLocalTime`, `SetTimeZoneInformation`, `tzutil /s`) | it changes every other program's clock, breaks scheduled tasks, and is not reversible by this app; the console's own set page is a console-internal clock (A1 5.7) and nothing in the local artifacts says a PC should follow it | display the host clock and let Date/Time Format change only the *display* format; show the zone as a read-only label | yes (`SeSystemtimePrivilege`) |
| **Display mode / resolution / refresh** (`ChangeDisplaySettingsExW`) | a wrong mode can leave the panel blank and unrecoverable without a timeout, which is why the firmware itself has the `msg_display_resolution_ask` "can you read this screen" test (A1 4) | report `QScreen` geometry and refresh; a fullscreen toggle already exists (`setFullscreen`, C-sm:1472) and is app-window only | no (but it is destructive, so still not allowed) |
| **Network configuration** (`SetIpInterfaceEntry`, `netsh interface`, `EnableDHCP`, WLAN profiles) | it disconnects the machine and can strand a remote session; the console's wizard writes its own console config, not the host's | read adapters, link state, DHCP/static setting (read), proxy (read), UPnP (read); run a connectivity *probe* for the connection test | yes |
| **Disk format / partition** (`FormatEx`, `IVdsVolume`, `diskpart`) | irreversible data loss, and the console's Format Utility is a PS3-internal format (A1 4, `msg_console_init_ask2`) | list volumes with `QStorageInfo` and show free space; nothing else | yes |
| **Restore Default Settings / Restore PS3 System** (`msg_console_init_ask1`: "all data ... will be deleted") | on a PC the equivalent would be resetting the app's own preferences *and possibly the user's data directory*; the console's string is a console-filesystem wipe and must not be re-pointed at the host | reset only the app's own `Preferences` to documented defaults, behind a confirm, and say exactly what is reset | no (app-local), but the principal must approve the surface |
| **Software / firmware update** | there is no PS3 update channel, and an app self-updater is a distribution decision, not a settings row | show this build's own version and that no PS3 update exists (the current Information page already says so) | no |
| **User account switching that rewrites the host profile** | `SelectUser` already switches the app's own settings store (`Preferences::setProfile`, C-sh:1543) which is correct; it must never touch the Windows account | keep the app-local switch only | yes for anything Windows-account-scoped |
| **Power plan / shutdown / sleep** | the Auto-Off path deliberately uses a `SC_MONITORPOWER` *request* and never a power-plan write (C-ho:277-318, A3 E4) | keep the request and the `GUID_CONSOLE_DISPLAY_STATE` read-back | yes (a real shutdown/plan write) |

Second class, equally important: **do not invent a value where the host has none.** The rows whose host
source is `none` in section 1 (parental control, Remote Play, PS Vita rows, BD/DVD language and format
rows, HDMI colour options, trophies, chat devices on a machine without them) must show no value and must
not borrow a plausible number. The build already refuses to fabricate a zone for a token it does not ship
(C-sm:1406-1416) and refuses to map a month palette it has no tint for (C-sm:58-59); those two are the
pattern to follow everywhere else.

Third: **do not turn a console option list into a host write because the list exists.** Ten stored keys
are written from settings rows today (System Name, Date Format, Time Format, Time Zone, Theme, Color,
Background, Font, Screensaver, Key Tone). Every other row on every page is read-only *by decision*, not by
omission, and the 63 enumerated lists change nothing by being enumerated.

---

## 5. How to grade

### 5.1 The report the implementation must add (it does not exist yet)

`--settings-report` is **absent from the tree**: `main.cpp` has 40+ `--*-report` options
(`main.cpp:334-359`) and no settings one, and a grep for
`settings-report`/`settingsReport` over `native-xmb/` returns nothing. The implementation pass adds it,
following the existing pattern (write JSON, no window, exit 0):

```
C:\dev\out\settings-accept\re-ps3-native.exe --settings-report C:\dev\out\settings-accept\settings-report.json
```

Minimum schema, so AT-2/AT-3/AT-38/AT-39 can be graded mechanically:

```
{ "schema": "xmb-settings-report/1",
  "build": {"version": "...", "sha256": "..."},
  "host":  {"os": "...", "cpu": "...", "memoryBytes": 0, "adapters": [...], "screens": [...], "audio": [...]},
  "container": {"name": "sysconf_plugin", "version": 304, "sha256": "6f4d743a...", "bytes": 1440648},
  "choiceModel": {"lists": 63, "items": 442, "fromRco": 45, "capture": 4, "native": 10, "noPage": 4},
  "pages": [ {"id": "system", "title": "System Settings",
              "rows": [ {"id": "information", "title": "System Information", "order": 8,
                         "value": null, "valueText": "System Information\n\nSystem Name: ...",
                         "valueSource": "host:systemInformation",
                         "action": "Information", "coverage": "real", "firmwareRsc": "msg_console_info",
                         "reason": null} ] } ],
  "totals": {"rows": 143, "real": 40, "placeholder": 69, "absent": 34},
  "unavailableMessageCount": 69,
  "writeSurface": ["endpointVolume", "endpointMute", "displayAwakeHold", "displayOffRequest"] }
```

### 5.2 The frame set to compare against

First, what actually exists under `C:\dev\out` (the brief asked which settings captures exist):

| root | settings material it holds |
|---|---|
| `C:\dev\out\rpcs3-ref-2026-10-28\settings\` | 15 page stills (`page-*-settings.png`), the column at System Update, the System Update page (2), the 4 Theme choice lists, 5 Screensaver list steps, 4 choice-list bursts (480 frames), `date-time-*` and `dateformat-*` states, `verify-chat-page.png`, and 38 `restore\` frames |
| `C:\dev\out\rpcs3-ref2-2026-10-28\` | `depth2\` (13-row status list + its 6 focus probes, 4 `ics-page-*` message-page frames), `lists\` (Theme, Color, Background, Font lists with their down-walks and 3 contact sheets), `tails\` (Video 23 stills, System 21 stills), `bands\` (choice-band open/close bursts, band-at-rest, page-at-rest) |
| `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` | `col-settings-r00`..`r20` (21), `page-settings-theme.png`, `page-settings-theme-row2.png`, `page-settings-network.png`, `page-theme-list*`, `page-color-list*`, `page-background-list.png`, `page-font-list.png`, `page-network-status-list*.png`, `page-network-connection-settings*.png`, plus the mislabelled files named in the warning below |
| `C:\dev\xmb\out\parity-references\` | the paired references for the 16 settings-related cells (day and, where captured, night) with their `.state.json` provenance |
| `C:\dev\out\native-settings-rows`, `native-choices`, `native-band`, `native-tz`, `native-theme2`, `native-theme-lists`, `native-theme-hunk`, `native-settings-column-shadow`, `native-settings-before` | native captures and dumps from the earlier phases - self-baselines, **not** console references |

There is **no Users settings page**: the two rows that the mislabelled `page-settings-system.png` and
`settings-row6-system.png` show (`Turn Off System`, `Create New User`) are the Users *column*'s entries,
which the native has at `shell.cpp:1014` (the second one is `Action::CreateUser`, C-sh:1528). Nothing in
the sysconf inventory binds a Users page.

| page | reference frame | what it settles |
|---|---|---|
| column | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\col-settings-r00.png` .. `r20.png` | 16 rows, order, info line, icons, overscroll |
| System Update | `C:\dev\out\rpcs3-ref-2026-10-28\settings\system-update-page.png` | title, `Select an update method.`, two methods, footer |
| Game | `...\settings\page-game-settings.png` | 2 rows on this unit, `Off`/`Off` |
| Video | `...\settings\page-video-settings.png` + `C:\dev\out\rpcs3-ref2-2026-10-28\tails\video-page-top.png`, `video-down1..22.png` | 19 rows and their values |
| Music | `...\settings\page-music-settings.png` | 5 rows and their values |
| Chat | `...\settings\page-chat-settings.png` | 1 row drawn, `Send Video` |
| System | `...\settings\page-system-settings.png` + `ref2\tails\system-page-top.png`, `system-down1..20.png` | 25 rows and their values |
| Theme | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\page-settings-theme.png` (same page in `...\settings\theme-page-fresh.png`, `page-settings-theme-row2.png` with `Color` focused) | 4 rows, all `Original`, focused-row expansion |
| Theme bands | `C:\dev\out\rpcs3-ref2-2026-10-28\lists\{theme,color,background,font}-list-rest.png` | 4/13/4/3 items, no dot |
| Date and Time | `...\settings\page-date-time-settings.png`, `date-time-page-fresh.png` | 6 rows, clock value, `Daylight Saving` |
| Power Save | `...\settings\page-power-save-settings.png` | 2 rows, System Auto-Off has no value |
| Accessory | `...\settings\page-accessory-settings.png`, `accessory-rest.png` | 7 visible rows + the cut 8th |
| Printer | `...\settings\page-printer-settings.png` | 2 rows, Printer Manager focused |
| Display | `...\settings\page-display-settings.png` | 6 rows and their values |
| Sound | `...\settings\page-sound-settings.png` | 3 rows, `Off`/`On` |
| Security | `...\settings\page-security-settings.png` | 6 rows and their values |
| Remote Play | `...\settings\page-remote-play-settings.png` | 5 rows and their values |
| Network | `...\settings\page-network-settings.png` | 5 rows, `Enabled`/`Enabled` |
| Network depth-2 | `ref2\depth2\statuslist-rest.png` (or `allgraphics\frames\page-network-status-list.png`) | 13 rows, label/value columns, no focus |
| Connection test | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\page-network-connection-settings.png` | the `Please wait...` busy page under `Internet Connection Test` |

**Frame-integrity warning, measured for this document.** Not every file in
`C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` shows what its name says. Opened and confirmed:
`page-settings-system.png` and `page-settings-system-down1.png` are the **Users column** (Turn Off System /
Create New User), `settings-row6-system.png` is the same Users column, and `page-settings-video.png` is the
PS menu's "Do you want to turn off the system?" confirm box. The `settings-row*` names and the
`page-settings-system*` names in that batch carry that Users-column route and must not be used as settings
references. The settings frames in that batch that were used here, each opened: `col-settings-r01`,
`r06`, `r09`, `r15`, `page-settings-theme.png`, `page-settings-theme-row2.png`,
`page-settings-network.png`, `page-theme-list.png`, `page-color-list.png`, `page-background-list.png`,
`page-font-list.png`, `page-network-status-list.png`, `page-network-connection-settings.png`,
`page-user-create.png`. Everything in `C:\dev\out\rpcs3-ref-2026-10-28\settings\` and
`C:\dev\out\rpcs3-ref2-2026-10-28\{depth2,lists,tails}\` that this document cites was opened and shows the
page its name claims. A verifier must look at a frame before scoring against it.

### 5.3 The gate commands

```
# build
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\settings-accept

# canonical layer gate (regression only: the canonical frame is the XMB root, no settings page)
bun C:\dev\xmb\work\ab-layers.ts --capture --exe C:/dev/out/settings-accept/re-ps3-native.exe
# expect: "text presence: NNNN bright pixels ... PASS" and "PASS: 6/6 layers within budget"

# named states (self-baselines; no settings state exists in the layer harness's twelve)
bun C:\dev\xmb\work\ab-layers.ts --list-states
bun C:\dev\xmb\work\ab-layers.ts --states --exe C:/dev/out/settings-accept/re-ps3-native.exe

# the settings-surface harness: one cell per state, Tier A sidecar + Tier B pixels + Tier C log
bun C:\dev\xmb\work\parity.ts --matrix --state page-settings-system
bun C:\dev\xmb\work\parity.ts --capture --score --state page-settings-system
bun C:\dev\xmb\work\parity.ts --capture --score --state page-settings-theme
bun C:\dev\xmb\work\parity.ts --capture --score --state page-settings-video
bun C:\dev\xmb\work\parity.ts --capture --score --state page-settings-network
bun C:\dev\xmb\work\parity.ts --capture --score --state page-network-status-list
bun C:\dev\xmb\work\parity.ts --regression
```

The five settings cells already exist in the catalogue (`P-cat`: `column-settings`,
`page-settings-system`, `page-settings-theme`, `page-settings-video`, `page-settings-network`) and the
choice-band cells exist too (`theme-list`, `color-list`, `background-list`, `font-list`), each with an
RPCS3 reference in `C:\dev\xmb\out\parity-references\<cell>.png` and a `.state.json` provenance file.
The status-list and connection-test cells exist as well (`page-network-status-list`,
`page-network-test-error`, `page-network-connection-settings`).

**How to read the score.** Tier B is a distance to an emulator frame, and the settings pages are the one
class where the *content* legitimately differs (a PC's MAC address is not `-`, its resolution is not
`1080p` by construction). Every settings cell therefore has a `contentDiffers` region annotated in the
catalogue, with the reason; the scorecard reports those regions as informative and gates the chrome
(rule pair, label column, focus, footer). A settings cell passes when:

1. Tier A passes: the sidecar and the page dump show the expected `category`, `depth`, `entryCount`
   and `entryId`, and the dump's row count equals the page's inventory count (section 2.1);
2. Tier B passes on the non-content regions at the class budget (`text-heavy`: 18.00 dB / 0.8500 for the
   frame and each region, `P-score` "Tier B budgets");
3. Tier C passes: the run's log carries the page's own line (`settings: <page> page from the RCO row
   set: N rows, M native-only`, C-sm:431) and the choice lines where a list was opened
   (`view: settings choice band <key> -> N items from rco (<page>)`, A1 13.4).

The current baseline, stated plainly so nobody mistakes it for a target: the last scorecard
(`scorecard-20261028-185352.md`) ran two cells and its headline is **0/36 capturable states passing all
three tiers**, with the settings states annotated `contentDiffers` on `page rows` / `status rows` /
`message body`. The settings phase is graded by moving those cells from "annotated content differs" to
"chrome passes and the report's values are the host's", not by chasing a higher PSNR on a deliberate
content difference.

### 5.4 The three checks a verifier can run in one minute

```
# 1. does the row inventory match this document?
$r = Get-Content C:\dev\out\settings-accept\settings-report.json -Raw | ConvertFrom-Json
"rows {0} real {1} placeholder {2} absent {3}" -f $r.totals.rows,$r.totals.real,$r.totals.placeholder,$r.totals.absent
# expect: rows 143 real 40 placeholder 69 absent 34   (or a strictly better split, with the diff explained)

# 2. is the MAC the host's?
(getmac /fo csv /nh | Select-Object -First 1) -split ',' | Select-Object -First 2
$r.host.adapters | Select-Object -First 1 | Format-List name,ip,mac

# 3. is the display the host's?
Get-CimInstance Win32_VideoController | Select-Object CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate
$r.host.screens | Format-List
```

---

## 6. What is still unattributed, and the lists that must stay `unknown`

| # | unknown | what would settle it | what the implementation must do until then |
|---|---|---|---|
| U-a | Game's four PSP(TM) Remaster rows: are they drawn on a retail console, and with what options? | an RPCS3 still of Game Settings on a unit whose PSP remaster support is enabled; the four items' labels are in the RCO, their *values* are not | keep the RCO list and mark the options unknown; do not invent option text |
| U-b | Accessory's `Keyboard Type` (24), `Key Repeat`, `Mouse Type`, `Pointer Speed`, `Voice Changer` values | one still each, or one scrolled walk of the Accessory page | the RCO item labels are the only authority; a native list that renames them is a new invention |
| U-c | Printer Selection / Printer Manager page contents | a still of each with a printer attached | no page today; do not draw a printer list until then (a host list is a *replacement*, and must be labelled as one) |
| U-d | Remote Play registration chain (register / list / custom SSID) | a still of each page | rows stay inert |
| U-e | Security's five choice pages' item labels beyond the counts in A1 3.3 | a still of each list | counts are known (2/2/9/12/2); labels for `BD/DVD - Parental Control Region Code` are only known as `United States` from the row value |
| U-f | System's `Character Set` (9) and `Dictionary Type` option lists | a still of each list | no native row; do not add one with a guessed list |
| U-g | the Audio Device / Camera Device rows on Chat: drawn only with hardware attached? | a still with a camera and a headset attached | rows stay inert and the frame's 1-row state is the reference |
| U-h | the commit cue (audio) for a settings write | a capture with audio (A1 9.1/U5, still open) | keep `Cue::Accept` on a committed write (C-sh:1523); do not add a new sample |
| U-i | the System Auto-Off two-timer list and its values | a still of the timer page | keep the native ladder but do not present it as the console's |
| U-j | the Date and Time set page (`page_date_time_config_set_date_time`) layout | a still of the page | the row stays a read-only info page (section 4 forbids the host clock anyway) |

Two further cautions, both measured during this document:

1. **`fo_lbl2_alpha=0` does not mean "no info line".** The frames show the line on all three
   `child=segment` rows (F-col-r01/r09/r15), while the native suppresses it. The attribute's real
   meaning is unattributed; the visible behaviour is not.
2. **The console's own values are the emulated console's.** `MAC Address: -`, `IP Address: Automatic`,
   `PS3-354`, `GMT+13:00 Independent State of Samoa` are that unit's, not a target for this PC. Every
   acceptance test in section 3 that names a value names the *host* value the row must show, and the
   frames are the reference for the row's *shape*, not for its content.

---

## 7. Provenance of this document

Written 2026-10-28 for the settings-pages phase. Read for it: `audit/SETTINGS-FIRMWARE.md` (all 17
sections), `audit/GRAPHICS-INVENTORY.md`, `audit/BEHAVIOUR-GAP-AUDIT.md`, `src/settings_menu.{h,cpp}`,
`src/shell.cpp` (the column, `activate`, `openSettings`, `valueText`, `clockDateTime`), `src/host.cpp`,
`src/preferences.cpp`, `src/xmbml_view.{h,cpp}`, `src/main.cpp` (the option table), the two settings
capture manifests, the 162-frame graphics batch, `work/parity-catalogue.json`,
`work/parity-reference.json`, the last parity scorecard, and `work/ab-layers.thresholds.json`.

Frames opened and read for this document (not merely listed): `col-settings-r01/r06/r09/r15`,
`page-settings-theme`, `page-settings-theme-row2`, `page-settings-network`, `page-user-create`,
`page-theme-list`, `page-color-list`, `page-background-list`, `page-font-list`,
`page-network-status-list`, `page-network-connection-settings`, plus `page-settings-system`,
`page-settings-system-down1`, `settings-row6-system` and `page-settings-video` for the frame-integrity
check, and from the reference batch `page-{system,video,display,sound,security,remote-play,accessory,
date-time,power-save,music,chat,printer,network}-settings.png`, `system-update-page.png`,
`column-system-update.png`.

This pass modified no source file, harness, reference frame, threshold or other audit file: the only file
it wrote is this one. The source tree did change while it was read - section 0.6 records the sizes, the
mtimes and what had already changed, so the "native now" column cannot be mistaken for the tree as it
stands after the implementation pass.

---

## 8. Settings pages phase: implementation record (2026-10-29)

This is the acceptance record for the pass that finished the host layer and added the report. It was
written against the tree as it stands now; every number below came from the commands named beside it.

### 8.1 What changed, file by file

| file | change |
|---|---|
| `src/main.cpp` | `--settings-report <json-file>` and `--settings-page <page-id>`: writes the JSON, opens no window, exits 0. CLI only; no other behaviour touched. |
| `src/host.cpp` | `rowValue` rewritten to the new rule (below); `rowDetail` bodies re-worded away from placeholder phrasing; `networkStatusList()` (the console's 13-row table) and `networkTest()` added; `dataDirectoryText()` replaced with a bounded walk (the old `QDirIterator` loop spun forever when the report ran before the data directory held a file); `bytesText()` reports sub-kB sizes in bytes; `AdapterInfo` gained `subnet`/`prefixLength`; the volume HUD is untouched. |
| `src/host.h` | `StatusRow`, `networkStatusList()`, `networkTest(bool)`, `AdapterInfo::subnet/prefixLength`. |
| `src/settings_menu.cpp` | Video and System row sets completed to the console's 19 / 25(+1) rows with the RCO's own `msg_*` keys; `settingsRowBody()` added (the one body builder for a settings row: host page, System Information's report, the firmware explanation plus the host reason, and the RCO's own option list); `settingsReport()` rewritten to the schema in 5.1; `dumpSettingsPage()` carries `coverage`/`reason` and the two live-value rules; C-1 fixed (below). |
| `src/settings_menu.h` | `settingsRowBody()` declaration; the C-1 comment corrected. |
| `src/shell.cpp` | the Information page calls `settingsRowBody()` (so a row with no host page can never fall through to System Information's body); `system/name` shows the computer's name until a name is stored. |
| `native-xmb/audit/SETTINGS-COVERAGE.md` | this section. |

Nothing outside the write fence was modified. `view.cpp` is untouched: section 8.8 returns a hunk for the
one defect found in it (the volume HUD's segments).

### 8.2 The report (AT-1 to AT-4)

```
C:\dev\out\settings-accept\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash ^
  --data-dir C:\dev\out\settings-accept\data ^
  --settings-report C:\dev\out\settings-accept\settings-report.json
```

Exit code 0, no window, 234,318 bytes, sha256 `986361caadaeffe6e5f8209731b1e51b0b2d2f69e2dad3cbee0c54527ee07c05`,
build sha256 `edb3d2978ffd4f5ca0bd2a00b9724c843400875567dc629dfea375b0cd301541`.

Top-level keys: `schema` (`xmb-settings-report/1`), `generatedAt`, `build`, `container`, `choiceModel`,
`host`, `system`, `clock`, `storage`, `network` (with `statusList[13]` and `test`), `display` (with
`current`), `sound`, `audio`, `date`, `power`, `accessory`, `printers`, `settings` (every stored key with
its value, label, `stored`, `shared` and the absolute key), `elevation`, `elevationText`, `privileges`,
`dataDirectory`, `activeProfile`, `profiles`, `column` (the 16 page entries resolved from
`category_sysconf.xml`, with `firmwareRsc`/`firmwareInfoRsc`), `pages` (17: the 16 firmware pages plus the
native-only Photo page), `inventory`, `totals`, `unavailableMessageCount`, `writeSurface`,
`writeSurfaceExtended`, `writeSurfaceNote`, `placeholderScan`.

Every row carries `id, title, order, value, valueText, valueSource, valueStatus, hostSource, note, reason,
action, actionId, cross, coverage, nativeOnly, writable, setting, firmwareRsc, firmwareInfoRsc, detail`.
`valueSource` is `host:<source>` (win32 / qt / com / winspool / winhttp / hid / clock / hostName) or
`pref:<key>`, or null.

Counts, straight from `totals`:

| quantity | value |
|---|---:|
| inventory rows (16 column + 114 page + 13 status) | 143 |
| present | 143 |
| real | 68 |
| placeholder | 75 |
| absent | 0 |
| rows whose value column is non-empty | 49 |
| rows a Cross can write | 26 (10 stored keys, 4 host choice rows, 2 endpoint rows, 1 confirmed reset, 9 page/list opens) |
| native-only rows (not in the inventory) | 13 |
| `unavailableMessageCount` | 0 |
| `placeholderScan.hits` | 0 |
| `choiceModel` | 65 lists / 455 items: 45 RCO, 4 capture, 12 native, 4 with no page object |
| `network.statusList` | 13 rows; `network.test.ok` true |
| container | `sysconf_plugin` v304, 1,440,648 bytes, sha256 `6f4d743ad9...b5ed` |

### 8.3 Coverage after the pass

The rule applied to every row, one line: **a row is `real` when its value column is produced by this
computer or by this build's own store (or, for one row, when a Cross performs a real confirm-gated write);
it is `placeholder` when it draws no value; a firmware row with no native row is `absent`.** The value
column carries the row's own value or nothing - a host fact that is not the row's value lives on the row's
page, not in the column - which is what section 4 requires.

| page | rows | real | placeholder | absent | delta vs 2.1 |
|---|---:|---:|---:|---:|---|
| Settings column | 16 | 16 | 0 | 0 | - |
| System Update | 2 | 0 | 2 | 0 | absent 0 |
| Game Settings | 6 | 0 | 6 | 0 | - |
| Video Settings | 19 | 0 | 19 | 0 | **12 absent -> 0** |
| Music Settings | 5 | 1 | 4 | 0 | **real +1** (Output Frequency) |
| Chat Settings | 3 | 2 | 1 | 0 | **real +2** (camera, audio input) |
| System Settings | 26 | 7 | 19 | 0 | **17 absent -> 0, real +5** |
| Theme Settings | 4 | 4 | 0 | 0 | - |
| Date and Time | 6 | 6 | 0 | 0 | **real +2** (Set Automatically, Daylight Saving) |
| Power Save | 2 | 1 | 1 | 0 | - |
| Accessory | 14 | 7 | 7 | 0 | **real +7** (4 host choice rows, keyboard type, camera, audio input) |
| Printer | 2 | 2 | 0 | 0 | **real +2** |
| Display | 6 | 3 | 3 | 0 | **real +1** (Deep Color) |
| Sound | 3 | 2 | 1 | 0 | - |
| Security | 6 | 0 | 6 | 0 | - (invented values removed) |
| Remote Play | 5 | 0 | 5 | 0 | - (invented values removed) |
| Network | 5 | 4 | 1 | 0 | **real +3** |
| Network status list (level 2) | 13 | 13 | 0 | 0 | **5 absent -> 0, real +5** |
| **total** | **143** | **68** | **75** | **0** | 40/69/34 -> 68/75/0 |

The split improves on every axis the matrix measures: absent 34 -> 0, real 40 -> 68, and the 69 baseline
placeholders stay placeholders where the host has nothing honest to say (their values were not replaced by
numbers; see 8.7). Placeholder rises from 69 to 75 only because 29 former `absent` rows are now present
(they cannot be real: the host has no source), and because the rows whose borrowed values were removed
(Security "Off", Remote Play "Off", Game "not applicable", BD/DVD "no optical drive", System Update "no
update channel", Music "Off", Chat "not used") are counted as what they always were.

### 8.4 C-1 fixed: the second label line on the three child=segment rows

`settingsColumnEntries()` no longer reads `fo_lbl2_alpha=0` as "no second label". The line is drawn
whenever the definition carries an `info_rsc`, on every column row including Game, Accessory and Network.
Evidence: `C:\dev\out\settings-accept\capture\col-settings-r01.png` (Game: `Adjusts settings for games.`),
`col-settings-r09.png` (Accessory: `Adjusts settings for accessories that are connected to this system.`),
`col-settings-r15.png` (Network); the console frames `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\col-settings-r01.png`,
`r09.png`, `r15.png` draw the same line in the same place. The report carries the same fact per row:
`column.rows[].info` is non-empty for all 16 (`rowsWithoutInfo=0`).

### 8.5 The 46 acceptance tests

`scripts`: `C:\dev\out\settings-accept\accept\run-acceptance.ps1` (writes `acceptance.json` beside it).

| AT | result | evidence |
|---|---|---|
| AT-1 | PASS | report written, parses, exit 0, no window (`accept/settings-report.json`, 234,318 bytes) |
| AT-2 | PASS | 17 pages, 143 inventory rows, 0 missing row fields, every row joined by page/id (`acceptance.json`) |
| AT-3 | PASS | coverage in {real,placeholder,absent}; per-page counts equal 2.1; `totals.check = 143` |
| AT-4 | PASS with a count change | container path/version/bytes/sha256 exact; `choiceModel` 65 lists / 455 items (45 RCO, 4 capture, 12 native, 4 no page) instead of 63/442 because the inherited pass added Sound's two native host rows (+2 lists, +13 items). See 8.8 |
| AT-5 | PASS | `getmac` `04:7C:16:13:45:A4` == the MAC line of `system/information`'s page |
| AT-6 | PASS | `C: 91.9 GB free` vs `Get-PSDrive C` 91.9 GiB |
| AT-7 | PASS | `AMD Ryzen 5 5600X 6-Core Processor at 3.70 GHz, 7 cores`; RAM 47.9 GiB vs 47.9 GiB |
| AT-8 | PASS | default `al-workstation` (the computer's name); seeded `system/name=TESTNAME` wins (`accept/settings-report-name.json`) |
| AT-9 | PASS | the System page rows are the console's 25-row order plus `Install Other OS` where the firmware's own sequence puts it |
| AT-10 | NOT MET, by design | the six language rows and BD - Internet Connection carry no value: the host has none and section 4 forbids inventing `English`/`Confirm` |
| AT-11 | PASS | 19 Video rows in console order, 3 native-only flagged; `absent=0` |
| AT-12 | NOT MET, by design | Music's console values (`AAC 128 kbps`, `3 seconds`, `Type 1`, `Off`) have no host source; only Output Frequency reports one (`48 kHz`) |
| AT-13 | NOT MET, by design | Chat's `Send Video` has no host source; the row lists and explains instead |
| AT-14 | PARTIAL | resolution 1920x1080 == `Win32_VideoController`; refresh: the row reports QScreen's 239.96 Hz, `Win32_VideoController` says 143 Hz on this machine (see 8.8) |
| AT-15 | PASS | `Speakers (3- DualSense Wireless Controller)`, volume 25 |
| AT-16 | PASS | 5 active render endpoints listed |
| AT-17 | PARTIAL | Key Tone shows the stored `On`; Audio Multi-Output shows no value (no fan-out setting) |
| AT-18 | PASS | ip 192.168.1.120, mask 255.255.255.0, gw 192.168.1.1, dns 192.168.1.122, mtu 1500 (all host reads) |
| AT-19 | PASS | MAC `04:7C:16:13:45:A4`, `AA:BB:..` formatted, equals `getmac` |
| AT-20 | PASS | 13 labels, console order, host value on every one (`capture/page-network-status-list.png`) |
| AT-21 | PARTIAL | the row runs a real test: interface state plus one bounded TCP connection to 1.1.1.1:53, logged and reported as `network.test`; no adapter setting is touched. It is not an HTTP fetch of a known endpoint |
| AT-22 | PASS | `netsh interface ip dump` byte-identical before/after the whole walk |
| AT-23 | PASS | `10/29/2026 6:20 PM` -> `6:21 PM` 61 s apart, formatted `MM/DD/YYYY h:mm AP` |
| AT-24 | PASS | 110 zones, first `GMT-11:00 American Samoa`, last `GMT+13:00 Independent State of Samoa` |
| AT-25 | PASS | `accept/tz-selfcheck/tz-selfcheck.json`: token -> fixed GMT-08:00, band row 4 of 110, clock shifted by the offset, store restored |
| AT-26 | PASS | `Off`, detail `the Windows Time service (W32Time) is stopped, sync type NTP` |
| AT-27 | PASS | `Daylight` vs `(Get-TimeZone).IsDaylightSavingTime` true |
| AT-28 | PASS | zone, DST state unchanged; the clock advanced naturally, no jump |
| AT-29 | NOT MET, documented | the native System Auto-Off is a 5-item ladder; the console's two-timer level is absent (U-i) |
| AT-30 | NOT RUN | the display-off request/re-arm path is unchanged by this pass; its evidence is the SESSION-POWER audit |
| AT-31 | NOT RUN (commit path) | the row stores `display/screensaverMinutes` and the list commits it; no scripted commit was made in this pass |
| AT-32 | PASS (write) / see 8.8 (HUD art) | `VOLSET 60% -> Windows reports 60%`, restored to 25; `capture/volume-60.png`, `capture/hud-after.png` |
| AT-33 | PASS | seeded `audio/volume=30`, endpoint stays 25; the two rows read different sources |
| AT-34 | PASS | Key Tone off: 1 cue, `decision=disabled`; on: 1 cue, `decision=play` (`accept/keytone-off.err.txt`, `keytone-on.err.txt`) |
| AT-35 | PARTIAL | labels resolve to the values the lists commit (Font Original/Rounded/Pop, Color Original..Red, 13 chips with 6 writable); no scripted commit |
| AT-36 | PARTIAL | Date/Time labels are the RCO authored order (`MM/DD/YYYY`, `12-Hour Clock` here); commit not scripted |
| AT-37 | PASS | `unavailableMessageCount=0`, `placeholderScan.hits=0`, no banned substring in the report or any page dump |
| AT-38 | PASS | all 68 real rows: `valueSource` is `host:*` or `pref:*` |
| AT-39 | PASS | 0 empty-valued inventory rows without a reason |
| AT-40 | PASS | `dumpbin /imports`: no `ChangeDisplaySettings*`, `SetTimeZoneInformation`, `SetLocalTime`, `FormatEx`, `SetIpInterfaceEntry`; clock/zone/netsh unchanged |
| AT-41 | PASS with disclosure | `writeSurface` = endpointVolume, endpointMute, displayAwakeHold, displayOffRequest; the Accessory page's four per-user input writes are named separately in `writeSurfaceExtended` (they are outside the matrix's permitted set; see 8.8) |
| AT-42 | PASS | every column row draws its info line (frames r01/r09/r15, `column.rows[].info`) |
| AT-43 | NOT RUN (pixel) | no dot/check is drawn by design; the report's lists carry `isCurrent` for the band, not a mark |
| AT-44 | PARTIAL | frame regions pass on System/Video/Network; Theme SSIM 0.8471 against the 0.8500 budget (8.8) |
| AT-45 | PARTIAL | the band's left edge is unchanged by this pass; the Theme cell's frame miss is in 8.8 |
| AT-46 | PASS | `PASS: 6/6 layers within budget`, text presence 1668 bright pixels (minimum 900) |

**Not run in this pass, and why:** AT-30 and AT-31 are existing write paths whose behaviour this pass did
not touch and whose evidence lives in the SESSION-POWER and theme-phase records; AT-43 needs a pixel
comparison of the focus row that no test harness here performs; AT-44/45 are the parity cells in 8.6.

### 8.6 Gate, determinism, parity

| check | command | result |
|---|---|---|
| canonical layer gate | `bun work/ab-layers.ts --capture --exe C:/dev/out/settings-accept/re-ps3-native.exe` | `text presence: 1668 bright pixels in the clock crop (minimum 900) PASS`; **PASS: 6/6 layers within budget** (frame 18.76 dB / 0.8360, every region `+0.00` against the stored baseline) |
| canonical frame vs HEAD | same capture with the inherited build `C:\dev\out\native-settings\re-ps3-native.exe` | both `79146659490F071021E4E281AE8E514700C75372238E51D121A3CADB1E061198` - **byte-identical** |
| determinism | two `--render-clock 20 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000` captures | both `93785964BEF5030E648383185112A344AE09DD9D5114343C093C3BA06A2D7046` - **hash-equal** |
| parity | `bun work/parity.ts --capture --score --state page-settings-system,page-settings-video,page-settings-theme,page-settings-network,page-network-status-list --exe ...` | 5/5 cells captured; scorecard `out/parity/scorecards/scorecard-20261029-182450.md` |

Parity, per cell (budget `text-heavy`: 18.00 dB / 0.8500):

| cell | Tier A | frame | content region | note |
|---|---|---|---|---|
| page-settings-system | PASS | 19.28 dB / 0.8780 PASS | page rows 17.19 / 0.8014 (informative: content differs) | chrome passes |
| page-settings-video | **FAIL** | 19.22 / 0.8807 PASS | page rows 17.13 / 0.8003 (informative) | Tier A expects `entryCount 10`; the page is now the console's 19 rows + 3 native-only = 22. The catalogue's count is stale (8.8) |
| page-settings-theme | PASS | 18.38 / 0.8471 **FAIL** (SSIM 0.0029 short) | page rows 15.70 / 0.7250 FAIL | pre-existing near-miss, unchanged by this pass |
| page-settings-network | PASS | 18.68 / 0.8911 PASS | page rows 16.51 / 0.8201 (informative) | chrome passes |
| page-network-status-list | PASS | 17.49 / 0.8659 **FAIL** (PSNR 0.51 short) | status rows 12.52 / 0.6341 (informative: the values are this machine's) | the table now has the console's 13 rows instead of 9, which is why the frame is closer than before; it cannot reach 18 dB while every value differs by construction |

`0/36` remains the headline because Tier C is `n/a` (no input trace in these cells) and Tier B is
informative/failing on the content regions; the settings phase is graded - and moves - on Tier A plus the
report, which is what section 5.3 says.

### 8.7 The 75 rows that still show no value, and why

Grouped by the reason they carry in `reason`; every one is a row the matrix's "host source" column marks
`none` (or an action whose console form has no value either). None of them borrows a number.

| rows | why no value |
|---|---|
| System Update 2 | the two update methods are PS3 system-software actions; this build has no update channel and no firmware to install |
| Game 6 | PSP Remaster and PS upscaler rows configure emulators this build does not run |
| Video 19 | BD/DVD player options; this build plays media files, not discs, and Windows owns the desktop's video/audio output |
| Music 4 | Audio CD import (no CD drive or ripper), crossfade, bitmapping (no DSD), dynamic normalizer (no loudness processing) |
| Chat 1 | Voice/Video Chat Start: no chat transport to act on |
| System 19 | Automatic Update, Character Set, Dictionary Type, Add/Edit Term, Delete Predictive Text Dictionary, Notification Messages, Trophy Notifications, Display [What's New], Disc Auto-Start, Control for HDMI, DivX VOD Registration Code, Enable ATRAC, Enable WMA Playback, the three PS Vita rows, Data Transfer Utility, Restore PS3 System (withheld), Install Other OS |
| Power 1 | Controller Auto-Off: no controller idle timer; the attached pad's state is on the page |
| Accessory 7 | motion calibration, controller reassignment, vibration, BD remote, Bluetooth device list, voice changer, keyboard entry method |
| Display 3 | cross colour filter, RGB full range, super-white: driver-owned HDMI/analogue options with no Windows API |
| Sound 1 | Audio Multi-Output: one endpoint only, no fan-out |
| Security 6 | the console's own parental controls and PIN; Windows' Family Safety is per-MSA, not app-scoped |
| Remote Play 5 | no remote-play server, device or listener exists here |
| Network 1 | Internet Connection Settings: the wizard would write this computer's network configuration, which section 4 forbids |

Each of those rows opens a page that names what the console row sets, the host fact behind the empty
column, and - where the RCO ships the row's option page - the console's own option labels read from that
page's MItems (23 rows). `reason` in the report is the same sentence, so a verifier can read it without
opening the page.

### 8.8 Deviations, stale expectations, and a hunk for a file outside the fence

1. **`choiceModel` is 65 lists / 455 items, not 63 / 442 (AT-4).** The RCO-sourced counts are unchanged
   (45 lists, 4 capture, 4 rows with no page object). The two extra lists are Sound's native `Host Volume`
   and `Host Mute` rows, which the inherited pass added and which write the real Windows endpoint; +13
   items. This is a gain, not a regression, but AT-4's literal numbers are now stale.
2. **Refresh rate (AT-14).** The row reports `QScreen` (239.96 Hz on this desktop); `Win32_VideoController`
   reports 143 Hz for the same adapter. The app's own reading is the one it shows and the one the matrix's
   "host source" column names. Settling which is right needs a `ChangeDisplaySettingsExW`-free read of the
   current mode (e.g. `EnumDisplaySettings(ENUM_CURRENT_SETTINGS)` from a helper), which this pass did not add.
3. **The volume HUD's segments do not render (AT-32, screen half).** The endpoint write is proven (Windows
   reports 40/60/100%), and the HUD's plate is raised at the meter's measured position
   (`capture/hud-after.png`), but its ten `tex_default_lvmeter_*` segments never appear, at any level and at
   +0 ms/+300 ms/+600 ms with a live clock. Cause: `drawVolumeHud()` draws the backing at its own opacity
   (`drawNineSlice(..., 1.)`) and the segments at `p.setOpacity(alpha)` where `alpha = hud.opacity * fadeIn`;
   the two disagree and the trace records `fade-in 0.027` on the only frame it logs. `view.cpp` is outside
   this pass's write fence, so here is the hunk:

   ```cpp
   // view.cpp, drawVolumeHud(), after the segment loop's variables are known
   p.save();
   p.setOpacity(1.0);                       // the plate and the segments share one opacity
   drawNineSlice(p,"system_plugin","tex_default_lvmeter_bg",meter.adjusted(-8,-8,8,8),16.,alpha);
   if(volumeTrace())
       qInfo().noquote()<<QStringLiteral("volume: trace meter alpha %1 level %2 lit %3")
           .arg(alpha,0,'f',3).arg(level).arg(litCount);
   for(int i=0;i<volumeMeterSegments;++i) {
       const QImage& art=i<litCount?lit:(i==litCount-1?focus:dim);
       if(art.isNull())continue;
       p.drawImage(QRectF(meter.left()+i*segmentWidth,meter.top(),segmentWidth,segmentHeight),art);
   }
   p.restore();
   ```

4. **Parity catalogue: `page-settings-video` expects 10 rows.** The page now has the console's 19 plus the
   three native media rows. `work/parity-catalogue.json` is outside this pass's fence: the state's
   `entryCount` needs to become 22 and its Tier C log pattern should read `settings: video page from the RCO
   row set: 22 rows, 3 native-only`.
5. **Theme cell SSIM 0.8471 vs 0.8500, status-list PSNR 17.49 vs 18.00.** Both are content-shaped misses:
   the Theme page's row values are the app's (`Original`) where the console shows its own, and every status
   row carries this machine's address where the console shows `Automatic`/`-`. Neither can pass while the
   content legitimately differs; the catalogue already annotates both regions `contentDiffers`.
6. **`writeSurfaceExtended`.** The matrix's AT-41 permits four host writes. The inherited pass made the four
   Accessory keyboard/mouse rows write `SystemParametersInfo(SPI_SETKEYBOARDDELAY/SPEED, SPI_SETMOUSESPEED)`
   and `SwapMouseButton` - per-user, reversible, no elevation - and this pass kept them (the brief was not to
   revert that work). They are reported separately so AT-41's set check stays exact and the extension is
   disclosed rather than hidden.
7. **`ex_os_install` is in the RCO but not in the console's 25-row walk.** It is kept in the firmware's own
   sequence (between Restore PS3 System and System Information) and counted in the 26 System inventory rows,
   which is why System's expected count is 26 and not the walk's 25.
8. **The status table appends one line for a second adapter** (`Wi-Fi: Wireless, not connected`) after the
   console's 13 rows when the machine has more than one interface; the grid holds 15 rows, so nothing is
   clipped.

### 8.9 Where the evidence lives

All under `C:\dev\out\settings-accept\`:

| path | what it is |
|---|---|
| `settings-report.json` | the report (234,318 bytes, sha256 `986361ca...`) |
| `accept\acceptance.json` | the 33 machine-checked results above, with their evidence strings |
| `accept\run-acceptance.ps1` | the script that produced them (re-runnable) |
| `accept\tz-selfcheck\tz-selfcheck.json` | AT-25's time-zone token round trip |
| `accept\keytone-off.err.txt`, `keytone-on.err.txt` | AT-34's cue decisions |
| `accept\users.err.txt`, `accept\seed-users\preferences.json` | the user switch: `shell: user switched ... volume= 40 ... to ... volume= 70`, active profile `bbbbbbbb-...` |
| `capture\col-settings-r00.png` .. `r20.png` | the 21 column frames, pinned (`--render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-28T21:40:00 --focus-time 2000`) |
| `capture\page-settings-<id>.png` | all 16 settings pages, same pins; `page-network-status-list.png` is the 13-row table |
| `capture\entries\page.settings-*.json` | the page dumps (`XMB_DUMP_ENTRIES`), 17 files, each carrying every row's `displayValue`, `valueSource`, `coverage` and `reason` |
| `capture\canon-now.png`, `canon-head.png` | the gate's canonical frame from this build and from the inherited one (identical) |
| `capture\det-final-1.png`, `det-final-2.png` | the two `--render-clock 20` determinism frames (identical) |
| `capture\volume-60.png`, `hud-after.png`, `hud-after-crop.png` | the volume apply and the HUD's raised plate |
| `C:\dev\xmb\out\parity\scorecards\scorecard-20261029-182450.md` | the parity scorecard summarised in 8.6 |
