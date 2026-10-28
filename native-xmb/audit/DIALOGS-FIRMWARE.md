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
