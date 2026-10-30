# FIDELITY-SURFACES: rows, open pages, options menus, dialogs and the info/indicator surfaces

Scope: the composited surfaces of one XMB frame that this domain owns - the focused row, the
unfocused rows and their icon quads, the row icon set of every column, depth-1 open pages, depth-2
pages, the Triangle options menu, every message/dialog class, and the info bar and its indicator
slots - measured on the current build against the console corpus. Analysis only: **no source, tool,
budget or reference file was written**. The one file this pass wrote is this document; every capture
it took lives outside the repository under `C:\dev\out\fs-audit\`.

Evidence classes: **M** = measured on a 1920x1080 frame (console or native) this pass; **R** = read
from source/artefacts; **D** = documented in an existing audit that this pass did not re-measure.

Console corpus used (all 1920x1080, theme Original, day preset, guest clock pinned inside 11:00-13:00):

- `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` - 162 frames, indexed by
  `C:\dev\out\rpcs3-allgraphics-2026-10-28\INVENTORY.md` (cited below as **INV** with a section).
- `C:\dev\out\rpcs3-day-2026-10-28\frames\` - the earlier day batch, used as a second console reading
  for the one tone measurement that an audit comment turns on.

Native side: `C:\dev\xmb\out\native-xmb\re-ps3-native.exe` (4,257,280 B, 2026-10-29 18:33 CDT, built
from commit `07a4fe4`), captured at the canonical clocks
`--render-clock 30 --background-clock 2026-10-29T12:00:00 --ui-clock 2026-10-29T12:04:00`
(the console frames read `29/10 12:04`..`12:07`), `--focus-time 500`, into
`C:\dev\out\fs-audit\frames\`. Scoring helper: `C:\dev\xmb\work\ab-compare.ts` (decode/PSNR/SSIM),
driven by a small probe (`C:\dev\out\fs-audit\tools\probe.ts`) that reports rect, ink box, band
profile and per-pixel luma. Firmware truth for row art is the XMBML the console itself reads:
`C:\dev\rpcs3\bin\dev_flash\vsh\resource\explore\xmb\category_*.xml` (**M**, read this pass).

> **Concurrency note.** While this pass was measuring, the shared working tree gained uncommitted
> edits to `native-xmb/src/view.cpp` and `view.h` (mtimes 19:50 and 19:47, `+114/-28`), one hunk of
> which is at `View::drawItemGlyph` (`git diff -U0`, hunk `@@ -1076`). They are **not this pass's**
> - this pass wrote no source - and **every native number below is from the 18:33 binary**, i.e. from
> `07a4fe4`, not from the working tree. Re-measure before treating any row here as still open.

---

## 0. Counts

**63 elements** audited across 8 surfaces. Every element has a measured native reading and either a
measured console reading or an explicit statement of why that side has no reference.

| Verdict | Elements | Meaning |
|---|---:|---|
| **MATCH** | 18 | the native geometry/tone/topology agrees with the console inside the tolerance named per row |
| **PARTIAL** | 15 | the element exists in the right place but a measurable term is off, or its state/content differs |
| **GAP** | 21 | the element (or a firmware row of it) is absent or wrong at a scale a viewer would see |
| **UNVERIFIED** | 8 | the native draws it and/or an audit claims it, but this pass did not obtain a console reading it could be scored against |
| **NO-REFERENCE** | 1 | the surface has no console counterpart on this rig |

Per surface:

| # | Surface | Elements | MATCH | PARTIAL | GAP | UNVER |
|---|---|---:|---:|---:|---:|---:|
| 1 | Focused row (depth 0) | 10 | 2 | 3 | 4 | 1 |
| 2 | Unfocused rows | 7 | 2 | 2 | 1 | 2 |
| 3 | Row icon set, every column | 10 | 1 | 4 | 4 | 0 (+1 no-ref) |
| 4 | Depth-1 open pages | 9 | 6 | 1 | 2 | 0 |
| 5 | Depth-2 pages | 6 | 3 | 1 | 1 | 1 |
| 6 | Options menu | 9 | 0 | 1 | 7 | 1 |
| 7 | Messages / dialogs | 6 | 0 | 2 | 1 | 3 |
| 8 | Info bar and indicators | 6 | 4 | 1 | 1 | 0 |

Headline: **the options menu is the weakest surface in this domain (7 of 9 gaps)**, and **one
rendering defect - the item-glyph "glass" pass - hollows every RCO row icon in the build** (§3.1).

---

## 1. The focused row (depth 0)

Reference: `frames\col-game-r00.png` (PS Vita row focused) and `frames\col-game-r01.png` (Game Data
Utility focused) against the native `game-r00.png`..`game-r10.png` walk of the same column
(`C:\dev\out\fs-audit\frames\`, states in the `.png.json` sidecars: 11 entries, `depth=0`).

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 1.1 | Focus bar / BG1 plate | **no separate plate measurable**: at y=505 the luma steps 96 (x<=500) -> 204 (x 505..625) -> 96 (x 630..); there is no pedestal outside the glyph silhouette. `col-game-r01.png` | same shape: 114 (outside) -> 142/150 rim -> body -> 132 outside. No plate drawn (`view.cpp` draws no plate object on a row) | **MATCH** |
| 1.2 | Focused icon quad | folder body span at row centre y=505: **x 501..628 = 128 px** | at y=495: **x 489..611 = 123 px** (-4%) | **PARTIAL** |
| 1.3 | Focused icon tone | body is a **flat** plateau **203-204** over bg 76-80 at x=565 (y 480..520); i.e. 0.71 of white. Day batch agrees: 165-167 over 53-54 = 0.56 | body **oscillates 104-139** and tracks the background wave through the glyph; 0.10-0.25 of white | **GAP** |
| 1.4 | Focused icon identity | XMBML `icon_rsc` per row (category_game.xml): Vita `tex_album_icon`, Game Data `tex_album_icon`, Memory Card `item_tex_ps12util`, SD PS2/PS3 `item_tex_ps3util`, SD PSP `item_tex_psputil` | shell.cpp:1039-1042 asks `item_tex_vitaformat`, `item_tex_ps3game_folder`, `item_tex_ps12util`, `item_tex_ps2game_folder`, `item_tex_pspgame_folder`, `item_tex_ps3sd_folder` | **GAP** (5 of 6 differ; §3.1) |
| 1.5 | Focused icon x centre | 564.5 | 550.0 (**-14.5 px**) | **GAP** |
| 1.6 | Label em / advance | `labelFont` em 32 (documented, commit `80f4ae4`); "Memory Card Utility" ink **366 px** wide, "PS Vita System Application Utility" **532 px** | em 32 (`view.cpp:2040`); same strings **311 px** (-15%) and **497 px** (-6.6%) | **PARTIAL** |
| 1.7 | Label ink height | 39-40 px (unfocused rows, y 667..706) | 32-39 px (y 827..866) | **PARTIAL** |
| 1.8 | Label glow (breathing halo) | visible soft halo around the focused title | `glow()` runs at `view.cpp:2061` with a 3.4 s breath | **UNVERIFIED** (not scored) |
| 1.9 | Sub-label (`*User`) | ink top y 528 under the focused title (INV §4); present on the Vita, both Saved Data rows and the signed-in user row | the **slot exists** (`view.cpp:2146`: `text({labelX,y+24,...}, entry.detail, 18, ...)`) but **no entry fills it** - the only detail line in the column is Steam Store's "Browse and install". Zero `*User` string in `src/` | **GAP** |
| 1.10 | Row expansion / pitch | focused row centre 505, next row 680, pitch 80 (INV §4) | focused 505, next 660/680, pitch 80 (`view.cpp:1939`) | **MATCH** |

---

## 2. Unfocused rows

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 2.1 | Icon quad | 48-56 px wide (INV §4) | **60 px** wide (`game-pinned.png` bands: 60x41 at y660, 60x15 at y720, 54x60 at y880) | **PARTIAL** |
| 2.2 | Icon tone | solid: `col-game-r00.png` folder body 165-167 over bg 53-54 (day batch), 0.56 white | hollow: body 0.10-0.25, rim only | **GAP** (`view.cpp:992-1058`, §3.1) |
| 2.3 | Label x | 683-684 | 678-681 (**-2..-5 px**) | **MATCH** |
| 2.4 | Label alpha/dim | unfocused ink mean 134, fill 20.7% (`col-game-r00.png` 683,660,400,50 threshold +45) | 150, fill 14.7% (same string, same threshold) - native's 0.455 factor (`view.cpp:2035`) matches the documented console 0.446 | **MATCH** |
| 2.5 | Idle fade of the other titles | not captured at rest | implemented (`view.cpp:2023-2026`, 4 s + 0.8 s ramp). Confirmed accidentally: an unpinned run at `idle=5045 ms` drew **no** unfocused labels at all | **UNVERIFIED** against a console frame |
| 2.6 | Rows above the focus | 2 rows, y 41 and y 120 (INV §4) | on a page: **4** glyph rows above the header (`page-theme.png` top-left cluster, y 0..162) vs 2 on `page-settings-theme.png` (y ~50 and ~120) | **PARTIAL** |
| 2.7 | End-of-list overscroll (40 px/press) | no frame in the corpus shows the sink (INV §12 says so) | implemented (`view.cpp:1952`, `overscrollStep`) | **UNVERIFIED** |

---

## 3. The row icon set, every column - and the Game-column verdict

### 3.1 The live defect: "plain outlined rectangles" below Steam Store

**Verdict: the art is present and correctly named for some rows, wrong-named for others, and the
draw path renders every one of them as a hollow outline. It is neither a missing asset nor the
correct art.** Two independent faults stack:

**(a) The draw path hollows the glyph (the visible cause).** `View::itemGlyph()`
(`native-xmb/src/view.cpp:992-1058`) classifies a texture as a "glass mask" when the median alpha of
its covered pixels is < 230, and then rewrites the alpha of every non-rim pixel to
`(255 - alpha) * 0.78` (`view.cpp:1015,1054`). The firmware row art is exactly such a mask:
`out/tex/png/explore_plugin_full/item_tex_plain_folder.png` is 314x178, **median alpha 195**, RGB
237. So a body texel of 195/255 (0.765) is drawn at `(255-195)*0.78/255 = 0.184`.

Measured consequence, `col-game-r01.png` (console, Game Data Utility focused) vs `game-r03.png`
(native, same row focused), scanline through the folder body:

```
console  x450..650/5, y505: 96 97 96 96 97 97 98 98 99 99 100 [202 204 204 204 ... 204 197] 97 96
native   x450..650/5, y520: 114 115 115 116 117 117 117 118 118 118 119 119 118 [142 150 149 150
                              152 151 151 152 152 124 126 129 131 135 137 140 143 146 148 149] 176
```

The console body is a **flat 204** (one blend of the mask at its own alpha, 0.68 of white); the
native body **swings 124..168 with the background wave**, i.e. it is a rim with a nearly transparent
fill - which is precisely what reads as "a plain outlined rectangle" at 100% zoom. The code comment
that justifies the transform (`view.cpp:987-991`: "the folder body reads as 0.18 of white over the
background") is falsified on both console batches: `col-game-r01.png` reads 0.68 and
`rpcs3-day-2026-10-28\frames\c-game-root.png` reads 0.56 on the same construct. The correct fallback
is the mask's own alpha (0.765), not its complement.

The same substitution runs for every texture drawn through `drawItemGlyph`: `item_tex_ps12util`
(median 227), `item_tex_ps3game_folder` (199), `item_tex_ps3sd_folder` (151), `item_tex_pspgame_folder`
(172), `tex_album_icon` (196) - so the hollow look is uniform across the Game column, the media
folders, the PSN trophy row and the Friends rows, not a Game-only bug.

**(b) Five of the seven firmware rows ask for art the firmware does not use there.**
`category_game.xml` (read this pass) binds `icon_rsc` per segment view; the native's row table is
`shell.cpp:1039-1042`:

| Game row (native id) | Console `icon_rsc` (M, category_game.xml) | Console frame shows | Native texture (R) | Verdict |
|---|---|---|---|---|
| `steam-store` | n/a (host row) | - | `item_tex_ps_store` | no reference |
| `steam-folder-all-games` | n/a (host row) | - | `item_tex_plain_folder` -> remapped to `item_tex_ps3game_folder` at `view.cpp:1973-1974` | no reference |
| `vita` | **`tex_album_icon`** (seg_psp2appdata) | plain folder (`col-game-r00.png`) | `item_tex_vitaformat` - a **110x28 wordmark badge**, drawn 60x15 unfocused / 104x26 focused | **WRONG ART** |
| `data` (Game Data Utility) | **`tex_album_icon`** (seg_gamedata) | plain folder (`col-game-r01.png`, no emblem) | `item_tex_ps3game_folder` (folder **+ gamepad emblem**) | **WRONG ART** |
| `memory` | `item_tex_ps12util` | card-slot folder | `item_tex_ps12util` | **MATCH** |
| `ps2` | `item_tex_ps3util` (128x128) | card-slot folder | `item_tex_ps2game_folder` (folder+emblem) | **WRONG ART** |
| `psp` | `item_tex_psputil` | card-slot folder | `item_tex_pspgame_folder` | **WRONG ART** |
| `ps3` | `item_tex_ps3util` | gamepad folder | `item_tex_ps3sd_folder` | **WRONG ART** |
| `store` (PlayStation Store) | seg_welcome | blue shopping bag | `item_tex_ps_store` (160x160, median alpha 255 = opaque) | **MATCH** |
| `content:refresh`, `content:import` | n/a (host rows) | - | `item_tex_plain_folder` | no reference |

Measured rects for the montage evidence (`C:\dev\out\fs-audit\frames\m-folder-focused.png`,
4 panels, 3x, crop 440,430,230,140):

- console Game Data Utility folder: body x 501..628, y 472..549 (128 x 78 at the scanlines used);
- native Game Data Utility: body x 489..611, y 464..569 - same order of size, **hollow**;
- console Vita row: plain folder, x 501..628;
- native Vita row: the `PS Vita` wordmark plate, x ~489..611 x 464..479 (a 3.9:1 bar).

Native unfocused quads for the same column (`game-pinned.png`): 60x41 (All Games), 60x15 (Vita
badge, the badge's 110:28 aspect drawn at 60:15), 54x60 (Memory Card), 60x39 (Saved Data PS2).

### 3.2 Per-column icon set

Console column contents are INV §5.1-5.9; console art names are the XMBML `icon_rsc` pairs read this
pass; native names are `shell.cpp:1014-1058`.

| # | Column | Console rows / art | Native rows / art | Verdict |
|---|---|---|---|---|
| 3.1 | Users | 3 rows, no `icon_rsc` in `category_user.xml` (icons are runtime avatars) | 2 rows (`new-user`, `user`), `item_tex_NewUser`, `user_tex_login` | **PARTIAL** |
| 3.2 | Settings | 17 rows, `tex_update`..`tex_network` (category_sysconf seg_fixed_items) | 16 rows, then re-resolved from the RCO row definitions and drawn through the `qgl/icontex` normal-map pass (`shell.cpp:938-944`), which is the brass material | **MATCH** on art/material; row count differs by the documented `edy` omission |
| 3.3 | Photo | 3 rows: `item_tex_dlna_scan`, gallery, playlists | 3 rows: `photo_tex_album_pic`, `tex_photo`, `item_tex_plain_folder` - a different row set (Library / Settings / Choose Folder) | **GAP** |
| 3.4 | Music | 2 rows: `tex_now_playing`, playlists | 3 rows: `music_tex_album_pic`, `tex_music`, folder | **GAP** |
| 3.5 | Video | 3 rows: `tex_add_playlist`, media-server search, BD data | 4 rows: Continue Watching, Video Library, Video Settings, Choose Folder | **GAP** |
| 3.6 | TV / Video Services | **no such column on this console** (INV §3) | 1 row (`qgl/icontex:35n`) | **NO-REFERENCE** |
| 3.7 | Game | 7 rows (§3.1) | 11 rows, 5 wrong art | **GAP** |
| 3.8 | Network | 4 rows: `tex_onlinemanual`, remote play, `tex_browser`, `tex_kensaku` | 5 rows incl. `Download Management` (host row) | **PARTIAL** |
| 3.9 | PSN | 5 rows (Sign Up, What's New, Trophy Collection, PS Plus, Store) | 3 rows (Sign In, Trophy Collection, Store) | **PARTIAL** |
| 3.10 | Friends | 0 rows - the column is rowless offline (INV §5.9) | 3 rows rebuilt from Steam (`item_tex_NewFriend`, `item_tex_Players`, `item_tex_Profile_Onlines`) | **PARTIAL** |

---

## 4. Depth-1 open pages

Reference pair: `page-settings-theme.png` (console) vs `page-theme.png` (native), same page, same
theme, same pinned clock; `page-settings-system.png` vs `page-system.png` for the row grid.

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 4.1 | Backdrop / dim of the parent column | content dims, background stays: region 300,700,600,200 p50 **53.3**, max 92 -> 60 when a band opens | same behaviour: p50 70.5 both, max 172 -> 131 | **MATCH** |
| 4.2 | Header icon + title | wrench+swatch tile at ~170..370 x 380..520, title "Theme" at x 626 | same tile at ~180..340 x 385..490, title at 626 | **MATCH** |
| 4.3 | Header rule | 1 px at y=199 (col 800: 81 81 81 **143** 81 81 81 on `page-network-status-list.png`) | 1 px at y=198-199 (**155** over 98 on `page-net-status.png`) | **MATCH** |
| 4.4 | Row list grid | focused 450, then 607 / 678 / 749 (pitch 71) | identical (607 / 678 / 749 / 820 / 891) | **MATCH** |
| 4.5 | Focused row inside a page | icon at x ~510, title 626, 2-line help at 626/523 | same | **MATCH** |
| 4.6 | Left chevron on the focused row | ink 432..455 x 492..517 (ICON-GAP-AUDIT §10 item 5, object `tex_arrow_anim` cell 0) | drawn from the same object | **MATCH** (D) |
| 4.7 | Value column | "Original" right-aligned, x 1160..1225 | "Original" at 1160..1240 | **PARTIAL** (native's is brighter/larger) |
| 4.8 | Row set of the Theme page | **3 rows**: Color, Background, Font (frame shows nothing below y 780) | **5 rows**: Color, Background, Font, **Select Theme, Select Wallpaper** (820, 891) | **GAP** |
| 4.9 | Glyph rows above the page | 2 (y ~50, ~120) | **4** (y ~0, ~50, ~100, ~120-162) | **GAP** |

---

## 5. Depth-2 pages

Reference: `page-network-status-list.png` (table page) and `page-network-connection-settings.png`
(message page) vs `page-net-status.png` / `page-net-conn.png`.

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 5.1 | Rule pair, table page | y=198 luma **143** (bg 81); y=879 luma **132** (bg 59) | y=198 **155** (bg 98); y=879 **140** (bg 76) | **MATCH** |
| 5.2 | Rule pair, message page | y=198 **194**; y=879 **192** (brighter rules on the message class) | y=198 **134**; y=879 **no rule** (col reads 76 76 76 76 76 76 76) | **GAP** |
| 5.3 | Header icon + title origin | header icon ink run **x 109..149**; title text starts **x 164** (strip y 130..194) | icon run **x 109..149**; title starts **x 164** | **MATCH** on icon rect and title origin; the *extent* of the title string is not scored because the native page background is 19 luma brighter (85 vs 66), so a fixed ink threshold saturates there |
| 5.4 | Back hint | `O Back` ink **1155..1240 x 897..916** | **1155..1240 x 897..916** (pixel-identical box) | **MATCH** |
| 5.5 | Table grid (label/value columns) | 13 rows, ink tops 291..771, pitch 40, labels right-aligned 939, values left 979 (INV §10.3) | page opens, content is host data; grid not scored | **UNVERIFIED** |
| 5.6 | Modal dim of the message class | the page behind the message is dimmed (DIALOGS-FIRMWARE class table; visible on the frame) | no wash measured on `page-net-conn.png` (bg 98 vs the table page's 98) | **PARTIAL** |

---

## 6. The options menu (Triangle)

Route used: Game column -> All Titles page -> TRIANGLE. Console reference:
`menu-options-open.png` (Corrupted Data, two groups), `menu-options-row2..row4.png`,
`menu-submenu-group-content.png`. Native: `options-game.png`, `options-game2.png`,
`all-games-page.png` (same page without the panel, for the dim comparison).

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 6.1 | Band plate origin | left edge where luma leaves the page: **x 1320** (53 -> 66 -> peak 103 at 1380-1410) | **x 1386** (71 -> 109, peak 113 at 1440) - **+66 px** | **GAP** |
| 6.2 | Label inset | text left edge **x 1336-1337** | **x ~1409** - **+73 px** | **GAP** |
| 6.3 | Group structure and the 199 px gap | two groups in one band: Sort By (301), Group Content (341), then a **199 px gap**, then Start (539), Delete (581), Information (621) | **one group of two**: Open (~537), Information (~581). No gap to make | **GAP** |
| 6.4 | Row set | 5 rows, 2 of them submenu rows | 2 rows on this page (`Open`, `Information`); no sort/group rows exist in this build (`shell.cpp:1786-1791` records the refusal) | **GAP** |
| 6.5 | Chevrons | right-pointing triangle at x 1575..1583 on both submenu rows; flips to a left chevron on the open row | **none drawn** | **GAP** |
| 6.6 | Gate badge | purple rounded square 1355..1394 x 346..363 after "Group Content" | **none** (no row to carry it; ICON-GAP-AUDIT §10 item 1 refused the glyph for the same reason) | **GAP** |
| 6.7 | Focused row brightness | Start is brighter than Delete/Information (ink 88 rows vs 45) | Open is brighter than Information (298 vs 79 ink counts at threshold 150) | **PARTIAL** |
| 6.8 | Entrance / close timing | 130 ms delay + 250 ms ramp in, hard cut out (documented, commit `0bc5281`) | implemented (`PafEase` delay) | **UNVERIFIED** (not re-timed) |
| 6.9 | Idle hint plate (Triangle/Options) | dark rounded plate ~1663,955 - 1820,990 after a few seconds idle (INV §7) | `idle-hint.png` is **identical** to the rest frame in that region (ink 4 px, same as baseline) even at `idle=9000` ms | **GAP** |

---

## 7. Messages / dialogs

| # | Class | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 7.1 | Inline (empty state in the row slot) | no console frame of this exact class (the console route uses a page) | `dialog-continue.png` (Video -> Continue Watching -> ENTER): a one-line message **"There are no content items."** in ink x ~618..985, y 480..510, the row keeps its icon and focus. Frame diff vs the rest column: 2.6% of the centre region changes, 0.3% of the frame | **PARTIAL** |
| 7.2 | Info page | measured in batch 1 (`dialogs/information-page-01.png`, not in this corpus) | exists (Action::Information) | **UNVERIFIED** |
| 7.3 | Page message (modal over a page) | rule pair 194/192, dimmed page, centred body | rules 134 / missing, no wash (§5.2, §5.6) | **GAP** |
| 7.4 | Box (two-button confirm) | `dlg-confirm-1800/3000.png`: dim wash, rule pair y199/y879, button row, `No` focused by default (INV §9) | route not reached this pass (needs a seeded resume + Triangle + 2x Down); code read only | **UNVERIFIED** |
| 7.5 | Error code | `page-network-test-error` class in the catalogue; no console frame in this corpus | not captured at the right row | **UNVERIFIED** |
| 7.6 | Notification | not in this corpus (INV §14.4: not captured) | `notification-nospace.png`: plaque ink 21.3% of the 1400..1918 x 130..300 window vs 2.3% at rest, drawn flush to x 1918 | **PARTIAL** (the catalogue's `plaque 1480,135,440,110` understates the right edge) |

---

## 8. The info bar and its indicator slots

Reference: `col-game-r00.png` (console, signed out) vs `game-pinned.png`, `ps-indicator.png`,
`notification-nospace.png`, `volume-hud*.png`.

| # | Element | Console (M) | Native (M) | Verdict |
|---|---|---|---|---|
| 8.1 | `indi_base` plate | top border row **75** (mean luma 136.9), left border column **1329** (144.7) | row **75** (138.1), column **1329** (145.7) | **MATCH**. The plate's translucent *fill* reads 82 luma against the console's 63.9, but the two rigs differ in background phase behind the plate, so that term is not attributed to the plate here |
| 8.2 | Clock text + glyph | text runs end at **x 1762** (start 1592: `29/10 12:04`); analog glyph run **1774..1805** | text ends at **x 1762**; glyph run **1775..1806** | **MATCH** to 1 px (format deviation aside: DD/MM 24 h vs US 12 h, the deliberate deviation recorded in `INFO-BAR.md`) |
| 8.3 | Analog clock glyph | 32x32, run **1774..1805** at y 88..119 (INV §6) | run **1775..1806**, same 32 px box | **MATCH** |
| 8.4 | Five 32x32 indicator slots | none drawn - the console is signed out (INV §6) | none drawn, by construction (`view.cpp` gated rule; ICON-GAP-AUDIT §10 item 2 measured the crop byte-identical) | **MATCH** |
| 8.5 | PS-tap controller indicator | `C:\dev\out\rpcs3-gaps-2026-10-28\stills\info-indicator__original__day__1080p.png` (not in this corpus) | `ps-indicator.png`: ink 11,552 px in 1140..1329 x 60..149 vs **60 px** at rest; pad glyph + `1` + `-` on a dark plate left of the bar | **PARTIAL** |
| 8.6 | Volume HUD | not in this corpus | **no HUD ink on any of three scripted paths**: `VOLSET=25` (6 px), `VOLUP` (594 px, all of it the info-bar border), `VOLKEY=up` (same). The catalogue's `volume hud 990,65,250,80` stays background | **GAP** |

---

## 9. The top 12 gaps by visibility

Rects are full-frame 1920x1080. "Delta" is the measured difference on the stated probe.

| # | Gap | Rect | Console | Native | Delta |
|---|---|---|---|---|---|
| 1 | **Every RCO item glyph is drawn as a hollow outline** (§3.1a) - Game column rows, media folders, PSN trophy, Friends rows | focused row 489..628 x 464..569; unfocused quads 535..594 x 660..1020 | body flat 0.56-0.71 of white (204 over 78; 166 over 54) | body 0.10-0.25, oscillating with the wave | body alpha **-0.52**; rim-only silhouette |
| 2 | **Game column row art identity** - 5 of 7 firmware rows ask a different texture than `icon_rsc` | Vita row 489..611 x 464..479; Game Data 489..611 x 464..534 | `tex_album_icon` (plain folder, 178x178) on the Vita and Game Data rows; `item_tex_ps3util` / `item_tex_psputil` on the SD rows | `item_tex_vitaformat` (110x28 wordmark), `item_tex_ps3game_folder`, `item_tex_ps2game_folder`, `item_tex_pspgame_folder`, `item_tex_ps3sd_folder` | 5/7 wrong object; Vita row is a 4:1 bar where the console draws a folder |
| 3 | **Options menu has 2 rows instead of 5** - the whole sort/group group is absent | band x 1320..1900, rows at 301/341 | 5 rows in 2 groups | 2 rows | **-3 rows**, and no 199 px inter-group gap |
| 4 | **Options band and labels sit +66/+73 px right** | band edge 1320 vs 1386; label 1336 vs 1409 | - | - | **+66 / +73 px** |
| 5 | **Volume HUD never draws** | 990,65,250,80 | n/a in corpus | 6 px of ink at the HUD rect on `VOLSET=25` | HUD absent on 3 paths |
| 6 | **Idle hint plate never draws** | 1663,955..1820,990 | plate after a few idle seconds | 4 px of ink, identical to rest | plate absent at `idle=9000` |
| 7 | **Theme page carries 2 rows the console does not** | rows 820 and 891 | 3 rows (607/678/749) | 5 rows | **+2 rows**, +142 px of list |
| 8 | **Message page loses its lower rule and its rule contrast** | y=879, x 200..1699; y=199 | 192 at y879, 194 at y199 | **0** at y879, 134 at y199 | **-192 / -60** luma |
| 9 | **`*User` sub-label never drawn** | x 684..760, y 528..546 | ink under the focused title | slot empty | 1 line of text missing per applicable row |
| 10 | **4 glyph rows above a page where the console has 2** | x 340..480, y 0..170 | 2 bands | 4 bands | **+2 rows** |
| 11 | **Unfocused icon quad too big, focused too small** | unfocused 535..594; focused 489..611 | 48-56 unfocused / 128 focused | 60 unfocused / 123 focused | **+4..+12 px** unfocused, **-5 px** focused |
| 12 | **Row label is narrower than the console's** (same strings, same 32 px em) | label row 678..1080 | "Memory Card Utility" 366 px, Vita title 532 px | 311 px, 497 px | **-15% / -6.6%** advance; ink height 32-39 vs 39-40 |

Two further gaps that a reviewer will ask about but that sit at the edge of this domain: the PSN
column has 3 rows against the console's 5 (Sign Up and PS Plus absent) and the Friends column draws
3 rows against a rowless offline console - both are **content** decisions recorded in `shell.cpp`,
not draw defects.

---

## 10. The local-match list (what is already right, measured)

Fifteen entries, chosen as the matches a reader is most likely to doubt; the element-level MATCH
count in §0 is 18 and the three not repeated here (the focused row's absent bar, the in-page focused
row layout, the plate's five empty slots) are argued in the tables above.

1. **Info-bar plate geometry** - top border row 75, left border column 1329, luma 138.1 vs 136.9,
   column 145.7 vs 144.7. This is a pixel-level match.
2. **Clock text and analog glyph** - both clock clusters end at **x 1762**, and the analog clock glyph
   sits at 1774..1805 (console) vs 1775..1806 (native). Format deviation (DD/MM 24 h vs US 12 h) is
   documented and deliberate.
3. **Depth-2 rule pair positions** - y=199 and y=879 exactly, on both the table page and the message
   page (the message page's *contrast* is the gap, not its position).
4. **Depth-2 back hint** - `O Back` ink 1155..1240 x 897..916 exactly on both sides.
5. **Depth-2 header** - header icon ink run 109..149 and title origin x 164 on both sides.
6. **Depth-1 page grid** - focused 450, rows 607 / 678 / 749, pitch 71, on both sides.
7. **Depth-1 header rule** - y=199/198, 1 px.
8. **Left chevron on a page row** - `explore_plugin_full:tex_arrow_anim` cell 0 at 432..455 x
   492..517 (landed in ICON-GAP-AUDIT §10 item 5, re-read here).
9. **Row pitch 80 and focused row at y 505** - matches INV §4 and `view.cpp:1939`.
10. **Unfocused label x** - 678 native vs 683-684 console, inside 5 px.
11. **Unfocused label dim** - native 0.455 (`view.cpp:2035`) against the console's measured 0.446.
12. **Options panel page dim** - both sides dim content and leave the background (p50 unchanged,
    max 172->131 native vs 92->60 console).
13. **Indicator slots** - both draw none (console signed out; native gated), so the crop is a match
    for the right reason.
14. **Settings column material** - the RCO row names resolve and the `qgl/icontex` normal-map pass
    draws them in brass, which is why that column does not show the hollow-outline defect
    (`shell.cpp:938-944`; the counter-example capture is named there).
15. **Game column state plumbing** - 11 entries, ids and titles read correctly from the sidecar
    (`steam-store`, `steam-folder-all-games`, `vita`, `data`, `memory`, `ps2`, `psp`, `ps3`, `store`,
    `content:refresh`, `content:import`).

---

## 11. Stale or refuted claims in the existing evidence

| # | Claim | Where | Status |
|---|---|---|---|
| S1 | "`C:\dev\out\rpcs3-allgraphics-2026-10-28\` does not exist on this machine" - so every console-frame verdict is drawn from earlier captures | `ICON-GAP-AUDIT.md:25-28` | **STALE**. The batch exists: 162 frames + `INVENTORY.md`. Every "no console frame" note in that audit can be upgraded against it; this document uses it as the primary console reading. |
| S2 | "`XMB plate BG1` bright core 502,463 - 627,547; soft gradient out to 405-724 x 420-560" | `INVENTORY.md` §4 | **OVER-READ**. A scanline at y=505 reads 96-100 from x 405 to 500 (ambient) then a single step to 204 at x 505..625. There is no plate pedestal outside the glyph: the "plate" is the folder's own body. Two console batches agree (`col-game-r01.png`, `day\c-game-root.png`). |
| S3 | "the folder body reads as 0.18 of white over the background" - the premise of the `(255-alpha)*0.78` rewrite | `view.cpp:987-991,1015` | **FALSIFIED**. Console bodies measure 0.68 (`col-game-r01.png`) and 0.56 (`day\c-game-root.png`); 0.18 is what the current code *produces*, not what the console draws. |
| S4 | "the captured Game Data Utility row is `item_tex_ps3game_folder` (folder + gamepad)" | `view.cpp:1967-1969` | **FALSIFIED**. `category_game.xml`'s `seg_gamedata` carries `icon_rsc = tex_album_icon`, and `col-game-r01.png` shows a plain folder with no gamepad emblem. |
| S5 | "the four info-bar indicator slots are gated, never lit" | `ICON-GAP-AUDIT.md` §10 item 2 | **CONFIRMED** (re-measured: both sides draw none). Keep. |
| S6 | Clock format "US date and 12-hour" as a deliberate deviation | `INFO-BAR.md` | **CONFIRMED as a deviation**: the build draws `10/29 12:04 PM` where the console draws `29/10 12:04`. Not a defect, but any future "clock text" score must not be read as a mismatch. |
| S7 | `parity-catalogue.json` row numbers | `work\parity-catalogue.json` states | **NOT STALE - 1-based**. Its "row 4 (Internet Connection Test)" is index 3, which is what `page-net-conn.png` (3 Downs) opened, and "row 1 (status list)" is index 0. Recorded so the next reader does not "fix" a correct route. |
| S8 | `volume-hud` state with `volume hud 990,65,250,80` | `work\parity-catalogue.json` | **UNREPRODUCED**. Three scripted paths raise no HUD ink; see §8.6. Either the state's route needs a different token or the HUD does not draw at 1080p. |
| S9 | "`XMB options band` x from ~1330" | `INVENTORY.md` §8 | **CONFIRMED** (1320 by the edge detector). The native's 1386 is the deviation, not the reference. |

---

## 12. What this pass did not do

1. **No confirm box, error-code page or info page was reached.** The routes need a seeded resume
   record and a specific row index; the box route in `parity-catalogue.json` was not reproduced and
   is marked UNVERIFIED rather than claimed.
2. **No transition timing was re-measured** (options entrance 130/250 ms, page open/close, prompt
   fades). This pass scored settled frames only; `TRANSITION-HARNESS.md` owns the motion.
3. **No theme, colour or background variant** was captured - every frame is Original/day, matching
   the console corpus.
4. **The Settings page row grids were not re-scored** beyond the Theme page, because
   `SETTINGS-COVERAGE.md` and `SETTINGS-FIRMWARE.md` own that surface and this pass's job was the
   page *class* (backdrop, header, rule, rows, chevron, value column).
5. **The unfocused-label idle fade was observed but not compared** against a console frame captured
   after the same idle period (the corpus does not contain one).
6. **One process caution for the next runner**: the `--script` argument must be quoted as a single
   token list (`--script "NOTIFY=Hello world,grab=..."`). Passing it unquoted splits the token on the
   space and the app never reaches `quit`; that is a harness error, not an app hang, and it cost two
   killed instances before it was isolated.

---

## 13. Evidence index

Console (all 1920x1080): `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` -
`col-game-r00..r06`, `col-settings-r00..r15`, `page-settings-theme`, `page-settings-system`,
`page-settings-network`, `page-network-status-list`, `page-network-connection-settings`,
`menu-options-open`, `menu-options-row2..row4`, `menu-submenu-group-content`, `menu-item-corrupted-focused`,
`dlg-confirm-1800`, `dlg-confirm-3000`, `infobar-game-row1-at-rest`, `col-game-idle-hint`;
`C:\dev\out\rpcs3-day-2026-10-28\frames\c-game-root.png` (second reading of the folder body).

Native (this pass, `--render-clock 30 --background-clock 2026-10-29T12:00:00 --ui-clock 2026-10-29T12:04:00 --focus-time 500`):
`C:\dev\out\fs-audit\frames\` - `game-r00..r10`, `game-pinned`, `col-{users,settings,photo,music,video,tv,network,psn,friends}`,
`page-{system,theme,theme-row2,network,net-status,net-conn,net-testerr}`, `all-games-page`,
`options-game`, `options-game2`, `dialog-continue`, `notification-nospace`, `ps-indicator`,
`volume-hud`, `volume-hud2`, `volume-hud3`, `idle-hint`, and the comparison sheets
`m-folder-focused.png`, `m-textures.png`, `m-options.png`, `m-msgpage.png`, `m-infobar.png`.

Source read: `native-xmb/src/view.cpp` (992-1075 item glyph material and draw, 1930-2070 row layout,
label, glow, detail line, 2146 detail slot), `native-xmb/src/shell.cpp` (1014-1058 column and row
tables, 938-944 settings normal-map pass, 1180-1181 host rows), `native-xmb/src/main.cpp` (1047-1130
script token vocabulary, 1131-1160 grab sidecar).

Artefacts: `C:\dev\out\tex\png\explore_plugin_full\item_tex_plain_folder.png` and siblings (alpha
histograms: median 195 / 199 / 151 / 172 / 227 / 255), `C:\dev\rpcs3\bin\dev_flash\vsh\resource\explore\xmb\category_*.xml`
(`icon_rsc` per segment view), `C:\dev\out\tex\all-images.txt` (image tables per container).

