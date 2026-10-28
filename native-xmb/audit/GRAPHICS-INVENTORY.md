# GRAPHICS INVENTORY

The visual inventory of the real PlayStation 3 4.93 XMB, captured in RPCS3 on 2026-10-28, reorganised as a
check-list for the native build. This document is the summary and the index; the per-surface tables with
every frame rectangle, every row label and every transient flag are in the capture manifest.

- **Frames**: `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` - **162 PNGs, every one exactly 1920x1080**,
  181.4 MB. No crop, no rescale, no re-encode.
- **Per-surface tables, rects and provenance**: `C:\dev\out\rpcs3-allgraphics-2026-10-28\INVENTORY.md`
- **Rig**: RPCS3 `v0.0.43-0b0fa082`, Vulkan 1920x1080, borderless client area exactly 1920x1080, Clocks
  scale 100. PS3 4.93 XMB from this `dev_flash`; no firmware or PKG installed.
- **Pinned state**: guest clock 29/10 12:00-12:31 (the pure day preset: day is held 11:00-13:00), Theme /
  Color / Background / Font all `Original`. The day preset was produced by the scratch config's
  `Console time offset (s)` only - the host clock and `xRegistry.sys` were restored byte-for-byte.
- **Written by**: this pass only these two files - this one and the capture manifest. No source, no other
  audit file, no git command.

---

## 1. The surfaces, and how many frames each has

| # | Surface | Frames | Route driven |
|---:|---|---:|---|
| 1 | Root - the nine-category horizontal row | 12 | one anchor frame per column at row 0 |
| 2 | Game column, all 7 rows | 10 | `UP` clamp, one `DOWN` per row, two past the end |
| 3 | Settings column, all 16 rows | 21 | same |
| 4 | Photo column, all 3 rows | 8 | same |
| 5 | Music column, all 2 rows | 8 | same |
| 6 | Video column, all 3 rows | 5 | same, with 130 ms key holds |
| 7 | Network column, all 4 rows | 9 | same |
| 8 | PlayStation Network column, all 5 rows | 8 | same |
| 9 | Users column, all 3 rows | 11 | same |
| 10 | Friends column (rowless) | 5 | same; no row appears |
| 11 | Info bar (plate, clock, glyph) | 1 + every column frame | - |
| 12 | Contextual button-hint plate (idle) | 3 | 4.2 s idle after the last key |
| 13 | Options menu (Triangle on `Corrupted Data`) | 5 | `TRIANGLE`, then `DOWN` per row |
| 14 | Options submenu (`Group Content`) | 1 | `DOWN` twice inside the menu |
| 15 | Delete-confirmation dialog | 7 | `TRIANGLE` -> `DOWN` -> `CROSS`, then Circle to cancel |
| 16 | Theme Settings page | 3 | Settings row 7 of 16 -> `CROSS` |
| 17 | Theme / Color / Background / Font choice bands | 10 | `CROSS` on each page row, `DOWN` in the band |
| 18 | System Settings page, Video Settings page | 7 | Settings row 6 and row 3 |
| 19 | Network Settings page + depth-2 status list + depth-2 message page | 7 | Settings row 16 -> `CROSS` -> `CROSS` |
| 20 | OSK + the user-name page | 8 | Users row 2 -> `CROSS` -> `CROSS`, then Circle twice |
| 21 | Media item pages (empty-state chrome) | 14 | Video / Music / Photo items opened |

Nine columns, not ten: **this console draws no TV / Video Services column**. The category row is Users,
Settings, Photo, Music, Video, Game, Network, PlayStation Network, Friends - Users at the left clamp,
Friends at the right clamp, `category_tv.xml` omits the category when no service is registered. The native
build draws a TV column between Video and Game; that is a real column-set difference and no rig run closes
it (also recorded in `REFERENCE-GAPS.md` 4.1).

---

## 2. What to check in the native build

60 distinct graphics. Each line names the graphic, and where it belongs.

### Root category row (4)

1. `XMB category icon <focused>` - the focused category's icon cell, drawn larger and brighter than its
   neighbours; measured 516,249-620,317 for Game, on a 200 px pitch with 10 px focus padding.
2. `XMB category icon <unfocused>` - a neighbour cell, 54-64 px of ink (Network 748,260-801,320).
3. `XMB category label` - the focused category's name, centred under its icon only. **Unfocused categories
   draw no label**, so a row of nine labelled icons is wrong.
4. `XMB category row pitch` - 200 px pitch, +-210 to the focus neighbours, row y 290 unfocused / 280
   focused. Belongs in the view's category-row projection.

### Open column, depth 0 (9)

5. `XMB plate BG1` - the soft gradient plate behind the focused item's icon; bright core 502,463-627,547,
   no hard edge. It is an artwork plane, not a focus rectangle.
6. `item_tex_plain_folder` - the white folder glyph at the focused size, 472,443-625,546 for the Game
   column's utility rows.
7. `XMB item title <focused>` - 684,492-1212,521, a 27 px ink band; the label column at depth 0 is x 684.
8. `XMB item subtitle` - the second line under the title (`*User` on the rows that carry a user scope),
   ink top y 528.
9. `XMB item icon <unfocused>` - the same glyph at the smaller size (542,695-589,696 for row 2) and at a
   dimmer alpha.
10. `XMB item label <unfocused>` - 683,667-957,698 for row 2; the unfocused label column matches the
    focused one, only the alpha differs.
11. `XMB item chevron left` - the small left triangle at ~x 395-420 on rows that open a submenu (`Remote
    Play`, `Playlists`, `Photo Gallery`). It marks "this row has a page", and it sits left of the icon.
12. `XMB item current-user bullet` - the small dot at ~x 452-462 on the `*User` row only.
13. `XMB item end-of-list overscroll` - pressing `DOWN` on the last row does not move the focus; the whole
    column sinks 40 px per press and stays sunk. A build that clamps silently is drawing a different
    console.

### Info bar (5)

14. `indi_base` plate - 1329,75-1919,132 (591x58), a 1 px light border with a translucent fill, flush to
    the right screen edge. The plate does **not** grow with the clock string.
15. `XMB plate border` - the 1 px border itself: top ink rows 75-77, bottom 130-132, left column 1329-1332.
16. `clock` text - 1592,88-1762,119, right-aligned, format per the console's own Date/Time settings.
17. `XMB glyph analog clock` - 1774,88-1805,119, a 32x32 circular face with two hands, right of the text.
18. `XMB indicator slots` - `indi_base`'s five 32x32 status-icon planes at the plate's left, empty on a
    signed-out console. The slots exist even when nothing fills them.

### Idle hint (2)

19. `XMB hint plate` - the dark rounded box at approx 1663,955-1820,990, bottom right.
20. `XMB hint triangle glyph` - the Triangle glyph plus the word `Options` inside that plate. Both appear
    **only after a few seconds without input** and vanish on the next key press.

### Options menu, Triangle (9)

21. `XMB options band` - a right-hand vertical gradient from ~x 1330 to a geometric zero near 1720, full
    height, over a dimmed page. No hard right edge.
22. `XMB options row label` - 23 px white text at text-left x 1336.
23. `XMB options row Sort By` - group 1, ink 1337,301-1583,322.
24. `XMB options row Group Content` - group 1, ink 1336,341-1502,362.
25. `XMB badge gate` - 1355,346-1394,363, a small purple rounded-square badge after `Group Content`.
26. `XMB chevron right` - 1400,301-1418,323, one per submenu row, at the band's text right edge.
27. `XMB chevron left` - replaces the right chevron on a row whose nested list is open.
28. `XMB options focused action row` - `Start` at ink 1337,540-1391,558, brighter than `Delete` (581-598)
    and `Information` (621-638).
29. `XMB submenu band` - the second band `Group Content` opens, with `By Format` / `By Album` / `All`.
    **The menu is two groups at 40 px pitch with a 199 px gap between them**, not five evenly spaced rows.

### Delete-confirmation dialog (8)

30. `XMB dialog dim` - the whole XMB behind is a flat dim wash. There is **no box plate and no scrim
    rectangle**; the dialog is defined by its rules and its content.
31. `XMB dialog item plate` - 425,400-710,555, a soft grey plate holding the subject's icon.
32. `XMB dialog item icon` - the corrupt-data glyph inside that plate, ~540,505-590,570.
33. `XMB dialog message block` - two lines from x 955: `The installed game will be deleted.` and `Are you
    sure you want to continue?`.
34. `XMB dialog button row` - `Yes` at ~1074,510 and `No` at 1150,515-1239,531, with **`No` focused**.
35. `XMB dialog meta block` - the three dimmed lines under the plate (`-`, `22/10/2026 2:14`,
    `Counting...`), ink bands around y 640-770. This is item metadata, not the console clock.
36. `XMB rule pair` - y = 199 and y = 879, 1 px, x 200-1699, the same pair every depth-2 page uses.
37. `XMB legend glyph row` - `X Enter` at ~730,800-820,814 and `O Back` at ~1030,800-1110,814.

### Depth-1 page (7)

38. `XMB page title bar` - the page's own icon and title at the top left, above the rule.
39. `XMB page rule` - y = 199, 1 px, full width.
40. `XMB page row icon` - 546,489-584,524, the focused row's 32 px glyph just left of its title.
41. `XMB page row title` - ink 626,493-1432,517. **The depth-1 label column is x 626, 58 px left of the
    column's 684** - one label column for both depths is wrong.
42. `XMB page row help` - ink 626,523-1449,547; wraps to two lines on `Remote Play Settings`, which is the
    only row that grows taller.
43. `XMB page artwork tile` - ~170,380-370,520, the large translucent artwork left of the focused row.
44. `XMB page chevron left` - ~395,480-420,510, marking the focused row as having a submenu.

### Choice band (4)

45. `XMB choice band` - border at x 1324-1325, gradient to ~1744, full height; the same construction as the
    options band but a different left edge (the two bands differ by 64 px on two pages of the same class).
46. `XMB choice item label` - text-left x 1341, ink tops 261/341/421/501, **pitch 40**;
    `Install` / `Original` / `Classic` / `Air Paint`.
47. `XMB choice item thumbnail` - 1304,430-1369,479, the small theme preview `Air Paint` carries.
48. `XMB color chip` - unlabelled 40 px chips, first ink top 531, the focused chip taking a 1 px outline.
    The current Theme carries **no check and no dot**; only the focus marks it.

### Depth-2 page (5)

49. `XMB depth2 rule pair` - y = 199 and y = 879, 1 px, x 200-1699.
50. `XMB depth2 row grid` - 13 rows, ink tops 291 -> 771, **pitch 40** (half the depth-1 80 px - two page
    classes, two grids).
51. `XMB depth2 label/value columns` - label right-aligned at x 939, value left-aligned at 979.
52. `XMB depth2 back hint` - `O Back` at x 1155-1239, y 897-916, the page's only control.
53. `XMB depth2 message body` - the centred two-line body of the `Internet Connection Settings` page. A
    depth-2 page has **no focusable row at all**: Down and Up change nothing.

### OSK and text entry (7)

54. `XMB OSK panel` - 493,290-1426,789: an opaque light-grey panel, 1 px border, dark title strip and
    dark entry strip, over a strongly dimmed XMB, with a second grey panel on the right for the
    prediction list.
55. `XMB OSK title bar` - ink 503,298-656,321 (`User Name`, with a pencil glyph).
56. `XMB OSK field` - ink 514,346-594,378, the dark entry strip holding `User 1`.
57. `XMB OSK caret` - the green I-beam / hollow cursor marker inside the field; it blinks (batch 1 measured
    ~936 ms, ~53 % duty).
58. `XMB OSK key grid` - y 400-699, four rows of ten keys (`1234567890`, `qwertyuiop`, `asdfghjkl;'`,
    `zxcvbnm,.?`) on light grey keycaps, one key highlighted (`g` at rest).
59. `XMB OSK function strips` - strip 1: `@#:` / `a/a` / `a/A` (yellow, text mode) then `Backspace` /
    `Space` (blue), L2/R2/L1/R1 labels on the shifted keys, and a `Prediction a 0` cell at the right;
    strip 2: the four arrows (blue), `Input Mode` (SELECT), `Enter` (START), clipboard and keyboard icons.
60. `XMB user-create page` - `Enter a user name.`, a 1 px outlined field prefilled `User 1`, an `OK`
    button, and the `X Edit` / `O Back` legend. This is the page that opens the OSK, and it is its own
    surface: a centred prompt, a field and one button, no item column.

---

## 3. The five things most likely to be drawn wrong

Stated because the frames make each one unambiguous:

1. **The options menu is two groups with a 199 px gap**, and its first group is a pair of submenu rows with
   right chevrons and a purple gate badge. Five evenly spaced rows is the wrong shape.
2. **A focused settings row is not a rectangle.** The row is marked by brighter text plus a gradient
   confined to the text block, plus a 32 px row icon at x 546 and an artwork tile at x 170-370.
3. **The delete confirmation has no box plate.** It is a full-screen dim, a rule pair at 199/879, a message
   block, a two-button row with `No` focused, an item-metadata block and the button legend.
4. **Depth-1 and depth-2 pages are two grids**: 80 px pitch with focus at y 505 and the label at x 626 at
   depth 1; 40 px pitch, read-only, label right-aligned at 939 / value at 979 at depth 2.
5. **Unfocused categories carry no label, and the current Theme carries no check.** Both are absences a
   recreation tends to invent.

---

## 4. Not in this inventory, and why

| Surface | Why |
|---|---|
| The system menu raised by a PS hold, and the PS-tap controller indicator | four attempts over three boots produced unchanged frames: `PS` is Backspace in the keyboard handler's default map and this session never registered it. The frames that were grabbed (`ps-menu-*`, `ps-tap-indicator-*`) showed the plain XMB and were deleted rather than shipped mislabelled. The same rig produced both surfaces on 2026-10-28 07:45: `C:\dev\out\rpcs3-gaps-2026-10-28\stills\ps-menu__original__day__1080p.png` and `info-indicator__original__day__1080p.png` - **those frames are not part of this batch** |
| A media player panel, photo viewer, video player, volume HUD, copy/busy dialog, notification pop-up, signed-in info bar | no media in the emulated VFS, no PSN account, no network, no BD remote. `REFERENCE-GAPS.md` section 7 lists each as do-not-chase for this rig |
| A real installed game's options menu | the only item here with a live options menu is `Corrupted Data`; its menu is captured and its row set is the corrupt-item set |
| Any non-`Original` theme, colour, background or font | the session pinned `Original` throughout, deliberately: every frame is the canonical cell |
| A numeric `800xxxxx` error page | not reachable on this console (batch 2 section 5.6, unchanged) |

---

## 5. Reading the frames

`frames\col-<column>-rNN.png` is the column at `NN` DOWN presses below its top row: `r00` is the top row,
each later frame is a scrolled position, and any frame past the last row is the end-of-list overscroll.
`col2-video|music|photo-rNN.png` is the authoritative walk of the three media columns (the earlier
`col-video-*` walk never left the last row and was deleted). `analysis\rows.csv` carries the machine-measured
ink boxes for all 162 frames, `analysis\ledger.csv` their sizes and hashes, and `analysis\sheet-*.png` the
contact sheets the labels were transcribed from.

Two frames in the set are **state proofs rather than surfaces**: `dlg-item-still-present.png` (the corrupt
title survived the cancelled delete) and `users-after-osk.png` (no user was created).
## Resolution: the two delete-confirm frames agree (2026-10-28, later)

The "no box plate" note above was read against the 07:45 frame that the native two-button box was built from
(`out/rpcs3-gaps-2026-10-28/stills/dialog__original__day__1080p.png`). Putting the two frames side by side
(`frames/dlg-confirm-1800.png` here) they are the **same layout**, not two different dialog kinds:

- a **319x176 lighter plaque around the item icon only**, x~430..712, y~395..558 - that is the object the native
  draws as its "box plate" (white 43/255 wash), so the native plate is that tile, not a plate behind the text;
- the **body text starts at x~1074** (the frame's x955 in the 1708-wide preview), left-aligned, two lines;
- **Yes / No** on one row below it, No focused, at the native's measured x;
- a **metadata block** under the plaque - a clock line (`22/10/2026 2:14`) and the status word (`Counting...`);
- the **rule pair at y199 and y879** and the **X Enter / O Back** pair at y~800-814.

So DIALOGS-FIRMWARE 2.1's dark `tex_msgdialog_bg` tile is the wrong plate for this surface (the frame's plaque
is a light wash), and the native box's remaining deltas are the **metadata block** (the native box draws none) and
the plaque's exact wash, not its geometry. The depth-2/message-page rule alphas stay per-class as measured.
