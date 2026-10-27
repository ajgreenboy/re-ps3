# Info-bar continuation — 2026-10-25

The saved 1920×1080 firmware root capture (`out/native-reference/ab-rpcs3-root.png`) places the bar at approximately (1329,75), size 591×58, with a clock icon centered near (1791,104). Native previously drew a 430×88 bar at (1490,60) and omitted the analog clock.

Native now uses the measured bounds and an analog minute/hour clock. `Shell::clockDateTime()` is shared by the text and clock hands, including the selected time zone and capture override. Cache invalidation follows the displayed minute. The original `tex_infobar` strip is composited at restrained opacity with a subtle outline; opacity/outline and procedural clock are measured approximations, not traced firmware materials or geometry.

The new 1600×900 scripted captures show the corrected proportions and clock placement. Runtime PAF projection, exact clock geometry, typography phase and a matched firmware pixel score remain open. Do not replace this geometry with zero-valued dynamic RCO template fields. Read-only infobar initialization traces remain under `out/native-reference/phase3-infobar-*.c`.

## Clock format: US date and 12-hour time (2026-10-26)

`Shell::clockText()` is the single source for the information-panel clock and every other clock surface,
and it now composes the string from the console's own two options instead of a fixed format:

| key | 0 | 1 (default) | 2 |
|---|---|---|---|
| `clock/dateFormat` | DD/MM | MM/DD | YYYY/MM/DD |
| `clock/hourFormat` | 24-hour | 12-hour | - |

A fresh data directory is therefore a US console: `MM/DD h:mm AM/PM` in the information panel
(`10/25 12:32 PM`, `10/25 7:05 PM`) and `MM/DD/YYYY h:mm:ss AM/PM` on the Date and Time screen. Why that
composition: the board carries the short date plus the time (the reference frame carries `25/10 12:32`,
11 characters, in the same slot), both date fields are zero-padded because the firmware's own option label
is `MM/DD/YYYY` and its reference frame pads its day (`25/10`), and the hour is not padded because that is
the US 12-hour convention. The order comes from the stored preference, so Settings > Date and Time >
Date Format and Time Format still offer DD/MM/YYYY, MM/DD/YYYY, YYYY/MM/DD and 24- or 12-hour; those rows
were already wired to `clockText()`, only their defaults were non-US.

The Date and Time screen's detail line used `QDateTime::currentDateTime()` and a hardcoded English format,
so it ignored both `clock/timeZone` and `--ui-clock`. It now formats `clockDateTime()` with the same
preference-driven full date/time, which makes that screen a pure function of `clock/dateFormat`,
`clock/hourFormat`, `clock/timeZone` and the capture override.

### One space, not two

The date and time were joined with two spaces. The reference's word gap is one space. Bright-glyph columns
of the info-bar crop (x 1400..1772, y 80..135) measure:

| frame | glyph bbox | date-to-time gap |
|---|---|---|
| firmware reference `ab-rpcs3-root.png` | 169 px | 16 px |
| native, two spaces | 166 px | 25 px |
| native, one space | 175 px | 16 px |

The one-space render matches the reference gap exactly; the residual 6 px of width is font advance, not
spacing. Layer score for the same DD/MM + 24-hour setting, two captures that differ only in that
character (offline scoring with `bun work/ab-layers.ts --native`):

| variant | clock text | info bar |
|---|---|---|
| two spaces | 13.66 dB / 0.6198 | 17.26 dB / 0.7098 |
| one space | 13.61 dB / 0.6226 | 17.21 dB / 0.7114 |

So the fix corrects the measured geometry but does not move the clock layer score: -0.05 dB PSNR,
+0.0028 SSIM (SSIM is the one that improves). That crop is dominated by glyph advance and position, not
by the space.

### Deliberate deviation from the stored reference

The stored RPCS3 frame was captured on a DD/MM, 24-hour console, so the US default no longer matches its
string and the two clock layers move by design:

| layer | stored baseline | US default | delta |
|---|---|---|---|
| clock text | 13.51 dB / 0.6131 | 11.39 dB / 0.4105 | -2.12 dB / -0.2026 |
| info bar | 17.12 dB / 0.7046 | 14.78 dB / 0.6117 | -2.34 dB / -0.0929 |

Every other layer is unchanged (frame -0.07 dB, inside the 0.5 dB budget), which confirms the change
touches only the clock string. Setting the preferences back to DD/MM + 24-hour reproduces the reference's
own `25/10 12:32` and scores 13.61 dB / 0.6226 and 17.21 dB / 0.7114, so the difference measures the new
default, not a regression. All four named-state self-baselines were captured with the pinned
`uiClock 2026-10-25T12:32:00` and contain the old string, so they now fail their 55 dB gate
(`steam-game-root`: 34.28 dB / 0.9970 while its item column stays bit-identical); refresh them
deliberately with `bun work/ab-layers.ts --states --save-reference` once the format is accepted.

---

# Information-panel clock writer - 2026-10-26

Appended pass on the open question from `audit/PAGE-MOVE.md:194-206` (which module calls the clock setter
once per displayed minute). Appended, not rewritten; no source edit, no git command. ASCII only in this
section.

**Headline: the premise was wrong, and the writer is now followed to a module boundary.** The XMB
information-panel clock is not a paf `PhClock` at all - it is a `PhText` object named `clock` in
`xmb_plugin_normal.rco`. The call that writes it is found, and it ends inside `vsh.elf` at VA 0x33FB58,
a function that is not in any local decompile.

## 1. The widget is a PhText named "clock", not a PhClock

From `out/native-reference/rco-behavior/resources.json` (122 resources / 8,070 objects / 0 errors), the XMB
indicator page is:

```
Page   page_xmb_indicator
Plane  indicator
Plane  indi_base
Plane  indi_adjust          children: 6
Text   clock                parent: indi_adjust
Plane  message              parent: indi_adjust
Plane  chat                 parent: indi_adjust
Plane  online               parent: indi_adjust   -> Text online_num
Plane  avatar               parent: indi_adjust
Plane  plus                 parent: indi_adjust
```

`xmb_plugin_normal.rco` and `xmb_ingame.rco` each hold 26 objects and **no `Clock` object**. A schema scan of
all 122 resources finds exactly five `Clock` objects (typeId 0x816), all in `sysconf_plugin.rco`:
`clock_date_time_config_set_date_time`, `clock_power_off_date_time_config_set_date_time`,
`clock_first_config_datetime_set`, `clock_tool_psstore_ad_clock_datetime_set`,
`clock_tool_preorder_timer_datetime_set` - i.e. the Date and Time settings pages (fields and their
min/max year attributes are the ones `audit/PAGE-MOVE.md:157-172` decoded).

Consequences:

- The claim "the firmware clock is the paf class `Clock` ... created from the RCO page template"
  (`PAGE-MOVE.md:152-155`) is contradicted for the XMB surface: the XMB has no `Clock` object, and the
  top-right date/time is a `Text`. It stands for the settings pages, which are the only `Clock` users.
- `page_infobar` in `system_plugin.rco` is not the XMB clock either: it holds `Plane infobar` plus
  `Text infotext1` / `Text infotext2` (notification text) and `infoicon` / `infoicon_shadow`. That page is
  the notification bar, and it is what `out/native-reference/phase3-infobar-*.c` traces.
- Everything `PAGE-MOVE.md:146-206` establishes about `PhClock` (passive, 8 fields, minute display, no
  ticker inside paf, setter 0x1B75E85F imported only by `sysconf_plugin`) remains true and is re-confirmed
  below - it just does not describe the XMB clock.

## 2. The writer chain, as far as the artifacts reach

`xmb_plugin.prx` builds the indicator page and writes the clock in `FUN_0000cb44`
(`out/native-reference/xmb_plugin_all.c:6249-6291`):

| line | call | what it is |
|---|---|---|
| 6249 | `FUN_000026e0(DAT_0001beac)` | get the indicator page |
| 6251 | `paf_paf_D557F850(page, "indicator", 0)` | find `indicator` by name |
| 6255 | `paf_paf_D557F850(page, "indi_base", 0)` | find `indi_base` |
| 6261 | `paf_paf_D557F850(page, "clock", 0)` | find `clock` by name |
| 6265-6266 | `paf_paf_B7DFCE90()` then `(*(vtable[0]))(clock, class)` | type check: `paf_B7DFCE90` returns `PTR_s_PhText_006fb214` (`paf-range.c:22496`), so this is "is this object a PhText" |
| 6270 | `(*(vtable+0xb0))(clock, 0x39, local_50)` | read property 0x39 into a local |
| **6271** | `vshcommon_vshcommon_85AD7A58(clock, param_1 + 0x18)` | **the write**: clock Text plus the plugin's display state at +0x18 |
| 6275-6286 | same lookup/type-check for `online_num` | proves the pattern is "find + type check + set", not a clock special case |
| 6291 | `paf_paf_986427A7(page, 0x2000e, &PTR_LAB_0001baac, param_1)` | teardown handler, see below |

`vshcommon_85AD7A58` resolves to `vsh.elf` VA **0x33FB58** (`rea/vsh_exports.csv`:
`vshcommon,0x85AD7A58,0x006DE098,0x0033FB58,0x006F5558,.opd.FUN_0033fb58`). So the chain is:

```
xmb_plugin.prx FUN_0000cb44 (xmb_plugin_all.c:6271)
  -> vsh.elf 0x33FB58 (vshcommon, export NID 0x85AD7A58)  [body not in the artifact set]
       -> ? the per-minute refresh, or a one-shot text write
```

Two things this chain already settles:

- **`xmb_plugin.prx` cannot read the time itself.** Its import table has no `cellRtc`, no `sys_time` and no
  clock-NID entries (`rea/plugins/xmb_plugin.prx_imports.csv`: the only non-paf imports are sysPrxForUser,
  stdc and vshcommon). So the time and the formatting must come from 0x33FB58 or from the state block the
  plugin hands it. A "1 s / 60 s timer in xmb_plugin" therefore cannot exist; if the clock ticks, the tick
  is on the vshcommon side.
- **Event 0x2000e is not a tick.** It is fired by paf when a widget tree is destroyed:
  `_opd_FUN_0039e0bc` (`paf-range.c:17680`) walks and destroys the children and then
  `_opd_FUN_0037de04(param_1, 0x2000e, auStack_40)` at `:17699` with event code 0x107. The handler
  xmb_plugin registers on it at `:6291` is therefore a cleanup hook, not a periodic callback. Anyone
  searching for "the clock tick" by event id should not stop at 0x2000e.

## 3. No periodic writer exists in the paf PhClock path (re-confirmed)

- The clock setter `_opd_FUN_003dedf4` (export 0x1B75E85F, OPD 0x006E0AC8) has exactly **one** call site in
  `vsh.elf`: 0x3E0D50, inside the clock constructor `_opd_FUN_003e056c` (`paf-range.c:53970-54116`, call at
  `:54113`). A `bl` scan of the whole first PT_LOAD confirms this (1 hit). paf initialises a clock once and
  never updates it.
- The clock constructor `FUN_003de988` (export 0x7D594F19) has four `bl` callers in `vsh.elf`
  (0x3A985C, 0x3C8DEC, 0x3D2E74, 0x3D7C64), all inside paf composite widgets: 0x3A985C is a date/time
  configurator (it allocates the widget, sets year bounds at +0x298/+0x29C, and creates a `Clock` child at
  +0x2DC, `paf-range.c:24720-24764`). No plugin imports 0x7D594F19 at all.
- Only `sysconf_plugin.prx` imports the setter (`rea/plugins/sysconf_plugin.prx_imports.csv`), and
  `explore_plugin` / `explore_category_game` / `explore_category_user` import only the getter 0xBBFF3B2F.
- paf has no ticker to inherit: the clock's display state is recomputed only when a field write sets the
  dirty flag (+0x2C4) and `FUN_003dec98` / `FUN_003dfbac` (`paf-range.c:53656`) rewrite the six digit
  children. The event ids the constructor registers (0x20001, 0x20005, 0x20006, 0x80002, 0x80003, 0x80006,
  0x180009-0x18000b) are all edit/value notifications.

## 4. The missing artifact, and how to obtain it locally

Missing: the body of `vsh.elf` VA 0x33FB58 and its callees. `out/native-reference/paf-range.c` starts at
0x3800B0 (first header, `paf-range.c:1`) and covers the paf region only; the local vsh.elf slices below that
are seven dialog/infobar helpers covering 0x3406F8, 0x340BE0, 0x3415E8, 0x341870, 0x341BFC, 0x341D30 and
0x343FFC (`phase3-system-layout.c:1,24,162,210,281` plus `phase3-infobar-common.c:1,16,37`; the fifth
system-layout header, 0x387550, is outside the region). 0x33FB58 is inside vshcommon, roughly
0x33F000-0x340000, which is entirely absent. Two more machine facts narrow it:

- A `bl` scan of `vsh.elf` finds **zero in-module callers** of 0x33FB58, so it is export-only: nothing in
  vsh.elf drives it on a timer, and the periodic behaviour (if any) is inside it (a `cellSysutil` callback, a
  paf timer/animation-slot registration, or a comparison of a stored minute against a fresh one).
- Its export neighbours are a contiguous vshcommon block (`rea/vsh_exports.csv`): 0x73F85259 -> 0x33F2FC,
  0xE011E7D0 -> 0x33F384, 0x9BD8429E -> 0x33F40C, 0x2438F1A4 -> 0x33F494, 0xF7A67D49 -> 0x33F51C,
  0x60DEE5B3 -> 0x33F5BC, 0x75DC9C2D -> 0x33F65C, 0x79A562D5 -> 0x33F6FC, 0xB9473E7A -> 0x33F79C,
  0xD1DE9F38 -> 0x33F7C8, **0x85AD7A58 -> 0x33FB58**, 0xAE251F8F -> 0x33FCC8, 0xE43C21F6 -> 0x34002C. That
  block is the whole artefact needed; it is about 3.4 KB of code.

Local, read-only recipe (the existing project, no re-analysis, no network):

```
analyzeHeadless <projectDir> vsh493 -process vsh.elf -noanalysis -scriptPath ghidra_scripts ^
  -postScript DecompileRange.java <out.c> 33fb58 340050
analyzeHeadless <projectDir> vsh493 -process vsh.elf -noanalysis -scriptPath ghidra_scripts ^
  -postScript RefsTo.java <out.txt> 33fb58
```

with `JAVA_HOME` set to the JDK of the existing project and `JAVA_TOOL_OPTIONS=-Duser.home=C:\dev\xmb\ghidra_home`,
as in `audit/LAYOUT-TRACE.md:126-134`. Then read `<out.c>` for (a) the text write, (b) any 1 s / 60 s / 60-frame
comparison, and (c) any callback registration; and cross-check the callees with the same `bl` scan. If a timer
appears, the interface list in `rea/plugins/*_imports.csv` for vsh.elf (`rea/vsh_imports.csv`) should show the
corresponding syscall import - it currently lists no `cellRtc` and no `sys_time`, which is itself a hint that
0x33FB58 gets its time from a *caller-supplied* struct (the plugin state at +0x18) rather than from the RTC.

Alternative that needs no decompile: settle the behaviour end-to-end. Pin the console clock close to a minute
boundary (the native equivalent exists as `--ui-clock`), capture the RPCS3 info bar for two minute rollovers
at >= 1 frame/s with CopyFromScreen, and score the clock crop per frame (`bun work/ab-compare.ts` on the
clock region, or `work/region-luma.ts` for a change detector). If the text changes at the rollover the writer
is live and periodic; if it does not change until the XMB is left and re-entered, the clock is written once
per page setup and the native's per-minute invalidation is a harmless superset rather than parity.

## 5. What to stop claiming

- "The widget is a passive paf `PhClock`" (`audit/PAGE-MOVE.md:155`, and the same wording in
  `HANDOFF-PHASES.md:83-84`): for the XMB information panel this is contradicted by the RCO object tree; the
  XMB clock is a `PhText` named `clock`. Keep the `PhClock` description only for the Date and Time screens.
- "The periodic writer has not been found" is still true, but it is now bounded: it is inside
  `vsh.elf` 0x33FB58 or it does not exist. It is not in `explore_plugin`, `xmb_plugin`, `basic_plugins` or
  paf, and no module in the set imports an API that could provide it.
- The minute-resolution invalidation already in `Shell::clockDateTime()` remains consistent with every
  observation (the reference frame is minute-resolution; nothing captured shows a seconds field), so it is
  not contradicted and does not need changing on this evidence.

