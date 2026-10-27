# ITEM-ICONS: open-column item-icon parity audit

Status: Phase 1 follow-up, 2026-10-25. Scope: the icontex item icons in the category's open item column and the
settings-page bullets, as drawn by `src/icons.{h,cpp}` and queued in `src/view.cpp`. Goal: establish from local
firmware evidence what the firmware does for these icons, compare it with what the native does, and rank the
visual gaps. `src/` is not modified by this audit.

The category icons themselves were already ported (`audit/PIPELINE.md` "Category icon renderer",
`audit/CATEGORY-ICON-FIX.md`). Item icons share the same firmware program, so most constants can be read from the
same captured RSX fragment-program dumps. Those dumps are firmware-derived and stay local: capture dirs under
`out/native-reference/`, decrypted modules under `fw493/vsh_module/`, and the extracted `icons` archive under
`out/native-reference/icons/`. Only values and addresses are recorded here.

## Evidence used

| Source | What it gives |
|---|---|
| `out/native-reference/fpdump4/`, `fpdump6/`, `fpdump7/` | RSX captures of the 1920x1080 icon fragment program as drawn, with constants patched in and the matching 512-float vertex-constant block. `fpdump4/6/7` hold 283 / 300 / 212 icon draws. |
| `out/native-reference/icons/lib/icons/icons_no_ss_no_shad.fpo` | The extracted fragment program; header field `ucodeSize = 3024` at offset 24. The 3024-byte `.ucode` dumps in those dirs are this program (the dirs also hold 8608-byte dumps of a different program). |
| `out/native-reference/icons/lib/icons/quad.vpo` | The icon vertex program; the captured `.vc` blocks use the same MVP/cookie slots the native writes. |
| `out/native-reference/icons/override/{day,night}/ICONS.mnu` | Material inputs; the captured material constants match the `night` values exactly (TIME COLOR MIX 0.61799, ATTN DIFF 3.12367, EXPOSE 1.1574), i.e. item icons take the same override as category icons. |
| `out/native-reference/icons/ICONS.mnu` | Base material file. Contains no size/scale key: icon size is per-draw geometry, not an MNU setting. |
| `audit/PIPELINE.md` "Category icon renderer" | Prior capture findings and camera/projection conventions (2700, ndc = (2.8125 X, 5 Y)/d, screen y = 540 + eye.y). |
| `work/item-icon-*.js` | Local decode/summarise scripts used for this pass (capture dirs are local-only). |

The captured `.params.txt` files next to the `.ucode` files were produced by
`re-ps3-native --dev-flash <dev_flash> --decode-fp icons:lib/icons/icons_no_ss_no_shad.fpo@<dir>` (see
`src/main.cpp` around line 406); the `.vc` blocks were decoded as little-endian float4 slots with
`work/item-icon-vc.js`.

## What the firmware does

1. **Program.** Item icons, category icons and bullets are all drawn with `quad.vpo` +
   `icons_no_ss_no_shad.fpo` at 1920x1080, i.e. no supersampling and `SHADOW STREN = 0`. Their captured fragment
   constants are identical material inputs: in the same capture, `_PackedLightParams`, `_Ambiant` rgb,
   `_LuminanceParams`, `_ChangingColor`, `_RefractParams` and `_ParallaxAndGlow` match between category, item and
   bullet draws; only `_EnvRot` (time) and the per-draw `_Eye` / `_Ambiant.w` differ. The captured values match the
   `override/night/ICONS.mnu` set (TIME COLOR MIX 0.61799; the -0.170123 luminance coefficient derives from
   GLASS 0.0624733), so item icons take the same override as category icons. There is no separate item-icon MNU
   override and no item-specific program.
2. **Geometry.** The icon is a square quad. The modelview-projection rows in the captured `.vc` block are
   `[256] = (2.8125*size, 0, 0, 2.8125*worldX)`, `[257] = (0, 5*size, 0, 5*worldY)`,
   `[258] = (0, 0, -1, distance-19.95)`, `[259] = (0, 0, -1, distance)`; the frame cut is `[466] = (15, 8.4375)`.
   With `distance = 2700`, a quad of world size `size` covers `size` reference pixels (screen px = size*2700/d).
3. **Per-state values captured at 1080p** (`_Eye = (x, y, distance, size)`; `_Ambiant.w` is brightness):

| State | Captured `_Eye` | Amb.w | Decoded file |
|---|---|---|---|
| Item icon, unfocused (row above focus) | (-5.00, -420.00, 2700, 77.616) | 1.000 | `fpdump7/fp_0198` |
| Item icon, focused | (-5.00, -35.00, 2700, 184.800) | 1.000 | `fpdump7/fp_0199` |
| Bullet, unfocused (row below focus) | (-5.00, 140.00, 2700, 35.721) | 1.000 | `fpdump7/fp_0245` |
| Bullet, focused | (-5.00, -35.00, 2700, 85.050) | 1.000 | `fpdump7/fp_0244` |
| Parent column, unfocused row, page open | (133.86, -420.00, 2857.200, 77.616) | 0.247 | `fpdump7/fp_0232` |
| Parent column, focused row, page open | (257.00, -35.00, 2700, 184.800) | 0.698 | `fpdump7/fp_0233` |
| Second (unresolved) size class | (-5.00, -500/-580/-660, 2700, 85.554) | 1.000 | `fpdump7/fp_0070/71/72` |

4. **Projection.** Screen x = 960 + X. For a captured `_Eye` at distance d, screen y = 540 + `_Eye.y`*(2700/d) and
   the projected size is `size`*(2700/d). The focused item row is at screen (565, 505); `_Eye` (-5, -35) at
   d = 2700 and the frame-cut slot `[467] = (-0.411458, 0.064815)` confirm it exactly.
5. **Rows.** The captured rows at d = 2700 sit at `_Eye.y` = -420, -35, 140, 220, 300, 380, 460, 540, ... i.e.
   screen y 120, 505, 680, 760, 840, ... So: focus 505; the next row below is +175 (80 pitch + 95); further rows
   step 80; the row above focus passes above the category band at screen 120 (385 above focus, i.e. 80 + 305).
   That is exactly the native `row` formula at level 0.
6. **Page-open (parent column behind the page).** Unfocused rows move back to camera distance 2857.2, so their
   projected size shrinks by 2700/2857.2 and their centres move toward screen centre; brightness drops to 0.247.
   The focused row stays at 2700 with size 184.8 and brightness 0.698 and steps to screen x 303. The captured
   unfocused row below focus at d = 2857.2 has `_Eye.y = 125`, which projects to screen 658.1: 15 px less than
   the same row's 680 at d = 2700 (the "gap below focus closes by 15").
7. **Alpha.** The captured `_Ambiant.w` is the icon brightness in the fragment program; the program's final alpha is
   envmap alpha times that value. The open column uses 1.0 at all focus levels; only the parent column behind a
   page uses 0.247 (unfocused) / 0.698 (focused).
8. **Unresolved capture feature.** Some frames contain a second size pair: 85.554 (= 77.616 x 1.10223) and, in one
   transition frame, 203.700 (= 184.8 x 1.10227) at intermediate x positions. The 85.554 rows are drawn at x = -5
   (screen 565) and y -500/-580/-660 with Amb.w 1.0. The capture does not establish whether this is a per-level
   scale, a page-entry animation scale, or a different list state. The native has no equivalent.

## Comparison table

Legend: CONFIRMED = captured firmware value reproduces the native value (delta 0 unless noted);
CONTRADICTED = the native value disagrees with firmware evidence; UNTRACEABLE = no firmware evidence in the
analysed artifacts.

| # | Constant | Firmware value and evidence | Native value / source | Delta | Status |
|---|---|---|---|---|---|
| 1 | Fragment program | `icons_no_ss_no_shad.fpo` at 1080p (captured ucode length 3024 matches the fpo header; material constants equal `override/night/ICONS.mnu`) | `icons.cpp:75` loads the same `.fpo` | 0 | CONFIRMED |
| 2 | Vertex program and quad transform | `quad.vpo`; captured slots 256-259 as above, `[466] = (15, 8.4375)`, `[467] = ndc centre` | `icons.cpp:74`, `:208`, `:224`-`:228` | 0 | CONFIRMED |
| 3 | Item icon size, unfocused, level 0 | 77.616 (captured; `vc[256].x = 218.295 = 2.8125*77.616`) | `77.616 + (184.8-77.616)*focus` (`view.cpp:235`) | 0 | CONFIRMED |
| 4 | Item icon size, focused, level 0 | 184.800 (captured; `vc[256].x = 519.75`) | same formula | 0 | CONFIRMED |
| 5 | Bullet size, unfocused/focused | 35.721 / 85.050 (captured; `vc[256].x = 100.465 / 239.203`) | `35.721 + (85.05-35.721)*focus` (`view.cpp:235`) | 0 | CONFIRMED |
| 5b | Size curve between focus endpoints | Only the two endpoints are steady-state capture values; intermediate sizes seen in captures belong to animations (e.g. 145.126 for a category, 203.700 for an item) | linear in the eased `focus` value (`view.cpp:235`, `PafEase` 200 ms) | n/a | UNTRACEABLE |
| 6 | Second size class (85.554 / 203.700, x1.1022) | Captured on some rows/frames: x = -5 (screen 565) at `_Eye.y` -500/-580/-660 (screen y 40/-40/-120), Amb.w 1.0; the 203.700 one at an intermediate x | none (single size pair for every level and state) | n/a | UNTRACEABLE (open gap) |
| 7 | Focus row | screen y 505, `_Eye.y = -35` (captured; `vc[467].y = 0.064815`) | `y = 540 + (505 + row - 540 - ...)` (`view.cpp:835`) | 0 | CONFIRMED |
| 8 | Row pitch below focus | 80 px; first row below focus +175 (screen 680) | `row = distance*80 + clamp(distance,0,1)*95` (`view.cpp:828`) | 0 | CONFIRMED |
| 9 | Row above focus, level 0 | screen 120, `_Eye.y = -420` (385 above focus) | `row -= clamp(-distance,0,1)*(level==0?305:95)` (`view.cpp:829`) | 0 | CONFIRMED |
| 10 | Row above focus, level > 0 (deeper pages) | not captured | 95 offset (`view.cpp:829`) | n/a | UNTRACEABLE |
| 11 | Focus column x | 565; `_Eye.x = -5`, `vc[256].w = -1110.9375 = 2.8125*(-395)` | `kColumnX = 565` (`view.h:145`), `iconX = 565` with no page shift (`view.cpp:837`) | 0 | CONFIRMED |
| 12 | Screen centre mapping (ndc) | focused `[467] = (-0.411458, 0.064815)`; above-focus row `[467].y = 0.777778` | `ndcX = centre.x/960-1`, `ndcY = 1-centre.y/540` (`icons.cpp:213`) | 0 | CONFIRMED |
| 13 | Brightness, open column | `_Ambiant.w = 1.000` both focused and unfocused | `brightness = 1 - behind*(1-(.247+.451*focus))` = 1 with no page (`view.cpp:867`) | 0 | CONFIRMED |
| 14 | Brightness, parent column behind page | 0.247 unfocused / 0.698 focused | same formula, behind = 1 | 0 | CONFIRMED |
| 15 | Parent-column distance behind page | 2857.200 (captured) | `2700 + 157.2*depth` (`view.cpp:868`); 157.2 = 2857.2-2700 | 0 | CONFIRMED |
| 16 | Parent unfocused row x | screen 455.5 (captured `_Eye.x = 133.86`) | `565 - 110 = 455` (`view.cpp:837`) | -0.5 px | CONFIRMED within capture rounding (the 110 constant was measured/rounded; capture implies 109.5) |
| 17 | Parent focused row x | screen 303 (captured `_Eye.x = 257`) | `565 - 262 = 303` (`view.cpp:837`) | 0 | CONFIRMED |
| 18 | Gap closure below focus behind page | 15 px (captured row at screen 658.1 vs 680 at d 2700), plus 2700/2857.2 scale | `-15*depth*clamp(distance,0,1)` and `(1-.055*depth)` (`view.cpp:835`) | 0 | CONFIRMED |
| 19 | Alpha / brightness model | final alpha = envmap alpha x `_Ambiant.w`; brightness does not tint RGB | opaque material: `light = (.58+.26*diffuse+bevel)*mix(.70,1.,brightness)`, alpha = normal-map coverage x opacity (`icons.cpp:110`-`:112`) | material differs | CONTRADICTED (appearance reconstruction, not the firmware material) |
| 20 | Qt fallback icon sizes | no firmware basis for 60/104, 40/56 or 18/39 | `view.cpp:842` | n/a | UNTRACEABLE; inconsistent with the captured glass quad sizes |
| 21 | "Item icons in the open column still go through Qt" | current code: icontex items are queued to `IconRenderer` (`view.cpp:866`-`:868`), only RCO/artwork/theme icons stay Qt (`:849`-`:870`) | `audit/PIPELINE.md:339`-`:340` | n/a | CONTRADICTED by current source (stale document statement) |

## What the native actually draws

- **Queue.** During page painting the native rebuilds `iconQueue_` (`view.cpp:702`): category icons at `:775`
  (117.13 + 50.2*focus, brightness .698 + .302*focus) and icontex item icons at `:868`. The queue is rendered in
  one pass at `view.cpp:616` into the background framebuffer, before the Qt UI layer (painted to its own MSAA FBO
  at `:607`-`:613`) is composited on top at `:617`-`:622`; `GL_DEPTH_TEST` is disabled (`icons.cpp:209`).
- **Which items.** `entry.container == "qgl/icontex"` and not a tile goes through `IconRenderer`; game ICON0
  artwork, RCO images, theme icon images and tiles are still painted by Qt (`view.cpp:849`-`:870`).
- **Material.** `IconRenderer` always selects the opaque reconstruction in normal presentation
  (`icons.cpp:150`-`:152`, `fixedTint_.lengthSquared() > 0` is always true because `setFixedTint` is always called
  at `view.cpp:264`). The firmware fragment program is only reachable through `XMB_ICON_FIRMWARE`,
  `XMB_ICON_DEBUG` or `XMB_ICON_UCODE` diagnostics. So item icons are drawn with the same opaque stand-in as
  category icons, not with the firmware glass material.
- **Size.** `itemIconSize(level, bullet, focus)` ignores `level` entirely: every level uses 77.616/184.8 (or
  35.721/85.05 for the `23n` bullet). The level-0 values match the captures; deeper levels were not captured.
- **Size semantics.** The renderer interprets `Icon.size` as world size and `Icon.distance` as camera distance
  (`icons.cpp:213`-`:228`); screen size = size*2700/distance. That reproduces the captured parent-column shrink
  exactly (77.616 at 2857.2 renders as 73.36 px).
- **Brightness.** `brightness` becomes `_Ambiant.w` in the glass path but only scales the opaque material's light
  (`icons.cpp:110`, `:232`); alpha comes from normal-map coverage times `layoutOpacity_` (`icons.cpp:112`,
  `:259`). This is why 0.247/0.698 change the RGB in the native but change alpha (blend over the frame) in the
  firmware.

## Contradictions and inconsistencies found

1. **Stale document statement.** `audit/PIPELINE.md:339`-`:340` says item icons in the open column still go
   through Qt. Since the icontex queue path exists, only non-icontex item artwork is Qt; icontex items go through
   the icon renderer (opaque material). The statement should be updated.
2. **Material model.** The opaque material's brightness factor (`mix(.70,1.,brightness)`) and its invented diffuse
   and bevel terms (`icons.cpp:108`-`:110`) contradict the captured firmware behaviour, where `_Ambiant.w` scales
   the program's alpha (envmap alpha x brightness) and the glass shading is produced by the firmware program. The
   captured values 0.247/0.698/1.0 are used, but they act on a different quantity.
3. **Qt fallback sizes vs glass sizes.** The Qt path draws square item icons at 60 + 44*focus (level 0) and
   40 + 16*focus (deeper), and bullets at 18 + 21*focus (`view.cpp:842`), while the same classes through the glass
   queue use 77.616/184.8 and 35.721/85.05. The Qt numbers were measured from the reference video; they are not
   firmware values and disagree with the captured quad sizes by up to ~2x. Visible size can be smaller than the
   quad because the normal map's alpha coverage does not fill it, but that explanation is unverified (the 23n
   normal-map coverage was not measured). Either way, the two paths disagree with each other.
4. **Second size class.** The 1.1022x pair (85.554/203.700) is not reproduced by the native. It may be a
   page-entry/level scale; it is not attributable from the current captures.
5. **Level assumption.** `itemIconSize` ignores `level`, but the firmware's level>0 size and above-focus offset
   were never captured. The `95` above-focus offset for deeper levels (`view.cpp:829`) is inferred, not traced.
6. **Small constant.** The captured unfocused parent-row screen x is 455.5, the native 455 (110 shift). The
   0.5 px is inside the 2-decimal capture precision; if the true shift is 109.5 the constant should be 109.5.

## Priority-ordered visual gaps and evidence that would close each

1. **Item icons are not glass-shaded in normal presentation.** They use the opaque theme material because that
   was chosen for category icons over wallpaper (`audit/CATEGORY-ICON-FIX.md`). Item icons are the same firmware
   glass program with the same material, so their refracted, env-lit look is missing. Close by: run
   `XMB_ICON_FIRMWARE=1` with a matched day/night clock against an RPCS3 capture of an open category column
   (Game/Photo), compare 3x icon crops; if the refraction is still wrong because the reconstructed background is
   not the firmware's cut, improve the CookieCutter input and then decide whether item icons can use the firmware
   path while category icons keep the opaque one.
2. **Brightness acts on the wrong quantity.** Make the opaque material scale alpha with `_Ambiant.w` instead of
   tinting RGB, or use the glass path; verify against a page-open capture (focused 0.698, unfocused 0.247) and an
   open-column capture (1.0). Evidence: captured constants already in `fpdump7`; only a matched A/B is missing.
3. **Second size class 85.554 / 203.700.** Capture a controlled state per level with a known selection and depth
   (open column, folder page, page opening/closing) and decode `_Eye.w`; if the 1.1022 factor is a page-entry
   scale, implement it in the depth/page animation. Evidence needed: RPCS3 captures with input timing, decoded as
   in this pass.
4. **Level>0 item icons.** The native uses the level-0 pair for deeper pages; capture a folder page (open a
   folder in Game/Music) and decode its item icons; also capture a deeper list with rows above the selection to
   test the 95 offset assumption. Evidence: same decode method, plus the selection index from scripted input.
5. **Non-icontex item icons stay Qt.** Game ICON0 artwork, RCO images, theme icon images and tiles are painted
   with measured sizes and no glass shading. Firmware draws RCO images as paf planes and ICON0 as file artwork,
   so trace those through their RCO object attributes (positions/sizes) rather than the icon program; until then
   mark them as appearance approximations.
6. **Bullet detection and bullet size.** The native detects bullets by hardcoded icontex name `23n`
   (`view.cpp:841`) and applies the glass quad size to them; every other setting icon falls back to the Qt
   18/39 sizes, which are about half the captured 35.721/85.05 quad. Verify the full bullet texture set and
   measure the 23n normal-map alpha coverage to reconcile the visible 18/39 with the 35.721/85.05 quad.
7. **Page-move motion.** Steady-state positions/sizes/brightness are confirmed, but the native approximates the
   page move with a fitted 300 ms mode-4 ease (`audit/PIPELINE.md` "Menu motion") and does not reproduce the
   incoming column's 1.1022x scale-in. Capture transition frames with input timing (the local captures include
   such frames, e.g. 145.126/203.700 at intermediate x) and fit the scale/position curves.
8. **Draw order/occlusion.** Icons are drawn into the background framebuffer and the Qt UI layer (labels, values,
   dialogs) is composited over them (`view.cpp:607`-`:622`), so text always covers icons. The firmware's paf
   ordering between the 3D icon scene and 2D text was not traced; check page-open and dialog frames for wrong
   overlap. Evidence: matched RPCS3 frames.
9. **Resolution scaling.** The native scales the layout by `height/1080` and centres the 1920-wide layout
   (`icons.cpp:153`-`:154`). Only 1080p firmware was captured; a 720p capture would confirm the same scaling and
   the quad-size/projection rule at other modes.

## Reproduction (local, read-only)

```
node work/item-icon-scan.js   <captureDir...>     # grouped _Eye/_Ambiant values per capture dir
node work/item-icon-eyes.js   <captureDir...>     # distinct decoded tuples, with sample file names
node work/item-icon-timeline.js <captureDir>      # decoded draws with screen positions (approximate ordering)
node work/item-icon-vc.js      <file.vc>          # vertex-constant slots 256/257/258/259/466/467 (little-endian)
node work/item-icon-vcdiff.js  <a.vc> <b.vc>      # which slots differ between two draws
re-ps3-native --dev-flash <dev_flash> --decode-fp icons:lib/icons/icons_no_ss_no_shad.fpo@<captureDir>
```

The capture dirs used here are `out/native-reference/fpdump4`, `fpdump6`, `fpdump7` (1920x1080, 3024-byte
`icons_no_ss_no_shad` microcode; the same dirs also hold 8608-byte dumps of a different program that the decoder
skips). Re-decoded copies and the scripts used this pass live under `work/` (local-only, ignored by git as
`/work/*` except `*.ts`/`*.ps1`).

## Summary for the handoff

Confirmed: item icons use the same `quad.vpo` + `icons_no_ss_no_shad.fpo` program as category icons; unfocused
77.616 / focused 184.8; bullets 35.721 / 85.05; focus row screen (565, 505); row pitch 80 with +175 first row and
the level-0 385 above-focus row; brightness 1.0 open column and 0.247/0.698 behind a page; parent column at
2857.2 with the 15 px gap closure and focused row at x 303. Contradicted: the document statement that item icons
still use Qt; and the native material model (brightness tints RGB instead of scaling alpha, with invented diffuse
and bevel terms). Untraceable/open: the 1.1022x second size class, level>0 sizes and the 95 above-focus offset,
non-icontex item-icon sizes, the 110 vs 109.5 parent shift, and the page-move scale motion.
## Experiment 2026-10-26: brightness on alpha vs on RGB

The contradiction above (`_Ambiant.w` scales the program alpha in the firmware, while the native opaque
material dims RGB) was tested rather than assumed. Moving focus brightness into the alpha
(`material.a * opacity * brightness`) and running the canonical capture dropped the category-icon layer
from **20.99 dB to 20.14 dB (-0.85 dB)**, with SSIM 0.8865 -> 0.8851, while every other layer stayed put.

Conclusion: with the current **opaque** material the RGB dimming is measurably closer to the reference
frame, because the firmware's alpha scaling acts on a glass surface that also refracts the background;
letting the background through an opaque icon changes its appearance more than the reference does. The
alpha change was reverted and the reason is recorded in `src/icons.cpp`. Switching to `_Ambiant.w` on
alpha therefore waits on drawing item icons through the refractive glass path (the open gap), and the
measurement above is the bar the change has to beat.

## Experiment 2026-10-26: the opaque material's light direction from ICONS.mnu

Contradiction 2 above (an invented material) was attacked at its strongest evidence, the light direction. The
firmware's key light is not guesswork: the captured fragment constants carry it. In `fpdump7/fp_0199` (the focused
item icon) the three `_PackedLightParams` rows are

```
[0] = 3.12367  -0.0166247  -0.0852542
[1] = 0.484729  0.382283   -0.97268
[2] = 2.45394   0.923896    0.215928
```

Columns 1 and 2 are the two light vectors, each `(sin Z cos X, sin Z sin X, cos Z)` of the MNU angles:
LIT1 ANG Z/X = 22.4976/92.4901 gives (-0.0166, 0.3823, 0.9239), LIT2 ANG Z/X = 282.47/84.9909 gives
(-0.0853, -0.9727, 0.2159). The same values appear in every captured icon draw, category and item alike, and
`_Ambiant` in that draw is `1.05025 1.05025 1.05025 1`, i.e. the MNU AMB triple with the per-draw state
brightness in w.

`IconRenderer::render` already derived both vectors from `LIT1/LIT2 ANG Z/X` for the glass path; the opaque
material ignored them and lit the normal map from a hand-written `(-0.35, 0.4, 0.85)` three-quarter key. That
constant is gone. The opaque fragment shader now takes the same `QVector3D l1` the glass path writes into
`_PackedLightParams`, so one code path supplies the direction for both, and the AMB triple is read once into
`ambientColour` for `_Ambiant` instead of being spelled out at the call site. Ambient level, matte diffuse weight
and edge bevel stay fitted stand-ins: nothing in the captures gives them a direct counterpart.

Canonical capture (`work/ab-layers.ts --capture`, stored RPCS3 reference, same tree both sides - the control run
has the old hard-coded direction):

| layer | control | LIT1 direction | delta |
|---|---|---|---|
| frame | 18.01 / 0.8301 | 18.01 / 0.8301 | +0.00 / +0.0000 |
| top background | 20.24 / 0.9344 | 20.24 / 0.9344 | +0.00 / +0.0000 |
| category icon row | 21.05 / 0.8950 | 20.91 / 0.8947 | -0.14 / -0.0003 |
| item label row | 11.94 / 0.3910 | 11.94 / 0.3910 | +0.00 / +0.0000 |
| clock text | 13.51 / 0.6131 | 13.51 / 0.6131 | +0.00 / +0.0000 |
| info bar | 17.12 / 0.7046 | 17.12 / 0.7046 | +0.00 / +0.0000 |

Six 180x180 crops centred on the category icons (focus row y 280, others 290, 200 px pitch) score 21.265 dB /
0.8839 before and 21.111 dB / 0.8846 after: PSNR -0.15 dB, SSIM +0.0007. A pixel diff of the focused-item and
bullet captures changes icon pixels only (root bbox 113,247..1212,520; settings page 249,237..1672,1048; no text
or clock pixel moves), maximum delta 91 levels on the focused icon.

Kept: the opaque material is now lit by the material's own vector. It costs 0.14 dB on the layer that matters,
inside the 0.5 dB / 0.004 budget, and the most plausible reason the fitted key scored slightly better is that the
firmware's glass adds an environment reflection (`tex2` rotated by `_EnvRot`) that the opaque stand-in has no
equivalent for; the invented left-bias key was standing in for that term. Brightness placement is unchanged for
the reason measured in the section above: `icon.brightness` scales RGB through `mix(.70, 1, b)` and alpha stays
coverage x opacity, so the captured state value (1.0 open column, 0.247/0.698 behind a page) keeps acting where
the stored reference layers agree with it.

Two variants were built and rejected:

- **MNU AMB folded into the opaque ambient** (`0.58 * dot(AMB, 1/3)` = 0.609 instead of 0.58, on top of the
  LIT1 direction) scored 20.67 / 0.8943 on the category icon row, -0.38 dB / -0.0007: reverted, the opaque
  ambient stays the fitted 0.58 because the AMB colour belongs to the glass ambient term, not to this level.
- **y-mirrored LIT1** (-0.0166, -0.3823, 0.9239), which would be the correct vector if the firmware's
  texture-space y were flipped relative to our uploaded normal map, scored 20.94 / 0.8957. It is also a wash and
  marginally better on SSIM, but there is no independent evidence for the flip and it would put the opaque path
  out of step with the glass path, which uses the unmodified vector: not adopted.

## Second size class (1.1022x): the view.cpp wiring

The unresolved pair is a pure uniform scale of the item pair, 85.554/77.616 = 1.10223 and 203.700/184.8 =
1.10227, and its focus ratio (2.381) is the item pair's, so it is neither a focus state nor a different glyph.
Neither position nor brightness moves with it: the 85.554 draws sit at `_Eye.x = -5` (screen 565, the open
column's focus x), on the exact 80 px row pitch (`_Eye.y` -500/-580/-660) and at Amb.w 1.0, and only `_Eye.w`
changes. In the native `_Eye.w` is `IconRenderer::Icon::size`, and a camera-distance change would drag the centres
toward the screen centre the way the 2857.2 parent column does, so this is a quad-size multiplier alone.

Rows above focus at the 80 px pitch mean a long scrolled list (the 16-row Settings page is the obvious candidate),
and level>0 pages are exactly what `itemIconSize` cannot express today: it ignores `level` (finding 5 above). Two
wiring options are consistent with the captures; both are `src/view.cpp` edits, outside this task's fence.

Wiring A - a level scale (correct if the 85.554 draws are a settled deeper page). The whole change is inside
`View::itemIconSize`, because both call sites already go through it (`view.cpp:925` theme icons, `view.cpp:930`
icon queue):

```cpp
double View::itemIconSize(int level,bool bullet,double focus) {
    const double base=bullet?35.721+(85.05-35.721)*focus:77.616+(184.8-77.616)*focus;
    return base*(level>0?1.10223:1.);
}
```

Wiring B - a page-entry scale on the incoming column (correct if the pair is a transition frame, which the 203.700
at an intermediate x suggests). `ahead` is already computed at `view.cpp:875`:

```cpp
const double entryScale=1.+.10223*std::clamp(ahead,0.,1.);
iconQueue_.push_back({entry.texture,{iconX,y},itemIconSize(level,bullet,focus)*entryScale,brightness,columnAlpha,2700+157.2*depth});
```

and the theme-icon quad two lines earlier (`view.cpp:925`) takes the same factor:
`const double quad=itemIconSize(level,bullet,focus)*entryScale;`.

Neither can be landed blind. The discriminating capture is a settled deeper page with a known selection and
depth (Game > All Games > Not Installed, decode `_Eye.w` for the focused row and for the rows above focus): if the
settled rows measure 85.554 the factor is a level scale (A); if only transition frames do, it is page-entry (B).
The bullet branch needs its own check either way: the settings-page focused bullet captures at 85.05 with no
1.1022 factor, so if A is chosen bullets may have to stay unscaled.