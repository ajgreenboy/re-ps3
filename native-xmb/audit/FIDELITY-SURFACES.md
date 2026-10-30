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

---

## 14. Dated implementation pass - 2026-10-29: the measured RENDER gaps

The first pass in this document that edits source. Files written: `native-xmb/src/view.cpp` (items 1,
2, 7, 8 below), `work/ab-layers.thresholds.json` (a dated `revisionNote20261029b`; no baseline was
lowered) and this section. `view.h` needed no change. No shell, dialogs, settings, assets, main or
hdr file was touched: the two fixes that belong there are returned as hunks in 14.10.

Pair: HEAD `07a4fe4` **plus** the committed-pending root-fix wave already in the tree (BG1 plate, clock
disc, icon growth, plate ramps, bottom band), built into `out/native-render2/re-ps3-native.exe` from
`native-xmb/build.ps1`. Every capture uses the audits' pins
(`--dev-flash C:\dev\rpcs3\bin\dev_flash --render-clock 30 --background-clock 2026-10-28T10:00:00
--ui-clock 2026-10-28T21:40:00 --focus-time 2000`, script token `size=1920x1080`), seed
`out/native-reference/store/ui3-data` (except the OSK route, which copies the `jf-smoke` seed). "Before"
and "after" are two captures of one source tree; frames live under `out/render2/frames/`, crops under
`out/render2/crops/`, both scratch, outside the repository. Luma is Rec.709, same reader as the audits.

### 14.1 Target vs achieved, all eight items

| item | audit target | achieved on the fixed build | verdict |
|---|---|---|---|
| 1 - hollow RCO glyphs | body flat 0.56-0.71 of white (204 over 96 focused; 166 over 54 unfocused) | focused folder body **204.8-206.6** over 134-150 (was 166.7); unfocused **178.0-180.7** over 49 (was 87.8-101.9); media folder **211.6-212.6** (was 150.0-151.8); PSN trophy **206.5-218.0** (was 145.9-153.9); Friends row **214.3-216.4** (was 148.0-150.7); every body flat, no background wave | **met** |
| 2 - options band geometry | band edge x 1320, label ink x 1336 | edge **1320** (was 1320 - the +66 did not reproduce), label ink **1333** (pen 1334) against 1336, rows **540/581** against 540/581 (were 542/581 on E1's 38 px grid) | **met** (the two-row list is the route, 14.3) |
| 3 - idle hint plate | dark rounded plate at 1663,955 - 1820,990 | **not drawn, deliberately**: five console frames measure nothing there (14.4) | **refused as fabricated** |
| 4 - message-page bottom rule | y=879 at 192, y=199 at 194 | the message class already draws **199.4 / 190.6** at y 199 / 879; the two network rows raise the *info* class (198 at 133.9, 872 at 98.5) - a shell routing hunk, 14.10 | **partly returned** |
| 5 - `*User` sub-label | ink x 684..760, y 528..546 | slot verified: this build draws the shell's sub-label at ink x 682..720, **y 528..541** (console `*User` 682..730, y 528..542); the string is shell data (14.6) | **slot met; string returned** |
| 6 - #10 / #11 / #12 | 2 glyph rows / 48-56 px quad / +15 % advance | #10 **3 rows on both sides** (the 4-vs-2 does not reproduce); #11 **50x34** against the console's 52x36 (already at the measured value); #12 **275 = 275** and **310 = 310** px, i.e. no advance deficit (14.7) | **two falsified, one already met** |
| 7 - OSK cursor marker | peak 215, present in the dark blink phase | ink **215** (was 255; no pixel above 245 in the band), apex-left at caret+4 (was apex-right at caret+12), and present at `--render-clock 20.4` while the I-beam is absent | **met** |
| 8 - volume HUD signature | a pinned capture reproduces the live meter | pinning `VOLSET=25` now draws the meter: plate **853..1066 x 1001..1052**, three lit segments at x 866..874 / 886..894 / 906..914, segment colour **128,154,50** (was: no meter at all in 6 of 6 pinned captures) | **met** |

### 14.2 Item 1 - the glyph alpha rewrite is deleted, not retuned

`View::itemGlyph` no longer classifies a texture by its covered-alpha median and no longer rewrites
non-rim alpha to `(255-alpha)*0.78`. It returns the source, format-normalised and cached. The
justification is a closed arithmetic loop, not a retune:

- `item_tex_plain_folder` is 314x178, median covered alpha **195** (0.765), median RGB **237**.
- Console `col-game-r01.png`, scanline through the focused folder body at y=505: background 95.9-99.8,
  then a **flat 202..204** from x 510 to x 620, then 96.8. Source-over with those texels gives
  `96*(1-.765) + 237*.765 = 203.9`.
- Second console batch: `rpcs3-day-2026-10-28/frames/c-game-root.png` body **166 over 54** =
  `54*(1-.765) + 237*.765 = 194` for a mask of the same median; the day frame's alpha is the same
  mask at a darker background.

So the composite the console draws IS the mask's own alpha, and the old rewrite inverted it: the 195
texel landed at `(255-195)*0.78/255 = 0.184`, which is the hollow outlined rectangle the audit saw.
The comment that justified it ("the folder body reads as 0.18 of white over the background") is
replaced with these numbers in the code, labelled TRACED, with FIT: nothing.

Measured, before -> after (same pins, same route, 5 px steps through the body):

| row / frame | console | before | after |
|---|---|---|---|
| focused folder, y=505 (`after-root-folder`, `col-game-r01`) | body 202..204 over 96 | 166.7 over 134..143 | **204.8..206.6** over 134..150 (emblem 246.2) |
| unfocused folder, y=679 (`after-root`, `col-game-r00`) | body 167.3..167.6 over 49 | 87.8..101.9 over 49.5..63 | **178.0..180.7** over 49.5 (emblem 244.2) |
| media folder `item_tex_plain_folder`, focused (`before/after-media-folder`) | plain folder, 0.68 white | 150.0..151.8 over 134 | **211.6..212.6** over 134..136 |
| PSN trophy row (``after-psn-trophy``) | - | 145.9..153.9 | **206.5..218.0** |
| Friends row (`after-friends`) | - | 148.0..150.7 | **214.3..216.4** |

The unfocused body lands 11 luma above the console's (179 against 167), i.e. 0.63 against 0.56 of
white; the focused body lands 1-2 luma above (205 against 204). The focused body's own *run* at y=505
is x 510..620 on both sides, i.e. the same 111 px of ink at the row's centre line, so the alpha fix did
not resize anything: the root wave's 128 px ink box (rim included) is unchanged. That residual level is
not retuned here: the
console's unfocused rows may carry a row-level dim of ~0.82 on the icon, which no local frame proves
independently of the texture, and FIDELITY-ROOT-BAR I9 explicitly measures "no icon dimming". The
larger remaining term is the background residue (B6, `hdr.cpp`, out of this pass's fence): the focused
row's background is +40 luma here, so the same alpha cannot produce the console's +108 lift.

Crop scores, before -> after, `work/ab-compare.ts` (content-bound: this build's rows carry different
art from the reference frame's, so PSNR and SSIM disagree, exactly as 11.4 records for the root wave):

| crop | vs `col-game-r01.png` | vs `col-game-r00.png` |
|---|---|---|
| focused glyph 470,440,200,130 | 12.97 / 0.4774 -> 12.48 / **0.5677** | 11.45 / 0.3951 -> 11.45 / 0.3951 |
| unfocused icon column 520,650,90,380 | 14.60 / 0.6039 -> 13.90 / **0.6257** | 14.45 / 0.6496 -> **14.52 / 0.6793** |
| item label row 400,425,800,100 | 11.85 / 0.4480 -> 11.71 / **0.4720** | 11.69 / 0.3987 -> 11.69 / 0.3987 |
| label band 660,470,600,120 | 11.39 / 0.3689 -> 11.39 / 0.3689 | 11.25 / 0.3039 -> 11.25 / 0.3039 |

SSIM rises where the glyph is (the structure is now the console's) while PSNR falls or stalls (the
residual error is now a bright glyph of the *wrong art* in the right place - the Vita row's 110x28
`item_tex_vitaformat` badge and the folder emblems the console does not draw, gaps 2 and 11 of
FIDELITY-ROOT-BAR 7, whose one-line shell hunk is 11.7 there). Unchanged to the last digit: the label
band and every crop without an RCO glyph, which is the evidence that the change reached only the masks.

### 14.3 Item 2 - the band was already at 1320; the row grid was not

FIDELITY-SURFACES 6.1/6.2 (+66 / +73 px) is **stale against the pending tree**: it was measured on the
18:33 binary (`07a4fe4`), and the committed-pending wave already draws the firmware plate
(`system_plugin:tex_optionmenu_bg` at object alpha 0.33) on `x0 = 1320`. Measured now:

- band edge, `menu-options-open.png` y=100: 84.8 at x1315 -> 89.9 at 1320 -> 108 peak; this build:
  95.9 at 1315 -> 105.9 at 1320 -> 133 peak. **Edge 1320 on both** (the fill's absolute level differs
  because the page behind is dimmed differently and this build's field is +12).
- label ink: console x 1336, this build x 1333 (pen 1334 = 1320 + the measured inset 14). **-3 px**,
  inside the glyph bearing.
- row grid: this is where the audit measured a real delta. The frame's ink tops are 301/341 (utility
  group) and 540/581/621 (action group), i.e. centres 311.5/351.5 and 549.5/590.5/630.5 - a **40.5 px
  pitch**, first action row **549.5**, group gap **238**. The committed values were E1's 38 px pitch at
  rowCentre 552. Changed to the frame's arithmetic: rows now at 540/581 against 540/581 (before:
  542/581), utility group at 311.5/351.5 (before: 313.5/351.5).

The row *structure* (5 rows in 2 groups) is not faked in the view and needs no hunk: the two-group form
is produced by the shell and drawn on the rows that have it. Measured on
`out/native-reference/layers/video-options-panel.png`, a media item's Triangle menu: `Sort By` at ink
302..324, a 215 px gap, then `Open` 540..562, `Copy` (disabled), `Delete` (disabled), `Information`
662..679. The audit's route (Game > All Games folder > TRIANGLE) legitimately lists two rows because a
*folder* row has no sort group; the one row that is genuinely absent from every list is `Group
Content`, which `shell.cpp:1892-1893` refuses in writing ("Group Content has no native store, so it
stays out rather than being drawn inert"). Drawing it inert is exactly what that refusal forbids, so it
is a content gap, not a view gap.

### 14.4 Item 3 - the idle hint plate does not exist in the corpus; left undrawn

Re-measured independently, the audit's own rect, five frames, max luma against a local background
(1663,955)-(1820,990) and (1663,900,157,40):

| frame | max luma in the rect | local background |
|---|---:|---:|
| `col-game-idle-hint.png` | 58.6 | 55.4 |
| `col-settings-idle-hint.png` | 58.6 | 55.3 |
| `col-video-idle-hint.png` | 58.6 | 55.4 |
| `infobar-game-row1-at-rest.png` | 57.6 | 54.3 |
| `col-game-r00.png` | 58.6 | 55.4 |

A dark rounded plate with a Triangle glyph and the word `Options` would have to be either much darker or
edge-bright in that rect; five frames read +3.2 luma over their own background, i.e. nothing. The
native side agrees and is unchanged: at `idle=9000` the rect carries no ink above luma 200 (peak 60.9,
the background). **Nothing was drawn.** This confirms FIDELITY-ROOT-BAR 6.1's own verdict from a second
reader; the plate remains a capture to obtain, not a surface to implement.

### 14.5 Item 4 - the message class is right; the frame is a different class

`page-network-connection-settings.png` is the console's D4 page (heading `Internet Connection Test`,
body `Please wait...`, the info bar still up). Measured rule pair, x=800:

| frame | y=199 | y=879 | class |
|---|---:|---:|---|
| console `page-network-connection-settings.png` | **194.3** (bg 55.4) | **191.6** (bg 48.2) | D4 message |
| this build, PSN > Sign In > message page (`after-psn-msg.png`) | **199.4** (bg 73.5) | **190.6** (bg 44.1) | D4 message |
| this build, Settings > Network > the two network rows (`after-netconn.png`, `after-nettest.png`) | **133.9** (bg 100.1, at y=198) | **no rule at 879** (row 872 reads 98.5) | D5 info |

So the message class this document's 5.2/7.3 describes already draws its pair correctly (+5 and -1 luma
against the console) - the audit's native frame was the *info* page, whose own rules are
`kInfoRuleTop/Bottom = 198/872` at alpha 56/51 (a fit with no console frame behind it). The gap that
remains is which page class the two network rows raise, and that is `shell.cpp:1811/1818`
(`Action::Information` -> `MessageKind::Info` with the host report). The hunk is in 14.10; the drawing
code needs no change, and changing the D5 pair to the D4 numbers instead would corrupt a page class
whose own reference does not exist.

### 14.6 Item 5 - the slot is real and already at the measured row; the string is shell data

Focused `Saved Data Utility (PS3)` (row 7, `before-ps3row.png` / `after-ps3row.png`): this build draws
`Shell::subtitleText(entry)` in the slot at `labelX, y+20`, 17 px. Measured ink **x 682..720, y
528..541** (14 px tall). The console's row (`col-game-r04.png`) reads **`*User`, x 682..730, y
528..542** (15 px tall). So the slot's geometry, type and origin are right to 0/1 px and only the
firmware's scope marker is missing; the shell returns the profile name (`profiles_.activeName()`, which
on the default profile is the word `User`) for `trophy`/`ps3` and nothing for the rows the console also
marks (Vita, Saved Data minis/PSP). Measured which rows carry it: `col-game-r00.png` (Vita, focused) and
`col-game-r03.png` / `col-game-r04.png` (both Saved Data rows) show `*User`; `col-game-r01.png` (Game
Data Utility) and `col-game-r02.png` (Memory Card Utility) show nothing, i.e. the two system-wide rows
are correctly blank. Zero console frame gives the PS trophy row a sub-label. The hunk is in 14.10.

### 14.7 Item 6 - #10 and #12 are falsified, #11 was already at the value

- **#10 (4 glyph rows above a page against 2).** Does not reproduce. On the matched page pair
  (`page-settings-theme.png` vs `before/after-theme-page.png`, window x 340..480, y 0..200) both sides
  draw **three** parent-column glyphs, one clipped at the top edge: this build's ink runs 0..11,
  43..90, 115..163; the console's brightest rows are 1..3/8..8/61/134 at the same x. The page header,
  the 199 rule and the parent dim are unchanged by this pass.
- **#11 (unfocused icon quad 60 against 48-56).** Already at the value: the unfocused folder glyph's
  ink is **x 540..589 (50 px) y 663..696 (34 px)** against the console's **x 539..590 (52) y 662..697
  (36)**. The committed-pending wave's 60 -> 50 change (FIDELITY-ROOT-BAR 11) did this; nothing here.
- **#12 (label advance -15 % / -6.6 %).** **Falsified.** Same string, same state:
  `Game Data Utility` unfocused is **275 px on both** (console x 683..957, this build x 680..954); the
  Vita title focused is **529 px on both** (console x 684..1212, this build x 681..1209); `Memory Card
  Utility` reads **310 px** on the console focused (x 685..994) and **310 px** here unfocused (x
  680..990) - the focused band on this build cannot be read at the 245 threshold because the background
  ribbon crest crosses it (the box then grows to x 1124). The audit's 366 px `Memory Card Utility` was
  measured the same way: on `col-game-r00.png` rows 743..781 the band's peak is 211 (a ribbon crest, not
  text) and the ink box grows to x 1049, while the same label on its own dark background is 275 px. No
  advance change was made, and no glyph metric was touched.

### 14.8 Item 7 - the marker: colour, orientation, and the blink phase

Measured on the three console OSK frames this pass (`osk-default.png`, `osk-default-2.png`,
`osk-key-down.png`; all three are identical in this band): the I-beam's green shaft (130,249,12) is
x 578..579 at rows 346..378, and the marker's ink is **x 582..594 x y 353..369** on a 71,71,71 field,
reading **215** on its vertical base (x 593..594) and 229 at the apex (x 584..585). Zoomed 10x, the
triangle is apex-LEFT: the two diagonals run right from the apex into the vertical base. The old draw
had it apex-right (vertical edge at `caret + markerInset 12`, apex at `+26`), 14x20, in pure white
(2 px `Qt::white` lines, 255 measured).

Changed: ink 215, apex at caret+4, 13 x 17, vertical base on the right, and drawn outside the caret's
blink gate. Measured after: ink **x 517..531, y 349..367** (caret at 514..515, so apex = caret+4), **no
pixel at or above 245 anywhere in the band**, peak exactly 215. Dark phase
(`--render-clock 20.4`, 20400/468 = 43.59 -> odd): the green shaft is **absent** (x 514..515 reads
72,72,72) and the marker is **present** with the same bbox and ink.

Evidence class, stated plainly: colour, bbox, offset and orientation are TRACED on the frames. That the
marker does not blink is **not** shown by any local frame - all three console frames are in the lit
phase - it rests on OSK-FIRMWARE 5.2 ("present whether or not the I-beam is lit") and FIDELITY-MEDIA
O6, and it is what keeps a pinned capture from losing the marker on an odd blink phase.

Bookkeeping: `osk_layout.h`'s `markerInset 12 / markerWidth 14 / markerHeight 20` (OSK-FIRMWARE 5.2's
readings on `s2/f0020-0077.png`, a different frame at a different caret position) are now **superseded
and unused** - the marker's three constants live in `view.cpp` with the frames that measured them,
because that header is outside this pass's fence. Delete the three fields when that file is next in
scope; nothing else reads them.

### 14.9 Item 8 - the HUD's fade enters the UI-layer signature

`view.cpp`'s signature carried `" vol <opacity> <level> <muted> "` and not the 150 ms appear edge that
`drawScene` multiplies in. The rebuild therefore happened once, 4 ms into the fade (level and opacity
both change at that instant), and never again while the level stayed put, freezing the layer at
`fadeIn = 0.027` - FIDELITY-MEDIA H3's 43.39 dB / 0.9966 "lost meter" in 5 of 6 pinned captures. The
fade amount now enters the signature quantised by 1/256 like the opacity beside it, so the layer
re-samples through the fade and settles at full strength.

Measured, `VOLSET=25` then two `wait` tokens (600 ms, past the 150 ms edge), scanline y=1028:

- before: x 850..1066 flat **69.1-70.7** - no meter, in the same capture recipe that produced the
  audit's "no HUD ink".
- after: plate **853..1066 x 1001..1052** (edges at x 852/853, x 1066, y 1001, y 1052 - FIDELITY-MEDIA
  H1's 853,1002-1066,1052 within 1 px), ten 20 px cells, **three lit** at x 866..874 / 886..894 /
  906..914 in the texture's own **128,154,50**, the other seven dim at 20-30 % - i.e. 25 % of ten
  segments, the same reading MEDIA-PANELS-FIRMWARE 11.1 records for `hud25` and the same value H3
  measured on a *live* run.

Two notes for the next reader. (a) The parity catalogue's rect for this state,
`volume hud 990,65,250,80`, is **not the HUD's rect**: it is the info bar's top edge, and section 8.6's
"no HUD ink on three paths" was measured there. The HUD's rect is 853,1002-1066,1052 (MEDIA-PANELS H1), which is
where both the before and after captures were re-measured. (b) The fade itself cannot be grabbed from a
`--script` run: tokens are 300 ms apart, so every pinned capture lands after the 150 ms edge; the
signature fix is what makes that post-fade frame the full-strength meter instead of a 2.7 % ghost.

### 14.10 Returned hunks (files outside this pass's fence)

Both are `native-xmb/src/shell.cpp`. Neither was applied.

**A - item 5, the `*User` marker (Shell::subtitleText, ~3479).**

```diff
 QString Shell::subtitleText(const Entry& entry) const {
-    // User-scoped data carries the active user's name, as in the reference (Trophy Collection, PS3 saved data).
-    if(entry.id=="trophy" || entry.id=="ps3")return profiles_.activeName();
+    // A user-scoped row's second line is the firmware's own scope marker, not the active user's name.
+    // Measured (FIDELITY-SURFACES 14.6): col-game-r00 (Vita, focused), col-game-r03 (Saved Data
+    // (minis/PSP)) and col-game-r04 (Saved Data (PS3)) all read "*User" at x 682..730, y 528..542, while
+    // the two system-wide rows (Game Data Utility, Memory Card Utility) carry nothing there. This build
+    // draws the word "User" at x 682..720 on the same 528 row, so only the marker is missing.
+    // [TRACED: which rows carry it, the string, the rect. FIT: none.]
+    static const QSet<QString> userScope{QStringLiteral("vita"),QStringLiteral("psp"),
+                                         QStringLiteral("ps3"),QStringLiteral("trophy")};
+    if(userScope.contains(entry.id))return QStringLiteral("*User");
```

**B - item 4, the two network rows raise the wrong page class (Action::Information, ~1811/1818).**

```diff
     case Action::Information: {
         const host::Label label=[this](const QString& id,const QString& fallback){return assets_.text("sysconf_plugin",id,fallback);};
         QString body;
-        if(entry.value=="host:network")body=host::networkStatus(label);
+        // The console answers the two network rows with the firmware's own D4 message page, not a report:
+        // page-network-connection-settings.png carries the heading msg_internet_connect_test and the body
+        // "Please wait..." on the page class's rule pair (194 at y199, 192 at y879), while this build draws
+        // the D5 info page there (198 at 133.9, 872 at 98.5; FIDELITY-SURFACES 14.5). The live report stays
+        // reachable from the status-list row, which is the row the console shows it on.
+        if(entry.value=="host:network") {
+            showMessage(assets_.text("sysconf_plugin","msg_internet_connect_test","Internet Connection Test"),
+                        assets_.text("sysconf_plugin","msg_wait","Please wait..."),MessageKind::Page);
+            break;
+        }
```

Both strings exist in `sysconf_plugin` (dumped this pass with `--rco-text`: `msg_internet_connect_test`
= "Internet Connection Test", `msg_internet_connect_setting` = "Internet Connection Settings",
`msg_internetsetting_disconnect_ask` = "Adjust settings for connection to the Internet....").  The
status-list row must keep `host:network`'s report: the shell needs to tell the report row from the two
message rows by id (`net_netlist` vs `net_internet_setting` / `net_internet_test`), which is why this is
a hunk and not a view edit.

### 14.11 The gate, and what moved

`bun work/ab-layers.ts --all --exe out/native-render2/re-ps3-native.exe`:

| family | before (floor) | after (measured) | verdict |
|---|---|---|---|
| stored pair (offline) | 18.2027 / 0.8388 | **18.2027 / 0.8388** | 6/6 PASS, unchanged by design |
| frame (canonical capture) | 19.2081 / 0.8475 | **18.9150 / 0.8468** | PASS (-0.30 dB, -0.0007) |
| top background | 21.0612 / 0.9434 | 21.0612 / 0.9434 | PASS, unchanged to the last digit |
| category icon row | 21.6196 / 0.9062 | 21.6196 / 0.9062 | PASS, unchanged |
| item label row | 12.0606 / 0.4171 | 12.0606 / 0.4171 | PASS, unchanged |
| clock text | 11.5202 / 0.4662 | 11.5202 / 0.4662 | PASS, unchanged |
| info bar | 15.1517 / 0.7738 | 15.1517 / 0.7738 | PASS, unchanged |
| text presence | 1703 bright px in the clock crop | **1703** (minimum 900) | PASS |
| named states | 55 dB / 0.9995 self-baseline | **12/12 PASS** | PASS |

**No floor was lowered.** The one layer that fell is the whole frame, and its floor stays at
19.2081 - 0.5 dB. The fall is content-bound and measurable: the stored reference
`out/native-reference/ab-rpcs3-root.png` carries a **single** `Corrupted Data` row - no icon ink above
luma 90 anywhere in x 520..610, y 640..1040 - so this build's 11-row Steam column adds correct, brighter
glyphs where the reference has background. Scored on the console frames that *do* show the glyphs
(col-game-r00/r01) the same change is the fix (14.2). The five layers without an RCO glyph are unchanged
to the last digit, which is the evidence that the pass reached only the item masks.

The 12 named-state references were re-saved (`--states --save-reference`) because the row glyphs are on
every surface. Old reference against the new capture, per state: `steam-game-root` 31.90 / 0.9970,
`steam-all-games` 30.17 / 0.9923, `steam-store-sections` 35.49 / 0.9970, `steam-not-installed` 41.12 /
0.9979, `xmb-notification` 31.90 / 0.9970, `xmb-volume-hud` 30.45 / 0.9919, `video-options-panel` 32.41
/ 0.9900, `continue-watching` 26.69 / 0.9857, `continue-watching-empty` 26.15 / 0.9853, `subtitle-cue`
inf / 1.0000, `jellyfin-signin` 41.47 / 0.9978, `steam-download-page` 37.11 / 0.9968. The volume-HUD
state additionally re-saves *with* the meter now drawn (14.9), so its reference changed for two reasons.

### 14.12 Determinism

Two `--render-clock 20 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-28T21:40:00
--focus-time 2000` captures with the token list `full,size=1920x1080,wait,wait,wait` are byte-identical:
SHA256 `43253C23CDEFF08D6D85E3A78856933ACAD0724D1D953E9FCD251F35E77E51DA` both times.

### 14.13 What this pass did not do

- Did not touch `shell.*`, `dialogs.cpp`, `settings_menu.*`, `assets.*`, `main.cpp` or `hdr.cpp`; the two
  fixes that belong there are 14.10.
- Did not fix the wrong row art (Vita badge, `ps3game_folder`'s emblem): gaps 2/11 of
  FIDELITY-ROOT-BAR 7, whose hunk is already written at 11.7 there.
- Did not add the ~0.82 icon dim the unfocused body would need to land exactly on 167 luma; FIDELITY-
  ROOT-BAR I9 measures "no icon dimming", and one texture's body alpha cannot out-vote it.
- Did not obtain a console frame of the marker's dark blink phase (none exists locally), nor one of the
  HUD (the console corpus has no volume change at all) - both verdicts rest on the audits' traces.
- Did not re-open the options panel's `Group Content` row (a content refusal, 14.3) or the D5 info
  page's own rule fit (no reference frame exists for it).

---

## 15. Dated implementation pass - 2026-10-29: the measured DATA gaps

The second pass in this document that edits source, and the first one whose subject is **row data**:
which texture each row binds, which rows a column carries and in what order, which rows an options menu
offers, and which columns the bar draws. Files written: `native-xmb/src/shell.h`, `shell.cpp`,
`settings_menu.h`, `settings_menu.cpp` and this section (`SETTINGS-FIRMWARE.md` gets a matching Theme
correction). `view.cpp` / `view.h` were held by the render pass (section 14) and **were not touched**:
everything the drawing half needs is returned as a hunk in 15.8. No dialogs, assets, main, ribbon, hdr,
CMake, tool, budget, reference or catalogue file was edited.

Build: `native-xmb/build.ps1 -OutputDirectory C:\dev\out\native-data1` from HEAD `362b3f7` plus the
pending behaviour wave **plus** the render pass's section-14 edits already in the tree. Pins for every
capture: `--dev-flash C:\dev\rpcs3\bin\dev_flash --render-clock 30 --background-clock
2026-10-29T12:00:00 --ui-clock 2026-10-29T12:04:00 --focus-time 2000` and the script token `full`
(1920x1080). Frames and sheets are under `C:\dev\out\data1\`, scratch, outside the repository.

### 15.1 The firmware text this pass read

| source | what it gave |
|---|---|
| `category_game.xml` | `icon_rsc`/`title_rsc` per segment view: `tex_album_icon` on `seg_psp2appdata` and `seg_gamedata`, `item_tex_ps12util` on `seg_mcutility`, `item_tex_ps3util` on `seg_sdps2` and on `seg_sdps3`, `item_tex_psputil` on `seg_sdpsp` |
| `category_video.xml` | the root view's ordered queries: `seg_bddata_mgmt`, `seg_dlna_scan`, `seg_editingvideo_mgmt` are the three the console shows at rest; every other query is a provider (disc, DLNA server, HDD contents, install, store, in-game) |
| `category_music.xml` | `seg_dlna_scan`, `seg_playlist_mgmt` (plus `seg_now`, which only appears while a track plays) |
| `category_photo.xml` | `seg_dlna_scan`, `seg_hakoniwa`, `seg_playlist_mgmt` (plus `seg_screenshot`) |
| `category_tv.xml` | nothing but content providers (`sel://.../welcome?type=tv`, the in-game exit, `xil://...t=tv`): the column exists only when something is registered |
| `explore_category_video/music/photo.rco` | the titles: `msg_bd_data_utility`, `msg_scan_mediaserver`, `msg_video_editor_and_upload`, `msg_playlists`, `msg_photo_gallery` |
| `explore_plugin_full.rco` | the item menu's labels: `msg_sort`, `msg_folder_class` ("Group Content"), `msg_game_start`, `msg_del`, `msg_info`, `msg_title_folder` ("By Title"), `msg_all` ("All"), and the seven other `*_folder` keys |
| `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` | `menu-options-open`, `menu-submenu-group-content`, `col-game-r00..r04`, `col2-video-r00..r02`, `col-music-r00/r01`, `col-photo-r00..r02`, `page-settings-theme` |

### 15.2 Item 1 - the Game column's row art, re-bound to the definition's `icon_rsc`

Measured on the console's own 7-row walk (`col-game-r00`..`r06`, INVENTORY 5.1). Five of the seven rows
bound a texture the definition does not name there; they now bind exactly what it names:

| native id | console `icon_rsc` (M) | old texture | new texture | container | what the frame shows |
|---|---|---|---|---|---|
| `vita` | `tex_album_icon` | `item_tex_vitaformat` (110x28 wordmark) | **`tex_album_icon`** (178x178, ink 121x84) | explore_plugin_full | plain folder |
| `data` | `tex_album_icon` | `item_tex_ps3game_folder` (folder + page-curl) | **`tex_album_icon`** | explore_plugin_full | plain folder |
| `memory` | `item_tex_ps12util` | `item_tex_ps12util` | unchanged (correct) | explore_plugin_full | two card slots |
| `ps2` | `item_tex_ps3util` | `item_tex_ps2game_folder` | **`item_tex_ps3util`** (folder + gamepad) | explore_plugin_full | (no PS2 row in this console's frame; the definition names the PS3 utility folder) |
| `psp` | `item_tex_psputil` | `item_tex_pspgame_folder` (folder + U emblem) | **`item_tex_psputil`** (folder + card slot) | **explore_category_game** | folder with a card-slot emblem |
| `ps3` | `item_tex_ps3util` | `item_tex_ps3sd_folder` (folder + four face buttons) | **`item_tex_ps3util`** (folder + gamepad) | explore_plugin_full | folder with a gamepad emblem |
| `store` | (the `seg_welcome` query has no `icon_rsc`) | `item_tex_ps_store` | unchanged | explore_plugin_full | blue shopping bag |

`item_tex_psputil` exists only in `explore_category_game.rco` (the export under `out/tex/png/
explore_category_game/`), which is why that row carries a different container from its six siblings.

**The 1:1 crop check the render pass asked for (the Vita row).** `col-game-r00.png` cropped to
440,430 230x150 and re-read at 4x (`out/data1/vita-4x.png`) shows a folder with a tab at the top left, a
flat body and a thin fold near the bottom: **no emblem inside the body and no badge anywhere**, so the row
is neither a composite nor the PSP-emblem folder. Ink measurements on the console frame put the focused
glyph at x 502..628, y 462..547 (128 x 85, aspect 1.51); the two candidate placeholder folders measure
`item_tex_plain_folder` 197x134 (1.47) and `tex_album_icon` 121x84 (1.44) against `item_tex_psputil`
88x64 (1.38, and visibly an emblem folder). The frame's art is a plain folder, and of the two folders the
definition names the one this row binds is `tex_album_icon` - so the answer is **the placeholder folder,
not a composite**, and no draw hunk is needed for this row. (The render pass's question is answered, not
deferred: the badge would have had to appear inside the body, and the 4x crop has none.)

**The build's own record of the binding** (grep target for a scripted run):

```
xmbml: game column row art ( 7 rows) vita=explore_plugin_full:tex_album_icon
  data=explore_plugin_full:tex_album_icon memory=explore_plugin_full:item_tex_ps12util
  ps2=explore_plugin_full:item_tex_ps3util psp=explore_category_game:item_tex_psputil
  ps3=explore_plugin_full:item_tex_ps3util store=explore_plugin_full:item_tex_ps_store
```

The entry dump (`XMB_DUMP_ENTRIES`) now carries `container` and `texture` on **every** row, so the same
binding is checkable from JSON as well as from the log.

**What the change costs the canonical gate, and why that is the right trade.** The five layer crops that
cover this pass's surfaces are unchanged to the last digit and the whole-frame layer falls from 19.21 to
18.01 dB (-1.20). The fall is entirely the row-icon column, and that is measured two ways:

- capture the same canonical recipe from the pre-pass build (`out/native-beh2/re-ps3-native.exe`, the
  behaviour wave): frame **18.91** / 0.8468 (-0.30 against the same floor, PASS). So this pass owns
  -0.90 dB of the -1.20;
- paste that build's icon column (x 460..700, y 440..1080) into this pass's capture: frame **18.90**,
  i.e. the whole delta is the icon column and nothing else. Re-pasting the whole row area gives 18.91.

The cause is the reference, not the art: `out/native-reference/ab-rpcs3-root.png` is a console session
whose Game column holds **one** row (`Corrupted Data`) - no icon ink above luma 150 anywhere in
x 520..620, y 660..1060 (measured: mean luma 58.9, 0 ink px), while the correct art puts solid folders
there. The comparator that does contain the rows is the console's own 7-row walk, and there the new art
is the fix (`col-game-r00`..`r04` versus `out/data1/game-r01..r06`): plain folders on rows 1-2, a
card-slot folder on Memory Card, card-slot and gamepad folders on the two Saved Data rows. A per-crop
`ab-compare` score on that region is reported in 15.10 with the caveat that the crop also carries the
focus plate, whose phase differs between the two sessions - which is why the identity evidence above is
the 1:1 crop and the texture table, not the PSNR.

### 15.3 Item 2 - the three media columns, rebuilt from their definitions

The console's media columns carry rows this build did not have at all (FIDELITY-MEDIA V1/M1/P1: "0 of the
console's 8 media-column rows exist natively; 10 native-only rows"). `Shell::resolveMediaColumns()` now
reads `category_video.xml`, `category_music.xml` and `category_photo.xml` on every start - the same
reader the Network column proved (`readCategoryView`) - and builds each column as:

1. the definition's rows, **in document order**, keeping any `title_rsc` the definition carries and
   taking the plugin-built labels from the column's own RCO where it does not (the count and the skipped
   queries are logged per column);
2. then this build's native rows, last, with `source = "native"` in the entry dump.

**Choice: native rows stay in the column, after the console's rows, rather than moving under a submenu.**
The console's order is preserved exactly - every one of its rows is above every native row - and the
three features that need a door keep one without inventing a menu level: Continue Watching is the only
surface a resume record has, Video/Music Library is the only way into a host folder, and Choose Media
Folder is the only way to pick one. A submenu would have required a row of its own (which is the very
thing this item is removing from the Theme page), so the separation is made in data (`source`) and in the
dump, and the rows are grouped at the end.

| column | console rows, in order (`source = xmbml`) | native rows (`source = native`) | new count | old count |
|---|---|---:|---:|---:|
| Video | `bddata` BD Data Utility, `dlna-scan` Search for Media Servers, `video-editor` Video Editor & Uploader | `continue` Continue Watching, `library` Video Library, `choose` Choose Media Folder | **6** | 4 |
| Music | `dlna-scan` Search for Media Servers, `playlists` Playlists | `library` Music Library, `choose` Choose Media Folder | **4** | 3 |
| Photo | `dlna-scan` Search for Media Servers, `gallery` Photo Gallery, `playlists` Playlists | `library` Photo Library, `choose` Choose Media Folder, `options` Photo Settings | **6** | 3 |

Two former native rows are **gone**, and neither loses a function: Video Settings and Music Settings are
the same pages the Settings column's own "Video Settings" / "Music Settings" rows open (`Action::
SettingsPage` with page id `video` / `music`), so keeping them in the media columns duplicated a door.
Photo Settings stays because no other surface reaches it (the console has no photo settings page).

Row art, from the console frames and the definition:

| row | console frame | texture | container |
|---|---|---|---|
| BD Data Utility | plain folder | `item_tex_plain_folder` | explore_category_video |
| Search for Media Servers (all three columns) | sliders + magnifier | `item_tex_dlna_scan` (the definition's own `icon_rsc`) | that column |
| Video Editor & Uploader | hollow triangle over two dots | `item_tex_upload` | explore_category_video |
| Playlists (music and photo) | filled triangle over bullets | `tex_playlist` | that column |
| Photo Gallery | a collage of three photos | `hakoniwa.png` (a 180x180 PNG on dev_flash, the `seg_hakoniwa` provider's own icon; the shop variant of `category_photo.xml` names it as `<Pair key="icon">`) | `file` |

Every one of the five was picked by cropping the console's focused row at 4x and matching the glyph, not
by name (`out/data1/sheet-media-icons.png`: console crop, then each candidate at the same scale:
`item_tex_plain_folder` and `bddata_tex_album_default` for BD Data; `item_tex_upload` and
`tex_add_playlist` for Video Editor; `tex_playlist` for Playlists; `photo_tex_album_pic` and
`hakoniwa.png` for Photo Gallery - the collage matches `hakoniwa.png` pixel for pixel and the others do
not match at all).

The rows this build cannot perform are `Action::Unavailable` **with their reason on the page**, so a
Cross opens an honest page instead of doing nothing: BD Data Management needs a Blu-ray drive, Search for
Media Servers needs the console's DLNA client (`dlna_plugin`), Video Editor & Uploader is the console's
own scene editor (`scenefolder_plugin`), Playlists is `playlist_plugin` (this build's queue is the folder
a track was started from), and Photo Gallery is the console's 3D gallery application.

Every skipped query is logged with its reason (`XMB_XMBML_LOG=1`), 13 for Video, 9 for Music and 6 for
Photo - a provider that has nothing attached produces no row on the console either.

Measured column counts from the build's own dump:

```
categories user:3 settings:16 photo:6 music:4 video:6 tv:1 game:11 network:5 psn:3 friends:3
photo    dlna-scan|gallery|playlists|library|choose|options
music    dlna-scan|playlists|library|choose
video    bddata|dlna-scan|video-editor|continue|library|choose
```

`out/data1/sheet-media-columns.png` pairs the console frame with this build for all three columns.

### 15.4 Item 3 - the Game-row options menu: five rows, two groups, and the flags the view needs

The settled 4.93 frame (`frames/menu-options-open.png`, re-measured this pass) is the Game column's row
menu: **Sort By** and **Group Content** at ink 301-318 / 341-358, a right-pointing chevron at
x 1573..1583 on both rows, the small purple square after Group Content at x 1517..1529 y 345..358, then a
gap, then **Start**, **Delete**, **Information** at 539-560 / 581-598 / 621-638. This build offered two
rows (Start, Information) and no group at all.

`Shell::buildOptions()` now builds that row set for the Game-row class (a row whose action launches a
title: installed, imported or Steam), and only for that class - the media and Continue Watching menus keep
their measured row sets (H2):

| # | row | key | command | flags | condition |
|---:|---|---|---|---|---|
| 0 | Sort By | `msg_sort` | `SortMenu` (the existing level) | submenu chevron | `game-row-list` |
| 1 | Group Content | `msg_folder_class` | `GroupMenu` (the level below) | submenu chevron + gate badge | `game-row-list` |
| 2 | Start | `msg_game_start` | `Activate` (launches the title) | - (first row of group 1: `dividerBefore`) | `game-title` |
| 3 | Delete | `msg_del` | `Delete` | - | `game-title`, **disabled** with its reason |
| 4 | Information | `msg_info` | `Information` | - | `always-last` |

- **Group Content** opens the level the console's own submenu frame shows
  (`menu-submenu-group-content.png`: `By Format`, `By Album`, `All` for that console's list). The keys a
  list can offer depend on the list, so this build offers the one key its rows carry - **By Title**
  (`msg_title_folder`), which groups a page into first-letter folders - and **All** (`msg_all`), which puts
  the ungrouped rows back. The seven other `*_folder` keys (album, artist, genre, format, month, year,
  track, release year) need metadata no row on these pages has, so they are not listed and the omission is
  logged. Grouping is real, reversible and needs no rescan: `Page::groupedSource` keeps the rows.
- **Start** is the item's own command (it launches the title).
- **Delete** is listed (the firmware's row 4 is unconditional) and cannot be chosen: this build has no
  delete backend for a title, and its note says which owner has the files. That is the panel's existing
  disabled-row rule, and Cross on it plays `snd_system_ng`.
- **Sort By** opens the existing level. On a title list - which has no file to read a timestamp from -
  the four date rows are now listed **disabled with the reason** instead of silently reordering nothing
  (they stay enabled on a local folder page, unchanged).

What the build writes (grep target):

```
shell: options opened category= game depth= 2 entry= steam:2807960 rows= 5 Sort By|Group Content|Start|Delete|Information
shell: options groups entry= steam:2807960 groups= 2 break= Group Content -> Start
shell: options flags entry= steam:2807960 submenu= Sort By|Group Content gateBadge= Group Content
shell: options group menu opened rows= 2 labels= By Title|All groups=1 parentGroups= 2 current= all
shell: group content by title page="All Titles" rows= 282 groups=26 keys=3,7,A,B,...,Z
```

Measured row bands, native against that frame (same crops, same threshold, ink > 140 over x 1330..1760):

| row | console | native | delta |
|---|---|---|---|
| Sort By | 301..318 (+ 321..322 descender) | 302..320 (+ 322..323) | +1.5 px at the centre |
| Group Content | 341..358 | 342..358 (the band above it is contaminated by the ribbon) | +1 px |
| Start | 539..560 | 539..559 | 0 |
| Delete | 581..598 | 581..598 | 0 |
| Information | 621..638 | 621..639 | +0.5 px |

The two utility rows land on the frame's 40 px pitch above the 238 px gap the view's `kOptionsBand`
already implements (`groupGap`), i.e. this pass supplies the row data that band was waiting for. The
chevron and the badge are **not drawn yet**: the view's call site does not pass the submenu vector and has
no badge pass, so both are returned as hunks in 15.8 - the shell half (which rows carry which flag) is
data and is logged above.

### 15.5 Item 4 - the Theme page: four rows, and the two pickers moved into the lists

The console's Theme page is `Theme`, `Color`, `Background`, `Font` and stops at 749 (SETTINGS 5.6: the
frame "shows exactly these four, in this order"). This build carried six, with `Select Theme` and
`Select Wallpaper` at 820/891.

**Decision: removed from the page and moved into the two choice lists**, which is where the firmware
itself folds those functions (SETTINGS 5.6 / D11: the four Theme choice pages are empty in the RCO and
built at run time). The `Theme` list's own first item is **`Install`** - the console's own label for
loading a theme, already modelled in the capture table as a disabled row - and it now opens the .p3t file
dialog; the `Background` list gains **`Select Wallpaper`** as its last item (after `Brightness`,
`Original`, `Classic`, `Wallpaper`), which opens the image dialog. Both are items of an existing band, so
the page has no extra row and neither function is lost.

Proof (`out/data1/sheet-theme.png`: console `page-settings-theme` beside the build):

```
settings: theme page from the RCO row set: 4 rows, 0 native-only
```

### 15.6 Item 5 - the tenth column: a preference, default ON, that gives the console's nine

`category_tv.xml` is the firmware's own answer: the TV column's root view lists nothing but content
providers, so a console with nothing registered draws **nine** columns and this one draws ten, shifting
every icon after Video by one 200 px cell (FIDELITY-ROOT-BAR C1). The Jellyfin surface and imported
channel apps live in that column, so it stays the default.

`appearance/tvColumn` (bool) now decides it. `Shell::tvColumnVisible()` reads, in order: the
`XMB_TV_COLUMN` environment override (the capture switch, and the only form available before the store
can hold the key - see 15.8's preference hunk), the stored value, then **ON** for an unset key.
`Shell::syncTvColumn()` is called at start-up and from `applyPreferences()`, and hides or restores the
category by erasing/inserting that one entry, so the bar, the navigation clamps and the entry dump all
follow `categories_` with no view change.

Both states captured (`out/data1/root.png`, `root-tvoff.png`, `sheet-tv.png`):

| state | categories | bar at the Game column |
|---|---|---|
| `appearance/tvColumn` on (default) | 10: user,settings,photo,music,video,**tv**,game,network,psn,friends | Video 355, TV 565, Game 765 |
| off (console parity) | **9**: user,settings,photo,music,video,game,network,psn,friends | **Music 155, Video 355, Game 565, Network 775, PSN 975, Friends 1175** - the console's own six visible icons |

The parity state's category row is also the measurement that closes the shift: at the Video column the
`category icon row` crop scores **18.60 dB / 0.8668** with the column shown and **21.96 dB / 0.9426** with
it hidden, i.e. the 200 px misalignment is worth 3.36 dB and the parity state clears the 20 dB / 0.900
budget that the tenth column fails.

The setting is documented here and in the source: **turn `appearance/tvColumn` off for console parity,
at the cost of the Jellyfin and channel surface, which lives in that column.**

### 15.7 Item 6 - the returned hunk, and what is still owed

The render pass returned two `shell.cpp` hunks (14.10). **A - the `*User` marker - is applied**: the
Vita, both Saved Data rows and the Trophy row now draw the firmware's own `*User` at the measured slot
(`out/data1/sheet-vita-label.png`: console above, build below, both `*User` under the title at x 682).
The active profile name it replaced is gone from that line.

**B - the two network rows' page class - is not applied.** It belongs to the dialogs/behaviour domain
(the two rows must raise the firmware's D4 message page instead of the D5 report), and applying it would
move the `page-network-test-error` / `page-network-connection-settings` reference frames that the
behaviour wave just re-based. It stays returned, unchanged, at 14.10 B.

### 15.8 Returned hunks (files outside this pass's fence)

Three files: `view.cpp` (four hunks), `view.h` (none - the flags ride on `MenuOption` in `shell.h`, which
this pass owns) and `preferences.cpp` (one hunk, for the TV preference's store key).

**V1 - the item menu's own chevron column.** The frame puts the submenu chevron at x 1573..1583 on the
item menu, while the settings band's chevron is measured at 1786..1796; the band needs its own column.

```diff
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@ struct PanelBand {
     double labelInset=16.;    // row text left = x0 + labelInset
     double groupGap=0.;       // fixed rows: distance up to a divider group's origin
+    // The submenu chevron's left edge. Two measured columns: the settings bands' 1786 (the Background
+    // list's `Brightness` row) and the item menu's 1573 (frames/menu-options-open.png, on both of its
+    // submenu rows). 11x10 px, on the row's centre line, both sides.
+    double chevronX=1786.;
     int groupBreakRow=0;      // fixed rows: rows before this index are the upper group (a band has no MenuOption)
@@ const PanelBand kOptionsBand=.firmware=true,.x0=1320.,.fadeEnd=1742.,.pitch=40.5,.fixedRows=true,
-    .rowCentre=549.5,.labelInset=14.,.groupGap=238.,.contentDim=.545,.stops=kOptionsBandStops,
+    .rowCentre=549.5,.labelInset=14.,.groupGap=238.,.chevronX=1573.,.contentDim=.545,.stops=kOptionsBandStops,
@@
             if(firmwareBand && submenus && submenus->value(i,false)) {
@@
-                p.save();p.setBrush(QColor(255,255,255,enabled?225:115));p.setPen(Qt::NoPen);
-                QPolygonF chevron;chevron<<QPointF(1786.,y-5.)<<QPointF(1786.,y+5.)<<QPointF(1796.,y);
+                p.save();p.setBrush(QColor(255,255,255,enabled?225:115));p.setPen(Qt::NoPen);
+                const double chevronX=firmwareBand?band->chevronX:1786.;
+                QPolygonF chevron;chevron<<QPointF(chevronX,y-5.)<<QPointF(chevronX,y+5.)<<QPointF(chevronX+10.,y);
                 p.drawPolygon(chevron);p.restore();
             }
```

**V2 - the purple gate badge after a row's label.** Measured on `menu-options-open.png`: 13x14 px at
x 1517..1529, y 345..358, i.e. 14 px after the label's ink (`Group Content` ends at 1503). Fill
`#BF4EA6` (191,78,166), a dark inner rect `#32172A` (50,23,42), and a slightly lighter lower band
(127,53,109). The label's own advance is measured with the same family/size the label is drawn at.

```diff
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@
             } else {
                 text(rect,title,23,QColor(255,255,255,level));
             }
+            if(rows && (*rows)[i].gateBadge && !chip) {
+                // The item menu's second utility row carries the small purple square the frame shows after its
+                // label (frames/menu-options-open.png: 13x14 at x 1517..1529, y 345..358, 14 px after the ink of
+                // "Group Content"). It is the firmware's gaiji U+F882 in msg_folder_class_with_icon, which the
+                // text path would draw as a missing-glyph box, so the shell names the row with msg_folder_class and
+                // the badge is drawn here: fill #BF4EA6, a #32172A inner rect and a lighter lower band.
+                QFont badgeFont(shell_.fontFamily());badgeFont.setPixelSize(23);
+                const double labelRight=labelX+QFontMetrics(badgeFont).horizontalAdvance(title);
+                const QRectF badge(labelRight+14.,y-7.,13.,14.);
+                p.save();p.setPen(Qt::NoPen);
+                p.setBrush(QColor(191,78,166));p.drawRoundedRect(badge,2.,2.);
+                p.setBrush(QColor(50,23,42));p.drawRect(badge.adjusted(3.,3.,-3.,-3.));
+                p.setBrush(QColor(127,53,109));p.drawRect(QRectF(badge.left()+3.,badge.bottom()-4.,badge.width()-6.,2.));
+                p.restore();
+            }
             if(firmwareBand && submenus && submenus->value(i,false)) {
```

**V3 - the item menu's call site passes the rows' flags.** `MenuOption::submenu` is data the shell
already sets; without this the chevron never draws.

```diff
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@
             if(optionsDrawn() && !optionsShown_.player) {
                 // Phase 11.3: the item menu is the firmware's own band (ITEM-MENU-INFO-FIRMWARE E1/F6): its
                 // measured edge, 38 px pitch, fixed rows and 1334 text column, and its MenuOption rows so a
                 // row this build cannot honour is dimmed with its reason. No dot: the band marks by focus.
+                // The two list-level rows carry the frame's own flags (FIDELITY-SURFACES 15.4): a right-pointing
+                // chevron on Sort By and Group Content, and the purple gate badge on Group Content.
+                QVector<bool> submenus;submenus.reserve(optionsShown_.rows.size());
+                for(const auto& row:optionsShown_.rows)submenus<<row.submenu;
                 drawOptionsPanel(optionsShown_.titles,optionsPanelEase_.value,optionsOpenEase_.value,1.-optionsOpenEase_.value,
-                                 optionsShown_.heading,&optionsShown_.rows,&kOptionsBand);
+                                 optionsShown_.heading,&optionsShown_.rows,&kOptionsBand,nullptr,nullptr,nullptr,
+                                 &submenus);
             }
```

**V4 - the Photo column's `Photo Gallery` row is an icon, not a wide tile.** The view's wide-tile rule
treats every `container == "file"` row as an ICON0 card (155..320 px wide). `hakoniwa.png` is a square
180x180 picture, so a focused gallery would draw 180x180 where the console's glyph is about 105 px tall.
`Entry::iconSized` is set on that row by the shell (a one-field addition to `shell.h`, this pass's file).

```diff
--- a/native-xmb/src/view.cpp
+++ b/native-xmb/src/view.cpp
@@
-                const bool tile=entry.gameTile || itemTexture.contains("album") || entry.container=="file"; // ICON0 thumbnails are wide
+                // ICON0 thumbnails are wide; a picture that is not (the Photo column's 180x180 hakoniwa.png
+                // gallery icon, Entry::iconSized) keeps the icon box the console gives it.
+                const bool tile=!entry.iconSized && (entry.gameTile || itemTexture.contains("album") || entry.container=="file");
```

**P1 - the TV preference's store key.** `appearance/tvColumn` is a bool; `Preferences::validBare` rejects
any key it does not name, so until this hunk lands the key cannot be stored and only `XMB_TV_COLUMN`
reaches the switch. (`Shell::tvColumnVisible()` already treats an absent key as ON, so nothing else
changes when the hunk lands.)

```diff
--- a/native-xmb/src/preferences.cpp
+++ b/native-xmb/src/preferences.cpp
@@
     if(key=="appearance/colour")return integer(0,5);
     if(key=="appearance/background" || key=="appearance/font" || key=="clock/dateFormat")return integer(0,2);
+    // The tenth column's switch (FIDELITY-SURFACES 15.6): off draws the console's nine categories.
+    if(key=="appearance/tvColumn")return value.metaType().id()==QMetaType::Bool;
     if(key=="clock/hourFormat" || key=="photo/fit")return integer(0,1);
```

### 15.9 What the parity catalogue must follow (not edited here)

`work/parity-catalogue.json` and `work/ab-layers.thresholds.json` are outside this pass's fence. The
states whose asserted data moved:

| state / assertion | was | becomes |
|---|---|---|
| `page-settings-theme` `entryCount` | 6 | **4** |
| `page-settings-theme` log | `settings: theme page from the RCO row set: 6 rows, 2 native-only` | `...: 4 rows, 0 native-only` |
| `theme-list` `entryCount` | 1 | **2** (the page now pushes `Install` as well as `Original`) |
| `background-list` `entryCount` | 4 | **5** (`Select Wallpaper`) |
| `background-list` log | `view: settings choice band appearance/background -> 4 items from capture` | `-> 5 items from capture` |
| `column-video` `catalogueHas` | `["continue","library"]` | unchanged (both rows still exist, now at index 3 and 4) |
| new ids to add | - | `column-video`: `bddata`, `dlna-scan`, `video-editor`; `column-music`: `dlna-scan`, `playlists`; `column-photo`: `dlna-scan`, `gallery`, `playlists` |
| states that would move if `appearance/tvColumn` were captured off | `column-tv` has no reference and no rows | a `column-tv` state cannot be captured with the column hidden; the parity capture keeps the default (on) |
| `options` states | `video-options-panel` asserts `rows= 5 Sort By|Open|Copy|Delete|Information` | unchanged - the media-row menu is deliberately untouched |

Row ids added and removed inside columns (for a catalogue that lists them):
`video`: added `bddata`, `dlna-scan`, `video-editor`; removed `options` (Video Settings, reachable from
the Settings column). `music`: added `dlna-scan`, `playlists`; removed `options` (Music Settings, same
reason). `photo`: added `dlna-scan`, `gallery`, `playlists`; kept `library`, `choose`, `options`.

### 15.10 Verification

**Build.** `native-xmb/build.ps1 -OutputDirectory C:\dev\out\native-data1` - clean, then re-built after
15.7's hunk landed.

**Canonical gate** (`bun work/ab-layers.ts --capture --exe C:/dev/out/native-data1/re-ps3-native.exe`):

| layer | PSNR | floor | delta | SSIM | floor | delta | gate |
|---|---:|---:|---:|---:|---:|---:|---|
| frame | 18.01 | 19.21 | **-1.20** | 0.8404 | 0.8475 | -0.0071 | **FAIL** |
| top background | 21.06 | 21.06 | +0.00 | 0.9434 | 0.9434 | +0.0000 | PASS |
| category icon row | 21.62 | 21.62 | +0.00 | 0.9062 | 0.9062 | +0.0000 | PASS |
| item label row | 12.06 | 12.06 | +0.00 | 0.4171 | 0.4171 | +0.0000 | PASS |
| clock text | 11.52 | 11.52 | +0.00 | 0.4662 | 0.4662 | +0.0000 | PASS |
| info bar | 15.15 | 15.15 | +0.00 | 0.7738 | 0.7738 | +0.0000 | PASS |
| text presence | 1703 bright px (minimum 900) | - | - | - | - | - | PASS |

**5 of 6 layers PASS and every one of the five that covers a surface of this pass is byte-identical
(+0.00).** The frame layer's -1.20 dB is the row-icon column and is attributed in 15.2: the pre-pass build
scores 18.91 on the same floor, pasting its icon column into this capture recovers 18.90, and the stored
reference's Game column holds one row against this build's ten. No floor was lowered and nothing was
re-baselined.

**Determinism.** Two captures with `--render-clock 20 --background-clock 2026-10-29T12:00:00 --ui-clock
2026-10-29T12:04:00 --focus-time 500` and the token list `full,wait,wait,wait` are byte-identical:
SHA256 `1F18834DCA407D0638C567154EE16D57C3FFE1FE17386995E275447200719E4C` both times.

**Parity** (`bun work/parity.ts --capture --score --state root,column-video,column-music,column-photo,column-tv`):

```
captured 5/5 cell(s)
parity scorecard: 0/36 capturable states pass A+B+C
  Tier A 5 pass, Tier B 0 pass, Tier C 0 pass
  1 captured cell(s) with no reference yet; 4 gating cell failure(s)

state         class             A               B     C  claim
root          icon-heavy     PASS            fail   n/a  A
column-photo  icon-heavy     PASS            fail   n/a  A
column-music  icon-heavy     PASS            fail   n/a  A
column-video  icon-heavy     PASS            fail   n/a  A
column-tv     icon-heavy     PASS    no-reference   n/a  A
```

Tier A (the dumps and the log assertions) passes on all five, which is the gate this pass's data
changes answer to. The Tier B numbers:

| cell | region | PSNR | SSIM | budget |
|---|---|---:|---:|---|
| root | frame | 18.08 | 0.8196 | 20.00 / 0.900 FAIL |
| root | top background | 23.71 | 0.9756 | PASS |
| root | category icon row | 20.74 | 0.9069 | PASS |
| root | item label row | 11.06 | 0.3956 | FAIL, annotated content-differs |
| root | clock text | 11.59 | 0.5026 | FAIL, not gating |
| root | info bar | 14.52 | 0.6195 | FAIL |
| column-video | frame / category icon row / item label row | 18.06 / 18.41 / 10.69 | 0.8316 / 0.8604 / 0.4059 | FAIL / FAIL / content-differs |
| column-music | same three | 17.99 / 18.31 / 10.79 | 0.8363 / 0.8615 / 0.3723 | FAIL / FAIL / content-differs |
| column-photo | same three | 17.88 / 18.91 / 10.63 | 0.8322 / 0.8746 / 0.3652 | FAIL / FAIL / content-differs |

The `category icon row` failures at the three media columns are the tenth column (15.6): with
`XMB_TV_COLUMN=0` the same crop at the Video column scores 21.96 / 0.9426 and passes. The `item label
row` is the console's own label against this build's different row text, which the tool already annotates
as a content difference rather than a defect.

**Row art, per crop** (`bun work/ab-compare.ts <console> <native> 440,430,230,150`, console frame first):

| row | pair | PSNR | SSIM |
|---|---|---:|---:|
| Vita | `col-game-r00` / `game-r01` | 6.40 | 0.5001 |
| Game Data | `col-game-r01` / `game-r02` | 6.30 | 0.4726 |
| Memory Card | `col-game-r02` / `game-r03` | 11.62 | 0.5414 |
| Saved Data (minis/PSP) | `col-game-r03` / `game-r05` | 11.82 | 0.6546 |
| Saved Data (PS3) | `col-game-r04` / `game-r06` | 10.75 | 0.6081 |

These numbers are **background-bound, not art-bound**, and are reported as such: the crop carries the
focus plate and the two sessions' ribbon phases, and the console frame's plate is absent where this
build's is drawn (a FIDELITY-ROOT-BAR item). The art's own evidence is 15.2's 1:1 crop and the binding
table, plus `out/data1/sheet-media-columns.png` for the three media columns and
`out/data1/sheet-theme.png` for the Theme page.

### 15.11 What this pass did not do

- Did not touch `view.cpp`/`view.h`; 15.8 is the whole drawing half. Until V1-V3 land, the item menu's
  chevron and gate badge are data only (`shell: options flags ...`) and the two rows look like plain text.
- Did not add a PS2 Saved Data row decision: the definition has `seg_sdps2` but this console's frame shows
  no PS2 row (its VFS has no PS2 device), and this build still lists `ps2` between `memory` and `psp`.
  The row keeps the definition's own art; whether it should be conditional is a content question this
  pass did not resolve.
- Did not reproduce the console's `seg_screenshot` row (this build keeps no console screenshot store) or
  `seg_now` (no XMB music player page): both are skipped with a logged reason.
- Did not re-baseline anything, and did not edit the catalogue or the thresholds file.
- Did not verify the .p3t and wallpaper pickers end to end: the band items are exercised only as far as
  `settings: theme picker row -> .p3t file dialog` in a scripted run, because the file dialog is modal and
  a scripted route through it would need a token the harness does not have.

### 15.12 The final merge pass (2026-10-29): what landed

Scope: the four 15.8 drawing hunks (V1-V4) plus the two hunks other waves returned that this pass also
owns - the `shell.h` busy accessors (DIALOGS-FIRMWARE 15.7) and the `shell.cpp` mojibake root fix
(FIDELITY-BEHAVIOUR 16.11; DIALOGS-FIRMWARE's 15.13 note). P1 (`preferences.cpp`) was already landed by
its own pass. Every added line is ASCII; `shell.cpp` is now pure ASCII (QChar code points), and the two
pre-existing comment em dashes in `view.cpp` are the only non-ASCII bytes left in the four files.

| hunk | landing point | state |
|---|---|---|
| V1 `PanelBand::chevronX` | `view.cpp` struct PanelBand (after `groupGap`, default 1786); `kOptionsBand` sets `.chevronX=1573.`; the draw uses `band->chevronX` | landed |
| V2 gate badge | `drawOptionsPanel`, after the label's `text()` and before the chevron: 13x14 at `labelRight+14`, fill `#BF4EA6`, inner `#32172A`, lower band (127,53,109) | landed |
| V3 submenu flags | `View::drawScene`, the item-menu call site: a `QVector<bool>` built from `MenuOption::submenu` and passed as the band's `submenus` argument | landed |
| V4 `Entry::iconSized` | `View::drawScene`, the wide-tile rule: `tile=!entry.iconSized && (entry.gameTile || albumArt || entry.container=="file")` | landed |
| busy accessors | `shell.h` beside `busy()`/`status()`, plus `busyProgress_`/`busySpinnerPhase_` (-1); `view.cpp` calls `drawBusySurface(p,shell_.busyProgress(),shell_.busySpinnerPhase())` | landed |
| mojibake | `shell.cpp`: the seven `U+00C2 U+00B7` pairs (app detail, file detail, the five `mediaQueueSummary()` suffixes) and the `U+2026` in "Refreshing Steam friends" are single code points built with `QChar(0x00B7)`/`QChar(0x2026)` | landed |

Measured on `C:\dev\out\native-final\re-ps3-native.exe` (built from this tree; the only build warnings
are the pre-existing ones in icons.cpp, rco_objects.cpp, settings_menu.cpp, view.cpp and main.cpp):

| check | result |
|---|---|
| canonical gate, default | **PASS 6/6** (5 pass, 1 informative); frame **18.84 / 0.8456** (-0.37 / -0.0019 against 19.21 / 0.8475); the other five layers +0.00; text presence 1703 bright px PASS |
| canonical gate, `--strict` | **PASS 6/6 layers within budget** - the frame is gated again at 18.84 against the unchanged 18.71 / 0.8435 floor. The tree already read 18.84 on the viewmerge pass's build (DIALOGS-FIRMWARE 15.13); the annotated 18.01 belongs to the earlier data-wave build before the album-tile fix, not to this merge, which does not move the canonical frame (next row) |
| determinism | two `--render-clock 20` captures, both sha256 `B80884308E06B8489662D14F14FEBB93B5D38E39E62A2D7E4697B57CBE101E08` - the same hash DIALOGS-FIRMWARE 15.13 measured on the same tree without these hunks, so the merge is frame-neutral on the canonical state |
| named states | `--states --save-reference` refreshed **12/12** references (pre-refresh scores 29.96-51.41 dB, three cells inf: those references predate the data and behaviour waves); then `--all` = stored pair PASS 6/6, canonical PASS 6/6, **states 12/12**, whole set PASS |
| V1/V2 on the Game-row menu | `options-game.png` (depth 2, entry `steam:2807960`, rows= 5): chevrons at **x 1573..1582** (Sort By y 307..315; console 1574..1583) and the badge at **x 1519..1531, y 345..358**, 13x14. Console badge 1517..1529: the 2 px is QFontMetrics advance vs ink. The pre-merge `out/data1/opts-game.png` measures 0 chevron ink and 0 purple px |
| busy page through the write route | ask -> `proceed page=busy text="Restoring default settings..."`; rule y199 **197.67 over 67.35 = 0.694** and y879 **190.71 over 47.21 = 0.691** (15.13's 0.694/0.695); the dismissed box's hint band x620..1120 y880..930 reads **0 bright px** (>=140); Back hint ink 1155..1239 x 897..916 |
| queue line, single U+00B7 | `player-shuffle.png` reads `Track 1 of 2 [U+00B7] Shuffle` with one 3 px dot run at x 254..256 (a doubled pair is two glyphs). The file-detail row is drawn by the view with no repair and reads `6 KB [U+00B7] 2026/10/26` (`music-library.png`); both are the shell's fixed strings |
| Photo Gallery icon | unfocused art **49x41 px** inside the 50 px icon box (`column-photo.png`); the pre-merge `out/data1/col-photo.png` reads **101x64** under the wide-tile rule. Focused (`column-photo-gallery-focus.png`) 83x106 |
| parity | Tier A 4/4 PASS. root frame 17.99 / 0.8266, top background 23.76 / 0.9784, category icon row 21.04 / 0.9133, item label 10.51 / 0.3752 (content-differs), clock 11.68 / 0.5538, info bar 14.73 / 0.7804; column-photo 17.34 / 0.8268, 19.12 / 0.8790, 10.50 / 0.4170 (content-differs); column-video 17.20 / 0.8240, 18.58 / 0.8664, 10.92 / 0.4885 (content-differs); options-panel frame **19.74 / 0.8867 PASS**, panel 19.65 / 0.8346 content-differs (B informative), Tier C PASS. Scorecard `out/parity/scorecards/scorecard-20261029-220343.md` |

The three gating B failures (root, column-photo, column-video frames and their category icon rows) are
the ones 15.10 already attributes: the tenth column (`XMB_TV_COLUMN=0` clears them) and the console's
own row set. V1-V4 do not reach those crops except the photo icon, which this pass moves toward the
console's glyph. The options-panel parity state still scores 19.65 dB as content-differs for a data
reason, not a missing flag: the state routes to the Video Library folder (`column:video >
catalogue:library[4]`, Tier A row "Movies"), so it draws the media row set Sort By/Open/Copy/Delete/
Information, while the stored console frame is the Game-row menu Sort By/Group Content/Start/Delete/
Information. Its frame region passes at 19.74 / 0.8867, i.e. the band's geometry is right and the row
text is what differs; the Game-row menu itself is the capture above, where both the chevron and the
badge measure at the console's columns.

Evidence: `C:\dev\out\final-merge\` (`captures\options-game.png`, `busy.png`, `player-shuffle.png`,
`music-library.png`, `column-photo.png`, `column-photo-gallery-focus.png`, `crop-gallery-before/after.png`,
the probe scripts) and the logs `final-merge-gate*.log`, `final-merge-det-*.log`,
`final-merge-states-save.log`, `final-merge-all.log`, `final-merge-parity.log`. Scratch only.

