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

---

## 10. Dated correction - 2026-10-28 capture batch 2

Evidence added after section 9 was written: a second targeted RPCS3 batch on the same 4.93 XMB, one session
in two boots, 1920x1080 (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md`): 251 stills, 22,149 burst
frames in 19 bursts (1:1 crops, 131-235 fps), **clock scale 100 %**. Nothing above is deleted. Frame paths
are relative to the capture root; the numbers marked "(re-measured)" were checked against the cited still
for this section.

This section records three things that bear on this document's numbers: the item plate rectangle (4.2, and
the `BG1` object the RCO names but does not place), the **depth-2 page** - the capture section 8 called the
single one that would close the most uncertainty - and the three band endpoints that 4.4 left to
`ITEM-MENU-INFO-FIRMWARE.md`.

### 10.1 The item plate: a rect at last, and the settings row that has none

Section 4.2 measures the item grid (focus row 505, pitch 80, first step +175) but contains **no plate
rectangle** - the RCO's `BG1` planes are all `posX/posY/width/height = 0` (2.2), so the focus plate's screen
rect was never a firmware number. The batch supplies it at 1080p:

| quantity | value | frame |
|---|---|---|
| XMB item plate (focused item in a column) | **x 405-731**, y about **420-560** | `error/column-corrupted-focused.png` |
| plate size | 326 x 140 | same |
| fill | soft vertical gradient, peak about 126 luma at y 520 over the icon; re-measured 136 at x 410 on y 520, dropping to 80 at x 730-750 | same |

So the focused item is a plate **326 px wide and 140 px tall**, centred on the focus row (420-560 straddles
505) and starting 160 px left of the label column - it is an object behind the icon and the label, not a
full-width row band. 4.2 #32 (the artwork tile, "about 300x180 at about (550, 490)") is a *different*
object: the plate is not the artwork card, and its width does not track the artwork.

**The settings pages do not do this.** On a depth-1 settings page at rest the focused row has **no filled
rectangle at all**: `bands/theme-page-ready.png` (Theme Settings, `Color` focused) reads 3-5 luma from
x 405 to x 1300 across y 470-560, the same value as the rows above and below, with the row marked only by
brighter text and a soft gradient confined to the text block (10-18 luma over the 3-4 background inside
y 470-500, back to 3.9 by y 545-560). Detail and consequences: `SETTINGS-FIRMWARE.md` 12.6.

That is the structural finding of this section: the two surfaces share the **grid** (focus row 505, pitch
80, label column) and not the **focus language**. Section 9.3's "settings pages reuse the item-row grid" is
confirmed for the grid and must not be read as "and therefore the plate". A view that reuses one focus
renderer for both is wrong on one of them.

### 10.2 The depth-2 page (section 8's outstanding capture)

Section 8 named a two-level-deep page as the single capture that would close the most uncertainty, and 9.4
records that it was not taken. It was taken:

Route: Settings column, row 16 `Network Settings`, Cross, row 1 `Settings and Connection Status List`,
Cross. Frame `depth2/statuslist-rest.png` (1080p, re-measured).

| quantity | value | relation to this document |
|---|---|---|
| title rule | y = 199 | the same rule as every captured page |
| rows | **13**, ink tops **291 -> 771**, **pitch 40 px exactly**, ink height 18 | **a second grid**: 4.2 #16's 80 px is the depth-1 column/page pitch, and the depth-2 table uses 40 |
| label column | right-aligned **x = 939** (938-940 over all 13 rows) | a new column pair, not 4.2 #34/#36's |
| value column | left-aligned **x = 979-980** | same |
| gutter | 38-41 px | new |
| lower rule | **y = 879** | the D4/D5 pair, not one rule |
| back hint | `O Back`, x 1155-1239, y 897-916 | unchanged |
| focus | **none** - the page is read-only | no focus plate and no row move to measure |
| category band | off-frame (no icon-column elevation at x 240-320: 11.8 luma against 25.8 on the depth-1 page) | this is what a depth-2 page does to the band, and it is what 3.5/5.1's parent-column question is *not* about |
| info bar | absent (luma 0 at x 1300-1900 on y 100) | differs from the depth-1 page, which keeps the outlined plate |

Two consequences:

- **Row pitch is page-class dependent.** 4.2 #16 (80) stays exact for the depth-1 column and the settings
  list pages; the depth-2 table is 40. Any code that carries one pitch constant for "rows" is wrong for one
  of the two classes - a native depth-2 table needs its own constant.
- **The depth-2 page is not a scaled depth-1 page.** Different rule count (two), different pitch (40),
  different column pair (939 / 980 against 1301 for a settings value), no info bar, no band. It is a page
  class of its own, like the choice band was.

The second depth-2 type captured is a **message page** (`depth2/ics-page-01.png`): title bar, rule 199,
two-line body, `OK`, `X Enter` / `O Back`. No focusable rows there either, so section 8's "depth-2 page with
focusable rows" remains uncaptured on this console - it does not exist in the pages this rig can reach.

### 10.3 The three band endpoints

4.4 deferred the panel to `ITEM-MENU-INFO-FIRMWARE.md` A6 and had no band numbers. The batch measured three
bands of the same visual family, and they do **not** share an origin:

| surface | left edge | geometric zero | vertical | frame |
|---|---|---|---|---|
| Options menu (Triangle) | **1330** | about **1690-1700** (nothing measurable right of 1750) | full height y 0-1079 | `menus/game-menu-open-check.png` |
| Choice band, Display page (Screensaver) | **1260-1261** | about **1680** (last >=2 luma 1648) | full height | `bands/screensaver-band-rest.png` |
| Choice band, Theme page | **1324-1325** | about **1744** | full height | `bands/theme-band-rest2.png` |

Three surfaces, three left edges, all full height, all fading into a dimmed parent page rather than ending
at a hard right edge. The item menu and the Theme band sit within 6 px of each other (1330 / 1324); the
Display band is 64 px further left. So:

- **there is no single band-origin constant in the firmware**, and this document should not gain one;
- 4.4's "the deferred numbers" are now half-answered: the span, the full-height shape and the fade are
  measured, the **`BG1` plate inside a band** is not (no frame isolates it);
- the native's `x0 = 1330` (`view.cpp:1110`) is exact for the options menu and the Theme band and 70 px
  right of the Display band - the same -70 px delta 9.2 already recorded, now known to be page-specific
  rather than universal.

### 10.4 Not resolved here

- The band's row origin: the same word `Original` sits at ink about 494 on the Theme list and about 537 on
  the Background list, so the rows are not placed from one origin (`SETTINGS-FIRMWARE.md` 12.4e). Measured,
  not derived - this document records it so no future pass hardcodes `505-(n-1)*20`.
- What sets a band's left edge (1260 vs 1324 vs 1330). No frame separates the candidates (list length,
  page class, the parent page's own column).
- The `BG1` plate inside a band, and the plate's alpha over a non-black background (10.1's numbers are over
  the column's backdrop, not over video).
- 5.1-5.3 (the 262 shift, the parent column's fate, the incoming column) and 5.4's `+113` label rule: this
  batch is all settled-state geometry and does not touch the transition.
- 3.7's 560x82 authored vs 591x58 drawn plate: still measured, still unexplained.

No source file was edited for this section.

---

## 11. Dated measurement pass - 2026-10-28: the BG1 plate rectangle over five columns, and the focused label's cap height

Read-only pass on the two existing batches, no new capture, no RPCS3 run, no source edit. It refines 10.1's
single-frame plate rectangle and answers the cap-height half of 10.4. Tools: `C:\dev\work\m.py` (Rec.709
luma, column/row profiles, ink runs) at the bundled Python; frame paths are relative to
`C:\dev\out\rpcs3-ref2-2026-10-28\` (`b2`) or `C:\dev\out\rpcs3-ref-2026-10-28\` (`b1`), all 1920x1080.

### 11.1 The plate is 319 x 175, not 326 x 140

Command (per frame): `m.py profx <frame> 396 416 495 515 2` for the left edge, `712 736` for the right,
`m.py profy <frame> 440 470 400 432 2` for the top and `575 605` for the bottom. Each edge is a 1 px step
between the page background and the plate fill, so the plate's rectangle is exact to the pixel:

| frame | left | right | top | bottom | plate |
|---|---|---|---|---|---|
| `nav/col-00-game.png` | **405** (2.7 -> 25.1 -> 50.2) | **723** (54.1 -> 29.6 -> 7.7) | **417** (1.6 -> 24.8 -> 39.6) | **591** (33.0 -> 24.6 -> 8.9) | 319 x 175 |
| `nav/game-01-down.png` | 405 | 723 | 417 | - | 319 x |
| `nav/game-02-down.png` | 405 | 723 | 417 | 592 | 319 x 176 |
| `nav/colL-3.png` | 405 | 723 | 417 | 592 | 319 x 176 |
| `b2/error/column-corrupted-focused.png` | 405 (48.0 -> 68.3) | 723 | 417 | 592 | 319 x 176 |
| `b2/nav2/game-column-back.png` | 405 | 724 | 417 | - | 319 x |

So the focused item's plate is **x 405-723 (319 px wide), y 417-591 (175 px tall)**, centred on **(564, 504)**
- the focus point the vertex captures give as (565, 505) (3.2, 4.2 #15). That centre is why the rectangle is
not the row band: it straddles 505 by +-88 px vertically and starts 160 px left of the label column, so it
sits behind the icon *and* the label (10.1's reading is right; only its numbers are off).

**Corrections to 10.1:** width 326 -> **319**, height 140 -> **175**, and the rectangle 420..560 ->
**417..591**. 10.1 measured one frame with a threshold above a bright part of the wave, which cut the plate's
lower half; the five frames above agree within 1 px and include two where the wave does not reach the plate.

**Fill and edges.** At x 440-470 the fill is a vertical gradient: **40.5 luma at y 420, 54.4 at y 510, 31.6 at
y 590** (the row above the plate reads 5.1 mean, so there is no plate there). ROI mean inside the rectangle is
55.5 (max 233.9, the icon); the edges are 1 px antialiased steps with no rounded corner detected at 1 px
resolution, i.e. a hard-edged rectangle, not a rounded card. The plate is the only filled rectangle on this
surface - the settings pages have none (SETTINGS 12.6) - so 10.1's "two focus languages" verdict stands with a
corrected shape.

### 11.2 The focused label's cap height is 26 px at both depths

Command: `m.py inkrow <frame> <x0> <x1> <y0> <y1> 120` over the first glyph only.

| surface | glyph | ink | cap height | frame |
|---|---|---|---:|---|
| depth 0, focused item label | `C` of `Corrupted Data`, x 758-795 | 492..517 | **26 px** | `b2/error/column-corrupted-focused.png` |
| depth 1, focused page row | `C` of `Color`, x 624-660 | 492..517 | **26 px** | `b2/bands/theme-page-ready.png` |
| depth 1, unfocused page row | `T` of `Theme`, x 624-660 (dim text, thr 60) | 317..341 | 25 px | same |

So the focused row's text cap height is **26 px** and it is the same at depth 0 and depth 1: the label size
does not change with depth, only its x (565 + 57 at level > 0, 4.2 #34) and brightness do. 4.2 #35's "label
ink height 492..519 (27 px row band)" is the same measurement including one row of antialiasing; the cap
itself is 26 px, and the focused item's full label bbox at depth 0 is x 752-1127, y 481-533 (the 481/533 are
the halo, the ink is 492..517).

**Consequence for a view:** the plate is 319 x 175 centred on the focus point, and the label it backs is a
26 px cap. Any renderer that sizes the plate from the label's ink box (10.1's "its width does not track the
artwork") will be wrong in both axes; the plate is a fixed-size rectangle centred on (565, 505).

### 11.3 One correction to 10.3: the options-menu endpoint is 1320, not 1330

10.3's three band endpoints table lists the options menu at **1330**. Re-measured with the same 1 px border
method as 15.1 (`m.py profx <frame> 1300 1345 95 105 3`, the 10-row mean at y 95..105, page black behind):

| frame | 1 px border | fill peak |
|---|---|---|
| `b1/dialogs/options-open-01.png` | **1320** (0.0 -> 15.2 -> 32.3) | 62.4 |
| `b2/menus/game-menu-open-check.png` | **1320** (0.0 -> 14.1 -> 31.2) | 50.2 by x 1344 |
| `b2/menus/game-menu-open-check2.png` | **1320** | 49.3 by x 1344 |
| `b2/menus/game-menu-rest.png` | 1329, peak 89, then 1-3 luma | **mid-close frame, not a rest measurement** |

So the item menu and the Theme-family choice band are **4 px apart** (1320 vs 1324), not 6 px in the other
direction, and 10.3's "the item menu and the Theme band sit within 6 px of each other (1330 / 1324)" becomes
"within 4 px (1320 / 1324)". The claim 10.3 exists to support is unchanged, and slightly strengthened: the
three band surfaces are 1260, 1320 and 1324, so there is still no single band-origin constant, and the
native's `x0 = 1320` (`view.cpp` item band, SETTINGS 13.3) is exact for the options menu.

### 11.4 What this does not settle

- 3.7's authored 560x82 info-bar plane against the drawn 591x58 plate is untouched; the plate above is a
  different object (`BG1` of the item column, `image=null`), and its size is still not in the RCO.
- The same plate over a **non-black backdrop** (video, a bright wallpaper) is unmeasured: every frame here
  has the plate over the column's own dark backdrop.
- The plate's alpha over content: the fill values are composite luma, not an alpha; separating them needs a
  frame pair with and without the plate over a known background, which does not exist.

No source file was edited for this section.

---

## 12. Dated correction - 2026-10-28: the column/page item layout, corrected against the matched pair

This section closes the layout half of the first genuine Tier B scorecard (`PARITY-SCORECARD.md` 15: whole
frames 17.6-19.0 dB, page rows 15.9 / 0.72, band rows 15.8-17.2 / 0.51-0.74, category icon rows often
passing). It is the first pass in this document that edits source: `src/view.cpp` only. Nothing above is
deleted, and two of the numbers above are corrected by the matched frames rather than by taste (12.6, 12.7).

Method: for each failing surface the native frame captured by the harness (same pins: render clock 30 s,
background clock 2026-10-25T10:00, UI clock 2026-10-25T12:32, focus age 2000 ms) was diffed against the
matched console frame in `out/parity-references/<state>__original__day__1080p.png`, using Rec.709 luma
column/row profiles and ink runs (Python + Pillow, Rec.709 luma, 1 px profiles at the cited rows/columns).
"Native, before" is `scorecard-20261028-012659` (exe `C:/dev/out/native-bands2/re-ps3-native.exe`, sha256
`94e57264...c29c54`); "after" is `scorecard-20261028-063514` (run `run-20261028-062356`, exe
`C:\dev\xmb\out\native-xmb\re-ps3-native.exe`, sha256 `0e8023fb...afc4eae`). The tree's final exe is
`a1c178ab...a37b` (a comment-order edit after the measured build); it was re-captured for `root` and
`theme-list` and produces **byte-identical frames** (SHA256 `fb3882e5...` and `8b6cbdd1...` both times), and
the canonical layer gate re-run on it gives the same six per-layer values, so every number below applies to
the installed build. No budget, reference or harness file was touched.

### 12.1 The per-surface delta table

Every row is a measured delta on the matched pair, not an inference. "Ref crop" names the frame and the
rows/columns the number was read at.

| # | element | native before | console (matched frame) | delta | ref crop |
|---|---|---|---:|---:|---|
| 1 | level-0 item label pen | 664 (565+99) | 678 (565+113) | **+14** | `root...png` label ink left 682 at y 492-523; `column-user/settings/network` ink 682-683; `5.4` measured 678 on `settings-b.png` |
| 2 | level-0 category title centre y | 355 | 350 | **-5** | `root...png` title ink y 342-358 against native 347-363 (same 21 px size) |
| 3 | parent-column unfocused icon x | 455.00 (565-110) | 455.54 (565-109.46) | **+0.54** | section 8 / 5.1 (traced transform), no settled frame isolates it |
| 4 | depth-1 category neighbours' alpha | 0.100 | about 0.067 | **-0.033** | `page-settings-theme...png`: icon peak 82 over a 70 background (implied 0.07) against native 100 over 84 (0.104) |
| 5 | Theme-page band left edge | 1260 | 1324-1325 | **+64** | `theme-list...png` y 100: 77 at 1322, 85 at 1324, 97 at 1326 (1 px step) |
| 6 | Theme-page band geometric zero | 1690 | 1744 | **+54** | `theme-list...png`: fill over the parent page 17 at x 1700, 6 at 1740, 1-3 from 1780 (12.3) |
| 7 | Theme-page band row text x | 1276 (1260+16) | 1341 (1324+17) | **+65** | `theme-list...png` ink left 1340-1341 on all four rows |
| 8 | Theme-page band fill | 61 peak, Screensaver curve | 56 peak at about 1400 | **-5** + shape | `theme-list...png`: delta over the parent page 25 at 1340, 35 at 1380-1420, 33 at 1460, 30 at 1500, 27 at 1540, 38 at 1620, 17 at 1700, 6 at 1740 |
| 9 | Theme band utility row (`Install`) | 465 (40 px above focus) | 261-278 ink | **-204** | `theme-list...png` first row ink 261-278 (12.4e) |
| 10 | Theme band theme rows | 505/545/585 | 499/541/578 ink | -6 / -4 / -7, pitch 40 kept | `theme-list...png` rows 2-4 ink tops |
| 11 | Color/Background/Font band | this build's panel: edge 1330, fill to 1920, text 1348 | same Theme-page plane: edge 1324, zero 1744, text 1341 | **+6 / -176 tail / -7** | `color-list`, `background-list`, `font-list` at y 100: identical delta profile to `theme-list` |
| 12 | Background-list rows | 505/545/585 | 539/581/621 ink | **+45** | `background-list...png` ink tops (against native 494/536/576) |
| 13 | info bar over the four bands | drawn (clock ink max 255) | absent (max 99 in the clock box) | state | `background-list/font-list/theme-list` clock box x 1500-1800, y 80-135 |

Rows 1-4 are the column surface; 5-13 are the Theme-page band family. The depth-1 *page rows* produced no
delta at all and were not touched (12.2).

### 12.2 What was already right and was deliberately not touched

Measured on the same pair, all within +/-2 px of the console:

| element | console | native | note |
|---|---|---|---|
| depth-1 page row centres | 675/755/835/915/995 | 670/750/830/910/990 (ink tops) | focus 505, pitch 80, `9.3` confirmed on three pages |
| depth-1 label pen | 625 ink | 623-625 ink | 565+57 = 622, exact |
| depth-1 value column | 1296 ink | 1296 ink | rect x 1300 (`U6`'s 1301 applies to the authored column, ink agrees) |
| focused label baseline | 492-523 ink | 495-515 ink | same 26-27 px cap, same baseline within 1 px |
| category icon row | quad/pitch/brightness | unchanged | the region already passes 20.0-21.7 dB in 3 of 9 column states (root, network, friends) and 21.3 dB on all four pages |
| settings focused row | no filled rectangle | no filled rectangle | `SETTINGS 12.6` holds for the native too |

So "the console's items sit around/beside the band while ours stack below" is **not** what the frames say:
the column rows, the depth-1 page rows and the 505/80 grid already coincide. The concentrated layout errors
were the level-0 label pen, the Theme-page band family, and the faint neighbours' alpha.

### 12.3 What changed (src/view.cpp, single writer)

1. `labelX`: `level==0 ? (tile?173:99) : 57` -> `(tile?173:113)`. Tile rows keep 173 because `5.4` shows the
   console's artwork-row label is set by its 300 px card, and this build's card is 320 px; the 20 px there
   belongs to the card width (ITEM-ICONS), not to the label constant.
2. Category title rect y 335 -> 330.
3. Parent unfocused icon offset 110 -> 109.46 (the focused 262 step is unchanged).
4. Depth-1 faint neighbour alpha 0.10 -> 0.067.
5. `PanelBand` gained `groupBreakRow` (a choice list has no `MenuOption` divider flag, so the Theme list's
   utility row is a measured index) and the Theme-page family gained its own band constants:
   `kThemeBand` (x0 1324, zero 1744, text inset 17, fixed rows at 505, pitch 40, group gap 239, group break 1),
   `kThemeLowerListBand` (same plane, no utility group: Color and Font), `kBackgroundBand` (same plane,
   row origin 548), `kThemeBandStops` (the Theme band's own 56-peak profile from `SETTINGS 12.3`, not the
   Screensaver band's 61-peak curve), and `pageBandForSetting()` keyed on the four Preferences keys
   `appearance/theme|colour|background|font`. Anything else keeps `kChoiceBand` (the Display page's 1260/1690).
6. The four Theme-page lists now draw that band even when their labels are this build's own (Color's list,
   Background's list): the *plane* is the firmware's on all four frames, only the row set differs.
7. The info bar is suppressed while any of the four bands is up, whether its labels are the firmware's or
   this build's (all four console frames have no clock; this build drew one for the native-labelled lists).

Not changed, with the reason in 12.6/12.7: the item plate, the artwork label 173, the transition numbers
(incoming 210 / 1.1022 scale / receding bar), the depth-2 page grid and rules.

### 12.4 Tier B per state, before and after

Frame and worst (gating) region, `scorecard-20261028-012659` before, `scorecard-20261028-063514` after.
PSNR dB / SSIM. States in roman were not touched by this change and reproduce their previous numbers.

| state | frame before | frame after | worst region before | worst region after |
|---|---|---|---|---|
| root | 17.83 / 0.8213 | **17.86 / 0.8226** | item label row 10.99 / 0.3952 | 11.03 / 0.3957 |
| column-user | 18.39 / 0.8438 | **18.42 / 0.8446** | item label row 11.62 / 0.4023 | 11.64 / 0.4027 |
| column-settings | 18.65 / 0.8372 | **18.69 / 0.8385** | item label row 12.71 / 0.4550 | 12.80 / 0.4549 |
| column-photo | 17.60 / 0.8334 | **17.62 / 0.8340** | item label row 10.52 / 0.3641 | 10.52 / 0.3641 |
| column-music | 17.69 / 0.8374 | **17.71 / 0.8382** | item label row 10.68 / 0.3732 | 10.68 / 0.3732 |
| column-video | 17.78 / 0.8327 | **17.80 / 0.8334** | item label row 10.58 / 0.4053 | 10.58 / 0.4053 |
| column-network | 18.26 / 0.8440 | **18.31 / 0.8457** | item label row 11.06 / 0.4203 | 11.14 / 0.4301 |
| column-psn | 8.81 / 0.6141 | **8.82 / 0.6146** | item label row 10.29 / 0.4256 | 10.29 / 0.4241 |
| column-friends | 17.75 / 0.8462 | **17.80 / 0.8479** | item label row 10.13 / 0.4459 | 10.13 / 0.4442 |
| page-settings-system | 18.49 / 0.8518 | **18.50 / 0.8533** | page rows 16.26 / 0.7395 | 16.26 / 0.7397 |
| page-settings-network | 17.95 / 0.8623 | **17.96 / 0.8639** | page rows 15.53 / 0.7364 | 15.53 / 0.7364 |
| page-settings-video | 18.27 / 0.8478 | **18.28 / 0.8496** | page rows 15.93 / 0.7202 | 15.93 / 0.7203 |
| page-settings-theme | 17.87 / 0.8339 | **17.88 / 0.8356** | page rows 15.27 / 0.7069 | 15.27 / 0.7071 |
| theme-list | 18.47 / 0.8492 | **18.92 / 0.8592** | band rows 17.23 / 0.7376 | 16.96 / 0.7412 |
| color-list | 18.42 / 0.8388 | **19.05 / 0.8583** | chip column 18.81 / 0.7846 | 19.43 / 0.8164 |
| background-list | 18.02 / 0.8383 | **18.72 / 0.8601** | band rows 16.26 / 0.6584 | 17.27 / 0.7034 |
| font-list | 17.63 / 0.8270 | **18.19 / 0.8472** | band rows 15.83 / 0.5085 | 16.11 / 0.5308 |
| options-panel | 18.83 / 0.8690 | **18.84 / 0.8701** | options panel 19.46 / 0.8324 | 19.46 / 0.8324 |
| osk | 25.95 / 0.8856 | **25.95 / 0.8857** | osk panel 21.18 / 0.7695 | 21.18 / 0.7695 |
| page-network-status-list | 16.84 / 0.8335 | **16.84 / 0.8335** | status rows 12.02 / 0.5804 | 12.02 / 0.5804 |
| page-network-test-error | 15.36 / 0.7783 | **15.36 / 0.7783** | message body 9.16 / 0.2567 | 9.16 / 0.2567 |
| page-network-connection-settings | 15.44 / 0.7774 | **15.44 / 0.7774** | message body 9.64 / 0.3163 | 9.64 / 0.3163 |

Region detail that the table compresses:

- `background-list` is the largest gain: band column 18.77 -> **20.33 / 0.8909 (PASS, 18.00 / 0.8800)** and
  band rows +1.01 dB / +0.045 SSIM. `font-list` band column 18.79 -> 19.95 / 0.8715. `color-list` band column
  18.54 -> 19.53 / 0.8664, chip column 18.81 -> 19.43 / 0.8164.
- `theme-list` is the only state whose band regions lost PSNR: band column 19.87 -> 19.57 (+0.0025 SSIM), band
  rows 17.23 -> 16.96 (+0.0036 SSIM), while its frame gained 0.45 dB / +0.0100. The cause is measured and is
  not the band: the parent page behind the band renders 20-21 luma brighter than the console's (native 99 at
  x 1300 y 100 against the console's 78), so a correctly placed band is still too bright. The three sibling
  lists gained +1.0 to +1.6 dB from the same geometry because their old panel fill was worse.
- The category icon row gained 0.12-0.38 dB in every column state (title y) and 0.22 dB / +0.0149 SSIM in
  every page state (faint alpha).
- `column-friends` and `column-psn` lost 0.0017 / 0.0015 SSIM on the item label row (10.13 and 10.29 dB
  unchanged). Both are content mismatches (a short native label against a different console string), so the
  +14 px pen helps root/network/settings/user and is a wash there; the pen itself is measured (row 1).
- No state flipped from FAIL to PASS overall: Tier B is still 0/22, because each failing state still has at
  least one failing region (9 `item label row`, 4 `page rows`, 3 `band rows`, the status rows, the two message
  bodies, the osk panel, the options panel and the color chip column = 22).

### 12.5 Gates

- Canonical layers: `bun work/ab-layers.ts --capture --exe C:\dev\xmb\out\native-xmb\re-ps3-native.exe`
  -> **PASS 6/6** with the text-presence line (`text presence: 1677 bright pixels in the clock crop
  (minimum 900) PASS`): frame 18.31 (base 18.29, +0.02) / 0.8356, top background 20.25 (+0.00), category icon
  row 21.20 (+0.27) / 0.9029, item label row 11.95 (+0.00), clock text 11.39, info bar 14.98.
- Named states (`ab-layers.ts --states`): **2/12 pass** after the change against 1/1 for the one state that was
  re-run on the pre-change exe (`steam-game-root` inf/1.0000). This is expected and reported, not repaired:
  those references are deterministic self-baselines at 55 dB / 0.9995, and this change deliberately moves the
  level-0 label pen in every state, the faint neighbours on every page and the four band planes. Measured
  drift: steam-game-root 27.48/0.9771 (item column 23.51/0.9500), xmb-notification 27.48/0.9771,
  xmb-volume-hud 27.66/0.9778, continue-watching-empty 32.15/0.9926 (dialog 29.02/0.9865),
  continue-watching 52.31/0.9993, steam-store-sections 51.74/0.9992, jellyfin-signin 53.11/0.9995,
  steam-all-games 53.07/0.9992, video-options-panel 54.50/0.9995, steam-not-installed 54.92/0.9995,
  steam-download-page 55.80/0.9997 (PASS), subtitle-cue inf (PASS). They need a deliberate
  `--states --save-reference` refresh by whoever owns those references; no reference was edited here.

### 12.6 What remains wrong, and why

1. **The dominant remaining error is the background, not the item layout.** A single per-channel gain fitted
   on the pair (0.785 root, 0.762 page-settings-system, 0.772 theme-list) raises the whole frame from 17.86 to
   20.12 dB (root, over the 20.00 icon-heavy budget), 18.50 to 21.51 and 18.92 to 22.18, and lifts
   `page-settings-system`'s page rows from 16.26 to 18.76 dB (budget 18.00). The native's upper gradient is
   about 1.25x the console's (1.23-1.30 over the 160x120 cells above y 360) and its ribbon sits lower and
   wider (the native/console cell ratio is 1.7-2.0x over y 480-600 and 0.83-1.18x over y 600-720), i.e. an
   exposure **and** a ribbon-phase difference. A fitted scalar is not a rendering constant and the ribbon's
   phase is a live animation state, so this pass did not fake either; it is a background-shader/atlas
   problem, not a layout one.
2. **Text metrics, not layout.** On the depth-1 pages the native label "System Name" inks 202 px wide against
   the console's 218 px (8 %), and the unfocused row ink peaks at 202-207 luma against the console's 148-175.
   Both are font-stack/rasteriser properties (view.cpp only chooses the size and the alpha), so no layout
   constant fixes them.
3. **The Theme band's fill over a too-bright page** (12.4): geometry and profile now match the frame
   (contribution 25-39 luma against the console's 25-38 at x 1340-1620), the page behind does not.
4. **Choice-list *contents*, outside this fence.** Color draws 6 text rows against the console's 12 unlabelled
   chips (`SETTINGS 12.4b`), and Background has no `Brightness` submenu row; both come from
   `settings_menu.cpp`'s choice model, which this pass does not edit.
5. **Transition-only numbers stay open.** `5.2`/`5.3`'s receding bar (d 2483.53, 217.4 px pitch, y 268/257),
   the incoming column (774.09 = 565+210 at 1.1022x) and `5.5`'s parent-label transform cannot be validated
   by a settled frame, and every parity cell is settled; they need a burst-pair comparison, and a wrong guess
   would move pixels no reference measures.
6. **Depth-2 pages** (`page-network-status-list`, status rows 12.02 dB) are drawn by
   `dialogs.cpp`/`xmbml_view.cpp`: rules 199/879, pitch 40, labels right 939, values 979-980. Outside the
   write fence of this pass.
7. **The artwork-row label** (`tile?173`) stays; `5.4` shows the console's 718 is set by a 300 px card and
   this build's card is 320 px.

### 12.7 The "item plate" is the BG1 artwork plane, not a focus plate (corrects 10.1 and 11.1's naming)

10.1 and 11.1 measure a rectangle at x 405-723, y 417-591 (319 x 175, centred on (564, 504)) over six
console frames and call it the focused item's plate. The matched root frame falsifies that reading: at
y 520 the console's luma is 52-53 from x 360 to x 500 with no step at x 405, i.e. the focused row of
`root__original__day__1080p.png` carries no rectangle, and the same holds for every column reference whose
focused row is a folder or a utility.

What the six frames have in common is the opposite: their focused items have **no image**. 319 x 175 is
1.82:1, the same shape as `4.2 #32`'s "about 300 x 180 at about (550, 490)" artwork tile, and the RCO calls
the object `BG1` with `image = null` - the plane that *would* carry ICON0. So the rectangle is the empty
artwork plane (the placeholder card drawn for an item with no ICON0), centred on the focus point, and it is
not a selection plate. Consequences:

- the native must **not** gain a plate behind every focused row (`SETTINGS 12.6`'s "no filled row" already
  says this for settings); that would have been a large regression on this pair;
- the native's missing element is narrower than 10.1 implied: an art-less *game* row should draw a
  319 x 175 placeholder card, which this build draws as a small glyph instead. No scored state has such a
  row (the Game column's rows there are folders and installed Steam entries with art), so it is recorded,
  not implemented.

No other source file was edited; no RCO, threshold, budget or reference was touched.

---

## 13. Phase 13.1/13.3 applied - 2026-10-28: the transition model, the code, and the frames

This is the first pass that applies the transition numbers 5.2/5.3 (the 262 shift's neighbour geometry
and the incoming column's offset/scale). It edits `src/view.cpp` only. It also reports a measured
disagreement between the decoded transition layer and the console's visible faint row (13.5) - the one
finding here that changes what the next pass should do.

Measurements below use: the decoded `fpdump7` transition draws (fp_0038..fp_0062, the close; and
fp_0174..fp_0181, the incoming column), the 139.7 fps page open/close burst at
`C:\dev\out\rpcs3-xmb-cap\burst-png\` (the Settings page, the two transitions phase 4c timed), and new
native captures from the rebuilt `C:\dev\out\native-phase13\re-ps3-native.exe`
(sha256 `2bfd4e0c...baab0e`; the tree kept moving under an in-flight pass, so the hash names the exact
build gated here). Frame measurements use `C:\dev\work\m.py` plus two scratch scripts under
`C:\dev\out\phase13-work\` (`peaks.py`, `blobs.py`, `track.py`, `focus.py`), read-only.

### 13.1 The model: the firmware animates distance, so the screen is a 1/d projection

The animated property is the **camera distance**, not a screen position. Evidence: the decoded close
series is d = 2483.53, 2504.75, 2523.89, 2541.16, 2544.09, i.e. equal *retention* of the remaining
distance to the 2700 target (0.902/frame, PAGE-MOVE 1.3). A screen-space lerp of the same endpoints
cannot produce a geometric series in d. Therefore the interpolation is **linear in distance** and
**hyperbolic in screen space**:

```
d(p)   = 2700 - (2700 - 2483.53) p = 2700 - 216.47 p
k(p)   = 2700 / d(p)                       projection factor, 1 -> 1.087162
screen = 960 + worldX * k                  (worldX from the depth-0 draw)
```

At p = 0, .25, .5, .75, 1 the category band is therefore:

| quantity | p=0 | .25 | .50 | .75 | 1.00 |
|---|---:|---:|---:|---:|---:|
| distance d | 2700.00 | 2645.88 | 2591.77 | 2537.65 | 2483.53 |
| projection k | 1.00000 | 1.02045 | 1.04176 | 1.06398 | 1.08716 |
| neighbour pitch 200k | 200.00 | 204.09 | 208.35 | 212.80 | 217.43 |
| focus padding 10k | 10.00 | 10.20 | 10.42 | 10.64 | 10.87 |
| unfocused y `540-250k` | 290.00 | 284.89 | 279.56 | 273.51 | 268.21 |
| bar focused y `540-260k` | 280.00 | 274.68 | 269.14 | 263.37 | 257.34 |
| bar focus x `565-395(k-1)` | 565.00 | 556.92 | 548.50 | 539.73 | 530.57 |
| bar neighbour n `barFocus+(200n+10sgn)k` | 565+(200n+10sgn) | ... | ... | ... | 530.57+(200n+10sgn) |

Against the decoded draws at d 2483.53 this is exact to the frame's global offset, **+0.28 px**:
bar focus 530.85 (5.2's value) vs 530.57, left neighbour 302.54 vs 302.27, right neighbour 759.15 vs
758.88, n=+3 1194.01 vs 1193.60. y 257.34 / 268.21 are 5.2's. So the model in this section *is* 5.2's
numbers, stated as an interpolation of the eased distance.

**The visible focused icon is a different draw.** In the decoded close the bar's focused row keeps its
root world x (`_Eye.x` -5.25/-5.12/-5.05), i.e. it only projects; the icon that travels 303 -> 565 in the
captured frames is the page's own category draw (LAYOUT-TRACE 4c measured it moving 565 -> 303 with the
visible label under it, and its y does not change). The native has one icon, so it draws that page draw:
`x = 565 - 262p`, `y = 280`, quad 167.328. The ramp in the `bar focused y` row above belongs to the dim
bar layer behind it and is not drawn separately.

**The incoming column (13.3).** The decoded incoming item rows (fp_0174..fp_0177) sit at screen 564.09,
i.e. 565 - 0.91 (the frame offset): the incoming column's *position* has already landed in the captured
frame. Its quads are 85.554 = 77.616 x 1.10223 and 203.700 = 184.8 x 1.10227; its rows keep the 80 px
grid (fp_0175/0176 at screen 120/40), so the 10.223% scale is on the icons, not on the row pitch or y.
The *outgoing* item column is the one 210 px right in the same frame (fp_0178..fp_0180 at 774.09,
`_Ambiant.w` 0.004), so `ahead*210` and `1 + .10223*ahead` are the two authored entry properties the
native expresses about its own anchor; at `ahead=0` both are identity and the settled page is unchanged.

**Two channels, one ease.** The 262 x move is the 18-frame / 0.774-retention channel; the distance and
brightness are the 0.902-retention (~29-frame) channel (PAGE-MOVE 7 reading 1). This pass drives both
from the existing depth `PafEase` because the phase owns the geometry, not the timing; that is labelled
a FIT in the source. The alternative (a second, slower recede ease) would reproduce the composite the
audits hypothesise but adds a duration the traces do not fix (PAGE-MOVE warns not to quote 483 ms).

### 13.2 What changed (`src/view.cpp`, single writer)

1. Category band (`drawScene`). Replaced the `level0X`, `kColumnX-40+distance*189` and `childAmount*262`
   neighbours with the model above:
   - `recedeDistance = 2700 - 216.47*childAmount`, `recede = 2700/recedeDistance`;
   - focused page icon `x = 565 - 262*childAmount` at y 280 and quad 167.328;
   - neighbours `x = (565-395*(recede-1)) + (200n + 10 sgn(n))*recede`, `y = 540 - 250*recede`,
     quad `117.13+50.2*focus` scaled by `recede` (the glass renderer gets the real distance instead,
     so its projection and `_Eye.z` are the firmware's; the themed and glyph paths scale in screen space);
   - the neighbours' material brightness ramps `0.698 -> 0.067` and their presence `1 -> 0`, so at
     `childAmount = 1` exactly one category icon is drawn (303) and no neighbour.
   The two factors are kept separate on purpose: the glass renderer multiplies `_Ambiant.w` by the
   layout opacity, and folding both into one produced a 0.698*0.698 double dim in the first build. The
   canonical gate caught it (category icon row -0.37 dB); after the fix the root frame is exact again.
2. Incoming column (13.3): `ahead*250` -> `ahead*210` at the row icon, label pen, value column and back
   arrow, and `entryScale = 1 + .10223*ahead` on the item icon, the focused-row icon and the ICON0 card
   in that column (not on row y or pitch, per the decoded rows).
3. `XMB_DEPTH_PIN=<0..2>`: a capture-only switch in `paintGL` that holds the depth progress at a value,
   so `--render-clock` grabs can measure a deterministic intermediate frame (the ease itself runs on the
   real frame clock, which `--render-clock` does not pin). Unset it changes nothing; the canonical
   capture is byte-identical with it off.

Deliberately not changed: the 505/80 grid, the parent focused 262 / unfocused 109.46, the level-0 label
113 and level-1 57, the Theme-band family, the info bar, and `itemIconSize`'s "no 1.1022x at rest" (the
scale is an entry property, not a level class, so its comment stays true). The parent column's label
(5.5) still has no `behind` term; it is visible mid-transition (the p=0.5 frame shows it overlapping the
incoming row's label) and remains open.

### 13.3 Capture method

Pinned series (deterministic): pin the clocks as the canonical recipe does, navigate Game -> 5x LEFT
(Settings), ENTER on `System Update` (a SettingsPage), then grab with `XMB_DEPTH_PIN` = 0, .25, .5,
.75, 1. Burst: `burst=<prefix>@ENTER` on the same route with the pin unset, 12 frames per run (this
machine: 25-158 ms after the key; the installed exe's run: 39-187 ms). The two RPCS3 transition frames
used for the visual comparison are `f_001410` / `f_001443` (open) and `page-open.png` (settled); the
before/after rest comparison uses the same route's settled grab from the pre-change installed exe
(`C:\dev\xmb\out\native-xmb\re-ps3-native.exe`, 2026-10-28 07:08).

### 13.4 Measured native geometry, before and after

Category icon centres (glyph centroids; model in parentheses), pinned series on the rebuilt exe:

| p | focused (page icon) | user n=-1 | photo n=+1 | music n=+2 | video n=+3 | tv n=+4 | game n=+5 | network n=+6 | neighbour glyph y (quad) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.00 | 567 (565) | 354.6 (355) | - | - | - | - | - | - | 283 (290) |
| 0.25 | 501 (499.5) | 342.1 (342.6) | 772.0 (771.3) | 980.0 (975.4) | 1179.3 (1179.5) | 1386.5 (1383.6) | 1589.1 (1587.7) | 1791.2 (1791.8) | 278 (284.9) |
| 0.50 | 436 (434.0) | 329.4 (329.7) | 768.1 (767.3) | 980.3 (975.6) | 1184.1 (1184.0) | 1395.0 (1392.4) | 1602.3 (1600.8) | 1808.7 (1809.2) | 272 (279.6) |
| 0.75 | 370 (368.5) | too faint to separate from the focused icon's window | 763.7 (763.2) | 979.3 (976.0) | 1188.3 (1188.8) | 1401.9 (1401.6) | 1614.5 (1614.4) | 1827.5 (1827.2) | 266 (273.5) |
| 1.00 | 305 (303) | absent | absent | absent | absent | absent | absent | absent | - |

The focused icon is exactly linear in p (567/501/436/370/305 = 565 + 2 - 262p, the +2 being the glyph
centroid). Neighbours track the model to <= 2.6 px except the music note glyph, whose centroid sits 4.7 px
right of its quad centre. The neighbour glyph centre moves 283 -> 278 -> 272 -> 266 while its quad moves
290 -> 284.9 -> 279.6 -> 273.5 (the glyph centroid sits about 7 px above the quad centre). At p=1 the
settled page's category icon bbox is 249..361, centre 305 - the console's `page-open.png` is 249..360,
centre 304.5: within 1 px.

The settled depth-1 rest frame changed as intended: the pre-change code drew the right-hand neighbours at
`525 + 189n` with opacity 0.067; the rebuilt exe draws none. Measured on the two settled grabs, ROI
x 690..740, y 240..320 (where the pre-change n=+1 neighbour sits at 714): mean luma 96.28 -> 90.88,
max 104.4 -> 96.0. Over x 650..1000, y 235..330 the p99 falls 104.4 -> 96.1. The p=0 root frame is
unchanged (13.6: every canonical layer delta is 0.00).

Incoming column at p=0.5: the incoming focused row's icon is at 668 (565 at rest; model +105), and the
level-1 label pen is at 727 (622 at rest; model +105). The 1.10223 scale is 1.0511 at that p (58.9 px on
the 56 px focused-row glyph); that 2.9 px is below this glyph's measurement resolution, so the scale is
reported from the decoded 85.554/203.700 rather than from a pixel fit.

### 13.5 The frames and the decoded layer disagree - a transition-layer finding

This is the one result that should change the next pass. The console's **visible** faint neighbour row in
the open burst does *not* sit at the d 2483.53 geometry. Measured at the two frames where the focused
icon has moved about as far as the native's p=0.38/0.64:

| frame | focus x | user n=-1 | camera n=+1 (template) | game n=+5 | network n=+6 | neighbour glyph y | camera scale |
|---|---:|---:|---:|---:|---:|---:|---:|
| `f_001410` | 466.5 | 350.6 | 763.0 | 1546.2 | 1739.2 | 292 | 0.962 |
| `f_001443` | 398.2 | - | 755.5 | 1536.8 | 1715.7 | 293 | 0.962 |

At the matched focus progress (p = (565-466.5)/262 = 0.376) the two models give:

| model | user | camera | game | network | neighbour y |
|---|---:|---:|---:|---:|---:|
| console frame (visible) | 350.6 | 763.0 | 1546.2 | 1739.2 | 292 |
| native **after** (this pass) | 336.1 | 769.3 | 1594.4 | 1801.1 | 275 |
| native **before** (old model) | 256.5 | 752.1 | 1535.5 | 1731.4 | 283 |
| decoded d 2483.53 layer | 302.3 | 758.9 | 1628.6 | 1846.0 | 268 |

So the change makes the *left* neighbour much better (-96 -> -15 px) and the near right neighbour better
(-11 -> +6), but the far right neighbours are +48 / +62 px right of the visible row, where the old model
was -11 / -8 px left; the visible y is +17 px high where the old was +9. The visible row's scale (0.962)
and pitch (~200, not 217.4) match neither model exactly.

The physically consistent reading is the one LAYOUT-TRACE 4c reached from the frames: the visible faint
row is the category bar receding **toward the parent-column distance 2857.2** (k = 0.945, pitch 189.0,
y 303.7). The captured frames show it part of the way there and the two estimators bracket the progress:
the template scale is 0.962 (about two thirds of the 1.000 -> 0.945 range) while the neighbour x
positions fit k about 0.98 (about a third); neither is the 1.087 the decoded layer would give. The
d 2483.53 layer is a second, dimmer draw (its first captured `_Ambiant.w` is 0.067) that the frames
alone cannot separate from it. That is why 5.2's "the correct target is the receding-bar model" and 4c's
"the visible faint row is 190.5 px at y 303" are both true of different layers.

**What this pass did with it:** implemented the decoded layer, because it is the only one with exact
endpoints and the phase names it, and measured/reported the visible-row delta instead of blending the
two. The next pass should either (a) take the burst that decodes both layers per frame (PAGE-MOVE's
"open `XMB_FP_DUMP`, press Cross at 500 ms, full width" recipe), or (b) if frame fidelity wins, switch the
neighbour ramp to d 2700 -> 2857.2 (pitch 189.0, y 290 -> 303.7, anchor 565, which is 4c's `565 + 190.5n`
and not the deleted `525` anchor). The two are a one-constant difference in `recedeDistance`; the rest of
13.2 stands either way.

### 13.6 Gates and determinism

- Canonical: `bun work/ab-layers.ts --capture --exe C:\dev\out\native-phase13\re-ps3-native.exe` ->
  **PASS 6/6** with `text presence: 1668 bright pixels in the clock crop (minimum 900) PASS`.
  frame 18.76 / 0.8360, top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label row
  11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667 - every delta **0.00 / 0.0000**
  against the stored baselines. The root frame is therefore unchanged by this pass; the first build's
  double-dim regression (21.25 / 0.9073) is what that exactness checked.
- Determinism: two `--render-clock 20` captures of the root are SHA-256
  `37e2b590c42ef0e3f3601ca492221147c9874d2e3b1785e6237b616f2c0bbc14` both.
- Named states (`--states --exe native-phase13`): **5/12 pass**. Passing: steam-game-root,
  xmb-notification, xmb-volume-hud, continue-watching-empty (the depth-0 states, all inf / 1.0000) and
  subtitle-cue (depth 1, but its frame is the media surface, which does not draw the category band).
  The seven failures are depth-1/2 states whose *whole frame* moved while their own content regions
  stayed identical: steam-all-games 52.15 / 0.9992 (rows inf), steam-store-sections 52.15,
  steam-not-installed 52.15 (rows inf), video-options-panel 49.85 (panel inf), continue-watching 49.73,
  steam-download-page 53.86 (rows inf). Their only changed region is the category band, where the rest
  frame lost the 0.067 neighbour row - the intended 5.1 change; the 55.00 dB self-baseline is simply
  tight enough to see five faint icons. jellyfin-signin is 50.66 and its rows region is also 50.21, so
  part of that state's drift is outside this pass (rows untouched; the in-flight dialogs pass owns them).
  No reference was edited.
- Tree state during the pass: another pass modified `view.h` (`drawMessageTablePage`), `dialogs.cpp` and
  `ribbon.cpp` concurrently. This pass edited `view.cpp` only; no edit of `view.h` was needed (the pin is
  an environment read, not a member). Two builds were taken while the tree moved; the gated exe is the
  hash in the header, built after `shell.cpp`/`shell.h` were restored to HEAD.

### 13.7 What remains

1. The visible-vs-decoded recede layer (13.5) - a burst that decodes both, or the 2857.2 switch.
2. The recede timing: this pass keeps 300 ms mode 4 (FIT to the 18-frame burst) while PAGE-MOVE 14.1
   traces the bar driver at 200 ms mode 5. The recede endpoints are independent of that; only its speed is.
3. 5.5 / 13.2's parent-label transform: the parent column's label still has no `behind` term, so it
   paints at 678 during the transition while its icons have moved left (visible in the p=0.5 grab).
4. The incoming *label* scale: the pen gets the +210 offset but not the 1.10223 (the decoded incoming
   text was not captured; the icon quads were).
5. 13.5 provenance markers for the numbers not touched here, and the depth-2 grid (12.2 / 10.2), stay
   with their owners.
6. The named-state references need a deliberate `--states --save-reference` by whoever owns them: every
   depth-1 state's rest frame legitimately lost five faint icons.

### 13.8 Reproduction

```
# build
C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-phase13

# one pinned intermediate frame (repeat with 0 / 0.25 / 0.5 / 0.75 / 1)
$env:XMB_DEPTH_PIN='0.5'
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <scratch>/data ^
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 ^
  --focus-time 2000 --script "full,wait,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,ENTER,wait,grab=<png>,quit"

# a real burst (no pin): replace the grab token with burst=<prefix>@ENTER
# decoded layer: node work/item-icon-eyes.js out/native-reference/fpdump7
#                node work/item-icon-vc.js out/native-reference/fpdump7/<fp>.vc 256 257 259 467
# console frames: C:\dev\out\rpcs3-xmb-cap\burst-png\f_001402/1410/1443/1498/1643.png, page-open.png
# gates: bun work/ab-layers.ts --capture --exe C:\dev\out\native-phase13\re-ps3-native.exe
#        bun work/ab-layers.ts --states  --exe C:\dev\out\native-phase13\re-ps3-native.exe
```

No source outside `native-xmb/src/view.cpp` was edited by this section; no RCO, threshold, budget,
reference, harness, CMake or asset file was touched.

