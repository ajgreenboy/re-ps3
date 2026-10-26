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
