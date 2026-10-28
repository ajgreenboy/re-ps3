# LAYOUT-TRACE: category column and item-row constants vs firmware data

Status: Phase 3 follow-up, 2026-10-25. Scope: the measured layout numbers currently used by
`src/view.h` / `src/view.cpp` for the top-level category column and the item rows. This document states, for each
constant, the value used now, what firmware data says, the delta, and whether the value is confirmed,
contradicted or untraceable. Nothing in `src/` was modified.

Firmware and capture data stay local: the raw RCOs (`C:\dev\rpcs3\bin\dev_flash\vsh\resource\*.rco`), the decrypted
modules (`C:\dev\xmb\fw493\vsh_module\*.prx`, `vsh.elf`) and the extracted dumps under `out/native-reference/` are not
committed. Only addresses, small values and conclusions are recorded here.

## Sources

| Source | What it is | Used for |
|---|---|---|
| `out/native-reference/rco-behavior/resources.json` | RCO object trees, 122 resources / 8,070 objects / 0 errors, produced by `work/rco-behavior-audit.ts` from the local dev_flash | RCO attributes of the XMB menu and lists |
| `out/native-reference/objattribdef-ps3.ini` | RCOMage object attribute definitions | Field names/offsets for the RCO decode |
| `fw493/vsh_module/vsh.elf` | Decrypted VSH executable (local only) | PAF widget class defaults (PhXmBar, PhXmList) and list-scroll code |
| `fw493/vsh_module/explore_plugin.prx`, `custom_render_plugin.prx`, `xmb_plugin.prx`, `xmb_ingame.prx`, `basic_plugins.prx`, `explore_plugin_game.prx` | Decrypted plugins (local only) | Negative-scan evidence for literals; explore_plugin widget tables |
| `ghidra_proj/vsh493` + `ghidra_scripts/{RefsTo,DecompileRange,DecompileAt}.java` | Local Ghidra project of `vsh.elf` and the boot modules | Reference tracing and decompilation |
| `audit/PIPELINE.md` ("Category icon renderer") | Firmware renderer values captured from RPCS3 `XMB_FP_DUMP` (vertex/fragment constants and eye positions at 1920x1080) | Camera distance, quad sizes, projected positions, brightness |
| `audit/VIDEO-REFERENCES.md`, `audit/PIPELINE.md` "Measured on the real XMB" | Reference-video and RPCS3 motion measurements | Values not available from RCO/plugin data |

## Coordinate convention (why an RCO attribute can confirm a screen position)

The icon renderer's own captured constants establish the projection at 1920x1080 (`audit/PIPELINE.md`, "Category icon
renderer"):

- camera distance 2700; `ndc = (2.8125 X, 5 Y) / d`, so one world unit is one 1080p pixel at the focus distance;
- therefore screen x = 960 + X and screen y = 540 + eye.y for the icon draw.

These are captured firmware vertex constants, not assumptions. They are what makes the RCO offset comparable with a
screen column: `xmenu` posX -395 gives 960 - 395 = 565.

## Comparison table

Legend: CONFIRMED = firmware data reproduces the used value (delta 0); CONTRADICTED = firmware data gives a different
value; UNTRACEABLE = no firmware source for the value was found in the analysed artifacts.

| # | Constant (used now) | Value in source | Firmware evidence | Delta | Status |
|---|---|---|---|---|---|
| 1 | Focus column x | `kColumnX = 565` (`src/view.h:145`); `src/view.cpp:762` | `xmb_plugin_normal.rco` / `xmb_ingame.rco`: object `xmenu` (schema XMenu, child of `page_xmb`) has `posX = -395.0`, `posY = 40.0`. Screen x = 960 + (-395) = 565. Both RCOs agree. | 0 | CONFIRMED (RCO attribute) |
| 2 | Category icon pitch | `distance*200` (`src/view.cpp:762`) | `vsh.elf` list-scroll routines move one item by exactly 200.0 px per scroll step: float 200.0 at vaddr 0x68f10c loaded by FUN_003b1a14 (scroll in), 200.0 at 0x68f118 loaded by FUN_003b1c1c (scroll out). The captured icon spacing is also 200 px (`audit/PIPELINE.md`: "icons 200 px apart plus 10 px either side of focus"). | 0 | CONFIRMED (indirect: firmware 200.0 scroll step equals the captured spacing; the item-layout formula itself was not decompiled) |
| 3 | Icon quads | `117.13+50.2*focus` (`src/view.cpp:769`, `:773`), i.e. 117.13 unfocused, 167.33 focused | Captured firmware vertex constants: 117.13 px unfocused and 167.33 px focused (`audit/PIPELINE.md`). No float32 or float64 literal of either value exists in the scanned modules, so the quad size is computed at draw time by `custom_render_plugin`, not stored as a constant. | 0 (the task brief's 167.32 is a rounding of the captured 167.33) | CONFIRMED (captured firmware renderer data) |
| 4 | Category icon centres y | `290-10*focus` (`src/view.cpp:770`, `:773`), i.e. 290 unfocused, 280 focused | Captured firmware renderer data puts the focus column at y 290 (280 focused) (`audit/PIPELINE.md`). Inverting the documented convention screen y = 540 + eye.y gives eye y = -250 / -260. The RCO only carries the menu origin `posY = 40.0`; the vertical offset from that origin to the icon row (-290) is a scene/camera transform, not an RCO attribute. | 0 against the capture; RCO/plugin delta not derivable | CONFIRMED against the firmware capture; UNTRACEABLE to an RCO/explore_plugin value |
| 5 | Page-open column shift | `childAmount*262` (`src/view.cpp:763`); the focused item row uses `110+152*focus` = 262 at focus (`src/view.cpp:835`) | Captured firmware position: on page open the focused row "steps to x 303" (`audit/PIPELINE.md`). 565 - 303 = 262. No literal 262.0 in the modules; it is the projected animation result. | 0 | CONFIRMED (capture-derived); the stale comment "steps 250 px left" (`src/view.cpp:757`) is contradicted by the capture (303, not 315) |
| 6 | Faint category pitch behind an open page | `... + distance*186` (`src/view.cpp:763`) | No firmware source found. Not in the RCO (the XMenu/XMList objects carry no pitch); not in `explore_plugin`; no float32/float64 186.0 in any scanned module; not recorded in `audit/PIPELINE.md`. The value comes from the reference video ("reference 95 s"). | n/a | UNTRACEABLE |
| 7 | Item row depth: base distance 2700, parent-column distance 2857.2, z step 157.2 | scale `2700/2857.2` (`src/view.cpp:830`-`833`); depth `2700+157.2*depth` (`src/view.cpp:866`) | Captured firmware constants: camera distance 2700 and, with a page open, the parent column's unfocused rows at camera distance 2857.2 (`audit/PIPELINE.md`, "Category icon renderer" / "Item icons"). 157.2 is the arithmetic difference 2857.2 - 2700 used to interpolate depth 0..1; neither fractional value is stored in the modules. | 0 | CONFIRMED (capture-derived; 157.2 is derived from the two captured distances) |

## What the firmware actually encodes

1. **The RCO holds the column origin, not the layout.** The XMB menu is `page_xmb` -> `xmenu` (type 0x804) with ten
   `XMList` children (`list_user`, `list_sysconf`, `list_photo`, ... `list_friend`). Raw decode of `xmenu`
   (`work/layout-trace-rco-raw.js`) gives, in order: posX -395.0 (0xC3C58000), posY 40.0 (0x42200000), posZ 0,
   scale 1/1/1/1, width/height/depth 0, iconOffset 0, event references, and `menus = 10` at attribute offset 88.
   The `XMList` schema has only a flag, an image reference and a ref (`objattribdef-ps3.ini`), so the lists carry no
   geometry or pitch. `xmb_ingame.rco` duplicates the same `xmenu` values exactly.
2. **`vsh.elf` carries the PAF widget defaults.** The `.data` block around vaddr 0x68eec0-0x68f1d0 holds default
   attributes for the XMB widget classes; the `xmbar_orig` name is at 0x68f070 (the name `PhXmBar` is at
   PTR_s_PhXmBar_006fb22c). The block contains float 200.0 and float 10.0 entries referenced by the list-scroll
   routines above. The `PhXmBar` constructor (FUN_003aee84, 0x3aee84) loads three floats with
   FUN_0033fbf4(10/11/12) from runtime config arrays (DAT_00731b64, installed via FUN_003705e8 with defaults of 0 in
   the image), so those bar parameters are theme/runtime values, not RCO attributes.
3. **`explore_plugin.prx` has a large widget-interaction table**, not a category layout table. The block at
   vaddr 0x270550-0x270710 contains the already-known page-move 300 ms values (0x270674, 0x270658) and many
   200/100/20/10 defaults; the functions that reference its first entries are file-copy/dialog code
   (`FUN_000107dc`, `FUN_0001036c`), not the XMB bar. It contains no 186, 262, 2857.2, 157.2, 117.13 or 167.33.
4. **Negative literal scan.** A big-endian float32 and float64 sweep of `vsh.elf`, `explore_plugin.prx`,
   `custom_render_plugin.prx`, `xmb_plugin.prx`, `xmb_ingame.prx`, `basic_plugins.prx` and
   `explore_plugin_game.prx` found none of 117.13, 167.33, 167.32, 565, 290, 280, 262, 186, 2857.2, 2700, 157.2,
   395.0, 0.698 or 0.247 (`work/layout-trace-floats2.js`). Only round values occur (200, 250, 10, 20, 100, 300).
   The fractional values in the native source are therefore runtime projections/derived numbers, not stored
   firmware constants - which is why they cannot be lifted from a table and must be traced through the projection.

## Adjacent confirmed/consistency notes

- Brightness `_Ambiant.w = 0.698` unfocused and 1 focused, and 0.247 behind an open page (`src/view.cpp:769`, `:843`,
  `:865`), are captured firmware shader constants, already recorded in `audit/PIPELINE.md`; no contradiction.
- The focused icon x of 303, unfocused row x shift of 110 and label column x offsets 99/57 (`src/view.cpp:883`) all
  come from the same captured frames; only the row-depth and column-origin values in the table above were re-checked
  here.
- The 10 px "either side of the focused one" is applied by `10*std::clamp(distance,-1.,1.)` (`src/view.cpp:762`).
  A firmware float 10.0 exists in the same `vsh.elf` widget block (e.g. 0x68f108, loaded by the scroll-in routine
  0x3b1a14), but its semantics there is a scroll/step value, not proven to be the focus padding; treat the 10 px as
  capture-measured until that call site is decompiled.

## Contradictions and source-side mismatches

- **No numeric contradiction** was found between the seven listed values and firmware data.
- **Stale comment:** `src/view.cpp:757` says the open category "steps 250 px left", but the code uses 262 and the
  captured firmware position is x 303 (565 - 303 = 262). The firmware supports 262; the 250 in the comment is
  stale. (250.0 does exist as a float in `vsh.elf` and `explore_plugin.prx`, but it belongs to scroll/step defaults,
  not to a page-open animation target that was traced.)
- **Rounding mismatch:** the task brief quotes the focused quad as 167.32; the captured value is 167.33 and the code
  computes 167.33 (117.13 + 50.2). Delta is 0.01, a rounding artifact.
- **Interpolation, not a traced curve:** the code's `117.13+50.2*focus` reproduces both captured endpoints exactly,
  but the focus-size curve between them was never captured, so the linear interpolation is a fit.

## Untraceable items and next steps

1. **Faint category pitch 186.** Needs either an RPCS3 capture of the page-open state's category positions, or the
   page-open bar layout call in `vsh.elf`/`explore_plugin`. A candidate physical model to test: if the parent
   category bar moved back with the rows to distance 2857.2, the 200 px pitch would project to about 189 px, not 186;
   this is a hypothesis, not evidence, and is not used in the code.
2. **Icon row y 290/280.** The RCO only gives the menu origin (posY 40); the -290 offset to the icon row (or the
   camera height that produces it) is in the scene transform. Trace the parent node/camera set up by the XMB bar
   code, then re-derive the row from 540 + eye.y.
3. **Pitch 200.** Decompile the `PhXmList` item-position routine (the class methods around `vsh.elf` 0x3b1a14 and
   0x3b1c1c) to turn the indirect scroll-step evidence into a direct `index * pitch` trace.
4. **Page-open shift 262.** Find the move/teleport call on the bar (the same code path that produces x 303 in the
   capture) and record its target value; this would settle 262 vs the stale 250.
5. **Per-bar theme parameters.** The three floats read by FUN_0033fbf4(10/11/12) in the `PhXmBar` constructor come
   from runtime config arrays; trace their install path (caller chain of FUN_0038a7dc -> FUN_003705e8) to learn which
   layout values are theme-dependent.

## Reproduction (local, read-only)

```
node work/layout-trace-query.js obj "." "xmb_plugin_normal\.rco$"      # decoded object tree
node work/layout-trace-rco-raw.js <path>\xmb_plugin_normal.rco xmenu    # raw attributes, incl. menus = 10
node work/layout-trace-find.js   <path>\vsh.elf 200 250 10              # float32 literal addresses
node work/layout-trace-floats2.js                                       # float32+float64 sweep (negatives)
node work/layout-trace-elf.js    <path>\vsh.elf dump <offHex> <count>   # big-endian float decode
```

Ghidra headless (project `ghidra_proj/vsh493`, program `vsh.elf`, `-noanalysis`; set `JAVA_HOME` to the JDK used by
the existing project and `JAVA_TOOL_OPTIONS=-Duser.home=C:\dev\xmb\ghidra_home`):

```
analyzeHeadless <projectDir> vsh493 -process vsh.elf -noanalysis -scriptPath ghidra_scripts ^
  -postScript RefsTo.java <out.txt> 68f10c 68f118
analyzeHeadless <projectDir> vsh493 -process vsh.elf -noanalysis -scriptPath ghidra_scripts ^
  -postScript DecompileRange.java <out.c> 3b1a00 3b1d20
```

Working files from this pass (not firmware dumps): `work/layout-trace-query.js`, `work/layout-trace-scan.js`,
`work/layout-trace-floats.js`, `work/layout-trace-floats2.js`, `work/layout-trace-find.js`,
`work/layout-trace-elf.js`, `work/layout-trace-rco-raw.js`, and the Ghidra outputs `work/layout-refs*.txt`,
`work/layout-decompile*.c`.

## Summary for the phase handoff

Confirmed: focus column x = 565 (RCO `xmenu` posX -395; exact), icon quads 117.13/167.33 (captured vertex
constants; exact), centres y 290/280 (captured renderer data; exact), page-open shift 262 (captured x 303; exact),
row distances 2700/2857.2 with 157.2 derived (captured constants; exact), category pitch 200 (firmware 200.0 scroll
step plus captured spacing; indirect). Contradicted: nothing numeric; one stale code comment (250 vs 262) and one
rounding mismatch (167.32 vs 167.33). Untraceable: the 186 px faint-category pitch and the icon-row y's scene
transform. No firmware literal exists for any fractional layout value, so those must be traced through the
projection/animation code, not through RCO tables.

---

# LAYOUT-TRACE phase 4b - 2026-10-26: open-page layout re-derived from decoded draw constants

Second pass over rows 2, 4, 5, 6 and 7 above. Same fence: this file only, no source edit, no git command. The
new evidence is not a new capture: it is the per-draw `_Eye` / `_Ambiant` values that were already decoded from
the local RPCS3 `XMB_FP_DUMP` frame-program dumps but had only been summarised per state. 2,852
`*.params.txt` files exist under `out/native-reference/{fp-dump2,fpdump3,fpdump5,fpdump6,fpdump7}` and
`work/item-icon-fp/decoded/{fp-dump2,fpdump5}` (read with `node work/item-icon-eyes.js <dir>`).

One caveat frames every claim below: only **differences** of `_Eye.x` / `_Eye.y` are used. The docs' convention
"screen x = 960 + X" cannot be applied to a raw `_Eye.x` value on its own - the drawn `_Eye.x` for the open
page's parent column is 133.86 while its recorded screen x is 455.5, so an extra per-object translation sits
between the two. Differences within one object class at one distance are free of that translation, so the pitch,
the focus padding, the focus lift and the page-open shift are safe; absolute screen x is not re-derived here.
`src/view.cpp` line numbers below were re-checked at 2026-10-26 21:30 local; that file is being edited by
another session, so the quoted expressions are the durable handle and the line numbers are a convenience.

## 1. Category pitch: 200 px, with +/10 px at the focus (row 2, upgraded)

`out/native-reference/fpdump7` (root-state frames, `_Ambiant.w` 0.698, size 117.130, distance 2700, y -250.00)
holds these distinct `_Eye.x` values (the on-screen ones appear in 9-12 draws each, the far-left ones in 1-3,
since they are culled in most frames):

```
1005.00   805.00   605.00   405.00   205.00   [-5.00 focus]   -215.00   -415.00   -615.00   -815.00   -1215.00
```

- Difference between consecutive unfocused icons: exactly **200.00** in every adjacent pair present
  (1005-805, 805-605, 605-405, 405-205, and (-215)-(-415), (-415)-(-615), (-615)-(-815)). The last gap,
  (-815) to (-1215), is 400, i.e. one intermediate position (-1015) is not drawn in this capture - the far-left
  icons are culled in most frames, so that is a draw-list artefact, not a different pitch.
- The focus icon is the size-167.328 draw at **-5.00**, and its neighbours are at 205.00 and -215.00, i.e.
  **210** away on each side, not 200.
- 210 = 200 + 10 on each side is exactly the native's `10*std::clamp(distance,-1.,1.)` term
  (`src/view.cpp:838`), and the documented "icons 200 px apart plus 10 px either side of focus"
  (`audit/PIPELINE.md`, "Category icon renderer").

Status change: row 2 of the comparison table moves from "CONFIRMED (indirect: firmware 200.0 scroll step equals
the captured spacing)" to **CONFIRMED (captured draw constants)** - the bar's own item pitch is 200 eye units,
and the 10 px focus padding is traced as well (it was listed as capture-measured in "Adjacent notes",
`LAYOUT-TRACE.md:82-85`).

## 2. Icon-row y: the value is `_Eye.y`, and it is traced (row 4, closed)

The same dump gives `_Eye.y` = **-250.00** for every unfocused category icon (all 10 x positions, size 117.130)
and **-260.00** for the focused one (size 167.328, `_Ambiant.w` 1.000). With the captured projection constant
540 (`audit/PIPELINE.md`, "Category icon renderer": screen y = 540 + eye.y):

```
540 + (-250) = 290   unfocused row
540 + (-260) = 280   focused row
```

That is the native's `290-10*focus` (`src/view.cpp:846`, `:849`) exactly, and it is the number row 4 of the
table recorded as "CONFIRMED against the firmware capture; UNTRACEABLE to an RCO/explore_plugin value".

What this settles:

- The value is a **per-draw uniform**, not an RCO attribute and not a camera/scene height. There is no RCO
  object carrying -250 or -260, which is why the earlier sweep for a literal could not find one.
- The RCO's `xmenu` `posY = 40.0` does **not** enter the icon row: 540 - 250 = 290 with no +40 term. So the
  "scene transform that puts the icon row at 290/280" (`LAYOUT-TRACE.md:105-107`) does not exist as a separate
  unknown; the row y is authored at draw time and the only constant involved is the -250/-260 pair.
- The focus lift is 10 px up (260 - 250), matching the focus padding of 10 px horizontally. Both are the same
  magnitude, which is a useful check that the two are the same authored "focus raise" parameter.

Status change: row 4 becomes **CONFIRMED (firmware draw constant; exact)** and the open item
"2. Icon row y 290/280" in "Untraceable items and next steps" is closed. The claim to stop making is that the
row y is untraceable: it is traceable, and it is traced.

## 3. Page-open column shift: 262 is the difference of two draw constants (row 5, upgraded)

In the same capture the focused category icon appears at two positions:

| State | `_Eye.x` | Size | `_Ambiant.w` | Draws |
|---|---:|---:|---:|---:|
| root | -5.00 | 167.328 | 1.000 | 9 |
| page open | 257.00 | 167.328 | 1.000 | 3 |

257.00 - (-5.00) = **262.00**, the same 262 the code applies (`src/view.cpp:839`) and the same 262 that
`LAYOUT-TRACE.md:46` derived from the captured screen x 303 (565 - 303). Row 5 moves from "CONFIRMED
(capture-derived)" to **CONFIRMED (difference of two decoded firmware draw constants)**; the "no literal 262.0
in the modules" note stays true and is now explained - the number is not stored, it is the animated delta.

Transition draws in the same dump also show the shift in flight: `_Eye.x` 205.87 / 405.87 / 605.87 / 805.87
(root values + 0.87) while the same draw's `_Eye.z` is 2483.53-2544.78 instead of 2700.00, i.e. the bar is
*closer* to the camera during the page transition, not farther.

## 4. The 186 px faint-category pitch: NOT traceable with the artifacts available (row 6, unchanged)

Status: remains **UNTRACEABLE**. It is a fit from the reference video (`LAYOUT-TRACE.md:47`), and this pass
found nothing in the artifacts that supports it. Three separate reasons:

1. **No decoded draw is on a compressed category pitch.** The full 2,852-file scan yields only these distinct
   distances: 2483.53, 2504.75, 2523.89, 2541.16, 2544.09/2544.23/2544.78, 2700.00 and 2857.20. The 2857.20
   draws are all item rows (size 77.616, `_Ambiant.w` 0.247, `_Eye.x` 133.86, `_Eye.y` -420..765) - there is not
   one category-size (117.13 / 167.33) draw at any distance other than 2700 in any dump.
2. **The one captured open-state depth argues the other way.** During the page transition the bar's
   `_Eye.z` is 2483-2544, i.e. closer than 2700, so a 200-unit pitch would project *wider* there
   (200 x 2700/2483 = 217 px), not narrower. Nothing in the decoded data compresses the bar.
3. **No firmware number 186 exists to lift.** The float sweep already reported none
   (`LAYOUT-TRACE.md:68-73`), and this pass re-checked the two nearest candidates: the only 188.0 literals in
   the scanned set are three unreferenced floats in `explore_plugin.prx` (vaddr 0x270f9c, 0x271358, 0x271b08 -
   no reference in `explore_plugin_all.c`), and a 0.94 float sits in the unattributed `vsh.elf` `.data` block
   at 0x68f1dc (200 x 0.94 = 188, and that block has no code xref in the 4,730 decompiled paf functions).
   Both candidates give **188**, not 186.

What would make it traceable (and what to stop claiming until then): one RPCS3 open-page frame whose
*category* draws are decoded - i.e. the same `XMB_FP_DUMP` capture that produced fpdump7's open state, taken
far enough into the open page that the neighbouring columns are on screen, decoded with
`re-ps3-native --decode-fp` and read with `work/item-icon-eyes.js`. The measurement to take is the `_Eye.x`
difference between adjacent category icons in that frame. Two outcomes are distinguishable and both are
useful:

| If the open-state `_Eye.x` pitch is | Then | Predicts, at that distance d |
|---|---|---|
| 200 (unchanged) with d about 2483 | the columns are only dimmed and magnified; the 186 is a measurement error | projected pitch 217, icon width 117.13 x 2700/d, row y 540 - 250 x 2700/d |
| 186 with d = 2700 | the bar's spacing itself shrinks when a page opens (an authored bar parameter) | projected pitch 186, icon width 117.13, row y 290 |
| some x with d = 2903.2 (= 200 x 2700/186) | the columns recede | projected pitch 186, icon width 108.9, row y 307.5 |

The third row is the only recession model that yields 186, and it predicts two things the existing reference
frames can already be checked against (icon width about 109 px and row y about 307 for the faint columns, not
117 px at 290). Until one of these is measured, `src/view.cpp:839`'s `distance*186` should be labelled a
fit in the source comment and must not be cited as firmware-derived.

## 5. Faint-column brightness: a measured bound, not a fit

The page-open steady-state brightness of the category bar is not directly in the dumps, but its upper bound is:
the return transition's first captured step is `_Ambiant.w` = 0.067 and rises to 0.698, so the open state is
at or below 0.067. The native uses `0.1*childAmount` (`src/view.cpp:841`), which is therefore about 0.03-0.04
too bright - a small, honest delta rather than an unknown. (The same series gives the open-state distance: the
first step implies a start near 2460 units, i.e. a bar about 1.10x magnified, which no native constant
reproduces; it is recorded here as a measurement, not yet as a target.)

## 6. Summary of status changes from this pass

| Item | Before | Now | Evidence |
|---|---|---|---|
| Category pitch 200 | CONFIRMED (indirect) | CONFIRMED (draw constants, exact) | `_Eye.x` 1005/805/605/405/205, step 200.00 |
| 10 px focus padding | capture-measured | CONFIRMED (draw constants) | neighbours at 210 from focus |
| Icon row y 290/280 | CONFIRMED vs capture, UNTRACEABLE to a firmware value | CONFIRMED (draw constant) | `_Eye.y` -250.00 / -260.00 |
| Page-open shift 262 | CONFIRMED (capture-derived) | CONFIRMED (delta of draw constants) | `_Eye.x` -5.00 -> 257.00 |
| Faint pitch 186 | UNTRACEABLE | UNTRACEABLE (three negative checks) | no draw, no 186 literal, nearest data value 188 |
| Faint brightness 0.1 | fit | measured upper bound <= 0.067 | `_Ambiant.w` series 0.067 -> 0.698 |

## 7. Reproduction for this pass

```
node work/item-icon-eyes.js out/native-reference/fpdump7            # the open-page capture, per-draw tuples
node work/item-icon-eyes.js out/native-reference/fp-dump2           # same transition, second capture
node work/item-icon-scan.js out/native-reference/fp-dump2           # grouped distance/size/brightness
# float candidates (no firmware write): BE float32 at file offset = vaddr + 0xF0 for the PRX modules,
#   and file offset = vaddr - 0x10000 for vsh.elf (its first PT_LOAD maps vaddr 0x10000 to offset 0)
#   explore_plugin.prx 0x270f9c / 0x271358 / 0x271b08 = 188.0 (no xref)
#   vsh.elf           0x68f1dc = 0.94 (unattributed .data block, no xref)
```

Firmware-derived values stay in this audit note; no dump, module, capture or scratch script is committed.

---

# LAYOUT-TRACE phase 4c - 2026-10-27: the open-page faint columns measured in RPCS3

Third pass over row 6, the 186 px faint-category pitch. Same fence: this file only, no source edit, no git
command. The new evidence is a real 4.93 XMB capture at 1920x1080 with one category page open, plus the
decoded `XMB_FP_DUMP` from the same run. Short version: **the faint neighbouring columns are 190.5 px apart,
not 186**, they are only visible during the page transition, and the settled open page shows no neighbouring
category icons at all.

## 1. Capture and provenance

- RPCS3 capture build `C:\dev\rpcs3-cap\rpcs3.exe` (the tree built from `rpcs3-rsx-capture.patch`), booted as
  `rpcs3.exe --no-gui --config C:\dev\out\rpcs3-xmb-cap\capture.yml --fullscreen <dev_flash>\vsh\module\vsh.self`.
- Scratch config (outside the repo and outside the RPCS3 folder): Vulkan, 1920x1080, 60 Hz, exclusive
  fullscreen disabled. The global input config was temporarily replaced with a Keyboard handler (PS Button
  Backspace, Cross PgDown, Circle PgUp, arrows unchanged) and restored in the same run. Original and restored
  SHA-256 of `C:\dev\rpcs3-cap\config\input_configs\global\Default.yml`:
  `E624DA0D66A8E971173C39A69A1DC2C89FFE146D57BF2A03C2C8CFACB459CEEE`.
- Navigation: Game -> four LEFT to Settings -> the Game Settings row (already focused) -> Cross. The page that
  opens is `PS - Upscaler` / `PS - Smoothing`.
- Local artifacts under `C:\dev\out\rpcs3-xmb-cap\` (never committed): `settings-b.png` (root Settings),
  `page-open.png` (settled page), `page-closed.png`, `crop-open-top2.png` (open-page top band at 4x contrast),
  `fp-open/` (2,020 `.ucode` captures; 1,748 decoded to `.params.txt` with
  `re-ps3-native --dev-flash C:\dev\rpcs3-cap\dev_flash --decode-fp icons:lib/icons/icons_no_ss_no_shad.fpo@fp-open`).

## 2. Result: the faint pitch is 190.5 px, not 186

- Settled open page (`page-open.png`): the only category icon is the open one, bbox x 249..360, centre x 304.5,
  bbox y 238..318. The 4x contrast crop of x 300..1900, y 220..380 shows no neighbouring category icons.
- During the transition (open and close) the other categories are visible faintly. Their spacing was measured
  by the autocorrelation of the high-passed column-luma profile over the icon band (x 650..1900, y 240..330).
  Four independent frames/pairs over the faint phase peak at **lag 190-191 px**, correlation 0.265-0.283:
  f_001410 vs f_001443 (0.2725), f_002760 (0.2833), f_002760 vs f_002802 (0.2667), f_002794 vs f_002844
  (0.2654). The lag-200 correlation on the same data is only 0.03-0.07.
- An independent anchor agrees: the faint PS icon's blob centre in `f_002760` is x 1709.5, which is 6 steps of
  190.8 from the focused column (`565 + 6 x 190.8 = 1709.8`); the root PS blob centre is x 1770.7. The whole
  faint row fits `x = 565 + 190.5 n` to about 5 px. That is a 0.95 scale of the 200-unit root pitch about the
  focused column.
- The pitch is state-dependent, not a constant 186: 191 at the first close frame (`f_002760`), 194-196 once the
  bar has brightened (`f_002802`..`f_002844`), and 199-200 at root (`settings-b.png`).
- The row's y also moves as a receded row: the faint PS blob centre is (1709.5, 303.2) against (1770.7, 284.5)
  at root; 303.2 matches the projection at camera distance 2857.2 (`540 - 250 x 2700/2857.2 = 303.7`), the
  distance already recorded for the parent column in phase 4b.
- Against the phase-4b prediction table: row 3 (recession) is the correct model in direction, and the measured
  190.5 is the 200-unit pitch at distance 200 x 2700 / 190.5 = 2835, i.e. essentially the recorded 2857.2
  (189.0), not the 2903.2 chosen to force exactly 186. Rows 1 (217 at 2483) and 2 (200 at 2700) are excluded.
- The 186 value is 2.4% tighter than the measurement. It is not reproduced by this capture.

## 3. Decoded `_Eye` values: what they settle and what is missing

`node work/item-icon-eyes.js C:\dev\out\rpcs3-xmb-cap\fp-open` yields 88 distinct tuples. The
`icons_no_ss_no_shad` category draws at the close start are:

```
n=1  eye=(1004.55, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=( 804.55, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=( 604.55, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=( 404.55, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=( 204.54, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=(  -5.45, -260.00, 2483.53, 167.31) amb 0.094
n=1  eye=(-215.45, -250.00, 2483.53, 117.13) amb 0.067
n=1  eye=(-615.45, -250.00, 2483.53, 117.13) amb 0.067
```

The eye pitch is exactly 200.00. This layer is at distance 2483.53, which projects 200 eye units to 217.4 px,
not the 190.5 px visible faint row. Therefore the visible faint row is **not** the category draw that the
>=2000-byte `icons_no_ss_no_shad` dump captures; it is a different (cheaper or differently scaled) draw. The
missing artifact is that layer's own fragment-program/vertex-constant dump. The pitch above was measured from
the frame, not inferred from these eye values; the decoded category layer's 2483.53/200 values should not be
used as the faint row's geometry.

## 4. Corrections and claims to stop making

- The 186 px faint pitch is contradicted: the measured faint-row pitch is 190.5 px (autocorrelation peaks at
  lag 190-191; PS anchor 190.8), a 0.95 scale of the 200-unit bar, and it relaxes back to 200 as the bar
  returns to the root state.
- The phase-4b statement "there is not one category-size draw at any distance other than 2700 in any dump" is
  contradicted by both `fpdump7` and this run: the close-start `icons_no_ss_no_shad` category layer is at
  distance 2483.53 with 200.00 eye pitch (it is simply not the visible faint row).
- The faint row is a transition state. The settled open page shows no neighbouring category icons at 4x
  contrast; drawing them permanently is a second mismatch, independent of the pitch.
- The measured faint geometry is `x = 565 + 190.5 n`, not `kColumnX - 40 + distance*186` (which starts 40 px
  too far left and uses a 3% tight pitch). No source edit was made; this is recorded for the next source pass.

## 5. Reproduction

```
node work/item-icon-eyes.js C:\dev\out\rpcs3-xmb-cap\fp-open
# decode the category program (writes .params.txt beside the .ucode files):
#   re-ps3-native.exe --dev-flash C:\dev\rpcs3-cap\dev_flash \
#     --decode-fp icons:lib/icons/icons_no_ss_no_shad.fpo@C:\dev\out\rpcs3-xmb-cap\fp-open
# pitch: powershell -File C:\dev\out\rpcs3-xmb-cap\pitch.ps1 / diffpitch.ps1 / blobs.ps1 on burst-png
```

Firmware-derived numbers stay in this audit note; no dump, module, capture or scratch script is committed.
