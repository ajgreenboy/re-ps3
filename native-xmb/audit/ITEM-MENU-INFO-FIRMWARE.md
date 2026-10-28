# ITEM-MENU and INFO-PANEL: firmware object trees, strings, geometry

Status: analysis only, 2026-10-27. No source edit, no RPCS3 run, no git command. Scope: the two
surfaces the native build approximates - the item options menu (Triangle) and the top-right
information panel with its indicators.

Every claim below carries an evidence path. Three evidence classes are used and never mixed:

- **RCO** - decoded from the local 4.93 dev_flash with `work/rco-tree-dump.ts` (object trees, from the
  pre-digested `out/native-reference/rco-behavior/resources.json`, 122 resources / 8,070 objects) and
  `work/rco-table-dump.ts` (image table with GIM dimensions, English text table, both read straight
  out of the RCO the same way `src/assets.cpp` does).
- **CODE** - decrypted module decompiles already in the tree: `out/native-reference/explore_plugin_all.c`
  (`explore_plugin.prx`), `xmb_plugin_all.c` (`xmb_plugin.prx`), `paf-range.c` (`vsh.elf` paf range).
  Line numbers are of those files as committed.
- **FRAME** - the saved 1920x1080 firmware capture `out/native-reference/ab-rpcs3-root.png`, measured
  pixel-wise in this pass (method in section D4).

Anything not in one of those three classes is marked **UNATTRIBUTED** and named with the capture or
trace that would settle it. Firmware-derived numbers stay in this note; no dump, module, RCO, texture
or capture is copied into the repository.

---

# Part A - the item options menu

## A1. Which RCO and which plugin supply it

**CODE + RCO.** The menu is a paf page named `expage_menu` inside
`vsh/resource/explore_plugin_full.rco`, built and driven by `explore_plugin.prx`:

| Step | Evidence |
|---|---|
| Triangle reaches a per-interface method that calls the menu opener | `explore_plugin_all.c:23492-23504` (`FUN_000333f4` -> `FUN_0004ec9c`), and the same call with context 0 at `:34275-34282` |
| Opener reads the current menu state, refuses when one is already open, builds the item array, creates the page | `explore_plugin_all.c:34238-34272` (`FUN_0004ec9c`; `FUN_0005f110` guards re-entry, `FUN_0004b0a0` fills the option struct) |
| Page + 3 menu instances created | `explore_plugin_all.c:37293-37353` (`FUN_0005ee6c`): `vshcommon_CDF4C62A(page,"expage_menu",&plane_menu0, ..., 3)` |
| A nested menu is opened as menu index+1 on the same page | `explore_plugin_all.c:37249-37290` (`FUN_0005ecf0`: `vshcommon_4A6E257D(page, plane_menu0[i+1], ...)` with `i+1 < 3`) |
| Page teardown hook | `explore_plugin_all.c:37318` (`paf_paf_986427A7(page,0x2000e,...)`), the same paf widget-destroy event documented in `audit/INFO-BAR.md` section 2 |

The per-category modules (`explore_category_game.prx`, `_video`, `_music`, `_photo`, `_user`, ...) extend
the same builder with category-specific rows; those modules are not decompiled locally, so their exact
row lists are **UNATTRIBUTED** (section A4 gives the one list that is proven).

`xmb_plugin_normal.rco` / `xmb_ingame.rco` do **not** contain the options menu - they hold the category
bar and the indicator page only (26 objects each, RCO). The options menu is not an XMB page level: it is
an `explore_plugin` page laid over the XMB, which is why it can own a page while `depthPosition_` in the
native is untouched.

## A2. The object tree (RCO, exact attributes)

`vsh/resource/explore_plugin_full.rco`, page `expage_menu` at table offset 222852, 16 objects:

```
expage_menu                         Page  0x801 @222852
  plane_menu0                       Plane 0x802 @222928   pos 0,0  size 0x0    (auto)
    plane_menu0_BG0                 Plane 0x802 @223068   size 0x0             (auto)
    plane_menu0_BG1                 Plane 0x802 @223208   size 0x0             (auto)
    plane_menu0_LowerMenu           MList 0x809 @223348   size 0x0             (auto)
    plane_menu0_UpperMenu           MList 0x809 @223596   size 0x0             (auto)
  plane_menu1                       Plane 0x802 @223844   pos 0,0  size 128x272
    plane_menu1_BG0/BG1             Plane 0x802 @223984 / @224124   size 0x0
    plane_menu1_LowerMenu/UpperMenu MList 0x809 @224264 / @224512   size 0x0
  plane_menu2                       Plane 0x802 @224760   pos 0,0  size 128x272
    plane_menu2_BG0/BG1             Plane 0x802 @224900 / @225040     size 0x0
    plane_menu2_LowerMenu/UpperMenu MList 0x809 @225180 / @225428     size 0x0
```

What each slot is, and the evidence for it:

| Object | Role | Evidence |
|---|---|---|
| `plane_menu0` | the item list's own plate container; index 0 is the list the user opened | `explore_plugin_all.c:37320-37327` addresses menu (0,0) and (0,1) after creation |
| `plane_menu1`, `plane_menu2` | the second and third menu levels (a submenu opened on the same page), each a fixed 128x272 box | RCO size attribute; `FUN_0005ecf0` opens `plane_menu0[i+1]` |
| `*_BG0` | the list's background plate | name `BG0` in every firmware menu page, e.g. `friendim_plugin.rco` `optionmenu_child_voice` (`plane_menu_voice_BG0/BG1/UpperMenu`), `:51764` region |
| `*_BG1` | the focused row's highlight plate | same pages; one BG1 per menu, i.e. one highlight, which is how a single-focus list is built |
| `*_UpperMenu` / `*_LowerMenu` | the rows above and below the focused row (inferred: the split exists in every firmware option menu - `plane_menu_voice_UpperMenu` holds two MItems - and there is exactly one `BG1` per level, so the highlight is the boundary between the two lists; the RCO does not state it) | `friendim_plugin.rco` `optionmenu_child_voice` region, `:51764` |
| rows | `MItem` children of a MList, each with an `image` ref and a `text` ref | `friendim_plugin.rco`: `plane_menu_voice_UpperMenu` -> `display_list_voice_view`, `save_list_voice_view` (`MItem 0x80a`), `optionmenu_child_download` -> `save_list_download_view` |

**The item menu's own MLists ship empty.** `expage_menu`'s four MLists per level have no children in
`explore_plugin_full.rco`; the rows are installed at runtime from the plugin's own array
(`explore_plugin_all.c:37337` `paf_paf_4618F156(menu, param_3[3])` - one call hands the whole string
array to the list). The friend-list pages in `friendim_plugin.rco` are the same structure with the MItem
templates shipped in the RCO, which is what makes `MItem` the row type for the item menu too.

Consequences that matter for the native diff:

1. The panel is a **plate-backed floating box** (`BG0` under the rows, `BG1` under the focused row), not
   a full-height translucent strip. There is no full-height object anywhere in `expage_menu`.
2. Focus is expressed by a **plate that moves between rows** (`BG1`), not by a text glow.
3. There is **no Scroll object** in the menu tree (compare the firmware's list pages, which do carry one:
   `scroll_friendtrophy_info_title` `Scroll 0x808`, width 8, height 172). The option menu does not
   scroll; a 12-row list at the firmware's row pitch still fits.
4. There are **no per-row icons** in the menu tree: `MItem` has an image ref, but no icon object exists
   per row and the item menu's rows are text entries handed to paf as strings.
5. There is **no button-hint object** in `expage_menu`; the Cross/Circle hints are their own page
   (`system_plugin.rco` `page_btnnavi` -> `btnbar` -> `cross_shadow{cross_icon, cross_label}`,
   `circle_shadow{...}`, `triangle_shadow{...}`, `rectangle_shadow{...}`; `premo_plugin.rco`
   `premo_page_btnnavi` duplicates it for the overlay case). Textures: `tex_cross` / `tex_circle` /
   `tex_triangle` / `tex_rectangle` 30x30 with 40x40 `*_shadow` counterparts.

## A3. Row texts, from the RCO text table (not the guide)

`bun work/rco-table-dump.ts <explore_plugin_full.rco> --text` dumps the English table (861 entries; the
runner prints them UTF-16BE/UTF-8 per entry the way `src/assets.cpp:readEnglish()` does, so the same
command reproduces every value below). These are the strings the option menu can show, with the table
key, so a reader can grep them:

| key | English value | native `OptionCommand` it corresponds to |
|---|---|---|
| `msg_open` | `Open` | `Activate` (folder/store row) |
| `msg_play` | `Play` | `Activate` (playable file) |
| `msg_display` | `View` | `Activate` (image row / information row) |
| `msg_game_start` | `Start` | `Activate` (game row) |
| `msg_resume` | `Resume` | `Activate` on a resumable row |
| `msg_play_begin` | `Play from Beginning` | `JellyfinRestart` |
| `msg_info` | `Information` | `Information` |
| `msg_information` | `Information` | (second key, same value; the item-menu builder uses `msg_info`, `explore_plugin_all.c:60789` and `:60986`) |
| `msg_sort` | `Sort By` | `SortMenu` |
| `msg_display_all` | `Display All` | (not reachable: native has no filtered page) |
| `msg_folder_class` | `Group Content` | (absent) |
| `msg_add_to_playlist` | `Add to Playlist` | (absent) |
| `msg_copy` | `Copy` | (absent) |
| `msg_copy_plural` | `Copy Multiple` | (absent) |
| `msg_del` | `Delete` | (absent) |
| `msg_del_plural` | `Delete Multiple` | (absent) |
| `msg_eject` | `Remove` | (absent) |
| `msg_disc_eject` | `Remove Disc` | (absent) |
| `msg_cancel` / `msg_cancel2` | `Cancel` | (native uses `Close` instead) |
| `msg_check_for_update` | `Check for Update` | (absent) |
| `msg_game_update_history` | `Update History` | (absent) |
| `msg_internet_search` | `Internet Search` | (absent) |
| `msg_purchase` | `Purchase` | (absent) |
| `msg_about_game` | `About this Game` | (absent) |
| `msg_psmove_notice` | `PlayStation(R) Move Safety Notice` | (absent) |
| `msg_savedata_auto_upload_psplus_with_icon` | `Saved Data Auto-Upload ` | (absent) |

Two facts fall straight out of the table and contradict the current native design:

- **There is no `Close` row in the firmware.** The string `Close` does not exist as an
  explore_plugin_full menu label (grep of the 861-entry table); the list is dismissed with Circle. The
  native's `Close` row (`shell.cpp:1641`) is invented.
- **`Remove` / `Remove Disc` is the firmware's eject label**, not `Eject Disc`
  (`audit/OPTIONS-MENU.md:47` and `:475` both say Eject Disc).

The firmware's own **sort** rows are date-ordered pairs with the direction glyph inside the string, and
its **group** rows are `By ...` labels. Both are in the same table, so a native Sort By / Group Content
row can be built from the RCO rather than from English literals:

| key | value |
|---|---|
| `msg_created_date_time_f` / `_r` | `Creation Date ^` / `Creation Date v` (U+25B2 / U+25BC in the file) |
| `msg_updated_date_f` / `_r` | `Update Date ^` / `Update Date v` |
| `msg_importeddate_f` / `_r` | `Import Date ^` / `Import Date v` |
| `msg_sale_year_f` / `_r` | `Release Year ^` / `Release Year v` |
| `msg_shootdate_f` / `_r` | `Film Date ^` / `Film Date v` |
| `msg_trophy_earned_f` / `_r` | `Earned Date ^` / `Earned Date v` |
| `msg_title_folder`, `msg_album_folder`, `msg_artist_folder`, `msg_genre_folder`, `msg_format_folder`, `msg_month_folder`, `msg_year_folder`, `msg_track_folder`, `msg_sale_year_folder` | `By Title`, `By Album`, `By Artist`, `By Genre`, `By Format`, `By Month`, `By Year`, `By Track`, `By Release Year` |

## A4. The proven row list and its conditions

`explore_plugin_all.c:60846-60997` (`FUN_000c44ec`) is the game category's option array. Each row is a
16-byte record `{label, handler, param, flag}` based at `DAT_002ca068` (label at +0, handler OPD at +4,
two small constants at +8 and +12; the builder indexes it as `(&DAT_002ca068)[i*4]`), and the builder
appends in this order under these conditions (field names are the decompile's own offsets):

| # | label | condition in the code | line |
|---|---|---|---|
| 0 | `Start` (`msg_game_start`) | only when `*(char*)(state+0xd4)` (disc/BD title) | 60868-60874 |
| 1 | `Internet Search` | always | 60882 |
| 2 | `Purchase` | `(state+0x2e4 & 0x80000)` and `*(char*)(state+0x6a8)` | 60889-60903 |
| 3 | `Copy` | `*(char*)(state+0x69c)` and not `0xd4` | 60908-60915 |
| 4 | `Delete` | always | 60926 |
| 5 | `Check for Update` | always | 60930 |
| 6 | `Update History` | `*(char*)(state+0x676)` | 60937-60943 |
| 7 | `Health Warning (PSN)` | `*(char*)(state+0xd4)` | 60948-60955 |
| 8 | `PS Move Safety Notice` | `(state+0x2e4 & 0x100)` | 60959-60966 |
| 9 | `About this Game` | `*(char*)(state+0x6aa) == 0` | 60970-60977 |
| 10 | `Intellectual Property Notice` | always | 60984 |
| 11 | `Information` | always, last | 60991 |

`FUN_000c421c` (`:60724-60797`) is the same array without update/purchase rows (Start, Internet Search,
Copy, Delete, Health Warning, Information) - i.e. the firmware builds the list per item capability, and
**Information is always the last row**. Neither builder appends a Close/Cancel row; the list is torn down
by `FUN_0005e94c` (`:37212-37246`), which destroys the page and its menu items.

The handler column is a function pointer per row, so a row's action is bound to the row, not dispatched
by a command enum - the native's `OptionCommand` switch is a different architecture, which is fine, but
the label/condition table above is the firmware's actual command set.

## A5. The confirm flow (its own pages, in the same RCO)

The firmware does not confirm inside the menu. The RCO ships a dialog page per action class, and these
are the objects a native rework has to reproduce:

| Page | Objects (RCO) | Used for |
|---|---|---|
| `info_list` @225676 | `info_base_plane` (Plane) | the `Information` action |
| `expage_fdelete_dialog` @229100 | `filedel_dialog_listbase`, `filedel_dialog_icon`, `filedel_dialog_info`, `filedel_dialog_msgdialog`, `fdel_plane_src_cmdbtn` {`fdel_cmdbtn_select_all`, `fdel_cmdbtn_deselect_all`, `fdel_cmdbtn_decide_src`}, `fdel_text_info_msg` | Delete: from the object names, a list selection dialog (select all / deselect all / decide) rather than a yes/no box |
| `expage_fcopy_dialog` @225892 | `thumb_info_base`, `filedialog_listbase`, `plane_src_cmdbtn` {`cmdbtn_select_all`, `cmdbtn_deselect_all`, `cmdbtn_decide_src`}, `text_info_msg`, `text_wait`, `plane_exist_dimmer`, `plane_exist_dialog_btn` {`cmdbtn_rename`, `cmdbtn_overwrite`, `cmdbtn_nosave`}, `plane_exist_chkbase` {`cmdbtn_chk_applyall`} | Copy: source picker, wait state, name-exists dialog with Rename/Overwrite/Do Not Save, "apply to all" |
| `expage_fcopy_exdlg` @228948, `expage_fcopy_msgdlg` @229024, `expage_message_dialog` @230908, `expage_filelist` @230832 | Page shells | progress / message / file list |

So the firmware's confirm flow is: menu row -> a **separate page** with its own plates, buttons and
multi-select list. The native's "second level of the same right-hand panel" (`audit/OPTIONS-MENU.md`
section 10.1) is a design invention; the firmware's shape is page-per-action.

## A6. Geometry at 1080p

**What the RCO actually carries** (all values are the decoded attributes; paf RCO units are 1080p pixels,
which `audit/LAYOUT-TRACE.md` row 1 confirms independently on `xmenu posX -395 -> screen x 565`):

| object | x | y | w | h |
|---|---:|---:|---:|---:|
| `expage_menu` (page) | - | - | - | - |
| `plane_menu0` (item list) | 0 | 0 | 0 (auto) | 0 (auto) |
| `plane_menu1` (first submenu) | 0 | 0 | 128 | 272 |
| `plane_menu2` (second submenu) | 0 | 0 | 128 | 272 |
| all `BG0` / `BG1` plates | 0 | 0 | 0 (auto) | 0 (auto) |
| all `UpperMenu` / `LowerMenu` MLists | 0 | 0 | 0 (auto) | 0 (auto) |

**The panel's on-screen box, row height, font size and paddings are UNATTRIBUTED.** They are paf's:
the page's planes are all auto-sized except the two nested 128x272 boxes, and the rows are created by
paf from the string array (`explore_plugin_all.c:37337`), so the row pitch, the text size, the row
padding and the plate padding are decided inside `PhMenuList`/`PhMenuItem` in `vsh.elf`
(`paf-range.c:49184-49313` is the PhMenuList cluster - the class-name getter returning
`PTR_s_PhMenuList_006fb24c` is at `:49278-49293`: `_opd_FUN_003d7ec8` sets two floats at `+0x2b0/+0x2b4`
from a caller value, `_opd_FUN_003d81c8` bounds-checks the item index against `+0x2a8`,
`_opd_FUN_003d8004` interpolates `+0x2c0/+0x2cc/+0x2d8` between `+0x2c4/+0x2d0/+0x2dc` and
`+0x2c8/+0x2d4/+0x2e0` by a frame factor at `+0x10`). The literals that class reads are `DAT_0068f80c`,
`DAT_0068f810`, `DAT_0068f814`, `DAT_0068f818`; none of them is a row pitch or a font size, and a float
sweep of the whole `.data` widget-default block at `vsh.elf` 0x68eec0-0x68f1d0 returns only 100, 250,
200, 160, 80, 40, 10, 5, 0.5, 1, 0.3 - animation durations, scroll steps and focus paddings already
documented in `audit/LAYOUT-TRACE.md`, no menu metric. The page's own position is not in the RCO either
(`posX/posY` are 0 on all three planes), so the placement comes from the plugin's page camera, which is
not in the local artifact set.

The only firmware-authored geometry that *is* attributable for this surface, and what it implies:

| value | source | what it says |
|---|---|---|
| 128x272 for menu levels 1-2 | RCO attributes | a nested menu is a small fixed box; it is not a second full-height strip |
| `tex_opt_obi` 660x6 | `explore_plugin_full.rco` image table (`work/rco-table-dump.ts --images`) | a 660 px-wide, 6 px-tall option-menu band asset exists. Its attachment to a specific object is **UNATTRIBUTED** (the plane image refs in `expage_menu` do not resolve to it with the local decoder) |
| one animation per menu instance, argument `100` | CODE, A7 | the menu's own property animation uses the same 100 the firmware uses as its widget animation default |
| no Scroll object | RCO | no scroll bar or scroll arrows in the options menu |
| one `BG1` per level | RCO | a single focus plate, i.e. one highlighted row |

**Current native values, for the diff** (`native-xmb/src/view.cpp`):

| native value | line |
|---|---|
| panel left edge `x0 = 1330 + ahead*600` | 1110 |
| panel spans the full height 0..1080 | 1122-1125 |
| first row `505 - (rows-1)*20` | 1112 |
| row pitch 40, rows `505 + (i-position)*40` | 1135 |
| label x `x0+16` (item menu), `x0+34` (player panel) | 1133 |
| row text 23 px, `255*(.78+.22*focus)` | 1148-1149 |
| focus "glow" halo, 23 px, strength `(focus-.5)*2` | 1143 |
| heading 19 px at `first-64` | 1129-1131 |
| note line 16 px under the list | 1150-1157 |

None of those numbers comes from an RCO object; `audit/OPTIONS-MENU.md:80` says so itself ("same visual
language as the settings panel").

**The capture that would turn the UNATTRIBUTED row into numbers** (not run here - another agent owns the
rig): RPCS3, 1920x1080, XMB root, Game column with one installed game focused, press Triangle, wait 1 s,
grab. From that single frame measure: panel left/right/top/bottom edges (the `BG0` plate), the `BG1`
highlight rect, row pitch (baseline-to-baseline over 4 rows), first and last row's text left edge, and
the text cap height. A second grab with Down held for 4 rows gives the `BG1` travel per step. That is
eight numbers and it closes A6 completely. Nothing in `C:\dev\out` currently contains a firmware
options menu: the only frames of this surface are native captures
(`out/native-reference/options/options-open.png`, `out/native-options/...`,
`out/native-continue/b3-options.png`, `out/native-jf1-evidence/captures/15-options.png`,
`out/smoke-options.png`), and `out/rpcs3-xmb-cap/*` is a settings page-open run, not a Triangle menu.

## A7. Open/close animation and timing

**RCO:** `explore_plugin_full.rco` has **0 animation entries** (`work/rco-tree-dump.ts --list`:
`explore_plugin_full.rco objects=87 anims=0`; `resources.json` `animations: []`). The options menu's
motion is therefore code-driven, not authored in the RCO - unlike `system_plugin.rco`'s button hints,
which do have `anim_cross_show/hide` as `Fade` entries with `durationRaw = 100.0`, `accelMode = 0`.

**CODE:** on creation of each of the first two menu instances the plugin sets a property with the same
constant set:

```
explore_plugin_all.c:37320-37327 (FUN_0005ee6c)
  piVar3 = E21C3432(0,1);  (*(vtable+0xf4))(DAT_002709e4, piVar3, 100, 7, 0x47d);
  piVar3 = E21C3432(0,0);  (*(vtable+0xf4))(DAT_002709e4, piVar3, 100, 7, 0x47d);
```

- `DAT_002709e4` decodes to float `0.0` in `explore_plugin.prx` (program header 0 maps vaddr to file
  offset `+0xF0`; `work/prx-va.ts`), so the leading double is not a duration.
- The third argument is the literal `100`, and `100.0` is the value the firmware uses as its widget
  animation default (`vsh.elf` .data 0x68eec0 / 0x68eec8 = 100, paired with 0x68eec4 / 0x68eecc = 250,
  i.e. the 100/250 fast/slow pair).
- The fourth argument `7` and fifth `0x47d` are a property id and value; the same call shape appears
  elsewhere in the same module as `(...,100,4,0)`, `(...,100,5,0x1f5)`, `(...,100,0,0x15)`
  (`:20747`, `:20750`, `:20753`) with different property/value pairs, which is consistent with
  "animate property N to value V" but **UNATTRIBUTED** as to which property 7 is and what 1149 means.

**Close:** `FUN_0005e94c` (`:37212-37246`) is the teardown: find `expage_menu`, loop-destroy the menu
items (`FUN_0005e018`), then destroy the page's widget. No fade/duration is passed - closing is a
destroy, not an animation.

**What this means for the native:** the native slides the panel in over `moveMilliseconds` = 200 ms
(`src/view.h:45`; `optionsOpenEase_.retarget(1, moveMilliseconds)` at `src/view.cpp:690`; the same 200 ms
serves every focus move, per `audit/OPTIONS-MENU.md:81` "slides in from the right over 200 ms"). The
firmware evidence points at a 100 ms property animation on each menu plate, so the open should be about
half the current duration. The close already matches in kind: the native draws the panel only while
`shell_.optionsOpen()` (`view.cpp:1410`), so a close removes it on the next frame rather than sliding it
out, which is what the firmware's page destroy does.

## A8. Sounds

The firmware's menu cues are nine `VAG` entries in `system_plugin.rco`'s sound table (there is no
`snd_*.rco`); the table, its offsets and the decoded waveforms are already documented with line-level
evidence in `audit/INTERACTION-AUDIO.md` sections 1-2. For this surface:

| firmware event | cue (RCO label) | evidence |
|---|---|---|
| open the options list | `snd_option` | `INTERACTION-AUDIO.md` section 2 (label name; **no decompiled call site** - weaker evidence, stated there) |
| move between rows | `snd_cursor` | `explore_plugin_all.c:9245`, `:9318` |
| confirm a row | `snd_decide` | `explore_plugin_all.c:33256`, `:34446`, `:34899`-`:34914` |
| Circle / close | `snd_cancel` | `explore_plugin_all.c:26546` |
| a row that cannot be chosen | `snd_system_ng` | `INTERACTION-AUDIO.md` section 2 (`snd_error` is all-zero samples) |

`snd_option`, `snd_cursor`, `snd_decide` and `snd_category_decide` decode to the **same 39.1 ms
waveform** (`INTERACTION-AUDIO.md` section 1), so "open" and "move" are audibly identical on hardware.
The native already maps open/move/confirm/close/error to Options/Move/Confirm/Back/Error
(`src/shell.cpp:1677`, `:1688`, `:1714`, `:1789`, `:1699`), which is correct in kind; only `snd_option`'s
assignment is weak evidence and nothing here contradicts it.

---

# Part B - the information panel and its indicators

## B1. The object tree (RCO, exact)

`vsh/resource/xmb_plugin_normal.rco` page `page_xmb_indicator`, and `vsh/resource/xmb_ingame.rco` which
carries the identical page in game:

```
page_xmb_indicator                 Page  0x801 @4068
  indicator                        Plane 0x802 @4144   pos 0,0     alphaScale 0   (anchor; hidden by default)
  indi_base                        Plane 0x802 @4284   pos 0,-74   560x82   image tex_indi_bg (24x24 tile)
  indi_adjust                      Plane 0x802 @4424   pos -26,0   alphaScale 0   (icon group anchor)
    clock                          Text  0x80d @4564   pos 0,-12   size 10  white  shadow 4,-5 @0.25
    message                        Plane 0x802 @4836   pos -244,0  32x32    image tex_indi_message
    chat                           Plane 0x802 @4976   pos -274,0  32x32    image tex_indi_chat
    online                         Plane 0x802 @5116   pos -342,0  32x32    image tex_indi_online
      online_num                   Text  0x80d @5256   pos +14,+2  size 10  white
    avatar                         Plane 0x802 @5528   pos -376,0  32x32    (no RCO image)
    plus                           Plane 0x802 @5668   pos -410,0  32x32    image tex_indi_plus
```

That is the whole top-right inventory for the XMB: **one plate, one text clock, five 32x32 icon slots and
one counter text**. Notes on each:

- `indicator` and `indi_adjust` are both authored with `alphaScale = 0` and `redScale = greenScale =
  blueScale = 0`, i.e. the whole indicator group is invisible until something turns it on. `xmb_plugin.prx`
  does not do that: `FUN_0000cb44` (`xmb_plugin_all.c:6236-6294`) touches only `indicator` (one call),
  `indi_base` (sets property `0x23` = 35), `clock`, and reads `online_num`'s property `0x39`. The alpha
  that makes the icons visible is set outside this module - the same missing `vshcommon` region as the
  clock writer (`audit/INFO-BAR.md` section 2, `vsh.elf` 0x33FB58).
- `avatar` has no image in the RCO: the 32x32 box is filled with the signed-in user's avatar at runtime.
- `plus` is the PlayStation Plus indicator: the exported texture (`work/tex/xmb_plugin_normal/tex_indi_plus.png`)
  is the yellow plus with the four shape symbols.
- `online` is the slot a friend/online count would use: the object name says online, its texture is a
  smiling face, and it is the only icon with its own counter text (`online_num`, +14/+2 inside the 32x32
  box, size 10). Which count it carries is runtime state, so that mapping is an inference from the name
  and the counter, not a decoded condition.

**There is no trophy, download, What's New, analog-clock, or busy object in this page.** The grep for
indicator-like object names across all 122 resources returns only: this page (in both XMB RCOs),
`game_indicator_plugin.rco` `page_indicator` {`button_indicator`, `text_indicator`}, `impose_plugin.rco`
`page_btnnavi` {`busy_icon`, `busy_icon_shadow`} + `impose_clock` {`clock`}, `system_plugin.rco`
`page_busy` {`busy_icon_shadow`, `busy_icon_anim1`, `busy_icon` {`busy_short`, `busy_long`}}, and the
settings `Clock` objects in `sysconf_plugin.rco` (already documented in `audit/INFO-BAR.md` section 1).

## B2. Textures, with sizes read from the RCO image tables

`bun work/rco-table-dump.ts <rco> --images` (GIM headers decoded the same way `src/assets.cpp` decodes
them):

| texture | RCO | size | use |
|---|---|---:|---|
| `tex_indi_bg` | `xmb_plugin_normal.rco` (also `xmb_ingame.rco`) | 24x24 | the info-bar plate tile, stretched to `indi_base` 560x82 |
| `tex_indi_message` | same | 32x32 | `message` slot |
| `tex_indi_chat` | same | 32x32 | `chat` slot |
| `tex_indi_online` | same | 32x32 | `online` slot (a smiling face) |
| `tex_indi_plus` | same | 32x32 | `plus` slot (PS Plus) |
| `tex_busy`, `tex_busy_s`, `tex_busy_l`, `tex_busy_shadow` | `system_plugin.rco` | 36x36, 8x36, 8x36, 46x46 | the busy indicator (`page_busy`) |
| `tex_cross`/`tex_circle`/`tex_triangle`/`tex_rectangle` (+`_shadow` 40x40) | `system_plugin.rco` | 30x30 | button hints (`page_btnnavi`) |
| `tex_infobar` | `system_plugin.rco` and `premo_plugin.rco` | **1x125** | the *notification* bar's vertical gradient - not the XMB clock plate |
| `tex_bar_time_icon`, `tex_bar_time_shadow_icon` | `sysconf_plugin.rco` | 46x46 | clock+arrow icon, a settings-row glyph (exported: `work/tex/sysconf/tex_bar_time_icon.png`), not the info-bar glyph |

**`tex_indi_bg` is a 9-slice plate tile, and its pixel content explains the measured frame.** Pixel
profile of the 24x24 GIM (RGBA, printed in this pass):

```
horizontal profile at y=12 (identical at x=12 for the vertical, i.e. symmetric on all four sides):
  x=0  alpha 0                        fully transparent 1 px margin
  x=1  alpha 89  RGB 255 255 255      white
  x=2  alpha 77  RGB 222 222 222
  x=3  alpha 59  RGB 207 207 207
  x=4  alpha 43  RGB 190 190 190
  x=5  alpha 31  RGB 156 156 156
  x=6  alpha 22  RGB 116 116 116
  x=7  alpha 16  RGB  48  48  48
  x=8..15 alpha 13 RGB 0 0 0          interior: 5.1% black
```

It is a symmetric 9-slice: 8 px border on every side and an 8x8 stretchable middle, so stretching it to
560x82 (or to the measured 591x58) keeps the 8 px edge ramp and grows only the black interior.

So the firmware plate is a **5% black fill with a soft white edge ramp on the top, bottom and left** -
and the measured 1080p frame says exactly that (B3). The native draws the opposite polarity (white fill)
and with the wrong asset (B4).

## B3. Measured 1080p geometry (FRAME)

Method: `out/native-reference/ab-rpcs3-root.png` (1920x1080, the saved 4.93 firmware root frame) read
pixel-by-pixel with `System.Drawing`; thresholds stated per measurement.

| element | measurement | detail |
|---|---|---|
| plate | x **1329..1919**, y **75..132** = **591x58** (luma >= 88) | edges are ramps, not 1 px lines: left 142 at x1329 -> 76 at x1340; top 136 at y75 -> 75 at y80; bottom 134 at y132 -> 68 at y126 |
| plate interior | luma 74..77 vs background 81 outside | interior is *darker* than the frame: the 5% black fill of B2 |
| plate right edge | none - the plate runs to x 1919 | the plate is flush with the screen edge |
| clock text | x **1592..1760** (169 px), y **94..115** (22 px cap height) | string in this frame is `25/10 12:32` (DD/MM, 24-hour) |
| analog clock glyph | **32x32** at x 1774..1805, y 88..119, centre **(1789.5, 103.5)** | ring outer diameter 32, stroke ~4 px, hands drawn as one nearly-straight stroke (12:32 puts hour and minute 176 deg apart) |
| gap between text and glyph | 1760 -> 1774 = 14 px | |

The 591x58 plate matches `audit/INFO-BAR.md:3`'s (1329,75,591,58) exactly, so that measurement is
re-confirmed independently. The RCO's authored box for the same object is 560x82 (`indi_base`), i.e. the
runtime box is 31 px wider and 24 px shorter than the authored one. That difference is **UNATTRIBUTED**:
either the plugin resizes `indi_base` before drawing, or the plate is the tile's visible core inside a
larger authored box (the tile's own 8 px edge ramp does not account for 31/24).
The capture that settles it is the same one A6 needs, plus a second grab with a longer date format
(`MM/DD/YYYY`, which the console offers) to see whether the plate grows with the string.

**The analog clock is drawn, not an RCO object or texture.** There is no Clock object in either XMB RCO
(`audit/INFO-BAR.md` section 1), no clock texture in any of the 122 RCOs (`--rco-images` sweep: the only
clock-ish assets are `sysconf_plugin:tex_bar_time_icon` 46x46, which is a different glyph, exported to
`work/tex/sysconf/`), and the theme's 63 icon ids (`--p3t-dump`, `work/theme/elements.txt`) contain only
`icon_datetime_setting`. The glyph therefore belongs to the same missing `vshcommon` code that writes the
`clock` Text, and its measured box above is the only usable specification.

## B4. What the native draws, and how it differs

`src/view.cpp:1419-1434`:

```cpp
const QRectF infoBar(1329,75,591,58);                                  // 1421 - matches the frame
p.setBrush(QColor(0,0,0,20)); p.drawRoundedRect(infoBar,4,4);          // 1422 - 7.8% black fill, radius 4
p.setOpacity(.12); p.drawImage(infoBar, assets_.image("system_plugin","tex_infobar")); // 1423 - WRONG ASSET
p.setPen(QPen(QColor(255,255,255,90),1)); p.drawRoundedRect(...);      // 1424 - 1 px white border, all four sides
text({1240,82,525,45}, shell_.clockText(), 27, Qt::white, AlignRight|AlignVCenter);   // 1425
const QPointF center(1791,104); ... drawEllipse(c,14,14); drawLine(minute 10, hour 7), pen 2.7   // 1426-1433
```

Deltas against B3, in the order they matter:

| # | native | firmware | delta |
|---|---|---|---|
| 1 | plate texture = `system_plugin:tex_infobar` (1x125 vertical gradient, the *notification* bar asset) | `xmb_plugin_normal:tex_indi_bg` (24x24 9-slice plate tile) | wrong asset and wrong construction; the 1x125 gradient cannot produce a bordered box |
| 2 | fill white 12% over black 7.8% (net brighter than the frame) | interior black 5.1%, edges white ramps | polarity inverted; measured interior must be *darker* than the frame (74..77 vs 81), the native's is brighter |
| 3 | 1 px white border on all four sides, corner radius 4 | white ramps on top/bottom/left only, no right edge (flush) | right edge and corner shape are wrong |
| 4 | text right edge 1765 | 1760 | +5 px; residual is font advance (already documented in `audit/INFO-BAR.md`) |
| 5 | analog clock ring r=14 (28 px), stroke 2.7, hands 10/7 | 32 px outer (r=16), stroke ~4, hands ~13/10 | ring 4 px small, stroke 1.3 px thin, hands short |
| 6 | glyph centre (1791,104) | (1789.5,103.5) | 1.5 px / 0.5 px |
| 7 | no indicator icons at all | five 32x32 slots at fixed offsets from the clock (B1) | whole indicator row missing |
| 8 | plate hidden on an information page (`if(!infoPage)`, `view.cpp:1419`) | no evidence of that condition | native-only rule |

## B5. Which objects appear under which conditions

What is provable locally, and what is not:

| object | proven condition | evidence |
|---|---|---|
| `indi_base` plate | always (in the captured root frame) | FRAME: plate present with no PSN sign-in, no media |
| `clock` text | always, rewritten once per displayed minute | `xmb_plugin_all.c:6271` writes it; writer is `vsh.elf` 0x33FB58 (`audit/INFO-BAR.md` section 2-3); FRAME shows it |
| analog clock glyph | always, same frame | FRAME (B3) |
| `message`, `chat`, `online`, `avatar`, `plus` | **all hidden** in the captured root frame | the group's alpha is 0 in the RCO (B1) and the frame shows no icons; nothing in `xmb_plugin.prx` raises it |
| the individual conditions (unread message, chat invite, friends online > 0, signed-in avatar, PS Plus member) | **UNATTRIBUTED** - the policy lives in the missing `vshcommon`/`vsh.elf` code | not in any local decompile, not in any local capture |
| busy indicator | not in the XMB indicator page at all; it is `system_plugin.rco page_busy` (`busy_icon` + `busy_icon_anim1` + `busy_short`/`busy_long` 8x36 bars) | RCO tree; textures `tex_busy*` |

**The condition set the native documents does not match the firmware's object set.** The brief (and
`audit/INFO-BAR.md`) speaks of clock + friends/message/trophy/download/What's New + busy. The firmware's
five slots are message, chat, online (with a count), avatar and plus; there is no trophy slot, no
download slot and no What's New slot in the indicator page, and the busy icon is a separate page owned by
`system_plugin`. The textures that look like "What's New" (`explore_plugin_full:tex_new_ws`,
`tex_urgent_ws`, `tex_check_ws`, 74x74) and the `tex_go_*` badges (84x49) exist, but no object in
`page_xmb_indicator` references them, so they belong to another surface (the What's New tile page), not
to the clock bar.

## B6. Change animations

- **Clock text.** No animation object. The text is written by `vshcommon_85AD7A58` from the plugin's
  display state (`xmb_plugin_all.c:6271`); the per-minute refresh is inside `vsh.elf` 0x33FB58, which is
  not in the local artifact set. The reference frame is minute-resolution and no captured frame shows a
  seconds field, so the native's per-minute invalidation is a harmless superset (same conclusion as
  `audit/INFO-BAR.md` sections 3 and 5).
- **Icons.** No animation entries exist in `xmb_plugin_normal.rco` (`--list`: `anims=0`), so any
  appear/disappear fade is code-driven; nothing in the artifacts gives its duration.
- **Busy icon.** `system_plugin.rco` `page_busy` carries `busy_icon_anim1` (a second plate) plus
  `busy_short`/`busy_long` 8x36 children of `busy_icon`, and the resource has 17 animation entries
  (`--list`). The two 8x36 bars are the rotating busy bars; the animation entries are the only authored
  motion for this indicator in the whole dev_flash.
- **Button hints.** `system_plugin.rco` `anim_cross_show`/`anim_cross_hide` (and the circle, triangle,
  rectangle pairs) are `Fade` animations, `durationRaw = 100.0`, `accelMode = 0`, target 0.0 or 1.0 -
  i.e. the firmware's own hint fade is 100 (ms under paf's convention, matching the 100.0 default pair at
  `vsh.elf` 0x68eec0/0x68eec8), with linear acceleration.

## B7. What the firmware does when crowded

There is no layout response in the object tree to be crowded *with*: the five icons sit at fixed x
offsets (-244, -274, -342, -376, -410) inside a fixed 560-wide plate, and the clock Text has no width, so
it grows around its origin and the icons do not move. The only mechanisms the artifacts leave are
(a) hiding slots at runtime, or (b) drawing a second row, and neither is present in the RCO. So:

- **Proven:** the layout cannot reflow. Icon positions are constants, and the plate's width is a plane
  attribute.
- **UNATTRIBUTED:** whether the runtime drops the lowest-priority icon, and in what order. That needs a
  capture with several indicators live at once - see the capture list in section D.

---

# Part C - the concrete native fix list

Ordered by evidence strength, not by effort. Every item names the function to change and the RCO object
or string to read at runtime instead of the hardcoded value.

## C1. Options menu

| # | Change | File / function | Runtime-read source |
|---|---|---|---|
| 1 | Label every row from the RCO text table instead of English literals: `Open`, `Play`, `View`, `Start`, `Information`, `Sort By`, `Display All`, `Group Content`, `Add to Playlist`, `Copy`, `Copy Multiple`, `Delete`, `Delete Multiple`, `Remove`, `Remove Disc`, `Play from Beginning`, `Resume`, `Cancel` | `src/shell.cpp` `buildOptions()` (1624-1642), `activateLabel()`, `optionsActivate()` (SortMenu rows at 1719-1721) | `assets_.text("explore_plugin_full", key)` with the keys in A3 (`msg_open`, `msg_play`, `msg_display`, `msg_game_start`, `msg_info`, `msg_sort`, `msg_display_all`, `msg_folder_class`, `msg_add_to_playlist`, `msg_copy`, `msg_copy_plural`, `msg_del`, `msg_del_plural`, `msg_eject`, `msg_disc_eject`, `msg_play_begin`, `msg_resume`, `msg_cancel`). `Assets::text` already exists and is already used this way at `src/view.cpp:1460`. |
| 2 | Delete the `Close` row (`shell.cpp:1641`). The firmware has no such label and closes on Circle. If a keyboard-only affordance is still wanted, keep the key binding (`Escape`, `view.cpp:1888`) and drop the row | `buildOptions()` | A3 (no `Close` key in the 861-entry table) |
| 3 | Replace the date-pair sort submenu with the firmware's own labels (`Creation Date ^/v`, `Import Date ^/v`, `Update Date ^/v`, `Release Year ^/v`, `Film Date ^/v`, `Earned Date ^/v`) and add the group labels (`By Title/Album/Artist/Genre/Format/Month/Year/Track`) where a surface can actually group | `optionsActivate()` `OptionCommand::SortMenu` (1717-1726) | `explore_plugin_full` text keys from A3 |
| 4 | Row order: keep the category's own command actually at the top and `Information` last (firmware invariant, A4); the per-category conditions are the ones in the A4 table (`Copy` only when the row is copyable, etc.) | `buildOptions()` | A4 |
| 5 | Draw the focus as a **plate** (`BG1`) that travels between rows, not as a text glow; drop the 23 px halo at `view.cpp:1143` for this panel (keep it for the settings choice column until that surface is measured) | `src/view.cpp` `drawOptionsPanel()` (1108-1160) | A2 (`*_BG1` per level), A6 |
| 6 | Draw a panel background plate (`BG0`) sized to the rows instead of a full-height strip from 1330 to 1920 | `drawOptionsPanel()` lines 1122-1125 | A2 |
| 7 | Panel geometry: take the numbers from the capture in A6 once it exists; until then this is the one remaining hardcoded block (`x0=1330`, pitch 40, 23 px, first row 505-(n-1)*20) and must be marked as an approximation in the source comment | `drawOptionsPanel()` 1110-1149 | A6 capture; `tex_opt_obi` (660x6) is the only authored width hint |
| 8 | Open timing: ~100 units (the firmware's widget animation default) instead of 200 ms (`view.h:45`). Close stays an instant removal, as it already is | `src/view.h:45` (`moveMilliseconds`), `:110` (`optionsOpenEase_`/`optionsPanelEase_`); `src/view.cpp:689-693`, `:1410` | A7 |
| 9 | A second level must be its own plate box (levels 1-2 are 128x272 in the RCO and independently positioned), not a heading plus replaced rows in the same strip | `drawOptionsPanel()` heading branch 1129-1132; `Shell::optionsActivate()` SortMenu nesting 1718-1726 | A2, A7 |
| 10 | Confirmation pages instead of nested confirmation rows: `Delete` and `Copy` rows currently do not exist; when they land, they open a page (`Page::transfer`-style) shaped after `expage_fdelete_dialog` / `expage_fcopy_dialog` (list base + icon + info + msgdialog + select-all/deselect-all/decide buttons + name-exists buttons) | new page code in `shell.cpp`; the existing `Page` model at `shell.h` | A5 |
| 11 | No scroll indicator in this menu (do not add one) | - | A2 (no Scroll object) |
| 12 | Keep the sound mapping (Options on open, Move on move, Confirm on choose, Back on close, Error/`snd_system_ng` on a disabled row) | `shell.cpp:1677`, `:1688`, `:1714`, `:1789`, `:1699` | A8 |

## C2. Information panel

| # | Change | File / function | Runtime-read source |
|---|---|---|---|
| 1 | Use the firmware plate: 9-slice `xmb_plugin_normal:tex_indi_bg` (24x24) into the 591x58 rect, instead of `system_plugin:tex_infobar` | `view.cpp` 1422-1424 | B2 (`tex_indi_bg` pixel profile), B3 |
| 2 | Correct the material polarity: interior = black at alpha 13/255 (5.1%), edges = white 8 px ramps on top/bottom/left; no right border (flush to 1920); corners come from the tile's 8 px border (1 px fully transparent outermost row/column, then a 7 px white-to-black alpha ramp), not a 4 px radius | `view.cpp` 1421-1424 | B2, B3 |
| 3 | Analog clock: ring outer 32 px (r 16), stroke ~4, hands ~13/10, centre (1789.5, 103.5) | `view.cpp` 1426-1433 | B3 |
| 4 | Clock text slot: keep the measured right edge 1760 rather than 1765 and the 22 px cap height (27 px `pixelSize` is 3.5% wide; a 26 px size or an explicit advance fix is the open question - `audit/INFO-BAR.md` already records the 6 px residual as font advance) | `view.cpp:1425` | B3 |
| 5 | Add the indicator row: five 32x32 slots at fixed offsets from the clock, right-to-left: `plus` -410, `avatar` -376, `online` -342 (+`online_num` at +14/+2), `chat` -274, `message` -244, all inside the 560x82 `indi_base` | `view.cpp` (new draw block beside 1425) | B1 |
| 6 | Bind the slots to what this build actually knows: message count -> `tex_indi_message`, friends online -> `tex_indi_online` + `online_num`, PS Plus -> `tex_indi_plus`; leave `chat` and `avatar` undrawn until there is state for them (an empty slot is honest; a slot with a fake count is not) | `Shell` accessors feeding the new draw block | B1, B5 |
| 7 | Do not draw trophy/download/What's New icons in this row: those objects do not exist in `page_xmb_indicator` | - | B5 |
| 8 | Busy indicator: draw `system_plugin:tex_busy` (36x36) + `tex_busy_s`/`tex_busy_l` (8x36 bars) + `tex_busy_shadow` (46x46) at the top-right when `Shell::busy()`, instead of the "Loading..." text inside the media overlay (`view.cpp:1191`) | new draw block; `Shell::busy()` | B1, B2, B6 |
| 9 | Button hints: use the 30x30 `tex_cross`/`tex_circle`/`tex_triangle`/`tex_rectangle` (the native uses `tex_circle` at 24x24, `view.cpp:1459`) and the 100 ms linear fade for show/hide | the info/dialog pages that draw hints (`view.cpp:1459-1460`, `:1472`) | B2, B6 |
| 10 | Remove the native-only rule that hides the plate on an information page (`if(!infoPage)`, 1419) unless a capture shows it; the firmware has no such condition in the object tree | `view.cpp:1419` | B4 row 8 |

## C3. Things the native must *stop* claiming

- `audit/OPTIONS-MENU.md:47` and `:475`: the firmware's eject label is `Remove Disc` / `Remove`, and the
  firmware has no `Close` row (A3).
- `audit/OPTIONS-MENU.md` section 6.4 "same visual language as the settings panel": the firmware's item
  menu is a plate-backed floating box with a travelling focus plate (A2), not the settings choice strip.
- Any statement that the info bar shows trophy, download or What's New indicators: the XMB indicator page
  has five slots named message/chat/online/avatar/plus (B1, B5).
- `audit/INFO-BAR.md:5` "the analog clock ... procedural clock are measured approximations, not traced
  firmware materials or geometry": the glyph's geometry *is* now measured (B3, 32x32 at 1789.5/103.5);
  what remains untraced is only the code that draws it.

---

# Part D - evidence index and the captures that would close the gaps

## D1. Evidence paths used in this note

| Evidence | Path |
|---|---|
| RCO object trees, 122 resources / 8,070 objects | `out/native-reference/rco-behavior/resources.json`; reader `work/rco-tree-dump.ts` |
| RCO image tables with GIM dimensions, English text tables | `work/rco-table-dump.ts` (new this pass), e.g. `--images`, `--text` |
| Full image-name inventory of `explore_plugin_full.rco` | `work/explore-full-images.txt` (245 images) |
| `xmb_plugin_normal.rco` image inventory (7 images) | `work/xmb-normal-images.txt` |
| Clock/indicator/busy image sweep across all 122 RCOs | `work/clock-indi-images.txt` |
| Exported indicator textures (local, not committed) | `work/tex/xmb_plugin_normal/`, `work/tex/system_plugin/`, `work/tex/sysconf/`, `work/tex/explore_plugin_full/` |
| `tex_indi_bg` pixel profile | `work/tex/indi_bg-zoom.png` (8x zoom) |
| Firmware info-bar crop and graph | `work/fw-infobar.png` (720x140 from 1200,40), `work/fw-clockicon.png` (8x zoom of 1760,80 60x50) |
| Native info-bar crop | `work/native-infobar.png` (same region, from `out/native-reference/options/options-open.png`) |
| Native options panel crop | `work/native-options-panel.png` (from `out/native-reference/options/options-open.png`) |
| `explore_plugin` decompile | `out/native-reference/explore_plugin_all.c` (menu builder 34238-34353, array 60724-60997, animation calls 37320-37327, teardown 37212-37246) |
| `xmb_plugin` decompile | `out/native-reference/xmb_plugin_all.c:6236-6294` (indicator page, clock, online_num) |
| paf menu class | `out/native-reference/paf-range.c:49184-49313` |
| Sound table + cue map | `native-xmb/audit/INTERACTION-AUDIO.md` sections 1-2; `out/native-reference/explore_plugin_all.c:9245,9318,26546,33256,34446,34899-34914` |
| Layout convention, widget-default float block | `native-xmb/audit/LAYOUT-TRACE.md` rows 1-7 and phase 4b |
| Firmware root frame | `out/native-reference/ab-rpcs3-root.png` |
| Native captures of the two surfaces (all native, no firmware) | `out/native-reference/options/options-open.png`, `options-info.png`, `sort-menu.png`, `settings-panel.png`; `out/native-reference/ps-menu/*`; `out/native-continue/b3-options.png`; `out/native-jf1-evidence/captures/15-options.png`; `out/smoke-options.png` |

## D2. Existing captures under `C:\dev\out` and what they do *not* show

`C:\dev\out\rpcs3-xmb-cap\` is the only firmware capture set in the tree: `root-b.png`,
`settings-b.png`, `page-open.png`, `page-closed.png`, `nav-settings.png`, `nav-left1.png`, plus
`burst-png/` and `burst2-png/` (transition bursts) and `fp-open/` (decoded `XMB_FP_DUMP`). It is a
**Settings page-open** run: no Triangle menu, no info-bar indicators other than the clock. That is why
every options-menu geometry line in A6 and every indicator condition line in B5 is marked
UNATTRIBUTED - the frame does not exist yet, not merely unread.

`out/native-reference/ab-rpcs3-root.png` is the only firmware frame of the info bar, and it is an
unsigned-in root state: plate + clock text + clock glyph, no icons. That is the whole FRAME evidence for
Part B beyond the RCO.

## D3. The captures that would prove what is missing

Each is a single RPCS3 run at 1920x1080 with a scripted input, and each closes a named gap. None was run
in this pass (the rig belongs to another agent).

| # | Capture | Closes |
|---|---|---|
| 1 | XMB root, Game column, one installed game focused, Triangle, settle 1 s, grab; then Down x4 with a grab per step | A6 in full: `BG0` box, `BG1` rect, row pitch, text left edge, cap height, focus travel per step |
| 2 | Same, then Triangle again (close), 5 frames at >= 30 fps | A7: whether close animates at all, and the open duration measured against the 100 ms default |
| 3 | Same, then Cross on `Information` | A5: the `info_list` page's real geometry, and `Information`'s page shape |
| 4 | XMB root with PSN signed in, one friend online, one unread message, a background download running; grab the top-right 700x150 | B5/B7: which of message/chat/online/avatar/plus appear, at what alpha, and whether anything hides when all five are eligible |
| 5 | Same as 4 but with the busiest state (trophy earned + download + message) then a busy operation (e.g. start a copy) | B6/B7: busy icon position/size/timing, and the crowding policy |
| 6 | XMB root, Date Format = `MM/DD/YYYY`, Time Format = 12-hour | B3: whether the plate grows with the clock string (the 591x58 vs 560x82 question) |

## D4. Reproduction for this pass (local, read-only)

```
bun work/rco-tree-dump.ts --res explore_plugin_full.rco --fields
bun work/rco-tree-dump.ts --res xmb_plugin_normal.rco --fields
bun work/rco-table-dump.ts <dev_flash>/vsh/resource/explore_plugin_full.rco --images
bun work/rco-table-dump.ts <dev_flash>/vsh/resource/explore_plugin_full.rco --text     # 861 English strings
bun work/rco-table-dump.ts <dev_flash>/vsh/resource/xmb_plugin_normal.rco --images
bun work/grep-indi.ts                                             # indicator/busy/clock objects, all 122 RCOs
bun work/vsh-data-floats.ts                                       # the .data widget-default float block
bun work/prx-va.ts <dev_flash>/vsh/module/explore_plugin.prx      # vaddr->file offset, decodes DAT_002709e4
re-ps3-native --dev-flash <dev_flash> --export-image "xmb_plugin_normal:tex_indi_bg@work/tex/xmb"
re-ps3-native --dev-flash <dev_flash> --p3t-dump "<dev_flash>/vsh/resource/theme/01.p3t@work/theme"
```

Frame measurements were taken with `System.Drawing` on `out/native-reference/ab-rpcs3-root.png`
(bright-pixel bounding boxes at luma thresholds 88 / 170 / 200, and per-column/per-row luma profiles);
the crops kept for review are under `work/`.

---

---

# E. Dated correction - 2026-10-27/28 capture batch

Evidence added after Parts A-D were written: one RPCS3 session of the real 4.93 XMB at 1920x1080
(`C:\dev\out\rpcs3-ref-2026-10-28\`, `MANIFEST.md`), 135 stills and 9 full-frame bursts. This part marks
the parts of A5, A6, A7 and B3 the batch resolved, and the parts it did not. Nothing above is deleted;
each old entry keeps its 2026-10-27 text. Frame paths are relative to the capture root.

## E1. A6 - the options menu geometry, measured

State: XMB root, Game column, `Corrupted Data` focused, Triangle. Frames: `dialogs/options-open-01.png`
(settled), `options-down-01.png` ... `-04.png` (Down x4 with a grab per step), `options-open-03.png`
(state check), `options-closed-03.png` (closed).

| quantity | measured | note |
|---|---|---|
| menu surface | full-height vertical gradient band; left edge x ~1330, fading to transparent by x ~1690 | the XMB behind is dimmed. The luma at y=30 is 0 for x 1180..1300, 50 at x 1340, 2 at x 1700. This is not a plate sized to the rows |
| rows | `Start`, `Delete`, `Information`; focused first row `Start` | the focused row is bright; `Delete` and `Information` are dim |
| row text left edge | x ~1334 | measured run start on the settled frame |
| row ink bands | y 538..562, 581..598, 621..638 | luma >= 120 over x 1340..1900 |
| row baselines / pitch | ~560, 598, 636 -> **pitch 38 px** | the original A6 question (row pitch) is closed; 38 px agrees with the 37-38 px the manifest reports |
| Down x4 | focus reaches `Information` and stays there (no wrap); the row set does not scroll | `options-down-01..04.png` |

Two corrections to the record:

- The absolute row y in `MANIFEST.md` section 6 is 479/516/554; a direct scan of the stills it cites puts
  the baselines at 560/598/636, i.e. a constant 82 px lower. The pitch (37-38) is common to both, and the
  frame value is used here. The frame is self-consistent with the rest of the batch (the info-bar plate
  is at y 75..132 in the same frames).
- A2's inference that the menu is "a plate-backed floating box, not a full-height translucent strip" is
  **not what the frame shows**: the surface touches y 0 and y 1079. The RCO facts in A2 stand (three menu
  levels, `BG0`/`BG1` per level, the 128x272 nested boxes); the *screen shape* is a full-height band with
  a horizontal fade, and C1 items 5-6 should be read with that correction.

Still unmeasured after this batch (A6's original list, minus the pitch and the first row):

- the `BG1` highlight rectangle (it is not separable from the band's gradient in these frames);
- the text cap height and the row paddings;
- the panel's top/bottom (it is the full frame height, so there is no box edge to measure).

## E2. A7 - open and close motion

Frames: `dialogs/options-open-burst2/` (108 frames, 1.977 s, 54.1 fps; the key is pressed at t=500 ms) and
`dialogs/options-close-burst2/` (110 frames, 1.990 s, 54.8 fps; the key is pressed at t=500 ms).

Measured:

- **Open**: `options-open-burst2/f_0000692.png` is ~192 ms after the key and the band is at partial
  alpha (the XMB behind is visible through it). The settled frame of the same burst (`f_0002008.png`)
  has the full row set.
- **Close**: `options-close-burst2/f_0000610.png` is ~110 ms after the key and the band is still
  partially visible; by `f_0002008.png` it is gone.
- The open burst's mid-alpha frame also shows `Sort By` and `Group Content` above the three rows, which
  the settled frame does not. The fade therefore composites more than the final row set, and the burst
  cannot be read as a clean single-surface alpha curve.

What this does to A7: the old inference was "the open should be about half the current duration
[100 units] and the close is an instant destroy, not an animation". The first half is **not supported**:
the open is still visibly at partial alpha ~190 ms in, so a ~100 ms fade is too short. The second half is
**contradicted**: the close is a visible fade of at least ~110 ms, not an instant removal. What is still
missing is the settle frame / duration, which the manifest does not publish; the exact open and close
durations stay open. Do **not** apply C1 item 8 (drop `moveMilliseconds` to 100) on this evidence.

## E3. A5 - the information page (`info_list`)

Frames: `dialogs/information-page-01.png`, `-02.png`. The page is the D4/D5 convention, measured at
1080p: rules at y 199 and 879 (1 px, full width), icon plate x 665..985 y ~263..442, rows `Title`,
`Parental Control`, `Created`, `Size` at ink tops 499/539/579/619 (pitch 40 px), labels right-aligned to
x 803, values left-aligned from x 844, `O Back` hint ink x 1155..1240 y 897..916, no dim, the info bar
replaced by the rules. Full table and the native deltas: `DIALOGS-FIRMWARE.md` section 8.1.

A5's page geometry is therefore closed. What the frame does not give: the page's own backdrop treatment
(alpha vs a 1 px border) and whether other `info_list` variants (file, trophy) share the same rect.

## E4. B3 - the clock plate does not grow with the string

Old entry (B3): "The capture that settles it is the same one A6 needs, plus a second grab with a longer
date format (`MM/DD/YYYY`) to see whether the plate grows with the string."

Frames: `dialogs/root-clock-mmdd-12h.png` and `-2.png` (Date Format `MM/DD/YYYY`, Time Format
`12-Hour Clock`, string `10/27 9:53 PM`), against the existing `C:\dev\xmb\out\native-reference\ab-rpcs3-root.png`
(string `25/10 12:32`, DD/MM 24-hour).

| quantity | `10/27 9:53 PM` | `25/10 12:32` |
|---|---|---|
| plate outline | left x 1329, top y 75, bottom y 132 (591 x 58, clipped by the right edge at 1919) | identical |
| text right edge | x 1760 | x 1760 |
| text left edge | x 1562 | x 1592 |
| analog clock glyph | x 1774..1805, y 88..119 | identical |

So: **the plate is fixed at 591x58 and the text is right-aligned to x 1760 in both formats; it does not
grow with the string.** The 560x82 authored `indi_base` box remains a different object, and the question
of why the runtime box differs from the authored one is still open (it was B3's "either the plugin resizes
it or the plate is the tile's visible core"). B3's 591x58 measurement and its 1760 text edge are
re-confirmed; `MANIFEST.md` section 6's "both end at x~1880" is not an edge in either frame (the glyph
ends at 1805).

## E5. D3 - status of the six capture wishes

| # | Wish | Status after the batch | Frames |
|---:|---|---|---|
| 1 | options menu open + Down x4, one grab per step | **CLOSED for pitch, first row, text column and clamp**; E1 | `dialogs/options-open-01.png`, `options-down-01..04.png` |
| 2 | Triangle again (close), >= 30 fps | **PARTIAL**: a visible fade of at least ~110 ms, exact duration not settled; E2 | `dialogs/options-close-burst2/` |
| 3 | Cross on `Information` | **CLOSED**; E3 | `dialogs/information-page-01.png`, `-02.png` |
| 4 | signed-in root with friends/message/download/avatar/plus | **OPEN**: the console is offline and has no PSN account (MANIFEST section 7 no. 1). `root-clock-mmdd-12h.png` is the unsigned-in bar only | none |
| 5 | busiest state plus a copy/busy operation | **OPEN**: the VFS holds no copyable object (MANIFEST section 7 no. 2) | none |
| 6 | `MM/DD/YYYY` + 12-hour clock, plate growth | **CLOSED**: plate does not grow; E4 | `dialogs/root-clock-mmdd-12h.png`, `-2.png` |

Item 1's "one installed game" caveat also stands: the batch used `Corrupted Data`, whose menu is the same
`expage_menu` page with the corrupt-item row set (`Start / Delete / Information`), not a game's
(MANIFEST section 7 no. 5). Every geometry value in E1 is item-independent.

## E6. Still unattributed after the batch

- Which of the five indicator objects appears under which condition, their alphas, and the hiding order
  when all five are eligible (B5/B7).
- The busy indicator's position and timing in a real copy (B6).
- The `BG1` rectangle, the menu text cap height and paddings (E1).
- The exact options-menu open/close duration (E2).
- The clock's per-minute writer (B6; PAGE-MOVE section 3 is still the state of that trace).
- The plate/anchor reconciliation (560x82 authored vs 591x58 drawn) - measured but not explained.

---

# Summary

**A. Options menu.** Supplied by `explore_plugin` as page `expage_menu` in `explore_plugin_full.rco`: three
menu levels (`plane_menu0/1/2`), each a plane with a background plate (`BG0`), a focus plate (`BG1`) and
two MLists split around the focused row (`UpperMenu`/`LowerMenu`); levels 1-2 are authored 128x272, level
0 auto-sized; rows are `MItem`s installed at runtime as a string array; no scroll object, no per-row
icons, no Close row. Labels come from the RCO text table (`msg_open`, `msg_play`, `msg_display`,
`msg_game_start`, `msg_info`, `msg_sort`, `msg_display_all`, `msg_folder_class`, `msg_copy`, `msg_del`,
`msg_eject`, `msg_disc_eject`, `msg_play_begin`, `msg_resume`, `msg_cancel`, plus the date-pair sort and
`By ...` group labels). The proven command list and its conditions are the game builder's twelve rows
(`explore_plugin_all.c:60846-60997`). Confirmations are separate pages (`info_list`,
`expage_fdelete_dialog`, `expage_fcopy_dialog`). The panel's row pitch, font size, paddings and screen
box are **not** in the RCO - they are paf's and are unattributed; the only authored numbers are the
128x272 nested boxes, the 660x6 `tex_opt_obi` band, the absence of a scroll bar, and a 100-unit property
animation at creation. Sounds: `snd_option` open, `snd_cursor` move, `snd_decide` confirm,
`snd_cancel` close, `snd_system_ng` refused.

**B. Information panel.** One page, one plate (`indi_base` 560x82 at (0,-74), image `tex_indi_bg` 24x24),
one `clock` Text (at (0,-12), size 10, shadow 4/-5 @0.25), five 32x32 icon slots inside `indi_adjust`
(alpha 0 in the RCO): `message` -244, `chat` -274, `online` -342 with `online_num` at +14/+2, `avatar`
-376 (runtime image), `plus` -410. Textures `tex_indi_message/chat/online/plus` 32x32, and `tex_indi_bg`
is a 9-slice tile whose interior is black at alpha 13/255 with white edge ramps. Measured at 1080p:
plate 1329..1919 x 75..132 (591x58), text 1592..1760 x 94..115 (22 px cap), analog clock glyph 32x32 at
centre (1789.5,103.5). No trophy, download, What's New or busy object in this page; the busy icon is
`system_plugin.rco page_busy`. Only the plate, the clock text and the clock glyph are visible in the
captured unsigned-in root frame; which of the five icons appears when is outside the local artifact set.

**C. The native's three biggest deltas.** (1) The options menu is a full-height white strip with a text
glow, drawn from the settings panel, where the firmware is a plate-backed floating box with a travelling
focus plate, closed on Circle and animated in 100 not 200 units. (2) The info bar uses
`system_plugin:tex_infobar` (the notification gradient) and inverts the plate polarity, where the
firmware stretches the 24x24 `tex_indi_bg` 9-slice with a 5% black interior. (3) The indicator row does
not exist in the native and the labels are English literals rather than `msg_*` table reads.

---

**Status of this summary, 2026-10-27/28.** Part E corrects three statements above without deleting them.
(1) On screen the options menu is a **full-height band** (left edge x ~1330, fading out by x ~1690), not
a floating box; its rows measure 38 px pitch at baselines 560/598/636; and its open/close motion is
visible at 54 fps for at least ~190 ms (open) and ~110 ms (close), so "animated in 100 not 200 units" is
not established. (2) The information page now has a 1080p frame (E3), and its rows sit 120 px left of
where the native draws them. (3) The clock plate is fixed at 591x58 and does not grow with the date string
(E4). A6's "row pitch, font size, paddings and screen box are unattributed" is now partly resolved: pitch
and first row are measured, the `BG1` rectangle, font size and paddings are still open.

---

# F. Phase 12.2 shell pass - the conditional row set and the label reads

Status: the **shell half** is implemented in `src/shell.cpp` / `src/shell.h`; the **drawing half** is recorded
in F6 as exact `view.cpp` hunks and was not applied. Nothing above is deleted; where F contradicts A this
section says so. Evidence classes are the same three as Parts A-E.

## F1. What the shell now does

`Shell::buildOptions()` builds the item menu as the firmware does - the item's own command first, optional
rows under their own conditions, `Information` last - and every row title is read from
`explore_plugin_full`'s English table through `Assets::text("explore_plugin_full", key, fallback)`, the same
read `showEntryInformation()` already uses (`shell.cpp` `msg_format`/`msg_size`/...). The old code called
`QObject::tr()` for every row. The one exception is the Continue Watching row (F3), whose command the
firmware does not have.

The label, its key and the condition that put the row in the list travel on the row
(`MenuOption::labelKey`, `MenuOption::condition`), so the panel log and a JSON dump name both:

```
shell: options opened category= video depth= 1 entry= .../Movies rows= 5 Open|Copy|Delete|Sort By|Information
shell: options rows entry= ... keys= msg_open|msg_copy|msg_del|msg_sort|msg_info
   conditions= actionable|local-content|local-content|page-sortable|always-last
shell: options eject labels msg_eject= Remove msg_disc_eject= Remove Disc
```

With `XMB_DUMP_ENTRIES=<file>` set, the same code writes `<file>.options.json`: every built row with its
key, command, condition, enabled flag and note, plus **all 27 item-menu labels the brief's string map names**
resolved from the table with a `row` boolean - so a key this build cannot use is still proven to read
correctly instead of being invisible. The `.options.json` dump is written by the shell, like the existing
`.folder.json`/`.dl.json`/`.store.json` dumps.

Rows the firmware proves but this build cannot offer are **left out, not faked**; F3 names each one and the
condition that is missing. Two firmware rows are listed but not selectable (`Copy`, `Delete`): the item is
local, so the firmware's condition holds, but this build has no copy/delete backend, and the panel's existing
disabled-row rule puts the reason on screen (`this native build does not copy files yet.`) instead of
pretending. Cross on such a row plays `snd_system_ng` (`Cue::Error`), the firmware's refused-row cue (A8).

There is **no Close row** and no `OptionCommand::Close` any more. Circle and Escape close the list through
`Shell::optionsClose()` (`view.cpp:1948`, `:2064`), which was already the behaviour; a nested Sort By list
steps back one level on Circle and has no Close row either. There is **no scroll object** and none was added;
`Shell::optionsMove()` now clamps the item menu instead of wrapping (E1: Down x4 stops on `Information`), and
a move that cannot move plays no cursor cue and writes no selection line. The in-player panel keeps its
existing wrap because its traversal has never been measured (it is a native surface, not `expage_menu`).

## F2. The text table, read directly (`RCO`)

Reproduction: `bun work/rco-table-dump.ts <dev_flash>/vsh/resource/explore_plugin_full.rco --text` (861
entries; the same reader `src/assets.cpp` uses). The keys the brief's string map names, with the values that
dump gives them (the shell resolves every one of them for the options dump, whether or not it builds a row):

| key | value | row in this build |
|---|---|---|
| `msg_open` | Open | first row, folder/store/list row |
| `msg_play` | Play | first row, playable file |
| `msg_display` | View | first row, image or information row |
| `msg_game_start` | Start | first row, game title |
| `msg_resume` | Resume | first row when a resume record exists |
| `msg_play_begin` | Play from Beginning | second row when a resume record exists |
| `msg_info` | Information | always, last |
| `msg_sort` | Sort By | local folder page with more than one row |
| `msg_copy` / `msg_del` | Copy / Delete | local content, disabled (F3) |
| `msg_created_date_time_f` / `_r` | Creation Date ^ / Creation Date v | Sort By submenu (U+25B2/U+25BC) |
| `msg_updated_date_f` / `_r` | Update Date ^ / Update Date v | Sort By submenu |
| `msg_display_all` | Display All | never built (no filtered page) |
| `msg_folder_class` | Group Content | never built (nothing is grouped) |
| `msg_add_to_playlist` | Add to Playlist | never built (no playlist backend) |
| `msg_copy_plural` / `msg_del_plural` | Copy Multiple / Delete Multiple | never built (no multi-select) |
| `msg_eject` / `msg_disc_eject` | Remove / Remove Disc | never built (no optical drive) |
| `msg_cancel` / `msg_cancel2` | Cancel / Cancel | not a row: Circle closes |
| `msg_check_for_update` | Check for Update | never built (no update backend) |
| `msg_game_update_history` | Update History | never built |
| `msg_internet_search` | Internet Search | never built (no search provider) |
| `msg_purchase` | Purchase | never built (no store purchase) |
| `msg_about_game` | About this Game | never built (no title metadata) |
| `msg_psmove_notice` | PlayStation(R)Move Safety Notice | never built (no Move hardware) |
| `msg_health_warning_psn` | Health Warnings | never built (no disc/PSN title metadata) |
| `msg_intellectual_property_notices` | Intellectual Property Notices | never built (no per-title notice data) |

A3's table stops at `msg_psmove_notice`; the last two keys are in the same table and are what the brief's
"Health Warning" and "IP Notice" rows need, so they are named here rather than guessed at.

**Two corrections proven by the same dump:**

- **There is no `Close` key.** A grep of the 861-entry table for `Close` returns nothing, so the old
  `QObject::tr("Close")` row was invented (A3 already said so).
- The eject labels are **`Remove` (`msg_eject`) and `Remove Disc` (`msg_disc_eject`)** - never "Eject Disc".
  That string exists only in this project's prose (`audit/OPTIONS-MENU.md:47`, `:475`, `:15`), not in the RCO
  and never in the shell. No eject row is built because this build has no optical drive
  (`audit/OPTIONS-MENU.md:473` says none is planned), so the panel log prints the resolved pair and the
  options dump records both labels with `row: false`. The fix is therefore in the label table and in the
  prose, and that is the whole of it - a row would need a disc backend, which the brief forbids inventing.

`Cancel` is not a row either. The confirm box already draws `msg_ok`/`msg_cancel` from
`explore_plugin_full` (`dialogs.cpp`), and Circle on the item menu is `snd_cancel`; no menu row is needed to
"cover" it.

## F3. The row set, per context (measured runs)

Every run below is the built `re-ps3-native.exe`, `--dev-flash C:/dev/rpcs3/bin/dev_flash`, a seeded
`--data-dir`, pinned clocks, and a scripted TRIANGLE; the rows are read from the `.options.json` dump and the
panel log, not from a screenshot. `condition` is the token the shell writes on the row.

| context | entry | rows (label - key - condition) |
|---|---|---|
| installed Steam title (Game > All Games > All Titles, first installed row) | `steam:2807960` Battlefield 6 | Start - `msg_game_start` - actionable; Information - `msg_info` - always-last |
| local video folder (Video > Video Library, row 0) | `...video-library/Movies` | Open - `msg_open` - actionable; Copy - `msg_copy` - local-content (disabled); Delete - `msg_del` - local-content (disabled); Sort By - `msg_sort` - page-sortable; Information - `msg_info` - always-last |
| local video file with a resume record (Video Library, row 2) | `.../Top Level Clip.mp4` | Resume - `msg_resume` - resume-record; Play from Beginning - `msg_play_begin` - resume-record; Copy - `msg_copy` - local-content (disabled); Delete - `msg_del` - local-content (disabled); Sort By - `msg_sort` - page-sortable; Information - `msg_info` - always-last |
| photo (Photo > Photo Library, row 0) | `...photo-lib/Alpha.png` | View - `msg_display` - actionable; Copy - `msg_copy` - local-content (disabled); Delete - `msg_del` - local-content (disabled); Sort By - `msg_sort` - page-sortable; Information - `msg_info` - always-last |

Conditions, and where each comes from:

- **actionable** - the entry has an action this build performs; the row is the item's own command
  (`activateLabel()`), labelled by the entry's kind exactly as A3 maps the keys (`msg_open` folder/store,
  `msg_play` playable file, `msg_display` image/information, `msg_game_start` game title).
- **resume-record** - `optionResumePoint()` is non-zero: a Jellyfin row's own `resumeMs`, or a local file
  with a `media/resume` point (including a Continue Watching row, whose record is the reason it is listed).
  The first row becomes `msg_resume` and the second is `msg_play_begin`; without a record neither appears, so
  "Play from Beginning" can never be a duplicate of row 0.
- **local-content** - `optionIsLocalContent()`: a `File`/`Folder` row whose value is an existing path on this
  PC, excluding Continue Watching rows (a resume record, not a library item). This is the build's stand-in for
  A4's copyable/not-disc condition; both rows are disabled because the backend is absent.
- **page-sortable** - the open page is a local directory with more than one row (the old Sort By gate).
- **continue-row** - a Continue Watching row. `Remove from Continue Watching` is a native command with no
  firmware row (A3 has no key for it), so it keeps a native wording and the dump gives it no `key`.
- **always-last** - `Information`, appended last in every context; A4's invariant (`msg_info` at
  `explore_plugin_all.c:60986`).

**Start.** A3 maps `msg_game_start` to "Activate (game row)", and that is what the build implements: Start is
the first-row label only for a launchable title (`Launch`/`LaunchSteam`/`LaunchApp`), never for a folder, a
file, a photo or a settings row. The firmware's own Start row is gated on a disc/BD title
(`*(char*)(state+0xd4)`, A4 row 0); **this build cannot evaluate that predicate** - it has no disc item and no
BD state - so the disc gate is not reproduced and the key is used where A3's mapping puts it. "Running" is
read as "a title that runs" (a title row), not "currently executing": under the literal reading the row would
relaunch an already-running game, which no firmware builder does.

**Rows deliberately absent** (A4 lists them, this build has no condition evaluable or no backend):

| firmware row | firmware condition (A4) | why it is out |
|---|---|---|
| Internet Search | always (game builder) | no search provider for this build's titles; a row would open nothing |
| Purchase | entitlement flag `(state+0x2e4 & 0x80000)` + `state+0x6a8` | no store/entitlement state |
| Check for Update | always | no title-update backend (Steam's own update state is not the firmware's) |
| Update History | `*(char*)(state+0x676)` | no update history exists |
| Health Warning (PSN) | disc/BD title | no disc/PSN title metadata |
| PS Move Safety Notice | `(state+0x2e4 & 0x100)` | no Move hardware or title capability |
| About this Game | `*(char*)(state+0x6aa) == 0` | no title metadata source (no SFO/PSN read) |
| Intellectual Property Notice | always | no per-title notice data |
| Display All | (list-page command) | a page already lists every row it has; nothing is filtered |
| Group Content | (list-page command) | nothing is grouped |
| Add to Playlist | (music command) | no playlist backend |
| Copy Multiple / Delete Multiple | (multi-select) | no multi-select and no copy/delete backend |
| Remove / Remove Disc | disc item | no optical drive (F2) |
| Cancel | not a row | Circle closes |

Rows the build can evaluate and does offer, but whose counts change with state: a disabled Copy/Delete adds
two rows on every local content item; a resume record adds one; a non-directory page (Steam, Jellyfin) has no
Sort By. That is the "conditional, not flat" requirement: the same panel draws 2 rows on a Steam title and 6
on a resumable local file.

## F4. Sound (verified against `audit/INTERACTION-AUDIO.md`)

The shell's existing call sites were checked, not changed blindly. Per INTERACTION-AUDIO section 2 and its
call-site evidence list:

| event | call | cue | evidence class |
|---|---|---|---|
| open the list | `toggleOptions` | Options (`snd_option`) | **guessed**: no decompiled call site; label/role inference plus a `sysconf_plugin.prx` string reference (INTERACTION-AUDIO sections 2 and 10.4). Correct in kind and unchanged. |
| move | `optionsMove` | Move (`snd_cursor`) | **trusted**: `explore_plugin_all.c:9245`, `:9318` |
| confirm a row | `optionsActivate` (Activate -> `activate()`, Information, Play from Beginning, sort choice) | Confirm (`snd_decide`) | **trusted**: `:33256`, `:34446`, `:34899-34914` |
| close / step back | `optionsClose`, `dismissOptions` | Back (`snd_cancel`) | **trusted**: `:26546` |
| refused row | `optionsActivate` disabled branch | Error (`snd_system_ng`) | **guessed-but-corroborated**: no options-menu call site; `snd_system_ng` has raw instruction references in `explore_plugin.prx` (INTERACTION-AUDIO 10.4), stronger than the label-only `snd_option` case |
| open the Sort By submenu | `optionsActivate` SortMenu | Options (`snd_option`) | **guessed**, as "open a menu" |
| open the remove-confirm box | `optionsActivate` RemoveResume | Options (`snd_option`) | **guessed**, keep (it opens a box, not a list) |

**No mismatch was found.** The three trusted cues (move, confirm, close) are on the three events the firmware's
call sites prove, and the guessed assignments are the same ones the audio audit already marks as guesses. Two
consequences of this pass: the refused-row cue is now reachable from the item menu for the first time (the
disabled Copy/Delete rows), and the new Play from Beginning row uses Confirm, the row-confirm cue. Nothing was
added to `interaction_audio.*`.

The mapping was also run, not only read: `XMB_AUDIO_LOG=1` on the verification runs gives
`cue Options source=snd_option` when the panel opens, `cue Move source=snd_cursor` on each row change,
`cue Confirm source=snd_decide` on the item's own command, `cue Back source=snd_cancel` on Escape
(`shell: options closed`), and `cue Error source=snd_system_ng` plus `shell: option unavailable Copy ...` when
the disabled Copy row is pressed.

## F5. Open timing - why the "100-unit open" is not in this pass

The brief for this pass repeats A7's "100-unit open". E2 measured that the open is still at partial alpha
~190 ms after the key and says explicitly **"Do not apply C1 item 8 (drop `moveMilliseconds` to 100) on this
evidence"**, and the close is a visible fade of at least ~110 ms, not the instant destroy A7 inferred. The
shell half does not own the animation (`view.h:45`, `view.cpp:689-698`), and the view hunks below do not touch
it. So the timing stays at the current 200 ms and this note records that the task brief contradicts E2; E2 is
the dated, measured evidence.

## F6. The measured band - `view.cpp` hunks (not applied)

The current `drawOptionsPanel` is shared by the settings choice column, the item menu and the in-player panel,
and its geometry (`x0=1330+ahead*600`, full height to 1920, first row `505-(n-1)*20`, pitch 40, label `x0+16`,
glow) is the settings-panel language, not the measured options menu. The hunk below adds one parameter,
`firmwareBand`, set only by the Triangle-menu call site, and gives that surface E1's numbers: a full-height
band from x1330 fading to nothing at x1690, row baselines `560 + 38*i`, label x1334, rows fixed as focus moves
(no scroll). It also hands the item menu its own `MenuOption` rows (the call site passed titles only before),
so the two disabled Copy/Delete rows are dimmed and their reason line is drawn, while the dark plate stays
with the in-player panel - the item menu is over the XMB, where a plate is not in the frame. The settings
column and the in-player panel are otherwise untouched because their geometry is a different, unmeasured
surface. The `BG1` focus rectangle is still unattributed (E1/E6), so the text halo and the
`255*(.78+.22*focus)` brightness stay for now; the hunk does not invent a plate rect. The nested heading
position is also unmeasured and is placed one pitch above the first baseline so it cannot overlap it.

The hunk was validated by applying it to a scratch copy of the tree (not this repository) and capturing the
Video Library panel at 1920x1080: the band's luma delta over the background is +43 at x1340 and +1 at x1690;
the five row ink bands start at y 549/588/626/664/703 (baselines 560/598/636/674/712, pitch 38) and the first
ink is at x1332-1341, i.e. the measured text column. The workspace's `view.cpp` is untouched.

```diff
diff --git a/native-xmb/src/view.cpp b/native-xmb/src/view.cpp
index bce2fa9..c055faa 100644
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@ -1135,13 +1135,17 @@ void View::drawScene(QPainter& p) {
             p.setPen(QColor(235,245,255,int(alpha*strength)));p.drawText(r.translated(std::cos(a)*radius,std::sin(a)*radius),flags,value);
         }
     };
-    // Options panel (reference frame 45.4 s): a translucent light strip from x~1330 to the right edge with a bright
-    // left edge; options 23 px, 40 px apart, centred on the focus row; the focused option glows. Slides in from the right.
-    // Shared by the settings choice columns, the item-level Triangle menu and the in-player panel so all three keep
-    // one visual language. `rows` switches it to the player panel's form: a marker column on the left for the current
-    // choice, dimmed rows for the ones this build cannot honour, and the focused row's note under the list.
+    // Options panel. The item menu's settled shape is measured on the 4.93 frame
+    // (audit/ITEM-MENU-INFO-FIRMWARE.md E1): a full-height vertical band whose left edge is x1330 and which
+    // fades to nothing by x1690, with row baselines 560/598/636 at a 38 px pitch and the row text starting at
+    // x1334. The list does not scroll: the rows stay where they are as focus moves, and Down stops on the last
+    // row. That is what `firmwareBand` switches on for the Triangle menu; the settings choice column and the
+    // in-player panel keep the existing form because their screen geometry is a different, unmeasured surface.
+    // `rows` carries the shell's own flags for either menu (a dimmed row this build cannot honour, the current
+    // choice, its reason line); only the in-player panel, which sits over video, gets the dark plate, which is
+    // why `firmwareBand` also identifies the item-menu form.
     const auto drawOptionsPanel=[&](const QStringList& items,double position,double alpha,double ahead,const QString& heading,
-                                    const QVector<MenuOption>* rows=nullptr) {
+                                    const QVector<MenuOption>* rows=nullptr,bool firmwareBand=false) {
         const double x0=1330+ahead*600;
         const double rowCount=rows?double(rows->size()):double(items.size());
         const double first=505-(rowCount-1.)*20.;
@@ -1149,25 +1153,39 @@ void View::drawScene(QPainter& p) {
         // Over the XMB's own background the light strip is enough. Over video it is not: white 23 px text
         // on a bright frame is unreadable, so the in-player panel lays the strip on a dark plate (the
         // console's control panel dims the picture behind it for the same reason).
-        if(rows) {
+        if(rows && !firmwareBand) {
             QLinearGradient shade(x0,0,1920,0);
             shade.setColorAt(0,QColor(0,0,0,158));shade.setColorAt(1,QColor(0,0,0,120));
             p.fillRect(QRectF(x0,0,1920-x0,1080),shade);
         }
-        QLinearGradient fill(x0,0,1920,0);fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
-        p.fillRect(QRectF(x0,0,1920-x0,1080),fill);
-        QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
-        p.fillRect(QRectF(x0-10,0,14,1080),edge);
-        // The list scrolls with the focused row, so the heading and the note follow the row they belong
-        // to rather than the list's resting box: `first` is only where row 0 sits when focus is at 0.
-        const double rowZero=505-position*40;
+        if(firmwareBand) {
+            // Luma at x1340 is 50 over the dimmed XMB and 2 at x1700 (E1), so the band is white at alpha 50
+            // at its left edge fading to nothing at x1690. No plate box and no bright left edge in the frame.
+            QLinearGradient band(x0,0,1690,0);
+            band.setColorAt(0,QColor(255,255,255,50));band.setColorAt(1,QColor(255,255,255,0));
+            p.fillRect(QRectF(x0,0,1690-x0,1080),band);
+        } else {
+            QLinearGradient fill(x0,0,1920,0);fill.setColorAt(0,QColor(255,255,255,46));fill.setColorAt(.08,QColor(255,255,255,30));fill.setColorAt(1,QColor(255,255,255,22));
+            p.fillRect(QRectF(x0,0,1920-x0,1080),fill);
+            QLinearGradient edge(x0-10,0,x0+4,0);edge.setColorAt(0,QColor(255,255,255,0));edge.setColorAt(.75,QColor(255,255,255,70));edge.setColorAt(1,QColor(255,255,255,0));
+            p.fillRect(QRectF(x0-10,0,14,1080),edge);
+        }
+        // The settings column scrolls with the focused row, so its heading and note follow the row they belong
+        // to rather than the list's resting box: `first` is only where row 0 sits when focus is at 0. The item
+        // menu's band does not move at all (no scroll object in the firmware), so its row 0 is fixed at 560.
+        const double rowZero=firmwareBand?560.:505-position*40;
         if(!heading.isEmpty()) { // a submenu (Sort By) or the player panel names itself above its list
-            const double headingY=rows?std::max(56.,rowZero-68.):first-64;
-            text({x0+(rows?34:16),headingY,1920-x0-40,40},heading,19,QColor(255,255,255,190));
+            // The nested menu's heading position is still unmeasured (the frame shows only the level-0 band);
+            // it is placed one row pitch above the first baseline so it cannot overlap it.
+            const double headingY=rows?std::max(56.,rowZero-68.):(firmwareBand?rowZero-48.:first-64);
+            text({firmwareBand?1334.:x0+(rows?34:16),headingY,firmwareBand?1690-1334-24:1920-x0-40,40},heading,19,QColor(255,255,255,190));
         }
-        const double labelX=x0+(rows?34:16),labelWidth=1920-labelX-24;
+        const double labelX=firmwareBand?1334.:x0+(rows?34:16),labelWidth=firmwareBand?1690-labelX-24:1920-labelX-24;
         for(int i=0;i<int(rowCount);++i) {
-            const double distance=i-position,y=505+distance*40;
+            // Firmware rows are top-anchored on the measured baselines and do not travel with focus; the
+            // settings column keeps its pitch and its centred-with-focus motion.
+            const double distance=i-position;
+            const double y=firmwareBand?560.+i*38.:505+distance*40;
             if(y<-40 || y>1120)continue;
             const double focus=std::max(0.,1.-std::abs(distance));
             const QRectF rect(labelX,y-20,labelWidth,40);
@@ -1187,7 +1205,7 @@ void View::drawScene(QPainter& p) {
                 // reason a row cannot be chosen. Never a claim the shell has not measured.
                 const QString note=(*rows)[i].note;
                 if(!note.isEmpty())
-                    text({labelX,std::min(1010.,rowZero+rowCount*40.+8),labelWidth,90},note,16,QColor(225,225,225,235),
+                    text({labelX,std::min(1010.,firmwareBand?560.+rowCount*38.+8:rowZero+rowCount*40.+8),labelWidth,90},note,16,QColor(225,225,225,235),
                         Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap);
             }
         }
@@ -1454,7 +1472,8 @@ void View::drawScene(QPainter& p) {
             if(shell_.optionsOpen() && optionsOpenEase_.value>0.) {
                 QStringList titles;titles.reserve(shell_.options().size());
                 for(const auto& option:shell_.options())titles<<option.title;
-                drawOptionsPanel(titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,shell_.optionsTitle());
+                drawOptionsPanel(titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,shell_.optionsTitle(),
+                                 &shell_.options(),true);
             }
         }
         if(!messageScreen && !shell_.status().isEmpty())text({680,850,1100,50},shell_.status(),26,QColor(230,230,230));
```

Notes on the hunk:
- With three rows the baselines are exactly the measured 560/598/636. A longer list runs on downward at the
  same pitch (the firmware's own 12-row game list would end at 978) and nothing scrolls or clips; E1 measured
  a three-row item, which is why "3 visible rows" is a property of that item, not a window on the list.
- `x0` still carries the `ahead*600` slide, so the settled edge is 1330 and the open still comes in from the
  right. The band fades at 1690; the area right of it is untouched, which is what the frame shows.
- The item menu keeps the band over the XMB (no dark plate: `firmwareBand` now gates the plate that the
  in-player panel needs over video). The menu passes its `MenuOption` list, so a disabled row is dimmed at
  level 115 and its note is drawn under the list; before this hunk the item menu passed titles only and a
  disabled row would have looked selectable. Until the hunk lands, the refusal is only the `snd_system_ng`
  cue plus the log line `shell: option unavailable ...` - the shell side is honest, the drawing is owed.
- The nested item menu shares `firmwareBand` and so gets the same band (a second level is a 128x272 box in
  the RCO, A2, but no frame shows it on screen; drawing the same band is the honest unmeasured choice and is
  noted here).
- Applying these hunks changes rendered pixels, so `video-options-panel`'s stored reference must be
  re-saved after the view pass (`--states --save-reference`), together with the log assertion in F7.

## F7. Verification harness consequences

`bun work/ab-layers.ts --capture --exe C:/dev/out/native-options/re-ps3-native.exe` scores the canonical
Game-root capture, which has no options panel open: its six layers are unaffected. Measured on the phase 12.2
build: frame 18.29/0.8342, top background 20.25/0.9363, category icon row 20.93/0.8990, item label row
11.95/0.3925, clock text 11.39/0.4500, info bar 14.98/0.7639 - **PASS 6/6**, no layer below its budget.

The named state `video-options-panel` (`work/ab-layers.thresholds.json:230-252`) asserts the old list:

```
"log": [{ "pattern": "shell: options opened category= video depth= 1 .*rows= 4 Open\\|Information\\|Sort By\\|Close" }]
```

It cannot pass any more, by design. The new row set on the same surface (Video Library, `Movies` folder row)
is:

```
shell: options opened category= video depth= 1 entry= .../Movies rows= 5 Open|Copy|Delete|Sort By|Information
shell: options rows entry= ... keys= msg_open|msg_copy|msg_del|msg_sort|msg_info conditions= actionable|local-content|local-content|page-sortable|always-last
```

So the expected assertion becomes `rows= 5 Open\|Copy\|Delete\|Sort By\|Information`, with no `Close`; its
stored reference frame must be regenerated after the F6 hunk lands (the rows move to the measured band and
there is one more row). `work/parity-catalogue.json:403-405` carries the same old `Open|Information|Sort
By|Close` trace and needs the same update. Neither file was edited by this pass (harness fence). Running the
state against the phase 12.2 build fails exactly there, not on the pixels: `ASSERT FAIL: the dump says a
different state - log "...rows= 4 Open|Information|Sort By|Close": no matching line`, after the discovery
capture and one retry (`--state video-options-panel --exe C:/dev/out/native-options/re-ps3-native.exe`).

**Status of F, 2026-10-27.** Shell half done and measured in four contexts; drawing half recorded, not
applied. Labels: 27 keys proven from the table, no Close key, eject pair reads Remove/Remove Disc. Rows:
conditional, Information last, no scroll, Down clamps. Sounds: four trusted cues on their traced events, two
guessed assignments unchanged, refused-row cue now reachable. Open timing: brief's "100 units" contradicts
E2 and is not applied. Still open: the `BG1` rect, the text cap height/paddings, the nested-level box on
screen, and the harness reference/log refresh that the view pass owes.

---

# G. Dated correction - 2026-10-28 capture batch 2: the menu clock, measured

Evidence added after F was written: a second targeted RPCS3 batch on the same 4.93 XMB, one session in two
boots, 1920x1080 (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md`): 251 stills, 22,149 burst frames in
19 bursts, crop bursts at 131-235 fps, **clock scale 100 %**. Nothing above is deleted; A6, A7, E1 and E2
keep their 2026-10-27 text. Frame paths are relative to the capture root. This part answers A7 and E2
(timing), corrects two of E1/E2's readings, and leaves A6's `BG1` rectangle where it was.

## G1. A7/E2 superseded: the ~190 ms partial reading is now a measured ramp

Both readings, because the second only makes sense against the first:

| reading | evidence | what it said |
|---|---|---|
| **E2, 2026-10-27/28 (54.1 / 54.8 fps, full frame)** | `dialogs/options-open-burst2/f_0000692.png` is ~192 ms after the key and still at partial alpha; `options-close-burst2/f_0000610.png` is ~110 ms after the key and still partial | the open is **not** a ~100 ms fade (A7's inference is unsupported), the close is **not** an instant destroy. Exact durations stay open |
| **batch 2, 2026-10-28 (135.2 / 175.9 fps, 1:1 crop)** | `menus/menu-open-crop2/` (432 f, 135.2 fps), `menus/menu-close-crop2/` (563 f, 175.9 fps), ROI 1520,300-1840,440 | open: **starts +115...150 ms** after the key, **ramps 245-255 ms**, at rest by **+360...410 ms**; close: **starts +9 ms**, **ramps 200 ms**, at rest by +215 ms |

The second reading supersedes the first as a number and confirms it as a shape: E2's "still partial at
~192 ms" lands inside the measured 115-150 + 245-255 window, and E2's "visible fade, not a destroy" is
exactly the 200 ms close. The reason the batch-1 burst could not settle it is the rate - a full-frame
`BitBlt` caps at 54-64 fps, so the ramp's start and end are both inside its sampling error. The crop bursts
resolve it because they are 1:1 and never rescaled.

What this does to A7 and to the code:

- **A7's "the open should be about half the current duration [100 units]" is dead in both halves.** The
  open is 360-410 ms to rest, i.e. **longer** than the native's 200 ms, not half of it. C1 item 8 (drop
  `moveMilliseconds` to 100) must not be applied on any reading of this evidence.
- **The native has no key-to-start latency.** The firmware waits 115-150 ms before the band begins to
  move; the native starts on the same frame as the key (`optionsOpenEase_.retarget` at
  `src/view.cpp:690`). That is a visible difference of about a fifth of a second and is not a duration
  constant - it is a delay the animation model does not have.
- **The close is a fade, not a page destroy.** A7's second half ("closing is a destroy, not an animation")
  stays contradicted, now with a duration: 9 ms late, 200 ms ramp.
- **`moveMilliseconds` = 200 ms is right for the close and about 150-200 ms short for the open.** If the
  two edges must share one constant, 350 ms fits the open (115 start + 245 ramp) and overruns the close by
  150 ms; the honest implementation has two timings, which is a view change, not a constant change.

## G2. The band's endpoints, and two corrections to E1/E2

**Endpoints** (re-measured on the settled still, cross-checked against batch 1's frame):

| quantity | value | frame |
|---|---|---|
| left edge | **x = 1330**: the fill begins there (33-34 luma at y 100), with the bright 1 px step at the same x as the info-bar plate's left edge (1329) | `menus/game-menu-open-check.png` |
| profile at y 100 | 67 @1330 (edge), 47 @1340, 55 @1400, 43 @1500, 26 @1600, 5 @1680, 3 @1690, 2 @1700, 0 @1900 | same |
| profile at y 30 | 45 @1340, 53 @1400, 42 @1500, 25 @1600, 5 @1680, 3 @1690, 2 @1700, 0 @1900 | same |
| geometric zero | **about 1690-1700**; nothing measurable right of 1750 | same |
| vertical extent | full height, y 0-1079 | same |
| batch-1 cross-check | `dialogs/options-open-01.png` at y 30: 15 / 36 / 50 / 59 / 46 / 27 / 5 / 3 / 2 / 0 at the same x samples | within 2 luma of the batch-2 frame |

So E1's "left edge ~1330, fading out by ~1690" is confirmed independently, and 1330..1690-1700 is the
band's span. F6's hunk (`x0=1330`, `1690` fade end) is right on both ends.

Two corrections:

- **`menus/game-menu-rest.png` is not the menu at rest.** It scans as the **closed** state: luma <=1 at
  x 1340-1900 on y 30 / 100 / 300, an identical profile to `menus/game-menu-closed-check.png`. The settled
  menu is **`menus/game-menu-open-check.png`** (55 luma at x 1400 on y 100). The frame worth citing for
  E1's numbers from this batch is therefore the `-open-check` still, not the `-rest` one; the numbers
  themselves are unaffected.
- **E2's row-set claim is wrong in one direction.** E2 said the mid-alpha frame shows `Sort By` and
  `Group Content` "which the settled frame does not", and read that as the fade compositing more than the
  final row set. Both settled frames carry five rows: batch 2's `game-menu-open-check.png` has ink bands
  301-323 and 341-362 (`Sort By`, `Group Content`) plus 539-560, 581-598, 621-638 (`Start`, `Delete`,
  `Information`), and **batch 1's own `dialogs/options-open-01.png` has the same five bands**
  (301-318, 341-358, 539-560, 581-598, 621-638, re-measured). The settled menu has two groups separated by
  a gap, not three rows; what changes during the fade is contrast, not the row set. This agrees with
  `MANIFEST` section 4.3 ("a gap, then `Start`, `Delete`, `Information`").

**Row pitch, stated with its uncertainty.** Ink tops are 539 / 581 / 621 for the action group - steps of 42
and 40 - and the ink bottoms are 560 / 598 / 638 - steps of 38 and 40. E1's 38 and the batch's 38 both sit
inside that window. The honest value is **40 +/- 2 px**, with the two batches bracketing it; a 1 px claim
is not supported by these frames, and cap-height differences between rows move the ink tops more than the
baselines move. F6's `560 + 38*i` is inside the bracket.

## G3. The row dims before the plate

New, and it is a drawing rule rather than a timing number: on close the row **ink** drops below luma 180
**within one frame (<9 ms at 176 fps)**, while the **plate** keeps fading for the following ~200 ms
(`menus/menu-close-crop2/`). Text and band are therefore not one alpha channel in the firmware.

What that means for C1 and F6: F6's hunk draws text and band from the same `alpha` (the label colour is
`255*(.78+.22*focus)` and the band's `fillRect` both ride the panel's alpha). Reproducing the measured
behaviour needs the text to snap to dim on the first frame of a close and the band to keep its ramp - i.e.
a second alpha for the text, or the same rule the settings rows show (text brightness is its own channel).
No hunk is written here for it; it is recorded because it is now measured rather than assumed.

## G4. What batch 2 does to E1's still-unmeasured list

| E1 item | after batch 2 |
|---|---|
| the `BG1` highlight rectangle inside the band | **still unmeasured**. The batch measures the plate on the XMB *column* (`error/column-corrupted-focused.png`, x 405-731, y about 420-560, `GEOMETRY-ROOT.md` 10.1) and shows that a focused *settings* row has no filled rectangle at all (`SETTINGS-FIRMWARE.md` 12.6) - but no frame isolates a plate inside this band |
| text cap height and row paddings | still open; the ink bands are measured, the glyph height is not |
| the panel's top/bottom | answered as "full height" (G2) - there is no box edge |
| Down x4 clamp | unchanged from E1 |
| nested-level box on screen | still no frame; `Sort By` / `Group Content` are rows of level 0, not an open level 1 |

## G5. Still open after batch 2

- The `BG1` rectangle inside the band (G4).
- The text cap height, row paddings and the exact 38-vs-40 pitch (G2).
- The nested menu level's on-screen box.
- Cue audio for open, move, confirm and close: still no audio channel (`MANIFEST` section 5 no. 6). A8's
  cue table is unchanged and still rests on the RCO label names plus the traced `snd_*` call sites.
- The harness reference/log refresh that F7 owes (unchanged; no source was edited by this part).

No source, threshold or reference was touched for this section.

---

# H. Dated pass - 2026-10-28: the two row groups, the label audit, the nested level

Status: the **grouping and wording half is implemented** in `src/shell.cpp` / `src/shell.h`; the **drawing
half** is the hunk in H6 and was not applied to the repository's `view.cpp` (write fence). Part F is not
deleted; where H contradicts F, H says so. Evidence classes are A's three (RCO, CODE, FRAME) plus **DUMP**
(the build's own `.options.json` / log lines) and the captures named below. Nothing in H is copied from a
firmware artifact.

## H1. The group break is data on the row

The settled 4.93 frame carries two row groups, not one list (G2): `Sort By` / `Group Content` at ink
301-323 / 341-362, then `Start` / `Delete` / `Information` at 539-560 / 581-598 / 621-638, with a gap between
the last utility ink and the first action ink. The firmware's own builder appends the item actions in order and
`Information` last (A4); the list-level commands are a second section above them.

`MenuOption` now carries one field, `dividerBefore` (`src/shell.h`): true only on the first row of a group that
starts after the gap. `Shell::buildOptions()` builds group 0 first and group 1 second:

| group | rows | condition |
|---|---|---|
| 0 - list-level / utility | `Sort By` (`msg_sort`). `Group Content` and `Display All` remain absent, as F3 records (nothing is grouped/filtered on this build) | `page-sortable` |
| 1 - item actions | the item's own command (`Resume` / `Open` / `View` / `Start` / `Play`), `Play from Beginning`, `Copy`, `Delete`, the Continue Watching `Remove`, and `Information` last | the F3 conditions, unchanged |

No row set and no condition changed. `dividerBefore` is set on the first group-1 row only when group 0 is
non-empty; with no utility rows the list is a single group and the flag is false. The menu opens with focus on
that first action row (the settled frame's bright row is `Start`, E1), not on row 0; Up reaches the utility
group and Down clamps on `Information` as before.

The panel log and the options dump both expose the break, so it is checkable without pixels:

```
shell: options groups entry= ... groups= 2 break= Sort By -> Open
```

Every `.options.json` row now carries `dividerBefore`, and the file has a top-level `groups` array:

```json
"groups": [
  {"group": 0, "dividerBefore": false, "rows": ["Sort By"]},
  {"group": 1, "dividerBefore": true,  "rows": ["Open", "Copy", "Delete", "Information"]}
]
```

## H2. Row sets and groups, per context (DUMP)

Same five contexts as F3, same seeds, pinned clocks (`--render-clock 30 --background-clock
2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000`), measured from
`<ctx>/entries.json.options.json` and the log. `entry` is the row Triangle was pressed on.

| context | entry | group 0 (utility) | group 1 (actions) | dividerBefore |
|---|---|---|---|---|
| installed Steam title | `steam:2807960` Battlefield 6, depth 2 | (none) | `Start`, `Information` | none (1 group) |
| local video folder | `.../video-library/Movies` | `Sort By` | `Open`, `Copy` (disabled), `Delete` (disabled), `Information` | on `Open` (2 groups) |
| video file with resume | `.../video-library/Top Level Clip.mp4` | `Sort By` | `Resume`, `Play from Beginning`, `Copy` (disabled), `Delete` (disabled), `Information` | on `Resume` (2 groups) |
| photo | `.../photo-lib/Alpha.png` | `Sort By` | `View`, `Copy` (disabled), `Delete` (disabled), `Information` | on `View` (2 groups) |
| Continue Watching | `continue:59e69a3e0a2a2c78651d8ab2` | (none - the page is not a directory, so `page-sortable` is false) | `Resume`, `Play from Beginning`, `Remove`, `Information` | none (1 group) |

Exact `options opened` lines (order is the firmware's group order, not F1's flat order):

```
steam:          rows= 2 Start|Information
video folder:   rows= 5 Sort By|Open|Copy|Delete|Information
video resume:   rows= 6 Sort By|Resume|Play from Beginning|Copy|Delete|Information
photo:          rows= 5 Sort By|View|Copy|Delete|Information
continue row:   rows= 4 Resume|Play from Beginning|Remove|Information
```

Conditions (all preserved from F3): `actionable`, `resume-record`, `local-content`, `page-sortable`,
`continue-row`, `always-last`. The two disabled rows keep their reason notes verbatim
(`This native build does not copy files yet.` / `... does not delete files yet.`).

## H3. Wording audit against the RCO table

Reproduction: `bun work/rco-table-dump.ts <dev_flash>/vsh/resource/explore_plugin_full.rco --text`
(861 English entries, the reader `src/assets.cpp` uses). Every row label the shell can emit in these five
contexts and in the Sort By submenu now resolves from that table:

| emitted label | key | context |
|---|---|---|
| `Start` | `msg_game_start` | Steam title |
| `Information` | `msg_info` | all five |
| `Sort By` | `msg_sort` | the three local directory pages |
| `Open` | `msg_open` | video folder |
| `View` | `msg_display` | photo |
| `Resume` | `msg_resume` | video resume, Continue Watching |
| `Play from Beginning` | `msg_play_begin` | video resume, Continue Watching |
| `Copy` / `Delete` | `msg_copy` / `msg_del` | local content, disabled (F3) |
| `Remove` | `msg_eject` | Continue Watching row |
| `Creation Date ^` / `Creation Date v` | `msg_created_date_time_f` / `_r` | Sort By submenu |
| `Update Date ^` / `Update Date v` | `msg_updated_date_f` / `_r` | Sort By submenu |

**The one label that was not a firmware key is fixed.** F3's Continue Watching row used
`tr("Remove from Continue Watching")` with no key. It is now `msg_eject` = `Remove`, with the row note
`Removes this title from Continue Watching; the file stays on disk.` and the existing confirm body
`Do you want to remove this title from Continue Watching?`. `Remove` is the firmware's own generic remove
word and the row performs exactly that on this build's record; it cannot be read as deleting the file
(the note and the confirm box both say so). The confirm body remains native wording because no firmware
string covers forgetting a resume point; that is a dialog body, not a row label. Older prose still names the
old wording and is outside this pass's write fence: `audit/MEDIA-CONTROLS.md:188-222`, `audit/DIALOGS-FIRMWARE.md:623`
and `:721`, `audit/VISUAL-BEHAVIOR.md` VIS-46, and F3's `continue-row` bullet below.

Checks that did not change:

- **No `Close` key and no `Close` row** in the 861-entry table; Circle/Escape still close (F1).
- **No `Cancel` row.** `msg_cancel`/`msg_cancel2` are the confirm box's buttons, already read in
  `dialogs.cpp`.
- **`Remove Disc` (`msg_disc_eject`) has no row** because this build has no optical drive; both eject
  labels still resolve and are printed by the panel log (F2).
- **No row claims a capability the build lacks.** `Display All`, `Group Content`, `Add to Playlist`,
  multi-select, update/purchase/search/Move rows stay absent; `Copy`/`Delete` are listed disabled with
  their reasons; `Start` is only used for a launchable title, `View` only on the photo context.
- The player panel's `Subtitles`/`Audio`/`Repeat`/`Shuffle` rows are the in-player surface, not
  `expage_menu`; they were not relabelled.

## H4. The Sort By submenu and the nested level

**Heading position is a fit, not a measurement.** No frame shows an open level 1 (G4; G2's `Sort By` /
`Group Content` are level-0 rows). The heading therefore stays where the view already puts it for a `rows`
menu, one row pitch above the first baseline (`rowZero - 68` = 484 at the current anchor). This is a fit
inside the unmeasured region, marked in the `SortMenu` case in `src/shell.cpp` and in this section; no new
number was invented.

**The two groups stay distinct across nesting.** Opening `Sort By` replaces the row vector, so the nested
level is one group of four date rows and carries no `dividerBefore`; the parent vector (with its flag) is
kept in `optionsParent_` and restored on Circle. DUMP evidence from one run:

```
shell: options groups entry= ... groups= 2 break= Sort By -> Open
shell: options selection 0 Sort By
shell: options sort menu opened rows= 4 labels=creation/update date pairs groups=1 parentGroups= 2 heading=fit(unmeasured)
shell: options back to item list groups= 2
```

The parent's action rows are not drawn while the nested level is up; the nested list is not merged into
either parent group.

**The nested plate is a mismatch, recorded and not rebuilt.** The RCO authors levels 1-2 as *separate*
`plane_menu1` / `plane_menu2` boxes, each 128x272 (A2, A6), with their own `BG0`/`BG1` and MLists. This build
does not create a second plate: it draws the nested list in the **same** full-height band (x 1320..1742),
heading above the rows (rows at the action anchor 552/590/628/666), and hides the parent rows. Verdict:

| aspect | firmware | this build | fit or mismatch |
|---|---|---|---|
| level container | separate 128x272 plane at its own position | the level-0 band | **mismatch** (shape and position) |
| heading | no level-1 frame exists; not measurable | one row pitch above the first row (484) | **fit** (inside an unmeasured region) |
| row metrics | level-0 measurements only | same 38 px pitch / 1334 column as level 0 | fit by reuse, unproven for level 1 |
| parent visibility | separate plate at its own position, the level-0 plate still exists in the tree | parent rows replaced, band unchanged | **mismatch** (no second plate/dim/offset) |

No second plate was rebuilt in this slice: there is no frame of an open level 1, and inventing a 128x272
rect would be a fit presented as a measurement. The gap is recorded in G4 / H8, not hidden. The capture
`sortmenu-visible/sort-panel.png` shows today's single-band form (heading `Sort By`, four rows, no parent
rows, no second plate).

## H5. Verification

Build (source: this repository; no `view.cpp` edit):

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-menugroups
```

Five context runs as H2, each with `XMB_DUMP_ENTRIES` set, so the group structure above is the build's own
data. `ab-layers.ts --capture --exe C:/dev/out/native-menugroups/re-ps3-native.exe`:

| layer | PSNR | baseline | SSIM | baseline | gate |
|---|---:|---:|---:|---:|---|
| frame | 18.92 | 18.92 | 0.8553 | 0.8553 | PASS |
| top background | 20.25 | 20.25 | 0.9363 | 0.9363 | PASS |
| category icon row | 20.90 | 20.90 | 0.8982 | 0.8982 | PASS |
| item label row | 12.04 | 12.04 | 0.3860 | 0.3860 | PASS |
| clock text | 13.64 | 13.64 | 0.5128 | 0.5128 | PASS |
| info bar | 17.61 | 17.61 | 0.8244 | 0.8244 | PASS |

**PASS 6/6.** Important caveat measured this pass: the candidate frame behind those numbers contains **no
glyphs** (the shared tree's text compositor currently draws none; see H6's status note), and the six scores
equal the stored `captureBaseline` exactly. The gate is therefore passing on a glyphless candidate: the
`clock text` layer's 13.64 is not evidence that text renders. The six-layer gate is unchanged by this pass,
but it is no longer a text guard until the shared-tree text regression is repaired and the captureBaseline
is re-measured on a text-bearing frame.

**Named state.** `work/ab-layers.thresholds.json`'s `video-options-panel` asserts the old flat order. The
exact new expectation is:

```json
"log": [{ "pattern": "shell: options opened category= video depth= 1 .*rows= 5 Sort By\\|Open\\|Copy\\|Delete\\|Information" }]
```

(and the description's row list should read `Sort By / Open / Copy / Delete / Information`). Running the
state against this build fails exactly there, before any pixel comparison:

```
ASSERT FAIL: the dump says a different state
- log "shell: options opened ... rows= 5 Open\|Copy\|Delete\|Sort By\|Information": no matching line
```

The harness was not edited (fence). `work/parity-catalogue.json:403-405` carries the same old order and
needs the same one-line update.

## H6. The view hunk (not applied)

`MenuOption::dividerBefore` is inert until the view consumes it. The hunk below adds the measured group
separation to the item menu's band and uses the flag only as the break marker; no per-context y offset and no
new panel geometry enter the view. `237` is G2's two origins: the action rows' rect centre 552 (E1's
baseline 560 minus the 8 px em offset) minus the utility rows' rect centre 315 (ink 301-323 minus the same
8 px). With pitch 38 the utility rows land at 315/353 (ink ~302-323 / 340-361) and the action rows keep
552/590/628/666.

```diff
diff --git a/native-xmb/src/view.cpp b/native-xmb/src/view.cpp
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@ -1137,5 +1137,6 @@ struct PanelBand {
     double rowCentre=505.;    // fixed rows: row 0's text centre; scrolling list: the focus row's centre
     double labelInset=16.;    // row text left = x0 + labelInset
+    double groupGap=0.;       // fixed rows: distance from the action origin up to a divider's group origin
     double contentDim=1.;     // brightness of the page the band covers, at rest (measured)
     const BandStop* stops=nullptr;
     int stopCount=0;
@@ -1180,3 +1181,3 @@
 const PanelBand kOptionsBand{.firmware=true,.x0=1320.,.fadeEnd=1742.,.pitch=38.,.fixedRows=true,
-    .rowCentre=552.,.labelInset=14.,.contentDim=.545,.stops=kOptionsBandStops,
+    .rowCentre=552.,.labelInset=14.,.groupGap=237.,.contentDim=.545,.stops=kOptionsBandStops,
     .stopCount=int(std::size(kOptionsBandStops))};
@@ -1315,7 +1316,15 @@
         const double labelX=firmwareBand?x0+band->labelInset:x0+(rows?34:16);
         const double labelWidth=(firmwareBand?fadeEnd:1920.)-labelX-24;
+        // The shell's dividerBefore is the firmware's group break (G2): the rows before it are the utility
+        // group, which starts groupGap px above the action group's own origin. Both origins are this band's
+        // measured numbers, so no menu-specific y offset enters the view.
+        int groupBreak=0;   // no break: 0 keeps i as i below
+        if(rows)for(int i=0;i<int(rows->size());++i)if((*rows)[i].dividerBefore){groupBreak=i;break;}
         for(int i=0;i<int(rowCount);++i) {
             const double distance=i-position;
             // Firmware rows are anchored on the measured baselines and do not travel with focus; the
             // settings list keeps its 40 px pitch and its centred-with-focus motion.
-            const double y=(firmwareBand&&band->fixedRows)?focusRow+double(i)*pitch:focusRow+distance*pitch;
+            const bool aboveBreak=firmwareBand&&band->fixedRows&&groupBreak>0&&i<groupBreak;
+            const double y=(firmwareBand&&band->fixedRows)
+                ?focusRow+double(aboveBreak?i:i-groupBreak)*pitch-(aboveBreak?band->groupGap:0.)
+                :focusRow+distance*pitch;
```

The hunk was applied to a scratch copy of the last text-capable tree (not this repository), together with
this pass's `shell.cpp`/`shell.h`, and captured. The measured ink bands in
`out/menugroups-verify/video-folder-grouped/panel.png` are:

| row | measured ink y | model |
|---|---|---|
| `Sort By` (utility) | 306-328 | 315 rect centre, ink ~302-328 |
| `Open` (action, focused) | 543-564 | 552 rect centre, ink 539-560 |
| `Copy` | 582-603 | 590 rect centre, ink 581-598 |
| `Delete` | 620-637 | 628 rect centre, ink 619-636 |
| `Information` | 657-675 | 666 rect centre, ink 657-674 |

That is the settled frame's two groups: utility above, a 215 px gap for this one-utility-row list (the frame's
two-row utility group leaves 177 px), then the action block. The capture is a
layout proof only: the shared tree's glyph compositor was bypassed to the view's QPainter fallback in the
scratch build because every build produced after 11:58 renders no glyphs (`text_renderer.cpp` /
`font_raster.cpp` mtime 11:58:42; `native-textrevert`, 11:58:35, is the last text-bearing build). This is
outside the write fence and is recorded so the next pass re-captures on a repaired tree. The repository's
`view.cpp` was not edited.

**Status of H.** Grouping and wording are done and measured from the build's own dumps in five contexts; the
drawing hunk is recorded, not applied. The nested level remains one band, explicitly a mismatch against the
firmware's separate 128x272 plate. The named state's log assertion needs the one-line order update above.
Still open: the `BG1` rectangle, cap height/paddings (unchanged from E1/E6), the text-compositor regression in
the shared tree, and the re-measure of the gate's captureBaseline once text renders again.

## H7. Corrections to F

- **F1's "the item's own command first" is true of the row builder, not of the screen order.** The firmware's
  list-level rows sit above the item actions (G2); the item's command is the first row of group 1 and is the
  row that gets initial focus. F1's wording is corrected by H1/H2; its conditions and the `Information`-last
  invariant still hold.
- **F3's table order is superseded** by H2's group order for the four contexts where `Sort By` exists. The
  row sets, keys and conditions are unchanged; only `Sort By` moves to group 0.
- **F2's "`msg_eject` ... never built"** is superseded: `msg_eject` is now the Continue Watching row's label
  (H3); `msg_disc_eject` still has no row. F3's `continue-row` bullet ("keeps a native wording and the dump
  gives it no key") is superseded with it.
- **F7's assertion** is superseded by H5's exact new pattern; the harness and `parity-catalogue.json` were
  not edited.
- F6's nested-band note ("drawing the same band is the honest unmeasured choice") stands, now with the
  explicit mismatch verdict in H4.

---

# I. Dated measurement pass - 2026-10-28: the nested plate, the two-button confirm, and the error surface

Read-only pass on the existing local artifacts, no new capture, no RPCS3 run, no source edit. Three items:
I.1 states what the RCO fixes about the level-1 plate and what no local artifact can; I.2 records the
two-button confirm as a **negative** with the capture that would settle it; I.3 restates the error surface
so a future pass does not go looking for it. Tools used are the same helpers as
`SETTINGS-FIRMWARE.md` 15 (`C:\dev\work\m.py` and friends) plus the already-dumped RCO trees in
`C:\dev\out\rco-dump\`.

## I.1 The nested plate (level 1): what is authored, what is not, what would settle it

Command: `Select-String` over `C:\dev\out\rco-dump\all.txt` and the `--rco-tree` dump of
`explore_plugin_full` (lines 1906-1993). `expage_menu` is 16 objects:

```
expage_menu                      Page  0x801 @222852
  plane_menu0                    Plane 0x802 @222928  pos 0,0  size 0x0     (auto)
    plane_menu0_BG0 / _BG1       Plane 0x802         size 0x0
    plane_menu0_LowerMenu / _UpperMenu  MList 0x809  size 0x0   (ships empty)
  plane_menu1                    Plane 0x802 @223844  pos 0,0  size 128x272
    plane_menu1_BG0 / _BG1       Plane 0x802         size 0x0
    plane_menu1_LowerMenu / _UpperMenu  MList 0x809  size 0x0   (ships empty)
  plane_menu2                    Plane 0x802 @224760  pos 0,0  size 128x272
    plane_menu2_BG0 / _BG1       Plane 0x802         size 0x0
    plane_menu2_LowerMenu / _UpperMenu  MList 0x809  size 0x0   (ships empty)
```

A2/H4 already record this tree. What this pass adds is a statement of what it does and does not determine:

1. **The level containers are siblings, not children of level 0.** `plane_menu1` and `plane_menu2` are
   `expage_menu`'s children beside `plane_menu0`, each with its own `BG0`, `BG1` and two MLists. So a
   level-1 list is a **second box with its own background plate and its own single focus plate**, and the
   level-0 plate is not reparented: it still exists in the tree while level 1 is open. H4's "parent
   visibility" row is therefore a real mismatch, not a modelling choice.
2. **The authored datum is the size only, and even that is not a screen size.** All three levels carry
   `posX=0 posY=0`, so the position is computed by `explore_plugin_all.c` (A2 cites the menu (0,0)/(0,1)
   addressing); GEOMETRY-ROOT 3.7/5.6 already shows the same container family's info-bar plane authored
   560x82 and drawn at 591x58, i.e. the RCO width/height is not 1:1 screen px. So 128x272 is a **box
   declaration**, not a measured rectangle.
3. **The numbers are at least self-consistent with a 4-row list.** At the menu's smallest text size (19 px,
   `metric-size-19 "Sort By"` = 69.73 px in `audit/TEXT-RENDERER.md` 5) the level-1 labels fit inside 128 px,
   and four rows at the measured 38 px pitch (E1/G2) occupy 152 of the 272 px of height. That is a
   plausibility check, not a measurement.
4. **The colour/material of the nested plate is not in `expage_menu` either.** `BG0`/`BG1` have no image
   reference (both `image=null`), like level 0's; the plate look comes from the plugin's own material path,
   which is why E1/G2 could only measure it from a frame.

**What a future rig run must capture to close it.** No frame shows an open level 1 (G4); the route is
replayable and is the one H2's row order implies:

- Route A (preferred): XMB **Video** (or Music/Photo) column, focus a **folder-class item**, press Triangle.
  The menu opens with the utility group above the action rows; focus starts on the item's own command, so
  press `Up` once to reach **`Sort By`** (H2: `Sort By` is group 0, row 0), then Cross.
- Route B (if the rig's media columns are empty): the **Game Data Utility** folder in the Game column
  (Triangle on the folder), same `Sort By` -> Cross.
- Keys on the batch-1/2 mapping: Triangle = `Insert`, Cross = `PgDown`, Back = `PgUp` (MANIFEST section 1).
- Capture: one full-frame still **at least 1 s after the Cross** (SETTINGS 15.4: the dim ramps after the
  band, so an earlier still reads high), plus a full-frame burst around the key for the arrival, plus a
  1:1 crop burst of `x 1100..1920, y 100..900` so the second box's rectangle, its own BG1 and its distance
  from the parent box can be resolved frame by frame.
- Measured on those frames: the nested box's left/right/top/bottom, its fill profile, its row pitch and
  first-row y, whether the level-0 rows and plate are still visible, and whether the nested box carries a
  heading.

Expected shape from the RCO: a **second, smaller, floating box** with its own focus plate, positioned by the
plugin relative to the parent box, with the level-0 plate still behind it - which is the opposite of what
this build draws (H4: same band, parent rows replaced). **Confidence: high that the shape is a second box
(RCO, three attributes); zero on its screen rectangle (no frame, and the RCO size is not a screen size).**

## I.2 The two-button confirm plate: no RCO declares one (negative result)

The native draws its own two-button confirm box. This pass searched every container the local tree has for a
plate (an image-bearing, sized plane) behind a two-button confirm, and found **none**.

| container | what it holds | verdict |
|---|---|---|
| `msgdialog_plugin.rco` (257,900 B, 2 objects) | `page_msgdialog` only: **no plane, no button, no text** | no plate |
| `system_plugin.rco` (59 objects, 17 animations) | pages `page_default_theme`, `page_wallpaper_theme`, `page_dummyload`, `page_infobar`, `page_busy`, `page_btnnavi`, `page_notification`, `page_pointcursor`, `page_autooff_guide` | **no dialog or confirm page at all** |
| `sysconf_plugin.rco` | `page_dialog_base` @765056 exists but has **no children**; `page_base0` @766188; the `page_*_finish` family are text result pages (section 4) | no plate |
| `explore_plugin_full.rco` `poweroff_page` @235652 | `Plane poweroff_base` with `pos 0,0 size 0x0 image=null`; `CheckboxGroup poweroff_select`; `Button poweroff_yes` and `poweroff_no`, both `image=null shadow=null image2=null`; `Text` = the turn-off question | **this is the two-button confirm, and it has no plate and no button textures** |
| image tables, all dumps | the only dialog-ish image names are `icon_dialog_base` (an icon) and the generic `tex_button` / `tex_button_focus` / `tex_button_shadow` used by the file-copy and file-delete dialogs | no dialog plate texture |

So on this console the two-button confirm is **drawn by code**, not by an authored plate: the one confirm
page that exists (`poweroff_page`) is a bare container plus a checkbox group plus two buttonless buttons.
The native's plate is a fit with no firmware counterpart, and it should be labelled as such rather than
"measured".

**What would settle it.** A frame of a real two-button confirm, in either of two routes:

- Route A - the PS menu's system confirm (the case the native imitates): `--script` can drive it without a
  controller, because the native's own script grammar has `pshold` (system menu) - on the firmware side the
  equivalent is the PS button hold. Route: hold PS -> `Turn Off the System` -> Cross. **Careful:** accepting
  it powers the console down, so capture the *dialog* (the frame after the second Cross) and stop there.
- Route B - an in-XMB confirm that reuses the same visual: Game column -> `Saved Data Utility (PS3)` ->
  a saved-data entry -> Triangle -> `Delete` -> Cross. This is `expage_fdelete_dialog`, which ships its own
  buttons (`cmdbtn_select_all`, `deselect_all`, `decide_src`) with textures, so it is the closest authored
  cousin and will show whether the confirm box has a plate when the page does carry textures.
- Measure on those frames: plate rectangle and fill, corner radius, the two buttons' rects and their focus
  treatment, the hint row, and the dim behind. That set is exactly what the native's own plate currently
  guesses.

**Confidence: high** that no RCO in the local tree declares the plate (four containers enumerated, all
zero-sized or childless); **high** that the confirm is code-drawn; **unmeasured** for every pixel of it.

## I.3 The error surface, restated so no future pass hunts for it

Four routes were tried in batch 2 (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md` 4.1) and this is the
whole of what this console produces. There is **no 800xxxxx error code and no error dialog** reachable on
this rig.

| attempt | what actually happens | evidence |
|---|---|---|
| Start a title with no `PARAM.SFO` (`Corrupted Data`, `dev_hdd0/game/TEST12345`) | **no-op**: no dialog, no page, no visual reaction; the clock keeps advancing and the XMB stays interactive across 9 s + 13 s + three 20 s grabs | `error/column-corrupted-focused.png`, `error/launch-burst/` (387 f), `error/corrupt-launch-burst/` (740 f), `error/retry-t01..t06.png` |
| `Settings > System Update > Update via Internet` | **not a firmware page**: vsh dies, RPCS3 fatal `fs::windows_file::write ... Access is denied (0x5)` in `ScePafJob`, console frozen on `Checking... Please wait.` with the host overlay drawn over the frame | `error/update2-launch-burst/` (660 f), `error/update2-settle-01..04.png`, `error/sysupdate-page-rest.png` |
| Launch `PlayStation Store` offline | the app's "please wait" then a **system message page** (`Internet Connection Settings`, body about the connection, `OK`, `X Enter` / `O Back`) - the same page an Internet Connection Test failure raises. **No code, no error number** | `error/retry-t01.png`, `error/retry-t04/t06.png`, `error/icp-page-rest.png` |
| `Settings > Network Settings > Internet Connection Test` | **the one producible string page**: title `Internet Connection Test`, message ink x 753-1163 y 530-546, one rule at y 199, a dimmed heading ghost, no focusable control, `O Back` | `error/ictest5-fail/` (10,569 crop frames at 234.9 fps), `error/ictest2-burst/`, `error/ictest3-burst/`, `error/page-at-rest.png` |

Consequences to carry forward: (a) a native "error surface" cannot be validated against this console beyond
the one message page in the last row; (b) the System Update failure is an emulator fault, so the
`software_update_plugin` RCO (SETTINGS U8) stays open and no update-failure page exists to copy; (c) the
corrupt-title no-op means a `Start` on an invalid title needs no native dialog either; (d) any 800xxxxx code
in a future spec is unmeasurable here and must be attributed to a source other than these captures.

No source file was edited for this section; no RCO, threshold or reference was touched.

