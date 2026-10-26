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

## 4. Info-bar clock

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
