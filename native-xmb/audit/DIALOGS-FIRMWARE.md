# System dialogs and message boxes - firmware specification vs native

Scope: every surface this build treats as a "dialog" or "message page" - the confirm/error message box,
the error-code page, the inline empty-state message, the titled status page, the information page, the
file delete/copy confirmation and progress pages, and the busy indicator - plus the PS menu's own
"Turn Off the System?" confirmation.

Analysis only: no source file was edited for this document. Firmware data stays out of the repository;
every raw dump this document cites is written under `work/` (git-ignored) or already exists under `out/`
(git-ignored), and only derived identifiers, object names and the handful of English strings the mapping
needs appear below.

## Evidence classes

- **RCO** - read from a local RCO object/resource table (`out/native-reference/rco-behavior/resources.json`,
  `work/rco-*.txt`). Offset is the object's offset in that RCO.
- **TRACED** - read from decompiled `vsh.elf` (Ghidra project `vsh493w`, dumps `work/dialog-mbox.c`,
  `work/dialog-layout.c`, `out/native-reference/phase3-system-layout.c`). Address is the code address.
- **DATA** - a constant read from `vsh.elf` `.data` at the address the traced code loads
  (`fw493/vsh_module/vsh.elf`, read with `work/prx-read.ts`).
- **MEASURED** - pixel measurement of a local firmware reference frame from the existing recording
  extraction `out/native-reference/recording-193556` (324 frames, 200 ms apart, 1262x800 window capture;
  content rect maps to 1920x1080 at scale 1.52139, y offset 70.7 px). No new capture was made.
- **NATIVE** - the present C++ implementation.
- **UNPROVEN** - nothing local decides it; section 5 says what would.

---

## 1. Dialog inventory: which RCO and which plugin provides each dialog type

There is no single "dialog RCO". The XMB's dialogs are built at run time out of four separate providers:
a texture/string/label RCO (`system_plugin.rco`), an error-text RCO (`msgdialog_plugin.rco`), the
per-category plugin's own dialog *pages* (`explore_plugin_full.rco` and the `explore_category_*.rco`
files), and `vsh.elf` code that creates the plate, the text, and the button row.

| # | Dialog type | Provider RCO | Provider code | Objects / assets used |
|---|---|---|---|---|
| D1 | System message box ("mbox"): normal, error, progress | `system_plugin.rco` (textures, button-hint page, animations, button labels) | `vsh.elf` 0x0034002C (open), 0x00387550 (construct), 0x00344094 (plate), 0x00343FFC (bars) | `tex_msgdialog_bg`, `tex_msgdialog_bg2`, `tex_msgdialog_bg_deep`, `tex_msgdialog_bar`, `tex_cross/_circle/_triangle/_rectangle` + `_shadow`, `page_btnnavi/btnbar` |
| D2 | Error-code page ("Possible Cause / Suggested Actions") | `msgdialog_plugin.rco` | `vsh.elf` 0x0034002C, 0x00340964, 0x003406F8, 0x00341870 | runtime `mbox_try_button`, `mbox_detail_scroll`, `mbox_detail_label`; `tex_default_text_focus`, `tex_default_scroll_*` |
| D3 | Inline empty-state / status message in a list | `explore_plugin_full.rco` (`msg_error_*`) | explore_plugin page code (not decompiled in the local set) | none - text only, in the item-label column |
| D4 | Titled status page (icon + title above a rule, centred body, one hint) | `explore_plugin_full.rco` pages; `system_plugin.rco` for the hints and `tex_infobar` | explore_plugin page code | `page_btnnavi/btnbar` hints, `page_infobar` |
| D5 | Information page | `explore_plugin_full.rco` `info_list` page (+ `filedel_dialog_info`, `thumb_info_base` for the file variant) | explore_plugin code | `info_base_plane`, host thumbnail, `Key: value` rows |
| D6 | File delete: confirmation, progress, result | `explore_plugin_full.rco` `expage_fdelete_dialog` | explore_plugin `FUN_0002xxxx` (calls the D1 API) | `filedel_dialog_listbase`, `filedel_dialog_icon`, `filedel_dialog_info`, `filedel_dialog_msgdialog`, `fdel_text_info_msg`, `fdel_cmdbtn_*` |
| D7 | File copy / "file exists" confirmation, progress, result | `explore_plugin_full.rco` `expage_fcopy_dialog`, `expage_fcopy_exdlg`, `expage_fcopy_msgdlg` | explore_plugin code | `thumb_info_base`, `filedialog_listbase`, `text_info_msg`, `text_wait`, `plane_exist_dimmer`, `plane_exist_dialog_btn` (rename/overwrite/nosave), `plane_exist_chkbase` (apply-all) |
| D8 | Busy / wait indicator | `system_plugin.rco` `page_busy` | `vsh.elf` busy page | `busy_icon`, `busy_short`, `busy_long`, `tex_busy` (36x36), `tex_busy_s`/`tex_busy_l` (8x36), `tex_busy_shadow` |
| D9 | PS button system menu: "Turn Off the System?" | none - no RCO dialog | none traced | **native-only surface**; no firmware reference exists locally (audit/PS-BUTTON.md measured the menu, not its confirmation) |

Key negative results, because they shape the rest of the document:

- `msgdialog_plugin.rco` (sha256 fb41b8ef...) contains exactly **two objects**: an ObjectTree (0x800 @16324)
  and `page_msgdialog` (0x801 @16364) with **no children**. The message box has no declared layout; every
  plane, label and button is created by `vsh.elf` and positioned by code.
- No RCO in the 122-resource inventory declares any `mbox_*` object. The only dialog-ish declared page
  is `page_btnnavi/btnbar` in `system_plugin.rco`, which is the button-hint row, not the box.
- `system_plugin.rco` is the only place the message-box *looks* live: 3 plate tints, 1 gradient bar,
  4 button glyphs with shadows, the hint row with 8 fade animations, and the button-label strings.

---

## 2. Layout per type

### 2.1 D1 - system message box

**Objects and assets (RCO, `system_plugin.rco`, sha256 a52058c610ea9315):**

| Asset | Image-table offset | Size | Pixels |
|---|---|---|---|
| `tex_msgdialog_bg` | 52760 | 12x12 | flat RGB(54,56,54), alpha 116 |
| `tex_msgdialog_bg2` | 52848 | 12x12 | flat RGB(0,0,0), alpha 255 |
| `tex_msgdialog_bg_deep` | 52928 | 12x12 | flat RGB(43,50,60), alpha 221 |
| `tex_msgdialog_bar` | 53016 | 960x1 | white, alpha ramp 0 at both ends -> 255 at the centre |
| `tex_cross` / `tex_cross_shadow` | 55304 / 55588 | 30x30 / 40x40 | button glyph + shadow |
| `tex_circle` / `tex_circle_shadow` | 56256 / 56588 | 30x30 / 40x40 | button glyph + shadow |
| `tex_triangle` / `_shadow` | 57256 / 57612 | 30x30 / 40x40 | button glyph + shadow |
| `tex_rectangle` / `_shadow` | 58376 / 58576 | 30x30 / 40x40 | button glyph + shadow |
| `tex_infobar` | 59108 | 1x125 | infobar plate |

**Plate ("panel background") and nine-slice.** All three plate textures are 12x12 with every one of the
144 pixels identical, so the plate is a **flat fill at 1:1 colour and alpha; there are no nine-slice
margins to recover for the message box** - stretching a uniform tile and nine-slicing it are pixel
identical. TRACED 0x00344094 selects the pair by kind:

- kind 0 (normal): slot 1 = `tex_msgdialog_bg`, slot 2 = `tex_msgdialog_bg2`;
- kind 2 (deep): slot 1 = `tex_msgdialog_bg_deep`, slot 2 = `tex_msgdialog_bg2`;
- otherwise: no RCO texture at all (slot 0 is cleared and a default is used).

The effective colour of the normal plate over the XMB is RGB(54,56,54) at 116/255 = 0.455 - a *neutral
grey* wash, not black. The deep variant is RGB(43,50,60) at 0.867 - a *blue-grey* near-opaque plate, i.e.
the firmware has two distinct dialog bodies distinguished only by these two tints.

The only dialog-family texture that is a real nine-slice candidate is `tex_notification` (64x64, a 1 px
border with rounded corners), which this build already draws with inset 16 (`src/view.cpp:945-970`,
`drawNineSlice`). No dialog uses it.

**Title and body icons.** `system_plugin.rco` ships no dedicated dialog icon. Its status-glyph family is
the `tex_notification_*` set, all 100x100: `_info`, `_caution`, `_settings`, `_friend`, `_music`,
`_mediaserver`, `_headset`, `_keypad`, `_trophy_{platinum,gold,silver,bronze}`, plus the plain
`tex_notification` plaque tile. `_info` and `_caution` are the two that read as a dialog body icon
(error vs information), and `_info` is what U+2139-style "there is something to know here" states use
elsewhere in this build's notification path. Which one a given message box draws is **UNPROVEN**: the
icon plane is one of the run-time planes created in the unresolved layout path (section 2.1,
"Screen position and size"), and no local frame shows an error box.

**Bars.** TRACED 0x00343FFC assigns `tex_msgdialog_bar` to a bar object **only when its index is < 2**,
so exactly **two** bars exist per box. The texture is 960 px wide and centre-bright: drawn to the full
1920 width it peaks at the screen centre and fades to nothing at both edges; drawn at its native 960 it
covers x 480..1440. Which of the two the firmware does is **UNPROVEN** (the bar plane's size is set by a
code path outside the decompiled range).

**Button row.** RCO `page_btnnavi` @37100 -> `btnbar` Group @37176 with identity colour scales
(red/green/blue/alpha all 0, i.e. hidden at rest) and four children, each a Plane holding a glyph Plane
and a Text:

| Child | Plane offset | Icon | Label text object | Fade animation |
|---|---|---|---|---|
| `cross_shadow` | 37304 (0x91B8) | `cross_icon` @37444 | `cross_label` @37584 | `anim_cross_show` @45016 / `anim_cross_hide` @44916 |
| `circle_shadow` | 37856 (0x93E0) | `circle_icon` @37996 | `circle_label` @38136 | `anim_circle_show` @44816 / `_hide` @44716 |
| `triangle_shadow` | 38408 (0x9608) | `triangle_icon` @38548 | `triangle_label` @38688 | `anim_triangle_show` @45216 / `_hide` @45116 |
| `rectangle_shadow` | 38960 (0x9830) | `rectangle_icon` @39100 | `rectangle_label` @39240 | `anim_rectangle_show` @45416 / `_hide` @45316 |

The four fade animations are the only animation entries that concern a dialog. Each is
`Fade` (0x906), `durationRaw` 100 with the same value stored again as float 0x42C80000 (= 100.0),
`accelModeRaw` 0, target = the matching `*_shadow` plane (the 8-byte target reference decodes to the
plane offset above). The payload float is 0x3F800000 (1.0) on `_show` and 0x00000000 on `_hide`, so the
pair is "fade this button group to 1" / "to 0" over 100 units. **Enter/exit of the hint row is therefore
a 100-unit fade, accel mode 0, per button - not a slide, not a scale.** The unit is not proven to be ms
in the RCO itself; every other measured paf duration in this project is milliseconds, so read it as
100 ms with that caveat.

The label texts are the `system_plugin` strings fetched by TRACED getters 0x00341BA8..0x00341BF0:
`msg_ok` ("OK"), `msg_cancel` ("Cancel"), `msg_option` ("Options"), `msg_back` ("Back"),
`msg_enter` ("Enter"), `msg_no` ("No"), `msg_yes` ("Yes"). The same RCO also carries `msg_cross`,
`msg_circle`, `msg_cross_gaiji`, `msg_circle_gaiji`, `msg_cross_nagaoshi_gaiji` - empty-looking in a text
dump because they are private-use glyph code points, i.e. the two button symbols exist as *characters*
as well as textures. Firmware strings that embed a button use the `%e` substitution token (for example
`sysconf_plugin msg_formatted_pls_restore = ... Press the %e button to restart the system.`), which is
where those gaiji strings are consumed.

**Screen position and size at 1080p.** UNPROVEN for the box itself. RCO gives none (every `mbox_*`
object is created at run time), and the creation code (0x00387550) asks paf for layout through
unresolved virtual calls: it issues `(obj, 0x4F, 9)` on the first sub-plane, `(obj, 0x4F, 1)` on the
second, then a virtual at vtable +0x5C with `(id 7, 8, 0, float, float)` on both. 0x00344094 adds
`(7,1)` and `(7,9)` through vtable +0xA8, and 0x003406F8 issues `(0xC9, 0x2D, 0xA0)` = (201, 45, 160)
on the try button. Without an identified vtable those numbers cannot be converted to a rectangle, and
inventing one would be exactly the fitting this audit exists to stop. What *is* traced and numeric:

| Quantity | Value | Source |
|---|---|---|
| dialog open/close fade of the dialog's own label object | 200 units, target alpha 1.0 | TRACED 0x00341BFC + DATA 0x0068C978=200.0, 0x0068C97C=1.0 |
| plate fade | 1000 units on the plate child | TRACED 0x00344094 + DATA 0x0068CA1C=1000.0 |
| label animate: durations / scale / offsets | 260.0, 40.0, 300.0, 200.0, factor 0.14 | TRACED 0x00341D30 + DATA 0x0068C984..0x0068C99C |
| bar count | 2 | TRACED 0x00343FFC |
| hint row fade | 100 units per button, accel 0 | RCO animation table |
| error-page scroll settle | 150 units | TRACED 0x00340964 + DATA 0x0068C2FC=150.0 |
| try-button row metrics | 16.6 leading, (201, 45, 160) | TRACED 0x003406F8 + DATA 0x0068C2F4=16.6 |

Other DATA constants in the same block that belong to this widget and are not yet assigned:
0x0068CA00 = 0.12, 0x0068CA08 = 5.0, 0x0068CA0C = 0.5, 0x0068CA10 = -100.0, 0x0068CA14 = 100.0,
0x0068CA18 = 1.0, 0x0068CA2C = 2000.0, 0x0068CA30 = 0.8, 0x0068C2F8 = -0.5, 0x0068C300 = 0.0.

**Dim / backdrop.** No dimmer is declared for the message box in any RCO. The only dimmer plane found in
a dialog page is `plane_exist_dimmer` in `expage_fcopy_dialog` (a Plane whose red/green/blue/alpha scales
are all 0 at rest, i.e. black, fully transparent until shown). The two firmware frames that do show a
message (section 5) have **identical background luminance** to the frame without one, so the firmware
does **not** black the XMB behind an inline message or a titled status page. A dim behind the *modal
message box* is UNPROVEN either way; neither reference frame is a modal.

**Sounds.** TRACED 0x00341870: pressing the box's confirm button plays `snd_decide` from
`system_plugin` (`_opd_FUN_0035de60(1.0, system_plugin, "snd_decide")`) and then hides the label and the
button. No cue is traced for open, cancel or error in the decompiled mbox range, so:
open = UNPROVEN, confirm = `snd_decide` (the same cue the native maps to `Cue::Confirm`), cancel and
error = UNPROVEN (`snd_cancel` / `snd_system_ng` exist in the RCO's nine-entry sound table and are the
obvious candidates, but nothing local ties them to a dialog).

### 2.2 D2 - error-code page

Structure (TRACED 0x0034002C): the box is created with `mbox_try_button` labelled from
`msgdialog_plugin` `msg_errorcode_solution` ("Suggested Actions"), carrying `tex_default_text_focus` in
its second texture slot and the numbers (18,16), (79,1), (19,112) fed to the same unresolved vtable
+t0xA8. Its hint row is set from the `msg_enter` and `msg_back` getters - i.e. **an error-code page ends
in "Enter" and "Back" labels, not a Cross/Circle glyph pair**.

The body is `mbox_detail_scroll` (TRACED 0x00340964): a Scroll built over `tex_default_scroll_base`,
`_base_shadow`, `_slider`, `_arrow_up`, `_arrow_up_shadow` (all `system_plugin`), containing
`mbox_detail_label`. It is vertically centred on its content by a factor of **-0.5** of the measured
content height (DATA 0x0068C2F8) and then settled with a **150-unit** animation. The page's long text
comes from `msgdialog_plugin.rco`: `msg_errorcode_solution_label` ("Suggested Actions"),
`msg_errorcode_cause_label` ("Possible Cause"), `msg_errorcode_solution_label_explanation`,
`msg_errorcode_if_no_solution_ask_ic_with_errorcode` (the "write down error code %1 and contact
technical support" line), and 16 `msg_errorcode_solution_*` action bullets. This is the firmware's
"cannot be displayed / here is what went wrong" page: a *title, an icon, two full-width rules, a
scrollable suggested-actions body, and an Enter/Back hint row*.

### 2.3 D3 - inline empty-state message (MEASURED)

Reference: `out/native-reference/recording-193556/frame-00014400.jpg` (14.4 s), the exact frame the
current source comment cites. What the firmware actually draws for
`explore_plugin_full msg_error_no_content` ("There are no content items."):

| Property | Firmware (measured, 1080p) |
|---|---|
| frame treatment | **none** - the XMB, category band, ribbon, particles and info bar are at full brightness |
| rules | **none** |
| button hint | **none** |
| text ink box | x 630..981, y 494..513 (centre x 806, centre y 504) |
| alignment | left-aligned at x ~630, i.e. the level-1 item-label column's left edge |
| vertical placement | on the focus row (y 505), not in a plate |
| ink height | 20 px -> font ~24-28 px |
| other elements | the "back" chevron at x ~350, y ~505 is still drawn; the clock and info bar are intact |

So the firmware's answer to "this list has nothing in it" is a *list-row message*, not a dialog. The
native treats the same string as a full-screen modal (section 4).

### 2.4 D4 - titled status page (MEASURED)

Reference: `frame-00020600.jpg` (20.6 s), "Search for Media Servers" / "Scanning... Please wait." /
"Back". This is the firmware shape the native dialog is trying to be, and it is measured:

| Element | Firmware (measured, 1080p) |
|---|---|
| header icon | x 119..~145, y 154..188 (24-34 px) |
| header title | left-aligned from x ~152, ink y 160..188 (font ~22-24 px), above rule 1 |
| rule 1 | full width, y 197 (core row 197-199), peak alpha ~0.22 over the background, **flat across x** |
| body | centred on x 960; line 1 ink y 509..530, line 2 ink y 542..559; line pitch ~31-33 px; font ~24 px |
| rule 2 | full width, y 877, ~1-2 px, alpha ~0.15, **flat across x** |
| hint | glyph + label at x 1155..1238 (centre 1197), ink y 889..909 (glyph ~20 px) |
| frame treatment | **none** - background luminance identical to the 14.4 s frame at every sampled point |
| info bar | plate and date text **absent**; only the analog clock face remains, centred ~(1776, 93) |

The two rule positions are within 2 px of the native dialog's 199 and 879, and the hint baseline is
within 8 px of the native's 895. The native's *geometry* for this surface is therefore close; its
*texture, hint content, dim, and header treatment* are not (section 4).

### 2.5 D5 - information page

Firmware object spine: `explore_plugin_full.rco` `info_list` Page @225676 with `info_base_plane`
@225752. The file variant adds `expage_fdelete_dialog.filedel_dialog_info` @229456 and
`expage_fcopy_dialog.thumb_info_base` @225968. The page is a header (host icon + title) above a rule,
then `Key: value` rows, then a `Back` hint - the same frame as D4.

Firmware field labels exist and are the correct source for these rows (`explore_plugin_full.rco`):
`msg_information` ("Information"), `msg_info_detail` ("Details"), `msg_format` ("Format"),
`msg_size` ("Size"), `msg_created_date_time` ("Created"), `msg_created_date` ("Filmed"),
`msg_resolution` ("Resolution"), `msg_genre` ("Genre"), `msg_album` ("Album"),
`msg_info_unknown` ("Unknown"), `msg_info_none` ("-").

There is **no** firmware string for a filesystem path or a "Kind", which matters for section 4.
**UNVERIFIED geometry:** the native source cites "reference 92 s" for this page, but the local recording
extraction is only 64.6 s long (`frames.json` durationMs 64639) and no other frame set exists under
`out/`. The 92 s frame cannot be checked locally.

### 2.6 D6 - file delete dialog

`explore_plugin_full.rco` `expage_fdelete_dialog` @229100 holds: `filedel_dialog_listbase` @229176,
`filedel_dialog_icon` @229316, `filedel_dialog_info` @229456, `filedel_dialog_msgdialog` @229596 (the
Plane that hosts a D1 box as a child), `fdel_plane_src_cmdbtn` @229736 with `fdel_cmdbtn_select_all`
@229876, `fdel_cmdbtn_deselect_all` @230104, `fdel_cmdbtn_decide_src` @230332, and `fdel_text_info_msg`
@230560 (Text, size field 10, shadow offset 4/-5 at 0.25).

Every one of those Planes has posX/posY/width/height = 0 in the RCO - the plugin positions them at run
time. The confirmation itself is a D1 box attached to `filedel_dialog_msgdialog`; TRACED
`out/native-reference/explore_plugin_all.c` around lines 21332, 21713, 21786, 22047 calls the D1 open
API with (text, type, 0, button table, button count, flags, flags, 100) - the trailing **100** matching
the RCO fade duration. Strings (`explore_plugin_full.rco`): `msg_del_*_ask` ("The image will be
deleted. Are you sure you want to continue?" etc.), `msg_deleting` ("Deleting... Please wait."),
`msg_deleted` ("Delete completed."), `msg_error_del` ("An error occurred during the delete operation.").

### 2.7 D7 - file copy / "file exists"

`expage_fcopy_dialog` @225892 (planes as listed in section 1), plus the empty pages
`expage_fcopy_exdlg` @228948 and `expage_fcopy_msgdlg` @229024 - two page stubs whose children are
created at run time, one per dialog variant. `plane_exist_dialog_btn` @227756 carries the three
answers as Buttons: `cmdbtn_rename`, `cmdbtn_overwrite`, `cmdbtn_nosave`; `plane_exist_chkbase` @228580
carries `cmdbtn_chk_applyall`. `text_wait` @227344 is the "Please wait." line and `text_info_msg`
@227072 the message line. `plane_exist_dimmer` @227616 is the black dimmer (all scales 0 at rest).
Strings: `msg_copy`, `msg_copy_plural`, `msg_copying` ("Copying... Please wait."),
`msg_copied` ("Copy completed."), `msg_copy_cancel_ask`, `msg_copy_incomplete*_ask`,
`msg_error_copy`.

### 2.8 D8 - busy indicator

`system_plugin.rco` `page_busy` @36324: `busy_icon_shadow` @36400, `busy_icon_anim1` @36540,
`busy_icon` @36680 with children `busy_short` @36820 and `busy_long` @36960, both Plane 8x36. Textures
`tex_busy` 36x36, `tex_busy_s` 8x36, `tex_busy_l` 8x36, `tex_busy_shadow`. The two 8x36 strips are the
animated sweep of the 36x36 ring. The firmware's text form of the same state is
`msg_sigining_in` / `msg_copying` / `msg_deleting` / `msg_wait` ("Please wait...").

### 2.9 D9 - PS menu confirmation

The "Turn Off the System?" step is native-only: `src/shell.cpp:1539` sets `psConfirmOff_`, and
`src/view.cpp:1506-1510` redraws item 0 as "Turn Off the System?" with an invented second line
("X turns the XMB off; the PC stays on   O cancels"). No firmware RCO, string or frame for this surface
exists locally, so it is out of scope for parity claims - it should keep being described as native.

---

## 3. String mapping: the firmware's own strings for the actions this build performs

All firmware text below is the English string in `system_plugin.rco`, `msgdialog_plugin.rco` or
`explore_plugin_full.rco`; the full tables are in `work/dialog-text.txt` and
`out/native-reference/rco-text.txt`.

### 3.1 Dialog button labels (one set, all dialogs)

| Key | English | Where |
|---|---|---|
| `msg_ok` | OK | `system_plugin`, getter 0x00341BA8 |
| `msg_cancel` | Cancel | `system_plugin`, getter 0x00341BB4 |
| `msg_yes` | Yes | `system_plugin`, getter 0x00341BF0 |
| `msg_no` | No | `system_plugin`, getter 0x00341BE4 |
| `msg_back` | Back | `system_plugin`, getter 0x00341BCC (also the D4 hint) |
| `msg_enter` | Enter | `system_plugin`, getter 0x00341BD8 (also the D2 confirm hint) |
| `msg_option` | Options | `system_plugin`, getter 0x00341BC0 |
| `msg_cross` / `msg_circle` (+ `_gaiji`, `_nagaoshi_gaiji`) | glyphs, not letters | `system_plugin`; consumed by the `%e` token in other strings |

### 3.2 Empty list / nothing to show (the app's `msg_error_no_content` case)

| Firmware key | English | Native site that should use it |
|---|---|---|
| `msg_error_no_content` | There are no content items. | `shell.cpp:663` "This folder is empty." |
| `msg_error_no_image` | There are no images. | photo folder with no images |
| `msg_error_no_savedata` | There is no saved data. | saved-data rows |
| `msg_error_no_gamedata` | There is no game data. | Game Data Utility |
| `msg_unsupported_data` (label) / `msg_error_unsupported_data` ("The data type is not supported.") | the "cannot be displayed" family | photo/video open failures (`shell.cpp:2152`, `:2405`) |
| `msg_error_corrupted_data` | The data is corrupted. | decode failures |
| `msg_error_hdd_full` | There is not enough free space on the system storage. | download/copy failures |
| `msg_error_hdd_no_access` | The system storage cannot be accessed. | library read failures |

### 3.3 Delete / remove

| Firmware key | English |
|---|---|
| `msg_del_content_ask` | The content will be deleted. Are you sure you want to continue? |
| `msg_del_image_ask` | The image will be deleted. Are you sure you want to continue? |
| `msg_del_imagefolder_ask` | The folder and all images in the folder will be deleted. Are you sure you want to continue? |
| `msg_del_title_ask` | The title will be deleted. Are you sure you want to continue? |
| `msg_del_track_ask` | Are you sure you want to delete the track? |
| `msg_del_images_ask` | The selected images will be deleted. Are you sure you want to continue? |
| `msg_del_title_protected_ask` | This title is protected. Are you sure you want to delete the title? |
| `msg_deleting` | Deleting... Please wait. |
| `msg_deleted` | Delete completed. |
| `msg_error_del` | An error occurred during the delete operation. |
| `msg_del` / `msg_del_plural` | Delete / Delete Multiple (the options-panel row labels) |

### 3.4 Copy (the app's library copy paths and its "downloads" wording)

| Firmware key | English |
|---|---|
| `msg_copy` / `msg_copy_plural` | Copy / Copy Multiple |
| `msg_copying` | Copying... Please wait. |
| `msg_copied` | Copy completed. |
| `msg_copy_cancel_ask` | Do you want to cancel the copy operation? |
| `msg_copy_incomplete_hdd_ask` | There is not enough free space on the system storage. The copy operation may not be completed successfully. Do you want to continue? |
| `msg_error_copy` | An error occurred during the copy operation. |

### 3.5 Sign-in / sign-out

| Firmware key | English | Note |
|---|---|---|
| `explore_plugin_full msg_signout` | Sign Out | the menu row label; also in `explore_category_network`/`_friend` |
| `system_plugin msg_signed_out` | Signed out. | result message |
| `system_plugin msg_sigined_in` | Signed in. | result message (firmware's own typo) |
| `system_plugin msg_error_session_signout` | An error has occurred. You have been signed out of PSN. | error result |
| `system_plugin msg_same_id_signin` | Another user with the same online ID has signed in. You have been signed out. | error result |
| `msgdialog_plugin msg_error_wrong_idpw` | The sign-in ID or password is not correct. ... | sign-in failure body |
| `msgdialog_plugin msg_error_no_signin` | You must sign in to PSN. | gating message |
| `npsignin_plugin msg_sigining_in` | Signing in... Please wait. | progress body |
| `npsignin_plugin msg_error_wrong_email_address` | The sign-in ID (email address) is not correct. | field-level error |

Negative result worth recording: **there is no `*_signout_ask` string in any local RCO**. The firmware's
"are you sure you want to sign out" body is composed at run time (the Yes/No box with `msg_yes` /
`msg_no`), so this build cannot cite a firmware body for a sign-out confirmation - only the label
(`msg_signout`) and the result messages above.

### 3.6 Settings save

| Firmware key | English | Note |
|---|---|---|
| `sysconf_plugin msg_error_setting` | The setting could not be completed. | the save-failure dialog body |
| `sysconf_plugin msg_cancel_setup_ask` | Do you want to cancel? The settings you have made so far will not be saved if you cancel. | leaving an uncommitted setup |
| `sysconf_plugin msg_registered` | Add completed. | success pattern: a short completed sentence, no dialog |
| `sysconf_plugin msg_clbrt_ycon_finished` | Calibration completed. | same pattern |

A successful settings change has **no** dialog in the firmware: the value is applied and the row's own
text changes. Only a failure (`msg_error_setting`) or an abandoned setup (`msg_cancel_setup_ask`) is a
dialog.

### 3.7 Generic error display

| Firmware key | English |
|---|---|
| `system_plugin msg_error` / `msgdialog_plugin msg_error` | An error has occurred. |
| `msgdialog_plugin msg_errorcode_cause_label` | Possible Cause |
| `msgdialog_plugin msg_errorcode_solution_label` | Suggested Actions |
| `msgdialog_plugin msg_errorcode_if_no_solution_ask_ic_with_errorcode` | If none of the items listed here resolve the issue, write down error code "%1" and then contact technical support for assistance. |
| `system_plugin msg_error_connect` | A connection error has occurred. |
| `system_plugin msg_error_dns` | An error occurred during communication with the server. This is a DNS error. |
| `system_plugin msg_error_network` | A network error has occurred. |
| `system_plugin msg_error_comm_server_timeout` | Communication with the server timed out. |
| `explore_plugin_full msg_error_corrupted_data` | The data is corrupted. |
| `explore_plugin_full msg_error_unsupported_data` | The data type is not supported. |

---

## 4. What differs today, and the exact changes

Native references: dialog `src/view.cpp:1459-1471`; info page `src/view.cpp:1434-1458`; the shared
`text` lambda `src/view.cpp:1087-1090`; `drawImage` `src/view.cpp:835-853`; `drawNineSlice`
`src/view.cpp:945-970`; message sources `src/shell.cpp:381-2658`; information rows
`src/shell.cpp:2047-2092`; PS confirm `src/shell.cpp:1539`, `src/view.cpp:1506-1510`.

### 4.1 Side by side

| Element | Firmware (evidence) | Native (NATIVE) | Verdict |
|---|---|---|---|
| inline empty-list message | a list-row line, left at x 630, centre y 504, ~24-28 px, no plate (MEASURED 14.4 s) | full-screen modal: dim + 2 bars + centred heading + centred body + "OK" (view.cpp:1459-1471) | **wrong surface** |
| frame dim behind a message | none (MEASURED: identical background luma at 14.4 s and 20.6 s) | `fillRect(0,0,1920,1080, QColor(0,0,0,135))` - 53% black (view.cpp:1462) | **invented** |
| full-width rules | flat line, alpha ~0.22 (rule 1) and ~0.15 (rule 2), uniform across x (MEASURED) | `tex_msgdialog_bar` stretched to 1920, centre-bright: in the 1600x900 native capture the rule row reads luma 41 at x 20, 88 at x 500, 254 at x 800 (the screen centre), 49 at x 1400, 39 at x 1580 | **wrong texture/curve** |
| rule y positions | 197 and 877 (MEASURED) | 199 and 879 (view.cpp:1464) | correct within 2 px |
| dialog title | left icon + left title above rule 1 at y 154..188 (MEASURED) | centred heading at y 250..300, size 25 (view.cpp:1468) | **wrong anchor and wrong row** |
| body text | centred x 960, first line ink top y 509, pitch ~31-33 px, ~24 px font (MEASURED) | centred, box top y 320, 22 px font (view.cpp:1469) | **too high, ~2 px small** |
| button hint | glyph + label at x 1155..1238, ink y 889..909, glyph ~20 px, label from `system_plugin` (MEASURED) | glyph 24 px at x 902 + hardcoded "OK" at 21 px (view.cpp:1470) | **centred instead of right-of-centre; text not from firmware** |
| button-hint row motion | 100-unit fade per button group, accel 0 (RCO anim) | none - the row appears and disappears with the message | **missing** |
| message open/close motion | 200-unit fade of the dialog label; 1000-unit plate fade (TRACED) | none | **missing** |
| plate | `tex_msgdialog_bg` RGB(54,56,54) @ 116/255; `bg_deep` RGB(43,50,60) @ 221/255; no nine-slice (RCO) | no plate at all | **missing** |
| dialog sounds | confirm = `snd_decide` (TRACED); open/cancel/error UNPROVEN | Confirm/Back/Error cues fire from shell actions, not from the dialog | partially right cue, wrong trigger |
| info page header | icon+title left, rule, rows, `Back` right-of-centre (MEASURED D4/D5) | icon 36 px at (107,158), title 22 px at (158,156), bars at 199/866, hint at (1133,882) (view.cpp:1441-1458) | **matches the firmware convention** |
| info page fields | `Format` / `Size` / `Created` / `Filmed` / `Resolution` / `Genre` / `Album` | `Kind:` / `Detail:` / `Path:` / `Value:` (shell.cpp:2083-2088) | **no firmware source for Path/Kind** |
| info page reference claim | "reference 92 s" (view.cpp:1435) | - | **unverifiable: the local frame set ends at 64.6 s** |
| error page | title/icon, 2 rules, scrollable `Suggested Actions` body, Enter/Back hints (TRACED) | no scrollable error page; the whole detail is one wrapped block | **missing surface** |
| confirm/cancel rows | two-hint row from `page_btnnavi` (`msg_ok`, `msg_cancel`, `msg_yes`, `msg_no`) | one hint, "OK", always Cross | **no two-button dialog exists** |

### 4.2 The changes, in dependency order

1. **Split the two message surfaces** - `View::drawScene` (`src/view.cpp:1076-1471`).
   Introduce an explicit kind instead of inferring it from the string (`src/view.cpp:1081-1086`):
   `MessageKind::Inline` (D3), `MessageKind::Page` (D4), `MessageKind::Box` (D1), `MessageKind::ErrorCode`
   (D2), `MessageKind::Info` (D5). `Shell` decides the kind at the call site that sets `message_`
   (`src/shell.h`, `Shell` message API; `src/shell.cpp` all `message_=` sites), and `View` only draws it.
   Runtime-read source for the set of kinds: the RCO/texture facts in section 2 - a kind with a plate
   needs `tex_msgdialog_bg*`, a kind with rules needs `tex_msgdialog_bar`, a kind with a hint row needs
   `page_btnnavi`.

2. **Draw `Inline` the firmware way** - replace the modal branch for empty lists with a single line of
   item text at `x 630`, vertically on the focus row (`y 505`), sized from the measured ink box
   (x 630..981 for 25 characters at ~24-28 px). No plate, no dim, no rules, no hint. Update the empty
   list sites: `src/shell.cpp:663` ("This folder is empty.") and the empty-store/queue sites
   (`:1139`, `:1188`) to `MessageKind::Inline` with the firmware strings from 3.2.

3. **Remove the invented dim** - delete `src/view.cpp:1462` for every kind. If a dim is ever wanted,
   it must be justified per surface; the only RCO evidence for one is `plane_exist_dimmer` in the copy
   "file exists" dialog (section 2.7), not the message box.

4. **Replace the rule texture for page header rules** - `src/view.cpp:1443` and `:1464` currently
   stretch `tex_msgdialog_bar` (960x1, centre-bright) to the full width. A flat rule is what the
   firmware draws (alpha ~0.22 at y 197, ~0.15 at y 877, uniform across x). Keep `tex_msgdialog_bar`
   only where a message *box* is drawn and its own bars are wanted, and then at the box's own width,
   not 1920.

5. **Move the dialog heading** - for `Page` and `Box`, draw the icon at x ~119 and the title at x ~152
   with ink at y 154..188 (the same code path the info page already uses at `src/view.cpp:1441-1442`),
   instead of a centred heading at y 250..300. Delete the "heading detection" heuristic at
   `src/view.cpp:1466-1468` and carry the title explicitly.

6. **Set the body metrics** - `Page`: first line ink top y 509, ~24 px, pitch ~31-33 px.
   `Box`: body centred under the title, same size. Both read the font family from
   `shell_.fontFamily()` as now; the *size* becomes a named constant replacing the literals 22/25 at
   `src/view.cpp:1469` and `:1468`.

7. **Draw the real button row** - replace the single hardcoded `tex_cross` + "OK" at
   `src/view.cpp:1470` with a row built from the firmware's own tables:
   glyph = `tex_cross`/`tex_circle`/`tex_triangle`/`tex_rectangle` (30x30, alpha-bounds fitted by
   `drawImage`, `src/view.cpp:835-853`), label = `assets_.text("system_plugin", ...)` for
   `msg_ok`, `msg_cancel`, `msg_yes`, `msg_no`, `msg_back`, `msg_enter`, `msg_option`
   (`View` already holds an `Assets` instance - `assets_.image(...)` at `src/view.cpp:1421`, `:1443` -
   so no new plumbing is needed; the info page's
   `assets_.text("sysconf_plugin","msg_tool_button_behavior_back","Back")` at `src/view.cpp:1458` is
   the existing precedent for a firmware-sourced hint label).
   Anchor the row right-of-centre at x 1155 with ink y 889..909 (measured), not centred at 902.
   Two-button boxes draw Cross+label then Circle+label on the same baseline.

8. **Add the hint-row fade** - the RCO animation table gives the only traced dialog motion: each button
   group fades 0->1 (show) or 1->0 (hide) over 100 units with accel mode 0. Implement it as a `PafEase`
   on the row's opacity reusing the existing mode-0 curve (`View::PafEase`, `src/view.cpp:125-156`,
   `src/view.h`), one entry per button, and add it to the UI signature the same way `optionsOpenEase_`
   is (`src/view.h`).

9. **Add the box's own fade** - 200 units on the label/plate group (`TRACED 0x00341BFC`), 1000 units on
   the plate child (`TRACED 0x00344094`). Same easing machinery.

10. **Draw the plate** - a flat fill using the firmware colours: normal RGB(54,56,54) at 116/255,
    deep RGB(43,50,60) at 221/255, black RGB(0,0,0) opaque behind the text. Read the *colour* from the
    RCO at run time rather than hardcoding: `Assets` already decodes RCO images, so a 1x1 sample of
    `assets_.image("system_plugin","tex_msgdialog_bg")` yields both the RGB and the alpha. Nine-slice is
    not needed (12x12 uniform), so `drawNineSlice` stays unused for dialogs.

11. **Error page** - add `MessageKind::ErrorCode`: header (icon + `msg_information`-style title), the two
    rules, and a scrollable body built from the `msg_errorcode_*` strings, with `msg_enter` /
    `msg_back` hints and the 150-unit settle (`TRACED 0x00340964`). Scroll implementation: reuse the
    page-list scroll the build already has rather than a new widget.

12. **Sounds** - fire `Cue::Confirm` (`snd_decide`) from the dialog's own confirm path, not from the
    action that opened it, matching `TRACED 0x00341870`. Cancel and error stay on `Cue::Back` /
    `Cue::Error` until a cue is traced; note in the code comment that open/cancel/error cues are
    unproven.

13. **Information page fields** - keep the page (its geometry matches the firmware convention), but
    relabel the rows from firmware strings: `Format` (`msg_format`), `Size` (`msg_size`), `Created`
    (`msg_created_date_time`) / `Filmed` (`msg_created_date`), `Resolution` (`msg_resolution`).
    `Path:` has no firmware source; either drop it or keep it explicitly labelled as native
    (`src/shell.cpp:2086-2088`). Values the app cannot know stay `msg_info_unknown` ("Unknown") or
    `msg_info_none` ("-") rather than being omitted, which is what the firmware does.

14. **Two-button dialogs** - when a destructive path is added (delete, remove-from-list), use the
    firmware pair: body from 3.3, buttons `msg_ok`/`msg_cancel` or `msg_yes`/`msg_no` (Cross on the
    accept label, Circle on the cancel label), routed through the same `Page`/`Box` draw code. The
    existing PS-menu confirmation (`src/view.cpp:1506-1510`) keeps its native wording until a firmware
    PS-menu reference exists.

Verification for each item: the existing `--script` harness plus a named `grab=` state, and the A/B gate
`work/ab-layers.ts`; no new RPCS3 run is needed for items 1-10 and 13, because their targets are already
measured in frames 14.4 s and 20.6 s.

---

## 5. Reference frames: what exists, what is spec-only

Extracted from the existing capture set - **no new capture was made**:

| Frame | Path | What it proves |
|---|---|---|
| 14.4 s | `out/native-reference/recording-193556/frame-00014400.jpg` | D3 inline empty-state message: no dim, no rules, no hint, line at x 630 / y 504, info bar intact |
| 20.6 s | `out/native-reference/recording-193556/frame-00020600.jpg` | D4 titled status page: left icon+title, flat rule at y 197, centred body at y 509/542, flat rule at y 877, hint at x 1155 / y 889, no dim, info bar plate hidden but the clock face kept |

Derived comparison images (local, not committed): `work/dialog-img/compare-grid.png` (2x2 firmware vs
native), `work/dialog-img/ref20600-hint-crop.png`, `work/dialog-img/ref20600-infobar-crop.png`,
`work/dialog-img/ref14400-infobar-crop.png`, plus the exported textures
`work/dialog-img/tex_msgdialog_*.png`, `tex_cross.png`, `tex_circle.png`, and the other two glyphs.

What the local set does **not** contain, and therefore what stays spec-only:

| Surface | Status | What would prove it |
|---|---|---|
| two-button confirm box (Cross/Circle rows, Yes/No) | spec-only: RCO + TRACED only, no frame | one console/RPCS3 capture of any "Are you sure?" box (e.g. delete a copied file on a scratch device) |
| the box's plate rectangle at 1080p | spec-only: positions live in unresolved paf vtable calls | the same capture plus a decode of the box's plane vertices, or identification of vtable +0x5C / +0xA8 |
| error-code page | spec-only | one capture of a failing network sign-in (it produces the code page) |
| dim behind a modal (if any) | spec-only, and contradicted for the two non-modal surfaces | the two-button capture, sampling background luma before and during |
| button-hint fade (100 units) | RCO only | a burst of the hint row appearing (100 ms at 60 Hz = 6 frames) |
| box open/close fade (200 / 1000 units) | TRACED only | the same burst |
| information page ("reference 92 s") | **unverifiable locally** - the frame set ends at 64.6 s | the original longer recording, or one capture of an Information page |
| PS menu "Turn Off the System?" | native-only, no reference | a PS-hold capture on the console |

Two honest notes for whoever picks this up:

- The comment at `src/view.cpp:1460-1461` says frames 14.4 s / 20.6 s show "the XMB dims, two thin
  full-width rules at y~200 and y~880". Half of that is right (the rule positions, on the 20.6 s frame)
  and half is not (there is no dim in either frame, the 14.4 s frame has no rules, and the 20.6 s frame
  has no centred heading). The native dialog was designed around a reading of these frames that the
  frames do not support.
- `work/rco-behavior/resources.json` reports `fields` for Planes exactly as stored. Every dialog Plane in
  `explore_plugin_full.rco` is posX/posY/width/height = 0, which is real data, not a parse failure:
  those pages are laid out by plugin code. Any future "RCO says the dialog is at (0,0)" reading would be
  wrong.

---

## 6. Evidence paths

Firmware data (git-ignored; regenerate, do not commit):

- `work/rco-tree-dump.ts` - object-tree dumper over the pre-digested inventory
  (`--list`, `--res <rco>`, `--grep`, `--fields`).
- `work/dialog-text.txt` - strings of `msgdialog_plugin`, `system_plugin`, `poweroff_plugin`,
  `user_plugin`, `profile_plugin`, `npsignin_plugin`, `data_copy_plugin`, `filecopy_plugin`,
  `ps3_savedata_plugin`, `gamedata_plugin`, `download_plugin`, `friendim_plugin`, `np_sns_plugin`,
  `software_update_plugin` (`--rco-text`).
- `work/dialog-mbox.c` - decompiled `vsh.elf` 0x00340008..0x00341AA0 (mbox open, plate, bars, error
  page, button handling, `snd_decide`).
- `work/dialog-layout.c` - decompiled `vsh.elf` 0x00342248..0x00344400 (dialog input/repeat, plate
  texture selection 0x00344094, bar texture assignment 0x00343FFC).
- `out/native-reference/phase3-system-layout.c` - decompiled 0x00343FFC, 0x00387550 (mbox construction),
  0x00341870 (confirm cue).
- `work/layout-decompile.c` - decompiled 0x00341AA0..0x00342150 (msgdialog string lookup, the seven
  button-label getters, the 200-unit fade helper, the label animator).
- `work/rco-system-images.txt`, `work/rco-system-pixels.txt` - `system_plugin.rco` image table with
  offsets, GIM headers and first pixels.
- `work/prx-read.ts` - big-endian constant reader for `fw493/vsh_module/vsh.elf`
  (0x0068C2E0..0x0068CA40, 0x0068C970..0x0068C9B0).
- `out/native-reference/rco-behavior/resources.json` - the 122-RCO object inventory this audit queries.

Native and reference evidence (existing):

- `out/native-reference/recording-193556/` - the reference frame extraction (324 frames) and
  `frames.json`.
- `out/native-reference/phase-continuation/dialog-awake.png`, `dialog-idle.png`,
  `out/native-reference/phase2/dialog-1080.png`, `final-dialog.png`,
  `out/native-reference/font-match/dialog.png` - native message-dialog captures.
- `out/native-reference/options/options-info.png` - native information page.
- `out/native-reference/ps-menu/native-ps-menu-confirm.png` - native PS-menu confirmation.

---

## 7. Implemented (2026-10-27 pass)

This section records what was built from sections 1-4, what was measured against the two reference
frames afterwards, and what is still open. All numbers are 1920x1080. Every capture was taken from the
build this section describes (`C:\dev\out\native-dialogs\re-ps3-native.exe`) with the animation clocks
pinned (`--render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00
--focus-time 2000`), then measured by pixel analysis (ink = pixels above the local background by 45
luma; the native text boxes are differences against the same frame with the message dismissed, so the
animated background cancels out). One exception is stated where it matters: the titled-page pair was
captured from the build immediately before the final recompiles, which touched only the box and
hint-row plumbing - the page constants and draw path are byte-identical.

### 7.1 The surface model

`Shell` now decides the surface where it raises the message, and `View` only draws it:

| Kind | Who sets it | Draw path |
|---|---|---|
| `Inline` | empty list / empty page sites (`openSteamFolder`, Steam Store empty, Download Management empty, Continue Watching empty, an open directory page with no rows) | `View::drawInlineMessage` |
| `Page` | every titled status message (start failure fallback, crash return, Steam/RPCS3/app/URL failures, max depth, theme/media failures) | `View::drawMessagePage` |
| `Box` | the two-button confirm (`Remove from Continue Watching`) | `View::drawMessageBox` |
| `ErrorCode` | a launch that fails after the OS rejects it (`startTracked`, `QProcess::FailedToStart`) | `View::drawErrorMessagePage` |
| `Info` | `Action::Information`, the download page, the Continue Watching info page | `View::drawMessageInfoPage` |

`Shell::showMessage(text, kind)` is the only writer of `message_`, so the kind can no longer disagree
with the text. `messageKind()`, `messageActions()` and the private `showConfirm`/`showErrorPage`/
`clearMessage` are the whole API. `Shell::activate` accepts a `Box` (confirm, `snd_decide`) and
dismisses the other four (their dismiss cue is still UNPROVEN, so they keep `snd_system_ok`);
`Shell::back` cancels a box (no action) and dismisses everything else (`snd_cancel`).

The draw code lives in `src/dialogs.cpp` (new translation unit, added to the CMake target). Its
interface is the `View` members declared in `view.h`; a separate `dialogs.h` would only duplicate them,
so there is none. Every constant there is either a measurement of one of the two reference frames or a
value explicitly marked as a native fit.

### 7.2 Measured native vs firmware

Inline empty state - firmware `frame-00014400.jpg` (D3) vs `capture/inline-page-on/shot.png` (a photo
library page opened on an empty folder, so the page is open and its list has no rows):

| Quantity | Firmware | Native | Delta |
|---|---|---|---|
| ink box | 630..982 x 494..514 | 628..985 x 494..516 | left -2, top 0, right +3, bottom +2 |
| ink height | 21 px | 23 px | +2 |
| font pixel size | ~24..28 (audit 2.3) | 26 | inside the band |
| alignment | level-1 label column | x 628 (measured ink origin) | column-aligned |
| rules / hint / plate / dim | none / none / none / none | none / none / none / none | same |

Titled page - firmware `frame-00020600.jpg` (D4) vs `capture/page3/shot.png` (a Network message page):

| Quantity | Firmware | Native | Delta |
|---|---|---|---|
| header icon ink | 118..154 x 153..188 | 126..147 x 154..189 (36x36 box at 119,154; the glyph inside is narrower) | box y +1 |
| title ink x0 | 169 | 170 | +1 |
| title ink y | 165..184 | 164..183 | -1 |
| rule 1 | y 198, alpha 0.218 | y 198, alpha 0.227 | 0 px, +0.009 |
| body line 1 ink top | 509 | 509 | 0 |
| body line pitch | 543 - 509 = 34 | 32 (constant; the captured body is one line) | -2 (inside the upscale quantum of 1.5 px) |
| rule 2 | y 872, alpha 0.201 | y 872, alpha 0.203 | 0 px, +0.002 |
| hint ink box | 1154..1239 x 888..909 | 1155..1235 x 889..910 | +1 / -4 / +1 / +1 |
| hint glyph | ~20 px ink | 22 px rect (alpha-bounds fitted) | +2 |
| info bar | plate and date absent, clock face only | plate and date absent, clock face at (1776,93) | same |
| frame treatment | none (background luma identical to the frame without a message) | none | same |

Modal box (D1) - spec-only, no firmware frame exists, so these are stated as a native fit rather than
a parity claim: plate 480..1440 wide with the height fitted to its text, two `tex_msgdialog_bar` bars
at the plate's own 960 px width (measured bright rows 250 and 469), title left inside the plate, body
centred at 32 px pitch, and the two hints (`tex_cross` + `msg_ok`, `tex_circle` + `msg_cancel`) from
x 1155 on a row just under the plate (measured ink 490..511). The page's measured 889 baseline belongs
to D4; a box has no measured hint row.

Information page (D5) - no local firmware frame exists (audit 2.5), so only the D4 frame is claimed:
icon 36x36 at (119,154), title from x 169, rules at 198/872, rows at 22 px on a 40 px pitch with keys
right-aligned to 925 and values from 961, hint `tex_circle` + `msg_back` at x 1155, ink y 889..910.

Error-code page (D2) - spec-only. Header, the two rules, `msg_errorcode_cause_label` and
`msg_errorcode_solution_label` (the firmware strings carry the full-width brackets), the cause and the
action lines, `msg_enter`/`msg_back` hints and the 150-unit settle.

Fades, all on the existing `View::PafEase` (mode 5, the XMB's own curve): the box's label group 200
units, its plate 1000, the hint row 100 per button group, the error page's body settle 150. Measured
effect on the box plate pixel at (1350,400): settled (94,82,63), ~300 units after opening (99,87,64),
and still drawn ~300 units after cancel (115,97,67) - the exit keeps the latched content until the
ease reaches zero.

### 7.3 What changed, file by file

- `src/shell.h`, `src/shell.cpp` - `MessageKind`; `showMessage`/`showConfirm`/`showErrorPage`/
  `clearMessage`; every former `message_=` site classified; the information rows rebuilt from
  `explore_plugin_full` strings (`msg_format`, `msg_size`, `msg_created_date_time`,
  `msg_resolution`, `msg_info_detail`, `msg_info_unknown`), with the path row the one stated as native;
  the empty-list sites use `msg_error_no_content`; the removal confirmation is the new two-button box;
  `activate`/`back` own the box's confirm/cancel cues.
- `src/view.h`, `src/view.cpp` - the kind-driven dispatch in `drawScene`, the invented 135-alpha
  full-frame dim deleted, the 0.18 background multipliers deleted, the flat rules, the icon+left-title
  header for the page/info/error surfaces, the measured body metrics, the analog-clock-only frame for
  those surfaces, the five eases and their place in the UI signature, and the latched box content.
- `src/dialogs.cpp` (new, in `CMakeLists.txt`) - the five draw paths, the shared chrome, the hint row,
  the plate fill sampled from `tex_msgdialog_bg`/`tex_msgdialog_bg2` at run time, and the metric table.
- `audit/DIALOGS-FIRMWARE.md` - this section.

### 7.4 Status of the 13 items

| # | Item | Status |
|---|---|---|
| 1 | Split the surfaces into an explicit kind | done (`MessageKind`, five paths) |
| 2 | Draw the inline case the firmware way | done (line at the measured ink origin, no plate/rules/hint/dim) |
| 3 | Remove the invented dim | done (no dim on any kind) |
| 4 | Flat rules, `tex_msgdialog_bar` only for a true box | done (flat 1 px at 198/872; the bar texture is used only by the box, at 960 px) |
| 5 | Move the header to icon + left title | done (119/154 icon, title 169) |
| 6 | Body metrics | done (ink 509, 24 px, 32 px pitch for every line) |
| 7 | Real button row from `page_btnnavi` glyphs + `system_plugin` labels | done (Cross/Circle/Triangle/Rectangle textures; `msg_ok`, `msg_cancel`, `msg_back`, `msg_enter`) |
| 8 | Hint-row fade | done (100 units, per group, `PafEase`) |
| 9 | Box open/close fade | done (200 label / 1000 plate) |
| 10 | Plate from the RCO | done (flat fill sampled from the 12x12 tile, no nine-slice, two bars) |
| 11 | Error-code page | done (layout + real call site); evidence caveat below |
| 12 | Confirm plays `snd_decide` from the dialog's own path | done (box accept); dismiss cues stay unproven and keep `snd_system_ok` |
| 13 | Information rows from firmware strings | done (`Format`, `Size`, `Created`, `Resolution`, `Details`, `Unknown`; the path row marked native) |
| 14 | Two-button dialogs | done (Remove from Continue Watching; body is native wording, buttons are the firmware pair); the PS menu confirm keeps its native wording as item 14 allows |

### 7.5 Verification

- Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-dialogs` - succeeds
  (only pre-existing warnings in `view.cpp`/`main.cpp`).
- A/B gate: `bun work\ab-layers.ts --capture --exe C:/dev/out/native-dialogs/re-ps3-native.exe` -
  **PASS 6/6** (frame 18.30 dB / 0.8323 SSIM, top background 20.25 / 0.9363, category icon row 20.92 /
  0.8990, item label row 11.95 / 0.3927, clock text 11.39 / 0.4105, info bar 14.78 / 0.6117). The root
  frame is unchanged by this pass because no message is up in it.
- Determinism: two `--render-clock 20` captures of the same state are byte-identical (root state
  SHA256 `BC67C6B892065B4C88EEA71B24878F53511452AFEF01EBD1321E8C477920D06D` twice; an earlier pair of
  the page-message state was also identical, `9E80E7FA...` twice).
- Region scores, `bun work\ab-compare.ts <ref> <native> <crop>` (the two frames have different
  wallpapers, so these bound shape and position, not pixels): inline depth-1 8.98 dB / 0.3581
  (560,460,760,120); inline depth-0 8.72 / 0.3568 (560,440,800,140); page header 10.38 / 0.3543
  (90,130,500,70); rule 1 band 12.36 / 0.5072 (0,185,1920,30); body 8.64 / 0.2662 (700,470,550,110);
  rule 2 band 22.14 / 0.8509 (0,860,1920,30); hint 17.26 / 0.6979 (1100,865,300,60); clock 12.17 /
  0.5220 (1300,55,600,110).
- Named state: `bun work\ab-layers.ts --state continue-watching-empty --exe ...` - its assertions still
  pass (depth 0, video, root, row `continue`), but its two pixel regions now FAIL against their stored
  native reference (frame 14.27 dB / 0.7139, dialog 13.56 dB / 0.6785). That is the intended change:
  those references are the old full-frame-dim modal, and the surface is now the inline line. The
  harness was not edited and the references were not re-saved; re-baselining them is the owner's call.
  No other named state draws a message, so no other state is affected.

### 7.6 What remains

- The modal box has no firmware reference: its rect, its title position and its hint baseline are
  native fits (only the plate colours, the two 960 px bars and the hint x/baseline of the page kinds
  are measured). A capture of any "Are you sure?" box on a console would close this.
- Which `tex_notification_*` glyph a normal message box draws is still unproven, so the box draws no
  icon rather than guessing between `_info` and `_caution`.
- The error-code page has a real call site (a registered application whose executable the OS refuses)
  and was rendered and inspected in this pass: header, both rules, `Possible Cause` + the OS error
  string, `Suggested Actions` + this build's own action line, Enter/Back hints. It is **not in the
  shipped capture set**: its recipe needs a vertical move (the app row is not the first row), and this
  machine's controller currently emits a constant up direction that defeats every DOWN (measured: 188
  `vertical(-1)` calls in a few seconds with no script key pressed). The recipe is in 7.7.
- The page surface hides the category band entirely; on the 20.6 s frame the band is off-frame rather
  than absent. That is the page-recession geometry (`childAmount` 262 px), a page-layout difference,
  not a message one. The open-page chevron sits at x 423 natively and ~350 on that frame for the same
  reason.
- The inline message is drawn in the focused row's label slot when no page is open (depth 0), because
  this build does not push an empty page for an empty list; the firmware frame is an open page, whose
  chevron is present.
- The wrapped-line pitch is 32 by construction (`View::wrapMessage` wraps, it does not delegate leading
  to the layout engine); no captured message is longer than one line except the error page above.
- D6-D8 (file delete/copy/publish progress and the busy indicator) remain unimplemented because this
  build has no destructive file operation to show; the PS menu's "Turn Off the System?" keeps its
  native wording (item 14).
- The information page's "reference 92 s" claim is still unverifiable locally (the frame set ends at
  64.6 s); its rows are now firmware-labelled but its geometry remains the D4 convention rather than a
  measurement of that page.

### 7.7 Reproduction

Captures live under `out/native-dialogs/capture/` (git-ignored). Each run needs a fresh copy of
`out/native-reference/store/video-data` as its data directory (the app migrates `preferences.json` to
version 2 on first use, after which the profile-scoped library keys change how a scan is started; the
ab-layers harness seeds a fresh temporary directory per capture for the same reason).

```
build   & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-dialogs
inline  --script "full,pause=4000,LEFT,wait,wait,LEFT,wait,wait,LEFT,wait,wait,LEFT,wait,wait,ENTER,wait,wait,wait,grab=inline.png,quit"
        (photo column, its first row is the media library; the data dir's preferences point library/photo at an empty folder)
page    --script "full,pause=4000,RIGHT,wait,wait,DOWN,wait,wait,ENTER,wait,wait,wait,grab=page.png,quit"
        (Network, an unavailable row; a second run without ENTER gives the no-message pair)
box     --script "full,pause=4000,LEFT,wait,wait,LEFT,wait,wait,ENTER,wait,wait,TRIANGLE,wait,wait,DOWN,wait,wait,DOWN,wait,wait,ENTER,wait,wait,wait,grab=box.png,quit"
        (data dir's preferences carry one media/resume record, id = sha1(path.toUtf8()) first 24 hex)
info    --script "full,pause=4000,TRIANGLE,wait,wait,DOWN,wait,wait,ENTER,wait,wait,wait,grab=info.png,quit"
error   register an app whose executable exists but is not a valid image, then reach its row and press
        Cross; the ErrorCode page reports QProcess::errorString() as the cause.
```

---

## 8. Dated correction - 2026-10-27/28 capture batch

Evidence added after sections 1-7 were written: one RPCS3 session of the real 4.93 XMB at 1920x1080
(`C:\dev\out\rpcs3-ref-2026-10-28\`, `MANIFEST.md`), 135 stills and 9 full-frame bursts. This section
marks each affected line above as resolved or still open. Nothing above is deleted or rewritten; the old
text stays as the 2026-10-27 record. Frame paths are relative to the capture root. Numbers are screen
pixels measured on the named frame; where the batch summary (`MANIFEST.md` section 6) rounds a value, the
frame value is used and the difference is named.

### 8.1 Resolved: the information page (D5) now has a 1080p firmware frame

Old entries this closes:

- section 2.5: "**UNVERIFIED geometry:** the native source cites 'reference 92 s' ... The 92 s frame
  cannot be checked locally."
- section 4.1, row `info page reference claim | "reference 92 s" ... | unverifiable: the local frame set
  ends at 64.6 s`.
- section 5, row `information page ("reference 92 s") | **unverifiable locally**`.
- section 7.2: "Information page (D5) - no local firmware frame exists (audit 2.5)".
- section 7.6, last bullet: "the information page's 'reference 92 s' claim is still unverifiable
  locally".

Frames: `dialogs/information-page-01.png` and `-02.png`, both 1920x1080. Route: XMB root, Game column,
`Corrupted Data` focused, Triangle, Cross on `Information`. Measured on `-01` (rules by full-row luma
profile; rows as ink bands at luma >= 150 over x 700..1300):

| element | firmware, measured | frame |
|---|---|---|
| frame treatment | no dim; the info bar is replaced by the two rules; the category band is off-screen | `information-page-01.png` |
| rule 1 | y = 199, 1 px, full width (mean luma 176.8 over x 200..1600) | `information-page-01.png` |
| rule 2 | y = 879, 1 px, full width (mean luma 182.0) | `information-page-01.png` |
| icon plate | x 665..985, y ~263..442 | `information-page-01.png` |
| rows | `Title`, `Parental Control`, `Created`, `Size`; ink tops y 499/539/579/619 -> pitch 40 px | `information-page-01.png` |
| label column | right-aligned; last ink x 802-803 | `information-page-01.png` |
| value column | left-aligned; first ink x 844-853 | `information-page-01.png` |
| back hint | `O Back` glyph + label, ink x 1155..1240, y 897..916 | `information-page-01.png` |

That is the D4 convention, not a D1 box: two full-width 1 px rules, the rows between them, one hint
under the lower rule, no dialog plate, no dim. The `Created` value in this frame is `22/10/2026 2:14`,
`Size` is `0 KB`, and `Title` / `Parental Control` are `-`, i.e. `msg_info_none` - the firmware's own
none/unknown behaviour that section 4.2 item 13 asks the native to copy with `msg_info_none` /
`msg_info_unknown`.

Deltas this gives the native info page (`View::drawMessageInfoPage`, described in 7.2):

| quantity | native | firmware | delta |
|---|---|---|---|
| rule 1 | y 199 | y 199 | 0 |
| rule 2 | y 866 | y 879 | **13 px high** |
| row pitch | 40 px | 40 px | 0 |
| label right edge | x 925 | x 803 | **122 px right** |
| value left edge | x 961 | x 844 | **117 px right** |
| back hint | x 1155, ink 889..910 | ink 1155..1240, y 897..916 | x 0, **7 px high** |

Two caveats. The manifest gives the rules as "y~190 and y~915"; a direct scan of the frame it cites puts
them at 199 and 879, and the older D4 frame measured 197/877, so 199/879 is used here (915 is the `Back`
hint row, ink 897..916). And the frame proves geometry only: whether the rules are an alpha line or a 1 px
texture is still spec-only.

### 8.2 Batch items and what they do to this audit

| batch item | capture | effect here |
|---|---|---|
| DIALOGS #1 options open + Down x4 | `dialogs/options-open-01.png`, `options-down-01.png` ... `-04.png` | not a D1-D9 dialog: it is the Triangle `expage_menu` overlay. It closes no row of section 5 and adds no dialog geometry; its numbers belong to `ITEM-MENU-INFO-FIRMWARE.md` A6/A7 |
| DIALOGS #2 open/close bursts at 54.1 / 54.8 fps | `dialogs/options-open-burst2/` (108 frames, 1.977 s), `options-close-burst2/` (110 frames, 1.990 s) | the same menu overlay, not a message box: the band fades (mid-alpha at `options-open-burst2/f_0000692.png`, ~190 ms after the key; still partial at `options-close-burst2/f_0000610.png`, ~110 ms after the key) over a stationary XMB. It does **not** close the "button-hint fade" or "box open/close fade" rows of section 5 - no frame shows a hint row or a box - and it does not touch D6-D8 |
| DIALOGS #3 Cross on `Information` | `dialogs/information-page-01.png`, `-02.png` | closes the information-page entries: 8.1 |
| DIALOGS #4 signed-in indicators | none | not a dialog surface (the five slots are `ITEM-MENU-INFO-FIRMWARE.md` B5/B7) and not captured: the console is offline with no PSN account (MANIFEST section 7 no. 1). `dialogs/root-clock-mmdd-12h.png` records the unsigned-in bar (plate + clock only) |
| DIALOGS #5 busy indicator / copy crowding | none | not captured: the VFS holds no copyable object (MANIFEST section 7 no. 2) |
| DIALOGS #6 `MM/DD/YYYY` + 12-hour clock | `dialogs/root-clock-mmdd-12h.png`, `-2.png` | adds no dialog row; it closes the info-bar plate question: the plate is a fixed 591x58 (1329..1919 x 75..132), text right edge 1760, in both date formats. The D4 frame's convention (plate and date text hidden, analog clock kept) is unchanged |

### 8.3 Still spec-only after the batch

Every row of section 5 that this batch did **not** close, restated so it stays visible:

| Surface | Status after 2026-10-27/28 | What would prove it |
|---|---|---|
| two-button confirm box (Cross/Circle, Yes/No) | spec-only: RCO + TRACED, no frame | a capture of any "Are you sure?" box |
| the box's plate rectangle at 1080p | spec-only | the same capture plus the box's plane vertices |
| error-code page | spec-only | a failing network sign-in (needs PSN, unavailable) |
| dim behind a modal | spec-only; the info page is non-modal and shows none | the two-button capture, sampling background luma |
| button-hint fade (100 units) | spec-only | a burst of a dialog's hint row appearing |
| box open/close fade (200 / 1000 units) | spec-only | the same burst |
| PS menu "Turn Off the System?" | native-only, no reference | a PS-hold capture on a console |
| D6 delete / D7 copy / D8 busy | no frame | an object to delete or copy (empty VFS) |
| which `tex_notification_*` glyph a box draws | still unproven | a normal message box |
| what the info page's rules are made of | geometry only | a decode or a pixel sample at several backgrounds |

No source file was edited for this section; no RCO or capture is committed. The frames stay under
`C:\dev\out\rpcs3-ref-2026-10-28\` (git-ignored).

---

## 9. Dated correction - 2026-10-28 capture batch 2: the error surface is real, and it has no code

Evidence added after section 8 was written: a second targeted RPCS3 batch on the same 4.93 XMB, one session
in two boots, 1920x1080 (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md`): 251 stills, 22,149 burst
frames in 19 bursts, crop bursts at 131-235 fps, **clock scale 100 %**. Nothing above is deleted. Frame
paths are relative to the capture root. Where the batch summary rounds a value, the frame value is used and
the difference is named; numbers marked "(re-measured)" were checked against the cited still for this
section.

Section 5's row "error-code page | spec-only | one capture of a failing network sign-in" was the wish this
batch was built around. The batch reached **one real firmware error page** - and it is not an error-code
page. Both halves of that sentence matter, and 9.3 states the limit plainly.

### 9.1 The one reachable firmware error page, measured

Route: Settings column, row 16 `Network Settings`, Cross, row 4 `Internet Connection Test`, Cross, wait out
the timeout. Frame `error/page-at-rest.png` (the settled state; the failure itself is the burst in 9.2).

| Quantity | Value | Note |
|---|---|---|
| Title-bar rule | **y = 199**, 1 px | mean luma 179 over x 200-1700 |
| Lower rule | **y = 879**, 1 px | mean luma 182.7 - **the D4/D5 rule pair**, not the single rule 8.1 could check on the info page |
| Title icon | globe, x about 100-140, y about 160-190 | 2.2's `D2` icon slot |
| Title ink | **x 145-446, y 167-185** | `Internet Connection Test`, 19 px cap height |
| Heading ghost | a dimmed repeat of the title at y about **490-523**, **max luma 43** (about 25 % alpha, below the 45 luma ink floor) | this is what is left of the heading: it is not a body line and not a plate |
| Message ink | **x 753-1163, y 530-546** (re-measured 753-1164 / 529-547) | `An Ethernet cable is not connected.`, one line |
| Back hint | `O Back`, ink **x 1155-1239, y 897-916** | the same rect as the information page and D4 |
| Focusable controls | **none** - Down and Up change nothing (mean frame delta 0.82 / 1.31 luma = the wave) | `error/page-after-down.png`, `error/page-after-up.png` |
| Dim behind | **none**; the XMB is not visible and the category band is off-frame | the page is not modal over a live screen |
| Info bar | **present but unfilled**: 1 px plate border at x 1329, clock ink x 1592-1799 y 88-119, no plate fill | see 9.4 |
| Numeric code | **none**: no `800xxxxx`, no code field, no `Enter` hint, no suggested-actions body | 9.3 |

In the D1-D9 taxonomy this is **D4** (titled status page) with a dimmed heading instead of a body: two
full-width rules, a title above the upper rule, one line of centred text between them, one hint under the
lower rule, no plate, no dim, no buttons. It is *not* D2.

Dismiss: Circle returns to the `Network Settings` page with the focus on the `Internet Connection Test` row
(`error/ictest2-dismiss-01..03.png`, `error/page-dismissed.png`, `error/ictest5-row.png`). The page has no
control of its own - Back is the only verb, which is what a D4 page with no button row should do.

### 9.2 The failure moment, at 234.9 fps

Burst `error/ictest5-fail/` (10,569 frames, **234.9 fps**, 1:1 crop `620,450,740,170`, Cross at about
800 ms) - the highest frame rate in either batch, and the only timestamped record of a firmware error
appearing.

| Phase | Time after Cross |
|---|---|
| `Please wait...` (spinner + `O Back`) | 0.8 s -> 11.9 s |
| `Please wait...` text fades out | 11.90 -> 12.05 s (about **150 ms**) |
| Message and heading fade in | 12.05 -> 12.80 s; white text full at **12.283 s**; plate at rest by 12.8 s |
| Whole transition | about **750-900 ms**, cross-fade style |

**The timeout is not deterministic.** Three runs of the same test failed at about **12 s, 15 s and 26 s**
after the Cross (`error/ictest5-fail/`, `error/ictest2-burst/` 868 f at 57.9 fps, `error/ictest3-burst/`
1,594 f at 63.7 fps). So the *offset* is unpredictable and only the *shape* is stable: a 150 ms text
fade-out followed by a ~250 ms message fade-in, end to end 750-900 ms. A native implementation that models
a fixed error delay would be modelling the rig.

### 9.3 The limit: a numeric error code could not be produced on this console

**Stated explicitly, because section 5's error-code row is the one this batch set out to close and did
not.** Four routes were tried; none produces a `800xxxxx` page, and the reasons are specific:

| Route | What it produced | Why it is not a code page |
|---|---|---|
| Game column, `Corrupted Data`, Cross | **nothing at all** - no dialog, no page, no code; clock advances, XMB stays live (`error/launch-burst/` 387 f, `error/corrupt-launch-burst/` 740 f, three 20 s grabs) | `dev_hdd0/game/TEST12345` has no `PARAM.SFO`; the firmware treats the item as dead and Start is a no-op. There is no failure to report |
| `PlayStation(R)Store` launch | the `Internet Connection Settings` message page (title, rule 199, two-line body, `OK`, `X Enter` / `O Back`), **no code** (`error/icp-page-rest.png`) | configuration advice, not a classified failure |
| `Settings > System Update > Update via Internet` | **vsh died**: RPCS3 fatal, `ScePafJob`, `fs::windows_file::write ... Access is denied (0x5)` (`sys_fs_write`); console frozen on `Checking... Please wait.` with the host overlay `The PS3 application has likely crashed...` (`error/update2-launch-burst/`, `error/update2-settle-01..04.png`) | an **emulator** fault, not firmware. The overlay is RPCS3 UI and cannot become a firmware page |
| `Settings > Network Settings > Internet Connection Test` | **the D4 error page of 9.1** | the firmware classifies this as a cable condition, not an error code: string only |

So: **D2 (section 2.2) stays spec-only.** It remains a traced structure -
`mbox_try_button`, `mbox_detail_scroll`, the `msg_errorcode_*` strings, the Enter/Back hint pair, the
150-unit settle - with no photograph. What the batch adds is a measured *sibling* from the same dialog
family, and the distinction is now concrete:

| | D2, error-code page (spec-only) | the measured error page (9.1) |
|---|---|---|
| hints | **`Enter` and `Back` labels** (2.2's traced getters) | **`O Back` only**, glyph + label |
| body | a scrollable suggested-actions body over `tex_default_scroll_*`, centred by -0.5 of its height | one centred line, no scroll object |
| code | the number, plus `msg_errorcode_if_no_solution_ask_ic_with_errorcode` ("write down error code %1 ...") | **none** |
| heading | the code/title as real text | a dim ghost below luma 45 |
| rules | two full-width rules (2.2) | two full-width rules at 199 / 879 |

A reimplementation that draws D2 for a cable-unplugged condition is drawing the wrong dialog: the firmware
draws D4 with a ghost heading and no code, and reserves D2 for failures it classifies.

### 9.4 Two additions to the D4/D5 convention

- **The rule pair is 199 / 879 on this page too** (re-measured mean luma 179.0 and 182.7). Section 2.4
  measured the pair on a `Search for Media Servers` frame as 197 / 877 and 8.1's info page as 199 / 879;
  the error page agrees with the 199 / 879 pair. The depth-2 status-list page uses the same pair
  (`SETTINGS-FIRMWARE.md` 12.2), so a two-rule page is the family's shape, not one page's.
- **The info bar is present here, unfilled.** Section 2.4 recorded for the D4 frame: "plate and date text
  absent; only the analog clock face remains". On the error page the bar is drawn as a **1 px outline at
  x 1329 with the clock string ink x 1592-1799, y 88-119 and no fill** (luma 0 at x 1700 on y 100). The
  batch summary's "the info bar is replaced by the rule" is therefore not what the frame shows; the frame
  value is used here. This is a **third variant** of the same bar (filled / absent / outline-only) and it
  means "the info bar" is not one state - worth knowing before a native dialog unconditionally hides it.

### 9.5 Status of section 5's and 8.3's open rows after batch 2

| Surface | Status after 2026-10-28 batch 2 | What would still prove it |
|---|---|---|
| two-button confirm box (Cross/Circle, Yes/No) | **spec-only, unchanged** - no reachable path produces one | a capture of any "Are you sure?" box |
| the box's plate rectangle at 1080p | **spec-only, unchanged** | the same capture plus the box's plane vertices |
| error-code page (D2) | **spec-only, still** - 9.3 | a failure the firmware itself classifies: a rejected PKG, a disc error, a failing PSN sign-in (needs PSN) |
| **a firmware error page of any kind** | **CLOSED** by 9.1 - D4-class, string-only | n/a |
| the error page's failure timing | **CLOSED** by 9.2 - 150 ms out, ~250 ms in, 750-900 ms total, variable offset | n/a |
| dim behind a modal | **spec-only**, and this batch adds a third non-modal surface with no dim | the two-button capture |
| button-hint fade (100 units) | **spec-only, unchanged** - no frame shows a hint row appearing | a burst of a dialog's hint row |
| box open/close fade (200 / 1000 units) | **spec-only, unchanged** - the measured fade is the *error page's*, not a box's | the same burst |
| D6 delete / D7 copy / D8 busy | **no frame, unchanged** (empty VFS) | an object to delete or copy |
| which `tex_notification_*` glyph a box draws | unchanged | a normal message box |
| what the info page's rules are made of | unchanged (geometry only) | a decode or pixel samples at several backgrounds |

No source file was edited for this section. The frames stay under `C:\dev\out\rpcs3-ref2-2026-10-28\`
(git-ignored).

## 10. Dated correction - 2026-10-28: the two-button confirm box has a frame

Section 5's first two rows ("two-button confirm box", "the box's plate rectangle at 1080p") and 9.5's
"spec-only, unchanged" rows are closed here. Batch `rpcs3-gaps-2026-10-28` reached a real confirm box and
imported it as `out/parity-references/dialog__original__day__1080p.png` (sha256 `ea7d5a98...0add`, the same
bytes as `C:\dev\out\rpcs3-gaps-2026-10-28\stills\dialog__original__day__1080p.png`). The RCO and TRACED
facts in 2.1 still stand; where the frame disagrees with a *measured* claim this section states the
disagreement rather than reconciling it by guess.

### 10.1 The frame

Route: Game column > `Corrupted Data` > Triangle > Down (Start -> Delete) > Cross, then Circle - cancelled,
never confirmed. One settled still, 1920x1080, no crop, no rescale. The surface replaces the info bar, so
its own frame carries no clock (the console plate read `10/28 10:03 AM` on the frames either side).

What it shows: one light plate at the left carrying the item's icon; the body as two left-aligned lines to
its right; `Yes` / `No` under the body with `No` focused (soft white halo); a full-width footer rule at
y 879 with `X Enter` and `O Back` under it. **No dim** anywhere in the frame, no heading, no
`tex_msgdialog_bar` row, no page rule at y 197, no info bar and no clock.

### 10.2 Measured geometry

Every value below is a single-pixel read of the still. "Step" means the luma on the two pixels either side
of an edge; edges are antialiased over 1-2 px, so rects are quoted to the nearest pixel.

| Quantity | Measured | How it was read |
|---|---|---|
| plate rect | x 480..798, y 452..627 (**319 x 176**) | steps at x 480 (87.7 -> 101.3) and x 798 (96.2 -> 81.4); y 452 (82.8 -> 93.4) and y 627 (80.5 -> 69.0) |
| plate fill | **white at 43/255 (0.17)**, uniform | over bg RGB(100,76,43) the delta is (+24,+28,+35); over bg RGB(85,61,28) it is (+29,+33,+39) - both fit `(255-bg)*0.17` in every channel |
| icon on the plate | item glyph, ink **x 612..667, y 512..567 (56 x 56)**, centre (639.5, 539.5) = the plate's own centre | thresholded ink box |
| body line 1 | ink x 1074..1468, **y 476..498** (395 x 23) | ink above local background |
| body line 2 | ink x 1074..1484, **y 509..532** (411 x 24) | same |
| body pitch | **34 px**, left-aligned at x 1074 | the two ink tops |
| body wrap bound | 411..440 px (line 2 fits at 411; `" Are"` after `deleted.` would exceed the column) | line widths |
| `Yes` | ink x 1183..1221, **y 584..601** (39 x 18) | ink box |
| `No` | core ink (>240) x 1346..1374 (29 wide), ink with halo x 1341..1376, **y 582..602** | ink box at two thresholds |
| answer gap / centre | **120 px between the two ink boxes**; pair centre **1279.5** = the body ink block's centre (1074..1484 -> 1279) | the two ink boxes |
| focus halo | reaches 8 px left of the `No` ink and 3 px above/below (x 1336, y 579..605); peak 255; `Yes` has none | luma ring around each label |
| footer rule | **y 879, 1 px, white at 177/255 (0.693)**, flat: 189 at x 20, 196 at x 960, 192 at x 1900 | RGB 200.6/192.6/183.5 over bg 77.9/51.6/21.9 |
| footer hints | `X` glyph ink 726..743, label `Enter` 762..814; `O` glyph 1155..1174, label `Back` 1192..1240; ink **y 897..916** | ink boxes |
| info bar / clock | **absent** - max luma 81.9 over x 1250..1920 y 60..140, 0 px above 150 | whole band |
| dim | **none**. Against `frames/a31-dialog-cancelled.png` (same boot, after Circle) the background bands move by -0.41..+0.89 luma with both signs, i.e. the wave advanced; a veil would be a same-signed drop of tens of levels | five bands compared |

### 10.3 Where the frame contradicts the earlier sections

1. **Plate colour and blend (2.1, 7.2).** 2.1 read `tex_msgdialog_bg` as a normal-blended neutral grey
   (RGB 54,56,54 at 116/255, "a neutral grey wash") and 7.2 rendered that. The frame's plate is *lighter*
   than the background, not darker, and the per-channel fit above is a 0.17 white wash on both sides of the
   plate. Two readings survive: the visible plane is a run-time light plane (2.1 already records that every
   dialog plane is created in code), or the RCO tile is composited with a lightening blend. One still
   cannot separate them, so the box now draws the measured wash and this document no longer claims the
   tile's colour is what the screen shows.
2. **The two bars (2.1, 7.2).** The frame has no bar row: the plate's top and bottom edges are the wash's
   own antialiased edge (1-2 px, +11..+37 luma), not a 960 px centre-bright line. `tex_msgdialog_bar`'s
   assignment to exactly two objects (TRACED 0x00343FFC) stays a fact about the dialog's object set, but it
   is not visible on this surface. The one full-width bright line in the frame is the footer rule at y 879
   and it is flat (189/196/192 at x 20/960/1900), so it is not that texture either.
3. **The button row (2.1, 7.4 item 7).** The answers are the `msg_yes` / `msg_no` strings as *text*, not
   `page_btnnavi` glyph+label pairs. The glyph pairs do appear in the frame - in the footer, as
   `X`+`msg_enter` and `O`+`msg_back`.
4. **The heading (7.2).** A box has no heading line. `Shell::showConfirm(title, body)` keeps its shape, but
   the box draws only the body; the title is stripped and not drawn.
5. **The box rect (5, 7.6).** It was the pass's one explicit native fit ("no local frame shows a modal
   box"). It is now a measurement.
6. **Which icon a box draws (2.1, 9.5).** Resolved for this box: the plate carries the *item's own* icon at
   56x56, centred. 2.1's open question was which of the 100x100 `tex_notification_*` glyphs a message box
   picks; the answer here is that this box carries no notification glyph at all.
7. **Whose plane the plate is.** The frame cannot say whether the 319x176 plane is the D1 box's own plate
   or the host delete page's icon plane (2.6: `expage_fdelete_dialog` holds `filedel_dialog_icon`,
   `filedel_dialog_info` and `filedel_dialog_msgdialog`, the plane that hosts the D1 box as a child; all of
   their stored pos/size are 0). The native has no delete page, so this build draws the plate as part of
   the box. A second capture of a box on a page with no icon plane would separate the two.

### 10.4 What changed

`src/dialogs.cpp` only - the metric block for the box and `View::drawMessageBox`:

- the plate is the measured rect and the measured white wash (no nine-slice: one uniform fill), with the
  focused row's own icon at 56x56 centred on it, from the same source the page header uses;
- the heading is not drawn; the body is left-aligned at x 1074 on the measured 34 px pitch, white;
- `Yes` / `No` are `msg_yes` / `msg_no`, 120 px apart, the pair centred on the text column, the focused
  answer carrying the XMB's halo (`No`, the console's default answer);
- the footer band is the measured flat 1 px rule plus `X Enter` / `O Back` at the measured anchors, fading
  with the hint group;
- the two `tex_msgdialog_bar` rows and the `msg_ok` / `msg_cancel` hint row are gone from this surface.

`src/view.h` was **not** edited (this pass's fence allows it only for a new member, and the box needs none).
Its one-line comment for `drawMessageBox` still reads "plate, two bars, two hints"; that comment is now
stale and should be corrected by the pass that owns the header.

### 10.5 Route

**Reachable.** Video column > `Continue Watching` (with one remembered position) > Triangle > Down twice
(the panel's Start -> Delete order puts `Remove from Continue Watching` there) > Cross. Evidence: the
capture's sidecar (`pageKind continueWatching`, depth 1, row `continue:<sha1>`) and the capture itself,
`out/native-dialog/after/box.png`. That is a real destructive-in-app action (it forgets a resume point), so
the box is reached through the same kind of command the console's Delete row raises.

**Not routeable, and why.** The console's own route is a *file delete* on a game item. This build has no
file-deletion backend at all (by instruction, none was added in this pass) and no `Corrupted Data` row,
because there is no corrupt game item on this host. So the console's body text and its icon cannot be
reproduced by routing: they differ by construction, and the region numbers below measure shape and
position, not content.

**Also not routeable in this slice: the catalogue's `dialog` state.** Its route is
`column video > catalogue:continue` over the empty seed, and its Tier A wants `continue watching: nothing to
continue` at depth 0 - that is the *inline empty-state line* (`drawInlineMessage`), not the box. So
`work/parity.ts --capture --score --state dialog` cannot see this pass's surface, and its number is
unchanged (10.6). Pointing the catalogue's `dialog` route at the box is a catalogue edit
(`work/parity-catalogue.json`: route stages, dataSeed, `expect`, `trace`), which this pass's fence forbids.

**Behavioural residual.** The console's box is a two-choice selector: `No` is focused and `X Enter`
activates the focused answer while `O Back` leaves. `Shell::activate` still accepts a Box unconditionally on
Cross and `Shell::back` cancels, because the shell has no choice index. The drawn focus is therefore the
console's default answer, not a live selection - pressing Cross performs the box's action whatever the glow
says. Closing that needs a choice field in `Shell`, left/right handling and a per-choice accept: one
`shell.cpp` change, outside this pass's fence, and the next thing to do on this surface.

### 10.6 Verification

Build: `build.ps1 -OutputDirectory C:\dev\out\native-dialog` (succeeded; only pre-existing warnings).
Pins: `--render-clock 30 --ui-clock 2026-10-25T12:32:00 --focus-time 2000` (the parity defaults), a fresh
seed copied from `out/native-dialogs/capture/box/data` on every run.

`ab-compare.ts`, reference vs capture, before this pass and after it. The two frames have different
backgrounds, so these bound shape and position, not pixels:

| crop | before | after | delta |
|---|---|---|---|
| full frame `0,0,1920,1080` | 18.45 dB / 0.8316 | **19.77 dB / 0.8937** | +1.32 / +0.0621 |
| `dialog` `240,190,1440,700` | 16.36 dB / 0.7104 | **18.12 dB / 0.8420** | +1.76 / +0.1316 |

The `dialog` crop ends at y 890, so it holds the plate, the body, the answers and the footer rule, but not
the hint labels at y 897; the footer hints are unscored by it. Run-to-run spread is +-0.01 dB.

Native geometry against the frame, measured by differencing the box against the identical frame with the
box dismissed (the wave cancels):

| Quantity | Frame | Native now |
|---|---|---|
| body line 1 ink | y 476..498 | y 476..499 |
| body line 2 ink | y 509..532 | y 510..533 |
| `Yes` ink | x 1183..1221, y 584..601 | x 1192..1228, y 584..600 |
| `No` ink | x 1341..1376 (core 1346..1374) | x 1352..1379 |
| footer rule | y 879, delta +138 luma | y 879, delta +141.7 luma |
| footer hints | 726..743 / 762..814 and 1155..1174 / 1192..1240, y 897..916 | 726..747 / 764..816 and 1155..1176 / 1194..1241, y 897..918 |

Determinism: two captures of the settled box differ by at most 1 level per channel (PSNR 56.88 dB / SSIM
0.9992) - rasterisation noise, no layout difference.

Parity, as asked: `bun work/parity.ts --capture --score --state dialog --exe
C:/dev/out/native-dialog/re-ps3-native.exe` -> `run-20261028-081714`, `scorecard-20261028-081743`. Tier A
PASS, Tier C PASS, `frame` 18.08 dB / 0.8384 FAIL and `dialog` 16.42 dB / 0.7576 FAIL (budget 18.00/0.880).
Both numbers are identical to section 16.4 because the state captures the inline line, not the box (10.5):
this is the state's first honest number and it measures a surface this pass did not change.

Canonical gate: `bun work/ab-layers.ts --capture --exe C:/dev/out/native-dialog/re-ps3-native.exe` -
**PASS 6/6**, text presence 1677 bright pixels in the clock crop (minimum 900) PASS. Frame 18.48 / 0.8327,
top background 20.25 / 0.9363, category icon row 21.20 / 0.9029, item label row 11.92 / 0.3891, clock text
11.39 / 0.4500, info bar 14.98 / 0.7639 - every layer inside budget.

Named states: none draws a Box (the box is raised only by `showConfirm`, whose single call site is the
RemoveResume command; the two Continue Watching states capture the inline line and the populated page), so a
change confined to `drawMessageBox` cannot move them. They nevertheless fail the 55 dB / 0.9995
self-baseline they are gated at: `continue-watching-empty` 32.47 / 0.9903 frame and 29.34 / 0.9805 dialog,
`continue-watching` 33.00 / 0.9934 frame and 22.06 / 0.9150 row. That predates this pass - 7.5 recorded the
same two states failing against the then-current reference - and no reference was edited; re-baselining is
the owner's call.

### 10.7 What remains open

- Whose plane the plate is (10.3.7), and what a box with no icon plane looks like.
- The native's icon is the row's own glyph fitted to its alpha bounds inside the 56 px box, so a wide glyph
  draws shorter than the frame's square one (the video clip measures ~55 x 40; the frame's corrupted-data
  glyph fills 56 x 56).
- The box's open/close fades stay TRACED only (200 / 1000 units): no 200+ fps burst of this box exists.
  The same is true of the hint row's 100-unit fade.
- The text column is *bounded* to 411..440 px by the frame's own wrap, not measured. 424 is inside the
  bound and also sets the answer row's centre, which is why the drawn pair sits ~6 px right of the frame's
  (1285.5 against 1279.5).
- Whether the answer row belongs to the box or to its host page's `cmdbtn_*` buttons (2.6) is not decidable
  from one frame.
- `view.h`'s stale `drawMessageBox` comment (10.4).
- The catalogue's `dialog` route still captures the inline line (10.5).

### 10.8 Reproduction

```
build    & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-dialog
seed     copy out/native-dialogs/capture/box/data/{preferences.json,profiles.json,settings.ini}
         (preferences carry one media/resume record for the video library's Top Level Clip.mp4)
script   full,pause=4000,LEFT,wait,wait,LEFT,wait,wait,ENTER,wait,wait,TRIANGLE,wait,wait,
         DOWN,wait,wait,DOWN,wait,wait,ENTER,wait,wait,wait,grab=<png>,quit
pins     --render-clock 30 --ui-clock 2026-10-25T12:32:00 --focus-time 2000
score    bun work/ab-compare.ts out/parity-references/dialog__original__day__1080p.png <png> 240,190,1440,700
```

The frame measurements were taken with numpy/PIL scripts kept under `out/native-dialog/` (git-ignored):
`measure2.py`, `edges.py`, `elements.py`, `elements2.py`, `elements3.py`, `bars.py`, `blend.py`,
`footer-check.py`, and on the native side `diff-after.py` and `measure-after2.py`.

## 11. Dated correction - 2026-10-28: the box is a two-choice selector

Section 10.5's behavioural residual is closed: `Shell::activate` no longer accepts a Box unconditionally on
Cross. The captured frame's `No` focus is now a live selection the user can move and confirm, and a box whose
call site asks no question keeps the single-action path it had.

### 11.1 What the frame fixes about the order

The still's two answer ink boxes (10.2) put `Yes` on the left (ink x 1183..1221) and `No` on the right (ink
x 1341..1376, carrying the halo). The console focuses `No`, so the *drawn* order is Yes, No and the default is
the second, right-hand answer. The footer's `X Enter` / `O Back` are the verbs: Enter confirms whichever
answer is focused; Back leaves without answering.

No local burst shows this box's D-pad, so the one thing the frame cannot settle is whether the focus clamps at
the pair's ends or wraps. This pass wraps: on a two-answer row a step in either direction lands on the other
answer. That keeps the spatial move the frame implies (No is the right answer, so LEFT from the default
reaches Yes) and also lets RIGHT from the default reach Yes. A clamp would make one of the two directions a
no-op on a two-item row; the wrap is stated here as a native decision, not a firmware claim, and it is the
one navigational value in this section that is not measured.

### 11.2 The choice model

- `Shell::showConfirm` raises a Box with an answer row (`messageBoxChoices_ = true`) and starts it on
  `BoxChoice::No` (0); `BoxChoice::Yes` is 1. That is the model's order, not screen space. The getters expose
  it: `boxChoice()` returns 0 (No), 1 (Yes) or -1 (no answer row), and `boxChoiceSlot()` maps it to the drawn
  pair: 0 = Yes (the left label), 1 = No (the right one), -1 = no answer row.
- Left/Right, keyboard and pad, both already reach `Shell::horizontal`: a step moves the answer and plays
  `Cue::Move` (`snd_cursor`, the XMB's own selection cue). `XMB_DIALOG_LOG=1` reports it as
  `dialog: box move choice=yes|no`.
- Cross/Enter confirms the focused answer with `Cue::Confirm` (`snd_decide`). Yes runs the box's action and
  closes; No closes with the action dropped. The box closes on either answer.
- Circle/Esc keeps `Shell::back`'s cancel path: `Cue::Back` (`snd_cancel`), the action is dropped whatever the
  focus was, and the box closes.
- A Box raised with `showMessage(text, MessageKind::Box)` has no answer row: both getters return -1,
  horizontal presses are ignored, and Cross keeps the old single-action path (dismiss, and run the message's
  own action if it has one). No call site changes behaviour: the tree's only Box call site is `showConfirm`
  (Continue Watching's Remove), so the two-choice path is the one the route exercises.
- `showMessage`, `showConfirm` and `clearMessage` all reset the row and the index, so a box never inherits a
  previous one's focus.

### 11.3 What changed

`src/shell.h` (this pass):

- `enum class BoxChoice { No = 0, Yes = 1 };` next to `MessageKind`, with the model order stated;
- public `int boxChoice()` and `int boxChoiceSlot()` (0/1/-1 each) beside `messageActions()`;
- private `messageBoxChoices_`, `messageBoxChoice_` and `dialogLog_` beside `messageAccept_`.

`src/shell.cpp` (this pass):

- `showConfirm` turns the answer row on and starts it on No; `showMessage`/`clearMessage` reset it;
- `horizontal()` moves the answer when a Box with answers is up, with `snd_cursor`, and leaves every other
  horizontal press exactly as before;
- `activate()` commits only on Yes (`log: box enter choice=yes commit=1`) and dismisses on No without running
  the action (`choice=no commit=0`); a Box with no answers logs `choice=none commit=1` and behaves as before;
- `back()` logs the focus it cancels and keeps the existing `snd_cancel` path;
- `boxChoice()`/`boxChoiceSlot()` definitions; `XMB_DIALOG_LOG=1` traces raised/moved/answered/cancelled.

`src/dialogs.cpp` - one hunk, RETURNED, not applied by this pass (the draw files are held by the view pass;
`view.cpp` is untouched as instructed). Without it the halo stays pinned to `No` by `kBoxFocusChoice` no
matter what the shell selects. The hunk is:

```diff
@@ the box's metric block @@
 constexpr double kBoxChoiceTop=579.;
 constexpr int kBoxChoiceSize=22;
-// The captured box focuses No: the console's default answer, and the non-destructive one. The native's box
-// has one accept path (Shell::activate, see the comment in drawMessageBox), so this is the drawn default.
-constexpr int kBoxFocusChoice=1;
+// The captured box focuses No: the console's default answer and the non-destructive one. That is where a
+// box starts, not a constant: the live focus comes from Shell::boxChoiceSlot() (0 = Yes, 1 = No).
 // Footer: the captured box keeps a footer band under the box - one flat full-width 1 px rule at y 879

@@ View::drawMessageBox, the answer row @@
     // Choices: the firmware's own msg_yes / msg_no as text, not glyph hints, 120 px apart and centred on
     // the body column. The focused one carries the XMB's soft white halo; the other does not (measured:
     // Yes has no halo, No's halo adds ~3 px above and below its ink).
-    // NOTE: Shell::activate still accepts on Cross whatever this draws, so the glow currently marks the
-    // console's default answer rather than a live selection - the shell needs a choice index for that
-    // (stated as the residual in audit/DIALOGS-FIRMWARE.md).
+    // The focus is Shell's live choice, not the console's default: boxChoiceSlot() is 0 for Yes (the frame's
+    // left answer), 1 for No (its right one) and -1 when the box has no answer row at all, so a
+    // single-action Box draws no halo.

@@ the halo test @@
+    const int focused=shell_.boxChoiceSlot();
     for(int i=0;i<2;++i) {
         const QRectF rect(centres[i]-150.,kBoxChoiceTop,300.,30.);
-        if(i==kBoxFocusChoice) {
+        if(i==focused) {
```

`src/view.h` needs no change for this hunk (the getter lives on `Shell`); its stale `drawMessageBox` comment
("plate, two bars, two hints") is still the view pass's, and is still wrong (10.4).

### 11.4 Verification

Build: `build.ps1 -OutputDirectory C:\dev\out\native-boxchoice` succeeded. The binary was built with the
`dialogs.cpp` hunk above applied so the two focus states could be captured; the delivered tree's
`dialogs.cpp` is restored to its pre-pass bytes (sha256 `EAD077FA...9B5A`), and applying the hunk and
rebuilding reproduces the frames.

Seed: `C:\dev\out\native-dialogs\capture\box\data` (one `media/resume` record for the video library's
`Top Level Clip.mp4`), copied fresh into each run's data directory. Pins:
`--render-clock 20 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000`.
Route, after `full,pause=4000`:
`LEFT,wait,wait,LEFT,wait,wait,ENTER,wait,wait,TRIANGLE,wait,wait,DOWN,wait,wait,DOWN,wait,wait,ENTER` then
six waits so the box's 1000-unit plate fade settles. `XMB_DIALOG_LOG=1` names every box event on stderr.

| proof | keys after the box is up | log (native.log) | state after | evidence |
|---|---|---|---|---|
| (a) default focus is No | none, `grab` | `dialog: box show choices=yes choice=no` | sidecar `pageKind=continueWatching`, depth 1, `entryCount=1`, `entryId=continue:117b37edc2b486d39670e7d8`; `preferences.json` still holds the record | `box-default.png`, halo on No, sha256 `160E7A90...BF166` |
| (b) RIGHT moves to Yes, ENTER commits | `RIGHT,wait,wait,grab`, `ENTER,wait,wait,wait,grab` | `box move choice=yes`; `box enter choice=yes commit=1`; `continue: removed Top Level Clip ... 0 still remembered`; `the list is empty; the page closed` | `pageKind=root`, depth 0, `entryCount=4`; `preferences.json` `media/resume` = `{}` | `box-yes.png` (halo on Yes, `FD9187D7...C7C7F`); `page-after-yes.png` with the inline `There are no content items.` |
| (c) LEFT back to No, ENTER leaves it | `RIGHT,wait,wait,LEFT,wait,wait,grab`, `ENTER,wait,wait,wait,grab` | `box move choice=yes`; `box move choice=no`; `box enter choice=no commit=0` | `pageKind=continueWatching`, depth 1, `entryCount=1`, same `entryId`; `media/resume` unchanged | `box-no-again.png` (halo back on No, same bytes as (a): `160E7A90...BF166`); `page-after-no.png` |
| (d) Circle cancels without committing | `RIGHT,wait,wait,grab`, `ESC,wait,wait,wait,grab` | `box move choice=yes`; `box cancel choice=yes`; `cue Back ... source=snd_cancel` | `pageKind=continueWatching`, depth 1, `entryCount=1`; `media/resume` unchanged | `box-cancel.png` (halo on Yes, `FD9187D7...C7C7F`); `page-after-cancel.png`, byte-identical to (c)'s page (`69DFB9F9...8AAB6`) |

Cue evidence: the (b) and (d) runs rerun with `XMB_AUDIO_LOG=1` print
`cue Confirm ... source=snd_decide` on Enter, `cue Back ... source=snd_cancel` on Esc, and
`cue Move ... source=snd_cursor` for the answer move - the mapping the pass was required to keep.

Determinism: two independent runs of the default-focus recipe at `--render-clock 20` (`det-a`, `det-b`)
produced byte-identical frames, both
`160E7A9064B4246DB11B5B65100B86651AB393D83853B521EB3162B5004BF166` (1920x1080); the Yes-state rerun is
identical to the first Yes capture (`FD9187D7...C7C7F`). A frame of every state was 1920x1080.

Canonical gate, `bun work/ab-layers.ts --capture --exe C:/dev/out/native-boxchoice/re-ps3-native.exe`:
**PASS 6/6**, text presence 1668 bright pixels in the clock crop (minimum 900) PASS. Frame 18.76 / 0.8360,
top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label row 11.99 / 0.3895, clock text
11.41 / 0.4528, info bar 15.05 / 0.7667 - every layer at its recorded baseline (+0.00), which is the check
that the change did not move the Box-free Game root the gate captures.

The two focus captures are `out/native-boxchoice/runs/a-default-no/box-default.png` and
`out/native-boxchoice/runs/b-right-yes/box-yes.png`.

### 11.5 What remains open

- Clamp vs wrap (11.1): no local burst shows the box's D-pad, so the wrap is a stated native decision.
- The `dialogs.cpp` hunk must be applied by the view pass; until it is, a build from this tree still draws
  the halo on No while the shell selects Yes (the binary in `out/native-boxchoice` already carries it).
- A Box with no answer row draws no halo (`boxChoiceSlot()` = -1) but still draws the Yes/No strings, because
  that is the captured surface. No such Box is reachable today; if a one-button Box ever appears it needs a
  drawn-surface decision of its own.
- The answer move plays `snd_cursor` by analogy with the XMB's selection move; no dialog burst ties that cue
  to the box.
- The box's 200/1000-unit open/close fades and the 100-unit hint fade remain TRACED only (10.7).
- `view.h`'s stale `drawMessageBox` comment (10.4).

### 11.6 Reproduction

```
build   & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-boxchoice
        (with the 11.3 dialogs.cpp hunk applied; the tree ships without it)
seed    copy C:\dev\out\native-dialogs\capture\box\data\* to <run>\data
pins    --render-clock 20 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00
        --focus-time 2000
script  full,pause=4000,LEFT,wait,wait,LEFT,wait,wait,ENTER,wait,wait,TRIANGLE,wait,wait,
        DOWN,wait,wait,DOWN,wait,wait,ENTER,wait,wait,wait,wait,wait,wait
        a: ,grab=<run>\box-default.png,quit
        b: ,RIGHT,wait,wait,grab=<run>\box-yes.png,ENTER,wait,wait,wait,grab=<run>\page-after-yes.png,quit
        c: ,RIGHT,wait,wait,LEFT,wait,wait,grab=<run>\box-no-again.png,ENTER,wait,wait,wait,
           grab=<run>\page-after-no.png,quit
        d: ,RIGHT,wait,wait,grab=<run>\box-cancel.png,ESC,wait,wait,wait,grab=<run>\page-after-cancel.png,quit
env     XMB_LOG=<run>\native.log XMB_DIALOG_LOG=1   (add XMB_AUDIO_LOG=1 for the cue trace)
runner  C:\dev\out\native-boxchoice\proofs.ps1
```

---

## 12. The depth-2 read-only table page (2026-10-28)

Status: implemented in `src/dialogs.cpp` (a new draw path, its measured constants and a page-surface
predicate) and one declaration in `src/view.h`. No other source file was edited. The page class is
SETTINGS-FIRMWARE 12.2's: a title above a rule, a two-column table, a lower rule and the Back hint, with
no focusable row, no info bar and no dim. Reference:
`out/parity-references/page-network-status-list__original__day__1080p.png` (RPCS3 4.93, 1920x1080, one
day pin), re-measured for this section. Route: Settings column, `Network Settings`, `Settings and
Connection Status List`.

### 12.1 Why this is a variant of Info and not a seventh MessageKind

`MessageKind` is declared in `shell.h`, which this pass may not edit, so a new enum value was not
available. The Info family already is the home for label/value lists (the D5 page), and the two share a
frame: title, rule pair, rows, Back hint. The class is therefore **the Info family's second shape**,
chosen in `dialogs.cpp` from the row that raised the message: the three host reports are
`Action::Information` rows whose `Entry::value` is `host:network`, `host:display` or `host:audio`
(`Shell::activate`, `shell.cpp`). The message text *is* the report, so keying on the row's own payload is
not an inference from string content, and a fourth host payload would have to be added in two places
(`shell.cpp`'s builder and the `hostReportTable` predicate) - stated as the residual in 12.6. Every other
Information message - System Information, a download's info page, Continue Watching's - keeps the D5
draw path unchanged, verified by capture (12.5).

If the class must become an explicit kind later, the hunk is small but touches the shell: add `Table` to
`enum class MessageKind` in `shell.h`, and in `Shell::activate`'s `Action::Information` branch pass
`MessageKind::Table` (instead of `Info`) for the three `host:*` payloads; `drawMessageSurface` then routes
`Table` to `drawMessageTablePage` and `hostReportTable` is deleted. That is not needed for the page to
draw or to be scored.

### 12.2 Measured geometry: the frame and the native page

Method, the same the audit used: RGB -> Rec.709 luma; the rules as isolated full-width bright rows in
the row profile; rows as ink runs (luma >= local background + 35..45) in the label and value windows;
columns as the first/last ink column of each row's band. Native numbers are from the final build's
capture, `out/parity/run-20261028-162332`, cross-checked by differencing the page against the identical
frame with the page dismissed (the wallpaper wave cancels, so only the surface remains).

| quantity | firmware frame | native page | delta |
|---|---|---|---|
| rule 1 | y 199, 1 px, flat (core 140.0 over 75.6 = white at 0.36) | y 199, 1 px, flat (151.4 over 93.2 = 0.359) | 0 px, alpha 0.00 |
| rule 2 | y 879, 1 px, flat (134.6 over 64.7 = 0.365) | y 879, 1 px (138.5 over 72.6 = 0.362) | 0 px |
| row 1 ink top | **291** | **291** | 0 |
| row pitch | **40** exactly, 13 rows (291 -> 771) | **40** exactly, 9 rows (291 -> 611) | 0, count 9 vs 13 |
| cap height | 18 px (descender rows 23-24) | 17-18 px | -1..0 |
| label ink right | 938..940 (mode 939) | 937..941 (mode 939) | +-1 |
| value ink left | 978..980 | 978..982 (digit/letter bearings) | +0..2 |
| icon ink | x 109..149, y 154..189 (41 x 36) | x 109..149, y 154..188 | 0, -1 at the bottom |
| title ink | x 164..574, y 167..190 | x 164..574, y 167..190 | **0** |
| Back hint ink | x 1155..1240, y 897..916 | x 1155..1235, y 897..918 | x 0, +2 at the bottom |
| focusable row / info bar / dim | none / absent (luma 0 at x 1300/1340/1700/1900 on y 100) / none | none / absent / none (page-dismissed difference: 0.00 mean on the background bands) | same |

The constants are `kTable*` in `dialogs.cpp`; the comment there names the rect-to-ink arithmetic (the
41 px icon rect puts the globe's 41 x 35 alpha bounds at 109..149 / 154..189; a VCenter row rect begins
its cap ink 11 px below its top, so the first row's rect top is 280 for ink 291).

Two numbers deserve their own note:

- **The rule alpha is this page's own measurement (0.36), not the D4 page's 0.22.** The D4 value comes
  from a 1262x800 window capture whose 1 px line lands on 0.66 of a pixel; the native-1080p frames of the
  same family read 0.36 here and about 0.69 on the message page (`page-network-test-error`, 193 over 53
  on the same boot). So the rule is not one alpha across the family: the table page carries the value its
  own frame measures and the D4/D5 chrome keeps its 56/51.
- **The table page keeps no analog clock.** The D4/D5 pages draw the clock face while the info bar's
  plate is hidden; this page's frame has an empty top-right corner and the audit's 12.2 records luma 0
  across the bar, so `drawMessageTablePage` draws its own header and rules and does not call
  `drawPageChrome`.

### 12.3 What the native draws, row by row

The title is the report's own heading, taken as the first line. For the status list that is the
firmware's `msg_netlist_current` ("Settings and Connection Status List"), resolved by
`host::networkStatus` through the RCO - the same string the row carries. Each remaining line is a row:
split at the first `": "` into label and value; a line with no `": "` (the adapter's own name, the audio
endpoint's) starts at the value column, because the label column is labels only. Empty lines are the
report's adapter separator and are not rows, so the grid stays unbroken. The grid is fixed from ink 291,
never centred, and stops at the last row that fits above the 879 rule (15 rows: 291 + 14 x 40 = 851,
ink bottom 868); the console's table is a fixed 13 rows and does not scroll. Rows are 22 px (the D5 row
size) and the label/value rects are 3 px right and 1 px left of the measured ink edges, which is where
the face's side bearings put them.

The three host reports all take the class, which is what makes the display and audio pages share it at
no extra cost: `Video Output Settings` becomes monitor rows (`<monitor>: 1920 x 1080, 240 Hz (primary)`)
and `Audio Output Settings` an endpoint line plus `Volume: 25%` (both captured,
`out/native-depth2/after-display.png` / `after-audio.png`). Their titles are host.cpp's own literals
("Video Output", "Audio Output") rather than the firmware row titles - a host.cpp change, outside this
pass (12.6).

### 12.4 Region scores, gate and determinism

Parity, `work/parity.ts --capture --score --state page-network-status-list`, same pins; before is the
D5 info page (run `20261028-155145`, the pre-change build), after is the table page (run
`20261028-162332`, the final build). `status rows` is content-different by construction and stays
informative:

| region | crop | before | after |
|---|---|---|---|
| frame | 0,0,1920,1080 | 16.84 dB / 0.8335 | **17.47 dB / 0.8641** |
| status rows | 400,280,800,500 | 12.02 dB / 0.5804 | **12.52 dB / 0.6277** |
| title rule | 80,150,1520,60 | 16.22 dB / 0.6867 | **22.16 dB / 0.9538** (budget 18.00 / 0.85, **PASS**) |

`work/ab-compare.ts` on the same crops reproduces these numbers exactly. Tier A stays page-scoped
(`depth 1`, row `net_netlist`), as the catalogue states; the region scores are the page evidence.

The residual on `status rows` is the content: the console's 13 fixed rows (Internet Connection ... UPnP)
against the host report's nine (Connection Method ... MTU). That difference is the data source's, is
already annotated `contentDiffers`, and cannot be closed without rewriting `host::networkStatus` in
`host.cpp`, outside this pass.

Canonical gate: `bun work/ab-layers.ts --capture --exe C:/dev/out/native-depth2/re-ps3-native.exe` -
**PASS 6/6 layers within budget**, `text presence: 1668 bright pixels in the clock crop (minimum 900)
PASS` (frame 18.76 / 0.8360, top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label
row 11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667 - every layer at its recorded
baseline, +0.00).

Determinism: two `--render-clock 20` captures of the page (fresh seed each) are byte-identical,
SHA256 `2866523FA0EA9166C8AF73E2AE63F4259C7BB1668D7D4AA324C9D9F23DAF8DE7` twice; a third capture with
`DOWN, DOWN, UP` after the page is up hashes to the same value, which is the no-focus claim measured
rather than asserted (the shell blocks vertical input while a message is up, and the table draws no
focus mark).

Named states, for the record and not edited: `--states` reports **5/12** on this build. The seven
failures are whole-frame differences of 49.73-53.86 dB / 0.9984-0.9995 against the 55 dB / 0.9995
self-baseline (`steam-all-games`, `steam-store-sections`, `steam-not-installed`,
`video-options-panel`, `continue-watching`, `jellyfin-signin`, `steam-download-page`); no region other
than `frame` fails except jellyfin-signin's `rows` (50.21 / 0.9987), and the five that pass include
`steam-game-root`, `xmb-notification`, `xmb-volume-hud`, `continue-watching-empty` and `subtitle-cue`.
The count moved 12/12 -> 1/12 -> 5/12 across rebuilds of this same page code while the concurrent
view/ribbon/icon pass was landing (the inspected failure's difference is confined to the category icon
row: 15,605 pixels above 8 luma, max 42, with the item column, labels, background, ribbon, clock and
info bar byte-identical). No named state draws a host report - the only message any of them shows is
the inline line - so none of this drift is this page's surface. Nothing was re-baselined.

### 12.5 Information pages kept working

`System Information` (an `Action::Information` row with an empty payload) still draws the D5 page:
verified by capture (`out/native-depth2/after-systeminfo.png`) - labels right-aligned at 925, values from
961, the centred block and the clock face, i.e. the geometry 8.1 measured and 7.2 describes. The
Continue Watching and download information pages take the same path because their payloads are text,
not `host:*`. Only the three host reports changed surface.

### 12.6 What remains open

- **Row count and rows.** The host report produces nine rows for one adapter; the console's thirteen are
  built in `host.cpp`, which this pass may not edit. The parity state's `contentDiffers` annotation is
  the honest disposition, not a hidden delta.
- **The adapter-name line** has no label and no firmware analogue; it is drawn at the value column so
  the label column stays labels-only. Two adapters would make 18 lines and the 15-row cap would clip the
  tail; the console's page does not scroll either, and no capture of a multi-adapter console page exists.
- **Display / audio titles.** `host::displayOutput` / `host::audioOutput` title their pages "Video
  Output" / "Audio Output"; the firmware rows are `msg_display_output_setting` / `msg_sound_output_setting`
  ("Video Output Settings" / "Audio Output Settings"). The page class is right; the title is host.cpp's.
- **The rule alpha is per page class** (0.36 here, ~0.69 on the message page, 0.22 on the D4 window
  capture). This page uses its own; unifying the family needs a second 1080p frame of each class.
- **The fixed row origin** (291, not centred) is the only measured placement of a table; a shorter list
  on the console might be centred and there is no frame of one.
- **The 2 px hint height** (+2 at the bottom, the family's known glyph size 22 vs the frame's ~20)
  is inherited, not new.

### 12.7 Reproduction

```
build   & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-depth2
state   bun work\parity.ts --capture --score --state page-network-status-list --exe C:/dev/out/native-depth2/re-ps3-native.exe
rows    bun work\ab-compare.ts out/parity-references/page-network-status-list__original__day__1080p.png <frame.png> 400,280,800,500
rule    bun work\ab-compare.ts out/parity-references/page-network-status-list__original__day__1080p.png <frame.png> 80,150,1520,60
gate    bun work\ab-layers.ts --capture --exe C:/dev/out/native-depth2/re-ps3-native.exe
manual  full,<10 waits>,LEFT,wait x5,<10 waits>,DOWN,wait x15,<10 waits>,ENTER,<10 waits>,ENTER,<10 waits>,grab=<png>,quit
        pins --render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00
        --focus-time 2000 --no-intro; seed = one parity cell's data/ directory
```



---

## 13. Message-surface chrome, 2026-10-28: per-page-class rules, hint anchors, footer band, notification plaque

Scope of this pass: the four elements listed in the UI-ELEMENT-GAP-AUDIT work brief - the message-page
header rules (P2/D2), the Back/Enter hint row (D5), the footer band (D3), and the notification plaque
(D7) - plus the two chrome defects the same draw path showed when the crops were re-measured (the
message-page header ink and the confirm box's answer row). Code touched: `native-xmb/src/dialogs.cpp`
and one signature in `native-xmb/src/view.h`. `view.cpp` was not touched; 13.7 lists the two deltas this
pass measured on message pages that live in it and returns them as hunks.

All numbers are 1920x1080 frame pixels, Rec.709 luma, 1 px rules read as a full-width row mean
(`x20..1900`) and alphas as `(row - background) / (255 - background)`. "before" is build
`out/native-msgchrome-before` (HEAD cbba0de), "after" is `out/native-msgchrome`, both captured at
`--render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000`.

### 13.1 The rules are a per-page-class pair, and the message page is NOT the old 198/872 fit

The frames decide this. Every 1080p message-surface reference carries one flat white 1 px pair at
**y 199 and y 879**, and the pair's alpha is the class's own:

| page class | frame | top rule (y199) | bottom rule (y879) | alpha | drawn before | drawn after |
|---|---|---|---|---|---|---|
| modal message page | `page-network-test-error` | 192.6 over 51.7 | 189.4 over 41.5 | 177/255 = 0.693 | 198 / 872 at 56 / 51 (0.218 / 0.201) | 199 / 879 at 177 |
| modal message page | `page-network-connection-settings` | 192.6 over 51.7 | 189.4 over 41.5 | 0.693 | 198 / 872 at 56 / 51 | 199 / 879 at 177 |
| confirm box | `dialog-box` (byte-identical copy of the `dialog` firmware frame) | 199.2 over 73.5 | 193.4 over 54.4 | 0.693 | footer rule only at 879 at 177; no top rule | 199 / 879 at 177 |
| depth-2 table page | `page-network-status-list` | 138.9 over 73.2 | 133.2 over 61.9 | 92/255 = 0.361 / 0.369 | 199 / 879 at 92 | unchanged, re-verified |
| D5 information page | none in the corpus | - | - | - | 198 / 872 at 56 / 51 | unchanged |

Consequences and corrections to this document:

- **Section 2.1's message-page rule values are a different capture's.** The 198/872 at 0.218/0.201
  numbers came from `out/native-reference/recording-193556/frame-00020600.jpg`, a 1262x800 window
  capture whose content rect does not map to 1080p by a single scale: its rule rows measure y201
  (alpha 0.220) and y644 (alpha 0.189), not 198 and 872. No 1080p frame of the D4 "Search for Media
  Servers" page exists, so those values were never verifiable at 1080p. The two 1080p message pages
  this build actually reaches draw the bright pair.
- **The D5 information page keeps the old fit.** It is the one shape in this file with no 1080p
  reference at all, so re-fitting it to the message page's pair would be invention. It is now an
  explicit constant (`kInfoRuleTop/Bottom`, `kInfoRuleAlphaTop/Bottom`) rather than a shared one, so
  the two classes cannot drift into each other.
- **The table page was already right and is kept.** Its pair is 199/879 at 92 (0.361/0.369 measured),
  which the previous pass landed; this pass re-measured it and changed nothing.
- **The extent is the frame width, not a 960 px centre-bright bar.** The message page's rule row reads
  189.6 at x0 and 191.1 at x1919 - uniform to both edges - so the flat rect runs 0..1919. The brief's
  "x20..x1900" is the audit's sampling window, not the rule's extent: the rule continues into the
  last pixel column. `tex_msgdialog_bar` (960 px, centre-bright) stays unused for these rules.
- The confirm box now draws the **pair**: the frame has a header rule at y199 as well as the footer
  rule, and the footer rule is one half of the same pair, not a lone band.

### 13.2 The hint row: one measured pair, and the 2 px glyph error was the box

Every measured message-surface frame puts the O glyph's ink at **1155..1174 x 897..916** (20 x 20) and
its label's ink at **1192..1240 x 898..914**; the box's X pair at **726..743 x 898..915** (18 x 18) and
**762..814 x 898..914**. The old code anchored the glyph's *rect* at 1155/889 with a 22 px box (which
draws 22 px of ink - the textures fill the box they are given) and flowed the label from
`glyph + 22 + 8`, i.e. 1185 rect -> 1187 ink.

| element | frame ink | before | after |
|---|---|---|---|
| O glyph (message, table, info pages) | 1155..1174 x 897..916 (20 x 20) | 1155..1176 x 889..910 (22 x 22, 8 px high) | 1155..1174 x 897..916 |
| O label "Back" | 1192..1240 x 898..914 | 1187..1235 x 892..908 (5 px left, 6 px high) | 1192..1240 x 898..914 |
| X glyph (box) | 726..743 x 898..915 (18 x 18) | 726..747 x 897..918 (22 x 22) | 726..743 x 898..915 |
| X label "Enter" (box) | 762..814 x 898..914 | 764..816 x 900..916 | 762..814 x 898..914 |

The 2 px inherited glyph-height error is now named where it comes from: the 30x30 `tex_cross` /
`tex_circle` art fills its destination rect, so a 22 px rect drew 22 px of ink against a 20 px (O) and
18 px (X) frame. The two glyphs have different ink margins, so the box carries a per-pair ink box
(`kBoxFooterGlyphTop/Size`) while the generic row uses the O's 20 px.

The 21 px label's two layout offsets are now explicit and measured rather than incidental: its ink
starts 2 px inside the AlignLeft rect (`kHintLabelBearing`) and the AlignVCenter rect sits 4 px above
the glyph's ink top (`kHintLabelRectTop`, giving ink rows 898..914).

### 13.3 The header was 10 px right and 6 px high on every message page

Measured while re-measuring the title rule crop: the message pages' icon ink is **109..149 x 154..189**
(41 x 36) and the title ink starts at **x 165** with cap rows **167..186** - the same anchors the table
page already used, not the 36 px icon at (119,154) / title from 169 that the D4 800p capture gave.

| element | frame ink | before | after |
|---|---|---|---|
| header icon | 109..149 x 154..189 | 119..154 x 156..187 | 109..149 x 154..188 |
| header title | from x 165, rows 167..186 | from x 171, rows 164..182 | from x 165, rows 167..185 |

One px short at the bottom of the icon and one px short on the title's right edge is the same
alpha-bounds/kerning residue the table page carries (12.4), not a new fit.

### 13.4 The confirm box's answer row

Found while scoring the box: the pair sat on the 424 px text rect's centre (1286) instead of the
frame's pair centre (1280.5), and the 22 px face drew a 38 x 18 "Yes" against the frame's 41 x 19.

| element | frame ink | before | after |
|---|---|---|---|
| "Yes" | 1182..1222 x 583..601 | 1191..1228 x 584..601 | 1184..1223 x 584..602 |
| "No" (focused) | 1337..1379 x 578..606 (halo) | 1350..1380 x 583..602 | 1345..1377 x 583..603 |
| pair centre | 1280.5 | 1285.5 | 1280.5 (by construction) |

The 23 px face reproduces the frame's 41 x 19 "Yes" (the 23 px table title face is the same size).
Residual: the focused label's halo is weaker than the frame's - ours adds 473 luma pixels over the ink
box, the frame 1003 with ~5 px of bloom. The glow call already passes `glow = 1.0`; the shape of the
bloom is `text_renderer.cpp`'s, which this pass may not edit.

### 13.5 Crop numbers (matched comparisons, `work/ab-compare.ts`)

| state | crop | before | after | budget | verdict |
|---|---|---|---|---|---|
| page-network-status-list | title rule 80,150,1520,60 | 22.16 / 0.9538 | 22.16 / 0.9538 | 18.00 / 0.850 | PASS both (landed by commit 719aeb8, re-verified) |
| page-network-test-error | back hint 1140,880,120,56 | 11.44 / 0.5197 | **16.71 / 0.8358** | 18.00 / 0.850 | +5.27 dB, +0.316 SSIM; still 1.3 dB short (13.7) |
| page-network-connection-settings | back hint 1140,880,120,56 | 11.44 / 0.5196 | **16.72 / 0.8359** | 18.00 / 0.850 | same |
| page-network-test-error | title rule 80,150,1520,60 (diagnostic: no catalogue region) | 13.94 / 0.6404 | **16.12 / 0.8569** | - | header + rule fix |
| page-network-connection-settings | title rule 80,150,1520,60 (diagnostic) | 13.75 / 0.6295 | **16.10 / 0.8563** | - | same |
| dialog-box | box footer 600,872,760,55 | 17.71 / 0.8712 | **22.25 / 0.9512** | 18.00 / 0.880 | +4.54 dB, now PASS |
| dialog-box | box answers 1150,560,420,60 | 19.32 / 0.8613 | **21.64 / 0.9012** | 18.00 / 0.880 | +2.32 dB, now PASS |
| dialog-box | frame 0,0,1920,1080 | 19.68 / 0.8924 | **19.88 / 0.9013** | 18.00 / 0.880 | PASS |
| page-network-test-error | frame 0,0,1920,1080 | 15.65 / 0.7866 | **15.85 / 0.8051** | 18.00 / 0.850 | FAIL both; residual is 13.7 |
| page-network-connection-settings | frame | 15.72 / 0.7856 | **15.93 / 0.8046** | 18.00 / 0.850 | same |
| page-network-status-list | frame | 17.47 / 0.8641 | 17.49 / 0.8648 | 18.00 / 0.850 | unchanged (13.7) |
| dialog-box | box plate 482,452,319,40 | 12.10 / 0.3815 | 12.10 / 0.3815 | - | unchanged; contentDiffers (different scene behind the wash) |

The back-hint crop's remaining 1.3 dB is the page dim, quantified in 13.7: with the frame's own page
factor applied offline the same crop measures **19.41 dB** (best-fit gain 0.735), i.e. the hint row is
now pixel-aligned and the residual is not this element.

### 13.6 Verification

| check | result |
|---|---|
| canonical gate `work/ab-layers.ts --capture --exe C:/dev/out/native-msgchrome/re-ps3-native.exe` | **PASS 6/6**, every layer delta +0.00 dB / +0.0000 SSIM (frame 18.76/0.8360, top background 21.06/0.9434, category icon row 21.62/0.9062, item label row 11.99/0.3895, clock text 11.41/0.4528, info bar 15.05/0.7667); text presence 1668 bright pixels (min 900) PASS - the rest frame did not move |
| determinism, two `--render-clock 20` captures | both SHA-256 `37E2B590C42EF0E3F3601CA492221147C9874D2E3B1785E6237B616F2C0BBC14`, PSNR Infinity / SSIM 1.0000 |
| `work/parity.ts --capture --score --state page-network-status-list,page-settings-system,dialog-box` | scorecard `out/parity/scorecards/scorecard-20261028-181513.md`, shipped exe sha256 `AE576594849AF17A9BACCF4913BA2EB9C0F9C7AB81AAE755C5BDEDA71E9593C9` (Tier A 3 pass, 2 cells B informative, 1 gating cell failure - the status-list frame below) |

| state | region | PSNR / SSIM | verdict |
|---|---|---|---|
| page-network-status-list | frame | 17.49 / 0.8648 | FAIL (18.00/0.850) - the host-report rows, contentDiffers |
| | status rows | 12.52 / 0.6277 | informative (content differs) |
| | title rule | 22.16 / 0.9538 | PASS |
| page-settings-system | frame | 19.08 / 0.8630 | PASS |
| | page rows | 16.80 / 0.7533 | informative (content differs) |
| | category icon row | 22.73 / 0.9593 | PASS |
| dialog-box | frame | 19.88 / 0.9013 | PASS |
| | box plate | 12.10 / 0.3815 | informative (content differs) |
| | box answers | 21.64 / 0.9012 | PASS |
| | box footer | 22.25 / 0.9512 | PASS |

`dialog-box` moved from "B fail" to "B informative" (only its annotated plate region is under budget
now) and `page-network-status-list` keeps its one gating frame miss, which is the row content.

The tree also carries the transitions pass's in-flight `view.cpp`/`view.h` work (PAGE-MOVE: page/bar
duration split, PS-hold envelope, options-panel timing). The before/after pair above holds it constant:
the crops this pass did not touch are byte-identical across the pair (status rows 12.52 / 0.6277 and
box plate 12.10 / 0.3815 in both, the same values the 18:09 and 18:10 builds score), and the canonical
gate's root frame is +0.00 on every layer, so the deltas in 13.5 belong to `dialogs.cpp` alone.

### 13.7 Deltas this pass measured but did not close (they are not this file's chrome)

1. **The page behind a message page is dimmed 0.70; this build draws no dim.** `page-network-test-error`
   and `page-network-connection-settings` read 47.0-51.0 where the same patch on a message-free frame
   reads 73-88 (ratio 0.56-0.74 by point; a least-squares gain over the whole page is 0.735). The confirm
   box and the table page measure **no** dim, so the treatment is per class: modal message page = dimmed,
   box/table = not. This is the dominant term in the two message pages' frame crops and in the back-hint
   crop's residual (11.44 -> 16.71 -> 19.41 with the gain applied). The stack order is
   page -> dim -> chrome, so the wash belongs where the page is composited (`view.cpp`'s page path), not
   inside `drawMessageSurface`, which draws after the whole scene including the ribbon and info bar.
   Hunk returned rather than landed.
2. **A message page keeps the whole info bar; this build suppresses it.** The reference keeps the plate's
   nine-slice ramps (y75 = 124.8 and y132 = 124.5 at x1400; left ramp x1329 = 135), the clock text
   (574 ink pixels in x1690..1770) and the filled clock face. Our `view.cpp` gate
   (`if(!messageScreen && !choiceBandUp && !optionsBandUp)`, `view.cpp:2097`) skips the whole block, so
   `dialogs.cpp` draws only the face - at the old fit centre (1776,93) with a 2.7 px ring and no dark
   disc, against the frame's 32x32 face at (1789.5,103.5), ~3 px stroke, dark disc and 1.5 px shadow.
   Hunk returned rather than landed: the plate/text are the info bar's, and the face should move with
   them.
3. **The box's focused-answer halo is weaker than the frame's** (13.4).

### 13.8 The notification plaque: no console frame exists, so nothing was changed

D7's plaque (400x58 one line / 400x84 two, right edge 1896, top 145, 48 px slide) is still a fit. The
search this pass ran:

- **Name search.** Every `*.png|*.jpg|*.bmp` under `C:\dev` whose name contains notif/plaque/trophy/
  message: 130 hits, all of them native captures (`out/native-reference/notify*`,
  `out/native-notify*`, `out/smoke-notify.png`), RCO texture extracts (`system_plugin/tex_notification*`,
  `profile_plugin/tex_*message*`) or unrelated assets. No console frame.
- **Capture metadata.** The RPCS3 manifests and session logs for the six capture trees under `out/`
  never script a notification event, and `out/rpcs3-ref2-2026-10-28/MANIFEST.md` explicitly lists the
  notification among the states that need a reference and do not have one. `work/parity-catalogue.json`
  catalogues `notification` as native-only, and every scorecard scores it `no-reference`.
- **Pixel search.** A plaque signature (mean luma of x1500..1890, y150..227 minus the same band at
  y300..377; the XMB background falls with y, so a plaque stands out) over 836 frames - all 1,018
  curated stills/pages/frames of the six capture trees plus `out/parity-references`, and a 1-in-40
  sample of 25,603 burst frames - found no plaque. The highest score belongs to
  `column-psn__original__day__1080p.png` (49.1), and it is not a notification: the PSN column's own
  category artwork is a 340 x 106 banner at x1560..1900, y144..250, not a 400 x 58 nine-sliced plate.
  Median score over the set is 0.67.

Therefore the plaque rect, the 48 px slide and the 22/18 px text stay exactly as they are; nothing was
invented to fill the gap. What would settle it is one console frame with a trophy/friend notification,
which needs an event that PSN-less RPCS3 cannot produce on this host.

### 13.9 Reproduction

```
build   & C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-msgchrome
before  bun work\parity.ts --capture --exe C:/dev/out/native-msgchrome-before/re-ps3-native.exe --state page-network-status-list,page-network-test-error,page-network-connection-settings,page-settings-system,dialog-box
after   bun work\parity.ts --capture --exe C:/dev/out/native-msgchrome/re-ps3-native.exe --state page-network-status-list,page-network-test-error,page-network-connection-settings,page-settings-system,dialog-box
rule    bun work\ab-compare.ts out/parity-references/page-network-status-list__original__day__1080p.png <frame.png> 80,150,1520,60
hint    bun work\ab-compare.ts out/parity-references/page-network-test-error__original__day__1080p.png <frame.png> 1140,880,120,56
footer  bun work\ab-compare.ts out/parity-references/dialog-box__original__day__1080p.png <frame.png> 600,872,760,55
gate    bun work\ab-layers.ts --capture --exe C:/dev/out/native-msgchrome/re-ps3-native.exe
parity  bun work\parity.ts --capture --score --state page-network-status-list,page-settings-system,dialog-box --exe C:/dev/out/native-msgchrome/re-ps3-native.exe
```

The measuring scripts for 13.1-13.4 (row profiles, ink boxes, the dim gain) are in
`C:\dev\out\msgchrome\` (`measure_refs.py`, `measure_frame.py`, `scan_notify3.py`), outside the repo.
