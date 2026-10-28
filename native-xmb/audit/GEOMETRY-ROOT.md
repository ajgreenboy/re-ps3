# GEOMETRY-ROOT: the root category column and an open page, from RCO data and captures

Status: P2.1-P2.4 groundwork, 2026-10-27. Scope: the top-level category column and the item column
of a page opened under a category (depth 0 and depth 1), plus the info bar that shares the root
page. This document does not edit `src/`; it states, for every hardcoded number in the native layout
code, what the firmware data says, and what the new `rco_objects` module should read at run time.

Firmware data stays local. Nothing here is a dump: only object names, RCO offsets, attribute values
and frame coordinates, all of which are already in `audit/LAYOUT-TRACE.md` in the same form. The raw
RCOs (`C:\dev\rpcs3\bin\dev_flash\vsh\resource\*.rco`), the decrypted modules (`fw493/vsh_module/*`)
and the captures (`out/native-reference/`, `C:\dev\out\rpcs3-xmb-cap\`) are not committed.

ASCII only. Coordinates are 1080p screen pixels unless marked `wu` (world units; 1 wu = 1 px at the
focus distance 2700, see section 3.1).

---

## 1. Summary of what is firmware geometry and what is not

| Layer | Where the geometry lives | Proven by |
|---|---|---|
| Column origin and column count | RCO: `xmenu` `posX`/`posY`, `menus` | `xmb_plugin_normal.rco` @3296; the same `-395` appears verbatim as the icon's world x in the captured vertex constants ([VC], section 3.1) |
| Icon pitch, focus padding, icon row y, quad sizes, focus brightness | per-draw constants of the icon program; three of them also exist as literals in `vsh.elf`'s widget-default block | [VC] `out/native-reference/fpdump7`; [PLUGIN] `vsh.elf` 0x68f10c/0x68f118 (200.0), 0x68f108/0x68f12c (10.0), 0x68f144/0x68f178 (80.0) |
| Item rows (focus y, pitch, above/below offsets) | per-draw constants only; no RCO object carries them | [VC] rows at 505/680/760/840/920/1000; no `XList`/`MList` object in any category RCO carries a position |
| Page image/label column x, label y, text sizes | plugin (paf) text placement; not in the RCO and not a module literal | [FRAME] `out/rpcs3-xmb-cap/settings-b.png` (level 0), `page-open.png` (level 1) |
| Page background/list/front page family and the option menu planes | RCO objects, but with **zero** geometry | `explore_category_*.rco`, `explore_plugin_full.rco` `expage_*` |
| Camera distance 2700 / parent-column distance 2857.2 | scene constants; 2700 is derivable from the projection (540 x 5), 2857.2 is not in any RCO | [VC] 259.w = 2700.00 / 2857.20 |
| Info bar plane size and the indicator icon slots | RCO `indi_base` 560x82 at `posY -74`, five 32x32 icon planes at -244..-410 | `xmb_plugin_normal.rco` @4284, @4836-@5668 |

The headline: **the RCO contributes the column origin, the column count, the info-bar plane and the
page family; it contributes no row, list or menu-item geometry at all.** Every number that the
console draws for a row is a per-draw or per-text value authored by the plugin/paf at run time.
That is why `audit/LAYOUT-TRACE.md` could not find most of them in any module literal: they are
computed, and the only authority is the captured draw constants ([VC]) or a frame [FRAME].

---

## 2. The RCO object trees

Dumped with `bun work/rco-tree-dump.ts --res <name>` and `bun work/p2-root-geometry.ts rco <path> <regex>`
(the second prints the raw attribute bytes, including the ref/image fields the JSON digest drops).
All 122 RCOs decode with 0 errors (`out/native-reference/rco-behavior/resources.json`).

### 2.1 The root column: `xmb_plugin_normal.rco` (sha256 cc69eb836f2e85d5...)

```
ObjectTree @3104
  page_xmb_bg          Page   @3144
  page_xmb             Page   @3220
    xmenu              XMenu  @3296   <-- the only object with position
      list_user        XMList @3468
      list_sysconf     XMList @3528
      list_photo       XMList @3588
      list_music       XMList @3648
      list_video       XMList @3708
      list_tv          XMList @3768
      list_game        XMList @3828
      list_network     XMList @3888
      list_psn         XMList @3948
      list_friend      XMList @4008
  page_xmb_indicator   Page   @4068
    indicator          Plane  @4144   posX 0 posY 0, rgb 0/0/0, alpha 0  (invisible container)
      indi_base        Plane  @4284   posX 0 posY -74, width 560, height 82, image ref set
      indi_adjust      Plane  @4424   posX -26 posY 0, rgb 0/0/0, alpha 0  (icon group origin)
        clock          Text   @4564   posX 0 posY -12, size 10, shadow 4/-5
        message        Plane  @4836   posX -244, 32x32
        chat           Plane  @4976   posX -274, 32x32
        online         Plane  @5116   posX -342, 32x32
          online_num   Text   @5256   posX 14 posY 2, size 10
        avatar         Plane  @5528   posX -376, 32x32
        plus           Plane  @5668   posX -410, 32x32
  page_xmb_fg          Page   @5808
```

Attribute values that matter (`objattribdef-ps3.ini` offsets, relative to the object):

| Object | Attribute | Offset | Value | Role |
|---|---|---:|---|---|
| `xmenu` @3296 | posX | +0 | **-395.0** | the column origin; screen x = 960 + posX = 565 |
| `xmenu` | posY | +4 | 40.0 | column origin y; **no captured draw uses it** (see 3.3) |
| `xmenu` | posZ | +8 | 0.0 | |
| `xmenu` | redScale/greenScale/blueScale/alphaScale | +12..+24 | 1,1,1,1 | column tint/alpha, all identity |
| `xmenu` | width/height/depth | +28..+36 | 0,0,0 | auto: the bar is item-sized, not a box |
| `xmenu` | scaleWidth/scaleHeight/scaleDepth | +40..+48 | 1,1,1 | identity |
| `xmenu` | menus | +88 | **10** | the number of XMList children = the column count |
| `indi_base` @4284 | posX/posY | +0/+4 | 0, **-74** | bar plane centre relative to `indicator` |
| `indi_base` | width/height | +28/+32 | **560 / 82** | the info-bar plane |
| `indi_adjust` @4424 | posX | +0 | **-26** | the clock/icon group origin |
| `clock` @4564 | posX/posY | +0/+4 | 0, **-12** | the date/time text relative to the group |
| `clock` | size | +112 | 10.0 | paf text size unit (not a pixel size) |

The ten `XMList` children carry **no geometry at all**: the schema is `unknown0(4) + image(8) +
ref(8)` = 20 bytes, and in this RCO all ten have both refs unset (`0xFFFFFFFF...`). They are empty
slots, and their file order is the console's column order (user, sysconf, photo, music, video, tv,
game, network, psn, friend), which is also the native's order in `src/shell.cpp:1012`.

`xmb_ingame.rco` is the same tree with the same `xmenu` values (`posX -395` @5996, `indi_base`
560x82 `posY -74` @6984). Its ten `XMList` slots are **not** empty: each carries an image ref
(offsets 0x130..0x34C, step 0x3C) and a ref (indices 7,0,1,2,3,9,4,5,10,6). So the normal-mode bar is
bound at run time and the in-game bar is pre-bound; neither form carries geometry.

### 2.2 The open page: `explore_category_<name>.rco` and `explore_plugin_full.rco`

Every category RCO is the same 24-object page family. `explore_category_sysconf.rco`
(sha256 460d6e896898e171...):

```
ObjectTree @42704
  expage_explore_bg    Page  @42744   z: behind the rows
  expage_explore_list  Page  @42820   z: the row list
  expage_explore_fg    Page  @42896   z: in front of the rows
  expage_menu          Page  @42972   the option menu (Triangle) page
    plane_menu0        Plane @43048   0,0  0x0     <-- menu level 0
      plane_menu0_BG0 / BG1        Plane @43188 / @43328  0x0
      plane_menu0_LowerMenu/UpperMenu  MList @43468 / @43716  0x0
    plane_menu1        Plane @43964   0,0  128x272  <-- menu level 1
    plane_menu2        Plane @44880   0,0  128x272  <-- menu level 2
  info_list            Page  @45796   information page
    info_base_plane    Plane @45872           0x0
  expage_filelist      Page  @46012   (sibling of the family, empty)
  expage_message_dialog Page @46088
```

`explore_plugin_full.rco` (sha256 462d07dec9b660bc...) repeats the family at
`expage_menu` @222852 with `plane_menu0` @222928 (0x0), `plane_menu1` @223844 (128x272),
`plane_menu2` @224760 (128x272), and adds `expage_fcopy_dialog`, `expage_fdelete_dialog`,
`expage_filelist`, `expage_message_dialog`, `control_panel_page`, `volume_page`,
`notification_page`, `poweroff_page`.

Facts that decide the layout question:

1. **A `Page` object has no geometry.** The schema is `unknownInt0(4) + 4 event refs(32)` = 36
   bytes (`page_xmb` @3220 raw: `01110000` then four `ffff0000ffffffff`), i.e. no position, no scale,
   no anchor. Every page is at the scene origin; where it appears on screen is the plugin's camera.
2. **The item row list is not an RCO object.** There is no `List`/`XList`/`MList` object for the item
   rows in any category RCO: the only MLists are `plane_menu{0,1,2}_{Lower,Upper}Menu` inside the
   option-menu pages, with zero geometry. The rows are created by `explore_plugin` from the XMBML
   view's `<Items>` (see 2.3) and positioned by paf.
3. **All geometry in the page family is zero** except the two nested option-menu boxes at 128x272.
   `plane_menu0` (the first menu level) is auto-sized, which is why `audit/ITEM-MENU-INFO-FIRMWARE.md`
   calls the option panel's box "UNATTRIBUTED"; that audit owns that surface and this document does
   not re-derive it.

### 2.3 The XMBML views carry content, not geometry

The 28 `vsh/resource/explore/xmb/category_*.xml` documents define `root` and the per-view `<Items>`
that become the column's rows. A scan of every `<Pair key="...">` in all 28 documents (98 distinct
keys) finds **no position, size, pitch or margin key**. The only position-like key is `ch_pos`, an
ordinal in the folder-child tables (e.g. `category_sysconf.xml` table `game`), not a pixel value.
So: XMBML decides *which rows exist and what they say*; the RCO decides *where the column starts*;
nothing in firmware data decides where a row sits - that is paf.

---

## 3. The transforms, and where each number comes from

### 3.1 The projection (captured vertex constants, exact)

The icon vertex program (`quad.vpo`) receives the object's world position in the w components of
vertex-constant slots 256/257 and the camera distance in 259.w. From any `.vc` capture:

```
[256] = ( 2.8125*size, 0, 0, 2.8125*worldX )
[257] = ( 0, 5*size, 0, 5*worldY )
[259] = ( 0, 0, -1, distance )
[467] = ( ndcX, ndcY )      cross-check: 960*(1+ndcX), 540*(1-ndcY)
```

which gives, at 1920x1080:

```
screen x = 960 + 2700*worldX/d
screen y = 540 - 2700*worldY/d
screen size = size*2700/d
```

2700 is not an independent constant: `2700 = 540 * 5`, i.e. the distance at which one world unit
projects to one 1080p pixel, with 5 the ndc y scale (`ndcY = 5*worldY/d`) and `2.8125 = 5*9/16` the
x scale. Slot 467 agrees with the slot-256/257 arithmetic on all 79 distinct draws of
`fpdump7` (checked in `work/p2-root-geometry.ts vc`), which is why this formula is used as the
authority below rather than the `_Eye` tuple's own x (see 3.6).

### 3.2 The column origin is the RCO value

For the focused category draw, `[256].w = -1110.9375` -> `worldX = -1110.9375/2.8125 = -395.0`,
byte-identical to `xmenu.posX`. The unfocused neighbours at screen 355/155/-45/... have
`worldX = -605/-805/-1005/...`, i.e. `-395 - (200*n + 10)` for the nth slot away. So:

```
worldX(category n) = -395 - 200*(n-focus) - 10*sign(n-focus)
screen x           = 565 + 200*(n-focus) + 10*sign(n-focus)
```

with 565 = 960 + (-395). The bar is a fixed-origin list whose focused item sits at the RCO origin;
the padding applies to **every** non-focused icon, not only the immediate neighbours (the native's
`10*std::clamp(distance,-1,1)` reproduces this by saturation - correct).

The item column shares the same origin: the focused item row's `[256].w` is also `-1110.9375` ->
screen 565. So category icons, the open column's item icons and the open column's labels are all
anchored on one origin, the RCO's `posX`.

### 3.3 What the RCO `posY = 40` does not do

No captured draw uses 40. The vertical positions are authored per draw: item rows at worldY 35
(screen 505) and -140 (680), category icons at 250/260 (screen 290/280), parent rows at 420 / -125
(screens 143.11 / 658.12). None of them contains a +40 term. `xmenu.posY` is therefore either the
bar's local origin inside a plugin-applied transform that is invisible in the captured draws, or
unused for this surface; the native's 290/280 are capture values, not RCO values. Do not derive the
row y from `posY`.

### 3.4 Depth 0 vs depth 1: the 262 shift is a *depth* shift, not a "page open" shift

This corrects the framing in `audit/LAYOUT-TRACE.md` phase 4b, which compared "root" against "page
open". Both frames that exist are different depths:

| State | Bar geometry | Evidence |
|---|---|---|
| depth 0 (no page, or only the category's own rows) | focused icon at 565, neighbours at 200 pitch +10, brightness 0.698 | [FRAME] `out/rpcs3-xmb-cap/settings-b.png`: icons at 354.4 / **566.5** / 775.6 / 977.1 / 1175.6 / 1376.5 / 1574.7 / 1773.6 |
| depth 1 (a page under the category's page) | **one** category icon, at 303; no neighbours at all | [VC] `fp_0243`: (303.00, 280.00) 167.328 amb 1.000 is the only category draw in the settled state; [FRAME] `page-open.png` shows no neighbour icons |

So the bar's 262 px shift fires when the *depth* goes 0 -> 1, exactly as `childAmount =
depthPosition_` does in the native. The native's driver is right; what it does with the neighbours
is wrong (section 5.2).

The same 262 appears on the focused parent row (303) and on the open category icon (303); the
unfocused parent rows move 109.46 (455.51).

### 3.5 The parent column behind a page (depth 1): a world transform, not a screen scale

| | worldX | worldY | d | projected size | screen |
|---|---:|---:|---:|---:|---|
| open column, focused row | -395 | 35 | 2700 | 184.800 | (565, 505) |
| parent, focused row | -657 | 35 | 2700 | 184.800 | (303, 505) |
| parent, row above focus | -533.857 | 420 | 2857.2 | 73.35 | (455.51, 143.11) |
| parent, row below focus | -533.857 | -125 | 2857.2 | 73.35 | (455.51, 658.12) |
| parent, next row | -533.857 | -205 | 2857.2 | 73.35 | (455.51, 733.72) |

Three separate authored changes produce what the native models as one "recession":

1. the column moves back to d 2857.2 (0.945x projection) -> icon 73.35 and all y offsets scaled;
2. the whole column shifts left in world space by 138.857 -> screen 455.51;
3. the row *below* focus is authored at worldY -125 instead of -140 (a 15 wu change), which is why
   the gap below focus closes by 15 px after the scaling.

The native reproduces the net result exactly (`(1-.055*depth)` = 0.944983, `-15`, 2700+157.2) but it
applies (2) as a flat screen-space 110 px and (1) as a screen-space scale about each row's own
centre, so it is a *fit that happens to be exact at rest*, not the firmware transform. That matters
for P2.4 (a 720p/resolution pass must scale this the way the firmware does) and for any depth other
than 0/1.

### 3.6 Why the `_Eye` tuple's x is not a screen coordinate

The decoded `_Eye` (x, y, distance, size) is what the fragment program receives, not the projected
position. For the open column, `_Eye.x = -5` and the draw is at screen 565; for the parent column,
`_Eye.x = 133.86` and the draw is at 455.51. No single offset maps one to the other; the vertex
constants (slot 256.w) do. Slot 467 is the independent check and agrees with 256.w in every draw.
Differences *within one object class at one distance* remain valid (that is what LAYOUT-TRACE phase
4b used), but absolute screen positions in this document always come from 256.w/467, not from
`_Eye.x`.

### 3.7 The info bar: RCO sizes, plugin anchor, capture rect

The RCO gives the plane (560x82 at `posY -74`) and the five indicator icon slots (32x32 at -244,
-274, -342, -376, -410, i.e. a 30-34 px pitch) but no anchor: `indicator` and `indi_adjust` are both
invisible containers at the origin, and `xmb_plugin` computes the page's position at run time.
`out/native-reference/xmb_plugin_all.c:5908-5992` reduces it to
`x = <runtime float table>[0xc] - <same>[0x290] + <same>[7] * 462 (or 282)` plus a second coordinate
`<same>[7] * (-10) + x`, applied through vshcommon property calls whose arguments the local
decompiler drops. The plugin's own fallbacks for those lookups are the `.data` block at
`xmb_plugin.prx` VA 0x18024..0x1805c (file offset = VA + 0xF0, validated against the name table at
0x185a8 = "chat"/"message"/"online"/"indi_base"): 462, 0.5, 82, 88, 282, 60, 200, -10, 4, -4, -72,
1, 0, 4, 0. So the absolute anchor is not recoverable from the RCO and the captured rect is the
authority:

* [FRAME] `out/native-reference/ab-rpcs3-root.png`: bar spans x 1329..1919 (591), y 75..132 (58);
  bright left edge at x 1329-1332, top edge at y 75-76, bottom edge at y 131-132; the date/time text
  is right-aligned ending near x 1760 with a circular clock glyph at 1776..1803 x 88..118.
* The RCO plane is 560x82, i.e. 31 px narrower and 24 px taller than the visible band. It is *not*
  the visible band's rect; `audit/INFO-BAR.md:7` already warns against replacing the measured
  geometry with these template fields. Reconciliation needs the plane's runtime transform, which is
  the plugin code quoted above.

### 3.8 Provenance ledger: RCO vs plugin constant vs capture

The short answer to "what should be read, what stays a documented constant, and what is only
measurable", per value:

| Value | RCO attribute? | Plugin/module constant? | Capture-only? |
|---|:-:|:-:|:-:|
| column origin 565 (= 960 + `posX -395`) | **yes** (`xmenu` @3296+0) | - | - |
| column count 10 | **yes** (`xmenu.menus` @3296+88) | - | - |
| column order (10 categories) | partial: XMList slot order | - | confirmed by frames |
| pitch 200 | no | 200.0 at `vsh.elf` 0x68f10c/0x68f118 (read by the scroll code) | **yes** (per-draw world x) |
| focus padding 10 | no | 10.0 at `vsh.elf` 0x68f108/0x68f12c (same block) | **yes** |
| icon row y 290/280, quads 117.13/167.328, brightness 0.698/1.0 | no | no | **yes** |
| item focus row 505, pitch 80, +95, -305 | no | 80.0 at `vsh.elf` 0x68f144/0x68f178, call-site semantics unproven | **yes** |
| base distance 2700 | no | derivable: 540 x 5 | **yes** (259.w) |
| parent distance 2857.2 (157.2), parent x 455.51 / 303, gap 15 | no | no | **yes** |
| label x 678 (level 0) / 622 (level 1), label y, text sizes | no | no (paf places text) | **yes** |
| info-bar plane 560x82 `posY -74`, five 32x32 icon slots | **yes** (`indi_base` @4284, @4836-@5668) | anchor: `xmb_plugin` code, args unreadable | **yes** (rect 591x58, text end, clock centre) |
| option-menu planes 128x272 at the origin | **yes** (`expage_menu` planes) | rows/anchor: paf `PhMenuList` | none in the local set (sibling audit) |
| page family (bg/list/fg/menu) | **yes**, zero geometry | creation/z-order in the plugin | - |

---

## 4. Every hardcoded layout number against firmware data

Legend: **EXACT** = firmware data reproduces the constant; **DERIVED** = the constant is arithmetic
on captured values (exact by construction); **FIT** = measured, but not traced to a firmware value;
**CONTRADICTED** = firmware data disagrees; **MISSING** = the native has no counterpart for a
firmware value. `[VC]` = decoded vertex constant from `out/native-reference/fpdump7`
(2,468 `.vc` files, 79 distinct draws); `[FRAME]` = measured on a capture in this pass;
`[RCO]` = RCO attribute; `[PLUGIN]` = module literal/flow.

### 4.1 Category column (root page)

| # | Native constant | Code site | Firmware value + evidence | Delta | Status |
|---|---|---|---|---|---|
| 1 | `kColumnX = 565` | `view.h:217` | [RCO] `xmenu.posX -395.0` @3296+0; [VC] focused draw `[256].w/2.8125 = -395.0` -> 960-395 | 0 | EXACT |
| 2 | pitch `200` | `view.cpp:1221` | [VC] unfocused draws 355/155/-45/-245/-445 and 775/975/1175/1375, step 200.00; [PLUGIN] 200.0 at `vsh.elf` 0x68f10c, 0x68f118 | 0 | EXACT |
| 3 | focus padding `10` | `view.cpp:1221` | [VC] focus at 565, neighbours at 355/775 = +-210; [PLUGIN] 10.0 at `vsh.elf` 0x68f108, 0x68f12c | 0 | EXACT |
| 4 | icon row y `290 - 10*focus` | `view.cpp:1232`, `:1235` | [VC] unfocused 290.00, focused 280.00 (`_Eye.y` -250/-260, worldY 250/260) | 0 | EXACT |
| 5 | quad `117.13 + 50.2*focus` | `view.cpp:1231`, `:1235` | [VC] 117.130 unfocused, 167.328 focused | 0.002 | EXACT |
| 6 | brightness `.698 + .302*focus` | `view.cpp:1231`, `:1235` | [VC] `_Ambiant.w` 0.698 unfocused, 1.000 focused | 0 | EXACT |
| 7 | open-page shift `262` | `view.cpp:1225` | [VC] bar 565 -> 303.00; focused parent row 303.00; open category icon 303.00 | 0 | EXACT (but see 5.1 for *what* it keys on) |
| 8 | faint pitch `189.0` | `view.cpp:1225` | [FRAME] 190.5 (phase 4c autocorrelation, four frame pairs); [VC] the transition bar layer at d 2483.53 projects 200 wu to 217.4 | -1.5 px vs 190.5; -28.4 px vs the decoded layer | FIT (not a firmware constant; the layer it claims to model is not the visible faint row) |
| 9 | faint anchor `kColumnX-40` = 525 | `view.cpp:1225` | [FRAME] the faint row fits `565 + 190.5 n` (phase 4c) | **-40** | CONTRADICTED |
| 10 | faint alpha `0.1*childAmount` | `view.cpp:1227` | [VC] transition `_Ambiant.w` series starts 0.067 and rises to 0.698 | +0.033 | CONTRADICTED (measured upper bound 0.067) |
| 11 | Qt category icon `76 + 16*focus`, brightness `.67+.33*focus` | `view.cpp:1236` | [VC] every captured category draw is the full quad 117.130/167.328 at 0.698/1.000 (11 distinct positions across the capture's states) | -41.1 / -75.3 px | FIT (a visible-glyph fit, not the quad; P4 owns per-icon coverage) |
| 12 | category title `{x-140, 335, 280, 40}`, 21 px, centred | `view.cpp:1237` | [FRAME] `ab-rpcs3-root.png` "Game" ink x 536..593 (centre 564.5), y 342..358 (centre 350) | 0 x; -5 y (ink vs box) | EXACT within glyph metrics |
| 13 | category-switch slide `150` | `view.cpp:1407`, `:1408` | no capture decoded for the category move in this pass | n/a | FIT (no firmware counterpart found) |
| 14 | column count (implicit: 10 hardcoded categories) | `shell.cpp:1012` | [RCO] `xmenu.menus = 10` @3296+88 and ten XMList slots in the same order | 0 | EXACT (assert at run time, 6.1) |

### 4.2 Item rows (open column, depth 0 and depth 1)

| # | Native constant | Code site | Firmware value + evidence | Delta | Status |
|---|---|---|---|---|---|
| 15 | focus row y `505` | `view.cpp:1280` | [VC] (565, 505) focused item, (565, 505) focused bullet, (303, 505) parent focused row | 0 | EXACT |
| 16 | row pitch `80` | `view.cpp:1273` | [VC] 680/760/840/920/1000/1080/1160/1240/1320; parent 143.11/658.12/733.72/... (74.60 step at 2857.2); [PLUGIN] 80.0 at `vsh.elf` 0x68f144, 0x68f178, read by the widget-layout switch at `paf-range.c:29732`, `:29954` | 0 | EXACT |
| 17 | first row below focus `+95` | `view.cpp:1273` | [VC] 680 - 505 = 175 = 80 + 95 | 0 | EXACT |
| 18 | row above focus, level 0 `-305` | `view.cpp:1274` | [VC] (565, 120) row above focus = 505 - 385 = 505 - (80+305); [FRAME] `settings-b.png` "System Update" text band 107..138 (row y 120); [FRAME] `ab-rpcs3-root.png` the two rows above the band at y 123.2 and 41.3 = 505-385 and 505-465 | 0 | EXACT |
| 19 | row above focus, level>0 `-95` | `view.cpp:1274` | not captured (no deeper page in the local set has rows above focus) | n/a | FIT |
| 20 | base distance `2700` | `view.cpp:1333` | [VC] 259.w = 2700.00 on every column draw | 0 | EXACT |
| 21 | parent distance `+157.2*depth` | `view.cpp:1333` | [VC] 259.w = 2857.20 for every parent-column draw; 2857.2 - 2700 = 157.2 | 0 | DERIVED (exact by construction) |
| 22 | depth scale `(1-.055*depth)` | `view.cpp:1280` | [VC] 2700/2857.2 = 0.944983; the native's 0.945 gives 4.6e-5 error (<0.01 px) | ~0 | DERIVED |
| 23 | gap closure `-15*depth` | `view.cpp:1280` | [VC] parent row below focus worldY -125 vs -140 open column (15 wu authored) | 0 | EXACT |
| 24 | parent unfocused row x `110` | `view.cpp:1282` | [VC]/[FRAME] 455.51 (`worldX -533.857` at 2857.2) vs native 565-110 = 455.00 | **-0.51 px** | CONTRADICTED (0.51 px; P2's gate is 0.5) |
| 25 | parent focused row x `152` (110+152=262) | `view.cpp:1282` | [VC] 303.00 exactly | 0 | EXACT |
| 26 | incoming column `ahead*250` | `view.cpp:1282`, `:1350`, `:1400` | [VC] transition: the incoming column is at 774.09 (one slot: 565+210, the -0.91 being the frame's global offset), scale 1.1022 | **+40 px** | CONTRADICTED |
| 27 | page-entry scale (none) | `view.cpp:1301` | [VC] transition draws 203.700 (focused) and 85.554 (rows) = 1.1022x the 184.8/77.616 pair | missing | MISSING (open gap, `audit/ITEM-ICONS.md`) |
| 28 | item quad `77.616 + 107.184*focus` | `view.cpp:352-359` | [VC] 77.616 / 184.800 | 0 | EXACT |
| 29 | bullet quad `35.721 + 49.329*focus` | `view.cpp:352-359` | [VC] 35.721 / 85.050 | 0 | EXACT |
| 30 | Qt item icon `60 + 44*focus` (level 0), `40 + 16*focus` (level >0) | `view.cpp:1287` | [VC] the row quad is 77.616/184.800 for every captured level | L0 -17.6 / -80.8; L1 -37.6 / -128.8 | FIT (visible-glyph fit; P4) |
| 31 | Qt bullet `18 + 21*focus` | `view.cpp:1287` | [VC] 35.721/85.050 | -17.7 / -46 | FIT (visible-glyph fit; P4) |
| 32 | artwork tile `155 + 165*focus` x `90 + 90*focus` | `view.cpp:1302` | [FRAME] `ab-rpcs3-root.png` focused artwork row: card roughly 300x180 at about (550, 490) | about -20 wide | FIT (one frame, not measured to a spec; needs its own pass) |
| 33 | parent brightness `1-behind*(1-(.247+.451*focus))` | `view.cpp:1311`, `:1332` | [VC] 0.247 unfocused, 0.698 focused behind a page; 1.000 in the open column | 0 | EXACT |
| 34 | label x `kColumnX + (level==0 ? (tile?173:99) : 57)` | `view.cpp:1350` | [FRAME] level 0, non-tile rows (`settings-b.png`): ink 684 on every row (pen ~678); level 0, artwork row (`ab-rpcs3-root.png`): ink 724 (pen ~718) next to a ~300 px card ending at x ~700; level 1 (`page-open.png`): ink 628 (pen ~622) | non-tile L0: **-14**; tile L0: **+20**; L1: 0 | CONTRADICTED at level 0 (see 5.4); the tile offset is not a constant, it follows the artwork's right edge |
| 35 | label box `{labelX, y-28, ..., 56}`, 27 px | `view.cpp:1356`, `:1371` | [FRAME] label ink height 492..519 (27 px row band) at the row y +-28 | 0 | FIT (paf text box; P3 owns metrics) |
| 36 | value column x `1300`, width 440, 21 px | `view.cpp:1390` | [FRAME] `page-open.png` second-row value "Off" ink 1301..1326 | +1 | EXACT |
| 37 | subtitle `+20`, 17 px; detail `+24`, 18 px | `view.cpp:1393`, `:1396` | [FRAME] `page-open.png` description ink band 527..545, i.e. ink top +22 from the row y | native ink top ~531 -> ~4 px low | FIT |
| 38 | back arrow `{kColumnX-142, 480, 50, 50}`, 42 px | `view.cpp:1400` | [FRAME] `page-open.png` "<" ink 433..449 x 495..513, centre (441, 504) vs the native box centre (448, 505) | -7 x (glyph bearing) | FIT (no firmware counterpart) |
| 39 | label elision limits `1050` / `640` | `view.cpp:1364` | no firmware counterpart (PAF draws a character range) | n/a | FIT (P3) |

### 4.3 Info bar (root page)

| # | Native constant | Code site | Firmware value + evidence | Delta | Status |
|---|---|---|---|---|---|
| 40 | bar rect `(1329, 75, 591, 58)` | `view.cpp:1422` | [FRAME] x 1329..1919, y 75..132 (591x58) in `ab-rpcs3-root.png` | 0 | EXACT against the frame; [RCO] plane is 560x82 -> not the same box |
| 41 | clock text `{1240, 82, 525, 45}` 27 px right-aligned | `view.cpp:1426` | [FRAME] text right edge ~1760 (native rect right edge 1765) | -5 | FIT |
| 42 | clock face centre `(1791, 104)`, r 14, hands 10/7 | `view.cpp:1427` | [FRAME] circle 1776..1803 x 88..118 -> centre (1789.5, 103), r 13.5 | 1.5 / 1.0 | FIT |
| 43 | indicator icon slots | none | [RCO] five 32x32 planes at -244..-410 relative to `indi_adjust` (30-34 px pitch) | missing | MISSING (the native draws no info-bar status icons) |
| 44 | `indi_base` 560x82 at `posY -74`, `indi_adjust posX -26`, `clock posY -12` | none | [RCO] @4284, @4424, @4564 | missing | MISSING (relative offsets only; the anchor is plugin-computed, 3.7) |

### 4.4 Options panel (deferred to `audit/ITEM-MENU-INFO-FIRMWARE.md` A6)

The native's panel numbers (`x0 = 1330 + ahead*600` `view.cpp:1110`; first row `505-(n-1)*20`
`:1112`; pitch 40 `:1135`; label `x0+16`/`x0+34` `:1133`; 23 px text `:1148`; heading 19 px `:1131`;
note 16 px `:1155`) have **no RCO counterpart** - `expage_menu`'s planes are all zero except the
128x272 nested boxes - and **no local capture** exists of a firmware Triangle menu. That audit owns
the surface and the measurement list; this document only records that the RCO side is exhausted.

---

## 5. Structural findings (wrong shape, not just wrong offset)

### 5.1 The 262 shift is keyed on depth, and the neighbours' fate is wrong (severity: high)

Depth 0 keeps the full bar: `settings-b.png` shows eight category icons on the 200+10 grid with the
focused one at 565 unshifted. Depth 1 keeps exactly one category icon at 303 and nothing else:
`[VC] fp_0243` is the only category-size draw in the settled open state, and `page-open.png` shows
no neighbour at 4x contrast.

The native draws the neighbours at depth 1 in both directions:
`distance<0` icons shift by 262 (so the neighbour at 565-210-262 = 93 is on screen), and
`distance>0` icons get the `525 + 189*distance` branch at 0.1 alpha. Both should be absent at rest;
both are wrong in pitch and anchor during the transition (5.2).

Proof frames: `out/rpcs3-xmb-cap/settings-b.png` (depth 0, neighbours present),
`out/rpcs3-xmb-cap/page-open.png` (depth 1, none), `out/native-reference/fpdump7/fp_0243_*.vc`
(the settled open state's category draw list).

### 5.2 The page-open transition's faint row geometry is not the console's (severity: medium)

During the open/close transition the console recedes the *whole bar* to d 2483.53 (200 wu -> 217.4 px)
and moves the focused icon toward its destination, keeping the neighbours relative to the moving
focused icon. `[VC] fp_0038..fp_0062` (and the same frames in the second capture
`C:\dev\out\rpcs3-xmb-cap\fp-open`) at d 2483.53 put the focused draw (size 167.315) at 530.85 and
the neighbours at 302.54 / 759.15 / 85.11 / 1194.01, i.e. `moving_focus +- (217.4*n + 10)` within the
frame's global -0.91 px offset. The neighbours' **y also moves**: worldY 250 at d 2483.53 projects to
268.21, not 290, and the focused icon's y moves 280 -> 257.34. The native keeps both at their 1080p
values (290-10*focus) and a fixed anchor at 525 with a fixed 189 pitch, so during the transition every
neighbour is 40+ px off in x, 22 px off in y, and the bar never recedes.

Separately, phase 4c measured the *visible* faint row's pitch at 190.5 with a fit `x = 565 + 190.5n`.
That number is a frame measurement of a different, cheaper draw layer - the decoded
`icons_no_ss_no_shad` category layer during the same frames is at d 2483.53 and 217.4 px pitch - so
189 is neither the frame measurement nor the decoded layer's pitch. The correct target for the
transition is the receding-bar model above; the 186/189 constant should not survive.

### 5.3 The incoming page column's offset and scale (severity: medium, transition-only)

`[VC] fp_0174..fp_0180`: the incoming column is drawn at 774.09 (= 565 + 210, one slot) with
`_Eye.w` 85.554 (77.616 x 1.10223) and 203.700 (184.8 x 1.10227), i.e. one slot right and 10% larger.
The native uses `ahead*250` and no scale. Forty pixels and the whole scale-in are missing. This is
the same gap as `audit/ITEM-ICONS.md`'s "Wiring B", now with the offset measured as well.

### 5.4 The level-0 label column is 14 px left of the console's (severity: medium, visible on every first-level page)

Measured label ink left edges:

| state | rows | ink left | implied pen | native pen | delta |
|---|---|---:|---:|---:|---:|
| level 0 (`settings-b.png`) | non-tile, focused and unfocused | 684 | ~678 | 664 | **-14** |
| level 0 (`ab-rpcs3-root.png`) | one artwork row (focused, ~300 px card ending x ~700) | 724 | ~718 | 738 | **+20** |
| level 1 (`page-open.png`) | non-tile, focused and unfocused | 628 | ~622 | 622 | 0 |

With a ~6 px left side bearing at 27 px, the console's non-tile offsets are **+113** at level 0 and
**+57** at level 1; the native has 99 and 57, so it gets level 1 exactly, level 0 14 px left, and its
level-to-level delta is 42 where the console's is 56. The +113 is also 2x57 within 1 px, which is a
candidate rule (the first level indents twice as far) that one capture of a two-level-deep folder
page would confirm.

The artwork row does not follow a label constant at all: its label sits just past the card's right
edge (card 400..700, label 718), so the native's `tile?173` is a proxy for "half card width + gap"
and moves whenever the card does. 173 puts the label 20 px right of the measured position because
the native's card is 320 px wide where the console's is ~300 px (4.2 #32).

Why this was never caught: no earlier capture in the local set is a level-0 page. `settings-b.png`
is the first one, and it is the frame that settles the non-tile case.

### 5.5 The parent column's label has no transform (severity: low, transition-only)

`view.cpp:1350` computes `labelX` with no `behind` term, so the parent column's labels sit at
565+113 (level 0), i.e. on top of the open column's labels, while their icons are at 455.51/303.
At rest the labels are invisible because their opacity is `columnAlpha*(1-behind)` = 0, and the
captured page shows exactly one label at the open column's x, so this is invisible today. It becomes
visible during the page move, when `behind` is between 0 and 1 and the label is drawn at the wrong x
while fading out. No capture decodes text draws, so the fix direction is "hide the parent label with
the same opacity as its icons, and keep its x on the icon" rather than a measured value.

### 5.6 The info bar's RCO plane is not the drawn box (severity: low, attribution)

560x82 (RCO) vs 591x58 (frame). Neither the width nor the height matches, and the anchor is plugin
code. The native's measured rect is the better authority today. What the RCO *can* supply is the
five status-icon slots (32x32, 30-34 px pitch) that the native does not draw at all - that is a
missing element rather than a wrong one, and it belongs to the info-bar/notification pass.

---

## 6. Implementation spec

### 6.1 What `rco_objects` should expose and the view should read at run time

The P1 module parses the RCO object model; for this surface it needs a lookup by object name that
returns the standard position block. Suggested (names only - the API shape belongs to P1):

```
struct RcoObject { int offset; QString type; /* + the attributes below, -inf when absent */ };
struct RcoTransform {   // "standard position definition" shared by Plane/XMenu/List/Button...
    float posX, posY, posZ;            // +0, +4, +8
    float rgbaScale[4];                // +12..+24
    float width, height, depth;        // +28..+36
    float scale[3];                    // +40..+48
};
// XMenu adds: int menus (+88), ref attrs at +56..+128
RcoObject* find(const QString& resource, const QString& name);   // by name, with offset
```

Reads the view should make (each with a fallback to the documented constant and one log line):

| Value | Source | Use |
|---|---|---|
| `xmenu.posX` (-395) | `xmb_plugin_normal.rco` @3296+0 | `columnOriginX = 960 + posX` replaces `kColumnX` (565). Keep 565 as the fallback when the RCO is absent |
| `xmenu.menus` (10) | same @3296+88 | assert equality with `categories_.size()` at start-up; log a mismatch (a column count difference is a parity break) |
| `xmenu.rgbaScale` / `scale*` (all 1) | +12..+24, +40..+48 | assert identity; a theme/preset that changes them is the signal that the column is transformed |
| `xmenu.width/height/depth` (0,0,0) | +28..+36 | assert 0; documents that the bar is item-sized |
| XMList names + order | @3468..@4008 | assert the ten names map to the existing category ids in the same order (they do today); do not read geometry (there is none) |
| `indi_base` w/h/posY (560/82/-74) | @4284+28/+32/+4 | keep beside the measured bar rect as a debug comparison; **do not** replace the rect (`audit/INFO-BAR.md`) |
| `indi_adjust.posX` (-26), `clock.posY` (-12) | @4424+0, @4564+4 | relative offsets for the info-bar pass (not the anchor) |
| indicator icon slots (32x32 at -244/-274/-342/-376/-410) | @4836..@5668 | the pitch (30-34) and sizes for the info-bar status icons, once that pass is funded |

Everything else in section 4 stays a documented constant with its provenance in the comment: the
row/icon/pitch/quads/brightness (captured vertex constants), the 110/152 parent shift (capture),
the 505/80/95/305 rows (capture), the label 113/57 (capture), the text sizes (P3), the tile sizes
(one frame), the info-bar rect (capture).

The rule from `PARITY-PHASES.md` applies: an RCO value is read at run time or generated from a
documented extraction; a fitted constant is labelled `fit` with the measurement that bounds it.
Numbers 8, 13, 19, 27 (the missing entry scale), 30, 31, 32, 35, 37, 38, 39, 41 and 42 above are
`fit`/`missing` and must be labelled as such in the source comment today.

### 6.2 `view.cpp` changes for the later stage (not applied)

In order of value per line changed:

1. **Depth-1 neighbours (5.1).** In the category loop, when `childAmount >= .999` draw only
   `i == shell_.category()`; during the transition draw the neighbours with the receding model:
   `x = movingFocusX + (200*n + 10*sign(n))*2700/dRecede`, `y = 540 - 250*2700/dRecede`, with
   `dRecede` easing 2700 -> 2483.53 and `movingFocusX` easing 565 -> 303, and opacity <= 0.067 while
   open (the return's first captured step is 0.067) fading in with the bar. Delete the `kColumnX-40`
   anchor and the 189 pitch.
2. **Level-0 label (5.4).** `view.cpp:1350`: `(level==0 ? (tile?173:113) : 57)` closes the non-tile
   gap exactly; the tile branch should become "artwork right edge + 18" rather than a constant, and
   it depends on fixing the card width first (4.2 #32), which is why it is second.
3. **Parent unfocused x (4.2 #24).** `view.cpp:1282`: 110 -> 109.46 (or compute
   `565 - 109.457`), which removes the only >0.5 px delta in the item-row geometry.
4. **Incoming column (5.3).** `view.cpp:1282`, `:1350`, `:1400`: `ahead*250` -> `ahead*210`, and add
   the entry scale `1 + .10223*clamp(ahead,0,1)` on the incoming column's icons (Wiring B in
   `audit/ITEM-ICONS.md`; needs a settled deeper-page capture to confirm it is entry-only, which is
   why it is listed after 1-3).
5. **Depth-keyed 262 (5.1).** Keep the expression, fix the comment: it is the depth-0 -> depth-1 bar
   shift, not "page open", and it applies to the focused category and the focused parent row only.
   The unfocused parent rows use 109.46, not 262.
6. **`kColumnX`.** Replace the literal with `960 + rco.xmenu().posX`, fallback 565, once
   `rco_objects` is linked. Add the `menus == categories_.size()` assert.
7. **Parent label opacity (5.5).** Multiply `labelX`'s block by the same `(1-behind)` the icons use
   (or skip `behind > .001` labels entirely) so the fading parent label cannot paint at the open
   column's x.

Nothing here changes the drawing of the item rows themselves: rows 15-18, 20-23, 28, 29 and 33 in
section 4.2 are exact today and the constants must not be "cleaned up".

### 6.3 What P2.4 (resolution) inherits from this pass

The layout is authored in world units with a 1 wu = 1 px mapping at 1080p, and the projection is
`540*(1 - 5*worldY/d)` vertically. A 720p output is the same world geometry projected with half the
half-height (360), i.e. a uniform 2/3 scale of the whole 1920x1080 layout - which is what
`icons.cpp:153-154` already does with `height/1080`. The parent-column transform (3.5) looks like an
exception but is not: it is a world-space translation plus a distance change, and both derive from
the same projection, so scaling the whole layout uniformly is equivalent to re-projecting it. The
open question in P2.4 is therefore PAF's filter/rounding (P10), not the layout arithmetic.

---

## 7. Reproduction (local, read-only)

```
# object trees and raw attributes (offsets, refs, image refs)
bun work/rco-tree-dump.ts --res xmb_plugin_normal.rco
bun work/rco-tree-dump.ts --res explore_category_sysconf.rco
bun work/p2-root-geometry.ts rco <dev_flash>\vsh\resource\xmb_plugin_normal.rco "^(xmenu|list_game|indi_base|clock)$"

# the exact screen geometry of every captured icon draw (slot 256/257/259/467 arithmetic)
bun work/p2-root-geometry.ts vc out/native-reference/fpdump7
bun work/p2-root-geometry.ts vc C:\dev\out\rpcs3-xmb-cap\fp-open

# frame measurements (blobs = icon centres; band = row/column luma profiles)
bun work/p2-root-geometry.ts blobs out/native-reference/ab-rpcs3-root.png "0,180,1920,240" 18
bun work/p2-root-geometry.ts blobs C:\dev\out\rpcs3-xmb-cap\settings-b.png "200,200,1720,180" 22

# module literals: which layout numbers exist in the firmware modules at all
bun work/p2-plugin-float-scan.ts fw493/vsh_module/explore_plugin.prx 80 95 305 505 110 152 57 99
bun work/p2-plugin-float-scan.ts fw493/vsh_module/vsh.elf 80 200 10 95 305 505 0.945 157.2 2857.2
```

Results of the two scans (negative result that motivates section 6.1): `explore_plugin.prx` has
float32 80, 110, 250, 200, 40, 27 and 15 but **no** 95, 305, 505, 152, 57, 99, 173, 210, 262, 189,
186, 640, 1050, 0.945, 0.055, 157.2, 2857.2, 2700, 0.247, 0.451, 0.698, 117.13, 167.328, 77.616,
184.8, 35.721, 85.05, 85.554 or 1.10223; `vsh.elf` has 80.0, 200.0 and 10.0 in the widget-default
block (0x68f144/0x68f178, 0x68f10c/0x68f118, 0x68f108/0x68f12c) and none of the fractional values.

A float existing is not evidence that it is the layout constant. Two examples that could mislead:
`vsh.elf` 0x6fb274 holds 57.0, but it is a field of a 0x1c-byte descriptor table starting at
0x6fb268 (`paf-range.c:71489`, indexed by `iVar5`), not a label offset; `vsh.elf` 0x6824e0 and
0x6a4370 hold 210.0 with no xref in the local decompile. That is why section 4 marks the row and
label numbers as capture-derived: the same value appearing somewhere is not a trace, and only the
widget-default block (0x68eec0-0x68f1e8, which also holds the `xmbar_orig`/`PhXmBar` names) has
literals that the widget code actually reads.

Scratch frames used for measurement live under `C:\dev\out\p2-geometry\` (info-bar crop,
open-page crop, downscaled frames) and are not committed; the helpers are `work/p2-root-geometry.ts`
and `work/p2-plugin-float-scan.ts`.

---

## 8. Handoff summary

* The root column's geometry is **RCO origin + per-draw constants**: `xmenu.posX -395`
  (`xmb_plugin_normal.rco` @3296) is the one RCO number that reaches the screen, and it does so
  verbatim as the icon's world x. `posY 40` reaches nothing captured. The ten `XMList` slots carry
  no geometry (and no refs in normal mode); `xmenu.menus = 10` is the column count.
* The open page's geometry is **not in the RCO at all**: pages have no attributes, the item rows are
  paf-created text/icons, and all page-family planes are zero except the two nested option-menu boxes
  at 128x272.
* Exact today: column origin 565, pitch 200, focus padding 10, row y 290/280, quads 117.13/167.328,
  brightness 0.698/1.0, shift 262, focus row 505, pitch 80, +95, -305, base 2700, parent 2857.2,
  gap 15, depth scale 0.945, parent focused x 303, item/bullet quads, item brightness, value x 1300,
  info-bar rect (against the frame), level-1 label +57.
* Wrong: level-0 label +99 (should be 113), parent unfocused x 110 (should be 109.46), depth-1
  neighbour categories drawn at all, faint pitch/anchor 189/525, faint alpha 0.1 (measured <=0.067),
  incoming column 250 (should be 210) with no 1.1022 entry scale.
* The single capture that would close the most remaining uncertainty: an RPCS3 frame of a
  two-level-deep page (a folder inside a category page) to confirm the label rule, plus a Triangle
  option menu for the sibling audit's eight numbers.

---

## 9. Dated correction - 2026-10-27/28 capture batch

Evidence added after sections 1-8 were written: one RPCS3 session of the real 4.93 XMB at 1920x1080
(`C:\dev\out\rpcs3-ref-2026-10-28\`, `MANIFEST.md`). This section records only what bears on this
document's numbers: the info bar (3.7, 4.3), the option-menu deferral (4.4), and the settings-page grid
that reuses the item-row geometry (4.2). Nothing above is deleted. Frame paths are relative to the
capture root.

### 9.1 Info bar: the plate does not grow with the clock string (3.7, 4.3)

Section 3.7 ends with the reconciliation open ("it is *not* the visible band's rect ... Reconciliation
needs the plane's runtime transform"), and 4.3 #40 records the measured 591x58 against the RCO's 560x82.
The batch captured the same bar with a longer date string:

| quantity | `10/27 9:53 PM` (MM/DD, 12-hour) | `25/10 12:32` (DD/MM, 24-hour) |
|---|---|---|
| plate | x 1329..1919, y 75..132 (591 x 58) | identical |
| clock text right edge | x 1760 | x 1760 |
| clock text left edge | x 1562 | x 1592 |
| clock glyph | x 1774..1805, y 88..119 | identical |

Frames: `dialogs/root-clock-mmdd-12h.png`, `-2.png`; the existing
`C:\dev\xmb\out\native-reference\ab-rpcs3-root.png`. So the measured 591x58 is **not** a
short-string artifact: the plate is fixed and the text is right-aligned inside it. The 560x82 authored
`indi_base` box is still a different object, and *why* the runtime box differs remains open (3.7's
hypothesis is unchanged, not confirmed).

This also closes the second half of the capture list in 3.7 ("a second grab with a longer date format
... to see whether the plate grows with the string"): it does not.

Clock text: 4.3 #41 records the native rect right edge at 1765 and the frame at 1760. The batch gives 1760
in both formats, so #41's -5 px delta stands. `MANIFEST.md` section 6's "both end at x~1880" is not an
edge in either frame; the glyph ends at 1805.

### 9.2 Options panel (4.4): the deferral is now partly answered

Section 4.4 defers the native's panel numbers to `ITEM-MENU-INFO-FIRMWARE.md` A6 "and no local capture
exists of a firmware Triangle menu". The batch supplies that capture; the full account is
`ITEM-MENU-INFO-FIRMWARE.md` E1. The numbers that bear on this document's 4.4:

| native value (4.4) | firmware, measured | delta |
|---|---|---|
| panel left edge `x0 = 1330 + ahead*600` | left edge x ~1330 (full-height band, fading out by x ~1690) | **0** on the left edge; the native's strip (A6 and `SETTINGS-FIRMWARE.md` 3.2) is described as reaching the right edge, while the firmware fades out by ~1690 |
| panel spans the full height 0..1080 | also full height | 0 in shape |
| first row `505 - (rows-1)*20` (465 for 3 rows) | `Start` baseline ~560 | the native's 3-row block sits ~95 px high |
| row pitch 40 | 38 | -2 |
| label x `x0+16` = 1346 | text left x ~1334 | -12 |
| row text 23 px, `255*(.78+.22*focus)` | not measured | font size still open |

So the native's panel *shape and left edge* are closer to the firmware than 4.4 implies (the RCO's
"floating box" reading is contradicted by the screen), and the real deltas are the vertical anchor, the
38 vs 40 pitch, the 12 px label inset and the missing right-hand fade.

### 9.3 Settings pages reuse the item-row grid (4.2)

The item-row constants in 4.2 (focus 505, pitch 80, next row +175) are not specific to the XMB root: the
settings list pages use the same grid. Measured on `settings/accessory-rest.png`,
`page-video-settings.png`, `page-system-settings.png` and `page-sound-settings.png`: the focused row's
label ink is y 490..519 (centre 505), the rows below start at y 682/762/842/922/1002, i.e. **pitch 80**,
and the first step is 177 px (the focused row's two-line expansion). Values start at x 1301, matching
4.2 #36's value column.

This is a confirmation rather than a correction: 4.2 #15/#16/#17 are exact for settings pages too, which
is why `SETTINGS-FIRMWARE.md` D24's native 80 px pitch is right and only the expansion is missing.

### 9.4 Not resolved here

- The two-level-deep page (section 8's "single capture that would close the most uncertainty") was not
  taken.
- The artwork row (4.2 #32) and the level-0 label rule (5.4, +113) are untouched by this batch; no
  captured page is a level-0 page with a non-tile row other than the existing `settings-b.png`.
- The parent column, depth-1 neighbours, faint row and incoming column (5.1-5.3) are untouched.
- The info-bar anchor/reconciliation (3.7, 5.6) is still an explanation short: the rect is measured, the
  authored 560x82 is not the drawn box.

No source file was edited for this section.
