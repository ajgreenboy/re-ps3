# UI-ELEMENT-GAP-AUDIT: the composited visuals of a 4.93 XMB frame, and what the native composites

Scope: every UI element that is neither an icon nor a behaviour - plates, panels, rules, bars, gradients,
glows, shadows, focus rings, scrollbars, badges, hints, counters, spinners, progress, masks and the
background/ambient treatments - accounted for one row per element, with the native's draw site and a
measured delta. Written 2026-10-28. Analysis only: no source, harness, reference or budget file was
changed, and the only file this pass wrote is this document. Icon identity and its bucket A/B/C rows are
`ICON-GAP-AUDIT.md`'s and are not repeated here; where an element's *art* is that audit's, the row says so
and scores the element's composite instead.

Evidence classes, per row and in this order where both apply: **T** = traced to firmware data (RCO
attribute, decoded draw constant, module literal), **M** = measured on a console frame or on the native
frame paired with it, **F** = fit (a measured value whose firmware provenance is not established),
**N** = native-only (this build draws it and no firmware owner exists).

## 0. Counts and method

| Measure | Count |
|---|---:|
| element rows | **71** |
| actionable gaps (P0-P3) | **49** |
| verified matching elements (no gap) | **22** |
| P0 - a whole composited layer absent on a reachable surface | **1** |
| P1 - present but measurably wrong, or absent from a surface the build draws | **13** |
| P2 - present with a measured small delta, or unmeasured | **21** |
| P3 - fit / polish | **14** |
| rows whose delta is a crop PSNR/SSIM | **16** |
| rows whose delta is a measured geometry or pixel value (no crop) | **40** |
| rows with no crop yet | **15** |

Per section:

| Section | Rows | Gaps |
|---|---:|---:|
| 1 Root frame | 17 | 11 |
| 2 Open pages | 8 | 3 |
| 3 Options menu and bands | 10 | 7 |
| 4 Dialogs and message surfaces | 9 | 5 |
| 5 Settings pages and bands | 7 | 6 |
| 6 Media surfaces | 7 | 7 |
| 7 HDR / pipeline level | 7 | 7 |
| 8 Text-as-graphic | 6 | 3 |

### Method, and one naming correction

The draw code was read at the line cited, not recalled. One correction to the brief's function list:
`view.cpp` has **no** `drawInfoBar`, `drawOptionsPanel`, `drawColumns` or `drawPageChrome` *functions*.
The mapping is:

| Brief's name | What exists |
|---|---|
| `drawScene` | `View::drawScene` (`view.cpp:1373-2159`), a 787-line function holding the whole XMB frame |
| `drawColumns` | a lambda inside `drawScene` (`view.cpp:1708-1939`) |
| `drawOptionsPanel` | a lambda inside `drawScene` (`view.cpp:1404-1546`) |
| `drawCategory` | `View::drawCategory` (`view.cpp:1080-1113`), the opaque-glyph fallback only |
| `drawInfoBar` | an inline block (`view.cpp:1979-2054`), not a function |
| `drawPageChrome` | `View::drawPageChrome` in `dialogs.cpp:178-195` - message pages only |
| notification | `View::drawNotification` (`view.cpp:1036-1079`) |

Frame measurements marked "**M (this pass)**" were taken on the paired frames of run
`out/parity/run-20261028-160444` (native executable `C:/dev/out/native-box2/re-ps3-native.exe`, pins
render 30 s / UI 2026-10-25T12:32 / focus 2000 ms) and the references the same run scored
(`out/parity-references/<state>__original__day__1080p.png`), read with Pillow: Rec.709 luma, block means,
row/column profiles, ink bounding boxes. Crop values come from
`out/parity/scorecards/scorecard-20261028-161558.md`, the last run that scored the full 35-state set.

Two caveats that bound every row. (1) The native reference pair is one build; the P3 hunks that landed
after it (label em 32, ink .455, pen 60) are in the source and are recorded as landed if the source says
so, but the scorecard numbers below belong to the earlier build and are quoted as such. (2) The 0.0594
background residue (section 7) adds **+16 to +19 luma to every element on every frame**; it is counted
once in H2 and is *not* counted again in each row's delta, because the row's delta is the element's own
construction.

Priority vocabulary: **P0** a whole composited layer absent on a reachable surface; **P1** present but
measurably wrong, or an element the console composites and this build does not; **P2** a measured small
delta or an unmeasured element; **P3** fit/polish. Priority "none" = verified match, kept as evidence.

## 1. Root frame

Root reference geometry, all **M**: category row y 280/290, focus column x 565, pitch 200+10, quads
117.13/167.328, brightness .698/1.0; item focus row 505, pitch 80; info bar 1329,75 591x58; clock glyph
32x32 at centre (1789.5,103.5).

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| R1 | Ribbon sheet: 16,384 clip positions of `lines1.fpo`; console bbox x -592..2399, y 413..769, crest 672 px (right end) and 543 px (left end) [T/M] | `ribbon.cpp` cloth from `LINE1.mnu` + a 14,400-step warm-up from an invented plane start (`ribbon.cpp:131-161`); the sheet is drawn and clipped correctly, its **state** is a fit [F] | crop `ribbon band` **16.14 dB**; shipped sheet x -680..2195 y 353..674 (440/511 crest - opposite slope); with `--vsh-modules` preset-1 tables x -575..2383 y 419..768, within 4 px y / 11 px x of the console [C] | P1 |
| R2 | Ribbon brightness: the emissive band, 7 lines, `+= mix(tint, .92, ...) * ribbon * ribbonLight` | same shader (`background_` fragment, `view.cpp:314-319`); night map energy 1.216x day, correlation 0.9998 [M] | **M (this pass)**: at x 1000 the console's ribbon peaks 130.8 (y 680) = 1.63x its top band; the native reads 222.3 at y 440 = 2.27x its top band, i.e. the native's band is ~39% brighter relative to the background [G] | P1 |
| R3 | Background day/night/month palette: `moyou/back_colours0.fpo`, 395 instructions, 143 literal slots, 4 month palettes (64x32 DDS) | exact Cg->GLSL translation, `renderOriginalBackground` (`view.cpp:555-571`), palettes from `qgl/lines` [T] | crop `top background` 0,0,700,200 = **23.76 / 0.9784 PASS**; the console's own microcode reproduces the native's scene to 0.24-0.42/255 (max 0.75), so no instruction is mistranslated [C] | none |
| R4 | Month phase `_MonthTime`: console 23.2258 at 10-25 10:00, i.e. `(day-1)/daysInMonth*30` exactly [T] | `b.month=(date.day()-1+t)/date.daysInMonth()*30` (`view.cpp:504`) still carries a time-of-day term `+t` [F] | 0.012/0.016/0.010 scene units = **3.7/255 display** (PIPELINE 4th pass 3); one-line hunk, not applied [G] | P3 |
| R5 | Top fade: the background's vertical day profile, brightest at the top | same program's `dayProfile` (`view.cpp:307`) [T] | **M (this pass)** console x20..90: 70.8 at y0 -> 52.4 at y480 (drop 18.4); native 87.5 -> 68.8 (drop 18.7): shape reproduced, offset is H2 [G] | none |
| R6 | Bottom band: the dark lower third, no separate fade object | shader's `nightProfile`/day tail only [T] | **M (this pass)** console 46.1..48.5 over y 800..1080, flat within 2.4; native 63.3..65.6, flat: no bottom-fade element exists in either and none is missing [G] | none |
| R7 | Corner/edge vignette: question whether the console darkens the frame corners | no vignette is drawn (`drawScene` has no corner layer) [T] | **M (this pass)** 40x40 blocks, ref/nat: TL 69.9/86.5, TR 64.6/81.8, BL 47.6/64.7, BR 50.5/67.2 - the corner spread is the background field (top vs bottom), identical in both; no vignette to add [G] | none |
| R8 | Category band glow: whether the focused category icon carries a halo | glass icon + quad only; no glow pass around the category row [T] | **M (this pass)** 10 px outside the focused quad (x 500): console 75.1 over bg 73.1 (+2.0); native 91.4 over 89.1 (+2.3) = the quad's own antialiased edge, no wide glow on either [G] | none |
| R9 | Faint neighbour treatment (settled): unfocused category icons at 117.13 quad, bar material .698 | glass icon at `material=barBrightness` .698 and quad 117.13 (`view.cpp:1654-1691`) [T] | **M (this pass)** neighbour peak over local background: console 148.9-73.1 = **+75.8**; native 181.3-89.1 = **+92.2** (**+22%**), on the `category icon row` crop 20.71 / 0.9143 PASS [G] | P2 |
| R10 | Faint row during the open/close move: bar recedes 2700 -> 2483.53, pitch 200 -> 217.4, y 290 -> 268.21, `_Ambiant.w` .698 -> .067 [T] | the recede model is implemented (`view.cpp:1637-1668`); the presence term is linear in `childAmount` [F] | no crop yet - no transition reference exists in the corpus; endpoints traced, only the curve between them is a fit [N] | P2 |
| R11 | Focused icon's own presentation: quad 167.328, brightness 1.0, no separate glow object [T] | `IconRenderer` with the real distance (`view.cpp:1679`) [T] | crop `category icon row` **20.71 / 0.9143 PASS**; root frame 18.23 / 0.8263 FAIL on the background, not this row [C] | none |
| R12 | Info bar plate: `xmb_plugin_normal:tex_indi_bg` 24x24, 8 px nine-slice, measured visible core 1329,75 591x58; interior 4 luma darker than the surround, white ramps top/bottom/left, no right edge [T/M] | nine-slice at the measured rect, `adjusted(-1,-1,8,1)`, inset 8 (`view.cpp:2006-2018`) [T] | crop `info bar` **14.68 / 0.7725 FAIL** (budget 17.0/0.75); **M (this pass)** edge alphas: top console 0.253 / native 0.241, bottom console 0.352 / native **0.279** (21% weak), left 0.247/0.233; right edge absent in both [C] | P1 |
| R13 | Indicator slot row: five 32x32 slots at -244/-274/-342/-376/-410 from the clock ink edge, `online_num` at +14/+2, RCO size 10 [T] | only the online slot and a 15 px counter (`view.cpp:2036-2053`); icon identity is ICON-GAP C2-C5 [T/F] | **M (this pass)** the slot pitch is not measurable on any reference (the console is signed out); the counter's 15 px is a fit inside a 32 px slot; 4 of 5 slots are empty by lack of state, not by omission [G] | P2 |
| R14 | Clock plate face: 32x32 at (1789.5,103.5), ring stroke ~3 px, **dark disc fill inside the ring** (interior 1.5-20 luma against a 67 plate interior), hands ~13/10, 1.5 px drop shadow [M] | `drawEllipse(c,14,14)` pen 2.7, no brush, 1.5 px black shadow (`view.cpp:2020-2027`) [M] | **M (this pass)** native bbox 30x30 (1776..1805 x 89..118), stroke 1.5-2 px, interior = plate interior (75-116): ring 2 px small, stroke 1 px thin, **the dark disc is not drawn at all**; crop `clock text` 11.61 / 0.5425 (non-gating) [C] | P1 |
| R15 | Clock text slot: 27 px, right-aligned, ink right edge 1760, cap 22 px [M] | `text({1240,82,525,45},...,27,AlignRight)` (`view.cpp:2019`) [F] | **M (this pass)** right edge 1765 vs 1760 (-5 px), ink 3.5% wide at 27 px vs the measured 22 px cap [G] | P2 |
| R16 | Category title: 21 px centred, ink x 536..593 (centre 564.5), y 342..358 [M] | `text({x-140,330,280,40},cat.title,21,AlignCenter)` (`view.cpp:1689`) [F] | 0 x; **-5 y** (ink vs box) - GEOMETRY-ROOT 4.1 #12, unchanged [G] | P3 |
| R17 | Status line - no firmware owner: the console routes transient text through the notification system, not a line at y 850 | `text({680,850,1100,50},shell_.status(),26)` (`view.cpp:1978`) [N] | no crop yet; a native-only element on every frame where `Shell::status()` is non-empty [N] | P2 |

## 2. Open pages

What a page *is* in firmware data: `expage_explore_bg/list/fg` and `expage_menu` are `Page` objects with
**no geometry and no image ref** (GEOMETRY-ROOT 2.2, RCO-OBJECT-MODEL); the page's visible furniture is
paf text and the two option-menu boxes. So most of this section's answer is "there is no such object",
and the rows that matter are the ones the console does draw: the message-page rules, the XMB column's
focus plate, and the back chevron.

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| P1 | Page backdrop / glass: no RCO plane with a rect or image; the page area is the background itself | nothing is drawn for a list page [T] | **M (this pass)** `page-settings-system`, x 400..1400: y 60..240 is a smooth 88.0 -> 75.5 with no step; x 380..490, y 470..560 reads the background (53-57). No plate, border or fill exists to draw [G] | none |
| P2 | Header rules: 1 px full-width, D4 page y 198 (alpha .218) and y 872 (.201), table page y 199/879 (alpha .36) [M] | flat 1 px rects, `kRuleTop/kRuleBottom` = 198/872, alpha 56/51; table page 199/879 alpha 92 (`dialogs.cpp:26-33,60-61`) [M] | crop `title rule` (80,150,1520,60) **16.44 / 0.6905 FAIL**; crop `back hint` 11.44 / 0.5197 FAIL on the same page; the D4 family's bottom rule is at 872 vs the table page's measured 879 [C] | P1 |
| P3 | Scroll / row backgrounds: the console's list page has **no** row plates at rest (12.6: 3-5 luma from x 405 to x 1300 across the focused row) | no row background is drawn [T] | **M (this pass)** `page-settings-system` y 470..560 x 405..1300: 2.4 luma of variation, the focused row marked by text only: matches [G] | none |
| P4 | Focused-row highlight, bar vs glow: the XMB **column** plate `BG1` is 326x140 at x 405..731, y ~420..560, soft vertical gradient, peak ~126-136 luma at y 520 [M] | no plate; the item row draws its icon + a breathing text glow (`view.cpp:1910-1912`) [M] | **M (this pass)** root, x 405..731: console plate mean 85.8 against 68.9 on the row above (+16.9 lift); native 114.1 against 85.6 (the difference is the ribbon crossing the box, not a plate: no `fillRect` exists in the column loop). The plate is absent; crop `item label row` 11.16 / 0.4023 (`contentDiffers`) [C] | P1 |
| P5 | Left triangle / back chevron: console ink 433..445 x 498..510 (13x13, 100 px above luma 150) [M] | `QChar(0x2039)` at 42 px in `{kColumnX-142,480,50,50}`, AlignCentre (`view.cpp:1935-1937`) [M] | **M (this pass)** native ink 415..479 x 480..522 (65x43, 604 px above 150): **4.6x the console's ink area**, 52 px wider and 18 px higher, on the same surface [G] | P2 |
| P6 | Page icon: the page's own category icon at x 303, y 280, quad 167.328 [T] | the focused category is drawn at `pageX = 565 - 262*childAmount` (`view.cpp:1645`) [T] | crop `category icon row` (page states) **22.73 / 0.9593 PASS** (budget 0.84 non-gating) [C] | none |
| P7 | Vertical rules: whether a divider runs between the label column and the value column | none drawn; none in firmware data [T] | **M (this pass)** column-mean scan of `page-settings-system` y 380..820: the only persistent deviation is x 1302 (the value column's first glyph stem); no rule exists to draw [G] | none |
| P8 | Page bottom line: whether a list page closes with a rule | none drawn [T] | **M (this pass)** y 840..1080 x 400..1400 on three page states: no full-width step; the only bottom rules are the message pages' (P2) [G] | none |

## 3. Options menu and bands

One drawing serves the item menu and the settings choice list (`PanelBand`, `view.cpp:1140-1250`). The
firmware's own band asset `system_plugin:tex_optionmenu_bg` (536x216) is unused (ICON-GAP B5); this
section scores the composited band.

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| O1 | Band fill: full-height vertical gradient, left edge 1320 (1 px step, then 34), peak 62 alpha at 1366-1388, geometric zero 1742, page behind at .545 [M] | 21 measured stops in `QLinearGradient` (`view.cpp:1178-1181,1439-1442`); `kOptionsBand.x0=1320` [M] | crop `options panel` (1300,430,620,300) **19.73 / 0.8339 FAIL** (`contentDiffers`); **M (this pass)** left step at y 100: console 77.9 -> 85.6 (+7.7), native 95.9 -> 107.7 (+11.8, 53% stronger); peak lift over the far-right background: console +41.9, native +50.6 (+21%) [C] | P1 |
| O2 | Row focus: `BG1` plate per menu level (RCO declares one per level, zero geometry) [T] | text glow only, no plate (`view.cpp:1488`) [F] | no crop yet - the plate is not separable inside the band on any local frame (ITEM-MENU G4) [N] | P2 |
| O3 | Dividers / group break: not a rule, a gap: rows 301-323 and 341-362 above, 539-560/581-598/621-638 below [M] | `groupGap=237`, `groupBreakRow` from the shell's `dividerBefore` (`view.cpp:1461-1479`) [T] | **M (this pass)** row 0 centre 315 = 552-237, ink 301-323: matches the measured band; no divider line exists to draw [G] | none |
| O4 | Value column: x 1300, width 440, 21 px, suppressed while a band is up (measured value max 2.9 luma) [T/M] | `text({1300+ahead*210+offset,...},21)` and `(1.-2*panelAmount)` fade (`view.cpp:1925`) [T] | `page-open.png` "Off" ink 1301..1326 vs the rect at 1300 (**+1 px**); GEOMETRY-ROOT 4.2 #36 EXACT [G] | none |
| O5 | Hint rows / note line: no firmware owner - the console's band has no note line | 16 px row note under the list, 15 px disabled note (`view.cpp:1524-1544`) [N] | no crop yet; retained deliberately (the disabled reason is this build's only way to explain a refused row) [G] | P3 |
| O6 | Panel entrance treatment: open starts +115..150 ms after the key, ramps 245-255 ms, at rest +360..410 ms; close starts +9 ms, ramps 200 ms [M] | one `PafEase` at 200 ms from the key, no start delay (`view.h:48`, `view.cpp:707-712`) [F] | the native is **160-210 ms short on the open** and has no delay; close is right on duration and lacks the 9 ms start. Crop O1's 19.73 includes this only in burst captures, not stills [G] | P1 |
| O7 | Row text size: em ~24 px, cap 18 px, 11.0 px/char measured [M] | 23 px (`view.cpp:1515`) [F] | ~1 px small: cap ~16.6 vs 18, `After 5 Minutes` ~8 px short (SETTINGS 15.3) [G] | P3 |
| O8 | Submenu chevron: 11x10 triangle at x 1786-1796, y +-5 [M] | hand-built `QPolygonF` at 1786, y+-5 (`view.cpp:1517-1522`) [M] | matches the measurement; the asset (`tex_optionmenu_*`, ICON-GAP B6) is unused, the geometry is not a gap [G] | none |
| O9 | Heading: no firmware owner (a nested level is a 128x272 box, not a heading) | 19 px heading above the list (`view.cpp:1454-1458`) [N] | no crop yet; the nested-level box (ITEM-MENU I.1) is not drawn at all - a separate gap, no frame exists [G] | P3 |
| O10 | Text/plate alpha split: on close the row ink drops below 180 within one frame (<9 ms) while the plate ramps ~200 ms [M] | one alpha for both (`p.setOpacity(alpha)` at `view.cpp:1425`) [F] | measured rule not implemented; no crop yet of the split (G3's burst is 1:1 crops, not a scored still) [G] | P3 |

## 4. Dialogs and message surfaces

There is no dialog RCO: `msgdialog_plugin.rco` holds two objects and no `mbox_*`. The plate, rules, hint
row and fades are assembled by `vsh.elf` (DIALOGS-FIRMWARE 1-2), so every number here is a frame
measurement.

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| D1 | Box plate: 319x176 at (480,452), flat white 43/255 wash (measured as `(255-bg)*0.17`), no nine-slice [M] | `fillRect(plate, QColor(255,255,255,43))` (`dialogs.cpp:276-281`) [M] | crop: the box's **direct crop 18.12 / 0.8420** (FIRMWARE-PARITY-GAPS M5) - over the 18 dB budget, 0.038 SSIM short. The catalogue's `dialog` cell (16.59 / 0.7605) does **not** measure the box: its native route captures the inline empty-state line (see 10.X4b), which is also why the box is absent from that native frame. **M (this pass)** the console's plate fill is 91-97 over a 65-72 surround, matching the 0.17 wash [C] | P1 |
| D2 | Rules: two flat full-width 1 px rules (198 alpha 56 / 872 alpha 51) and the box footer at 879 alpha 177, uniform x 20..1900 [M] | flat rects, `kRuleTop/kRuleBottom/kBoxFooterRuleY` (`dialogs.cpp:32-33,120-124,187-192,302-306`) [M] | crop `title rule` **16.44 / 0.6905 FAIL**; the 960 px centre-bright `tex_msgdialog_bar` is correctly not used (ICON-GAP B4) [C] | P1 |
| D3 | Footer band: a full-width band under the box carrying the rule and the X/O hints [M] | rule at 879 alpha 177 + hint pair at 897 (`dialogs.cpp:353-361`) [M] | **M (this pass)** the console's rule is one row at y 879 measuring 193.6 over a 55.0 row mean (alpha **0.69**), flat x 100..1800; the native's captured `dialog` frame carries no rule at 879 (72.0 flat) because that frame draws the inline line, not a box - so the box footer has **no native still in this run** to compare with [G] | none |
| D4 | Dim: claimed absent - DIALOGS-FIRMWARE's two measured frames keep background, ribbon, particles and info bar at full luminance [M] | no dim is drawn (the 135-alpha wash was removed; `dialogs.cpp:11-13`) [M] | **M (this pass)** the box frame's top band (x 20..300, y 30..90) reads **73.6** against the root's **73.8**: no dim behind the box (0.3%). But the same patch on `page-network-connection-settings` and `page-network-test-error` reads **51.8 / 51.9** (a 0.70 factor) while `page-network-status-list` reads 73.5 - see 10.X8 [G] | none |
| D5 | Hint row: 22 px glyphs from the 30x30 `tex_cross`/`tex_circle`, label 8 px right, group gap 22 px, 100-unit fade [M/T] | `drawHintRow`, `{x,inkTop,22,22}` (`dialogs.cpp:466-480`) [M] | x 726/762 and 1155/1192 anchored to the measured pairs; within glyph bearing [G] | none |
| D6 | Container bar/plate objects: `tex_common_dialog` + `_shadow`, `tex_operation_guide_base` + `_shadow`, `tex_line` - the common-dialog plate and the operation guide's plate have no drawn counterpart [T] | 0 references [T] | no crop yet - no local frame of the common dialog or an operation guide [N] | P3 |
| D7 | Notification plaque: `tex_notification` 64x64 nine-slice inset 16, plaque 400x58 (one line) / 400x84 (two), right edge at 1896, top 145 [F] | nine-slice + a 48 px slide (`view.cpp:1036-1079`) [F] | no crop yet - the state has no firmware reference (Tier B `no-reference`); the rect, the 48 px slide and the 22/18 px text are all fits [N] | P2 |
| D8 | Error page settle: body rises 24 px into place over a 150-unit animation [T] | `dialogSettleEase_` at 24 px (`dialogs.cpp:370-374`) [T] | present; the settle is a transition, so no still can score it [G] | none |
| D9 | OSK plate and key faces (bonus row): measured key cells 66x50 at a 4-stop face gradient, prediction header, footer [M] | procedural 4-stop faces per role (`OskFace` at `view.cpp:3060-3067`, `drawOsk` at `view.cpp:3181-3464`) [F] | crop `osk panel` (480,280,960,520) **21.19 / 0.7700 FAIL** - the PSNR clears 18 and the SSIM is 0.11 short; the faces are ICON-GAP B9's measured-override [C] | P2 |

## 5. Settings pages and bands

The band's fill is the same shape as the options band, translated (SETTINGS 15.1). What differs per page
is the **origin** and the **row grid**, and both are measured per list.

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| S1 | Band fill and dim: 420 px profile, 64 px translation between the 1260 and 1324 families; page behind at .40 (measured .404/.408/.421, the .48-.60 samples are mid-ramp) [M] | three `PanelBand` sets with the measured stops and `contentDim=.40` (`view.cpp:1170-1223`); the dim is applied to the page's columns only (`view.cpp:1728-1735`) [M] | crops: `band column` theme **19.99 / 0.8745 FAIL**, color 20.72 / 0.8896 PASS, background 20.72 / 0.8933 PASS, font 20.92 / 0.8959 PASS (budget 18/0.88) [C] | P2 |
| S2 | Panel origins: left edge 1260 (Screensaver/Date/Time Format) or 1324 (Theme family) or 1320 (item menu); row origins 418/458/498/261+501/381+541/531, pitch 40, rows fixed [M] | encoded per list (`kChoiceBand`, `kThemeBand`, `kBackgroundBand`, `kThemeLowerListBand`) [M] | **M (this pass)** the 1320 step is confirmed on `options-panel` (77.9 -> 85.6 at x 1320), settling ITEM-MENU G2's 1330 in favour of SETTINGS 15.1's 1320; the 3 px grid phase (18 vs 21 mod 40) is still unexplained [G] | P2 |
| S3 | Band rows: 40 px pitch, cap 18 px, no heading, no scroll; the value block's gaps are 240 px (Theme) and 160 px (Background) [M] | `pitch=40`, `groupGap=240/160`, `fixedRows` (`view.cpp:1206-1223`) [T] | crops `band rows`: theme **17.26 / 0.7518**, background **17.24 / 0.6931**, font **17.67 / 0.6629** (all `contentDiffers`, budget 18/0.88) [C] | P2 |
| S4 | Disabled-state treatment: the firmware has no disabled chip or dimmed row convention - a choice page's items are all live [T] | level 115, halo x.45, chip x.40 opacity, note line (`view.cpp:1488-1512,1535-1544`) [N] | no crop yet; native-only by construction, and the honesty rule (never a fabricated value) is why it exists [G] | P3 |
| S5 | Chips/swatches: 27x28 px at the row's text column, measured fill, 1 px outline when focused; 12 unlabelled chips on the month order [M] | `drawRoundedRect` chip + 1 px focused outline (`view.cpp:1496-1513`); 12 chip items in `settings_menu.cpp:61-77` [M] | crop `chip column` (1324,470,420,520) **21.23 / 0.8565 FAIL** (`contentDiffers`: the scorecard's note says "six labelled colours", the source draws 12 unlabelled chips - see 10.X3) [C] | P2 |
| S6 | Group gaps: Theme 261 -> 501 = 6 x 40 = **240**; Background 381 -> 541 = 4 x 40 = **160** [M] | `groupGap=240` / `160` (`view.cpp:1211,1222`) [T] | exact; SETTINGS 15.2's correction of 239 -> 240 and 548 -> 545 is in the tree [G] | none |
| S7 | Disabled-note line: no firmware counterpart (the console never draws a reason under a list) | 15 px line under the list (`view.cpp:1535-1544`) [N] | no crop yet; native-only, kept for the seven months whose tint table has no value [G] | P3 |

## 6. Media surfaces

All media states are `no-reference` in the current scorecard (Tier B), so every delta here is geometry
against firmware data rather than a scored crop.

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| M1 | Video player chrome: 25-button 13x3 matrix over a time/chapter page, 74x74 status planes, button `_shadow`/`_focus` triples [T] | no buttons; a text band (title 970, queue 895, ASCII hints) plus 6 `tex_cp_help_*` icons (`view.cpp:1579-1600,2428-2469`) [F] | no crop yet; the panel's plate and its 25 button faces have no counterpart, and the hint strip's 6 slots at a 283 px pitch are a fit [N] | P1 |
| M2 | Status plane row: `play/prev/next/pause/stop/searchb/searchf` 74x74, repeat/shuffle 98x74, "1" badge 72x36, all 10 with `alphaScale=0` until published [T] | play/pause + repeat/shuffle + "1" only; 5 planes undrawn (`view.cpp:2521-2546`) [T] | no crop yet; the undrawn five are ICON-GAP C12's decision ("rather than fabricated"), so the row records the element's state, not a new gap [N] | P2 |
| M3 | Video progress/seek bar: Progress stack base/playable/playing (no rect), `tex_progress_bar` 30x20 + `_in`, jump marker 20x12, 8+8 digit cells 16x16 [T] | nine-slice 30x20 at `{110,941,1700,20}` (FIT rect), jump marker, 19 px per 16 px cell (`view.cpp:2370-2419`) [F] | no crop yet; the rect, the cell size and the marker's semantics are recorded fits (the console's own rect is runtime) [N] | P2 |
| M4 | Audio player's own progress page (`audioplayer_plugin` `play_progress_page`): a 200x6-class bar with per-cell time [T] | a flat 4 px white/60 track at y 941 plus one `MM:SS / MM:SS` string (`view.cpp:1605-1609`) [F] | no crop yet; the page is not rebuilt, so the strongest statement is that the element is a native simplification of a firmware page [N] | P2 |
| M5 | Music control panel: `tex_cp_panel` 72x72 plate at scale 1.3, 7 buttons 90x60 sharing `tex_cp_focus`, status 74x74, progress 200x6, plate nine-slice [T] | exactly that, nine-sliced at inset 20, anchor centred x 960 with its bottom at y 1000 (FIT), plate grows 1.0 -> 1.3 on the 150 ms fade (`view.cpp:2477-2571`) [T/F] | no crop yet; every object size and texture is the container's, the anchor and the 12 px padding are fits [N] | P2 |
| M6 | Photo viewer frame: `Pv_front_screen_page` / `Pv_back_screen_page` each hold a **1920x1080** plane with a 1x1 stretchable texture - a full-screen colour treatment - plus 12x12 thumbnail frames and the 86x30 `tex_dki_*` hint strip [T] | nothing: no screen plane, no thumbnail frame, no `tex_dki_*`; the photo viewer draws the image plus an ASCII hint line and a "Zoom %" line (`view.cpp:1588-1594`) [M] | no crop yet - and this is the only P0: a whole composited layer (the screen planes) and the thumbnail frames are absent on a surface the build reaches [N] | P0 |
| M7 | Volume HUD: one paf `PhLevelMeter`, 20x40 segments over a 48x48 backing, no caption, no percentage, no plate, no border [T] | 10 segments at `{x,1008,200,40}`, backing nine-sliced inset 16, mute drawn unlit, no caption (`view.cpp:2290-2349`) [T/F] | no crop yet; the segment count (10) and the rect are spec-only fits (MEDIA-PANELS 5.4), the art and the 150 ms fade are the firmware's [N] | P2 |

## 7. HDR / pipeline level

This chain has been measured by three passes and is cited, not re-derived: PIPELINE's second (residue
magnitude and state dependence), third (the excess is in the scene before `Hdr::finish`; the firmware's
final composite is `ToneApplyDisplay`, not `ApplyGlare`) and fourth (the console's own microcode at the
console's own constants reproduces the native's value; the residue is an achromatic -0.059 applied to the
background layer's output).

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| H1 | Glare/tone/dither chain: `ToneMap` -> `GlareSourcePre` -> threshold -> 15-tap Gaussian per mip -> `AccGlare` -> `ToneApplyDisplay` (glare x weight **minus noise/8**, the dither) [T/M] | `ToneMap`, `GlareSourceNoTone`, `Gaussian`, `AccGlare`, `ApplyGlare` (`hdr.cpp:43-217`); `ApplyGlare` is the same sum **without the dither** and samples/replaces instead of adding [M] | the day excess is already in the scene before `Hdr::finish` and the frame equals `ToneMap(scene)` to 0.0/255 inside the window, so **no chain term can own it**; projected through the tone curve the post-tone-map divergence is 1-2/255, an order below the dither's 16/255 [G] | P2 |
| H2 | The +0.0594 background residue: achromatic, +0.0594/+0.0590/+0.0591 linear scene at day, +0.002 at night, flat in x and y [M] | not fitted into `hdr.cpp` (deliberately: a subtraction would be the constant the first pass refused) [F] | display **+17.8/+17.4/+16.9** at the console's month phase; **M (this pass)** per element: top band +17.7, y 480 +16.4, y 1000 +17.2, category row +17.0, info-bar interior +17.4, options band peak +21%; closes with the console's 64x32 surface dump [G] | P1 |
| H3 | UI inside the glare chain: the console blooms its UI through the same chain; the native composites the UI after the tone map | `XMB_UI_GLARE=<scale>` hook exists, off by default (`view.cpp:463-475`) [M] | A/B measured: clock SSIM 0.4105 (off) -> 0.4015 at 0.15 -> 0.3451 at 1.0; item label row +0.0007/+0.0009. The post-tone-map composite wins at every strength [G] | P3 |
| H4 | UI composite itself: 2x layer, 4x MSAA, resolved then bilinear-sampled at half scale (`view.cpp:779-867`) - the mask/edge treatment for every element | implemented [M] | **M (this pass)** the info-bar plate's bottom ramp is 21% weaker in alpha and its top ramp 5% weaker than the console's, at the same rect: a half-scale resolve is a candidate (thin ramps are averaged 2x2), not proven [G] | P2 |
| H5 | Particles: 1536-slot pool, 48-byte stride, 0.0088883 s fixed step, one draw into the HDR scene and a second into the glare source (`particles_second`) [T] | both passes ported (`particles.cpp:200-280,806-863`) [F] | no crop yet; four recorded fits (0.12 defocus span, 1.15 size gain, 0.85 defocus dim, a depth-fade stand-in for 6-B3) and the `o[10].w` alpha chain is carried in intensity rather than alpha [N] | P3 |
| H6 | Ribbon energy at night: 1.216x the day map, uniformly, correlation 0.9998 over 258,456 overlapping pixels [M] | the same shader and the same map [T] | measured energy ratio is the console's; geometry identical (PIPELINE 5th pass 2) [G] | P3 |
| H7 | Ambient overlays with no firmware owner: a 35/255 black wash over a wallpaper (`view.cpp:1552`) and a 0.55 dim for the screensaver (`view.cpp:868-873`) | drawn [N] | no crop yet; the screensaver dim is labelled a native approximation in the source and has no console measurement [N] | P3 |

## 8. Text-as-graphic

| # | element (composition; measured geometry) | native implementation (code, status) | delta measure | pri |
|---|---|---|---|---|
| T1 | Text shadow: firmware `Text` 1304/1305 carry shadow (4,-5), RGB .25, alpha 1; the console's **clock** band bottoms at 30.9 against a 66.4 background (0.47 multiply); the console's **item labels carry none** [M] | the material is read (`text_renderer.cpp:105-120`) and the multiply branch is shipped with `GL_DST_COLOR/GL_ZERO` (`text_renderer.cpp:652,711`), but it is **inert**: swapping its blend state gives a byte-identical frame (sha256 73AF46A7...ED6) [M] | **M (this pass)** the native's clock band bottoms at 83.6 against 85.6 - no shadow at all; the multiply contributes 0 pixels in both the per-tile and the union-mask path [G] | P1 |
| T2 | Focused-row label bloom: the console's label sits in a halo several pixels wide [M]; MaXMB records a 3.4 s ease from ~0.35 of the peak | `glow()` at 32 px, strength `(focus-.5)*2*breath` (`view.cpp:1906-1911`); renderer halo sigma 2.2, radius 6 [F] | no crop yet: the glow's shape and period are unmeasured on the console (the label-row residual is glow *shape*, not glyph shape - TEXT-RENDERER 7.4) [N] | P2 |
| T3 | Label ink level and em: console unfocused ink 0.442-0.449 (mean 0.446) across 12 rows in 3 states; em 27 -> 32 corrected | `.455+.545*focus` at 32 px (`view.cpp:1885,1890-1912`) [M] | matched-string crop **22.81 / 0.9457** (from 14.85 / 0.4813); ink now ~1.5 luma high on the console [C] | none |
| T4 | Label pen: level 0 at +113, level 1 at +60 (three columns: 678/622 measured) [M] | `level==0?(tile?173:113):60` (`view.cpp:1884`) [M] | 0 delta on the ten glyph starts at the corrected em; level-0 artwork rows still use the 173 proxy (card width is 20 px wide) [G] | none |
| T5 | Elision as a graphic: the firmware positions a separate `label2_text_ellipsis` Text after the last visible character [T] | `elideToWidth` prefix + a separate ellipsis draw at `elided.ellipsisX` (`view.cpp:1897-1915`) [T] | both call sites inert in the canonical state (no label overflows), so no crop can score it [G] | none |
| T6 | Marker/indicator row: the console's band marks the current row by focus only - no check, no dot (SETTINGS U4); the item menu has one `BG1` per level [T] | no dot on a band; the player panel's 4.5 px dot is hand-drawn (`view.cpp:1491-1493`, ICON-GAP B16); `entry.disabled` dims to .5/.55 [N/T] | no crop yet for the dot; the band's no-dot rule matches the frames [G] | P3 |

## 9. Top 20 element gaps

Ranked by how much of a frame each changes and whether a user of this build can reach the state today.

| Rank | Element | Console composites | This build composites | Evidence |
|---:|---|---|---|---|
| 1 | Photo viewer frame | two 1920x1080 screen planes with 1x1 stretchable textures, 12x12 thumbnail frames, an 86x30 hint strip | the image, an ASCII hint sentence, a "Zoom %" line | M6 |
| 2 | Clock face fill and stroke | 32x32 ring, ~3 px stroke, **dark disc inside**, hands 13/10 | 30x30 ring, 1.5-2 px stroke, no disc, hands 10/7 | R14 |
| 3 | XMB item focus plate (`BG1`) | 326x140 plate at 405..731 with a soft gradient, peak 126-136 luma | icon + breathing text glow, no plate | P4 |
| 4 | Ribbon sheet state | crest 672/543, bbox y 413..769, single sagging band | crest 440/511, bbox y 353..674, opposite slope | R1 |
| 5 | Ribbon brightness | 1.63x its top band, night energy 1.216x day | 2.27x its top band (+39%) | R2 |
| 6 | Text shadow on the clock | 0.47 multiply shadow under the clock band | none (the multiply branch is inert) | T1 |
| 7 | Message-page header rules | flat 1 px rules at 198/872, alpha .22/.20; table page 199/879 alpha .36 | flat rects at 198/872 and 199/879, alpha 56/51/92 | P2, D2 |
| 8 | Background residue | -0.059 in linear scene units, achromatic, day-state | +0.0594 high, flat; every element inherits +16..19 luma | H2 |
| 9 | Info bar plate edge ramps | 0.35 bottom / 0.25 top white alpha at the measured rect | 0.28 bottom / 0.24 top (21% / 5% weak) | R12 |
| 10 | Options band fill and left step | 1320 step +7.7, peak lift +41.9 over the far background | 1320 step +11.8, peak lift +50.6 (+21%) | O1 |
| 11 | Options panel entrance | +115..150 ms delay, 245-255 ms ramp (360-410 to rest); close 9 + 200 | 200 ms from the key, no delay | O6 |
| 12 | Box plate and rules | 319x176 white 43/255 wash, two flat rules, footer rule at 879 alpha 177 | the same by measurement, box crop 18.12/0.8420 | D1, D2 |
| 13 | Video player chrome | 25-button matrix, 74x74 status planes, button shadow/focus triples | a text band and 6 hint icons | M1 |
| 14 | Settings band column (Theme) | one shape, 1324 border, 64 px translated family | same stops, crop 19.99/0.8745 (0.0055 SSIM short) | S1 |
| 15 | Neighbour category brightness | neighbour peak +75.8 over its background | +92.2 (+22%) | R9 |
| 16 | Back chevron | 13x13 ink at 433..445 x 498..510 | 65x43 ink at 415..479 x 480..522 | P5 |
| 17 | Clock text slot | right edge 1760, 22 px cap | 1765, 27 px em (3.5% wide) | R15 |
| 18 | Options band row type | em ~24, cap 18 | 23 px (cap ~16.6) | O7 |
| 19 | Colour chips | 12 unlabelled chips, 27x28 at the row's text column | 12 chips, crop 21.23/0.8565 | S5 |
| 20 | Notification plaque | 64x64 nine-slice, rect unmeasured | a fitted rect, a 48 px slide, no reference | D7 |

## 10. Claims in the existing audits this pass could not reproduce

| # | Existing statement | What this pass found | What would settle it |
|---|---|---|---|
| X1 | `ICON-GAP-AUDIT.md` C13: "unidentified left-pointing triangle at (385,455) ... **not drawn**" (seen on `display-page-rest.png`, `theme-list.png`) | **Half contradicted.** On all three page references (`page-settings-system`, `-network`, `-video`) the console draws **nothing** at (385,455): max luma 57.4-57.5 against a background of ~55. What the console does draw is the back chevron at ink 433..445 x 498..510, and the native **does** draw it (`view.cpp:1936`), 4.6x too large. So "not drawn on every open page" is not reproducible on the parity corpus, and the drawn one is a size error, not an omission | one frame of the state C13 measured; the (385,455) mark may belong to `theme-list.png`'s band state, where the parent column is drawn differently |
| X2 | `ICON-GAP-AUDIT.md` C14 and section 8: scrollbars absent, "no local frame shows a long enough list with the bar in view" | **Negative measurement now exists.** `page-settings-system` is the console's 25-row page (past its window) and its right edge x 1840..1919 is flat: column means 58.6-59.0 over y 380..900, i.e. no track, no knob, no arrows. The absence is not a visible gap on this surface; it may still be one on the 110-item time zone list, which no reference covers | a list longer than the console's page window, with the bar in view |
| X3 | `scorecard-20261028-161558.md`, `color-list` chip-column annotation: "the native list draws six labelled colours in its own panel" | **Not reproducible from source.** `settings_menu.cpp:61-77` defines **12 unlabelled chips** (empty labels, `swatch=true`, measured month colours) and `view.cpp:1496-1513` draws each as a 27x28 swatch with a 1 px focused outline. Five months are writable, seven disabled with a note. The six-colour statement describes an earlier state | a re-run of the `color-list` cell on the current tree |
| X4 | `ITEM-MENU-INFO-FIRMWARE.md` G2: the options band's left edge is "**x = 1330**" | **Contradicted by measurement and by the later SETTINGS pass.** At y 100 the first lifted column is **1320** (77.9 at 1319 -> 85.6 at 1320 -> 96.6 at 1321), the same value SETTINGS 15.1 records; `kOptionsBand.x0` is 1320 and matches the frame | none needed; G2's `-open-check` still needs its own edge re-read if the 10 px matters elsewhere |
| X5 | `ICON-GAP-AUDIT.md` X1: the info-bar plate is "now" the nine-slice and the old invented rect is gone | **Confirmed, and extended.** The nine-slice is at `view.cpp:2006-2018`; but the composite is still measurably short of the console: bottom ramp alpha 0.279 vs 0.352, crop 14.68/0.7725 FAIL. "The plate landed" is true; "the plate matches" is not | a plate-only crop (the rect alone) scored at 1:1, plus a test of the half-scale UI resolve |
| X6 | `ITEM-MENU-INFO-FIRMWARE.md` B4 rows 5-7 (clock ring r=14 vs 16, stroke 1.3 px thin, "no indicator icons at all") | **Partly stale and partly worse.** `tex_indi_online` is drawn (`view.cpp:2036-2053`), so "no icons at all" is stale. The clock is still 2 px small and 1 px thin - and the measured console face also carries a **dark disc fill** that no audit records and the native does not draw | the disc's colour/alpha from a 1:1 crop; it is a 32 px circle and needs no more than one still |
| X7 | `GEOMETRY-ROOT.md` 4.2 #38: "back arrow `{kColumnX-142, 480, 50, 50}`, 42 px ... -7 x (glyph bearing)" | **Understated.** The measured console ink is 13x13, not a 42 px glyph: the rect is right but the glyph inside it is 3.2x too large in each dimension. The "-7 x" delta is the smaller half of the error | the same still used for the size measurement (already in the corpus) |
| X4b | `scorecard-20261028-161558.md`: the `dialog` cell carries **0** `contentDiffers` regions, i.e. it claims its two sides are the same surface | **Not reproducible.** The console reference is the delete-confirmation **box** (319x176 plate at 480,452, Yes/No, footer rule at 879); the native capture of that route draws the **inline empty-state line** - measured this pass: no plate (plate interior x 500 reads the background, 77.2), no rule at 879 (72.0 flat where the console reads 193.6). So the cell's 16.59 / 0.7605 compares a box with a line and should be annotated `contentDiffers`; the box's own crop is 18.12 / 0.8420 | a native route that opens a real `MessageKind::Box`, then the cell can be scored |
| X8 | `DIALOGS-FIRMWARE.md` 11-13 and section 2: a message surface draws **no** dim (two measured frames) | **True for the box, not established for the page class.** The box frame's top band reads 73.6 against the root's 73.8 (no dim), but `page-network-connection-settings` and `page-network-test-error` read 51.8 / 51.9 at the same patch - a **0.70 factor** - while `page-network-status-list` on the same day and clock reads 73.5. Two of the three message-page references are therefore either dimmed by their page class or captured in a different session state, and the frames cannot separate the two | one session capturing all three message pages plus the root, same clock, same pins |

## 11. What this pass did not do

* Did not build or run the native. Every native claim is a source read at the line cited, cross-checked
  against the 16:04 run's frames and the 16:15 scorecard.
* Did not score a new crop. The crop values are the scorecard's except the box's direct crop (FIRMWARE-PARITY-GAPS M5); the new numbers in this document are
  frame measurements (pillow, in memory) of the reference/native pairs the scorecard used.
* Did not measure the transition-only elements (R10, O2, O10) - no transition still exists in the scored
  corpus, and the burst captures are 1:1 crops, not states.
* Did not re-derive the glare/tone/dither chain; PIPELINE's second, third and fourth passes are cited.
* Did not touch any icon row: icon identity, the `bestImage` allow-list and the glyph fallbacks are
  ICON-GAP-AUDIT's.
* Did not write anything but this file.
