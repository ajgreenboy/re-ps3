# FIDELITY-ROOT-BAR: the root frame and the category bar, element by element

Scope: everything visible at depth 0 on the root page - the background field and its palette/fades, the
ribbon sheet and its particles, the nine-category bar (icons, glass material, spacing, labels, the
departing/entering move), the item column's row block, the info bar (plate, ramps, clock text, clock face,
indicator slots, PS-tap plate), the idle hint, and the frame's corner/edge treatments.

Read-only pass. Nothing was edited except this file: no source, no work tool, no budget, no reference
frame. Captures were written to `C:\dev\out\root-bar-audit\` (a scratch folder, not a reference set).

---

## 0. Counts

| Measure | Count |
|---|---:|
| elements audited | **49** |
| match (measured correct, or no element to draw on either side) | **26** |
| differ (present, measured delta) | **19** |
| absent (the console composites it, this build does not) | **3** |
| native-only (this build composites it, the console has no owner) | **1** |
| blocks the look | **5** |
| visible | **7** |
| subtle | **11** |

Per section:

| Section | Elements | Match | Differ | Absent | Native-only |
|---|---:|---:|---:|---:|---:|
| 1 Background field | 7 | 3 | 4 | 0 | 0 |
| 2 Ribbon and particles | 5 | 0 | 5 | 0 | 0 |
| 3 Category bar | 13 | 9 | 4 | 0 | 0 |
| 4 Item column | 13 | 8 | 3 | 1 | 1 |
| 5 Info bar | 10 | 6 | 3 | 1 | 0 |
| 6 Idle hint | 1 | 0 | 0 | 1 | 0 |

### The pair this pass measured

| side | file | why this one |
|---|---|---|
| console | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\col-game-r00.png` | the root page at `Game`, day preset, `Original` theme, 1920x1080, guest clock `29/10 12:04` |
| console (2nd) | `...\frames\col-game-idle-hint.png` | the only frame in the corpus with the `BG1` item plate fully clear of the ribbon (it is the `Corrupted Data` state, not an idle state - see 6.1) |
| console (3rd) | `C:\dev\out\rpcs3-gaps-2026-10-28\stills\info-indicator__original__day__1080p.png` | the only frame in the corpus carrying the PS-tap plate |
| native | `C:\dev\out\root-bar-audit\native-root-day-1080.png` | this build, run 2026-10-29 with the pinned recipe |

Native capture recipe (exactly the brief's, plus the two arguments the executable requires and the size
token that makes the grab 1:1):

```
re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <scratch> \
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-28T21:40:00 \
  --focus-time 2000 --no-intro --script "size=1920x1080,full,wait,wait,wait,grab=<png>,quit"
```

Two notes on the recipe. Without `--dev-flash` the executable prints its usage and exits (so the brief's
command line as written does nothing); without `size=1920x1080` the scripted grab is the window's
960x540, which `work/ab-compare.ts` will not score against a 1920x1080 reference. The build stamp is
`out/native-xmb/re-ps3-native.exe` 2026-10-29 18:33.

Measurement was done with a compiled `System.Drawing` luma reader (Rec.709) held in the tool session, plus
`bun work/ab-compare.ts <ref> <native> x,y,w,h` for PSNR/SSIM. Every number below is a measurement from
this session unless it is explicitly marked `[cited]`.

### The one thing that makes crop scores unfair here, said once

Three of this domain's crops are **content-bound** and must not be read as fidelity: the clock string is
`29/10 12:04` on the console and `10/28 9:40 PM` here (different length, different glyphs), the item
column's row set differs (console `PS Vita System Application Utility` first, this build `Steam Store`),
and the clock hands point to different minutes. Those crops are reported with `[contentDiffers]` and the
verdicts for those elements rest on the geometry measurements instead. This is the same trap
`UI-ELEMENT-GAP-AUDIT.md:257` records for the `dialog` cell.

---

## 1. Background field

Console evidence class: the palette is firmware data (`qgl/lines/lib/moyou/back_colours0.fpo`, 395
instructions); the vertical profile and the horizontal gradient are frame measurements.

| # | element | console (rect + measurement) | native now (code) | status | delta | sev |
|---|---|---|---|---|---|---|
| B1 | Day/night/month palette, `Original` | `M`: frame is a warm brown field; mid-left RGB at (40..80,520..560) = **116.6, 91.7, 57.4** | `renderOriginalBackground` (`view.cpp:561-577`) drives the translated Cg program; crop `top background` 0,0,700,200 = **21.61 dB / 0.9657** | **match** | RGB **+8.8,+9.2,+9.5** (constant offset, same hue) | - |
| B2 | Month phase `_MonthTime` | `T`: the console's captured constant at 10-25 10:00 is **23.2258** = `(25-1)/31*30`, day-quantised, no time-of-day term | `b.month=(date.day()-1+t)/date.daysInMonth()*30` still carries **`+t`** (`view.cpp:510`, with the comment at `:504-509` saying why it is kept) | **differ** | the `+t` term adds `t/31*30` = **+0.403 month-units** at 10:00 (console 26.129 vs build 26.532 at 2026-10-28; the same 0.403 at the 10-25 pin, 23.226 -> 23.629) = 1.34 % of the month | subtle |
| B3 | Top fade (vertical day profile) | `M`: x20..300 mean **69.90 at y0 -> 63.50 at y250**, drop 6.40 | same program's day profile; **90.90 -> 83.04**, drop 7.87 | **differ** | drop **+1.47 luma steeper (23 %)**; absolute field +21.0 at y0 | subtle |
| B4 | Bottom band (the dark lower third; no separate fade object) | `M`: x20..300 **49.94 at y780 -> 56.84 at y1075**, rise 6.90 | **64.09 -> 67.24**, rise 3.15 | **differ** | rise **-3.75 luma (46 % shallower)**; absolute +14.2 at y780 falling to +10.4 at the bottom edge | visible |
| B5 | Corner vignette | `M`: 40x40 blocks TL **66.61**, TR **66.75**, BL **54.79**, BR **54.72** - the corner spread is the top/bottom field, left vs right differ by **0.14** | native TL 86.49 / TR 81.79 / BL 64.67 / BR 67.18 - left vs right differ by **4.70** | **match** (no vignette on either) | no vignette element exists to add; the native's left-right asymmetry is 4.7 luma | - |
| B6 | Frame brightness (the achromatic residue) | `M`: top rows y0..16 x200..1700 = **84.33** flat; corners as B5 | **101.15** flat; corners as B5 | **differ** | **+16.8 luma** at the top rows, **+21.0** at y0 x20..300, **+10.4** at the bottom edge - not a constant in display space (a constant addition through the tone curve) | blocks the look |
| B7 | Frame edges: left / right / top border or bezel | `M`: column means y200..800 x0..39 = **66.89 -> 66.36** (flat, -0.53); x1880..1919 = **66.15 -> 66.88** (flat); row means x200..1700 y0..16 flat **84.33** | **74.53 -> 76.27** left, **77.94 -> 76.15** right, top flat **101.15** | **match** (no border, no bezel, no dark frame edge on either) | the 1.7 luma ramp across the native's left 40 px is the background's own gradient | - |

Notes. B3/B4/B6 are one physical cause seen three ways: the residue of B6 is not constant in display
units, so it is largest where the field is darkest (top of frame, +21) and smallest at the bottom (+10),
and the console's bottom tail rises faster than this build's. Treat a fix to B6 as also moving B3/B4; do
not tune three fades independently.

---

## 2. Ribbon sheet and particles

| # | element | console (rect + measurement) | native now (code) | status | delta | sev |
|---|---|---|---|---|---|---|
| R1 | Ribbon sheet extent and clip | `M`: the emissive band crosses the whole frame; crest found per column in clean windows (x 0..430 and 1520..1919, y330..860) | `ribbon.cpp` cloth from `LINE1.mnu` with a 14,400-step warm-up (`ribbon.cpp:131-161`), drawn at `view.cpp:428`; the sheet is drawn and clipped | **differ** | the band's y at the same x: see R2 | blocks the look |
| R2 | Crest curve (the sheet's own phase) | `M`: crest y = **555** (x0), **371** (x300), **361** (x400), **635** (x1520), **648** (x1700), **653** (x1900) - high in the left-middle, low at both ends | **568** (x0), **500** (x300), **486** (x400), **366** (x1520), **357** (x1700), **369** (x1900) - high at the right edge, low in the middle | **differ** | **x=1700: 648 -> 357 = -291 px; x=1900: 653 -> 369 = -284 px; x=300: 371 -> 500 = +129 px; x=400: 361 -> 486 = +125 px**. The slope is inverted on the right half (the shipped sheet sags right, the console's sags left) | blocks the look |
| R3 | Ribbon phase / start position | `M`: as R2; the console's left end is a 543 px crest and its right end a 672 px crest (`GRAPHICS-INVENTORY.md:60`, `[cited]`), i.e. the sheet is asymmetric | a 14,400-step warm-up from an invented plane start (`ribbon.cpp:131-161`) | **differ** | the phase is a fit, not traced; the 291 px crest error at x1700 is the visible size of it | blocks the look |
| R4 | Ribbon brightness / energy | `M`: crest peak luma **114.9** (x0), **118.8** (x90), **134.4** (x200), **104.4** (x400), **105.1** (x1520), **106.1** (x1700) | **144.9**, **174.8**, **157.1**, **171.0**, **210.3**, **187.4** | **differ** | **+30 to +105 luma at the crest** (+26 % at x0, +76 % at x1700); crop `ribbon band` 0,330,1920,450 = **16.32 dB / 0.7161**, `ribbon-right` 1250,330,670,450 = **18.06 / 0.7792** | blocks the look |
| R5 | Sparks / dust particles | `M`: `PARTICLES.md:708-712` `[cited]` - 106 specks, peak delta 171, neutral (32,30,33) on the 480x360 lower-left dust crop; my proxy count in x1300..1900 y780..1020: **0 specks** above local+20 | the same pool ported (`particles.cpp:200-280`); same crop: **14 specks** above local+20 in y780..860, **9** in y940..1020 | **differ** | native **1.7x the speck count** and **-28 peak delta** (143 vs 171, `PARTICLES.md:712`); the native's dust is more numerous and dimmer | subtle |

---

## 3. Category bar

Geometry anchors (all `[VC]`/`[RCO]` in `GEOMETRY-ROOT.md:319-331`, re-confirmed here on the frame):
focus column **x 565**, pitch **200 px** with **+-10** focus padding (neighbours at 355/775), unfocused row
y **290**, focused y **280**, quad **117.13** unfocused / **167.328** focused, material **0.698** / **1.0**.

| # | element | console (rect + measurement) | native now (code) | status | delta | sev |
|---|---|---|---|---|---|---|
| C1 | The nine console columns | `M`: at `Game` the row is **Music 155, Video 355, Game 565, Network 775, PSN 975, Friends 1175** - six visible, no TV column (`GRAPHICS-INVENTORY.md:46-50` `[cited]`: nine columns, `category_tv.xml` omits TV when no service is registered) | ten entries in `shell.cpp`; the TV column sits between Video and Game, so the row reads **Video 155, TV 365, Game 565, Network 765, PSN 965, Friends 1165** | **differ** | one whole extra icon cell; every icon left of the focus is one slot out of place (**Video at 355 -> 155**, and the console's Music column is off-screen here) | visible |
| C2 | Column pitch | `M`: neighbour ink centroids 152.5 / 355 / (565) / 774.5 / 973.5 / 1175.5 -> pitch **200.00** | 200 px (`view.cpp:1782`, `kColumnX=565` `view.h:370`) | **match** | 0 | - |
| C3 | Focus slot x | `M`: focused label centre **564.5**, focused icon centre 564.5 | 565 (`view.cpp:1782`) | **match** | -0.5 px | - |
| C4 | Icon art / visible size | `M`: at y290 (through the pad's body) the focused Game pad's ink runs **x 511..604**, and at y310 its two grip arms are **514..534** and **598..618** with background between; unfocused Network disc **64x64**, PSN **65x63**, Friends **71x71** | the same scans give the focused pad **x 511..604** and the arms **514..534 / 598..618**; Network **64x65**, PSN **60x62**, Friends **78x73** | **match** | focused pad edges **0 px**; Network **+1 px**; PSN **-5x-1** and Friends **+7x+2** are threshold noise on a low-contrast sphere and a multi-blob glyph (the pad body reads 176 console / 192 native against a +21 background) | - |
| C5 | Focused icon brightness | `M`: peak **246.0** over a **75.2** local background = lift **+170.8** | peak **249.0** over **96.0** = lift **+153.0** | **differ** | absolute peak **+3.0** (match); **lift -17.8 (-10 %)** because the background under it is +20.8 (that is B6, not this element) | subtle |
| C6 | Neighbour (unfocused) brightness | `M`: Network peak **176.0** over 80.2 = **+95.8**; PSN 200.6 over 81.9 = **+118.7**; Friends 188.0 over 79.8 = **+108.2** | Network 185.8 over 99.1 = **+86.7**; PSN 203.8 over 98.5 = **+105.3**; Friends 184.3 over 94.5 = **+89.8** | **differ** | absolute peaks **+4.3 to +9.8**; lift **-9.5 % to -17.0 %**. The `.698` material is exact; the loss is the residue behind it | subtle |
| C7 | Category label ink position | `M`: `Game` ink **x 536..593, y 342..358** (centre 564.5, 17 px cap) | **x 536..593, y 342..359** | **match** | 0 x, **+1 px** height | - |
| C8 | Only the focused column carries a label | `M`: no label under Music/Video/Network/PSN/Friends on `col-game-r00` | `if(i==shell_.category()) ... text(...)` plus the departing label (`view.cpp:1831-1833`) | **match** | - | - |
| C9 | Label font and size | `M`: 17 px ink cap, 58 px word width for `Game` | 21 px draw (`view.cpp:1831`), same font family | **match** | 17 vs 18 px ink cap; word width identical (536..593 both) | - |
| C10 | Icon row y | `M`: unfocused ink centred at y **290** (Network 258..321, Friends 254..324); the focused pad carries ink at y250, y290 and y310 in the same x band and none at y330, i.e. it spans **~y 245..325**, centred 285 | focused pad ink present at **y250, y290, y310**, none at **y330** - the same band and the same centre | **match** | 0 (the -10 focus lift is present on both) | - |
| C11 | Band glow / halo around the focused icon | `M`: 10 px outside the focused quad the console reads +2.0 over its background | no glow pass in the category loop (`view.cpp:1769-1835`) | **match** (neither has one) | - | - |
| C12 | The departing / entering category move | `M`: `TRANSITION-HARNESS.md:297` `[cited]` - console **198 ms**, icon track 715 -> 561 px, 83.6 fps burst | **165 ms**, track 725 -> 565 (`view.cpp:1782` `+200*distance*recede`, the 200 ms mode-5 ease at `view.h:48`) | **differ** | **-33 ms (-17 %)** duration; **p=0 delta +10 px**; time-aligned within **6 px from +66 ms** (the leftover is the capture window, `TRANSITION-HARNESS.md:302-306`) | subtle |
| C13 | Label cross-fade on the move | `M`: `TRANSITION-HARNESS`/`PAGE-MOVE 14.2` `[cited]` - outgoing .76/.58/.44/.33/.25/.19/.14, entering .31/.72/.83/.89, both riding their own icons | `categoryFade_` cross-fade, both labels drawn (`view.cpp:1831-1833`) | **match** | not re-measured (no console burst in this session); the model and the two draw sites are present | - |

---

## 4. Item column

Row grid (`view.cpp:1939-1954`, re-confirmed on the frame): focused row **y 505**, pitch **80 px**, the
first row below focus at **+175** (80 + 95), the row above at **-305** at level 0.

| # | element | console (rect + measurement) | native now (code) | status | delta | sev |
|---|---|---|---|---|---|---|
| I1 | Row block position | `M`: focused label ink **y 492..521** (row 505); `Game Data Utility` ink **y 667..698** (row 680); `Memory Card Utility` ink **y 743..778** (row 760). Pitch 80, +175 to the first row below | focused label ink **y 493..527**; `Game Data Utility` ink **y 827..858** (row 842); `Memory Card Utility` **y 907..938** (row 922) | **match** | rows land on the same y grid: row-to-row offsets agree to **0 px** (`+175`, `+80`, `+80`); only the *contents* are shifted one slot because the row sets differ | - |
| I2 | Focused row expansion (icon) | `M`: the focused folder's ink runs **x 500..628** at y500 (128 px wide) and **y 443..546** (104 tall) inside the firmware's **184.8 px** item quad; `GRAPHICS-INVENTORY.md:72` `[cited]` gives the same glyph as 472,443 -> 625,546 | `size = 60+44*focus = 104` px box (`view.cpp:1966`); the glass path carries the real 184.8 quad (`itemIconSize`, `view.cpp:1980`). The focused artwork occupies **x 536..590** at y500 (~50 px) | **differ** | box **104 vs 184.8 = -80.8 px (-44 %)**; and the **focus expansion is 1.73x** here (60 -> 104) against the console's **2.56x** measured on the same folder art (50 -> 128 px). The visible widths (128 vs 50) are not comparable because the console draws a folder and this build draws a shopping bag on that row | visible |
| I3 | Unfocused row icons | `M`: plain folder ink **50x35** at 540..589 x 662..696 for row 680 (peak 184.0) | folder ink **60x40** at 535..594 x 820..859 (peak 209.7) - the code's 60 px box (`view.cpp:1966`) against the firmware's 77.616 px quad | **differ** | **+10 wide / +5 tall (+20 % / +14 %)** - the opposite sign from I2, because the firmware's own art carries an inset inside its quad that this build's renderer does not reproduce. Neither element can be fixed by scaling the box alone. One wrong asset as well: the `PS Vita System Application Utility` row draws a **horizontal pale strip** where the console draws a folder-shaped glyph | visible |
| I4 | Label column x | `M`: label ink starts **677** for `Game Data Utility` (pen 678 per `GEOMETRY-ROOT.md:926` `[cited]`) | ink starts **680**; pen `kColumnX+113 = 678` (`view.cpp:2034`) | **match** | **+3 px** ink start (glyph bearing at the threshold), pen 0 | - |
| I5 | Label typography | `M`: `Game Data Utility` ink **282x32** (cap 32), `Memory Card Utility` 311x36 | **276x32**, 312x32 | **match** | width **-6 px (-2 %)**, cap 0 | - |
| I6 | Label ink level (unfocused) | `M`: same string, mean ink **134.8**, peak **141.7** | mean **146.0**, peak **156.7** | **match** | **+11.2 mean** - fully explained by the background residue showing through the alpha (`(1-.455)*+17 = +9.3`): the `.455` alpha (`view.cpp:2035`) is right | - |
| I7 | Subtitle / notes line | `M`: `*User` ink top **y 528** under the focused row | `Browse and install` ink top **y 532** (`text({labelX,y+20,...},17)`, `view.cpp:2078`) | **differ** | **+4 px low** | subtle |
| I8 | Value column (x 1300, 440 wide, 21 px) | `M`: not exercised on the Game column (no row carries a value) | `text({1300+ahead*210+offset,y-25,440,50},21)` (`view.cpp:2075`) | **match** (not exercised) | 0 (`GEOMETRY-ROOT.md:359` `[cited]` records +1 px on a page row) | - |
| I9 | Dimming of unfocused rows | `M`: unfocused labels at alpha .446 (I6) and **no icon dimming** - row icons keep full opacity; the console marks the row by focus only | label alpha `.455+.545*focus` (`view.cpp:2035`); icon alpha = `columnAlpha` only (`view.cpp:1990`, no focus term) | **match** | 0 construction delta | - |
| I10 | Focused-row `BG1` plate | `M` on `col-game-idle-hint.png`: a **319x175** light plate, left edge **x 405** (86 -> 114 at x 406), right edge **x 724** (98 -> 64 at x 726), top edge **y 419** (58.7 -> 90.4), bottom ~**y 594**; lift over the unplated row **+43 to +60 luma** at y 440..540 | **nothing** - the column loop draws icon, label, value, subtitle and note and no `fillRect` (`view.cpp:1934-2084`) | **absent** | the whole 319x175 plate is missing; it is the item column's only focus cue besides the text glow | visible |
| I11 | Row above focus at -305 | `M` on `col-game-idle-hint.png`: two rows visible above the focus, glyphs at y ~40 and ~120 | `-305` for level 0 (`view.cpp:1940`), clipped at `y<-100` (`:1954`) | **match** | 0 | - |
| I12 | Idle fade of the unfocused labels (after 4 s) | `M` on `info-indicator...png`: every unfocused row draws **icons only, no labels** - the idle state is real on the console | `idleFade=(resting-4000)/800` over `(1-focus)` (`view.cpp:2023-2026`) | **match** (construction) | curve and 4 s onset are a MaXMB fit; no local console burst measures the ramp | - |
| I13 | Status line at y 850 | `M`: no owner - the console routes transient text through the notification system, never a line at y 850 | `text({680,850,1100,50},shell_.status(),26)` (`view.cpp:2144`) | **native-only** | draws only when `Shell::status()` is non-empty; empty in this capture (0 ink) | subtle |

---

## 5. Info bar

| # | element | console (rect + measurement) | native now (code) | status | delta | sev |
|---|---|---|---|---|---|---|
| F1 | Plate rect | `M`: bright border line rows **y 75** and **y 132**, left border column **x 1329**, right border past x1919 (no right edge); interior 71.4 | `QRectF infoBar(1329,75,591,58)` nine-sliced at `adjusted(-1,-1,8,1)`, inset 8 (`view.cpp:2188-2191`) | **match** | 0 on the rect; crop `plate-only` 1332,77,240,55 = **22.74 dB / 0.9533** | - |
| F2 | Plate ramps (top / bottom white ramps) | `M`: top line y75 = **138.8** over a 76.05 background -> alpha **0.351**; bottom line y132 = **137.5** over 73.7 -> alpha **0.352**; ramp decays over ~6 rows (y76 120.1, y77 106.4, y78 95.2, -> y82 72.6) | y75 = **137.2** over 88.9 -> alpha **0.291**; y132 = **136.1** over 87.1 -> alpha **0.292**; decays y76 129.5 -> y82 84.6 | **differ** | **top -17 %, bottom -17 %** (both ramps equally weak); crop `plate-top-ramp` 26.41 / 0.9575, `plate-bottom-ramp` 26.21 / 0.9905 | visible |
| F3 | Plate interior wash | `M`: interior **71.4** against a **76.05** surround = **-4.65** | **84.2** against **88.9** = **-4.70** | **match** | **-0.05 luma** | - |
| F4 | Plate border / edge overshoot | `M`: nothing at y74 (**75.90**) or y133 (**73.83**) - the plate's core is exactly rows 75..132 | **95.74 at y74** (alpha 0.041) and **94.13 at y133** (alpha 0.041), and x1328 reads 102.3 | **differ** | the plate is **1 px taller and 1 px wider on every edge**, at ~4 % alpha | subtle |
| F5 | Clock text | `M`: ink **x 1592..1761, y 94..115** (right edge 1761, cap 22) | ink **x 1555..1762, y 94..116** (right edge 1762, cap 23) | **match** | right edge **+1 px**, cap **+1 px**; width difference is the string (13 chars vs 11) | - |
| F6 | Clock face ring | `M`: ring outer bbox **1774,88 -> 1805,119 = 32x32**, centre (1789.5,103.5), stroke **~3 px** (rows 88-90 solid, row 91 the inner edge) | **1776,89 -> 1805,118 = 30x30**, centre (1790.5,103.5), stroke **~3 px** | **differ** | **-2 px diameter, -1 px on each side**, centre **+1 px x**; stroke matches (the old "1 px thin" is not reproducible, see 7.2) | subtle |
| F7 | Clock face dark disc | `M`: the interior is **dark** - 6x6 block at (1778,106) = **28.1** against a 71.4 plate interior, i.e. a black disc at **~0.61 alpha**; the ASCII map of the face shows `.` (<60) everywhere except the hands and the ring | **no disc**: the same block reads **151.7** (the minute hand) and the disc-free interior reads **78-90** = the plate level (`view.cpp:2215-2227`: the disc is drawn only when `measuredFace`, i.e. on a modal message page) | **absent** | the console's face is a filled black disc at ~0.61 alpha inside a 32 px white ring; crop `clock-face` 1768,84,46,40 = **11.46 / 0.5135** `[contentDiffers: different hand angles]` | visible |
| F8 | Indicator slots (five 32x32 at -244/-274/-342/-376/-410 from x1760) | `M`: all five empty on this signed-out console (no lit slot anywhere in x 1400..1760, y 88..120) | five slots declared, only `tex_indi_online` drawn and only while `onlineFriends>0` (`view.cpp:2263-2276`); a fresh data dir has none | **match** | 0 | - |
| F9 | PS-tap plate | `M` on `info-indicator...png`: dark plate left edge **x 1192** (82.3 -> 43.5 at x1192), top **y ~83** (81.0 -> 49.5 at y85), bottom **y ~126** (51.4 -> 78.9 at y127), carrying the controller glyph, `1` and `-`; area mean x1200..1320 y85..125 = **66.40** | `drawRoundedRect(QRectF(1191,81,134,46))` flat `QColor(53,49,41)` + `tex_controller` + `1` + `-` (`view.cpp:2384-2399`), 155/66/2000/66 ms window (`:2404-2409`) | **match** | left edge **-1 px**, top **-2 px**, bottom **+1 px**; area mean **+1.44 luma** | - |
| F10 | Bar suppressed while a firmware band is up | `M` `[cited]` `SETTINGS 11.6`: the captured band frames have neither plate nor clock (luma 0 at x1750..1918, y100) | `choiceBandUp` / `optionsBandUp` gate the whole block (`view.cpp:2171-2181`) | **match** | - | - |

---

## 6. Idle hint plate and frame corners

### 6.1 The idle hint plate - the claim does not survive the frame

`GRAPHICS-INVENTORY.md:100-102` claims a `XMB hint plate` (dark rounded box at "approx 1663,955-1820,990")
carrying a Triangle glyph and the word `Options`, appearing after a few seconds without input. **This pass
could not reproduce it.** All three frames the inventory offers as the idle state measure the same thing in
that rect: **max luma 58.6** against a local background of **55.3-55.4** (+3.2), i.e. no plate, no border,
no glyph - a dark rounded box would be either much darker or edge-bright. And the frame's own content says
it is not an idle state at all: `col-game-idle-hint.png` is the `Game` column scrolled to `Corrupted Data`
(two rows visible above the focus, the `BG1` plate clear of the ribbon).

| frame | max luma in (1663,955)-(1820,990) | local background (1663,900,157,40) |
|---|---:|---:|
| `col-game-idle-hint.png` | 58.6 | 55.4 |
| `col-settings-idle-hint.png` | 58.6 | 55.3 |
| `col-video-idle-hint.png` | 58.6 | 55.4 |

| # | element | console | native now | status | delta | sev |
|---|---|---|---|---|---|---|
| H1 | Idle hint plate + Triangle glyph + `Options` | **not found in any local frame** (the claim's own frame shows nothing at (1663,955)-(1820,990)) | nothing is drawn - no hint-plate code exists in `view.cpp` (no `1663`, no hint plate in `drawScene`) | **absent** (both; the inventory's claim is unverified) | n/a - a capture of a real idle state is needed before either side can be scored | subtle |

Recommendation for the lead: treat H1 as a **claim to re-capture**, not a gap to implement. Implementing a
plate from a frame that does not show one would fabricate a console surface - the same reasoning
`ICON-GAP-AUDIT` uses for the empty indicator slots.

### 6.2 Corner and edge treatments

Covered as B5 (no vignette on either), B7 (no border/bezel on either side; both edge columns flat within
1.7 luma), and the top rows (flat 84.33 / 101.15 over y0..16). There is **no corner or edge element in this
domain that either side draws and the other does not** - the apparent corner difference in B5/B6 is the
field's own top-vs-bottom gradient plus the residue. Crop `corners-tl` 0,0,200,200 = 22.01 / 0.9630 (the
best-scoring large crop in the set) and `left-edge` 0,300,60,600 = 20.51 / 0.8501 confirm it.

---

## 7. The top 12 gaps, by visual importance

Rects are console geometry; deltas are native-minus-console in luma or px. Severity in the last column.

| Rank | Gap | Console rect | Measured delta | Sev |
|---:|---|---|---|---|
| 1 | **Ribbon crest is inverted on the right half** (R1-R3) | full width, crest y 648 at x1700 | **-291 px** at x1700, **-284 px** at x1900, **+129 px** at x300; `ribbon band` 16.32 dB / 0.7161 | blocks the look |
| 2 | **Ribbon is 30-105 luma too bright at its own crest** (R4) | crest peaks 105-134 luma | **+30** (x0) to **+105** (x1520) at the crest; +26 % to +76 % | blocks the look |
| 3 | **Whole background field is +10..+21 luma high** (B6, and see B3/B4) | y0..16 x200..1700 = 84.33 | **+16.8** top rows, **+21.0** at (20..300, 0), **+10.4** at the bottom edge; frame 18.58 dB / 0.8347 | blocks the look |
| 4 | **Item focus plate `BG1` not drawn** (I10) | **405,419 -> 724,594** (319x175) | the whole plate; +43..+60 luma lift missing; `item label row` 400,425,800,100 = 12.05 dB / 0.4098 `[contentDiffers]` | visible |
| 5 | **Extra TV column** (C1) | nine columns, TV omitted | one extra icon cell; console Video at **x 355** is the native's TV at **x 365**, console Music (x 155) is off-screen | visible |
| 6 | **Clock face has no dark disc** (F7) | 1774,88 -> 1805,119 | interior **28.1 vs 78-90** (a 0.61-alpha black disc missing); `clock-face` 11.46 / 0.5135 | visible |
| 7 | **Info-bar plate ramps are 17 % weak on both edges** (F2) | y75 and y132, x1329..1919 | alpha **0.351 -> 0.291** (top), **0.352 -> 0.292** (bottom); the plate is also 1 px taller/wider (F4) | visible |
| 8 | **Focused item icon expands only 1.73x instead of 2.56x** (I2) | focused glyph x 500..628, y 443..546 inside a 184.8 px quad | box **104 vs 184.8 px (-80.8, -44 %)**; measured expansion on the same folder art **60 -> 104 (1.73x)** vs the console's **50 -> 128 (2.56x)** | visible |
| 9 | **Bottom band rises only half as fast** (B4) | y780..1075, x20..300 | rise **6.90 -> 3.15 luma** (-46 %); +14.2 at y780 vs +10.4 at the edge | visible |
| 10 | **Category neighbour contrast is 10-17 % low** (C6, a consequence of #3) | Network 64x64 at 775 | lift **+95.8 -> +86.7** (-9.5 %); PSN -13.4 %; Friends -17.0 % | subtle |
| 11 | **One item row draws the wrong glyph** (I3) | row 2 of the Game column | the `PS Vita System Application Utility` row shows a horizontal pale strip instead of a folder-shaped glyph; folder ink elsewhere +10 wide / +5 tall | visible |
| 12 | **Dust is more numerous and dimmer** (R5) | whole frame | **106 -> 184 specks**, peak **171 -> 143** (`PARTICLES.md:708-712` `[cited]`); my proxy: 0 console specks vs 14/9 native in two lower-right bands | subtle |

Just outside the twelve, in this order: C12 category move **-33 ms** (165 vs 198), B2 month phase
**+0.403 month-units**, I7 subtitle **+4 px low**, F6 ring **-2 px**, F4 plate **1 px oversize**, F5 clock
text **+1 px**.

---

## 8. At ceiling - measured correct, or impossible to improve locally

Each line is a measurement from this session, not a code read.

1. **Category grid.** Pitch **200.00 px** (five neighbour centroids), focus column **565**, unfocused row
   **290**, quad 117.13/167.328, material .698/1.0. `GEOMETRY-ROOT.md:319-325` traces every one to a vertex
   constant or a `vsh.elf` literal; the frame agrees to **<1 px** on the x grid and the unfocused row y (the
   focused row's own y is the same band on both - C10).
2. **The category label.** Console ink `Game` **536..593 x 342..358**; native **536..593 x 342..359**.
   Zero x delta, +1 px cap. No label on any unfocused column on either side.
3. **The item row block.** Focused row 505, pitch 80, first row below **+175**, row above **-305** -
   all three reproduce to **0 px** on the same string pairs (`Game Data Utility`, `Memory Card Utility`).
4. **Label typography and alpha.** Cap **32 px** on both, string width **-2 %**, and the ink-level
   difference (+11 luma) is exactly the background residue passing through the `.455` alpha, i.e. the
   text itself is right.
5. **Info-bar plate geometry and interior.** Rect 1329,75,591,58; interior wash **-4.65** vs **-4.70**;
   no right edge on either. `plate-only` **22.74 dB / 0.9533**.
6. **The PS-tap plate.** Left edge **1192 vs 1191**, top **83 vs 81**, bottom **126 vs 127**, area mean
   **66.40 vs 67.84**. This surface is not a gap, and the reference frame exists
   (`out/rpcs3-gaps-2026-10-28/stills/info-indicator__original__day__1080p.png`), which closes the
   "no PS-tap capture" note in `GRAPHICS-INVENTORY.md:206`.
7. **Clock text placement.** Right edge **1761 -> 1762**, ink top **94 -> 94**, cap 22 -> 23.
8. **Clock face centre and ring stroke.** Centre (1789.5,103.5) vs (1790.5,103.5); stroke **~3 px** both.
9. **Indicator slots.** Five authored slots, none lit, on both. Nothing to add without a PSN account or a
   message/chat source - inventing a lit slot would be a fabricated surface.
10. **No vignette and no frame border.** Corner means 66.61/66.75/54.79/54.72 (left-right spread 0.14) and
    flat edge columns on the console; flat edges and no border object on the native. Both are
    "draw nothing", so both are correct.
11. **Top-row flatness.** y0..16 flat to 0.05 luma on both.
12. **The value column** is not exercised at the root (no Game-column row carries a value), so it is
    neither a gap nor a proof here.
13. **The focused category icon's own silhouette.** The pad's left and right edges land within 0 px
    (x 511..604 at y290; grip arms 514..534 and 598..618 at y310), and the pad body is
    **176 -> 192 luma**, i.e. the whole difference is the background residue behind it, not the art.
14. **The bar is correctly suppressed while a firmware band is up** (`view.cpp:2171-2181`), matching the
    measured absence in `SETTINGS 11.6`.

---

## 9. Claims in the existing audits this pass could not reproduce

| # | Existing statement | What this pass measured | What would settle it |
|---|---|---|---|
| X1 | `UI-ELEMENT-GAP-AUDIT.md:94` (R9): neighbours are **+22 % brighter** than the console ("console +75.8, native +92.2") | **Contradicted on the current build.** Network lift **+95.8 -> +86.7 (-9.5 %)**, PSN **-13.4 %**, Friends **-17.0 %**. The absolute peaks agree within 10 luma; the lift is *lower* because the background under the icons is +20 (B6). R9's numbers belong to the 2026-10-28 16:04 build, not this one | a re-run of R9's crop on HEAD; the element's absolute brightness is right, so R9 now double-counts B6 |
| X2 | `UI-ELEMENT-GAP-AUDIT.md:99` (R14) / `ITEM-MENU-INFO-FIRMWARE` B4: the clock ring is "2 px small, stroke **1 px thin**" | **Half stale.** The ring is 2 px small (**32 -> 30**), confirmed. The stroke is **not** thin: the ASCII map of both faces shows a **~3 px** stroke (rows 88-90 solid on both). The part that is worse than recorded is the **missing dark disc** (F7), which no audit lists | the 1:1 face crop this pass used (`C:\dev\out\root-bar-audit\crops\{ref,nat}-clock.png`) |
| X3 | `UI-ELEMENT-GAP-AUDIT.md:97` (R12): plate ramps "top console 0.253 / native 0.241 (5 % weak), bottom 0.352 / 0.279 (21 % weak)" | **Different numbers, same direction.** At 1:1 over x1420..1680 I measure top **0.351 -> 0.291 (-17 %)** and bottom **0.352 -> 0.292 (-17 %)** - both ramps equally weak, not 5 %/21 %. Also unrecorded: the plate is **1 px tall/wide** at ~4 % alpha (F4) | a fixed crop definition (the ramp rows are 1 px and the background behind them differs by 13 luma between the two frames) |
| X4 | `GRAPHICS-INVENTORY.md:100-102`: the idle hint plate at "approx 1663,955-1820,990" with a Triangle glyph and `Options` | **Not reproducible.** All three `*-idle-hint.png` frames measure **max 58.6** luma in that rect against a **55.3-55.4** local background (+3.2) - no plate, no border, no glyph. `col-game-idle-hint.png`'s own content is the `Corrupted Data` state, not an idle one. Neither side draws a hint plate, and no `view.cpp` site exists | a console capture taken after >= 4 s of no input, with the crop origin recorded |
| X5 | `GRAPHICS-INVENTORY.md:46-50`: "nine columns, not ten ... the native build draws a TV column between Video and Game; that is a real column-set difference and **no rig run closes it**" | **Confirmed on HEAD** (C1): at `Game`, the console's Video is at x 355 and the native's TV is at x 365; the console's Music at x 155 is off-screen here. The claim is current, not stale - recorded so the lead does not re-dispatch it | a decision, not a capture: either drop the TV column from `shell.cpp`'s list when no service is registered, or accept the difference |
| X6 | `GRAPHICS-INVENTORY.md:206`: the PS-tap indicator frames "were deleted rather than shipped mislabelled", so the surface is not in this batch | **Superseded.** `out/rpcs3-gaps-2026-10-28/stills/info-indicator__original__day__1080p.png` is a 1920x1080 PS-tap frame, and the native's plate matches it to 1-2 px (F9) | none - the evidence exists; update the inventory line |
| X7 | `INFO-BAR.md:3-7`: the bar is at (1329,75) 591x58 and the analog clock was added | **Confirmed**, and extended: the analog face is the right radius only on modal pages; at the root it is 2 px small and has no disc (F6/F7) | the `measuredFace` branch at `view.cpp:2215-2220` is already the correct geometry - lifting it to the root is a 3-line change, but it moves the recorded clock/info-bar gate layers (`UI-ELEMENT-GAP-AUDIT.md:255` X6) |
| X8 | `UI-ELEMENT-GAP-AUDIT.md:89` (R4): the month phase still carries `+t` and the one-line hunk is "not applied" | **Confirmed on HEAD** (`view.cpp:510`), with a comment at `:504-509` that says passing the console's own value drops the frame gate to 10.97 dB. The fit is deliberate, not forgotten | resolving the phase convention between the captured `_MonthTime` and the shader's parameter (PIPELINE, second pedestal pass) |
| X9 | `GEOMETRY-ROOT.md:361` (#38): the back arrow is a `42 px` glyph in `{kColumnX-142,480,50,50}` | **Stale.** HEAD draws `explore_plugin_full:tex_arrow_anim` cell 0 as a **30x30** quad at (430,490), alpha .89 (`view.cpp:2097-2101`). This is the item column's left chevron, adjacent to my domain | none - the code moved past the doc |

---

## 10. What this pass did not do

- Did not edit any source, tool, budget or reference file; the only file written is this one. Captures and
  crops live under `C:\dev\out\root-bar-audit\` (scratch).
- Did not measure the night preset, a non-`Original` theme, or a month other than October. B1/B2/B3/B4 are
  day-preset measurements at 2026-10-28 10:00 only; the night map's 1.216x energy ratio
  (`PIPELINE 5th pass 2` `[cited]`) was not re-derived.
- Did not capture a native category-move burst. C12/C13 rest on `TRANSITION-HARNESS.md`'s stored
  sequences plus the code; the console side of C13 was not re-measured this session.
- Did not measure the idle hint (H1) on either side - no valid console frame exists locally.
- Did not re-derive the glare/tone/dither chain. The residue figure (B6) is a frame measurement of the
  composite, not an independent confirmation of `PIPELINE`'s -0.0594 linear value.
- A live instance of the build was already running (`PID 34392`, started 19:23); every capture in this
  pass is a separate scripted instance with its own `--data-dir`.
