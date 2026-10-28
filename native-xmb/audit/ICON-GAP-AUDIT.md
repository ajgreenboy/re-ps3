# ICON-GAP-AUDIT: every visible icon and graphic the firmware draws, and what the native draws

Scope: an object-by-object accounting of the XMB's icon and graphic surfaces in 4.93, matched against the
native build's draw sites. Written 2026-10-28. Analysis only: no rendering source was changed, and the only
file this pass wrote is this document.

Evidence classes: **M** = measured from firmware bytes, an exported picture or a console frame; **F** = fit
or inference; **R** = recorded run. Firmware bytes stay local; this document carries names, sizes, offsets,
counts and verdicts only.

## 0. Bucket counts

| Bucket | Meaning | Rows | Firmware image names named |
|---|---|---:|---:|
| **A** | present and traced - the native loads the firmware object and draws it in the measured place | 34 image rows (+2 supporting rows A35/A36: `ICONS.mnu`, RCO strings) | 115 |
| **B** | drawn but not from the firmware object - glyph fallback, hand-drawn shape, approximation, `bestImage` allow-list hit, wrong layer | 16 | 46 |
| **C** | absent - the firmware object exists and is on screen (or is one state away) and nothing in the native draws it | 41 | 208 (plus 234 in the unbuilt symbol-palette keyboard) |
| **Total** | | **91** (+2) | **369** (plus 234) |

Of the 41 C rows, 6 are **measured visible on a console frame** we hold (`Y`), 18 are **reachable by a state
this build implements** (`R`, so their absence is a live hole), and 17 are **firmware-only** (`U` - the object
exists and decodes but no frame in the local corpus shows that surface, so its absence cannot be called a
visible gap yet). That split is the honest measure of bucket C, and it is carried as a column below.

The rig pass's `INVENTORY.md` was **not available**: `C:\dev\out\rpcs3-allgraphics-2026-10-28\` does not
exist on this machine at 2026-10-28 17:05 CDT (checked with `Test-Path` and a directory listing; the parent
`C:\dev\out` holds 278 sibling capture directories but not that one). Every console-frame claim below is
therefore drawn from the earlier captures that do exist locally, named per row.

---

## 1. Method - how the objects were enumerated

Four passes, in this order. Nothing was guessed from memory.

**1. The image tables of all 122 RCOs - M.** `C:\dev\out\tex\all-images.txt` is the dump of every
`vsh/resource/*.rco` image table produced by `work/tex-coverage.ts scan` over
`C:\dev\rpcs3\bin\dev_flash`. It has exactly 122 lines, and the split was re-counted for this pass: 89 carry a
non-empty name list, 33 are empty (`msgdialog_plugin`, `ap_plugin`, `search_service`, ...). It is the
enumeration this audit works from: every `tex_*`, `item_tex_*`, `music_tex_*`,
`trophy_tex_*`, `BUTTON_BASE_*`, `LABEL_*` and `COLOR_PALETTE_*` name below was read out of that file, not
recalled.

**2. The RCO object model - M.** `C:\dev\out\rco-dump\all.txt` (3.2 MB) is the `--rco-tree *` dump of the
same 122 containers: 8070 objects, 165 animations. Each line carries the object type, name, table offset,
`posX/posY/width/height`, and the RESOLVED `image=`, `shadow=`, `image2=` references. This is what attaches a
named image to an object and a geometry:

* `xmb_plugin_normal.rco` - `page_xmb_indicator/indicator/indi_base` `posY=-74` `560x82` `image=tex_indi_bg`;
  five 32x32 children at `posX` -244 (`tex_indi_message`), -274 (`tex_indi_chat`), -342 (`tex_indi_online`),
  -376 (avatar, `image=null`), -410 (`tex_indi_plus`), each with a `Text` child (`clock` at `0,-12`,
  `online_num` at `14,+2`).
* `xmb_plugin_normal.rco` - the ten `XMList` entries (`list_user`..`list_friend`) carry **`image=null`**: the
  root column's category icons are not an RCO image, which is why they come from `qgl/icontex` and
  `ICONS.mnu` instead.
* `xmb_ingame.rco` - the same ten `XMList`s carry `image=tex_user`, `tex_sysconf`, ... `tex_friend`
  (128x128): the in-game menu's category row is a different asset set from the XMB's.
* `explore_plugin_full.rco` - `control_panel_base` (`tex_cp_panel`), `plane_play_status`..`plane_shuffle_status`
  (ten status planes), seven `msg_*_button` with `image=`/`shadow=`/`image2=` triples, `cmdbtn_*` pairs on
  `tex_button`.
* `system_plugin.rco` - `busy_icon`, `busy_icon_anim1`, `busy_short` (8x36), `busy_long` (8x36),
  `busy_icon_shadow`; `page_btnnavi/btnbar/{cross,circle,triangle,rectangle}_shadow/*_icon`.
* `impose_plugin.rco` - `impose_battery_padicon` (`tex_controller`), `numbers_icon` (`tex_controller_number`),
  `impose_pad_battery_notice_padicon` + `_shadow`.

**3. What the native actually asks for - R.** Every `assets_.image(container, name)` call site, every `tex_*`
string literal, and every glyph table was read out of the source. The measured result:

| Measure | Value |
|---|---:|
| `assets_.image(` / `assets.image(` call sites in `src/` | 40 (`view.cpp` 30, `main.cpp` 3, `osk_layout.cpp` 3, `icons.cpp` 2, plus the two in `main.cpp`'s export/dump paths) |
| Distinct firmware image-name literals in `shell.cpp`/`view.cpp`/`dialogs.cpp`/`settings_menu.cpp`/`icons.cpp`/`jellyfin_shell.cpp`/`xmbml_view.*` | 103 |
| minus 2 string prefixes used for name construction (`tex_notification_`, `tex_notification_trophy_`) | 101 |
| minus 1 name that exists in **no** container (`tex_controller`, see C1) | 100 |
| plus the 52 `oskfullkeypanel_plugin` textures in `osk_layout.cpp` | 152 |
| plus the `qgl/icontex` DDS names (`2n`..`8n`, `35n`, `1n`-adjacent, `dif`), `qgl/icons:.../texenv.dds`, `qgl/lines:textures/month_bg/*` | +12 |
| **Total firmware image names the native requests** | **164** |

**4. What the console frames show - M.** Every "seen?" verdict below cites one of the local captures:
`out/rpcs3-day-2026-10-28/frames/` (9 columns + settings pages + lists, day preset),
`out/rpcs3-ref-2026-10-28/dialogs/` (root, options, information page),
`out/rpcs3-ref2-2026-10-28/bands/` (depth-1 settings pages),
`out/rpcs3-ps/frames/` (PS tap + PS menu), `out/rpcs3-gaps-2026-10-28/burst/` (tap burst).
The native side is compared against `out/native-depth2/state-root-now.png` (2026-10-28 16:20),
`out/native-settings-rows/after-settings-column.png`, `out/native-depth2/iconrow-before-above-after.png`.

**One structural fact that bounds every "A" below.** The RCO object model is read on the render path by
**exactly one** surface: `src/osk_layout.cpp` (`objects.container("oskfullkeypanel_plugin")`, 20+ object
lookups, `imageRef()`). Outside it, `rco_objects.h` is included by `hdr.cpp` (materials) and `main.cpp`
(the dump CLI) only. So for every other surface the **asset** is the firmware's and the **place** is a
constant measured from console frames - not the RCO's `posX/posY/width/height`, which are zero on the pages
that matter (`control_panel_page`, `play_progress_page`, `xmb_indicator`'s parent). Each A row says
"measured rect" when that is what it is.

---

## 2. The decode/coverage facts this audit verified, and the ones it did not

| Claim | Status after this pass |
|---|---|
| 3174 / 3174 named images decode, 0 failures | **Read, not re-run.** This is `TEXTURE-COVERAGE.md` 1.1, from the 89-run `work/tex-coverage.ts sweep`. This pass verified the *inventory* it rests on (122 container lines in `all-images.txt`, 89 non-empty) and spot-checked exported PNGs for 12 of the names that matter most here (`xmb_plugin_normal:tex_psn`, `tex_indi_bg`, `explore_category_sysconf:tex_game`, `explore_plugin_full:user_tex_load`, `system_plugin:tex_optionmenu_bg`, `qgl/icontex:1n`). It did not re-run 89 app launches. |
| The decoded set is the set the XMB draws | **Partly.** 164 firmware names are requested by the native (section 1.3). 6 of the SWEPT names are named here for the first time as gaps (C1 `tex_controller`, C8 `tex_rectangle`, C9 `tex_triangle`, C11 `tex_busy*`, C14 `tex_default_scroll_*`, C10 the p3t theme icon) - i.e. the sweep's coverage number was never the binding constraint; **the consumed set is**. `TEXTURE-COVERAGE.md` 2.3 already said this; this audit extends the list from 7 families to 12. |
| `bestImage` no longer swaps artwork | **Verified in source.** `sameArtNames()` (`assets.cpp:196-204`) is exactly `{tex_psn, tex_video, tex_photo}`, guarded by a pixel gate. The `tex_music` -> `explore_category_sysconf:tex_music` swap that section 2.1 reported is gone. Two allow-listed swaps remain and are bucket B rows (B13, B14). |
| Theme pictures decode through the shared GIM reader | Read (`TEXTURE-COVERAGE.md` 7.2), not re-run. Theme *icons* reach the screen at `view.cpp:1857`; theme *list thumbnails* do not (C10). |
| `msgdialog_plugin` has no images | Verified: `all-images.txt` line `msgdialog_plugin: ` is empty, and the RCO tree has 2 objects. |
| The RCO geometry is available for the XMB surfaces | **True but unused outside the OSK** (section 1, last paragraph). Every non-OSK A row's geometry is a frame measurement. |

---

## 3. Bucket A - present and traced (34 rows)

Frame geometry: `category row` = y 280, quad 117.13 unfocused / 167.33 focused; `item row` = x 565, focus y 505,
pitch 80 (first row below focus +175), quad 77.616 / 184.8; `settings bullet` = 18 px / 39 px focused.

| # | Firmware object (RCO + name) | Surface and frame geometry | Native status | Code reference |
|---|---|---|---|---|
| A1 | `qgl/icontex:2n` | Settings category icon, category row x 275 y 280, 117.13/167.33 | firmware normal map, opaque material reconstructed from the normal | `shell.cpp:1015`; `view.cpp:1080-1120` |
| A2 | `qgl/icontex:3n` | Photo, next slot | as A1 | `shell.cpp:1024` |
| A3 | `qgl/icontex:4n` | Music | as A1 | `shell.cpp:1025` |
| A4 | `qgl/icontex:5n` | Video | as A1 | `shell.cpp:1026` |
| A5 | `qgl/icontex:6n` | Game | as A1 | `shell.cpp:1038` |
| A6 | `qgl/icontex:7n` | Network | as A1 | `shell.cpp:1043` |
| A7 | `qgl/icontex:8n` | Friends | as A1 | `shell.cpp:1050` |
| A8 | `xmb_plugin_normal:tex_psn` 128x128 | PlayStation Network category icon | factory artwork, own colours kept (not tinted) | `shell.cpp:1049`; `view.cpp:1103` |
| A9 | `qgl/icontex:dif` 64x64, `qgl/icons:widgets/other/texenv.dds` | diffuse + environment maps of the icon material, every glass icon | read as material inputs | `icons.cpp:165-166` |
| A10 | `qgl/icontex:{9,14,10,12,13,24,15,57,16,17,18,19,21,20}n` | the 14 Settings column rows (System Update, Game/Video/Music/System/Theme/Date/Power/Accessory/Display/Sound/Security/Remote/Network Settings), item row, quadrant + wrench-disc as baked into the normal map | firmware normal maps through the icon program. The RCO art `explore_category_sysconf:tex_*` is the same disc+glyph as flat 128 px masks and is **not** what is drawn (see note N1) | `shell.cpp:1079-1084`; `view.cpp:1860-1863` |
| A11 | `explore_category_sysconf:tex_chat`, `tex_printer` | Chat Settings, Printer Settings rows (the two not in the normal-map map) | firmware 128 px masks via `drawItemGlyph` | `shell.cpp:1018,1021` |
| A12 | `explore_plugin_full:item_tex_vitaformat`, `item_tex_ps3game_folder`, `item_tex_ps12util`, `item_tex_ps2game_folder`, `item_tex_pspgame_folder`, `item_tex_ps3sd_folder`, `item_tex_ps_store` | the Game column's 7 rows | firmware artwork; glass masks through `itemGlyph`, the PS Store bag opaque | `shell.cpp:1039-1042`; `view.cpp:923-1000,1864` |
| A13 | `explore_category_network:tex_onlinemanual`, `tex_premo`, `tex_browser`, `tex_kensaku`, `tex_dl_manage` | the Network column's 5 rows | firmware artwork | `shell.cpp:1044-1048` |
| A14 | `explore_category_friend:item_tex_NewFriend`, `item_tex_Players`, `item_tex_ChatRoomText`, `item_tex_NewAvc`, `item_tex_Messages`, `item_tex_IgnoreList` | the Friends column's 6 rows | firmware artwork | `shell.cpp:1053-1058` |
| A15 | `explore_plugin_full:user_tex_login` | the Users column's "User" row | firmware artwork | `shell.cpp:1014` |
| A16 | `explore_plugin_full:photo_tex_album_pic`, `music_tex_album_pic`, `video_tex_album_default`, `item_tex_plain_folder` | Photo/Music/Video library rows and every local folder row | firmware artwork | `shell.cpp:1024-1036`; `view.cpp:373-378` (folder remap) |
| A17 | `explore_plugin_full:item_tex_trophy`, `tex_Sign_In`, `item_tex_ps_store` | PSN column rows | firmware artwork | `shell.cpp:1049` |
| A18 | `explore_plugin_full:tex_cp_panel` 72x72, `tex_cp_focus` | music control panel plate (nine-slice inset 20, scaled 1.3) and the shared focus under one of 7 buttons | firmware, nine-sliced | `view.cpp:2519,2565-2568` |
| A19 | `explore_plugin_full:tex_cp_prev`, `tex_cp_next`, `tex_cp_pause`, `tex_cp_stop`, `tex_cp_repeat`, `tex_cp_shuffle`, `tex_cp_volume` | the 7 buttons, 90x60 cells | firmware | `view.cpp:2480-2481,2569` |
| A20 | `explore_plugin_full:tex_play_status`, `tex_pause_status`, `tex_repeat_status`, `tex_1repeat_status`, `tex_shuffle_status` | the panel's status planes, 74x74 (98x74 for repeat/shuffle); only the states the shell has | firmware; the 5 transports with no state stay undrawn (C12) | `view.cpp:2527-2546` |
| A21 | `videoplayer_plugin:tex_cp_help_circle`, `_cross`, `_l1`, `_r1`, `_l2_left`, `_r2_right` | the 6-button help strip, 86x30 icons on a 283 px pitch | firmware | `view.cpp:2442-2468` |
| A22 | `videoplayer_plugin:tex_progress_bar`, `tex_progress_bar_in`, `tex_progress_bar_jump_indi` | player progress bar and jump marker | firmware | `view.cpp:2356-2400` |
| A23 | `system_plugin:tex_default_lvmeter_on`, `_off`, `_focus`, `_bg` | volume meter segments and backing | firmware | `view.cpp:2278-2281,2340` |
| A24 | `system_plugin:tex_default_progress_base`, `tex_default_progress_slider` | `progress_bar_playing_time` (200x6) inside the music panel | firmware defaults (the `Progress` object carries no image) | `view.cpp:2541-2561` |
| A25 | `xmb_plugin_normal:tex_indi_bg` 24x24 | information-panel plate, nine-slice inset 8 into the measured 591x58 rect at (1329,75), flush right; no right edge | firmware tile, nine-sliced at native pixel size | `view.cpp:1992-1994` |
| A26 | `xmb_plugin_normal:tex_indi_online` 32x32 | online-friends slot at (1418,88) with its `online_num` counter at +14/+2 | firmware, drawn only when the count > 0 | `view.cpp:2033-2037` |
| A27 | `system_plugin:tex_notification` 64x64 | notification plaque, nine-slice inset 16, 400 x (58 or 84) at (1496,145) | firmware tile; **the rect is a fit**, not traced | `view.cpp:1043-1047` |
| A28 | `system_plugin:tex_notification_{info,caution,settings,friend,music,mediasever,headset,keypad,psbutton_insensitive,trophy_{platinum,gold,silver,bronze}}` 100x100 | notification glyph, plaque height - 12, centre 13 px in | firmware, all 13 in a real allow-list | `shell.cpp:1318-1321,1324-1327`; `view.cpp:1051` |
| A29 | `system_plugin:tex_cross`, `tex_circle` 30x30 | every dialog / page / PS-menu footer hint glyph, fitted to ~22 px ink | firmware | `dialogs.cpp:350-356,474`; `view.cpp:2136-2138` |
| A30 | `qgl/lines:textures/month_bg/rgb/NN.dds` and `.../night/NN.dds` | the month background behind every frame | firmware (DDS through the qgl path) | `view.cpp:132,385,546`; `main.cpp:616` |
| A31 | `oskfullkeypanel_plugin:SBTN_FOCUS` 84x66, `LBTN_FOCUS` 166x66 | OSK focus ring under the focused key | firmware, centred on the key | `view.cpp:3347-3350` |
| A32 | `oskfullkeypanel_plugin:SCROLL_BASE_SYMBOL`, `SCROLL_KNOB_SYMBOL` | OSK scroll track and knob | firmware | `view.cpp:3453-3458` |
| A33 | `oskfullkeypanel_plugin:LABEL_L1,L2,R1,R2,SELECT,START,SQUARE,TRIANGLE,L1..` 74x28 | OSK function-row badges, 20 px pill | firmware | `view.cpp:3315-3316` |
| A34 | `oskfullkeypanel_plugin:KEYTOP_COPY`, `KEYTOP_PASTE` 72x54 masks | prediction buttons: mask tinted to the measured ink, drawn at the card's own size | firmware masks, correctly used as glyphs after the 2026-10-27 fix | `view.cpp` (prediction block); `TEXTURE-COVERAGE.md` 2.2 |

Two supporting rows that are firmware but not images, kept here because they drive every A above:

| # | Object | Native | Code |
|---|---|---|---|
| A35 | `ICONS.mnu` (21 preset dirs in `icons.qrc`) - LIT1/LIT2, ATTN, AMB, REFR, EXPOSE, GLASS, TIME COLOR MIX | one reader, four presets, night->day->keyframe blend | `icons.cpp:97-102`; `rco_objects.h` `Materials::icons` |
| A36 | `explore_category_friend` text rows (`msg_friend_register`, `msg_play_with`, `msg_text_chatroom`, `msg_start_new_chat2`, `msg_msg_box`, `msg_ignore_list`, `msg_error_not_*`) | the Friends column's titles and empty states are the RCO's strings | `shell.cpp:1012,1053-1058` |

---

## 4. Bucket B - drawn but not from the firmware object (16 rows)

Verdict column: **substitute** (a different firmware object is drawn), **hand-drawn** (no firmware object is
read at all), **approximation** (a shape/gradient stands in for a texture that exists), **allow-list**
(`bestImage` deliberately serves another container's copy), **measured-override** (the native chose to depart
from the firmware object and recorded a frame measurement as the reason).

| # | Firmware object (RCO + name) | Surface and frame geometry | What the native draws instead | Verdict | Code reference |
|---|---|---|---|---|---|
| B1 | `qgl/icontex:1n` (the house-with-a-face Users icon, 128x128, decodes) | Users category icon, category row, 117.13/167.33 | `explore_plugin_full:user_tex_load` 90x90 - the firmware's own *loading* glyph, near-white on white; the column renders a faint blob where the console shows the house | substitute (wrong asset; the right one exists and is not requested anywhere) | `shell.cpp:1014`; `view.cpp:1080` |
| B2 | `qgl/icontex:35n` (screen-with-antenna, 128x128) | "TV/Video Services" category icon, a **10th** column between Video and Game | the same firmware normal map, correctly | wrong column: no console frame among the 9-column set (`c-settings.png`, `c-game-root.png`, `c-friends.png`) shows a TV column; this build adds one and its synthetic "My Channels" row | `shell.cpp:1037` |
| B3 | `system_plugin:tex_msgdialog_bg` 12x12, flat RGB 54,56,54 @ alpha 116 (and `tex_msgdialog_bg2`, `tex_msgdialog_bg_deep`) | message box body, measured 319x176 at (480,452) | flat white at 43/255 over the box rect - a 0.17 wash | measured-override: `DIALOGS-FIRMWARE.md` 2.1 says the firmware pixel-paints this tile; the captured 2026-10-28 frame reads a lightening `(255-bg)*0.17` where a normal blend of the dark tile would darken it. One of the two is wrong and the frame is the newer evidence | `dialogs.cpp:93-100,281` |
| B4 | `system_plugin:tex_msgdialog_bar` 960x1, centre-bright alpha ramp | the two rules across a status page (y ~199 and the footer) and the box footer rule at y 879 | flat full-width 1 px white alpha rects (177/255 at the box footer) | measured-override: the captured rule is uniform from x 20 to x 1900, which a 960 px centre-bright texture cannot be | `dialogs.cpp:187-192,304-305` |
| B5 | `system_plugin:tex_optionmenu_bg` 536x216 (white, bright at the top, fading down) | triangle options band, x 1320 -> 1742 fade, 38 px pitch, row centre 552 | a 4-stop hand-fit horizontal gradient `QLinearGradient(x0,0,fadeEnd,0)` | approximation: the firmware texture decodes, is 12.7 KB, and has **0 references** in `src/` | `view.cpp:1178-1250,1435-1448` |
| B6 | `system_plugin:tex_optionmenu_line`, `tex_optionmenu_base`, `tex_optionmenu_game` | the options band's left edge / base plate / in-game variant | nothing; the left step is a second gradient; a submenu chevron is a hand-built 11x10 `QPolygonF` at (1786, y+/-5) | hand-drawn | `view.cpp:1446-1447,1517-1522` |
| B7 | `impose_plugin:tex_controller_number`, `impose_plugin:tex_red` | PS menu controller row: four 17x17 slots at x 132 + 44.33*i, y 828, numbers above at y 801 | four hand-filled `QRectF`s (slot 1 at RGB 160,13,13) and four font digits | hand-drawn | `view.cpp:2123-2128` |
| B8 | `explore_category_friend:tex_indi_AFK`, `tex_indi_Game`, `tex_indi_Me`, `tex_indi_Message`, `tex_indi_NewRoom`, `tex_indi_None`, `tex_indi_Sign_in`, `tex_indi_Sign_out` | friend-row presence mark, lower-right of the row icon | a 5-7 px hand-drawn `drawEllipse` tinted grey / blue / green by a 3-value persona state | hand-drawn. **Not visible in any local console frame** - `c-friends.png` is the signed-out empty Friends column, so the firmware's own mark has never been measured here | `view.cpp:1865-1868` |
| B9 | `oskfullkeypanel_plugin:BUTTON_BASE_WHITE`, `BUTTON_BASE_BLUE_GRAY`, `BUTTON_BASE_B`, `_G`, `_O`, `_P`, `_E`, `_SHORT`, `BUTTON_FORCUS` | every OSK key face, 66x50 | a 4-stop procedural gradient per role (letters/olive/blue/mint/arrow), measured from `s1/f0060-0257.png` | measured-override: `view.cpp:3057-3059` argues `BUTTON_BASE_WHITE` reads 165/219/239 where the captured face reads 169/196/190/152 and the per-key row assignment is runtime. Nine decodable face textures are unused | `view.cpp:3054-3067` |
| B10 | `oskfullkeypanel_plugin:LABEL_UP`, `LABEL_DOWN`, `LABEL_LEFT`, `LABEL_RIGHT` 72x54 masks | the four arrow keys' faces | a font triangle caption over the procedural face; `View::drawOsk` still never reads `key.faceTexture` | hand-drawn (unchanged since `TEXTURE-COVERAGE.md` 2.2) | `view.cpp:2358-2382` region; `osk_layout.cpp` `faceTexture` |
| B11 | `oskfullkeypanel_plugin:EN_LABEL_DELETE/SPACE/RETURN/MODE/CLOSE` 140x44, `MODE_GUIDE_ALPHABET`/`_CAP` 29x30, `LIST_WINDOW_FOCUS` 42x42 | backspace / space / line-break / mode / enter key legends, mode cell, list focus | RCO text captions and font text; no code path reads these 9 masks | hand-drawn | `osk_layout.cpp:364-375`; `view.cpp:2425` |
| B12 | `system_plugin:tex_default_text_field`, `tex_default_text_field_focus` | the OSK's text entry field | drawn procedurally from the measured field geometry | approximation | `view.cpp` OSK field block |
| B13 | `explore_plugin_full:tex_video` 46x46 | the Video/TV rows' icon | `xmb_ingame:tex_video` 128x128 (same filmstrip, upscaled) via the `bestImage` allow-list | allow-list | `assets.cpp:196-204,533` |
| B14 | `explore_plugin_full:tex_photo` 46x46 | the Photo row's icon | `xmb_ingame:tex_photo` 128x128 (same camera) via the allow-list | allow-list | `assets.cpp:196-204,533` |
| B15 | none - the info-bar clock is a `Text` object only (`xmb_plugin_normal` `clock`), and `vsh.elf` draws the face | analog clock face, centre (1791,104) r 14, and the page clock at its own centre | hand-drawn circle + 2 hands with a 1.5 px drop shadow | hand-drawn, and unavoidable: no firmware image object for the face exists in any of the 122 containers | `view.cpp:2005-2012`; `dialogs.cpp:196-208` |
| B16 | none - no `mbox_*` object is declared by any RCO (`DIALOGS-FIRMWARE.md` 1) | the in-player panel's "current item" dot | a 4.5 px hand-drawn white disc at (x0+18, y) | hand-drawn | `view.cpp:1491-1493` |

---

## 5. Bucket C - absent (41 rows)

"Seen" column: **Y** = the object is identifiable on a console frame in the local corpus; **R** = reachable by
a state this build implements but no frame shows it; **U** = firmware-only, no local frame of that surface.

| # | Firmware object (RCO + name) | Surface and frame geometry | Seen | Native status |
|---|---|---|---|---|
| C1 | `impose_plugin:tex_controller` (the pad glyph; `impose_battery_padicon`, `impose_pad_battery_notice_padicon`) | PS menu controller row at (448,799) 31x31, **and** the PS-tap plate at (1199,88) 31x34 | **Y** - `out/rpcs3-ps/frames/03-hold-08.png` (glyph between "Controller" and "1") and `01-tap-02.png` | the call sites exist but ask the **wrong container**: `system_plugin` has no `tex_controller` (verified against its image table), `drawImage` returns on a null image (`view.cpp:904`), so **nothing is drawn on either surface** |
| C2 | `xmb_plugin_normal:tex_indi_message` 32x32 | info-panel slot at posX -244 from the clock ink edge, (1516,88) | R - no local frame has an unread message | 0 source references |
| C3 | `xmb_plugin_normal:tex_indi_chat` 32x32 | slot at -274, (1486,88) | R - an XMB text-chat session is not reachable in this build | 0 references |
| C4 | `xmb_plugin_normal:tex_indi_plus` 32x32 | slot at -410, (1350,88) | R - needs a PS Plus account | 0 references |
| C5 | `xmb_plugin_normal` `avatar` slot, 32x32 at -376, `image=null` | signed-in user's avatar in the indicator row | R - needs a PSN sign-in | nothing drawn |
| C6 | `system_plugin:tex_battery_indicator` 30x30 | PS-tap plate / information panel charge state | **Y** - `01-tap-02.png`: the tap plate carries a state glyph beside "1" | 0 references |
| C7 | `impose_plugin:tex_battery`, `tex_battery_shadow`, `tex_charge_arrow`, `tex_charge_arrow_shadow`, `tex_error_batsu` | the impose battery-notice page and the charge-level cell of the PS menu | **Y** (the PS menu's "Charge Level" cell renders a dash; the art family is the candidate for the slot) | 0 references; the dash is font text |
| C8 | `system_plugin:tex_rectangle` 30x30 | the Square button glyph: "Group Content [ ]" in the item options menu, and the fourth hint slot of `page_btnnavi/btnbar` | **Y** - `out/rpcs3-ref-2026-10-28/dialogs/options-open-01.png`, a square glyph at x 1348 y 310 | only `tex_cross` and `tex_circle` are requested anywhere |
| C9 | `system_plugin:tex_triangle` 30x30 | the Triangle hint slot of `btnbar` | R - the console's options page in the corpus shows Cross/Circle pairs only | not requested |
| C10 | the selected theme's own icon/preview (`.p3t` `info/icon`, 65 GIM pictures in `01.p3t`, all decode) | Theme Settings list rows: a small thumbnail left of the label, 32x24-ish, on the "Air Paint" row | **Y** - `out/rpcs3-day-2026-10-28/lists/theme-list.png` | the theme *choice list* is a label-only `PanelBand`; theme icons are used for category/row slots (`shell.cpp:1087-1103`) but never for the list row |
| C11 | `system_plugin:tex_busy` 36x36, `tex_busy_s`/`tex_busy_l` 8x36, `tex_busy_shadow` 46x46 | `page_busy` - any blocking operation (loading, deleting, scanning) | R | 0 references |
| C12 | `explore_plugin_full:tex_cp_stop`, `tex_cp_prev`, `tex_cp_next`, `tex_searchb_status`, `tex_searchf_status`, `tex_stop_status`, `tex_prev_status`, `tex_next_status` | the music panel's status row and transport buttons for states the shell has | R - `stop` and `prev/next` are reachable actions | deliberately undrawn ("rather than fabricated", `view.cpp:2523-2525`), so the buttons render as empty 90x60 cells |
| C13 | unidentified left-pointing triangle at (385,455) | every open page, between the parent column and the page; 20x20 | **Y** - `display-page-rest.png` and `theme-list.png` both show it | not drawn. Candidate: `system_plugin:tex_arrow_left` 30x30 (unused, decodes); identity is **F**, not traced |
| C14 | `system_plugin:tex_default_scroll_base`, `_slider`, `_arrow_up`, `_arrow_down` + 4 shadows | scrollbars on any list longer than its window | U - no local frame shows a long enough list with the bar in view | 0 references |
| C15 | `system_plugin:tex_default_check` | check marks in multi-select lists (the copy/delete "apply to all" set) | U | 0 references |
| C16 | `system_plugin:tex_3x3_focus`, `tex_3x3_shadow` | the 3x3 focus plate settings pages use around a value grid | U | 0 references |
| C17 | `system_plugin:tex_line`, `tex_line_shadow` | separator rules inside lists and dialogs | U | flat rects are drawn instead |
| C18 | `system_plugin:tex_common_dialog`, `tex_common_dialog_shadow` | the common dialog plate | U | 0 references |
| C19 | `system_plugin:tex_operation_guide_base`, `_shadow` | the operation guide plate behind button hints | U | 0 references |
| C20 | `system_plugin:tex_pointer_arrow`, `_click`, `_finger`, `_grab`, `_hand`, `_pen` 53x54 | pointer theme glyphs | U | 0 references |
| C21 | `system_plugin:tex_arrow_up`, `_down`, `_left`, `_right` + 4 shadows | navigation arrows in lists and panels | R | 0 references |
| C22 | `system_plugin:tex_default_spin_arrow_up`, `_down` + shadows | spinner/stepper widgets | U | 0 references |
| C23 | `system_plugin:tex_temporary_icon` | the placeholder icon for an item whose art has not loaded | R | 0 references |
| C24 | `system_plugin:tex_msgdialog_bg_deep` | the "deep" message box tint (kind 2 in `TRACED 0x00344094`) | U | 0 references (see B3 for the normal variant) |
| C25 | `explore_plugin_full:tex_indi_AFK`, `_Game`, `_Me`, `_Message`, `_NewRoom`, `_None`, `_Sign_in`, `_Sign_out` (the column-side copies) | in-row presence marks inside an open column | U | only the `explore_category_friend` copies are relevant and they are substituted (B8) |
| C26 | `explore_category_video:tex_3D`, `tex_divx`, `tex_wmv`, `tex_avc`, `tex_mpg1`, `tex_mpg2`, `tex_mpg4`, `tex_mjpg` | codec / 3D badges on video rows | U | 0 references |
| C27 | `explore_category_music:tex_track_0`..`tex_track_9`, `tex_track_base`, and `explore_plugin_full:tex_track_0`..`_9`, `tex_track_base` | track-number badges on CD/track rows | U | 0 references |
| C28 | `explore_category_photo:tex_3D`, `tex_screenshot`, `tex_multiangle`, `tex_download_music/photo/video` | photo-row capability badges | U | 0 references |
| C29 | `explore_category_psn:trophy_tex_grade_{platinum,gold,silver,bronze,hidden}`, `trophy_tex_default`, `trophy_tex_capsule`, `trophy_tex_addon` | trophy grade icons in the Trophy Collection | R - the Trophy row exists in this build | only `item_tex_trophy` is requested |
| C30 | `explore_category_network:tex_dl_dst_game`, `_music`, `_photo`, `_video` | download-destination icons on the Download Management page | R - the downloads page exists | 0 references |
| C31 | `sysconf_plugin:tex_cable_avmulti`, `_component`, `_composite`, `_d`, `_dsub`, `_hdmi`, `_pin_lr`, `_s`, `_scart`, `_spdif` | the AV cable icons on Display Settings | R | 0 references |
| C32 | `sysconf_plugin:tex_logo_{amg,atok,atrac,divx,dnas,dnas_sd,dolby_digital,dolby_truehd,dts,ezitext,flash,java,ps,ps_sd,qgames,rsa}` | supported-format logos | R | 0 references |
| C33 | `sysconf_plugin:tex_bar_{bddvd,console,device,display,game,init,music,network,power,remote,security,sound,theme,time}_icon` + `_shadow_icon` (17) | the settings pages' category strip icons | U | 0 references |
| C34 | `oskpanel_plugin` - 86 masks + 148 images | the alternate symbol-palette keyboard panel | U | keyboard not built; `TEXTURE-COVERAGE.md` 1.3 records the container |
| C35 | `xmb_ingame:tex_user`..`tex_friend` (10 x 128x128) | the in-game XMB's category row (`XMList image=` refs) | U - the in-game menu is a separate surface | used only as a `bestImage` source (B13/B14) |
| C36 | `explore_plugin_full:tex_Sign_Up`, `tex_Signing_In`, `tex_ps_plus_invitation`, `tex_psplus_icon` | PSN column's sign-up / signing-in / Plus states | R | only `tex_Sign_In` is requested |
| C37 | `premo_plugin:tex_circle`, `tex_cross`, `tex_triangle`, `tex_rectangle` + shadows, `tex_premo_bar` | Remote Play's button guide | R - the Remote Play row exists | 0 references |
| C38 | `deviceconf_plugin:tex_battery`, `tex_bt_state_base`, `tex_bt_state_on`, `tex_level_meter_{blue,grey,red,yellow}` | Accessory Settings' battery / Bluetooth state art | R - the Accessory row exists | 0 references |
| C39 | `auth_plugin:tex_bar_security_icon`, `tex_bar_security_shadow_icon` | Security Settings' bar icon | R | 0 references |
| C40 | `netconf_plugin:tex_infobar_icon`, `tex_infobar_initial_setting_icon`, `tex_network_connect_icon`, `tex_arrow_left` | Network Settings' bar and connect-state art | R - `shell.cpp` delegates 5 network rows to `explore_category_network` but not to `netconf_plugin` | 0 references |
| C41 | `explore_category_*:tex_aa_plane` (11 containers) | the antialiasing edge plane each category page declares | U | 0 references |

---

## 6. Top 20 bucket-C items by visual importance

Ranked by how much of the frame each changes, and by whether a user of this build can reach the state today.

| Rank | Firmware object | Surface | Evidence | Why it ranks here |
|---:|---|---|---|---|
| 1 | `impose_plugin:tex_controller` | PS menu "Controller" row + PS-tap plate | **Y** (`03-hold-08.png`, `01-tap-02.png`) | a completely blank slot on a surface the user opens with a held PS button; the code *tries* to draw it and asks the wrong container, so this is a one-line fix with the picture already decoding |
| 2 | `system_plugin:tex_battery_indicator` | charge state beside the pad glyph | **Y** (`01-tap-02.png`) | same plate as #1; the console shows battery, this build shows nothing |
| 3 | `impose_plugin:tex_battery`, `tex_charge_arrow`, `tex_red` | PS menu charge-level cell, controller slot markers | **Y** | the PS menu's lower panel is three hand-drawn rectangles and a dash where the console has art |
| 4 | `system_plugin:tex_rectangle` | the Square glyph in the item options menu | **Y** (`options-open-01.png`) | one of the four button glyphs is missing from the options menu while Cross/Circle are drawn as real textures elsewhere |
| 5 | `xmb_plugin_normal:tex_indi_message` | info-panel slot at -244 | R | the information panel is on every frame; one of its five slots is permanently empty |
| 6 | `xmb_plugin_normal:tex_indi_chat` | info-panel slot at -274 | R | as #5 |
| 7 | `xmb_plugin_normal:tex_indi_plus` | info-panel slot at -410 | R | as #5 |
| 8 | avatar slot (`xmb_plugin_normal` `avatar`, `image=null`) | info-panel slot at -376 | R | the fifth slot; needs a signed-in user this build does not have |
| 9 | theme `info/icon` / `info/preview` | Theme Settings list rows | **Y** (`lists/theme-list.png`) | the theme list is the one settings list whose rows carry a picture on the console, and it is the only picture this build drops |
| 10 | `system_plugin:tex_busy` + `_s`, `_l`, `_shadow` | `page_busy` | R | any long operation on this build shows a static screen where the console spins |
| 11 | `system_plugin:tex_default_scroll_base`, `_slider`, `_arrow_up`, `_arrow_down` | long lists | U | the 16-row Settings column and the 13-row Theme list both have windows |
| 12 | `system_plugin:tex_triangle` | the Triangle hint in `btnbar` | R | the hint row's fourth slot; the options menu is the natural home |
| 13 | the (385,455) left triangle on every open page | page/parent indicator | **Y** | drawn on every open page on the console and on none here; the asset candidate decodes |
| 14 | `explore_plugin_full:tex_cp_stop`, `tex_stop_status`, `tex_prev_status`, `tex_next_status` | music panel transport + status row | R | 5 of 7 buttons currently render as empty cells |
| 15 | `explore_category_psn:trophy_tex_grade_*` (5 + 2 defaults) | Trophy Collection rows | R | the Trophy row exists and shows one generic trophy for every grade |
| 16 | `explore_category_music:tex_track_0`..`_9`, `tex_track_base` | track badges | U | the music column lists tracks |
| 17 | `explore_category_video:tex_3D`, `tex_divx`, `tex_wmv`, `tex_avc`, `tex_mpg*` | video row badges | U | the video column lists files |
| 18 | `sysconf_plugin:tex_bar_*_icon` (17) | settings page category strip | U | every settings page's header |
| 19 | `sysconf_plugin:tex_cable_*` (10) | Display Settings cable icons | R | Display Settings is implemented |
| 20 | `system_plugin:tex_3x3_focus`, `tex_default_check` | grid/checkbox focus | U | every multi-select and grid page |

---

## 7. Evidence conflicts found with the existing audits

These are places where this pass disagrees with, or dates, an existing document. Each is stated with what
would settle it.

| # | Existing statement | What this pass found | Settling evidence |
|---|---|---|---|
| X1 | `TEXTURE-COVERAGE.md` 2.0 and item 1 of section 4: "the information-panel plate still draws the wrong asset ... `src/view.cpp:1467-1470` draws a 12 % strip inside an invented black rounded rect" | **Stale.** `view.cpp:1992-1994` nine-slices `xmb_plugin_normal:tex_indi_bg` at inset 8 into the measured 591x58 rect; `system_plugin:tex_infobar` is now only the `!plate.isNull()` **fallback**, at `view.cpp:2001`. The same is true of the volume HUD (`view.cpp:2293-2296`). The plate work landed after that doc's 22:15 capture | source at the cited lines; no build needed |
| X2 | `TEXTURE-COVERAGE.md` 2.0: "Controller / status indicator row ... 0 source references ... nothing is drawn" | **Partly stale.** `tex_indi_online` is drawn at `view.cpp:2033` with its counter, gated on a non-zero Steam/Steam-friends presence count. Only `message`, `chat`, `plus` and the avatar slot are still at 0 references | source; also the comment block at `view.cpp:2013-2020` |
| X3 | `TEXTURE-COVERAGE.md` item 5 / section 2 table "Settings rows ... **invented**: every row uses the glass bullet instead of the firmware's row icon" | **Stale, and the recommended fix would be a regression.** Since 2026-10-27 22:35 the 14 Settings rows are pointed at `qgl/icontex:{9,14,...}n` (`shell.cpp:1079-1084`), which are the firmware's own wrench-disc normal maps; the console frame `c-settings.png` shows the same disc+glyph, and the native frame `after-settings-column.png` reproduces it. The *bullet* `qgl/icontex:23n` is used only inside settings **pages**, where the console frame (`display-page-rest.png`) does show a uniform wrench-disc bullet on every row - so the "invented bullet" charge is wrong on both counts | the two frames side by side |
| X4 | `DIALOGS-FIRMWARE.md` 2.1: the message-box plate is `tex_msgdialog_bg` (flat RGB 54,56,54 @ 116) and the bars are `tex_msgdialog_bar` | **Contradicted by the later frame.** The 2026-10-28 captured box measures a *lightening* `(255-bg)*0.17` plate and a *uniform* full-width rule from x 20 to x 1900; a normal blend of a dark tile darkens, and a 960 px centre-bright bar cannot be uniform. The native follows the frame (`dialogs.cpp:93-100,187-192`). Either the frame is not the same box kind as the traced one, or the trace's asset assignment is wrong | a second box frame with a different `kind`, or the `vsh.elf` 0x00344094 slot-1 write re-read against the frame |
| X5 | `RCO-OBJECT-MODEL.md` section 8: "every dialog/list/panel geometry ... not wired yet" | Still true **for geometry**, and this pass adds the bound: the object model is consumed on the render path only by `osk_layout.cpp`. So the "A" rows in section 3 are firmware *assets* in frame-measured *places*, not firmware geometry | `grep RcoObjects src/*.cpp` |
| X6 | `ITEM-ICONS.md` comparison table row 21: "item icons in the open column still go through Qt" is marked CONTRADICTED-by-source already | Confirmed and extended: `qgl/icontex` items are queued to `IconRenderer` (`view.cpp:1860-1863`), and every non-icontex item goes through `drawItemGlyph` with the glass-mask polarity of `ITEM-ICONS.md` 3 | source |
| X7 | `TEXTURE-COVERAGE.md` 2.2: `KEYTOP_COPY`/`KEYTOP_PASTE` "drawn as a tinted mask ... two masks reach the screen" | Still accurate; the other 12 OSK masks remain undrawn (B10, B11). No change, recorded so the count is not assumed to have improved | source |
| X8 | `PS-BUTTON.md` / `DIALOGS-FIRMWARE.md` 1 D9: "PS button system menu: no firmware reference exists locally" | The **menu** is measured (`03-hold-08.png`), and its controller panel art **does** have a firmware owner: `impose_plugin:tex_controller` / `tex_controller_number` / `tex_red` / `tex_battery`. The "no firmware reference" line is about the *confirm dialog*, but it has been read as "no art exists" | the impose_plugin image table |

### Note N1 - why the Settings rows are A and not B

`explore_category_sysconf:tex_game` exports as a **white 128x128 mask**: a gamepad plus a small disc with a
wrench. The native draws a gamepad-plus-wrench disc too, but from `qgl/icontex:14n`. Both are firmware
objects for the same row; the native's choice is the normal map through the icon program, which is what
`ITEM-ICONS.md` 1 established the console does for item icons ("item icons, category icons and bullets are all
drawn with `quad.vpo` + `icons_no_ss_no_shad.fpo`"). So the row is A, and the flat `tex_*` masks in
`explore_category_sysconf` are the fallback material, not the drawn one. If a later pass finds the console
drawing the flat mask instead, A10 becomes B with `explore_category_sysconf:tex_*` as the right asset - the
two are visually near-identical, which is why this is recorded as a note rather than a verdict change.

---

## 8. What this pass did not do

* Did not run the 89-container decode sweep again; the 3174/3174 figure is `TEXTURE-COVERAGE.md`'s (section 2).
* Did not build or run the native; every native claim is a source read at the line cited, cross-checked
  against the 2026-10-27/28 native frames named in section 1.
* Did not measure per-row pixels for B8 (friend presence) or C14 (scrollbars) - no local frame shows either.
* Did not open `out/rpcs3-gaps-2026-10-28/`'s tap burst frame by frame; the PS-tap findings come from two
  still frames.
* Did not touch any source, harness, tooling or reference file. The only write is this document.
