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

---

# PAGE-MOVE phase 14.1 - 2026-10-27 (late): the bar animate call, read at the instruction level

Third pass on the same question, and the first one that reads the machine code instead of the
decompiler. Fence: this file plus two new helpers under `work/` (`page-move-scan.ts`,
`page-move-listing.ps1`). No `src/` edit, no git command, no RPCS3. Modules are the same local,
gitignored 4.93 files as before: `vsh.elf` sha256 `54f6216dd498da24...a525fa`,
`xmb_plugin.prx` sha256 `553b3e212cb4b343...0462c9`, both re-hashed this pass and both identical to
the hashes recorded in this file.

Headline: **the only 0.3 s constant in the paf bar code is not a duration.** `DAT_0068f020` is loaded
into `f1` of `_opd_FUN_003b0620` and that function uses `f1` as the *target value* of the animated
property, not as the duration. The duration is the *second* float argument, and the plugin's own bar
driver feeds **200 ms**, not 300 ms. The mode literal in every bar animate call is **5**; there is no
mode-4 animate call anywhere in paf. `pageMilliseconds=300, mode 4` is therefore **contradicted as a
firmware value** and remains a *fit to the capture* only - and the capture's 18-frame count now
disagrees with the traced 200 ms, which is a new, specific open question rather than the old
mode-4-versus-mode-5 one.

## 0. Method, and what Ghidra did to the project

Two evidence classes, both local and read-only:

| Tool | What it produced |
|---|---|
| Ghidra 12.1.4 headless, project `ghidra_work/vsh493w`, program `vsh.elf`, `-process -noanalysis -readOnly`, scripts `ghidra_scripts/ListRange.java` and `ghidra_scripts/DecompileAt.java` | instruction listings of 0x3981EC, 0x3ABC9C, 0x3AC980-0x3ACD00, 0x3ACCf4, 0x3AD3xx, 0x3AF1E4-0x3AF320, 0x3B0600-0x3B06A0; decompile of 0x147E28 |
| `work/page-move-scan.ts` (new), `work/elf-const.ts` | OPD descriptor lookup, word cross-reference scans, constant reads from `vsh.elf` and `xmb_plugin.prx` |
| `rea/vsh_exports.csv`, `rea/plugins/xmb_plugin.prx_imports.csv` | NID -> export VA, and which paf exports `xmb_plugin` actually imports |
| `out/native-reference/rco-behavior/resources.json` | animation-entry counts per RCO |

**The Ghidra project was not saved and not modified.** The project was opened with `-readOnly`, the
headless logs contain no `Saving`/`save program` line, and the newest file under
`ghidra_work/vsh493w.rep/user/` still carries its 2026-10-21 timestamp. `-noanalysis` means the
existing analysis was used as-is; every range listed is inside a function the project already
defined, and `DecompileAt` found `FUN_00147e28` already a function, so no function was created. The
only non-instruction bytes encountered were the mode-4 jump table at 0x398390-0x3983A0, which
`ListRange` printed as `?? 38`/`?? 44`/... rather than decoding; even if a script had touched the
program, `-readOnly` means no change could be persisted. No firmware-derived file was written into
the repo - all listings went to `%TEMP%\p14-1` / `%TEMP%\page-move-14.1`.

## 1. Two identical trampolines: how a paf animation actually starts

`FUN_003abc9c` (0x3ABC9C) and `FUN_003af114` (0x3AF114) are byte-for-byte the same idea; both are paf
internal entry points that all the exported wrappers funnel through. The listing of 0x3ABC9C is
short enough to quote whole:

```
003abc9c: or r11,r3,r3        ; r11 = object
003abca0: mfspr r0,LR
003abca4: stdu r1,-0x70(r1)
003abca8: std r0,0x80(r1)
003abcac: or r5,r7,r7         ; forwarded arg3 = incoming r7
003abcb0: or r7,r9,r9         ; forwarded arg5 = incoming r9
003abcb4: lwz r9,0x0(r11)     ; r9 = object->vtable
003abcb8: or r6,r8,r8         ; forwarded arg4 = incoming r8
003abcbc: lwz r9,0x13c(r9)    ; r9 = vtable[0x13c]  (an OPD descriptor)
003abcc0: lwz r0,0x0(r9)      ; r0 = descriptor->code
003abcc4: std r2,0x28(r1)
003abcc8: mtspr CTR,r0
003abccc: lwz r2,0x4(r9)      ; r2 = descriptor->toc
003abcd0: bctrl
```

So the animate call is `object->vtable[0x13c](object, arg2, arg3, arg4, arg5, f1..f8)` where this
trampoline **forwards r3, r4, r7, r8, r9 and drops r5, r6**, and passes every float register through
untouched. `FUN_00147e28` (decompiled this pass) is a fourth instance of the same shape:

```c
uVar1 = (*(code *)**(undefined4 **)(*param_1 + 0x13c))(param_1,param_2,param_5,local_10,in_stack_0000007c);
```

That is what page 4's open item asked for ("resolve the mode argument by disassembly"): the argument
*positions* are now known exactly, and they are the ones the decompiler was guessing at. The five
integer arguments by position are `(state, propertyId, mode, targetValue4f, flag)`; the floats are
`(duration, delay)` plus up to four more.

## 2. The animation setup's own ABI (0x3981EC), from the machine code

`_opd_FUN_003981ec`, the paf animation setup, was listed and it fixes the register model that this
file's section 3 had only inferred from the decompile:

```
003981f8: lfs  f0,-0x1508(r9)  ; [0x68eaf8] = 0.001
003981fc: fmuls f2,f2,f0       ; f2 = delayMs  * 0.001
00398204: fmuls f1,f1,f0       ; f1 = durationMs * 0.001
00398210: stfs f2,0x18(r3)     ; state->+0x18 = delay (seconds)
00398214: stfs f1,0x4(r3)      ; state->+0x04 = duration (seconds)
0039822c: stw  r4,0x20(r3)     ; state->+0x20 = MODE  <- r4
00398230: lfs  f12,[0x6fb1e8]  ; 1/60
00398238: lfs  f13,[0x6fb1ec]  ; 60.0
00398240: fmuls f0,f0,f13      ; frames = duration_s * 60
00398244: fctiwz f0,f0         ; int()
...
00398290: lwz  r0,0x20(r3)     ; switch on the mode
00398294: blelr (r0 <= 3)      ; modes 0-3: no curve set
003982a4: cmpi r0,5 -> formula k = 1/(2.2+(n-5)/3) for n>4
00398354: cmpi r0,4 -> table of 14 k values for n=1..14
```

So `setup(f1 = durationMs, f2 = delayMs, r3 = state, r4 = mode)`, frames = `int(ms*0.001*60)`, and
the ease constants are exactly the ones this file derived in section 3. **The duration is `f1` and the
mode is the fourth integer argument.** That is the one piece page 4b called "possibly a decompiler
artifact of the float/int register split"; it is not an artifact, and the 5 in the wrappers is a real
immediate (`li r7,0x5`), not a mis-placed argument.

## 3. Candidate call sites

Every site below is a paf wrapper that starts or retargets a `PhXmBar` item animation. "Duration arg"
is the float position that reaches `setup`'s `f1`; "mode" is the integer position that reaches its
`r4`. Addresses are `vsh.elf` VAs; "plugin line" is `out/native-reference/xmb_plugin_all.c`.

| # | Call site (VA, symbol) | paf export NID | Reached from | Object | Property id | Mode | Duration arg | Delay arg |
|---|---|---|---|---|---|---|---|---|
| 1 | `_opd_FUN_003b0620` @ 0x3B0620 | 0xF35FE0BC | `xmb_plugin` `FUN_00002838` (decompile line 2141); `_opd_FUN_003accf4` | one bar entry (vector at `bar+0x294`) | 0x1000016 | **5** (`li r7,0x5` @0x3b0670) | **2nd float** | 3rd float |
| 2 | `_opd_FUN_003accf4` @ 0x3ACCF4 | 0xC94E8621 | no importer, no in-module `bl` | every bar entry | 0x1000016 (non-focused) | 5 | caller's `f1`; focused entry instead goes to `FUN_003b4310` | 0.0 |
| 3 | `_opd_FUN_003acae4` @ 0x3ACAE4 | 0x7BAAD6D1 | `xmb_plugin` lines 1929, 1988, 2000, 3183 | every bar entry | 0x1000003 | 5 | 2nd float | 3rd float |
| 4 | `_opd_FUN_003acafc` @ 0x3ACAFC | 0x62ED555B | `xmb_plugin` lines 1828, 1857, 1881, 1956 | every bar entry (focused gets 1.0, others 0.0 in the flag slot) | 0x1000003 | 5 | 2nd float | 3rd float |
| 5 | `_opd_FUN_003ac79c` @ 0x3AC79C | 0x5837B7E2 | `xmb_plugin` lines 1823, 1950, 2046, 2050, 2103 | every bar entry | 0x1000015 | 5 | 2nd float | 3rd float |
| 6 | `FUN_003ac6dc` @ 0x3AC6DC | - (internal) | `paf` | every bar entry | 0x1000018 | 5 (`li r7,0x5` @0x3af1f0) | 2nd float | 3rd float |
| 7 | `_opd_FUN_003ac524` @ 0x3AC524 | 0xDDE862B5 | `xmb_plugin` `FUN_00002804` -> `paf_paf_DDE862B5(DAT_00017f4c = 0.0, ...)` | focused bar entry only | 0x52 (a different dispatch inside `FUN_003b069c`) | 5 | 2nd float | 3rd float |
| 8 | `_opd_FUN_003ac5c0` / `_opd_FUN_003ac668` @ 0x3AC5C0 / 0x3AC668 | 0xEC7C4469 / 0x8846BC65 | `xmb_plugin` lines 2047, 2097 (0xEC7C4469) | one bar entry (index argument) | - (`FUN_003b07a8`) | 5 | 2nd float | 3rd float |
| 9 | `_opd_FUN_003ad3bc` / `_opd_FUN_003ad728` @ 0x3AD3BC / 0x3AD728 | 0xF744FE00 / 0xBC8DD07D | `paf` key handler `FUN_003af01c` (paf-range.c:27662, :27668) | bar focus index | - | - | `DAT_0068f054` = **200.0 ms** (constant) | - |

Reading of row 1, which is the important one, from the listing at 0x3B0620:

```
003b062c: fmr f30,f3          ; save delay
003b0634: fmr f31,f2          ; save duration
003b0654: bl  0x0042cbe0      ; fills a 4-float block with f1 (f1 untouched)
003b0658: lis r4,0x100
003b0660: fmr f1,f31          ; f1 = saved 2nd float  -> duration
003b0664: or  r3,r28,r28      ; r3 = the bar object
003b066c: ori r4,r4,0x16      ; r4 = 0x1000016 (property id)
003b0670: li  r7,0x5          ; r7 = 5  -> forwarded as r5 -> the mode
003b0674: li  r9,0x0
003b0678: bl  0x003abc9c
```

`_opd_FUN_0042cbe0(double value, float *out)` writes `value` into `out[0..3]` (see
`phase3-infobar-common.c:101`), so the incoming `f1` becomes a **four-component target value**
`{f1,f1,f1,f1}`, not a duration. That is the fact that invalidates the 0.3 s reading.

## 4. Correction to section 2 of this file: `DAT_0068f020` is not a 300 ms duration

Section 2 of this file says `DAT_0068f020` (0.3 s) is "a genuine 300 ms bar duration" whose "only use
is `_opd_FUN_003accf4`, which calls `_opd_FUN_003b0620(0.3, item)`". The call is real; the
interpretation is not. At 0x3ACD50-0x3ACD6C the non-focused branch is:

```
003acd50: bl   0x003abd20          ; r3 = &bar->items[i]
003acd54: lis  r9,0x69
003acd58: fmr  f2,f31             ; f2 = the export's own f1
003acd5c: lwz  r3,0x0(r3)         ; r3 = the bar object
003acd60: lfs  f3,-0xfdc(r9)      ; f3 = [0x68f024] = 0.0
003acd68: lfs  f1,-0xfe0(r9)      ; f1 = [0x68f020] = 0.3
003acd6c: bl   0x003b0620
```

and in `_opd_FUN_003b0620` that `f1` is only ever consumed by `bl 0x0042cbe0` - the 4-float target
block - while the duration slot is filled from `f2`. So in this call:

* **0.3 is the target value** of property 0x1000016 for every non-focused bar entry (a plausible
  dim/scale target: the same call family passes 1.0 for the focused entry);
* **the duration is the caller's `f1`**, i.e. supplied by whoever calls export 0xC94E8621.

And nobody does: `0xC94E8621` is not imported by any plugin in `rea/plugins/*_imports.csv`
(`xmb_plugin`, `explore_plugin`, `explore_category_*`, `system_plugin` set included), and a `bl`
scan of `vsh.elf` finds no in-module caller of 0x3ACCF4. So row 2 of the table is
**export-only and uncalled** in the artifacts at hand, and it cannot be the page move. The 300 ms
"corroboration" that section 3 of this file recorded has to be withdrawn:

> Retracted: "there is now a firmware 300 ms bar-duration constant (0x68f020) consistent with it."
> 0x68f020 is a target value inside an uncalled export, not a duration.

## 5. What the plugin actually feeds the bar: 200 ms

`xmb_plugin.prx` imports exactly the wrappers in rows 1, 3, 4, 5, 7 and 8, plus the bar accessors
(`0xC1A00166` -> 0x3ABD58 item lookup, `0x796A32FE` -> 0x3B456C focused index, `0x703117AD` ->
0x3ABCF8 `PhXmBar` class). Its bar driver is `FUN_00002838` @ 0x2838 (`xmb_plugin_all.c:1674-2223`):

```c
1786:  dVar23 = (double)*(float *)(DAT_0001beac + 0x520);   // the bar animation duration
1823:  paf_paf_5837B7E2((double)DAT_00017f50 /*1.0*/, dVar23, 0.0, piVar2);
1828:  paf_paf_62ED555B((float)(dVar23 * DAT_00017f78 /*1.2*/), 0.0, piVar2);
1929:  paf_paf_7BAAD6D1(dVar23, piVar2);
2141:  paf_paf_F35FE0BC(dVar18, dVar23, (double)DAT_00017fb8 /*0.0*/, iVar10);   // per item
```

`state+0x520` has exactly two writers in the whole plugin, both in `FUN_00003ab0` @ 0x3AB0:

| Site | Value | When |
|---|---|---|
| `xmb_plugin_all.c:2249` and `:2253` | `DAT_00017fbc` = **200.0** | the transition-refcount at `state+0x534` falls to 0 -> the bar is re-laid-out |
| `xmb_plugin_all.c:2262` | `0.0` | the refcount rises from 0 -> the re-layout is a snap |

`xmb_plugin.prx`'s float block at VA 0x17F40-0x17FDC (file offset = VA + 0xF0) holds
`1.0, 0.5, 1.1, 10.0, 1.5, 3.0, 380, 500, 240, 1.2, 1.25, 1.8, 24, 0.2, -5, 0.3, 0.7, 0.4, 0.64,
0.6, 0.1, 0.05, 0.25, 0.9, -0.5, 0.0, **200.0**, 1400, 0.0, 3000, 0.0, 2000, 100, 200, 200`. There is
**no 300.0 anywhere in xmb_plugin**, and 200.0 appears three times (0x17FBC, 0x17FD8, 0x17FDC).

The refcount pair is the page-change hook:

* `FUN_00003bd0(state, 1)` (bar becomes inactive) is called from `FUN_0000805c` @ 0x805C, which is
  called from the page/view switch driver `FUN_00008e38` @ 0x8E38 (`xmb_plugin_all.c:4532`) when a
  newly activated page object has level 0 or 3. That path sets the duration to 0 first (snap) and
  then, when the refcount returns to 0, arms **200 ms** and re-runs the whole bar layout.
* `FUN_00003bd0(state, 0)` is called from `FUN_00007dec` @ 0x7DEC - the counter-release path.

So the firmware's own bar re-layout that runs on an XMB page change is **200 ms**, using wrappers
whose mode literal is **5**. There is no 300 ms figure and no mode-4 call in the entire bar path.

Supporting detail, not proof: in `PhXmBar`'s default-attribute block (the class name at 0x68F080,
defaults 0x68F090-0x68F1E8, which is the block `audit/GEOMETRY-ROOT.md:587` identifies as the one the
widget code actually reads) the only pair that looks like duration + value is `200.0` at 0x68F1A8
followed by `0.3` at 0x68F1AC - the same two numbers the plugin uses. Neither word is read by any of
the functions listed above, so this is a coincidence of the defaults table, not evidence.

## 6. RCO animations: checked, and empty for every XMB surface

Re-read from `out/native-reference/rco-behavior/resources.json` this pass (13 resources, animation
entry counts):

`explore_category_friend/game/music/network/photo/psn/sysconf/tv/user/video.rco`,
`explore_plugin_full.rco`, `xmb_ingame.rco`, `xmb_plugin_normal.rco` - **0 animation entries each**
(24-87 objects each). So the depth transition is not an RCO-authored animation on any page surface,
which confirms and extends page 4b section 1.1 (that pass had only checked the two `xmb_*` RCOs).
Combined with section 5 above, the page transition is a code-driven `PhXmBar` animation, and every
code-driven bar animation carries mode 5.

## 7. Conclusion

**Duration: traced, and it is 200 ms, not 300 ms.** The value reaches the setup from
`state+0x520` (200.0, `xmb_plugin` VA 0x17FBC) through the second float argument of the bar
animators; the only 0.3 s constant in paf is a target value in an export that nothing calls.

**Mode: literal 5, everywhere; no mode-4 call exists.** Every one of the nine sites above is either
`li r7,0x5` in the wrapper or passes `DAT_0068f054` with a 200 ms duration. Mode 4 is implemented in
the setup's curve table but no caller in paf or in either XMB plugin selects it. What is *not* fully
proven is that the 5 is the ease mode rather than a flag, because of one remaining hop:

> **The one open hop.** The only word in `vsh.elf` that points at the animation handler's OPD
> (`0x6DF3A0` -> `0x3981EC`) is at `0x63D40C`. If the handler called by the trampolines is entered
> directly, its `r4` is `0x1000016` (the property id) and the property id would land in
> `state+0x20`, the field the ease switch reads - which cannot be what shipping code does. Either
> (a) `0x63D2D0` is not the vtable base and that word is a table entry for a different class, or
> (b) one intermediate function sits between the trampoline and 0x3981EC and moves `r5` into `r4`.
> The artifacts at hand do not decide between them. What they do decide is that the literal is **5**
> and that no **4** appears.

**Is `view.cpp` `pageMilliseconds=300, mode 4` confirmed, contradicted, or still a fit?**

| Claim | Status after this pass |
|---|---|
| 300 ms duration | **CONTRADICTED as a firmware value.** The firmware bar duration is 200 ms; the 0.3 s constant is not a duration. 300 ms survives only as a fit to the phase 4c capture |
| mode 4 | **CONTRADICTED as a firmware value** for the bar path: no mode-4 animate call exists, and every bar call carries 5. (The one-hop caveat above means "5 is the mode" is not proven, but "4" is not present at all) |
| the pair as a description of the console | **STILL A FIT**, and now a fit that disagrees with the firmware on both numbers |

**The new conflict, stated precisely.** Phase 4c counted **18 distinct motion steps** at 139.7 fps on
a 60 Hz animation and matched them to `int(300*0.001*60) = 18` frames. A 200 ms animation is
`n = int(200*0.001*60) = 12` frames, and at 139.7 fps a 12-frame move would present about 12 distinct
steps, not 17-18. Two readings remain, and this pass cannot separate them:

1. the captured channel is a composite - the 12-frame item move plus the slower bar recede recorded
   in page 4b 1.3 (retention 0.902, ~29 frames) - and the extra 5-6 steps are the second channel;
2. the console's page transition is not the `FUN_00002838` bar re-layout that this pass traced, and
   some other path (uncalled export 0xC94E8621, or a module not analysed) drives it at 300 ms.

Reading 2 has no supporting call site left - export 0xC94E8621 has no caller and no importer - so
reading 1 is the better hypothesis, but "one frame-indexed burst that decodes `_Eye` as well as
screen x" is what would close it. That is a capture, not a code read, and it is out of this pass's
scope (another agent owns the rig).

## 8. What the native code should change

Nothing is edited by this pass. If the parent decides to land the change, it is two constants and one
easing mode in `src/` (both currently owned by other passes):

1. `native-xmb/src/view.h:46` - `pageMilliseconds` 300 -> **200**.
2. `native-xmb/src/view.cpp:684` - `depthEase_.retarget(shell_.depth(),pageMilliseconds,4)` -> mode
   **5** (the `PafEase` default, already used by `moveMilliseconds`).

The mode-5 curve the native already implements is the one the firmware setup uses for every bar
animation: `n = int(ms*0.001*60)` = 12 at 200 ms; `k = 0.9615, 0.78125, 0.6369, 0.5291` for frames
1-4 and `k = 1/(2.2+(n-5)/3)` after that; per-frame growth 1.000001; per-frame retention
`(1-k)*growth` = **0.7794**, settling on frame 12. The mode-4 curve currently in use
(`k = 1/(3.775+0.217*(n-15))` at 300 ms, retention 0.7741) is 0.7% away in shape and 6 frames away in
length, which is exactly the distinction the phase 4c frame count was supposed to settle.

**Recommendation:** land the *documentation* change now (this section) and hold the two constants
until either (a) the one open hop above is closed - the exact command is section 9 - or (b) the
composite-channel reading of 4c is confirmed by a burst that decodes `_Eye.x` and `_Eye.z` per
frame. Landing 200 ms mode 5 on the firmware evidence alone would make the app 100 ms shorter than a
capture that was measured three times; landing nothing keeps a number this pass has now shown to be
unsupported on both of its halves.

If the intent is instead "match the capture, document the firmware", then the honest form is to keep
300 ms / mode 4 and relabel it `fit` (the phase 14 exit criterion allows a fit with a frame count),
deleting the 0x68f020 citation, which no longer supports it.

## 9. The precise gap, and the command that closes it

**Missing artifact:** the prototype of the vtable handler that the trampolines call, i.e. of
`_opd_FUN_003981ec` as *called through* `0x63D40C`, and the identity of the vtable base that owns that
slot. Everything else on the chain is read.

**Exact local commands** (read-only, nothing saved; the project is `ghidra_work/vsh493w`):

```
# 1. the handler slot and the words around it (OPD word at 0x63D40C -> 0x6DF3A0 -> fn 0x3981EC)
bun work/page-move-scan.ts fw493/vsh_module/vsh.elf word 0x6df3a0
bun work/page-move-scan.ts fw493/vsh_module/vsh.elf dump 0x63d2c0 80
# 2. the handler itself, as instructions (already done; repeatable)
powershell -File work/page-move-listing.ps1 list 3981ec 3983a0
# 3. the trampolines and one wrapper
powershell -File work/page-move-listing.ps1 list 3abc9c 3abcec
powershell -File work/page-move-listing.ps1 list 3b0600 3b06a0
# 4. the animate entry that the other family uses (not in any local decompile)
powershell -File work/page-move-listing.ps1 decomp 147e28
```

What to look for in (1): a class descriptor whose first word is a vtable base `B` with
`[B+0x13C] == 0x006DF3A0`, and a constructor that stores `B` into a new object's first word. That
constructor is the object the trampolines receive; its animate method then either is 0x3981EC (and
the mode question is settled as "mode = 0x1000016 is impossible, so 5 is a flag, not a mode") or is a
function that forwards `r5` into setup's `r4` (and the mode is 5). Either outcome changes what the
native should do, which is why the constants above are held.

Secondary gaps, named so the next pass does not re-derive them: `paf_ext.prx`, `paf_psjs.prx`,
`paf_web.prx`, `vshmain_ext.prx` and `silk*.prx` are not in any local decompile and are the only
remaining places a page-transition animation could be started from; `xmb_ingame.prx` was not
analysed at all.

## 10. Reproduction for this pass

```
# constants (read-only, ELF only)
bun work/elf-const.ts --elf fw493/vsh_module/vsh.elf --at 0x68eff0 --count 72 --type float
bun work/elf-const.ts --elf fw493/vsh_module/xmb_plugin.prx --at 0x17f40 --count 40 --type float
# class-name pointer table (PhXmBar is entry 13: 0x6fb22c -> 0x68f080)
bun work/page-move-scan.ts fw493/vsh_module/vsh.elf dump 0x6fb1f0 40
# OPD descriptor of the animation handler
bun work/page-move-scan.ts fw493/vsh_module/vsh.elf opd 0x3981ec
# instruction listings / decompile (Ghidra headless, -noanalysis -readOnly, nothing saved)
powershell -File work/page-move-listing.ps1 list   3ac980 3acd00
powershell -File work/page-move-listing.ps1 list   3accf4 3ace00
powershell -File work/page-move-listing.ps1 list   3af1e4 3af320
powershell -File work/page-move-listing.ps1 list   3b0600 3b06a0
powershell -File work/page-move-listing.ps1 decomp 147e28
# RCO animation counts (no firmware read; the existing decode)
node -e "const j=require('./out/native-reference/rco-behavior/resources.json');..."
```

No `.cpp`/`.h` file was modified; no git command was run; no capture was taken; the RPCS3 rig was
not touched. Firmware-derived numbers stay in these audit notes; no dump, module or capture is
committed.

---

# PAGE-MOVE dated note - 2026-10-27/28: do the dialog bursts bear on the composite question?

The 2026-10-27/28 capture batch (`C:\dev\out\rpcs3-ref-2026-10-28\`, `MANIFEST.md`) produced two full-frame
timestamped bursts across a Triangle-menu open and close:

| burst | frames | span | rate | key |
|---|---:|---:|---:|---|
| `dialogs/options-open-burst2/` | 108 | 1.977 s | 54.1 fps | Triangle at t = 500 ms |
| `dialogs/options-close-burst2/` | 110 | 1.990 s | 54.8 fps | Triangle at t = 500 ms |

The bursts are a timestamped open/close of an `explore_plugin` overlay, and they are of the right *kind*
for section 1.4's asking price (a known frame index per capture, `frames.csv` gives `ms,file`). The
question worth answering is whether they also settle the page-move composite question left open at the
end of section 7.

**They do not.** Four reasons, each checkable on the frames:

1. **They are the wrong surface.** The animation is `expage_menu`, the Triangle item menu, which is an
   `explore_plugin` page laid over the XMB. There is no XMB page change in either burst: the category
   bar stays at rest and the item row stays on the 505 focus row. At the open's mid-alpha frame
   (`dialogs/options-open-burst2/f_0000692.png`) the Game category icon is still at x 565 and
   `Corrupted Data` is still on row 505; at the close's partial frame
   (`dialogs/options-close-burst2/f_0000610.png`) the same is true. The 262 px bar recede is absent, and
   so is the item-depth move.
2. **They are the wrong channel.** What changes between `options-open-burst2/f_0000031.png` (closed) and
   `f_0002008.png` (open) is the menu band's alpha and its row set, not a screen-space x and not
   `_Eye.z` / `_Ambiant.w`. The composite question is precisely whether the captured 18-step screen
   motion is the 12-frame item move plus the ~29-frame distance/brightness channel (section 7,
   reading 1). An alpha fade has neither channel; it cannot separate them.
3. **They carry no `_Eye` decode.** Section 7's closing condition is "one frame-indexed burst that
   decodes `_Eye` as well as screen x". The batch has per-frame PNGs plus a `frames.csv`, not an
   `XMB_FP_DUMP` window, so the cleanest record of the transition is not in it.
4. **The rate is only just enough, and untested for this surface.** 54.1-54.8 fps is below the 60 Hz the
   animation runs at (phase 4c used 139.7 fps and still saw 18 distinct steps). At 54.8 fps a 12-frame
   move presents about 11 samples and an 18-frame move about 16; that could in principle separate the two
   frame counts on a *page move*, but it has not been tested on one, and the menu fade is not that.

What the bursts *do* add belongs to `ITEM-MENU-INFO-FIRMWARE.md` E2: the menu open is at partial alpha
~192 ms after the key (`f_0000692.png`), and the close is still partial ~110 ms after the key
(`f_0000610.png`), so the close is a visible fade rather than an instant destroy. That is a statement
about a page overlay, not about `PhXmBar`, and it must not be imported into the page-move duration.

**Effect on this document:** none. Section 7's conclusion stands - the firmware bar re-layout is traced
at 200 ms mode 5, the 139.7 fps capture counted 18 steps (300 ms at 60 Hz), and the conflict is resolved
only by reading 1 (a composite of the 12-frame item move and the slower bar recede) or by a page-move path
outside the traced code. The missing artifact is still a page-oriented burst that decodes `_Eye.x` and
`_Eye.z` per frame across one XMB page open and one close.

**Constructive note:** the batch rig is the right instrument for that burst. `tools/refcap.ps1` already
does full-frame `BitBlt` with a per-frame millisecond timestamp and writes `frames.csv`, which is exactly
the "known frame index per capture" phase 1.4 asked for, and `tools/series.ps1` gives the per-frame luma
channel. A future run only needs to park on a category and press Cross at 500 ms with the `XMB_FP_DUMP`
window open; the analysis would then be the same `motion-track.ts` / `region-luma.ts` pair over the
category band, with the frame count as the discriminator.

No source, threshold or reference was touched by this section.

---

# PAGE-MOVE dated note 2 - 2026-10-28 capture batch 2: do the high-frame-rate bursts bear on the composite question?

The second targeted batch (`C:\dev\out\rpcs3-ref2-2026-10-28\`, `MANIFEST.md`) is the one that answered the
*instrument* half of batch 1's objection. It has 22,149 burst frames in 19 bursts, and the new capability is
**1:1 crop bursts at 130.2-234.9 fps** with per-frame millisecond timestamps, at clock scale 100 % - against
batch 1's 54.1-54.8 fps full-frame ceiling. The manifest names fifteen of the nineteen bursts; by kind:

| kind | bursts (named in `MANIFEST.md` 4.1-4.4) | what moves |
|---|---|---|
| message / error pages | `error/launch-burst/`, `corrupt-launch-burst/`, `update2-launch-burst/`, `update-via-internet-burst/`, `ictest2-burst/`, `ictest3-burst/`, `ictest5-fail/` (234.9 fps) | a text fade and a page cross-fade |
| menu and band fades | `menus/menu-open-crop2/`, `menu-close-crop2/`, `menu-open-crop/`, `menu-close-crop/`, `bands/theme-band-open2/`, `theme-band-close2/`, `screensaver-band-close/` | a band's alpha |
| a focus move | `bands/bg1-focus-move/` (602 f, 143.5 fps, crop `450,380,1000,650`, Down at 901 and 2503 ms) | a row's focus between two settings rows |

**They still do not bear on the composite question.** Four reasons, each checkable:

1. **No burst crosses a page boundary.** Every burst above is an overlay, a message page or a focus move.
   The transition this document is about - a category page opening (the 262 px bar recede plus the item
   move) - is not in any of them. The batch's page evidence is stills (`depth2/*`, `nav/*`, `nav2/*`),
   which cannot time anything.
2. **Wrong channel.** The tracked quantity is band alpha (the ROI luma ramps in `menus/*-crop2`) and, in
   `bg1-focus-move`, a row's focus. Section 7's composite question is whether the captured 18-step screen
   motion is the 12-frame item move plus the ~29-frame distance/brightness channel. An alpha fade and a
   single focus step carry neither of those two channels, so they cannot separate them.
3. **Still no `_Eye` decode.** Section 7's closing condition is "one frame-indexed burst that decodes `_Eye`
   as well as screen x". This batch is PNGs plus `frames.csv` (ms, file), no `XMB_FP_DUMP` window - the
   cheap half of the record is there, the decoded half is not.
4. **The crops exclude the discriminating region by construction.** The three crop rects are
   `1150,150,770,700`, `620,450,740,170` and `450,380,1000,650`. The first two start at y 150 / y 450 and
   cover 740-770 px of width; the third starts at y 380. **None covers the category bar at y about 120, and
   none is full width**, so the 262 px recede - the channel that would decide reading 1 - is outside all of
   them. Only `bg1-focus-move`'s rect even contains the item rows, and its motion is a focus step.

**What the batch does change is the instrument, and that is worth recording here.**

- Batch 1's note item 4 ("the rate is only just enough, and untested for this surface") is **retired**: a
  crop burst now runs at 2-4x a 60 Hz animation's frame rate, 1:1, with the clock at 100 %, so a page move
  would present 2-4 samples per rendered frame instead of under one.
- `bands/bg1-focus-move/` is the project's first **timestamped screen-space row-transition record** (602
  frames, 143.5 fps, focus moving between `Theme Settings` and `Date and Time Settings` at 901-972 ms). It
  proves the rig can resolve a per-frame row move, which is the same measurement class the page-move
  x-channel needs. It does not answer the composite question, because a 40/80 px row step is not a 262 px
  bar recede plus an item move.
- The threaded-key runner is proven at 235 fps across a key press (`error/ictest5-fail/`), which is exactly
  what batch 1's pre-thread runner failed at (`menus/menu-open-crop/` vs `menu-open-crop2/`: the key hold
  gapped the capture loop at 175.7 fps and the crop2 run holds 135.2 fps continuously). A page-move burst no
  longer needs a workaround for that.

**The burst that would settle it** is now cheap to specify: park on a category, open `XMB_FP_DUMP`, press
Cross at 500 ms, and capture **full width** - ideally two crops, the category bar (`0,60,1920,160`) and the
item column (`300,380,700,700`) - at 130 fps or better, for both an open and a close. The discriminator is
the count of distinct samples in the bar's x (or in `_Eye.z`), not the alpha.

**Effect on this document:** none. Section 7's conclusion stands unchanged - the bar re-layout is traced at
200 ms mode 5, the 139.7 fps capture counted 18 steps (300 ms at 60 Hz), and the conflict is resolved only
by reading 1 (a composite of the 12-frame item move and the slower bar recede) or by a page-move path
outside the traced code. This batch supplies no page-move burst, so nothing here moves.

No source, threshold or reference was touched by this section.

---

# PAGE-MOVE Phase 14.2 - 2026-10-28: the category move's two row sets, measured on the console

Phase 14.1 settled the bar driver (200 ms, mode 5) and left one shape question behind it: on a **category**
move (LEFT/RIGHT) does the console cross-fade two item-row sets, or does it slide one row block as a unit?
This phase measured it on console frames and changed the two compositing calls in `src/view.cpp` to match.

## 0. Why a new console burst was needed

The 642-frame burst at `C:\dev\out\rpcs3-xmb-cap\burst-png` cannot answer the question. It is a page
**open and close** - `burst.ps1`'s key log is `key1 PgDown at 707.032`, `key2 PgUp at 2706.328`, and the
crop is band `y 220..380` - so it holds the category bar and no item row at all: the labels whose doubling
is the question are below y 470 and outside every frame. The audited `nav/` and `nav2/` sets are stills.
So the console frames that decide this were taken for this phase (section 1), and the local burst keeps its
existing role: timing the bar recede.

## 1. Capture

| item | value |
|---|---|
| Rig | `C:\dev\rpcs3-cap\rpcs3.exe` (the RSX-capture tree), `--no-gui --config C:\dev\out\rpcs3-xmb-cap\capture.yml --fullscreen C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self` - the same build and config as batch 2 |
| Input | global keyboard map installed for the run (`Handler: Keyboard`, Cross `PgDown`, Circle `PgUp`, Triangle `Insert`, Square `Delete`, arrows arrows): sha256 `40200e9c6b2954ec95d8db1c3fbada210fe469a70b7ae75d7c2881cd3ed24835`, 139 B. Restored at the end to the DualSense file, 66 B, sha256 `e624da0d66a8e971173c39a69a1dc2c89ffe146d57bf2a03c2c8cfacb459ceee` = the documented target. Both hashes in `C:\dev\out\navfeel\logs-input-hash.txt` |
| Bursts | `tools/refcap2.ps1` (plan runner, keys on a background thread), 1:1 crop `(x=0, y=180, w=1920, h=680)` - the category bar, both category labels and six item rows, nothing scaled |
| Stills | `navprobe/boot.png` (Game column at rest), `navprobe/after-right1b.png` |
| Frame rate | 82.0-83.7 fps (this crop is 1.31 MPix, so slower than batch 2's small crops), per-frame ms in `frames.csv` |

| burst | plan | frames | fps | keys (ms into the burst) |
|---|---|---:|---:|---|
| `navmove/right1` | `plans/p3-right.txt` | 100 | 83.6 | RIGHT at 540.4 |
| `navmove/left1` | `plans/p4.txt` | 92 | 71.2 | LEFT at 540.1 |
| `navmove/right2step` | `plans/p4.txt` | 108 | 72.1 | RIGHT at 547.6 and 672.7 (125 ms apart) |

The first probe still (`navprobe/boot.png`) is the Game column with `Steam Store` focused, but the move the
bursts captured runs from the **Network** column: the vsh restarted between the probe and the bursts (the
clock jumps from `10/25 12:32` to `28/10 17:03`, the EPILEPSY warning is on screen in the first burst's
frames, and the first burst batch was discarded for that reason). `right1` is therefore
`Network -> PlayStation Network`, `left1` the reverse, and `right2step` a second key pressed 125 ms into
the first move's 200 ms.

## 2. Per-frame motion of the console move (`right1`, Network -> PlayStation Network)

Every value is measured on the frames, read with the ink-run and template trackers in
`C:\dev\out\navfeel\` (`measure.py`, `blocks.py`, `profile.py`, `track.py`). "Entering icon" is the glyph
centroid of the category icon that is taking the focus slot (glyph-centroid bias about +-12 px; the icon
quads themselves are on the 200 px grid). Row-block x is the left edge of the focused row's label; the
outgoing and incoming values are relative to their own settled x (684 outgoing at rest, 682 settled after).

| frame (ms) | t after key | entering bar icon x | icon y | bar pitch | outgoing label x | incoming label x | two row sets? |
|---|---:|---:|---:|---:|---:|---:|---|
| 539 | -1 | 551 (settled Network) | 278 | 205 | 684 (settled) | - | no |
| 566 | +26 | merged with the leaving icon | 278 | 205 | 634 (-50) | 765 (+83) | **yes**, both readable |
| 605 | +65 | 632 | 278 | 217 | 545 (-139) | 733 (+51) | **yes**, both readable |
| 653 | +113 | 592 | 278 | 215 | 505 (-179) | 709 (+27) | **yes**, outgoing faint |
| 703 | +163 | 575 | 278 | 215 | 499 (-185) | 696 (+14) | **yes**, outgoing very faint |
| 754 | +214 | 567 | 278 | 215 | - (gone) | 688 (+6) | no |
| 1197 | settled | 561 | 278 | 215 | - | 682 | no |

Alpha of each set, from the label peak luma in the same frames (settled peak 255, background about 100):

| t after key | outgoing alpha | incoming alpha |
|---|---:|---:|
| +26 ms | 0.62 | 0.70 |
| +65 ms | 0.37 | 0.76 |
| +113 ms | 0.25 | 0.85 |
| +163 ms | 0.15 | 0.92 |
| settled | 0 | 1.00 |

Progress p is not assumed: it is read off the measured offsets (outgoing -215 p, incoming +125 (1-p)),
which is why the two columns above are quoted against time and the slide numbers below against p.

## 3. What the frames show

1. **The bar is one continuous set.** No frame shows a doubled category icon: the pitch stays 200-215 px
   (glyph measurement) and the icons all translate together, the entering one arriving 632 -> 592 -> 575 ->
   567 -> 561 while the leaving one goes. The only place two texts coexist at the focus slot is the bar's
   own *label* pair - the leaving category's label fades while the entering one brightens - which this build
   does not draw (it draws only the focused category's label). That is a separate, still-open detail.
2. **The row block is two sets, not one.** From +26 to +163 ms both label sets are readable in the same
   frame, at different x and different brightness. So the console **does** cross-fade; the native's
   compositing *kind* was right and the question was always the curve.
3. **Each set translates rigidly.** At +65 ms the outgoing row's icon centre is 426 (-139 from its 565 rest)
   and its label left edge 545 (-139 from 684): one offset for icon and text, no scale on the block. The
   outgoing set is not a receded parent column; it is the same block moved.
4. **The fade is asymmetric, and that is the visible difference.** The outgoing block travels about
   **215 px** (one bar step) while its alpha falls 1 -> 0 on the move ease - measured -50/-139/-179/-188 px
   at p .30/.63/.78/.88, i.e. 215 px at p = 1. The incoming block travels only about **125 px** and is
   already at **0.70** alpha on its first visible frame (+26 ms), reaching 1.0 at settle - measured
   +83/+51/+27/+14/+6 px, i.e. 125 px at p = 0 and 0 at p = 1. The two alphas do not sum to 1 (0.62+0.70,
   0.37+0.76); the new column is a fresh draw that is mostly opaque throughout, and the old one is the ghost.
   That is the shape the "mushy" native move was missing: it had both sets at 1-p and p, so at mid-move two
   half-bright label sets sat 150 px apart.
5. **A second key re-uses one outgoing set.** In `right2step` (RIGHT at +125 ms) the frame 107 ms after the
   second key shows exactly one fading set (the column that was incoming) plus the new incoming one - the
   console does not accumulate two ghosts. The native's `outgoing_` latch behaves the same way (it is
   overwritten), so nothing changes there.
6. **Duration.** The incoming set's approach is an exponential: offsets 83/51/27/14/6 give a constant
   tau = 72-75 ms (per-frame retention 0.80). The 200 ms mode-5 candidate is tau = 66.8 ms and the 300 ms
   mode-4 candidate 65.7 ms, so this channel does not separate them - it confirms 14.1's 200 ms mode 5 is
   at least not contradicted, and this phase keeps it. The outgoing set's fade is slightly ahead of the
   slide (alpha 0.62 where 1-p is 0.70 at p = .30), so its alpha is not exactly 1-p; the four samples fit
   1 - p within +-0.08, which is what the source keeps.

## 4. What changed (`src/view.cpp`, single writer)

The two compositing calls, and nothing else:

| quantity | was | is | provenance |
|---|---:|---:|---|
| outgoing set slide | 150 px | **215 px** | TRACED (section 2/3.4) |
| incoming set slide | 150 px | **125 px** | TRACED |
| outgoing opacity | `categoryFade_` (1 -> 0) | unchanged | TRACED within +-0.08 by the four samples |
| incoming opacity | `1 - categoryFade_` (0 -> 1) | **`.58 + .42*(1-categoryFade_)`** (0.58 -> 1) | TRACED samples .70/.76/.85/.92; FIT line - the p = 0 intercept is extrapolated, no captured frame exists before p = .30 |
| duration / mode | 200 ms mode 5 | unchanged | PAGE-MOVE 14.1 |

The rest frame cannot move: at rest `categoryFade_` is 0 and both offsets are 0, so the incoming opacity
evaluates to exactly 1.0 and the canonical capture is unchanged (section 6). The page (depth) move is
untouched - its incoming offset `ahead*210` and pinned focused icon are separate code.

Also added: `XMB_MOVE_DEBUG=1` skips the outgoing set at draw time (a static env read, capture only), so a
burst can measure the incoming block alone. Unset in every normal run, where both sets draw. It was used
for one burst (`dbg-right`, section 5) and is kept because it is the only way to separate the two alphas
in a native burst.

## 5. Before/after, measured

Native RIGHT from the same route (`Game -> Network`, `--render-clock 30`, `burst=<prefix>@RIGHT`, 12 frames,
`C:\dev\out\navfeel\bef-right_*` and `aft-right_*`; trackers as in section 2; the "before" figures are the
pre-change build gated at `7ca1a0e`):

| quantity | before | after | console |
|---|---:|---:|---:|
| outgoing block travel (p = 1) | 150 px | **215 px** | 215 px |
| outgoing label x at about 40 ms | -80 px | -112 px | -95 px (interpolated 26..65 ms) |
| incoming block travel (p = 0) | 150 px | **125 px** | 125 px |
| incoming label x at about 40 ms | +75 px | +74 px | +80 px |
| separation of the two sets | constant 150 px | 125 -> 215 px | 133 -> 206 px |
| incoming alpha at p about .35 | 0.35 (1-p) | 0.75 measured / 0.73 modelled | 0.70 |
| frames with both sets between .25 and .75 alpha | about half the move | none (incoming >= .58) | none |

The incoming alpha after the change was measured with the outgoing set disabled (`XMB_MOVE_DEBUG=1`,
`dbg-right_*`), on a glow-free row 240 px below focus (peak luma 154.7 settled, background about 100):
136.2 at 36 ms -> 151.7 at 169 ms = alpha 0.65 -> 0.95. The full-draw track of the same set gives
0.753 at 28 ms -> 0.970 at 178 ms, which includes the focused label's glow and so reads high; both agree
that the incoming set is legible from the first frame, which the pre-change 1-p could not do.

Visual: `C:\dev\out\navfeel\navfeel-strip.png` - three rows (console, ours after, ours before) x four
matched times (26/65/113/163 ms). At 163 ms the "before" row still shows the outgoing label over the
incoming one; the "after" row does not, and its overlap frames carry the console's asymmetry.

The two other directions were captured on the same build and behave as the mirror image: `aft-left_*`
(LEFT from the same route, Game -> Video) slides the outgoing block +215 px right and brings the incoming
one from -125 px, and `aft-right2_*` (a second RIGHT from the settled Network column, one burst per key -
`main.cpp`'s burst token presses one key, so a true in-flight two-step is only available on the console
side, where section 3.5 shows the latch is re-used) repeats the same one-step geometry.

## 6. Gates and determinism

- Canonical: `bun work/ab-layers.ts --capture --exe C:\dev\out\native-navfeel\re-ps3-native.exe` ->
  **PASS 6/6** with `text presence: 1668 bright pixels in the clock crop (minimum 900) PASS`.
  frame 18.76 / 0.8360, top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label row
  11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667 - every delta **0.00 / 0.0000**
  against the stored baselines, i.e. the canonical frame at rest did not move and the gate's text-presence
  line is unchanged.
- Determinism: two `--render-clock 20` root captures are SHA-256
  `37e2b590c42ef0e3f3601ca492221147c9874d2e3b1785e6237b616f2c0bbc14` both - also the hash phase 13
  recorded, which is the same statement as the gate: this phase changes no rest frame.
- Rig restore: input config back to `e624da0d...` (66 B), RPCS3 stopped; `logs-input-hash.txt` holds all
  three hashes. No RCO, threshold, budget, reference, harness, CMake or asset file was touched.

## 7. What remains

1. The bar's leaving **label**. The console fades the departed category's label while the entering one
   brightens (section 3.1); `src/view.cpp` draws only `i==shell_.category()`'s label, so a category move
   currently swaps the label without a fade. Measured on the same frames; not changed here.
2. The bar's entering icon. The console's arrives by sliding (632 -> 561 over 200 ms); this build's
   `open?pageX` branch pins the selected icon at 565 from the first frame, which is a 0-210 px difference
   at the start of a move depending on direction. The strip does not separate it cleanly enough to change
   it here - it needs a burst that isolates the bar (crop `0,180,1920,180`) the way this phase isolated the
   row block.
3. The outgoing alpha: the four samples fit 1-p within +-0.08, but the measured 0.62 at p = .30 is below
   the fit's 0.70, i.e. the fade runs slightly ahead of the slide. A 200 fps crop of the label band alone
   would settle it; the 83.6 fps burst cannot.

## 8. Reproduction

```
# console (rig; input map swapped and restored around the run)
C:\dev\rpcs3-cap\rpcs3.exe --no-gui --config C:\dev\out\rpcs3-xmb-cap\capture.yml --fullscreen ^
  C:\dev\rpcs3\bin\dev_flash\vsh\module\vsh.self
powershell -File C:\dev\out\rpcs3-ref2-2026-10-28\tools\refcap2.ps1 -Plan C:\dev\out\navfeel\plans\p3-right.txt ^
  -ProcessId <pid> -Root C:\dev\out\navfeel

# native (build + burst + settled frames)
C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-navfeel
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir C:\dev\out\navfeel\data ^
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 ^
  --focus-time 2000 --script "full,wait,wait,wait,grab=<root>/aft-settled.png,burst=<root>/aft-right@RIGHT,wait,wait,grab=<root>/aft-after.png,quit"

# measurement + strip
python measure.py <png> [--dy 180] | python blocks.py <png> [--dy 180] | python track.py <dir> <frames.csv> <pre> <post>
python strip.py   # C:\dev\out\navfeel\navfeel-strip.png

# gates
bun work/ab-layers.ts --capture --exe C:\dev\out\native-navfeel\re-ps3-native.exe
```

Artifacts (never committed, this machine): `C:\dev\out\navfeel\` - `navmove\right1|left1|right2step\`
(console frames + `frames.csv` + `rect.txt`), `navprobe\`, `bef-right_*`, `aft-right_*`, `dbg-right_*`,
`aft-settled.png`, `aft-after.png`, `navfeel-strip.png`, `logs-input-hash.txt`, `plans\`.

---

# PAGE-MOVE Phase 15 - 2026-10-28: the transitions pass

Five transitions, each measured on console frames and on a native before/after build. Source writes:
`native-xmb/src/view.cpp` and `native-xmb/src/view.h` only (plus this section). No capture tool, budget,
reference or other audit was edited; `BEHAVIOUR-GAP-AUDIT` gets two cross-links where its claims needed
correcting (B1, C7). Every number below is either **TRACED** (a call, constant or decoded value establishes it),
**FIT** (this build's curve to a measured endpoint) or **NOT MEASURABLE** (the local frames cannot settle it),
and each is labelled where it is used.

Capture grammar (`src/main.cpp`): `burst=<prefix>@KEY` presses a key and grabs consecutive frames at 12 ms
(the saved name carries the real elapsed ms), so one process run gives one ~130-230 ms window; the frames
are full 1920x1080 grabs. The console runs are the existing ones: the 642-frame page open/close burst at
`C:\dev\out\rpcs3-xmb-cap\burst-png\` (139.7 fps, band y 200..380), the `navfeel\navmove\` category-move
bursts (83.6 fps, 1:1 crop y 180), and the PS-hold burst at
`C:\dev\out\rpcs3-gaps-2026-10-28\burst\ps-menu-entries\` (206.6 fps, 1:1 crop 300,470,1320,240). Native
captures: `C:\dev\out\transitions\cap-before\` and `cap-after\` (same script, same pinned clocks).

## 0. Summary of what changed

| # | transition | was | is | evidence |
|---|---|---|---|---|
| 1 | page (depth) move | one ease: 300 ms mode 4 | **two eases**: bar layer 200 ms mode 5 (TRACED, 14.1) + page content 300 ms mode 4 (FIT to the 18-frame burst) | 15.1 |
| 2 | selected category icon on a category move | pinned at x 565 from the first frame | **slides from +200 px** (one bar step) on the category ease | 15.2, TRACED |
| 3 | departed category bar label | not drawn | **drawn under its own icon, alpha = categoryFade_**, entering label = 1 - categoryFade_, both riding their icons | 15.3, TRACED |
| 4 | options panel entrance | 200 ms from the key, no delay | **130 ms delay + 250 ms ramp** (rest at 380 ms); close keeps a 9 ms delay but still cuts | 15.4 |
| 5 | PS-hold menu | full menu on the frame after the 1000 ms rule | **black at press+1052, text 1279..1326, selected row's bloom 1326..1840** | 15.5, TRACED (endpoints) |

## 1. The page (depth) move: the trace and the burst disagree, so the channels are split

### 1.1 What each evidence set says

- **TRACED (14.1):** the firmware's own bar re-layout on an XMB page change is **200 ms, mode 5** -
  `xmb_plugin` `state+0x520` = 200.0, written by the transition-refcount path, and every one of the nine bar
  animator sites passes the mode-5 literal. There is no 300 ms duration and no mode-4 call in the bar path.
- **The burst's frame count (4c, re-measured here):** the visible page channel is **18 rendered frames**
  at 0.762/frame, i.e. 300 ms at the shape mode 4 / 300 ms predicts (0.7741).

Both are correct about different channels, and this pass measured the discriminator that 14.1 said was
missing: **whether the console's visible channel snaps at 200 ms.** It does not.

### 1.2 The console page open, re-measured frame by frame

Template track of the page's own category icon (template = the settled icon of `page-open.png` at
x 246..366, y 236..332; the tracked value is the icon's left edge, 507.9 at rest, 246.3 settled, so
**261.6 px** of travel), key at 701.266 ms:

| t after key | icon left edge | progress p | 1 - p |
|---:|---:|---:|---:|
| +709 | 407.9 | 0.382 | 0.618 |
| +726 | 369.2 | 0.530 | 0.470 |
| +742 | 339.9 | 0.642 | 0.358 |
| +759 | 317.6 | 0.727 | 0.273 |
| +775 | 300.5 | 0.793 | 0.207 |
| +788 | 287.5 | 0.842 | 0.158 |
| +805 | 277.7 | 0.880 | 0.120 |
| +822 | 270.1 | 0.909 | 0.091 |
| +839 | 264.3 | 0.931 | 0.069 |
| +856 | 260.0 | 0.947 | 0.053 |
| +872 | 256.7 | 0.960 | 0.040 |
| +892 | 254.1 | 0.970 | 0.030 |
| +909 | 252.2 | 0.977 | 0.023 |
| +926 | 250.8 | 0.983 | 0.017 |
| +942 | 249.6 | 0.987 | 0.013 |
| +973 | 248.1 | 0.993 | 0.007 |
| +1005 | 247.2 | 0.996 | 0.004 |
| +1063 | 246.6 | 0.998 | 0.002 |

Per-frame retention over the distinct frames is **0.761 +/- 0.004** (4c's 0.7623 reproduced), which with the
documented -0.014 tracker bias is 0.775: mode 4/300 = 0.7741 and mode 5/200 = 0.7794 sit 0.6% apart and the
shape cannot separate them. The duration can, and **neither candidate snaps**: at the mode-5 end (+200) the
channel is at p = 0.974 and still moving; at the mode-4 end (+300) it is at 0.996, and it keeps decaying to
0.998 by +362 (a further 100-200 ms of 1-2 px tail). The page **close** (key at 2702.865) mirrors it: first
motion +57 ms, retention 0.762/frame, p = 0.237/0.419/0.557/0.663/0.759/0.827/0.875/0.909/0.934/0.952 at
+57/+74/+91/+126/+141/+158/+174/+191/+207/+224.

So the visible channel is a slower, non-snapping composite, exactly reading 1 of 14.1 section 7: the 12-frame
(200 ms) bar move plus the ~29-frame distance/brightness channel 4b 1.3 decoded at 0.902/frame. The burst
alone cannot separate them.

### 1.3 What the native does now

`view.h` now has two constants and two eases, each labelled:

```
pageMilliseconds = 300, mode 4   // FIT: the page content channel (262 px icon travel, item column entry,
                                 //      label fade). The burst's 18-frame count is the only direct evidence
                                 //      for this channel.
barMilliseconds  = 200, mode 5   // TRACED: the bar layer (recede distance/projection, the bar's own focus
                                 //      row, the neighbours' brightness/presence). PAGE-MOVE 14.1.
```

Both start on the same frame. `barDepthPosition_` reaches 1.0 at **+202 ms**, `depthPosition_` at
**+300 ms** (measured from the `XMB_MOTION_LOG` trace of the rebuilt exe: `depth: 7238 ms position 0.2712
bar 0.2651` ... `7419 ms position 0.9552 bar 1.0000` ... `7516 ms position 0.9903 bar 1.0000`). The rest
frame is untouched because both land on `shell_.depth()`.

Why this is not "splitting to avoid the trace": the **trace governs the channel it describes** - the bar
re-layout - and it is now what drives the recede and the neighbours. The 262 px icon travel is a *different
draw* (GEOMETRY-ROOT 13.1: the page's own category draw, not the bar's focused row), and its only evidence is
the burst's 18 frames. Landing 200 ms on both would put the visible icon 6 frames and ~2.5% ahead of the
console at the 200 ms mark, which is the one measurement 4c made three times.

Measured native page-open series (template = the settled icon of that build's own `po-settled.png`, which is
byte-identical across the two builds; the template origin is 240, the rest edge 502, so the travel is
262 px; `t` is ms after the ENTER token; both builds run the same 300 ms mode-4 content ease, so the two
columns are the same channel measured twice):

| t | before: icon x | before: p | after: icon x | after: p |
|---:|---:|---:|---:|---:|
| +28 | - | - | 391.4 | 0.422 |
| +38 | - | - | 371.5 | 0.498 |
| +50 | - | - | 349.8 | 0.581 |
| +52 | 346.1 | 0.595 | - | - |
| +63 | - | - | 329.7 | 0.658 |
| +71 | 318.2 | 0.701 | - | - |
| +75 | - | - | 313.9 | 0.718 |
| +86 | - | - | 302.6 | 0.761 |
| +97 | - | - | 292.8 | 0.799 |
| +101 | 289.8 | 0.810 | - | - |
| +111 | - | - | 282.2 | 0.839 |
| +127 | 273.1 | 0.874 | - | - |
| +135 | - | - | 269.3 | 0.888 |
| +146 | - | - | 264.8 | 0.905 |
| +156 | 260.9 | 0.920 | - | - |
| +157 | - | - | 260.9 | 0.920 |
| +204 | 250.0 | 0.962 | - | - |
| +299 | 240.0 | 1.000 | - | - |

The two columns agree wherever they overlap (+156/+157 both 0.920), and the differences elsewhere are
one-frame real-clock lead (the ease advances by the frame interval, not by the time since the key) plus the
shorter after-burst window. **The page content channel did not change**, by design: what changed is the bar
layer underneath it.

The **bar layer** is what moved. Its brightness channel is visible in the neighbour's peak luma (window
x 690..840, y 235..335, page background 90): before 153 at +52, 138 at +71, 121 at +104, 110 at +139,
104 at +175, 99 at +241, 97 at +299; after 171 at +28, 155 at +50, 138 at +75, 126 at +97, 115 at +123,
107 at +157. The after series is the same curve sampled earlier, and its landing is at +200 by the log.

**NOT MEASURABLE here:** the console's own bar-layer duration. Its neighbours are already at the 0.067
brightness end-state in the first visible frame (+709): the column profile of `f_001410` in the bar band has
no run above luma 95 at the n=+1/n=+2 slots, while the root frame `f_001402` still has them at 104-115. The
~700 ms page load outlasts the whole 200-300 ms recede, so the burst cannot time it. That is why the bar
layer takes its duration from the instruction-level trace and the icon channel from the frame count, and it
is the same reason GEOMETRY-ROOT 13.2 already labels the recede's driving ease a FIT.

## 2. The selected category icon slides in on a category move

Console `navmove\right1` (Network -> PlayStation Network, key at 540.4 ms): the entering category icon
arrives **632 -> 592 -> 575 -> 567 -> 561** at +65/+113/+163/+214/settled (14.2 section 2), and its label
follows it: label ink centre **725 -> 687 -> 658 -> 636 -> 619 -> 606 -> 596 -> 589 -> 579 -> 565.5** at
+26/+42/+54/+65/+89/+102/+113/+140/+163/settled. The offset from the settled slot is **+159.5 at +26 ms,
+70.5 at +65, +30.5 at +113, +13.5 at +163**, i.e. it starts at one whole bar step (**200 px**, not the
125 px the item rows travel) and decays on the same 200 ms mode-5 curve.

The native pinned it: the open icon's x was `pageX` (565 at depth 0) from the first frame, so a RIGHT move
teleported the incoming icon to the focus slot while the old one slid away. Now:

```cpp
x = open ? pageX + 200.*distance*recede : barFocusX + (200.*distance + 10.*sign)*recede;
```

`distance = i - categoryPosition_` is +1 at the key and 0 at rest, so the open icon starts exactly one bar
step right (or left) and rides the category ease; at rest `distance` is 0 and the expression is `pageX`, so
the canonical frame is unchanged.

Measured (native, template = the settled icon of `cat-settled.png`, origin 480; and the departing icon from
`cat-root.png`, origin 480):

| t | before: entering x | after: entering x | offset (after) | console offset at the same t |
|---:|---:|---:|---:|---:|
| +39 | 480.0 (pinned) | 579.9 | +99.9 | +93 (interpolated +26..+65) |
| +50 | 480.0 | 564.7 | +84.7 | +82 |
| +64 | 480.0 | 548.6 | +68.6 | +71 (+65) |
| +85 | 480.0 | 530.4 | +50.4 | +53 (+89) |
| +103 | 480.0 | 518.6 | +38.6 | +41 (+102) |
| +115 | 480.0 | 512.4 | +32.4 | +30.5 (+113) |
| +163 | 480.0 | 495.9 | +15.9 | +13.5 (+163) |

The departing icon (the old focused category) travelled 316.9 -> 278.1 in the before build (a template
matched against a fading glyph, so the tail is noise) and 374.1 -> 294.6 in the after build, i.e. -105.9 at
+39 ms = -210 x 0.50 - the same bar motion, unchanged.

## 3. The departed category label fades while the entering one brightens

The console draws **one** category label at rest (under the focused icon) and **two** during a move: the
departing label slides out under its own icon while its ink fades, and the entering label slides in under
its icon while it brightens (14.2 section 3.1). Per-frame label ink, measured on the same `right1` frames
(label cap band, screen y 347..358; background 59; settled peak 255; the two words are separated by their
own ink runs, so the values are per set and not a fit):

| t after key | departing: mid x | departing alpha | entering: mid x | entering alpha |
|---:|---:|---:|---:|---:|
| +26 | 515 | 0.765 | 725 | 0.388 |
| +42 | 477 | 0.587 | 687 | 0.464 |
| +54 | 448 | 0.444 | 658 | 0.668 |
| +65 | 425 | 0.337 | 636 | 0.750 |
| +89 | 409 | 0.260 | 619 | 0.770 |
| +102 | 400 | 0.199 | 606 | 0.806 |
| +113 | 388 | 0.158 | 596 | 0.847 |
| +140 | - (gone) | 0 | 589 | 0.903 |
| +163 | - | 0 | 579 | 0.934 |
| +252 | - | 0 | 569 | 0.980 |
| settled | - | 0 | 565.5 | 1.000 |

Against the move's own progress p (read from the entering label's offset / 200): p = 0.203 at +26,
0.648 at +65, 0.848 at +113, 0.933 at +163. The departing alpha tracks **1 - p** (+/-0.04 after +42) and the
entering tracks **p** (+0.06..+0.19 early, +/-0.01 after +65) - i.e. a cross-fade on the category ease, not
the asymmetric 0.58 + 0.42 p line the *item rows* use (14.2 section 4).

The native now draws both, each at its own category's x, so the departing label rides the departing icon:

| t | before: labels drawn | after: departing | after: entering |
|---:|---|---:|---:|
| +39 | one word at 566, peak 255 | mid 460, alpha 0.52 | mid 666, alpha 0.52 |
| +50 | one word | 444, 0.44 | 650, 0.60 |
| +64 | one word | 427, 0.36 | 634, 0.67 |
| +72 | one word | 419, 0.33 | 627, 0.71 |
| +85 | one word | 408, 0.27 | 616, 0.76 |
| +103 | one word | 395, 0.21 | 604, 0.82 |
| +115 | one word | 389, 0.18 | 598, 0.84 |
| +163 | one word | (overlapped, 0.10) | 581, 0.92 |

Alphas are `(peak - 77) / (255 - 77)` from the native's own background 77 and settled peak 255; the
entering/leaving alphas are `1 - categoryFade_` / `categoryFade_`, i.e. the same 200 ms mode-5 cross-fade the
columns use, with no new constant. At rest `categoryFade_` is 0 and `outgoing_` is empty, so only the focused
label draws - the canonical frame is unchanged.

## 4. The options panel's entrance has a measured delay

UI-ELEMENT-GAP-AUDIT O6 measured the console's open as: starts **+115..150 ms** after the key, ramps
**245..255 ms**, at rest **+360..410 ms**; the close starts +9 ms and ramps 200 ms. The panel's own ease
(`optionsOpenEase_`) drove the slide (`x0 = 1330 + ahead*600`), the band's alpha and the page's 0.545 dim
from the key with a flat 200 ms, i.e. **160-210 ms short**. `PafEase` now takes a fourth argument,
`delayMilliseconds`, which holds `from_` for that long before the 60 Hz curve starts (`duration_` and
`elapsed_` are seconds, so the delay is converted once at retarget). It is used only here; every other
retarget keeps the default 0. The delay is carried by the ease rather than by starting it late because a
late start cannot express the close's measured 9 ms offset and would desynchronise the slide from its own
opacity.

Measured: band left edge (leftmost column in y 200..900 whose luma departs from that run's root grab by more
than 1.0; the resting band edge is 1320):

| t after Triangle | before edge | after edge | after open amount (log) |
|---:|---:|---:|---:|
| +34 | 1640 | - (no change, 0.0) | 0.0000 |
| +44 | 1600 | - | 0.0000 |
| +56 | 1552 | - | 0.0000 |
| +67 | 1518 | - | 0.0000 |
| +84 | 1474 | - | 0.0000 |
| +105 | 1434 | - | 0.0000 |
| +118 | 1412 | - (no change, 0.0) | 0.0000 |
| +136 | 1400 (interpolated) | 1834 | 0.0278 (+134) |
| +144 | 1388 | 1784 | 0.1474 (+145) |
| +155 | 1376 | 1728 | 0.2595 (+151) |
| +166 | 1362 | 1676 | 0.4201 (+194) |
| +177 | 1358 | - | 0.5390 (+197) |
| +300 | 1320 (rest) | 1427 (0.84 of the ramp) | 0.7182 (+245) |
| +600 | 1320 | 1320 (rest, byte-identical to before) | 1.0000 (+380) |

The after build draws **nothing** until +130 (+/-1 frame), first shows the band at +136, and is at rest at
+380 = 130 + 250. The console's window is +115..150 for the start and +360..410 for rest; both are inside it.
The settled panel is **pixel-identical** to the before build (max |diff| 0.0 over the whole frame), so only
the entrance moved.

**The close is still a hard cut, and O6's "close is right on duration" is wrong for this build.** The panel's
draw is gated on `Shell::optionsOpen()`, so when the shell closes it the panel disappears in one frame no
matter what the ease does: every frame from +21 ms after the closing Triangle is the plain page
(`close_00_0021ms.png` .. `close_11_0160ms.png`, 1032123 bytes = the root grab; measured on the rebuilt exe).
The 9 ms close delay is applied to the ease and is inert until the panel's content is latched across the
close; that latch is not in this pass (see section 7).

## 5. The PS-hold menu's staged envelope

PARITY-SCORECARD 16.3 gives the envelope (black 1052 ms after the press, entries 1202..~1970). This pass
re-measured the burst it came from frame by frame and found the staging inside it. Crop 300,470,1320,240;
the plan (`plans/a07-ps-hold.txt`, `cburst ... 150:PS:1200`) puts the press at 150 ms of the burst, which
makes the black frame exactly press+1052 and confirms the alignment:

| burst ms | after press | crop mean | crop peak | reading |
|---:|---:|---:|---:|---|
| 1197 | +1047 | 65.97 | 255 | XMB still up |
| 1202 | +1052 | **0.00** | 0 | fully black, instant |
| 1411 | +1261 | 0.00 | 0 | black |
| 1429 | +1279 | 0.52 | 63 | first text ink |
| 1436 | +1286 | 1.62 | 148 | text ramping |
| 1452 | +1302 | 2.42 | 207 | text ramping |
| 1476 | +1326 | 3.21 | 255 | text full; focused row's bloom starts |
| 1602 | +1452 | 3.49 | 255 | bloom growing (bright>88: 4,899) |
| 1786 | +1636 | 3.87 | 255 | bloom growing (5,536) |
| 1986 | +1836 | 4.27 | 255 | bloom settled (6,236) |
| 2402 | +2252 | 3.43 | 255 | rest (the mean drifts down after the bloom peaks) |

So 16.3's "entries fade in between 1202 and 1970" is the **whole appear window**, not a text fade: 1202 is
the black onset, the text's own ink runs 1279..1326 (~47 ms), and what keeps changing until ~1840 is the
selected row's bloom (its bright-pixel count grows 3,903 -> 6,236 with the text already opaque at 1326).

The native drew the full menu on the first frame after the watcher's 1000 ms rule. Now `psPressed()` records
the press instant and the menu block draws: nothing before press+1052 (the XMB stays up for the 52 ms between
the rule and the envelope), the black plate from +1052, all menu content at
`psHoldTextAlpha` (0 at 1279, 1 at 1326, linear - FIT), and the selected row's bloom at
`psHoldBloomAmount` (0 at 1326, 1 at 1840, linear - FIT: the burst fixes the endpoints, not the shape).
The envelope also had to enter the UI-layer signature: it advances with the clock, not with a shell change,
so without it the cached layer froze on the frame the menu opened (measured: one `pshold:` line and then
nothing, with every grab showing the plain XMB).

| t after press | console: crop mean/peak | before: crop mean/peak | after: crop mean/peak |
|---:|---:|---:|---:|
| +1200 | (black at +1202) | 4.45 / 255 (full menu) | **0.00 / 0** |
| +1800 | 3.87 / 255 (+1636, bloom growing) | 4.45 / 255 | 4.45 / 255 |
| +2400 | 3.43 / 255 (+2252, rest) | 4.45 / 255 | 4.45 / 255 |

The rebuilt exe's own trace (`XMB_MOTION_LOG=1`, one line per painted frame): `pshold: 1011 ms ... black 0`,
`1059 ms ... black 1 text 0.000`, `1341 ms ... text 1.000 bloom 0.029`, `1850 ms ... bloom 1.000`. The 1000 ms
hold rule in `ps_watcher.cpp` is unchanged (outside this pass's fence); the envelope is what the console
spends after it.

## 6. Gates and determinism

- Canonical: `bun work/ab-layers.ts --capture --exe C:\dev\out\transitions\after\re-ps3-native.exe` ->
  **PASS 6/6** with `text presence: 1668 bright pixels in the clock crop (minimum 900) PASS`.
  frame 18.76 / 0.8360, top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label row
  11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667 - every delta **0.00 / 0.0000**
  against the stored baselines.
- Determinism: two `--render-clock 20` root captures are SHA-256
  `37e2b590c42ef0e3f3601ca492221147c9874d2e3b1785e6237b616f2c0bbc14` both - the same hash phases 13 and
  14.2 recorded.
- The two rest states this pass could have moved are clean: the settled options panel is byte-identical
  before/after, and the settled depth-1 page is also byte-identical (max |diff| 0.00 over the whole frame;
  both sides report the same state: settings, depth 1, "Update via Internet"). The first after grab of that
  page caught the last 4 px of the 300 ms ease - the re-run is the one quoted, and it is what the table in
  1.3 now uses.
- Nothing was re-baselined. No reference, budget, capture tool, `main.cpp`, `shell.*`, `dialogs.cpp`,
  `ribbon.cpp`, `hdr.cpp`, CMake or asset file was touched.

## 7. What remains

1. **The console's bar-layer duration** stays NOT MEASURABLE with the local artifacts (section 1.3): its
   neighbours are already at their end state in the first visible frame because the page load outlasts the
   recede. A burst that starts within ~50 ms of the recede's first frame, or a decoded per-frame `_Eye` /
   `_Ambiant.w` dump of a page open (not just a close), would settle whether the bar layer is really 200 ms.
2. **The console's visible page channel does not snap** anywhere in the 18-25 frames it moves; a two-channel
   model for the *icon* itself (a 12-frame move plus the ~29-frame distance channel) would fit the tail
   better than one 18-frame ease. This build keeps one content ease because the split is not separable from
   these frames.
3. **The options panel's close** draws no fade (section 4). The fix is a latch of the panel's titles, rows
   and selection in `View` so the closing panel can draw while `Shell::optionsOpen()` is already false; the
   measured 9 ms start and 200 ms ramp are already applied to the ease.
4. GEOMETRY-ROOT 13.5's visible-vs-decoded recede layer is untouched: the neighbours still use the decoded
   d 2700 -> 2483.53 geometry (pitch 217.43) rather than the visible row's 2857.2 / pitch 189.
5. The PS envelope's two ramps are linear (FIT); the burst fixes the endpoints only. The console's slow
   post-1840 drift (crop mean 4.27 -> 3.43 over ~400 ms) is not modelled.
6. The category label's y is unchanged: the console's ink sits at screen y 347..358 and this build's 21 px
   label in `{x-140,330,280,40}` lands inside the same band (3 px high). The geometry was not touched because
   the rest frame matched references.

## 8. Reproduction

```
# native captures (one process per burst; the burst's own filename carries the elapsed ms)
powershell -File C:\dev\out\transitions\work\caps.ps1 -Exe <exe> -Out <dir>

# page open, with the two-channel trace
$env:XMB_MOTION_LOG='1'
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <dir>/data ^
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 ^
  --focus-time 2000 --script "full,wait,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,ENTER,wait,wait,wait,grab=<png>,quit"

# measurement (bun; pure TS)
bun C:\dev\out\transitions\work\report.ts page|cat|opt|ps
bun C:\dev\out\transitions\work\neighbour-luma.ts

# gates
bun work/ab-layers.ts --capture --exe C:\dev\out\transitions\after\re-ps3-native.exe
```

Artifacts (never committed, this machine): `C:\dev\out\transitions\` - `cap-before\`, `cap-after\`,
`det1\`, `det2\`, `work\` (the scratch measurement scripts and the contact sheets `band-look.png`,
`ps-envelope.png`).

---

# PAGE-MOVE Phase 16 - 2026-10-28: the navigation-feel pass

Four items, each measured on console frames and on a native before/after build. Source writes:
`native-xmb/src/view.cpp` and `native-xmb/src/view.h` only (plus this section and a verdict note in
`TRANSITION-HARNESS.md`). The shell-side overscroll count is returned as a hunk, not applied (the
`shell.*` fence). Every number is TRACED, FIT or NOT MEASURABLE and labelled where it is used.

Method note that matters for every verdict below: the extended burst grammar
(`burst=<prefix>@KEY@<frames>@<intervalMs>`) records an onset grab that the harness's previous native
captures did not have. That changes the progress axis on two of the four transitions (sections 1.3 and
2.4), so this pass reports both the harness verdict and a **time-aligned** track measured from the same
frames. The time-aligned track is the stable one: the same before build scores WARN 6 px with a
24-frame burst and FAIL 52 px with a 40-frame burst on page-close, because the harness's `p = 0` is a
template lock on the page glyph whose value depends on the sequence's first-frame composition.

## 0. Summary

| # | item | was | is | evidence |
|---|---|---|---|---|
| 1 | category move: entering icon + row block | reported pinned at the focus slot | **verified sliding from one bar step (+211 px) on the 200 ms mode-5 move**; no code change was needed - the fix is in HEAD (15.2) and the pinned verdict came from frames captured before it landed | 1.1-1.3, TRACED |
| 2 | page close | starts on the key, ~101 px ahead of the console at +57 ms | **holds 49 ms then runs the same 300 ms mode-4 content ease**; within 13 px of the console at matched time | 2.1-2.3, TRACED (hold bracket) + FIT (49 in the bracket) |
| 3 | options close | hard cut: every frame from +8 ms was the plain page | **latched content**: panel drawn through a 9 ms hold + 200 ms ramp, 105,653 px still different from the page at +8 ms, 0 at +205 ms | 3.1-3.2, TRACED (O6 9/200) |
| 4 | end-of-list overscroll | clamped silently | **projection half in the view** (40 px per press, label and icon together), count hook returned as a shell hunk | 4.1-4.3, inventory #13 + measured projection |
| + | page icon size on the open page | (checked, not changed) | **already right**: 113 px against the console's 111 px, before and after | 2.5, measured ink bbox |

## 1. The category move: the entering icon and its row block are already on the bar's motion

### 1.1 The stale verdict

The harness's stored native `cat-right` sequence was captured at 17:47, before `view.cpp` 18:52 (Phase
15.2). On that build the selected category's icon was drawn at `open ? pageX : ...`, i.e. at the focus
slot from the first frame, which is what the stored verdict records (`native 565 pinned`, delta -150 px
at p = 0). At HEAD the draw is

```cpp
x = open ? pageX + 200.*distance*recede : barFocusX + (200.*distance + 10.*sign)*recede;
```

so the entering icon starts one whole bar step (+200 x 1.087 on a combined move, +211 measured) and
lands on the focus slot when `categoryEase_` (200 ms, mode 5) reaches 0. The row block is the same
channel: `outSlide` 215 px on the ghost and `inSlide` 125 px on the entering column, both multiplied by
`(shell_.category() - categoryPosition_)`, and the entering label is drawn under the entering icon.

### 1.2 The console's own icon track, segmented by colour

The template track is unreliable at the start of a category move (it merges the two icons; the audit's
own `left1` track has a 73.7 px settled sigma). The PlayStation Network icon is a blue sphere, so it can
be segmented without a template: pixels with `b > 90`, `b > r + 35`, `g > r` in the category band,
reported as the sphere's bbox centre. Console `navmove/right1` (crop y 0..240 of a y-origin-180 crop)
against the final native build (full frame):

| t after key | console x | native x | delta |
|---:|---:|---:|---:|
| +16 | 758 | 707 | -51 |
| +26 | 723 | 688 | -35 |
| +36 | 699 | 670 | -29 |
| +42 | 685 | 661 | -24 |
| +54 | 656 | 645 | -11 |
| +66 | 635 | 630 | -5 |
| +79 | 632 | 620 | -12 |
| +89 | 618 | 612 | -6 |
| +102 | 605 | 604 | -1 |
| +113 | 595 | 598 | +3 |
| +140 | 588 | 586 | -2 |
| +163 | 577 | 580 | +3 |
| +190 | 575 | 575 | 0 |
| +214 | 570 | 565 | -5 |
| +240 | 569 | 565 | -4 |

The native's own before build (no item-1 change was made in this pass) measures the same numbers to
within 1 px, i.e. HEAD already had the slide. The 24-51 px lead in the first 40 ms is the burst's
synchronous tap: the onset grab forces a paint inside the tap, so the ease advances one frame before
the console's first frame; a live key press pays the same one-frame input latency the console's 552 ->
566 sample shows. The console's slow 4-5 px tail past +200 ms is not modelled (it would be a second,
slower channel; the burst cannot separate it).

### 1.3 Why the harness still says FAIL (and why that is the probe, not the move)

The final capture (`burst=@RIGHT@24@10`, onset at 7 ms): p = 0 track.x 715 console / 725 native
(+10, WARN), max |delta| 32.5 px, verdict FAIL. The residual is the `@motion` progress axis: the native's
window is 165 ms against the console's 198 ms because the onset grab's first interval (0 -> 7 ms) makes
the peak motion rate high and the relative detection threshold cuts the exponential's tail early. A
24-frame/12 ms capture of the same motion (the before build; item 1 changed no code) gives a 92 ms
window and max 58.3 px; the harness's own doc records native duration as a lower bound for this reason.
In time (1.2) the two agree within 6 px from +66 ms.

The left direction is the mirror (`distance` is -1): the entering Network icon starts at 565 - 200 =
365, and the console's left neighbour centroid is 356.8 (`navmove/right1` bar probe, p = -40), 8 px
apart. The harness's `cat-left` stays NO-DATA because its console template track is unstable and the
native `bar.pitch` does not resolve on that sequence; it is not a motion failure.

## 2. The page close has its own start

### 2.1 The console's close, measured frame by frame

The 642-frame burst (`C:\dev\out\rpcs3-xmb-cap\burst-png`, 139.7 fps, PgUp at 2706.9 ms) holds the page
icon in the y 220..380 band. Segmented as the bright ink bbox at threshold 115 in x 200..720:

| burst ms | t after key | icon left | moved |
|---:|---:|---:|---:|
| 2702-2752 | -5 .. +45 | 250 | 0 |
| 2760 | +53 | 312 | +62 |
| 2777 | +70 | 360 | +110 |
| 2794 | +87 | 396 | +146 |
| 2828 | +121 | 424 | +174 |
| 2861 | +154 | 466 | +216 |
| 2902 | +195 | 488 | +238 |
| 2951 | +244 | 502 | +252 |
| 3002 | +295 | 508 | +258 |
| 3102 | +395 | 512 | +262 |

The icon is untouched through 2752 and has moved 62 of its 262 px at 2760 - exactly one 60 Hz curve
frame (0.226 x 262 = 59). So the console's ease starts in (2743, 2760], a **hold of 37..54 ms after the
key**, and then runs the open's own shape: the per-frame retention is 0.762 (the audit's 0.7623), the
same within the tracker's -0.014 bias, so the close is NOT a different ease - it is the same 300 ms
mode-4 content ease with a hold in front of it. That is the mirror-breaking difference the harness saw:
before the fix the native's close started at the key and was ~50 ms early in time.

### 2.2 What changed

`PafEase::retarget` already takes a delay (added in Phase 15 for the options panel), and `paintGL` now
tells the depth eases which direction the page is moving (`depthSeen_` vs `shell_.depth()`), so a
closing move retargets both channels with `pageCloseDelayMs = 49`:

```cpp
const bool pageClosing = depthSeen_ >= 0 && depthNow < depthSeen_;
const double depthDelay = pageClosing ? pageCloseDelayMs : 0.;
depthEase_.retarget(shell_.depth(), pageMilliseconds, 4, depthDelay);
barEase_.retarget(shell_.depth(), barMilliseconds, 5, depthDelay);
```

49 is inside the measured hold bracket (37..54) and is the value that lands the native's first rendered
step on the console's 2760 render: PafEase's elapsed clock starts at the paint before the key (up to a
frame early), and measured against the console's +62 px at +57, a 41 ms hold overshoots by 22 px and a
57 ms hold lags by 36 px. The **bar channel's** share of the hold is a **FIT**: the burst cannot time
the bar layer's own start because its neighbours are already at the end state in the first visible
frame (section 1.3 of Phase 15), so it takes the content channel's measured offset. **The ease itself
is not re-fit.**

### 2.3 Measured before/after (same template, same frames)

Template track of the page icon (48x48 patch from the page-state frame, position reported in template
x, so both sides use the same axis; console column from the same segmented series as 2.1 with the
template's 22 px offset):

| t after key | console | before build | after build | before delta | after delta |
|---:|---:|---:|---:|---:|---:|
| +57 | 334 | 435 | 324 | +101 | -10 |
| +74 | 382 | 456 | 372 | +74 | -10 |
| +91 | 418 | 475 | 412 | +57 | -6 |
| +125 | 446 | 498 | 459 | +52 | +13 |
| +158 | 488 | 511 | 488 | +23 | 0 |
| +199 | 510 | 521 | 509 | +11 | -1 |
| +248 | 524 | 526 | 522 | +2 | -2 |
| +299 | 530 | 529 | 528 | -1 | -2 |
| +399 | 534 | 529 | 532 | -5 | -2 |

The close is now within 13 px at matched time across the whole move (the tail within 2 px), against up
to 101 px ahead before. The final build's own close is 244 ms from first to last moving sample against
the console's 217 (the harness's window measure; both include the hold).

### 2.4 The harness's page-close verdict after

`bun work/transition-compare.ts -t page-close` on the final capture: **FAIL**, max |track.x| 57 px, best
alignment offset **-20.0 ms** (was -55 ms with a 24-frame burst and -33.3 ms when the same before build
is captured with the same 40-frame burst). The 57 px is one grid point: p = 0 is the *page-state* glyph,
where the console's 48x48 bar template locks at x 360 and the native's at 303 for the same physical
icon (console ink bbox 250..360, native 249..361, delta 2 px). Every later grid point is within the
time-aligned deltas in 2.3. Capture stability, same before build: WARN 6 px (24-frame burst) vs FAIL
52 px (40-frame burst). The stable number is the time-aligned one, and it moved by 90 px.

### 2.5 Checked, not changed: the page icon's size is already right

The harness's page-close `track.scale` reads 1.1 on the console and 1.0 here, which looks like a missing
1.08716 (2700/2483.53) projection on the page's own icon. It is not. Measured directly on the same
threshold-115 ink bbox as 2.1, the page icon is **111 px** wide on the console (250..360) and **113 px**
on this build (249..361), before and after the phase, i.e. already within 2 px. The glass icon path
sizes the page's icon from `focus` (`117.13 + 50.2*focus` = 167.33 at the page, quad scale 1.0); the
1.087 camera factor applies to the console's bar layer, not to its page icon. The track.scale 1.0 is a
template-scale artefact (the 48x48 bar patch is not scale-sensitive on this glyph), not a geometry gap.
An experimental `iconScale` term was measured, found inert on the glass path and reverted, so the
settled depth-1 frame stays byte-identical and no rest frame moves.

## 3. The options panel's close is latched

### 3.1 What was wrong

The panel's draw was gated on `Shell::optionsOpen()`. The closing Triangle clears the options on the
same tick, so the panel vanished in one frame no matter what `optionsOpenEase_` did: measured on the
rebuilt before build, the panel band (x 1250..1920, y 450..760) is identical to the settled plain page
in every burst frame from +8 ms (0 pixels differ, mean 74.56).

### 3.2 The latch

`View::OptionsShown` keeps the last titles, heading, `MenuOption` rows, selection and which panel shape
(in-player vs item menu) while the shell still holds them, and the three draw sites read it while
`optionsOpenEase_ > 0.001` instead of asking the shell. The close retargets to 0 with the measured 9 ms
delay and 200 ms mode-5 ramp (UI-ELEMENT-GAP-AUDIT O6 [M]; no console close burst exists, so the
duration is O6's measured value, not a fit), and the page dim and the info-bar suppression use the same
latch so the page does not pop back to full brightness under a fading panel.

Measured on the same script with the final build (`full,wait,TRIANGLE,wait,wait,grab=open,
burst=@TRIANGLE@24@8,...`), panel band pixels differing from the settled plain page by more than 6
luma:

| t after closing Triangle | before | after |
|---:|---:|---:|
| +8 | 0 | 105,653 |
| +28 | 0 | 102,055 |
| +48 | 0 | 93,462 |
| +69 | 0 | 67,418 |
| +90 | 0 | 48,278 |
| +121 | 0 | 24,805 |
| +152 | 0 | 1,348 |
| +184 | 0 | 549 |
| +205 | 0 | 0 |

The band mean goes 91.1 -> 74.56 over the same window, and the settled open panel is **byte-identical**
before/after (SHA-256 prefix fc96b66d), so only the exit moved. The content persists for ~197 ms after
the hold, i.e. the panel is gone 205 ms after the key against O6's 9 + 200.

**NOT MEASURABLE:** O10 says the console's row *text* drops below 180 within one frame while the plate
ramps 200 ms. No console close burst of this panel exists on disk, so the text/plate split cannot be
separated from the stills; this pass latches one alpha for both, which is the measured plate's curve.
That split is recorded as open item 3 below.

## 4. End-of-list overscroll: the view half, and the shell hunk

### 4.1 The behaviour and its evidence

GRAPHICS-INVENTORY #13: "pressing DOWN on the last row does not move the focus; the whole column sinks
40 px per press and stays sunk. A build that clamps silently is drawing a different console." The
local frames do **not** re-measure it: `col-game-r06..r09` are near-identical (best integer shift 0,
mean |diff| 2.1-2.6 on the item column) and `col2-video-r02 -> r03` is the same (best dy 0). The 40 px
is therefore the inventory's claim (its frames' route is a live walk), and the easing is **NOT
MEASURABLE**: this build applies a straight offset, no curve.

### 4.2 The view half (implemented)

`View::setColumnSink(int presses)` is the one hook: a shell-owned press count. `drawColumns` adds
`overscrollStep * columnSink_` (40 px) to every row's y of the focused column only, so the icon, label,
value, subtitle and note travel together; the category bar and the other columns do not move. At rest
the count is 0 and every y is exactly what it was. `XMB_OVERSCROLL=<0..8>` is the capture-only pin
(same idea as `XMB_DEPTH_PIN`) that states the count so the projection is measurable before the shell
hunk lands; the sink amount also enters the UI-layer signature only while it is non-zero.

Verified with a scripted DOWN-at-end run (`full,wait,DOWN x10,wait,grab=end,quit`, Game column, last row
`Import Games and Apps`, selection 9). An extra DOWN leaves the selection at 9 (state sidecar), i.e.
the focus is clamped as on the console, and:

| XMB_OVERSCROLL | focused row's label ink rows | selection |
|---:|---|---:|
| 0 | 500-504, 513-517 | 9 |
| 1 | 540-544, 553-557 | 9 |
| 2 | 580-584, 593-597 | 9 |

+40 px per press, the label's ink rows move with the row, and the row's icon moves with them (the
`os-0..2` strip shows the icon and the label travelling together); it stays sunk, and the focus does not
move (selection 9 with the extra DOWN).

### 4.3 The shell hunk (returned, not applied)

The count belongs to `Shell`, because only the shell knows the move was clamped. The hunk is in the
reply to this pass and adds:

- `shell.h`: `int columnSink() const { return columnSink_; }` and `int columnSink_=0;`.
- `shell.cpp` `Shell::vertical`: on `delta > 0` and `previous == selection()` (already at the end),
  `++columnSink_; emit changed();`; reset to 0 on any successful move, on `horizontal()`, `back()` and
  `activate()`.
- `view.cpp` (one line, ships with the hunk): in the existing `Shell::changed` lambda,
  `setColumnSink(shell_.columnSink());` so the projection follows the shell's count.

## 5. Gates and determinism

- Canonical: `bun work/ab-layers.ts --capture --exe C:\dev\out\navfeel\certified\re-ps3-native.exe` ->
  **PASS 6/6** with `text presence: 1668 bright pixels in the clock crop (minimum 900) PASS`.
  frame 18.76 / 0.8360, top background 21.06 / 0.9434, category icon row 21.62 / 0.9062, item label row
  11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667 - every delta **0.00 / 0.0000**.
  (The capture runs above used `C:\dev\out\navfeel\final\`; the certified build differs only by a reverted
  experimental `iconScale` term that never affected the glass icon path the captures show - the page icon's
  measured ink bbox is 113 px on both.)
- Determinism: two `--render-clock 20` root captures are SHA-256
  `37e2b590c42ef0e3f3601ca492221147c9874d2e3b1785e6237b616f2c0bbc14` both - the hash Phases 13, 14.2
  and 15 recorded, i.e. the canonical rest frame is untouched.
- The options panel's settled state is byte-identical before/after (fc96b66d), and the depth-1 page
  frame is unchanged too (2.5). Nothing was re-baselined. No shell, dialogs, assets, main, ribbon, hdr,
  CMake, harness, budget or reference file was touched.

## 6. What remains

1. **The harness's progress axis on the onset-grab captures.** `cat-right` max 32.5 px and page-close
   p = 0 -57 px are `p = 0`/window artefacts, not curve errors (1.3, 2.4). The instrument needs a
   time-aligned track (or a pin at stated progress, which the probes JSON already has a `pins` slot
   for) before its headline can judge these two.
2. **The entering icon's size growth** lags the console: the segmented sphere grows 74 -> 106 px on the
   console but 78 -> 92 px here even though the glass quad grows 117 -> 167. The rendered glyph does not
   scale with the quad in `icons.cpp` (out of this pass's fence) - measured, not fixed.
3. **The options close's text/plate split** (O10): one alpha for both here; a console close burst would
   settle whether the row ink cuts before the plate.
4. **The page open's ~709 ms page-load delay** is still not modelled; with the onset grab the harness
   now samples the native's first frame at +11 ms against the console's +695 ms, so page-open's FAIL 61
   is that design difference (the before build scores 63 with the same capture), unchanged by this pass.
5. **The console's own overscroll sink** is not in the local frames (4.1); a live capture with two
   DOWNs past the last row would settle the 40 px and whether it eases.

## 7. Reproduction

```
# native captures (one process per burst; the file name carries the elapsed ms)
$env:XMB_INPUT_SCRIPT_ONLY='1'
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <dir>/data ^
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00 ^
  --focus-time 2000 --script "full,wait,RIGHT,wait,grab=<p>-before.png,burst=<p>@RIGHT@24@10,wait,wait,grab=<p>-after.png,quit"
# page close: ENTER on Settings, then burst=@ESC@40@10
# options close: TRIANGLE, waits, burst=@TRIANGLE@24@8
# overscroll: DOWN x10, then grab with XMB_OVERSCROLL=0|1|2

# harness + recordings
bun work/transition-compare.ts -t cat-right -t cat-left -t page-open -t page-close
bun work/transition-compare.ts -t cat-right -t page-close --render

# gates
bun work/ab-layers.ts --capture --exe C:\dev\out\navfeel\certified\re-ps3-native.exe
```

Artifacts (never committed, this machine): `C:\dev\out\transitions\` - `native\{cat-right,cat-left,
page-open,page-close}\` (the final captures), `cat-right|cat-left|page-open|page-close-side-by-side.gif`,
`*-contact-sheet.png`; `C:\dev\out\navfeel\` - `certified\` (the build), `before-build\`, `optclose-before\`,
`optclose-final\`, `os-{0,1,2}-final\`, `base-cat\`, `pclose-before\`, `cr10-{1,2,3}\` (the burst
captures), `gate-certified.log`, `certdet1\`, `certdet2\`.

