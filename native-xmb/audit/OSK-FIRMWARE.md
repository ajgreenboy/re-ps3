# The real on-screen keyboard (PS3 4.93): source, object tree, layout, behaviour

Analysis only. No file under `native-xmb/src/`, no CMake, no harness and no threshold was
modified for this document. Written 2026-10-27.

This document recovers the console's on-screen keyboard (OSK) from the firmware and from a
live RPCS3 run of that firmware, and turns it into a specification a native implementation can
follow. It exists because the keyboard in `src/view.cpp` (`View::drawOsk`, `osk_*`) was drawn
free-hand from a video frame and is "similar but not the same": the grid is offset by 4-5 px,
the key faces are plain gradients instead of the firmware's per-row textures, there are no
button badges, no symbol palette, no prediction content, and the dim behind the panel is about
47% instead of the firmware's ~70%.

Read this with `work/osk-layout.proposed.json` (the machine-readable form of section 4) and
with `audit/FIRMWARE-PARITY-GAPS.md` row M6, which this document supersedes for the OSK.

## Evidence classes

The project's tags are used throughout: **M** = measured or traced (a firmware file and offset,
or a captured frame), **F** = fit or inference, **R** = recorded run.

Everything firmware-derived stays local: RCOs, modules, captures. This document carries only
names, offsets, constants, colours and conclusions.

## 1. Which firmware component draws the XMB keyboard -- M

`vsh.elf` (local `fw493/vsh_module/vsh.elf`, sha256
`54f6216dd498da243132377b830fa17c74cbe564ec8b8ebd40c0a2b071a525fa` per
`audit/TEXT-RENDERER.md`) carries the loader and parameter names for three keyboard
implementations, each a module + resource pair:

```
oskfullkeypanel   __osk_fullkey_view_loader__    /dev_flash/vsh/resource/oskfullkeypanel_plugin.rco
                                                 /dev_flash/vsh/module/oskfullkeypanel_plugin.sprx
                                                 oskfullkeypanel_param
oskpanel          __osk_view_loader__            /dev_flash/vsh/resource/oskpanel_plugin.rco
                                                 /dev_flash/vsh/module/oskpanel_plugin.sprx
                                                 oskpanel_param
osk               __osk_dialog_view_loader__     /dev_flash/vsh/resource/osk_plugin.rco
                                                 /dev_flash/vsh/module/osk_plugin.sprx
                                                 osk_param
```

Those strings are found by an ASCII scan of the decrypted vsh ELF (method: `strings`-style run
extraction, reproducible with `work/osk/prx-elf.ts <file> strings --ascii --min 5 --grep osk`).
The same scan shows the paf-side API the panel modules call:

```
oskDialogLoad  oskDialogUnLoad  oskDialogAbort  oskDialogSystemAbort
oskDialogGetSize  oskDialogSetLayout  oskDialogAddPanel  oskDialogKeyLayout
PhOskButton     IME/OSK library      sysutil osk_texture_surface
```

Interpretation (M for the names, F for the roles):

| Component | Role | Evidence |
|---|---|---|
| `oskfullkeypanel_plugin` | the **full qwerty keyboard** the XMB uses for text entry: 10-column grid, function rows, prediction column | its module carries the `1234567890/qwertyuiop/asdfghjkl'/zxcvbnm,.?` row data (section 3) and the captured keyboard's exact labels match it |
| `oskpanel_plugin` | the **10-key / phone panel** ("tenkey") style; its module carries only `abc/def/ghi/...` keypad rows | `oskpanel_plugin.prx` UTF-16BE rows at file 0x36048-0x36f00 are keypad strings; its RCO has `tenkey_list_window_focus` |
| `osk_plugin` | the **dialog host** for `oskDialog*`; its RCO page is `phonekey8` (8-key numeric panel) | `osk_plugin.rco` object tree: one page `phonekey8`, 31 objects |

The XMB screens that open it are not an XML node: the XMBML views hand text entry to the paf
osk dialog API. In the firmware's own XML, the Users column is a folder-pixmap provider
(`dev_flash/vsh/resource/explore/xmb/category_user.xml:11-20`, `user://localhost/create` and
`user://localhost/users`), and selecting **Create New User** opens the name dialog whose
"Edit" action opens the full keyboard. Other XMB text entry uses the same API from
`sysconf_plugin` (`editbox_network_wpakey_setting`, `editbox_network_wepkey_setting`,
`editbox_bt_device_manager_register_passkey_audio`), which is where password masking lives
(section 5.4).

Capture proof that the XMB path uses the full keyboard: the boot sequence
Users -> Create New User -> OK button -> Edit produced the keyboard in section 4, whose row 1
is `1234567890`, not the `phonekey8` keypad.

## 2. The resource: `oskfullkeypanel_plugin.rco` -- M

Local path `C:/dev/rpcs3/bin/dev_flash/vsh/resource/oskfullkeypanel_plugin.rco`,
sha256 `1e09cf4aca86a2e6804ce8467ee09f02ebe7df92a115b6a5067a1ffce73b7a77`,
193344 bytes after inflation, 132 objects, 203 images, 65 English strings (section 2.2).
`oskpanel_plugin.rco` is the same shape: 135 objects, 148 images, sha256
`69ffcb301a0ef39a8ddc18793ea13d83ff1df7cd91308cbaa32087706ed244a1`.

Object tree (offsets are file offsets into the inflated table; each entry is a 40-byte header,
`[type u16][?][label u32][?][size u32][children u32][next-sibling u32][...]`):

```
0x007160  ObjectTree                          children 6
0x007188    Page   "fullkey_panel"            76 B
0x0071d4      Plane "fullkey_osk_init_point" 140 B  children 37   <- panel root, all chrome hangs here
0x0088ac    Page   "fullkey_menue"            76 B
0x0088f8      Plane "fullkey_menuelist_base" 140 B  children 12   <- 9-slice + focus + 2 text styles
0x00911c    Page   "symbol_palette"          76 B
0x009168      Group "symbol_palette_group"   128 B children 55
0x00c0d4    Page   "notify_window"           76 B
0x00c120      Group "notify_window_group"    128 B children 7    <- message/ok/cancel/cross/circle texts
0x00c808    Page   "copy_window"             76 B
0x00c854      Group "copy_window_group"      128 B children 8    <- copy/paste message window
0x00cfc8    Page   "osk_pointer"             76 B
0x00d014      Plane "move_pointer"           140 B  image=POINTER, width 40 height 47
```

`fullkey_osk_init_point` children, in file order (all `Plane` 140 B unless noted, 37 of them):

```
0x007260 fullkey_osk_panel_base            panel 9-slice anchor
0x0072ec fullkey_list_base                 key-grid area
0x007378 fullkey_candidate_list_base0x0    candidate window 9-slice (3x3: x0/x1/x2, y0/y1/y2)
0x007404 fullkey_candidate_list_base0x1
0x007490 fullkey_candidate_list_base0x2
0x00751c fullkey_candidate_list_base1x0
0x0075a8 fullkey_candidate_list_base1x1
0x007634 fullkey_candidate_list_base1x2
0x0076c0 fullkey_candidate_list_base2x0
0x00774c fullkey_candidate_list_base2x1
0x0077d8 fullkey_candidate_list_base2x2
0x007864 oskpanel_base1x0
0x0078f0 oskpanel_base1x1
0x00797c oskpanel_base1x2
0x007a08 oskpanel_base2x0
0x007a94 oskpanel_base2x1
0x007b20 oskpanel_base2x2
0x007bac oskpanel_base2x3
0x007c38 oskpanel_base3x0
0x007cc4 oskpanel_base3x1
0x007d50 oskpanel_base3x2
0x007ddc fullkey_button_list_focus         focus ring for a key
0x007e68 fullkey_button_list_focus_offset  focus ring inset (alphaScale 0: the offset carrier)
0x007ef4 fullkey_button_input_mode
0x007f80 fullkey_button_input_mode_kb
0x00800c fullkey_button_input_mode_left
0x008098 fullkey_button_input_mode_right
0x008124 fullkey_button_input_mode_center
0x0081b0 fullkey_scroll_base               scrollbar track
0x00823c fullkey_scroll_knob               scrollbar knob
0x0082c8 fullkey_list_window_focus
0x008354 Text "fullkey_list_text"       272 B  key caption style
0x008464 Text "fullkey_list_text_focus" 272 B  key caption style, focused
0x008574 Text "fullkey_mode_text"       272 B  mode caption
0x008684 Text "fullkey_guide_text"      272 B  guide line
0x008794 fullkey_list_gem_guide_top
0x008820 fullkey_list_gem_guide_bottom
```

The four Text objects decode cleanly in their tail fields and agree on the caption style:
`size = 10` (paf text units, the same `size 10` template value the XMB's `text_info_msg`
carries), all colours (1,1,1) white, horizontal spacing 0, shadow `x=4, y=-5, perspective=0`,
shadow RGB (0.25, 0.25, 0.25) at alpha 1. Those are the template values, not output pixels;
the measured pixel sizes are in section 4.6.

`symbol_palette_group` children: `symbol_palette_bg` (0x0091e8), `scroll_bar_base` (0x009274),
`scroll_bar_knob` (0x009300), `symbol_focus` (0x00938c), then **`sym_btn01`..`sym_btn49`**
(0x009418-0x00bed8, 49 `Button` entries, 228 B each), then `guide_allow_up`/`guide_allow_down`.

### 2.1 What the object payload does and does not give

The 4.93 RCO entry payload does not follow the PSP-era `objattribdef-ps3.ini` field list: the
Plane payload is 88 bytes and the ini implies 100, the Text payload is 232 and the ini implies
188. Decoded against observed values, the position block is the first **13 floats**
(posX, posY, posZ, redScale, greenScale, blueScale, alphaScale, width, height, depth,
scaleWidth, scaleHeight, scaleDepth), which reproduces `move_pointer` exactly
(0,0,0,1,1,1,1,40,47,0,1,1,1 against its known 40x47 size). The remaining words are a
v4-specific encoding this pass did not resolve (the image field is not an index into the image
table: `move_pointer`'s words carry `0x00120000` where POINTER is image 200).

The reference words are **4+4 byte pairs [table tag][target]**, not single ids: tag 0x04020000
marks an image reference whose target is the *table offset* of the image record, tag 0x04010000
a text reference. Proof: `system_plugin.rco`'s `cross_icon` carries (0x04020000, 0x00000bf8) and
the image record named `tex_cross` sits at table offset 0xbf8; on this keyboard
`fullkey_scroll_base` resolves to `SCROLL_BASE` (12x10) and `symbol_palette_bg` to `BASE_SYMBOL`
(60x72) the same way. The remaining payload words (`iconOffset`, `unknown17/18`,
`unknownInt20/21`, ids 2631-3245 in this file) are a second reference scheme whose tables this
pass did not identify.

Resolved object to texture map for the panel (`work/osk/rco-objdump.ts <rco> tree`):

| Object | Texture | Size |
|---|---|---|
| `fullkey_osk_panel_base`, `fullkey_list_base` | none (assigned at runtime) | - |
| `oskpanel_base1x0..1x2`, `2x0..2x3`, `3x0..3x2` | `tbase1x0..tbase3x2` | 10x15, 1x15, 20x15, 10x328, 1x328, 1x328, 20x328, 10x62, 1x62, 20x62 |
| `fullkey_candidate_list_base*` (3x3) | `CANDIDATE_WINDOW*` | 10x9 / 1x9 / 19x9, 10x1 / 1x1 / 19x1, 10x18 / 1x18 / 19x18 |
| `fullkey_button_list_focus` | `BUTTON_FORCUS` (RCO default; the module re-assigns it) | 198x36 |
| `fullkey_button_input_mode` / `_kb` | `MODE_BASE` / `KB_MODE_BASE` | 62x30 |
| `fullkey_button_input_mode_left/right/center` | `TEXTURE_NONE` | 144x30 |
| `fullkey_scroll_base` / `_knob` | `SCROLL_BASE` / `SCROLL_KNOB` | 12x10 |
| `fullkey_list_window_focus` | `LIST_WINDOW_FOCUS` | 42x42 |
| `fullkey_list_gem_guide_top/bottom` | `LIST_WINDOW_GUIDE` | 40x33 |
| `fullkey_menuelist_base_*` | `MENU_BASE_*` (3x3) | 20x20 / 22x20 / 20x22 / 22x22 |
| `fullkey_menuelist_focus` | `MENU_FOCUS` | 15x15 |
| `symbol_palette_bg` | `BASE_SYMBOL`, and its own width/height are **7 x 7** | 60x72 |
| `symbol_focus` | `FOCUS_SYMBOL` | 53x53 |
| `scroll_bar_base` / `_knob` | `SCROLL_BASE_SYMBOL` / `SCROLL_KNOB_SYMBOL` | 12x10 |
| `guide_allow_up/down` | `LIST_WINDOW_GUIDE` | 40x33 |
| `move_pointer` | `POINTER` | 40x47 |
| `notify_window_bg`, `copy_window_bg` | `BASE_NOTIFY` | 66x66 |

What the RCO does *not* carry is the key faces and the key focus ring: `fullkey_list_base`'s
image is null and `fullkey_button_list_focus` defaults to the 198x36 `BUTTON_FORCUS`, while the
captured ring is the 84x66 `SBTN_FOCUS` (66+18 wide, 50+16 tall around a 66x50 key) and the key
faces are `BUTTON_BASE_*` at 72x54 and 144x54. Those assignments come from the module, whose
string table lists object names and image names next to each other (`fullkey_list_text`,
`SBTN_FOCUS`, `BUTTON_BASE_O`, `MODE_GUIDE_ALPHABET`, ...) - the shape of a `paf` panel builder
that looks an object up by name and then sets its image by name. So: geometry, 9-slice chrome,
palette and captions are in the RCO; the per-key faces, the key focus ring and the row data are
in the module. Tooling: `work/osk/rco-objdump.ts <rco> tree|images|text|hex|json`.

### 2.2 The text table -- M

The RCO's 65 English strings include every caption the panel draws, so the captions are text
lookups, not textures:

```
msg_space_osk                    = "Space"          <- the Space key caption
msg_back_space_osk               = "Backspace"      <- the Backspace key caption
GRAPHIC_input_mode               = "Input Mode"     <- the SELECT key caption
GRAPHIC_enter                    = "Enter"          <- the START key caption
msg_prediction                   = "Prediction"     <- the candidate column header
GRAPHIC_input_mode_en            = "A"              / GRAPHIC_input_mode_jp = "a" (hiragana)
msg_prediction_on/off            = "Prediction On" / "Prediction Off"
msg_start2                       = "Start"
msg_abc / msg_abc_accent         = "ABC" / "ABC" + A-grave   <- mode captions
msg_123 / msg_123_2byte          = "123" / fullwidth 123
msg_hiragana / msg_katakana      = Japanese kana mode captions
msg_url / msg_abc_ru             = "URL" / Cyrillic ABC
msg_del_all_shift_backspace_osk  = "[Shift] + [Backspace]: Delete All"
msg_line_break_alt_enter_osk     = "[Alt] + [Enter]: Line Break"
msg_help_dell_all_osk            = " + : Delete All"
msg_copied                       = "Copy completed."
msg_select_range_to_copy         = "Specify the range to copy."
msg_input_cancel_ask             = "Do you want to cancel text entry?"
msg_error                        = "An error has occurred."
msg_error_hdd_no_access          = "The system storage cannot be accessed."
msg_yes / msg_no / msg_fix / msg_cancel = "Yes" / "No" / "OK" / "Cancel"
msg_console_lang_*               = the keyboard-type names (English (United States), ...)
```

Two of those strings are behaviour evidence rather than decoration: the panel documents
**Shift+Backspace = delete all** and **Alt+Enter = line break** for a USB keyboard attached to
the console (section 6).

The caption text objects in the panel (`fullkey_list_text`, `fullkey_guide_text`) carry
`size = 10` and white (1,1,1) colours, so the caption style is one template at one size; the
measured pixel heights in section 4.6 are the rendered result at 1080p.

## 3. The layout data inside the module -- M

`oskfullkeypanel_plugin.prx` (decrypted ELF64 big-endian, sha256
`7b33ffb59d8cee5893dbc4dab784dfeed7d3e7b045a4f5b7f3f315d2d2b64256`). Its PT_LOAD0 starts at
file 0xf0 with vaddr 0, so **file offset = VA + 0xf0**.

### 3.1 Row strings (UTF-16BE, NUL terminated)

| File offset | VA | String | Meaning |
|---|---|---|---|
| 0x000403c0 | 0x000402d0 | `1234567890` | lower page row 1 (captured) |
| 0x000403d8 | 0x000402e8 | `qwertyuiop` | lower row 2 (captured) |
| 0x000403f0 | 0x00040300 | `asdfghjkl'` | lower row 3 (captured) |
| 0x00040408 | 0x00040318 | `zxcvbnm,.?` | lower row 4 (captured) |
| 0x00040420 | 0x00040330 | `!@#$%^&*()` | caps page row 1 (captured) |
| 0x00040438 | 0x00040348 | `QWERTYUIOP` | caps row 2 (captured) |
| 0x00040450 | 0x00040360 | `ASDFGHJKL"` | caps row 3 (captured) |
| 0x00040468 | 0x00040378 | `ZXCVBNM-_/` | caps row 4 (captured) |
| 0x00040540 | 0x00040450 | `asdfghjkl-` | symbol-variant page row 3 |
| 0x00040558 | 0x00040468 | `zxcvbnm/._` | symbol-variant row 4 |
| 0x00040570 | 0x00040480 | `!@#$%~&*;+` | symbol-variant row 1 |
| 0x00040588 | 0x00040498 | `ASDFGHJKL=` | symbol-variant row 3 (caps) |
| 0x000405a0 | 0x000404b0 | `ZXCVBNM:,?` | symbol-variant row 4 (caps) |
| 0x000407f8 | 0x00040708 | `!"#;%:?*()` | further variant row |
| 0x00040870 | 0x00040780 | `asdfghjkl` | accent-less variant row |
| 0x00040c7a | 0x00040b8a | `:!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~` | symbol palette order, page flag set |
| 0x00040cfa | 0x00040c0a | `!!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~` | symbol palette order, page flag clear |
| 0x00041178 | 0x00041088 | `Prediction` | English prediction caption (captured) |
| 0x00041190+ | | `Convertir`, `Predicci`, `Omzetten`, `Voorspellen`, `Konvertieren`, `Converti`, `Predizione`, `Converter`, `Konverter`, `Forudsigelighed`, `Konvertera`, `Prediktering`, `Forutsigelse`, `Ennakointi`, `Konwertuj`, `Podpowiedzi`, `Tahmin`, `Polski` | the same caption per console language |
| 0x00040b10-0x00040c70 | | hiragana/katakana rows (a-row, i-row, u-row, e-row, o-row) | Japanese layouts |

The `hiragana/katakana/ru/ko/zh` rows continue past 0x405b8; `work/osk/prx-elf.ts <file>
strings --utf16 --min 3` reproduces the whole list with offsets.

### 3.2 The label table

At file 0x5f994 (VA 0x5f8a4) there is an array of 24-byte records:

```
+0x00 u32  label string VA (UTF-16BE, NUL terminated)
+0x04 u32  0
+0x08 u32  id
+0x0c u32  1
+0x10 u32  1
+0x14 u32  0
```

`id` steps by 4 per record, and **five consecutive records form one layout**: four row
captions of ten characters plus a blank terminator. Decoding the groups from record 0x1bc
(`work/osk/osk-layout-scan.ts <prx> groups 0x5f9f4 6`) gives the lower page, then the caps page,
then four caption-less layouts whose keys come from textures:

| id range | Contents |
|---|---|
| 0x1ac-0x1b8 | fullwidth caps rows: fullwidth QWERTYUIOP, fullwidth ASDFGHJKL plus a fullwidth right quote, fullwidth ZXCVBNM plus hyphen/underscore/slash, blank |
| 0x1bc-0x1cc | **lower page**: 1234567890 / qwertyuiop / asdfghjkl' / zxcvbnm,.? , blank terminator |
| 0x1d0-0x1e0 | **caps page**: !@#$%^&*() / QWERTYUIOP / ASDFGHJKL" / ZXCVBNM-_/ , blank |
| 0x1e4-0x230 | blank records (layouts whose captions are textures, e.g. the keypad and CJK modes) |
| 0x234-0x244 | symbol-variant page (asdfghjkl- / zxcvbnm/._ / 1234567890 / qwertyuiop) |
| 0x248-0x258 | symbol-variant caps page (!@#$%~&*;+ / QWERTYUIOP / ASDFGHJKL= / ZXCVBNM:,?) |
| 0x25c-0x280 | repeats of the lower/caps pages (other keyboard types) |
| 0x284+ | Japanese kana rows (a-row a ka sa ta na ha ma ya ra wa; i-row i ki shi chi ni hi mi yu ri wo; ...) |

Reproduce with `work/osk/osk-layout-scan.ts <prx> table 0x5f994 40` (raw records) or
`... groups 0x5f9f4 6` (layout groups). The first two groups are exactly the two pages captured
in section 4, which is the strongest link between the module data and the screen.

### 3.3 Mode and texture names the module references

Captions/badges and per-language key labels (ASCII strings in the module):

```
@#:  is drawn as text           Badges: LABEL_UP LABEL_DOWN LABEL_LEFT LABEL_RIGHT
EN_LABEL_DELETE EN_LABEL_SPACE EN_LABEL_RETURN EN_LABEL_MODE EN_LABEL_CLOSE
(JR/FR/SP/GE/IT/DU/PO/RU/CP/FI/SW/DA/NO/KO/CH/TR versions of the same five)
MODE_SYMBOL MODE_SYMBOL_PICT MODE_10KEY MODE_OPTION MODE_FULLKEY MODE_MENU
MODE_GUIDE_ALPHABET MODE_GUIDE_ALPHABET_CAP MODE_GUIDE_ALPHABET_ACCENT
MODE_GUIDE_ALPHABET_ACCENT_CAP MODE_GUIDE_Z_ALPHABET(_CAP) MODE_GUIDE_URL
MODE_GUIDE_JP_HIRA MODE_GUIDE_JP_KANA MODE_GUIDE_KO MODE_GUIDE_ZH MODE_GUIDE_PY MODE_GUIDE_CJ
KB_MODE_GUIDE_* (the same set for the keyboard-mode cell)
FUNCTION_ABC FUNCTION_a_ac FUNCTION_h_k FUNCTION_h_z FUNCTION_SUBPANEL FUNCTION_JP_ABC
FUNCTION_h_z_CH FUNCTION_S_CHINA_ABC FUNCTION_S_CHINA_ABC2 FUNCTION_RU_ABC FUNCTION_KO_ABC
FUNCTION_CI_ABC
KEYTOP_COPY KEYTOP_PASTE KEYTOP_HIRAGANA_4x9 TEXTURE_NONE
SBTN_FOCUS LBTN_FOCUS MENU_FOCUS FOCUS_SYMBOL
snd_decide snd_cancel snd_cursor  (interaction cues the panel plays)
```

Object names it looks up: `fullkey_panel`, `fullkey_osk_init_point`, `fullkey_osk_panel_base`,
`fullkey_list_base`, `fullkey_candidate_list_base*`, `oskpanel_base*`, `fullkey_button_list_focus`,
`fullkey_button_list_focus_offset`, `fullkey_button_input_mode*`, `fullkey_scroll_base`,
`fullkey_scroll_knob`, `fullkey_list_window_focus`, `fullkey_list_text`, `fullkey_list_text_focus`,
`fullkey_mode_text`, `fullkey_guide_text`, `fullkey_list_gem_guide_top/bottom`,
`fullkey_menue`, `fullkey_menuelist_base*`, `fullkey_menuelist_text`,
`fullkey_disable_menuelist_text`, `symbol_palette*`, `notify_window*`, `copy_window*`,
`osk_pointer`, `move_pointer`.

## 4. The measured layout at 1920x1080 -- M

All numbers are pixels measured from the reference frames named in section 9, with
`work/osk/measure-keys.ps1` (brightness-run scans and point probes). The default page frame is
`C:/dev/out/osk-fw/s1/f0060-0257.png`.

### 4.1 Frame

| Element | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Panel | 493 | 290 | 934 | 500 | rounded ~6 px, 1 px dark outline, light 9-slice frame (`tbase*`) |
| Title bar | 494 | 290 | 932 | 46 | flat (141,142,140) with a sheen at the bottom edge |
| Title icon | 506 | 300 | 12 | 18 | pencil glyph (U+270E), white |
| Title text | 520 | 300 | cap height 20 | | ~27 px font, white, soft shadow; the text-entry dialog's own hint shows through at ~12% |
| Entry field | 494 | 336 | 932 | 58 | vertical gradient (86,86,86) -> (62,62,62) -> (47,47,47) |
| Key grid | 496 | 406 | 714 (10 cells) | 324 (6 rows) | cell 72 x 54, key face 66 x 50 |
| Prediction column | 1216 | 394 | 203 | 336 | list 246 tall + header 33 + button row 52 |
| Footer | 493 | 731 | 934 | 58 | (70,70,70) -> (95,96,94) |
| Panel dim | 0 | 0 | 1920 | 1080 | black at **alpha ~0.70** (measured multiplier 0.29-0.30 on the already-dimmed dialog) |

Grid arithmetic that confirms the cell size: the four character rows are 54 px apart
(406, 460, 514, 568), the function rows continue the same pitch (622, 676), key faces are 66 px
wide with a 6 px seam, and the two-column keys measure 138 px (2*66+6). The module's own
1-column key textures are 72x54 and its 2-column textures 144x54, i.e. the cell size including
the seam - an independent confirmation from the RCO image table.

### 4.2 Rows

Column c is at `x = 496 + 72*c`, row r at `y = 406 + 54*r`.

| Row | y | Keys (column: label) |
|---|---|---|
| 0 | 406 | 0:`1` 1:`2` 2:`3` 3:`4` 4:`5` 5:`6` 6:`7` 7:`8` 8:`9` 9:`0` |
| 1 | 460 | 0:`q` 1:`w` 2:`e` 3:`r` 4:`t` 5:`y` 6:`u` 7:`i` 8:`o` 9:`p` |
| 2 | 514 | 0:`a` 1:`s` 2:`d` 3:`f` 4:`g` 5:`h` 6:`j` 7:`k` 8:`l` 9:`'` |
| 3 | 568 | 0:`z` 1:`x` 2:`c` 3:`v` 4:`b` 5:`n` 6:`m` 7:`,` 8:`.` 9:`?` |
| 4 | 622 | 0:`@#:` 1:(blank) 2:`a/a-grave` L2 3:`a/A` R2 4-5:`Backspace` [square] 6-7:`Space` [triangle] 8-9:(blank) |
| 5 | 676 | 0:`[up]` 1:`[down]` 2:`[left]` L1 3:`[right]` R1 4:(grid icon) 5:(blank) 6-7:`Input Mode` SELECT 8-9:`Enter` START |

Function-row faces: measured x extents at y=645 are 498..560, 569..631, 641..703, 713..775
(1-cell) and 785..922, 929..1066, 1073..1210 (2-cell); at y=700 the arrow keys measure
500..559, 570..631, 642..703, 714..775.

Row 5's right-hand prediction column adds two icon buttons outside the 10-column grid:
copy at x 1217..1278 and paste at x 1322..1383, both y 678..730 (paste drawn disabled).

**Caps page** (frame `s4b/f0010-0038.png`): rows become
`!@#$%^&*() / QWERTYUIOP / ASDFGHJKL" / ZXCVBNM-_/`; the prediction mode cell changes from
`a` to `A`. Everything else is identical.

### 4.3 Symbol palette (the `@#:` key) -- M

`@#:` does **not** switch the grid to symbols. It opens a popup palette that overlays the left
half of the keyboard (frame `s11-symbol/f0108-0299.png`):

| Property | Value |
|---|---|
| Frame | x 595..948, y 398..730 (353 x 332), rounded, drop shadow (horizontal edges measured at y=500, vertical from the crop; +-4 px) |
| Grid | 7 columns, pitch 48, origin (634, 434); the RCO's container object reports width 7 x height 7 and there are exactly 49 `sym_btn` buttons, so a page is 7x7 = 49 cells; the popup is 5 rows tall and scrolls |
| Contents | printable ASCII 0x21-0x7e, 94 symbols in reading order, space excluded (it has its own key); 94 > 49, so the set is reached by scrolling and/or a second page - the capture shows only the first 35 cells and did not exercise the rest |
| Scrolling | yes - scrollbar strip at the right edge, `[` and `]`-style page marks at the bottom (`guide_allow_up/down`) |
| First cell | focused (light ring) when the palette opens |
| Textures | `BASE_SYMBOL` (60x72, the palette frame), `FOCUS_SYMBOL` (53x53, the focused cell), `SCROLL_BASE_SYMBOL` / `SCROLL_KNOB_SYMBOL` (12x10) |

Visible layout of the first rows: `! " # $ % & '` / `( ) * + , - .` / `/ : ; < = > ?` /
`@ [ \ ] ^ _ \`` / `{ | } ~`.

### 4.4 Prediction column -- M

| Element | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Candidate list | 1216 | 394 | 203 | 246 | flat (110,110,110), 9-slice `CANDIDATE_WINDOW*` |
| Header bar | 1216 | 641 | 203 | 33 | (133,133,133); caption "Prediction" at x 1230, cap height 12 (~16 px font) |
| Mode cell | 1364 | 641 | 27 | 33 | `a` (lower) / `A` (caps) - `MODE_BASE` + `MODE_GUIDE_*` |
| Count cell | 1393 | 641 | 27 | 33 | candidate count, `0` when empty |
| Copy button | 1217 | 678 | 62 | 52 | `KEYTOP_COPY`, enabled when text is selected |
| Paste button | 1322 | 678 | 62 | 52 | `KEYTOP_PASTE`, drawn disabled (nothing on the clipboard) |

Typing two characters (`s12-type/f0095-0260.png`) puts the typed string in the candidate list
and the counter stays 0: the list is a *prediction* list whose entries are selected with Up/Down
while the focus is in it.

### 4.5 Colours -- M

Sampled at 1080p from the default page unless noted.

| Element | RGB |
|---|---|
| Letter/digit key face | top (158,162,158), band (196,194,196), bottom (169,167,169) - a vertical gradient with a bright band at 40% height |
| Olive keys (row 4, cols 0-3) | (173,181,139) |
| Blue keys (Backspace/Space) | (140,173,179) |
| Arrow keys (row 5, cols 0-3) | (139,169,177) |
| Mint keys (Input Mode/Enter/blank) | (139,176,160) |
| Copy/paste keys | (137,169,175) |
| Key caption text | (30,40,50) |
| Title text | (250,250,250) on (141,142,140) |
| Field text | (255,255,255) on (47..86) gradient |
| Caret (I-beam) | **(130,249,12)** |
| Focus ring | `SBTN_FOCUS` 84x66 = 66+2*9 by 50+2*8, white outline |
| Dim overlay | black, alpha 0.70 |

### 4.6 Text sizes -- M (measured cap heights)

| String | Cap/x height | Derived font px | Frame |
|---|---|---|---|
| Title "User Name" | 20 | ~27 | s1 |
| Field "User 1" | 16 | ~22 | s1 |
| Key digit "1" | 19 | ~26 | s1 |
| Key letter "a" x-height | 13 | ~26 | s1 |
| "Backspace" / "Space" | 18 | ~24 | s1 |
| "Prediction" | 12 | ~16 | s1 |

The face is the same SCE-PS3 Rodin family the XMB text renderer already loads
(`src/font_raster.cpp`, `audit/FONT-MATCHING.md`); no new font file is needed. The captions
"Backspace", "Space", "Input Mode", "Enter" are drawn as *text* in the capture (they blur like
text at 1080p and they are the RCO text entries in section 2.2), while the button badges ([square], [triangle], L1, L2,
SELECT, START) come from the 74x28 `LABEL_*` textures.

## 5. The entry field -- M

### 5.1 Geometry

* Field rect: x 494, y 336, w 932, h 58.
* Text origin: x 514 (20 px inset), baseline ~369; cap height 16, font ~22 px, white.
* The text is clipped at the field edges and scrolls: the left inset is the clip origin.
* One line only. The captured field never wraps; a name longer than the field scrolls.

### 5.2 Caret -- M

Two marks, not one:

1. **I-beam caret**: bright green (130,249,12), a 2 px shaft with 12 px serifs at top and
   bottom, 27 px tall, sitting at the pen advance of the text before the insertion point
   (x 578..579 in the frame `s2/f0020-0077.png`, y 345..372).
2. **Cursor marker**: a hollow white triangle pointing left, ~14 x 20 px, immediately right of
   the I-beam (x 590..604, y 350..370). It is present whether or not the I-beam is lit.

The I-beam and the marker are both drawn by the OSK panel, not by the field's text object.
The I-beam was seen both lit and dark in consecutive captures, so it blinks; the captures were
taken at ~5 fps, which is too slow to resolve the period - the blink period and duty cycle are
**open** (F: the XMB's own caret conventions suggest ~500 ms, but that is a guess, not a trace).

The lit I-beam is green only while the keyboard owns input; when the OSK is open but not yet
focused the same mark is grey. Reproduce the two states rather than one.

### 5.3 Editing model -- M (observed) / F (inferred)

* Characters insert at the caret; the caret advances by the glyph advance (the field's own
  font, not a separate metric).
* Backspace deletes the character before the caret.
* L1/R1 move the caret left/right without deleting (the [left] [right] keys carry those badges).
* Enter accepts and closes; Circle closes without accepting (the dialog then shows the
  unchanged name).
* Text is horizontally scrolled to keep the caret visible (seen when the field already held
  "User 1" and the caret sat after it).

### 5.4 Masking

Not captured. The XMB's password fields are `EditBox` objects in other plugins
(`netconf_plugin.rco`: `editbox_network_wpakey_setting` at 0x164d4, `editbox_network_wepkey_setting`
at 0x16814; `deviceconf_plugin.rco`: `edit_bt_device_manager_register_passkey_audio` at 0xea50),
so the masked rendering is owned by `paf`'s osk dialog, not by `oskfullkeypanel_plugin`. The
native implementation already has a masked mode (`Osk::masked`, one bullet per character) that
matches what those fields show on a console in shape; what is **not** verified is the bullet
glyph, its advance relative to the real font, and whether the caret still shows. Treat masking
as "shape correct, metrics unverified" until a WPA-key capture exists.

## 6. Input behaviour -- M

Button-to-action mapping, read from the on-screen badges and confirmed by pressing them in
RPCS3 (Cross PgDown / Circle PgUp mapped through a temporary keyboard handler, section 9):

| Button | Action |
|---|---|
| Cross ([cross]) | press the focused key |
| Circle ([circle]) | back: close the keyboard without accepting (legend "[circle] Back") |
| Square ([square]) | backspace, same as the Backspace key (badge [square] on that key) |
| Triangle ([triangle]) | space, same as the Space key (badge [triangle] on that key) |
| L1 / R1 | move the text caret left / right (badges on [left] [right]) |
| L2 / R2 | accent page / caps page (badges on a/a-grave and a/A) |
| SELECT | Input Mode (badge SELECT on that key) |
| START | Enter / accept (badge START on that key) |
| D-pad | move the key focus |
| Any key | the focused key lights: the face goes to a white-blue gradient and the 84x66 `SBTN_FOCUS` ring is drawn at -9,-8 |
| Shift+Backspace (USB keyboard on the console) | delete all - documented by the panel string `msg_del_all_shift_backspace_osk` |
| Alt+Enter (USB keyboard on the console) | line break - documented by `msg_line_break_alt_enter_osk` |

Focus model: a single rectangular focus over the 10x6 grid; left/right wrap inside the row
(observed: pressing Right on the wide Space key lands on the next single key), up/down move a
row and keep the column, and the function rows are reached by moving down past row 3. Wide keys
occupy their span, so a horizontal move jumps the whole span.

Default focus on open: **the `g` key** (row 2, column 5) in two independent boots. No data in
the RCO or the module explains that choice (F: it may be the layout's centre key or a remembered
position); reproduce it as observed.

Repeat: not measured. The XMB list navigation repeats at 300 ms then 66.7 ms
(`src/view.cpp:1336`), and the OSK's own D-pad repeat was not exercised long enough to time.
Open.

## 7. Open and close animation -- M (kind) / open (duration)

The panel **fades**: frame `s6-close/f0006-0012.png` catches it at partial alpha with the
text-entry dialog's own text ("User 1") clearly visible through the title bar, the field and the
keys, while the same dialog text is fully covered at rest. The panel does not slide or scale as
a whole in that frame; the content is simply blended.

At rest the title bar still shows the dialog's hint text at roughly 12% contrast
(`crop-title.png`), so the panel is composited with a slight translucency even when settled.

Duration is **open**: the capture rig tops out near 5 frames per second, so a 200-300 ms fade
cannot be resolved. What is needed: a capture at >=30 fps, or the emulator slowed with
`Core: Clocks scale` (25% worked for navigation but the capture rate, not the emulator, is the
limit).

Page changes (lower <-> caps) and palette open/close also animate; the captures caught partial
states (a narrower, right-shifted panel with smaller keys in `s5/f0012-0049.png`), which is
consistent with a scale/alpha transition of the key list inside a fixed panel frame. **Not
settled:** in several states the *panel itself* is not centred - measured left edges 493
(default, `s1`), 532 (`s11`), 571 (`s4b`), 610 (`s12`), 649 (`s5`), with the width constant at
~934. The default page sits at 493 (the centred position); the others are unexplained and are
listed as an open question rather than as a layout rule.

## 8. What the firmware does that a native recreation cannot reproduce

1. **The paf scene graph itself.** The panel is built at runtime by `oskfullkeypanel_plugin.sprx`
   from the RCO's objects and the module's tables; the native app has no paf. The layout can be
   reproduced, the *mechanism* cannot, so any statement that the native OSK "is" the firmware's
   has to be scoped to geometry, colours and behaviour.
2. **Prediction.** The candidate list is filled by the console's IME/dictionary
   (`ime.prx`, `ime_atok.prx`, `ime_zi.prx` are present in `dev_flash/vsh/module`); English
   candidates came from the system dictionary. A PC build has no equivalent dictionary, and
   fabricating one would be a different feature. The list box, counter and selection
   interaction can be reproduced; the candidates cannot.
3. **Input Mode / language switching.** `Input Mode` cycles the console's input methods
   (hiragana, katakana, 2-byte ABC, pinyin, cangjie, zhuyin, Russian, Korean, ...) and depends on
   the console's keyboard type setting and on `ime_*` modules. The key and its mode caption can
   be drawn; the methods cannot.
4. **The copy/paste clipboard of the XMB.** The two buttons operate the console's own clipboard
   (`copy_window` page, "Copied" message). A PC build can wire its own clipboard, but that is a
   host feature, not parity.
5. **Sound.** `snd_decide`, `snd_cancel`, `snd_cursor` are played by the panel module; the native
   app has its own cue set (`src/interaction_audio.cpp`). Matching the *sample* would need those
   resources decoded; matching the *timing* needs a capture the audio path can be aligned to.
6. **Exact text rasterisation inside the panel.** The panel draws captions through paf's text
   path; the native app's glyph pipeline is already a reconstruction
   (`audit/TEXT-RENDERER.md`), so the captions inherit its open gaps.
7. **The panel's translucency behaviour.** The panel composites with alpha over the calling
   dialog, and the dialog's own dim interacts with it. The native app draws an opaque panel.

## 9. Reference captures

All under `C:/dev/out/osk-fw/`. Taken with RPCS3 0.0.43 (`FPS: ... | Vulkan | 0.0.43 | vsh.self`)
booting `dev_flash/vsh/module/vsh.self`, scratch config
`C:/dev/out/osk-fw/config.yml` (Resolution 1920x1080, exclusive fullscreen disabled, borderless
fullscreen, `Clocks scale` 25 for the slow runs), captured with
`work/osk/osk-cap.ps1` (CopyFromScreen of the client area, which is exactly 1920x1080 in
borderless fullscreen).

| Directory | Frames | What it shows |
|---|---|---|
| `s1/` | 61 | the XMB, the "Enter a user name." dialog, then the default keyboard page; **canonical reference** `s1/f0060-0257.png` |
| `s2/` | 21 | focus moved to Space, then Backspace; green I-beam caret (crop `crop-caret.png`) |
| `s3/` | 21 | focus on `a/A` - the focused-key state: white-blue face + `SBTN_FOCUS` ring |
| `s4-shift/`, `s4b/` | 16 + 11 | the caps page `!@#$%^&*() / QWERTYUIOP / ASDFGHJKL" / ZXCVBNM-_/`, prediction cell reads `A` |
| `s5/` | 13 | partial page-transition state (panel not centred, keys scaled) - the evidence for section 7's open question |
| `s6-close/` | 27 | the close fade (`f0006-0012.png`) |
| `s9-slow/` | 274 | full navigation + keyboard open at 25% clock rate |
| `s10/` | 37 | one Down press: focus `g` -> `b` |
| `s11-symbol/` | 109 | the **symbol palette** open over the keyboard |
| `s12-type/` | 96 | text typed ("User 1zz"), prediction candidate `zz` in the list, focus on `z` |
| `run1`-`run7` | 15-68 each | first-pass 901x545 captures that found the navigation path; superseded by the 1080p runs |

Crops used above: `crop-title.png` (title bar and field at 2x), `crop-field.png` (field 4x),
`crop-func.png` (function rows 2x), `crop-right.png` (prediction column 3x),
`crop-caret.png` (caret 8x).

### 9.1 Input configuration handling -- M

The global input config `C:/dev/rpcs3/bin/config/input_configs/global/Default.yml`
(`Player 1 Input: Handler DualSense, Device "DualSense Pad #1"`, 66 bytes) was backed up to
`C:/dev/out/osk-fw/Default.yml.orig`, its sha256 verified as
`e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` **before** any change, then
replaced for the capture with a Keyboard handler that maps Cross to PgDown and Circle to PgUp
(`C:/dev/out/osk-fw/Default.yml.capture`), per `HANDOFF-PHASES.md:110-115`. After the captures
the original was restored and its sha256 re-checked: **`e624da0d...c459ceee`, verified equal**.

### 9.2 Reproducing the run

```powershell
# 1. back up and hash the global input config, then install the capture mapping
Copy-Item C:\dev\rpcs3\bin\config\input_configs\global\Default.yml C:\dev\out\osk-fw\Default.yml.orig
(Get-FileHash C:\dev\out\osk-fw\Default.yml.orig -Algorithm SHA256).Hash  # e624da0d...c459ceee
Copy-Item C:\dev\out\osk-fw\Default.yml.capture C:\dev\rpcs3\bin\config\input_configs\global\Default.yml

# 2. boot
Start-Process C:\dev\rpcs3\bin\rpcs3.exe -ArgumentList '--no-gui','--config','C:\dev\out\osk-fw\config.yml',
  'C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self' -WorkingDirectory C:\dev\rpcs3\bin

# 3. navigate: 6x Left to the Users column, Up to "Create New User", Cross, Cross on "Edit"
powershell -File C:\dev\xmb\work\osk\osk-cap.ps1 -OutDir C:\dev\out\osk-fw\s1 -Seconds 26 -IntervalMs 250 `
  -Keys "1:LEFT,2:LEFT,3:LEFT,4:LEFT,5:LEFT,6:LEFT,8:UP,10:PGDN,13:PGDN"

# 4. restore
Copy-Item C:\dev\out\osk-fw\Default.yml.orig C:\dev\rpcs3\bin\config\input_configs\global\Default.yml
(Get-FileHash C:\dev\rpcs3\bin\config\input_configs\global\Default.yml -Algorithm SHA256).Hash
```

Notes learned the hard way: `SetForegroundWindow` is refused for a background caller, so the
capture script attaches to the foreground thread's input queue before sending a key (otherwise
an unrelated window - in this session the native app - receives it); CopyFromScreen captures
whatever is on top, so the emulator must own the foreground before every grab.

## 10. Implementation spec

The fence for this document is analysis only; this section names the changes, it does not make
them.

### 10.1 Preferred design: read the layout from the firmware at runtime

Do not hardcode the table. The app already opens firmware RCOs through `Assets`
(`src/assets.cpp`, `readContainer`/`image`) with the dev-flash root passed by `--dev-flash`, so
the data is present at run time.

New module `src/osk_layout.{h,cpp}`:

```cpp
struct OskKey { int column, span; QString caption; QString badgeTexture; QString faceTexture;
                QColor faceTop, faceBottom; QString action; };      // action: insert/space/backspace/...
struct OskRow { int row; QVector<OskKey> keys; };
struct OskLayout {
    QRect panel, titleBar, field, footer;            // 1920x1080 reference space
    QPoint gridOrigin; QSize cell; QSize face;       // 72x54 cell, 66x50 face
    QVector<OskRow> rows;                            // 6 rows, spans included
    QVector<QString> pages;                          // "lower", "caps", "symbol-variant", ...
    QRect candidateList, predictionHeader, copyButton, pasteButton;
    QColor dim; qreal dimAlpha;                      // black, 0.70
    qreal caretR,g,b; int caretHeight, caretShaft;   // 130,249,12 / 27 / 2
};
class OskLayoutSource {
public:
    // Reads dev_flash/vsh/resource/oskfullkeypanel_plugin.rco for the panel geometry and
    // dev_flash/vsh/module/oskfullkeypanel_plugin.sprx for the row strings and the label table,
    // and falls back to work/osk-layout.proposed.json (shipped as a Qt resource) when either is
    // missing, logging which source was used.
    bool load(const QString& devFlashRoot, const QHash<QString,QImage>& images);
    const OskLayout& layout() const;
};
```

What to query, exactly:

Reader: if the parallel P1.1 work has landed `src/rco_objects.{h,cpp}` (`RcoObjects`, which decodes
object properties, image references and the text table from the firmware bytes), use it - the OSK
loader then only needs geometry, the row strings and the image assignments. Otherwise read the
same data through `Assets` plus the offset constants in sections 2 and 3. Do not write a third
RCO parser.

| Value | Source |
|---|---|
| panel rect, title bar, field, footer, prediction column | object geometry of `fullkey_osk_init_point` and its children in `oskfullkeypanel_plugin.rco` (float position block, section 2.1); measured defaults from section 4.1 when a field reads 0 |
| image of each 9-slice object | its `image` reference resolved against the image table (section 2.1 table) |
| key cell / face size, grid origin | the same objects (`fullkey_list_base`, `fullkey_button_list_focus`, `oskpanel_base*`); cross-check against the 72x54 and 144x54 image sizes |
| row captions, per page | the UTF-16BE row strings in `oskfullkeypanel_plugin.sprx` at the offsets in section 3.1, selected through the label table at 0x5f994 (5 records per layout) |
| captions ("Space", "Backspace", "Input Mode", "Enter", "Prediction") | the RCO text table, by name: `msg_space_osk`, `msg_back_space_osk`, `GRAPHIC_input_mode`, `GRAPHIC_enter`, `msg_prediction` (section 2.2) |
| button badges and per-language label images | `LABEL_*` and `EN_LABEL_*` images in the RCO image table |
| key faces and focus ring | `BUTTON_BASE_WHITE`/`_B_S`/`_SHORT`, `SBTN_FOCUS`, `LBTN_FOCUS` via the existing `Assets::image("oskfullkeypanel_plugin", name)` |
| palette contents | the UTF-16BE run at 0x40c7a, split 7 per row |
| mode caption | `MODE_GUIDE_ALPHABET` / `_CAP` images |

If the firmware root is absent (`--dev-flash` not given, or a fresh data dir), fall back to the
committed JSON generated from `work/osk-layout.proposed.json` and log
`osk: layout from committed table (provenance ...)`. That keeps one source of truth with a
documented provenance chain: audit section 4 -> JSON -> runtime fallback.

### 10.2 Files and functions to change

| File | Function | Change |
|---|---|---|
| `src/osk_layout.h/.cpp` | new | the loader above; no Qt widgets, no GL |
| `src/view.h` | `struct Osk` | add `OskLayout layout; int page; bool paletteOpen; int paletteRow, paletteColumn; bool caretLit;` and keep `row/column/text/cursor/shift/symbols/masked` |
| `src/view.cpp` | `View::openOsk` | load the layout once (`oskLayout_` member, loaded at startup with the other assets), reset to the firmware default focus (row 2, column 5), page 0 |
| `src/view.cpp` | `View::oskCharacter` | replace the three hardcoded 10-char arrays with `layout().rows[r].keys[c].caption` for the current page |
| `src/view.cpp` | `View::oskMove` | walk the span-aware key list from the layout instead of the `oskFunctionRows` table; keep wrap; keep the "jump over multi-column keys" rule |
| `src/view.cpp` | `View::oskPress` | dispatch on the layout key's action string instead of `OskAction`; `@#:` opens the palette, the grid icon opens the palette, `Input Mode` is a no-op with a logged "unavailable" |
| `src/view.cpp` | new `View::oskPalette*` | palette state: 7 columns x 5 visible rows over the 94 ASCII symbols, Up/Down/Left/Right, Cross inserts the focused symbol |
| `src/view.cpp` | `View::drawOsk` | rewrite against the layout: panel 9-slice from `tbase*`, title bar with the dialog title, field with the measured gradient, key faces drawn with the per-row `BUTTON_BASE_*` textures (fall back to the measured gradients when a texture is missing), badges from `LABEL_*`, captions via `textRenderer_`, prediction column from section 4.4, footer, and **dim alpha 0.70** instead of 120/255 |
| `src/view.cpp` | caret draw (inside `drawOsk`) | replace the 2x36 green bar with the I-beam + hollow triangle (section 5.2); keep the metric from `textRenderer_.measure` |
| `src/view.cpp` | `View::paintGL` signature | include `osk_.page`, `osk_.paletteOpen`, `osk_.caretLit` so a page/palette/blink change invalidates the UI layer |
| `src/text_renderer.*` | none | the existing `measure()`/`draw()` already give the field and caption metrics; no change needed |
| `src/assets.cpp` | none | `Assets::image("oskfullkeypanel_plugin", name)` already resolves named images from any `vsh/resource` RCO |

Textures through the existing path: `Assets::image` returns a `QImage` decoded from the RCO's
GIM, drawn by `View::drawImage(painter, "oskfullkeypanel_plugin", "<NAME>", rect)` - the same
call the notification and options panels use. Key faces are 9-slice: draw the `BUTTON_BASE_*`
image into the cell rect with the corners unscaled (10 px horizontal, 15 px vertical border for
`tbase*`; 3 px for the button bases), or, if that reads wrong at 66x50, draw the gradient from
the layout colours - the textures were authored for exactly 72x54 cells, so a direct draw at the
cell size is the faithful option.

Caret and masking:

* caret position = `textRenderer_.measure(fieldFont, composed.left(cursor))`, the same call the
  current code makes; draw the I-beam at that x, 27 px tall, 2 px shaft with 12 px serifs, in
  (130,249,12); draw the hollow triangle 12 px to its right.
* blink: toggle `osk_.caretLit` on a timer; the period is unknown (section 5.2), so use 500 ms
  and mark it F in the code comment.
* masking: keep one bullet per character; the bullet glyph should be U+2022 in the same face,
  and the caret must still be drawn after the last bullet. Unverified against a console
  password field (section 5.4).

### 10.3 Verification

Build and capture the native app against the RPCS3 frames:

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-osk
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-shot.ts --exe C:/dev/out/native-osk/re-ps3-native.exe ...
```

| Native state | Compare with | Expected per-region score |
|---|---|---|
| OSK open, default page, no text (script opens the Jellyfin server-name field) | `C:/dev/out/osk-fw/s1/f0060-0257.png` | new layer "osk panel": >= 18 dB / 0.85 SSIM. The current free-hand OSK scores far lower: its grid sits 4-5 px left and up, dim is 47% vs 70%, faces are gradients and there are no badges |
| OSK open, caps page (press Right to `a/A`, Cross) | `s4b/f0010-0038.png` | "osk caps row": >= 18 dB / 0.85 SSIM |
| symbol palette open | `s11-symbol/f0108-0299.png` | "osk palette": >= 16 dB / 0.80 SSIM |
| text typed "zz" | `s12-type/f0095-0260.png` | "osk field + caret": >= 15 dB / 0.70 SSIM (text rasterisation is the reconstruction) |
| full frame with the OSK open | `s1/f0060-0257.png` | frame layer: >= 20 dB / 0.90 SSIM, since ~72% of the frame is the dimmed XMB |

Add the four layers to `work/ab-layers.thresholds.json` only as part of an implementation
change (they are deliberately not touched here), with the reference frames copied out of
`C:/dev/out/osk-fw/` into the reference tree at that point.

Behavioural checks that pixel scoring cannot cover: the 6x10 focus walk, span jumps, the caps
page round-trip (`a/A` twice returns to lower), the palette insert at the caret, backspace at
position 0, caret left/right, Circle closing without changing the field, and Start accepting.

## 11. Evidence index

| Path | What it records |
|---|---|
| `C:/dev/rpcs3/bin/dev_flash/vsh/resource/oskfullkeypanel_plugin.rco` | panel objects, 203 images, geometry (section 2) |
| `C:/dev/rpcs3/bin/dev_flash/vsh/resource/oskpanel_plugin.rco` | 10-key panel variant (135 objects) |
| `C:/dev/rpcs3/bin/dev_flash/vsh/resource/osk_plugin.rco` | `phonekey8` dialog host (31 objects) |
| `C:/dev/xmb/fw493/vsh_module/oskfullkeypanel_plugin.prx` | row strings 0x403c0/0x40420/0x40540, label table 0x5f994, palette 0x40c7a, image and object names, `snd_*` names |
| `C:/dev/xmb/fw493/vsh_module/oskpanel_plugin.prx` | keypad rows 0x36048+ (proves the 10-key style is a separate module) |
| `C:/dev/xmb/fw493/vsh_module/vsh.elf` | loader symbols, `/dev_flash/...` paths, `oskDialog*` and `PhOskButton` names |
| `C:/dev/rpcs3/bin/dev_flash/vsh/resource/explore/xmb/category_user.xml` | the XMBML view whose `user://localhost/create` entry leads to the name dialog |
| `C:/dev/out/osk-fw/s1/f0060-0257.png` | canonical default page (all geometry in section 4) |
| `C:/dev/out/osk-fw/s4b/f0010-0038.png` | caps page |
| `C:/dev/out/osk-fw/s11-symbol/f0108-0299.png` | symbol palette |
| `C:/dev/out/osk-fw/s12-type/f0095-0260.png` | typed text, prediction candidate |
| `C:/dev/out/osk-fw/s3/f0020-0078.png` | focused key (`a/A`) |
| `C:/dev/out/osk-fw/s6-close/f0006-0012.png` | close fade |
| `C:/dev/out/osk-fw/crop-*.png` | 2x-8x crops cited in sections 4.2, 5.2, 9 |
| `work/osk/rco-objdump.ts` | RCO object tree + property dumper (new; used by later phases) |
| `work/osk/prx-elf.ts` | ELF segment/string/xref reader for decrypted modules |
| `work/osk/osk-layout-scan.ts` | the 24-byte label table and row-string reader |
| `work/osk/measure-keys.ps1` | brightness-run and point probe measurement |
| `work/osk/osk-cap.ps1` | the capture rig (client-area grabs + scheduled keys) |
| `work/osk/oskfullkey-textures.txt` | the 203-image inventory with GIM sizes |
| `work/osk-layout.proposed.json` | the machine-readable layout of section 4 |
| `native-xmb/audit/FIRMWARE-PARITY-GAPS.md` row M6 | the gap this closes; its "no prediction content, Input Mode inert" description is superseded |
| `native-xmb/HANDOFF-PHASES.md:102-121` | the RPCS3 capture how-to this run followed |

## 12. The runtime layout module (phase 12.4) -- M

`native-xmb/src/osk_layout.{h,cpp}` resolves the keyboard from the firmware when the app runs. It
does not copy a layout table: `loadOskLayout(Assets&, RcoObjects&)` reads the resource through
`RcoObjects` (the P1.1 reader, not a second RCO parser), reads the module through
`Assets::vshModule` (the existing `--vsh-modules` path), and resolves the named textures through
`Assets::image` (the same call `View::drawImage` makes). `work/osk-layout.proposed.json` is used
only as the logged fallback when `--dev-flash` is absent or the firmware read fails.

### 12.1 What each source supplies

| Value | Source at run time |
|---|---|
| panel, title bar, field, footer, prediction column | measured reference sections 4.1/4.4 (the RCO panel planes store `width=0`; the module assigns them at runtime) |
| key cell / face, grid origin | `BUTTON_BASE_B_S` = 72x54 and `BUTTON_BASE_WHITE` = 144x54 through `Assets::image`, cross-checked against the measured 72x54 cell / 66x50 face |
| 9-slice image references | `oskpanel_base1x0..3x2`, `fullkey_candidate_list_base0x0..2x2`, `fullkey_menuelist_base0x0..2x2` object `image` references resolved through the image tree |
| palette 7x7 grid and 49 buttons | `symbol_palette_bg` width/height = 7/7 and the 49 `sym_btnNN` children of `symbol_palette_group` |
| lower/caps row strings | the 24-byte label table in `oskfullkeypanel_plugin.prx`; the first English groups are `0x5f9f4` (ids `0x1bc..0x1cc`) and `0x5fa6c` (ids `0x1d0..0x1e0`) |
| palette symbols | the UTF-16BE string at module file `0x40c7a`: one page flag character followed by 32 symbols |
| captions | the RCO text table by label: `msg_space_osk`, `msg_back_space_osk`, `GRAPHIC_input_mode`, `GRAPHIC_enter`, `msg_prediction`, `GRAPHIC_input_mode_en` |
| caption material | `Materials::textMaterial("oskfullkeypanel_plugin", "fullkey_list_text")`: size 10, white, shadow 4/-5 at alpha 1 |
| 52 named textures | the declared list from `work/osk-layout.proposed.json`, each checked against the RCO image table and then decoded through `Assets::image` |

The module SHA-256 is decoded from the same bytes and recorded in the layout (`7b33ffb5...b64256`,
407488 B). The RCO is reported as 132 objects, 203 images, 65 English strings, 193344 B. Bounds:
module 64 MiB, 64 program headers, 32 load segments, 64 code units per label string, 65536 label
records, 512 groups, 65536 palette candidates, 512 palette units. No string or table read is
unbounded, and every read is length-checked against the segment or file.

### 12.2 Resolved shape (firmware mode)

```
panel            493,290 934x500
title/field/footer 932x46 + 932x58 + 934x58
grid             496,406 cell 72x54 face 66x50 10x6
keys             rows 0-3: 10+10+10+10; row 4: 7; row 5: 8; total 55 (40 character)
pages            lower, caps, symbolPalette
palette          7x7 (49 buttons, 5 visible rows), 32 symbols, origin 634,434
                 cell 53x53 pitch 48x48 frame 595,398 353x332
prediction       list 1216,394 203x246; header 1216,641 203x33;
                 copy 1217,678 62x52; paste 1322,678 62x52;
                 copy KEYTOP_COPY enabled, paste KEYTOP_PASTE disabled
captions         Space | Backspace | Input Mode | Enter | Prediction
caret            130,249,12 height 27 shaft 2 serif 12 blink 500 ms (F, not traced)
dim              0,0,0 alpha 0.70
focus            row 2, column 4 (1-based column 5) = g
textures         52 named, 48 default-surface, 52 in image table, 38 decoded
```

The default focus is the capture's `g`: row 2, column 4 in the zero-based `View::Osk` state. The
audit's "row 2, column 5" is one-based; `g` is the fifth letter of `asdfghjkl'` when counted from 1.

### 12.3 The palette is 32 symbols, not 94

The string at module file `0x40c7a` is one page-flag character followed by the 32 punctuation
symbols:

```
!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~
```

The first character is `:` for the page-flag-set variant and `!` for the page-flag-clear variant,
so stripping it gives the same 32 symbols in both. The symbols fill 4 full rows of 7 plus 4 cells
of the fifth visible row, which is what the `s11-symbol` capture shows. The module string continues
with accented symbols after the ASCII run; the layout records them as `palette.extras` but does not
draw them on the captured page. Section 4.3's "94 printable ASCII" is superseded: 94 is the whole
printable ASCII range including digits and letters; the palette page is punctuation only.

### 12.4 Texture names and the decode gap

All 52 declared names exist in the RCO image table (203 images). The existing `Assets::image` path
returns pixels for 38 of them. The other 14 are named but fail with
`Unsupported GIM pixel format 0x5` (the same failure through `Assets::bestImage`, so it is not a
container-choice problem):

```
LABEL_UP LABEL_DOWN LABEL_LEFT LABEL_RIGHT
EN_LABEL_DELETE EN_LABEL_SPACE EN_LABEL_RETURN EN_LABEL_MODE EN_LABEL_CLOSE
KEYTOP_COPY KEYTOP_PASTE MODE_GUIDE_ALPHABET MODE_GUIDE_ALPHABET_CAP LIST_WINDOW_FOCUS
```

The layout therefore stores `present` (name in the image table) and `decoded` (pixels returned)
separately. The draw slice must use the text badge/caption fallbacks the keys already carry for
those 14, or a later pass must extend the asset decoder (the asset loader is outside the 12.4/12.5
write fence). No layout assertion depends on all 52 decoding: `textures` asserts the 52 names and
the 48 default-surface subset; `texturesDecoded` is reported as an informational skip (38/52).

### 12.5 Cost

Measured with `QElapsedTimer` inside `loadOskLayout` on the reference tree, 10 consecutive loads
in one process after the RCO body was warm (`native-osk-layout/osk-layout-check.exe --repeat 10`).
The first line is the cold-in-process load; later loads reuse `RcoObjects` and the decoded image
cache.

| Measurement | Result |
|---|---|
| first load, total | 9.76 ms |
| warm loads, total | 6.81 ms min / 8.23 ms median / 9.76 ms max |
| RCO read + parse (`RcoObjects`) | 0.27-0.45 ms, 193344 B |
| text table + caption map | 0.03-0.05 ms, 65 strings |
| module read + label/palette scan | 3.76-5.05 ms, 407488 B |
| image table + 52 texture lookups/decodes | 2.12-3.28 ms (38 decodes; 14 decoder failures) |
| fallback layout (no `--dev-flash`) | 0.10-0.27 ms, 0 B read |

The load is one startup call, not a per-frame cost. `View::paintGL` only reads the resolved
`OskLayout` (`page`, `keyAt`, `keyFaceRect`, `palette`).

### 12.6 Fallback provenance

With no `--dev-flash` the loader logs exactly:

```
osk: layout from committed table (provenance work/osk-layout.proposed.json; dev-flash absent)
```

The fallback carries the same 55-key shape, 934x500 panel, 72x54 cell, 7x7/49-button palette and
32-symbol palette page. It cannot resolve the 52 textures (0 in the image table, 0 decoded), so
the seven firmware-only assertions are skipped rather than failed. With `--dev-flash` present but
`--vsh-modules` absent, the RCO captions/geometry/textures are kept and only the row strings and
palette come from the committed table, with a log line naming the missing module.

### 12.7 Verification

The scratch build and self-check live outside the repository, at
`C:/dev/out/native-osk-layout` (build) and `C:/dev/out/native-osk-layout-src` (CMake and check
main), and compile the two new files without touching the repository's `CMakeLists.txt`:

```
& C:/dev/out/native-osk-layout/osk-layout-check.exe `
  --dev-flash C:/dev/rpcs3/bin/dev_flash `
  --vsh-modules C:/dev/xmb/fw493/vsh_module --repeat 1
```

Result: `osk: self-check PASS (33/33 applicable assertions; 1 skipped; source=firmware)`. The skip
is `texturesDecoded` (38/52; section 12.4). The same executable with no `--dev-flash` reports
`PASS (27/27 applicable assertions; 7 skipped; source=committed)`.

The View::Osk integration hunk was also applied to a scratch copy of `view.h`/`view.cpp` and the
full app built and linked (`C:/dev/out/native-osk-layout/full-repo/re-ps3-native.exe`), so the hunk
is compile-tested; the repository's `view.*`, `shell.*` and `main.cpp` remain untouched.

### 12.8 Files

- new `native-xmb/src/osk_layout.h`
- new `native-xmb/src/osk_layout.cpp`
- this section
- integration only (not applied here): one source-list line in `CMakeLists.txt`; the
  `View::Osk` state and call-site hunks are in the phase 12.4 hand-off report, because
  `src/view.*`, `src/shell.*` and `src/main.cpp` belong to the dialogs pass until 12.5.
