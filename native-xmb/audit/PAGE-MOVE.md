# Page-move duration and info-bar clock: Phase 4 trace notes

Written 2026-10-25 by phase4-motion. Scope: trace the five explore_plugin float constants behind the
"page move is 300 ms in paf mode 4" claim (HANDOFF-PHASES.md, commit dbee2de) and establish the info-bar
clock's update source/cadence. All artifacts are the local decrypted firmware modules under
`fw493/vsh_module/` (gitignored, read locally only); no firmware was fetched and no dump was added to git.

Artifacts inspected (SHA-256):

| File | SHA-256 |
|---|---|
| `explore_plugin.prx` (decrypted ELF) | `80b08ea439bf4fdb18999facd058cb468576ef8437bd9141332300836755c5b9` |
| `vsh.elf` | `54f6216dd498da243132377b830fa17c74cbe564ec8b8ebd40c0a2b071a525fa` |
| `basic_plugins.prx` | `29a7fec140c26b1b574fff6e05459935ec25498709a8efc08d5cc5da9c83bc3c` |

Decompiled listings used: `out/native-reference/explore_plugin_all.c`, `paf-range.c`,
`xmb_plugin_all.c`, `phase3-infobar-*.c` (all gitignored). New machine reads are reproducible with
`work/page-move-constants.ps1`, `work/find-bl-callers.ps1` and the repo's Ghidra headless scripts.

Summary: the five constants are exactly 300.0f, but they are content/dialog transition durations in
explore_plugin and are not identified as the XMB cross-bar page move. The page move belongs to paf's
PhXmBar in vsh.elf, which does contain a 0.3 s (300 ms) bar duration. The measured curve still selects
mode 4 over mode 5 at 300 ms, but the page move's own animate call was not located, so "300 ms in
mode 4" stays a fit with strong corroboration, not a traced call. The info-bar clock is a passive paf
PhClock widget written through an exported setter; its display resolution is one minute and no internal
ticker exists, but the periodic writer (and therefore its exact cadence) was not found in the analyzed
modules.

Updated 2026-10-26 (Phase 4b below): the clock is not a passive `PhClock`. It is a `PhText` named `clock`
under `page_xmb_indicator`, written through vshcommon export 0x33FB58 (section 3; `audit/INFO-BAR.md`).
The page-move statement above stands, and the later pass adds a frame-count discriminator between
mode 5 at 200 ms and mode 4 at 300 ms (section 1.4).

(Link this file from `native-xmb/HANDOFF-PHASES.md` when editing that file; this task's write fence
covered `native-xmb/audit/` and `work/` only.)

## 1. The five explore_plugin constants

`explore_plugin.prx` has two PT_LOAD segments; the first loads at vaddr 0 from file offset 0xF0, so file
offset = vaddr + 0xF0 for the data below. A 4-byte big-endian read at each address
(`work/page-move-constants.ps1 -Module explore ...`) gives:

| Symbol | vaddr | file offset | big-endian u32 | f32 |
|---|---|---|---|---|
| `DAT_00270674` | 0x00270674 | 0x00270764 | 0x43960000 | 300.0 |
| `DAT_002706c0` | 0x002706c0 | 0x002707b0 | 0x43960000 | 300.0 |
| `DAT_00270658` | 0x00270658 | 0x00270748 | 0x43960000 | 300.0 |
| `DAT_00270d30` | 0x00270d30 | 0x00270e20 | 0x43960000 | 300.0 |
| `DAT_00271d0c` | 0x00271d0c | 0x00271dfc | 0x43960000 | 300.0 |

All five are the identical float 300.0 (0x43960000), i.e. the "300 ms" value in the claim is correct as
a constant value. They sit in duration tables next to 200.0f entries:

- `DAT_00270660`..`DAT_00270670` (0x270660-0x270670) = 200.0, `DAT_00270674` = 300.0; the same block also
  holds 20.0 (`0x270650`, `0x270690`, `0x270698`), 1.0 (`0x270678`, `0x27067c` = 0.0), 300.0 at
  `0x270680`, and 0.0 fills.
- The second block `DAT_002706bc`/`DAT_002706c0` = 200.0 / 300.0, with `DAT_002706c4` = 0.0,
  `0x2706c8` = 1.0, `0x2706cc` = 100.0, and small curve values 0.2, 0.068, 0.01, 0.03 at 0x2706d0-0x2706dc.
- `DAT_00270d2c`/`DAT_00270d30`/`DAT_00270d34`/`DAT_00270d38` = 200.0 / 300.0 / 1.0 / 0.0.
- `DAT_00271d04`..`0x271d28`: 1.0, 30.0, 300.0, 0.0, 1.0, 1000.0, 0.0, 1.0, 0.0, 0.0.

The pattern (200.0 next to 300.0, with 1.0/0.0/100.0 in the same tables) is a duration block, not
geometry. The values are passed into paf animation-start wrappers as the timed float; paf's animation
setup converts that class of value to frames with `n = int(ms * 0.001 * 60)` (see section 3), so on the
call sites they read as 300 ms playback durations.

Traced call sites (grep of `explore_plugin_all.c`; enclosing function from the nearest `// FUN_` header):

| Constant | Call site | Enclosing function | What the call is |
|---|---|---|---|
| `DAT_00270658` | line 21778 `FUN_0002af44(300.0, fVar1, dialog, uVar5, 0)` | `FUN_0002dcd0` | file-delete dialog "deleted" state transition, 300 -> 0 |
| `DAT_00270674` | line 22008 `FUN_0002af44(200.0, 300.0, dialog, 0, 1)` | `FUN_0002e970` | file-delete dialog intro (immediate variant) |
| `DAT_00270674` | line 22041 `FUN_000144f4(300.0, view, 0x195)` | `FUN_0002e970` | starts paf animation id 0x195 on the delete-dialog view |
| `DAT_002706c0` | line 22555 `FUN_0002af44(200.0, 300.0, dialog, DAT_002c934c, 1)` | `FUN_000305c4` | file-delete dialog open animation |
| `DAT_00270d30` | line 50751 `FUN_00027c64(200.0, 300.0, 1.0, 0.0, 0.0, 1.0, obj, 0x1000003)` | `FUN_0008c598` | `user.info` dialog animation |
| `DAT_00271d0c` | line 92356 `paf_paf_33A47C59(1.0, 300.0, 0.0, item)` | `FUN_0017e7e4` | list-label / folder-item animation, 300 ms |

None of these functions is the XMB cross-bar page move. They are the file-delete dialogs, the
`user.info` dialog and a list/label update inside explore_plugin's content UI. So:

- Traced: the constants are 300.0f and are used as 300 ms animation durations.
- Not supported: that these particular constants are the page move's constants. All six traced call
  sites are content/dialog/list transitions. The attribution in HANDOFF-PHASES.md / dbee2de ("its 300 ms
  float constants are at ...") is unsupported by the call sites; explore_plugin also uses 300 ms for
  dialogs, which is the likely source of the guess. At minimum these constants cannot be cited as
  evidence for the page-move duration.

## 2. Where the page move actually lives

`audit/PIPELINE.md` "Menu motion" already establishes: the cross bar is the paf widget class `PhXmBar`
inside vsh.elf's paf (around 0x3ab000-0x3b1000), not explore_plugin; its left/right focus moves are
200 ms; explore_plugin reaches paf animations through exported wrappers.

Re-checked in vsh.elf this session:

- Focus-move durations: `DAT_0068f054` = 200.0 (used by the key handler `FUN_003af01c` -> `_opd_FUN_003ad3bc` /
  `_opd_FUN_003ad728`), and `DAT_0068f02c`, `0x68f034`, `0x68f040`, `0x68f044`, `0x68f048` are also 200.0.
- A genuine 300 ms bar duration exists: `DAT_0068f020` = 0x3e99999a = 0.3 s at vsh.elf file offset
  0x67f020. Its only use is `_opd_FUN_003accf4` (paf export NID `0xC94E8621`, `vsh_exports.csv`):
  for every PhXmBar item it calls `FUN_003b4310` for the focused index and
  `_opd_FUN_003b0620(0.3, item)` for all others. `FUN_003b0620` forwards to `FUN_003abc9c` with slot id
  0x1000016 and passes 5 in the argument slot that the sibling wrappers use for the paf acceleration
  mode (`_opd_FUN_003af114` chain, mode value at `+0x20` of the animation state).
- So firmware does have a 300 ms cross-bar animation, but it is a bar re-layout of the non-focused
  items, and in the decompiler its wrapper shows mode 5 - not mode 4. Whether the depth/page transition
  uses this same call is not established.

## 3. Re-derivation of the paf easing model and the 300 ms question

The paf easing model was re-read from `out/native-reference/paf-range.c` and matches PIPELINE.md:

- Setup `_opd_FUN_003981ec(durationMs, delayMs, state, mode)`:
  `n = (int)(durationMs * 0.001 * 60)` frames (0x68eaf8 = 0.001, 0x6fb1ec = 60.0),
  `frameSeconds = n * 0.0166667 * 0.999999` (0x6fb1e8, 0x68eafc), rate = 1 / frameSeconds;
  mode stored at state+0x20.
- Mode 4: k from a 14-entry table (0.9950, 0.9050, 0.7905, 0.6873, 0.6042, 0.5376, 0.4843, 0.4396,
  0.4016, 0.3704, 0.3431, 0.3195, 0.2990, 0.2809) for n = 1..14, and
  `k = 1 / (3.775 + 0.217 * (n - 15))` for n >= 15; per-frame growth 1.0001.
- Mode 5: k = 0.9615, 0.78125, 0.6369, 0.5291 for n = 1..4, then `k = 1 / (2.2 + (n - 5) / 3)`;
  per-frame growth 1.000001.
- Step `FUN_003984f8`: `v' = ((1 - v) * k + v) * growth`; at the frame-snapped duration the value is
  set to the target.

For the two candidate durations of the depth/page move (the RPCS3 capture in PIPELINE.md measured the
remaining distance falling by about 0.760 per frame, steadily, for 17-18 frames, with no snap at
frame 12 and a capture bias of about -0.014):

| Model | n frames | k | predicted remaining / frame | matches measured 0.760 over 17-18 frames? |
|---|---|---|---|---|
| mode 4, 300 ms | 18 | 1/(3.775+0.217*3) = 0.2259 | 0.7740, 0.5991, 0.4637, ... | yes: 0.774 - 0.014 bias = 0.760; 18 frames |
| mode 5, 300 ms | 18 | 1/(2.2+13/3) = 0.1531 | 0.8469, 0.7173, 0.6075, ... | no: far too slow per frame |
| mode 5, 200 ms | 12 | 1/(2.2+7/3) = 0.2206 | 0.7794 ... 0.0503, snap at frame 12 | no: 12 frames and a snap; measured has 17-18 frames |

So: 300 ms is the only paf duration whose frame count matches the measured 17-18 frames
(300 / 16.667 = 18), and at 300 ms only mode 4 reproduces the measured per-frame fraction; mode 5 at
300 ms is excluded by the curve (0.847 vs 0.760). The measured page move therefore confirms the
duration/curve pair "300 ms, mode 4" as a fit, and there is now a firmware 300 ms bar-duration constant
(0x68f020) consistent with it.

What is still not traced: the actual animation-start call issued for the depth/page move (the call that
would show mode 4 in the mode argument). No mode-4 animate call was located in the analyzed code; the
only 300 ms cross-bar call found passes 5 in the mode slot (possibly a decompiler artifact of the
float/int register split, not yet verified by disassembly). Confidence: duration 300 ms - high;
mode 4 - medium-high (curve fit plus firmware 300 ms constant, call site unproven).

The constants in section 1 do not raise this confidence: they belong to explore_plugin dialogs, not to
the cross bar. The page-move claim in HANDOFF-PHASES.md should be reworded as "fitted (RPCS3 curve) to
300 ms mode 4; explore_plugin's 300.0f constants are unrelated dialog durations" unless the page-move
call is found later.

## 4. Info-bar clock (superseded 2026-10-26 by Phase 4b section 3)

The `PhClock` identification below was wrong: the XMB clock is a `PhText` named `clock`. The text is
kept as the 2026-10-25 record.

Display (from the saved RPCS3 root capture `out/native-reference/ab-rpcs3-root.png`, cropped to the
info bar at x=1280..1920, y=40..160): the bar shows `25/10 12:32` followed by a small analog clock icon.
That is a day/month + hour:minute text with an analog icon - a one-minute display resolution.

Widget: the firmware clock is the paf class `Clock` (`PhClock`, vsh.elf paf, factory/class code around
0x3de988-0x3e0fd4; `objattribdef-ps3.ini` has a `[Clock]` object with two text children and several
child refs; `paf-range.c` has `PTR_s_PhClock_006fb25c`). It is constructed by paf export `0x7D594F19`
(`FUN_003de988`) and is created from the RCO page template, not by vsh hard-coding.

State, traced from `FUN_003dec98` / `_opd_FUN_003dedf4` / `_opd_FUN_003e0e5c` (`paf-range.c`):

| Field | Offset | Notes |
|---|---|---|
| year | +0x2cc (u16) | clamped to the parent page's min/max year (+0x298/+0x29c) |
| month | +0x2ce (u8) | clamped 1..12 |
| day | +0x2cf (u8) | clamped to days-in-month; leap-year aware via `FUN_003dec14` |
| hour | +0x2d0 (u8) | clamped 0..23 |
| minute | +0x2d1 (u8) | clamped 0..59 |
| second | +0x2d2 (u8) | clamped 0..59 |

Any change sets the dirty flag at +0x2c4 and recomputes the derived display state; `FUN_003dec14`
computes days-in-month from year + month (28/29/30/31, leap-year aware) so the day field is clamped
correctly. Date/time enters through paf export `0x1B75E85F` (`FUN_003dedf4`, takes a packed date/time
struct), and is read back through `0xBBFF3B2F` (`FUN_003debd8`, same struct), with `0xEA36F30F`
(`FUN_003df8c4`) as a refresh call.

Who updates it (traced import cross-reference from `rea/plugins/*_imports.csv` and `link_boot_set.csv`):

- `sysconf_plugin.prx` imports the setter `0x1B75E85F` (its Date and Time settings page) plus the
  getters/refresh; so the settings page is one writer.
- `explore_plugin.prx` and `explore_category_game/user.prx` import the getter `0xBBFF3B2F` and
  `0x8A65C27B`/`0x2293AB67`, i.e. content pages read the displayed clock back rather than calling
  `cellRtc` directly.
- vsh.elf's own import table (`rea/vsh_imports.csv`) lists no `cellRtc` and no `sys_time` module, so
  the paf clock widget itself does not read the RTC. A scan for the paf setter's OPD descriptor value
  (`0x006E0AC8`) found the value only in the export table, and a scan for the usual
  `addis`/`addi`/`bctrl` load-and-branch sequence produced no call site inside vsh.elf, so no direct
  in-vsh.elf caller of the setter was found (an indirect call through a computed pointer would not be
  caught by this check).
- `basic_plugins.prx` (which owns infobar initialization, `FUN_00007c7c`, per audit/INFO-BAR.md)
  imports `cellRtcGetCurrentClock`, `cellRtcGetCurrentTick`, `cellRtcGetTime_t`, `cellRtcSetTick`,
  `cellRtcTickAddMonths` and `sys_time_get_system_time`. Its three `cellRtcGetCurrentClock` call sites
  (`FUN_00027990`, `FUN_00032bec`, `FUN_0003b950`, found with `work/find-bl-callers.ps1 -Module basic
  0x464ec`) are game-stat timestamps (`"Game:Common.stat.timeLastUsed"`), a DB record update and a date
  string formatter - not the info-bar clock.

Conclusion for the clock:

- Traced: passive widget, 8-field date/time state, minute-resolution display in the reference capture,
  setter/getter exports and the importers above; no RTC access or timer inside paf, and no writer
  inside vsh.elf.
- Cadence: the clock's visible state only has minute resolution and the widget is push-driven, so the
  natural update is once per displayed minute; there is no 1 Hz/60-frame paf tick to inherit. The
  native implementation's existing "invalidate on the displayed minute" (audit/INFO-BAR.md) matches
  this and is therefore consistent, not contradicted.
- Still open: which module/event actually calls the setter with the running cellRtc time once per minute
  (or whether the page text is instead written through the clock's text children). The push driver was
  not found in the analyzed modules; treat the exact cadence source as unknown, with one-minute
  resolution as the strong constraint.

## 5. Open items and suggested next traces

1. Page move: find the depth/page transition call in xmb_plugin or vsh.elf's XMB page code and resolve
   the mode argument by disassembly (the decompiler's float/int split is not reliable). Only a call
   with mode 4 would convert the fit into a trace. Candidate API leaves: `FUN_00147e28` (mode as last
   argument in the decompiled calls), `FUN_003af114` / `FUN_003abc9c` (mode in vtable slot 0x13c
   argument), and the `paf_54D21B47` (`FUN_00399cc8`) wrapper used by both xmb_plugin and explore_plugin.
2. Info-bar clock: find the periodic writer. Search for importers of paf `0x1B75E85F` in the modules not
   analyzed here, for `cellRtcGetCurrentClock` call sites in vshmain/vshcommon code (0x34xxxx region,
   `phase3-infobar-common.c` shows the infobar animation helpers), and for text writes to the `Clock`
   object's two text children. The clock is created from `page_infobar`; tracking that page's update
   callback should reveal whether the refresh is driven per minute or per infobar change.
3. Do not replace the audio/video-derived numbers in this note with the dialog constants; they are
   different subsystems.

## 6. Reproduction

```
# five constants + neighbours
powershell -File work/page-move-constants.ps1 -Module explore 0x270640:24 0x2706b0:24 0x270d20:28 0x271cfc:12
# vsh.elf paf duration and easing constants
powershell -File work/page-move-constants.ps1 -Module vsh 0x68eaf0:36 0x68f018:64 0x6fb1e0:8
# call sites of the clock helper in basic_plugins
powershell -File work/find-bl-callers.ps1 -Module basic 0x464ec
# Ghidra decompiles (existing projects; -noanalysis keeps them read-only)
#   vsh.elf:        project ghidra_proj/vsh493,  program vsh.elf,
#                   DecompileAt.java  out.c  37e8e4 3766c4 3accf4 3b0620 3af114
#   basic_plugins:  project ghidra_work/boot493, program /boot/basic_plugins.prx (needs -recursive),
#                   DecompileAt.java  out.c  27990 32bec 3b950
```

Both machine reads and the Ghidra runs only read firmware-derived modules that are already local and
gitignored. No `.cpp`/`.h` file was modified; no git command was run.

---

# Phase 4b - 2026-10-26: page-move timing, open-page layout, clock writer

Second evidence pass over the same unknowns. Same write fence (audit files only), no source edit, no git
command. Scope: (1) is the open/close page animation a paf `PhXmBar` call at all, and if so with which ease
mode and duration; (2) the open-page layout (186 px faint pitch, icon-row y); (3) the periodic writer for the
information-panel clock. Answers 2 and 3 are appended to `audit/LAYOUT-TRACE.md` and `audit/INFO-BAR.md`;
this section carries all three answers so this file stays self-contained.

## 0. Artifacts and provenance for this pass

Firmware modules are unchanged from the first pass (same SHA-256, so the addresses below are comparable):
`vsh.elf` = `54f6216dd498da243132377b830fa17c74cbe564ec8b8ebd40c0a2b071a525fa`,
`explore_plugin.prx` = `80b08ea439bf4fdb18999facd058cb468576ef8437bd9141332300836755c5b9`,
`xmb_plugin.prx` = `553b3e212cb4b343f98f20f52664a4ffd8993b75318a003d892284b18f0462c9`. All under
`fw493/vsh_module/`, local only.

New evidence classes used here (all local, all read-only):

| Artifact | What it is | New use in this pass |
|---|---|---|
| `out/native-reference/rco-behavior/resources.json` | 122 resources / 8,070 objects / 0 errors, `animationEntries: 165` | RCO animation table (duration + accelMode are real RCO fields) and the object tree that names the XMB clock |
| `out/native-reference/{fpdump6,fpdump7,fp-dump2,...}` decoded `*.params.txt` (2,852 files) | per-draw `_Eye` / `_Ambiant` / `_Color` values decoded from RPCS3 `XMB_FP_DUMP` `.vc` captures | exact animated values (distance and brightness) instead of pixel scoring |
| machine `bl` scan of `vsh.elf` (PPC opcode 18, LK=1) | call-site search by branch target, not by decompiler symbol | who calls the clock setter / clock constructor / vshcommon writer; proves absences |
| `rea/vsh_exports.csv`, `rea/plugins/*_imports.csv` | export OPD table and per-plugin imports | NID -> VA for the clock and indicator exports |

The 0.1-0.2 px reading bias of the pixel tracker (PIPELINE.md "Measured on the real XMB") does not apply to
these decoded values: they are the constants the firmware passed to the shader, before any projection.

`src/view.cpp` citations in this section were re-checked at 2026-10-26 21:30 local; that file is being edited
by another session, so the quoted expressions (`childAmount*262`, `distance*186`, `10*std::clamp(...)`,
`290-10*focus`, `0.1*childAmount`) are the durable handle and the line numbers are a convenience.

## 1. Page-move timing: is the open/close page animation a PhXmBar call?

**Answer: not traceable with the artifacts available.** Two independent negative checks (the RCO animation
tables, and the PhXmBar duration slots present in the decompile) plus one new measurement that is too coarse
in time to settle a duration. What the artifacts do give now is the exact animated quantity and its start/end
values; only the ease mode and the duration remain open.

### 1.1 It is not an RCO animation

RCO animation resources carry exactly the two fields the question asks for: `duration` (float, ms) and
`accelMode` (int) - `out/native-reference/animattribdef-ps3.ini:8-13` (`MoveTo`), `:19-26` (`Recolour`),
`:28-34` (`Rotate`), `:36-42` (`Resize`), `:44-48` (`Fade`). The dump decodes both
(`durationRaw` / `accelModeRaw`, e.g. `resources.json` `gamelib_plugin.rco` offset 1132: `Resize`,
`durationRaw: 863`, `accelModeRaw: 4`).

- `vsh/resource/xmb_plugin_normal.rco` and `vsh/resource/xmb_ingame.rco`: **0 animation entries** each
  (26 objects each). The XMB page RCO authors no timed move at all.
- The 165 entries that exist are elsewhere: `custom_render_plugin.rco` 51 (cold boot), `premo_plugin.rco`
  30, `system_plugin.rco` 17 (button-label fades, 100 ms / mode 0), `impose_plugin.rco` 16,
  `gamelib_plugin.rco` 14, `friendim_plugin*.rco` 12 each, `bdp_plugin.rco` 5, `sacd_plugin.rco` 5,
  `npsignin_plugin.rco` 3. None targets the cross bar.
- Useful corroboration rather than evidence for the page move: mode 4 is a mode real RCO data uses
  (`gamelib_plugin.rco` `Resize` 863 ms `accelModeRaw: 4` at offset 1132; `Fade` 863 ms mode 3 at
  offset 1072), so a "mode 4 page move" is not implausible as an authoring choice - it is still unattested
  for the page.

### 1.2 It is not one of the PhXmBar duration slots that are in the decompile

Re-confirmed from `out/native-reference/paf-range.c`: the only 0.3 s bar duration in `vsh.elf` is
`DAT_0068f020` = 0x3E99999A (`work/page-move-constants.ps1 -Module vsh 0x68f020`, file offset 0x67F020).
Its single user is `_opd_FUN_003accf4` (`paf-range.c:26530`, export NID `0xC94E8621`), which calls
`FUN_003b4310` for the focused index and `_opd_FUN_003b0620(0.3, item)` for every other item
(`:26541`, `:26545`); `_opd_FUN_003b0620` forwards to `FUN_003abc9c(..., 0x1000016, ..., 5, ...)`
(`:28544`, `:28551`). So the 300 ms bar animation is a re-layout of the non-focused items, in the slot the
decompiler renders as mode 5, and not a page/depth transition.

Every other PhXmBar slot wrapper in the decompile also shows 5 in the mode slot
(`paf-range.c:27726`, `:27736`, `:27781`, `:27868`, `:28517`, `:28744`, `:28776`, `:30165`, `:31891`,
`:31901`, `:31911`, `:31972`, `:31986`, `:32000`, `:32014`, `:32445`, `:32447`, `:33340`), and
the only wrappers that do not are mode 0 (`:33379`, `:33381`, slot 0x1000023/0x1000027) and the ones with no
mode argument. **No mode-4 animate wrapper exists anywhere in the paf decompile**, and `explore_plugin.prx`'s
five 300.0f constants are already excluded (section 1). Note the caveat from the first pass still stands: the
`.opd` wrappers have mismatched prototypes in the decompile (e.g. `FUN_003abc9c` is declared with 7
parameters and called with 9, `paf-range.c:25881` vs `:28551`), so the *position* of the 5 is not proof; a
disassembly of one wrapper is still needed before "mode 5" can be asserted for these slots.

### 1.3 The animated quantity is now known exactly

The decoded per-draw values show the page transition moving the category bar itself: the focused category
icon's `_Eye.x` goes from **-5.00** (root, 9 draws at `_Ambiant.w` 1.000) to **+257.00** (page open, 3 draws)
in `out/native-reference/fpdump7` - a **262-unit shift**, which is the 262 the native already applies
(`src/view.cpp:839`, `level0X-childAmount*262`). So the 262 is no longer only capture-derived: it is the
difference of two decoded firmware draw constants. (Layout detail in `audit/LAYOUT-TRACE.md`, this pass.)

The same capture series also decodes the *return* transition (page close) frame by frame, in two independent
channels (`out/native-reference/fpdump7` and `fp-dump2`, identical values in both):

| Step | `_Eye.z` (distance) | Remaining to 2700 | `_Ambiant.w` | Remaining to 0.698 |
|---:|---:|---:|---:|---:|
| 1 | 2483.53 | 216.47 | 0.067 | 0.631 |
| 2 | 2504.75 | 195.25 | 0.129 | 0.569 |
| 3 | 2523.89 | 176.11 | 0.184 | 0.514 |
| 4 | 2541.16 | 158.84 | 0.235 | 0.463 |
| 5 | 2544.09 / 2544.23 / 2544.78 | 155.91 | 0.243 | 0.455 |
| settled | 2700.00 | 0 | 0.698 | 0 |

Per-step retention: distance 195.25/216.47 = 0.90198, 176.11/195.25 = 0.90197, 158.84/176.11 = 0.90193;
brightness 0.9017, 0.9033, 0.9008. Both channels agree on **0.9020** to better than 0.2%, and both sit at
the same step index (step 1 of an exponential approach from 0 brightness / from about 2460 units), which is
what makes it credible that these are consecutive frames even though the capture carries no timestamps.

Against paf's per-frame modes (k from `audit/PAGE-MOVE.md:107-117`, retention = (1-k)*growth):

| Model | k | retention/frame | measured 0.9020? |
|---|---:|---:|---|
| mode 4, 300 ms (n=18) | 0.22594 | 0.7741 | no |
| mode 5, 200 ms (n=12) | 0.22059 | 0.7794 | no |
| mode 5, 300 ms (n=18) | 0.15306 | 0.8469 | no |
| mode 5, n=29 (int(ms*0.001*60)=29, 483 ms) | 0.09804 | 0.9020 | yes, to 4 decimals |
| mode 4, solved for 0.9020 | 0.09809 | - | needs n = 44.6, not an integer frame count |

So the transition captured in these dumps is a **slower** ease than the 300 ms / mode 4 pair, and it is the
bar's distance and brightness (the page open/close of the bar), not the item-depth move that PIPELINE's pixel
tracker measured at 0.760/frame over 17-18 frames. Two readings are possible and the artifacts do not
discriminate: (a) the page move really is ~29 frames of mode 5 and the 0.760/frame measurement was of a
different, faster channel; or (b) these steps are not single frames (they would have to be 2.3 frames apart
for 0.902 to become 0.774, which the two-channel agreement makes unlikely but does not exclude). **Do not
quote 483 ms as the page-move duration from this table.** It is a measurement of one transition's retention,
with an unresolved frame-index assumption.

### 1.4 What capture would settle 200 ms mode 5 vs 300 ms mode 4

Pixel scoring cannot prove a duration - a settled frame scores the same whether it took 12 frames or 18, and
`work/ab-layers.ts` scores still frames only. The discriminator has to be a *timestamped* sequence of frames
across one page move.

Capture (RPCS3, `vsh.self`, per `HANDOFF-PHASES.md:89-108`): disable Exclusive Fullscreen in the scratch
config, map Cross to `PgDown`, CopyFromScreen at >= 100 fps (PrintWindow returns stale Vulkan frames), park
on a category with the console clock pinned so the background is static, then send **one** `PgDown` and
record about 1.5 s (approx. 150 frames) of the page opening - and a second run of the page closing (Back).
Analyse with `bun work/motion-track.ts <dir> horizontal x0 x1 250 330 <threshold> <maxShift>` over the
category-icon band, plus `bun work/region-luma.ts` on the same band for the brightness channel.

Measure: the per-frame remaining fraction r of the tracking channel, and the **frame index at which the value
stops changing**. The second number is the decisive one, for a reason worth stating precisely: both paf modes
are geometric decays with a *constant* per-frame retention that depends only on the frame count, so

- mode 5 at 200 ms (n=12) retains 0.7794 per frame and settles on frame 12;
- mode 4 at 300 ms (n=18) retains 0.7741 per frame and settles on frame 18.

Those two retentions differ by **0.7%**, which no pixel-fit can resolve and which even decoded `_Eye` values
would struggle to separate on 3-4 samples. The two candidates are therefore *degenerate in shape* and separate
only in **duration / frame count** (12 vs 18). That is the whole discriminating measurement, and it is also why
the existing 300 ms / mode 4 claim has to stand or fall on its recorded 17-18 frame count rather than on its
curve.

Reference numbers for the same channel:

| Candidate | retention/frame | frames to settle | settles at |
|---|---:|---:|---|
| 200 ms mode 5 (n=12) | 0.7794 | 12 | 200 ms |
| 300 ms mode 4 (n=18) | 0.7741 | 18 | 300 ms |
| 300 ms mode 5 (n=18) | 0.8469 | 18 | 300 ms |
| captured bar transition in 1.3 | 0.9020 | ~29 if 60 Hz | ~483 ms |

So the asking price is a burst with a *known* frame index per capture (frame counter or timestamp per PNG,
which `ab-shot.ts`-style captures do not currently record), covering one page open and one page close, plus a
note of the capture rate: a 12-frame ramp recorded at 40 fps would be mistaken for 18 frames. Sanity check on
the same burst: the per-frame ratio r should be *constant* across frames 1-3 (0.774 for mode 4 at 300 ms, 0.779
for mode 5 at 200 ms). A varying r in frames 1-3 means the capture mixed duplicate or dropped frames, and then
the frame count cannot be trusted either.

Because the native app can emit the same channel (`XMB_MOTION_LOG=1`, `HANDOFF-PHASES.md:107-108`), the same
frame count can be produced natively and compared like for like: 12 native frames vs 18 captured frames is a
clean, mechanical pass/fail.

If that capture is taken, also record `_Eye` directly (add a burst-length `XMB_FP_DUMP` window): the decoded
`_Eye.z` / `_Ambiant.w` pair is the cleanest possible record of the transition - it removes the pixel-tracker
bias and gives r to 4 decimals, as in 1.3 - but it still does not remove the need for a frame-indexed burst.

## 2. Open-page layout (summary; detail in `audit/LAYOUT-TRACE.md`)

- **Traced:** category pitch 200 and the +/-10 focus padding, from decoded `_Eye.x` (1005.00, 805.00,
  605.00, 405.00, 205.00, -5.00 focus, -215.00, -415.00, -615.00, -815.00, -1215.00).
- **Traced:** the icon-row y, as a draw constant: `_Eye.y` = -250.00 (unfocused) / -260.00 (focused), which
  is 540 + `_Eye.y` = 290 / 280. It is not an RCO attribute and not a scene transform. The
  "untraceable row-y" item in `audit/LAYOUT-TRACE.md:45` and `:105-107` is closed.
- **Not traceable:** the 186 px faint-category pitch. No decoded draw in the 2,852-file set places a
  category-size quad on a compressed pitch, and the only captured open-state depth (the bar at 2483-2544,
  i.e. *closer* than 2700) would make the projected pitch *wider* (200 x 2700/2483 = 217), not narrower.
  186 has no support in these artifacts and should be re-measured before it is cited again.

## 3. Information-panel clock writer (summary; detail in `audit/INFO-BAR.md`)

- The XMB clock is **not** a paf `PhClock`. It is a `PhText` named `clock` under
  `page_xmb_indicator -> indicator -> indi_adjust` in `xmb_plugin_normal.rco`
  (`out/native-reference/rco-behavior/resources.json`); the only five `Clock` objects in all 122 RCOs are
  in `sysconf_plugin.rco` (typeId 0x816, the Date and Time settings pages).
- The writer entry point is found: `xmb_plugin.prx` `FUN_0000cb44` (`out/native-reference/xmb_plugin_all.c:6249-6291`)
  looks `"clock"` up at `:6261`, type-checks it as `PhText` (`paf_B7DFCE90` returns
  `PTR_s_PhText_006fb214`, `paf-range.c:22496`; called at `:6266`), then calls
  `vshcommon_vshcommon_85AD7A58(clockText, state+0x18)` at `:6271`.
- That export is `vsh.elf` VA **0x33FB58** (`rea/vsh_exports.csv`: `vshcommon,0x85AD7A58,0x006DE098,0x0033FB58`).
  0x33FB58 is inside vshcommon (0x33xxxx) and is **not in any local decompile** - `paf-range.c` starts at
  0x3800b0, and the only vsh.elf code in the local set below that is seven dialog/infobar helpers at 0x3406F8,
  0x340BE0, 0x3415E8, 0x341870, 0x341BFC, 0x341D30, 0x343FFC (`phase3-system-layout.c:1,24,162,210,281`,
  `phase3-infobar-common.c:1,16,37`). A `bl` scan of `vsh.elf` finds **zero in-module callers** of 0x33FB58,
  so it is export-only and the per-minute behaviour is decided inside it.
- Therefore: the call chain is found up to the module boundary, and the periodic tick is inside a function
  that is missing from the artifact set. Obtaining it is a small, local, read-only job - see the reproduction
  notes in `audit/INFO-BAR.md`.

## 4. Reproduction for this pass

```
# decoded per-draw _Eye/_Ambiant tuples (no firmware read; reads the existing decode)
node work/item-icon-eyes.js out/native-reference/fpdump7
node work/item-icon-eyes.js work/item-icon-fp/decoded/fp-dump2
# paf bar duration constants
powershell -File work/page-move-constants.ps1 -Module vsh 0x68f018:0x50
# RCO animation entries with duration/accelMode
#   (read resources.json: resources[].animations[].durationRaw / accelModeRaw)
# PPC bl call-site scans (opcode 18, LK=1; target = va + sign_extended(LI<<2))
#   setter 0x3dedf4 -> 1 site (0x3e0d50, inside the clock constructor FUN_003e056c)
#   constructor 0x3de988 -> 4 sites (0x3a985c, 0x3c8dec, 0x3d2e74, 0x3d7c64)
#   vshcommon writer 0x33fb58 -> 0 sites
```

The scans read `fw493/vsh_module/vsh.elf` directly (PT_LOAD 0: file offset 0, vaddr 0x10000, size
0x6ABEA8) and write nothing. Firmware-derived numbers stay in these audit notes; no dump, module or capture
is committed.

---

# PAGE-MOVE phase 4c - 2026-10-27: timestamped page open/close burst

Second evidence pass on the 200 ms mode-5 vs 300 ms mode-4 question. Same fence: this file only, no source
edit, no git command. The new evidence is one 139.7 fps timestamped CopyFromScreen burst covering one page
open and one page close on the real 4.93 XMB. Short version: **18 rendered frames (about 300 ms) in both
directions; the screen-space per-frame remaining fraction is 0.762 (0.776 after the documented -0.014 tracker
bias), matching mode 4 at 300 ms; mode 5 at 200 ms (12 frames) is excluded by the frame count.**

## 1. Capture

- Same run and safety procedure as LAYOUT-TRACE phase 4c: `C:\dev\rpcs3-cap\rpcs3.exe`, scratch config,
  exclusive fullscreen disabled, 1920x1080, Keyboard pad (Cross PgDown, Circle PgUp). The global input config
  was backed up before the run and restored after it; `C:\dev\rpcs3-cap\config\input_configs\global\Default.yml`
  had SHA-256 `E624DA0D66A8E971173C39A69A1DC2C89FFE146D57BF2A03C2C8CFACB459CEEE` before and after.
- Parked on Settings -> Game Settings (the row that opens the PS-Upscaler page), root state.
- Burst: **642 frames in 4,588.7 ms = 139.7 fps**, band 1920x180 at screen y 200..380, full 1080p window,
  BitBlt into a 32bpp DIB with a Stopwatch timestamp per frame, written raw and converted to `f_<ms>.png`.
  Key 1 (PgDown, open) at 701.266 ms; key 2 (PgUp, close) at 2702.865 ms.
- Files under `C:\dev\out\rpcs3-xmb-cap\` (never committed): `burst-open-close.raw` (846 MB),
  `burst-png\` (642 PNGs), `template.csv` (sub-pixel template track), `track.txt` / `track-narrow.txt`
  (`motion-track.ts`), `luma.txt` (`region-luma.ts`).
- A second burst was discarded: its frames showed the native XMB window in front of RPCS3, so the desktop
  capture was occluded. Every number below comes from the one valid burst.

## 2. The tracked quantity and the two transitions

The clean channel is a normalized correlation of the focused toolbox (template from the root frame: x 500..631,
y 240..321, 1 px search step, parabolic sub-pixel). Root is 0 and the settled open page is -261.9 px.

Open (press at 701.3 ms):
- first visible motion at 1410 ms, i.e. **709 ms after the press** (page/content load; the earlier record was
  about 530 ms);
- the first visible frame is already at -100.1 px (38% of the move), then the remaining distance falls
  123.4, 94.0, 71.5, 54.5, 41.6, 31.7, 24.1, 18.4, 14.0, 10.7, 8.1, 6.2, 4.8, 3.7, 2.8, 2.1, 1.7 px;
- per-frame remaining fraction, frames 2..16: **0.7623 +/- 0.0043**;
- within 1% of the target after 16-17 rendered frames and within 0.5% after 18; the last 1-2 px take another
  100-200 ms (sub-pixel tail).

Close (press at 2702.9 ms):
- first motion at 2760 ms, i.e. **57 ms after the press**;
- remaining 199.7, 151.9, 115.8, 88.2, 63.0, 45.2, 32.7, 23.9, 17.3, 12.6, 9.6, 7.25, 5.58, 4.19, 3.18, 2.43,
  1.90, 1.38 px;
- the first four fractions are 0.7623, 0.7608, 0.7624, 0.7616 (**0.7618 +/- 0.0007**); later frames read
  0.715-0.730 and then 0.75-0.78;
- within 1% after 16 frames and within 0.5% after 18.

Cross-checks with the project tools:
- `bun work/motion-track.ts burst-png horizontal 0.0 1.0 0.0 1.0 150 40` tracks a 0 -> 200.14 px compromise
  for the open and 200.14 -> -21.57 for the close, because the profile mixes the focused toolbox, the fading
  neighbouring icons and the moving "Settings" label. The narrow region (x 0.12..0.40) gives 0 -> 201.35 and
  203.26 -> -19.45. Both versions put the first motion at 1410/2760, confirming the start frames, but neither
  tracks the full 262 px, so the absolute distances and retentions above use the template track.
- `bun work/region-luma.ts burst-png 0.34 0.99 0.0 0.70` confirms the same windows on the brightness channel:
  open falls from 55.26 to 48.82 (first drop at 1410, about 90% by 1557), close rises from 47.58 to 53.96
  (first rise at 2760, about 90% by 2977), with a slow tail after that.

## 3. Which ease the numbers match

| Model | frames | retention/frame | measured against it |
|---|---:|---:|---|
| mode 4, 300 ms | 18 | 0.7741 | screen 0.762; +0.014 bias = 0.776; 18 frames |
| mode 5, 200 ms | 12 | 0.7794 | 12 frames would end at 200 ms; at 200 ms both transitions are still 6-7 px (2.4-2.8%) out and keep moving |
| mode 5, 300 ms | 18 | 0.8469 | retention far too slow |

- The 0.7% retention gap between the two candidates is below this measurement's resolution: the screen x also
  contains the slower distance/brightness channel recorded in phase 4b 1.3 (retention 0.902, about 29 frames),
  which explains the varying screen retention (0.762 early, 0.72 mid-tail, 0.75-0.78 late) and makes the
  "pure" icon-x retention uncertain by about 1%.
- The **frame count** is the discriminator: at 200 ms after first motion the open icon is at -255.7 (6.2 px
  out) and the close at -7.25 (7.25 px out); neither has snapped. A 12-frame mode-5 ease would be at the
  target. At 300 ms the open is at -260.8 (1.1 px out) and the close at about -1.5 (1.5 px out).
- So the page move is 18 rendered frames at 60 Hz = 300 ms, and the retention is within bias of mode 4 at
  300 ms (0.7741). The older pixel-tracker result (0.760, 17-18 frames) is independently confirmed by this
  timestamped burst.

## 4. Honest limits

- One valid burst only. The second attempt was occluded and discarded; there is no second independent frame
  count.
- Screen x is a projection of the eye-space animation, not the animation value itself. The distance/brightness
  channel (phase 4b 1.3) adds a slower component: it speeds up the close mid-tail and slows the open tail, so
  the per-frame fraction is not a clean constant and the mode-4/mode-5 retention difference cannot be settled
  from the curve alone.
- The first open frame is 38% of the move rather than the 22.6% a mode-4 first frame gives; the close has no
  such first-step jump. This is either a layout snap when the page opens or 1-2 frames hidden by the page load.
  It is unresolved and excluded from the retention fit. It does not change the 18-frame end point.
- The 709 ms open delay is page-load dependent; it is not part of the 300 ms animation. The close delay (57 ms)
  is consistent with the previously recorded 34 ms plus capture granularity.
- The settle threshold is arbitrary: "within 0.5%" is 1.3 px on a 262 px move, which is at the template
  tracker's noise floor. The conclusion rests on the 200 ms point being 6-7 px out, not on the exact final
  pixel.

## 5. Answer

The page open and page close are **18 rendered frames, about 300 ms at 60 Hz**. The screen-space per-frame
remaining fraction is 0.762 (0.7618 over the clean first four close frames), which becomes about 0.776 after
the documented -0.014 tracker bias and is within measurement error of mode 4 at 300 ms (0.7741). Mode 5 at
200 ms (12 frames, 0.7794) is excluded by the frame count: at 200 ms both transitions are still 6-7 px from
the target. The earlier 300 ms / mode 4 fit stands; this burst is a direct frame-count confirmation, not a
trace of the animate call. The call-site trace in vsh.elf paf `PhXmBar` remains the open item.

## 6. Reproduction

```
# raw burst: C:\dev\out\rpcs3-xmb-cap\burst.ps1 (BitBlt into a 32bpp DIB, timestamp per frame, band 1920x180)
# convert: raw2png.ps1; track: template-track.ps1 / analyze.ps1; pitch: pitch.ps1 / diffpitch.ps1
# project tools: bun work/motion-track.ts ... and bun work/region-luma.ts ... over burst-png
```

Firmware-derived numbers stay in these audit notes; no dump, module or capture is committed.
