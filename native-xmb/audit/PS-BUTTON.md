# The PS button: what the console does, and what this application now does

Captured from the real 4.93 XMB in RPCS3 on 2026-10-26, then implemented with the DualShock/DualSense
HID input the app already reads.

## The console's behaviour (measured)

- **Tap PS** - the XMB comes back to the front and a controller indicator flashes beside the clock in the
  information panel (a pad glyph and the controller number, then "Charge Level -"). Nothing else changes.
- **Hold PS for about a second** - the frame goes completely black and the system menu appears:
  - centred, two white lines: **Turn Off the System** (focused, brighter) and **Turn Off the Controller**;
  - the date/time stays at the top right;
  - bottom left, the controller/charge panel: numbered slots 1-4 with the active one filled red,
    "Controller 1", "Charge Level -";
  - along the bottom, the button legend: **Cross Enter**, **Circle Back**.
- While a title is running the same menu carries a **Quit Game** entry above those two (not captured here:
  no game is installed in the local RPCS3, and the firmware string is the evidence for that entry).

## How the capture was made

`audit/HANDOFF-PHASES.md` documents the boot recipe. In short: a scratch config with exclusive fullscreen
disabled, `rpcs3.exe --no-gui --config <scratch.yml> <dev_flash>\vsh\module\vsh.self`, then CopyFromScreen
frames (PrintWindow is stale under Vulkan). For this capture the global input config was temporarily
replaced with a Keyboard handler - vsh.self ignores `--input-config` - mapping **PS Button to Backspace**
and Cross/Circle to PgDown/PgUp (letters do not arrive through RPCS3's keyboard handler; this mapping does).
The original `Default.yml` was backed up first, restored immediately afterwards, and its SHA-256 checked
against the recorded original `E624DA0D...CEEE`; RPCS3 was stopped afterwards.

Frames are kept locally under `out/native-reference/ps-menu/`:
`00-root.png` (XMB), `02-after-tap.png` (tap indicator), `03-hold-06.png` (the menu).

## What this application implements

| Input | Behaviour |
|---|---|
| PS on a DualShock 3 / DualShock 4 / DualSense over HID (button bit 0x400, `src/pad.cpp`) | tap < 1 s: closes the menu if open, otherwise flashes the controller indicator; hold >= 1 s: opens the system menu |
| Keyboard `F10` | opens the system menu (substitute for a controller) |
| While the menu is open | arrows / D-pad move, Cross / Return confirms, Circle / Escape backs out; the rest of the XMB does not see input |
| Script runner | `ps` and `pshold` tokens drive the two paths for captures |

Menu entries: **Turn Off the System** (asks once: "X turns the XMB off; the PC stays on" then quits the
application), **Turn Off the Controller** (explains that a pad connected to a PC is owned by Windows and
cannot be switched off from here), and **Quit Game** whenever a tracked title is running (it terminates
that process and reports it).

### Honest limits

- "Turn Off the System" on the console removes power. Here it closes the XMB and returns to the Windows
  desktop; the confirmation line says so. Wiring it to a confirmed suspend or shutdown of the PC is a
  separate, deliberate change (see `HTPC-IDEAS.md`).
- "Turn Off the Controller" cannot power a PC-owned pad off; the entry is kept because the console has it,
  and it explains instead of pretending.
- The console's menu also carries a "Quit Game" row only while a game runs; the app matches that, but a
  Steam launch is tracked through Steam's registry, so the row closes the title the app itself launched,
  not one started outside it.

## Verification

Scripted captures (`--script "full,wait,wait,pshold,...,grab=...,DOWN,...,ENTER,...,ESC,...,ps,...,grab"`):

- `out/native-reference/ps-menu/native-ps-menu.png` - the menu, matching the console layout above;
- `native-ps-menu-down.png` - selection moved to "Turn Off the Controller";
- `native-ps-menu-confirm.png` - the confirmation step;
- `native-ps-hud.png` - the tap indicator over the XMB.

The log records `shell: PS menu opened` and each selection change. The existing layer gate is unaffected:
the menu only draws while it is open.

## Scripted runs and the live pad (input gate, 2026-10-27)

### The failure

A scripted capture of a settings page logged 188 `vertical(-1)` calls with no script key pressed. The
script's own DOWN presses were undone before the next token, so the row under the cursor was whatever the
controller had just chosen, not what the script had asked for. Captures of that route were not reproducible.

### Root cause (measured, not inferred)

The live controller was the source, and it was read correctly:

- `--pad-report` shows a DualSense over USB attached throughout (`kind=DualSense`, `connection=USB`,
  `battery=100% (full)`).
- The device's own HID report descriptor and a live report were read with `HidP_GetUsageValue` by a local
  probe (`out/input-probe/`), not with this application's parser: game pad collection `0001:0005`, report
  id 1, six 8-bit axes at report bytes 1-6. The live report `01 63 37 5e 87 00 00 ...` decodes as X=99,
  Y=55, Z=94, Rz=135 (Rx and Ry, the triggers, 0). `src/pad.cpp` reads those same bytes:
  `lx=(99-128)/127=-0.23`, `ly=(55-128)/127=-0.575`.
- `View::pollController()` (`view.cpp:1986`) merges `pad::latest()` into the navigation bits and turns
  `ly < -0.5` into D-pad Up; a held direction repeats every 67 ms (`view.cpp:2027`). -0.575 therefore
  produced a `shell_.vertical(-1)` every 67 ms for as long as the script ran: 188 calls = 12.6 s of a stick
  held up. The value was constant across the samples read (a held or badly centred stick, not jitter or a
  one-off report), and the script token parser was not involved.
- The parser is not shifted: the HID stack's own X/Y/Z/Rz for the same bytes match what pad.cpp's offsets
  read, and the hat/PS bits come from the bytes the descriptor names (hat=8, i.e. null, in the samples).

So the defect was structural: nothing distinguished "the script is driving" from "a live controller is
attached", and `pollController()` merged the second into the first.

### The gate

`src/main.cpp` turns the gate on when `--script` is present or `XMB_INPUT_SCRIPT_ONLY=1`, before the View
(and therefore before the pad reader and the PS watcher) starts, and logs the choice once:

    input: scripted run; live pad events ignored (XMB_INPUT_SCRIPT_ONLY)

While the gate is on, `pad::latest()` - the one place the shell can see a live state - delivers nothing:
D-pad, stick, trigger, button and PS bits are parsed, observed and counted, then withheld. The pad is still
read, so `--pad-report` and the battery line behave exactly as before. `PsWatcher` is affected too: a live
PS press is logged (`input: PS press observed on the pad; ignored (scripted run)`) but cannot open the
system menu, which is navigation. Scripted `ps`/`pshold` tokens still work because they enter the watcher
through `replayPress()`/`replayRelease()`, not through `latest()`.

With no `--script` and no `XMB_INPUT_SCRIPT_ONLY`, `setScriptOnly(false)` logs
`input: live pad events accepted (no --script, XMB_INPUT_SCRIPT_ONLY unset)` and `latest()` returns exactly
what it returned before this change.

### Diagnostics

- `XMB_INPUT_TRACE=1` logs each input decision through Qt's message handler (so `XMB_LOG=<file>` collects
  it): `input: trace source=script|pad|keyboard action=<name> accepted=0|1`. Pad lines are throttled to a
  change of action and at most one a second, and carry the running `ignored=<n>` count, so a stick held for
  a minute stays a handful of lines. Keyboard lines are logged when the shell dispatches a navigation key;
  `accepted=1` there means "not gated", and the focused page decides what to do with the key. The script's
  own taps are logged as `source=script` because they call the view directly.
- The scripted sidecar (`grab=...png` also writes `...png.json`) now carries `padEventsIgnored` (live
  samples withheld: one per `latest()` call that saw a button, a trigger past 0.5 or a stick axis past the
  0.5 deadzone) and `inputScriptOnly`. A deterministic capture reports a nonzero count while its frame and
  selection are still the script's own.
- `XMB_PAD_FAKE=<tokens>` is a local diagnostic, unset in every normal run: it stands a synthetic state in
  for a controller (`up|down|left|right`, face/shoulder buttons, `lx=|ly=|rx=|ry=|l2=|r2=`), so the live
  path and the gate can both be proved without moving hardware. It is sampled exactly like a real pad, so a
  scripted run ignores it too.

### Verification (2026-10-27, build `out/native-input`, exe built 22:42)

The failing case is a settings page that needs DOWN: category Game -> five LEFT (Settings) -> five DOWN
(System Settings) -> ENTER -> two DOWN. Two runs with the same script and `XMB_INPUT_TRACE=1`:

- trace: `input: scripted run; live pad events ignored (XMB_INPUT_SCRIPT_ONLY)`, then
  `input: trace source=pad action=stick-up accepted=0 ignored=1..540 (scripted run)` interleaved with
  `source=script action=DOWN accepted=1`; both runs end at `category=settings, depth=1, entryCount=9,
  selection=2, entryTitle=Format Utility` (`entryId=disc_utility`), with `inputScriptOnly=true` and
  `padEventsIgnored=539`/`539` (the counter at grab time; the trace continues one more second after it);
- the two PNGs are byte-identical: SHA-256
  `117B85D63CE19E90AFB67B9610ED7C9149ED18A5C9E7B280E587F6C6C4D184E0`;
- a third run with the same script and `XMB_PAD_FAKE=ps` logs
  `input: PS press observed on the pad; ignored (scripted run)`, opens no menu, and produces the same
  byte-identical PNG: a live PS bit is observed and logged but cannot inject navigation;
- `--pad-check` still parses the five synthetic reports and `--pad-report` still reports the attached
  DualSense (`attached=true, DualSense, USB, 100% full`), so the parser and the reader are unchanged (the
  parser is proved separately from the polling).

Non-scripted path (no `--script`, XMB window in the foreground, `XMB_INPUT_TRACE=1`):
`input: trace gate scripted=0 trigger=none` and `source=pad action=stick-up accepted=1` from the real pad.
A synthetic `XMB_PAD_FAKE=down` moved the Game column to its last row
(`final-fake-down.png`, SHA-256 `FA9DED9558F9A7CCC31DFD33F0FBD3E15263D4789717F596EE80D42E7A7BB0CA`),
while the same run without the fake stayed on row 0
(`final-baseline.png`, SHA-256 `938A110AC45809ABAF1F2E13358F5EF77147CEBA90BE413834411778D654B61E`),
so pad input still reaches the view. A real keyboard key in the same run is traced as
`source=keyboard action=LEFT accepted=1`, which shows the trace covers all three sources.

### Honest limits

- **XInput pad 0 is not gated here.** `View::pollController()` also merges `XInputGetState(0)`
  (`view.cpp`), outside this change's write fence. No XInput device is attached to this machine - the only
  game pad present is the DualSense on HID - so it contributes nothing to these runs; a machine with an
  Xbox-style pad attached would still let that pad steer a scripted run until the view pass gates its own
  merge.
- **Real keyboard input is not gated**, deliberately: a scripted run drives the shell through the same key
  handling, and the requirement is that the live controller must not steer. Keyboard decisions are traced
  as `source=keyboard`.
- The gate withholds PS from `PsWatcher`, so a physical PS press during a scripted run does nothing to the
  UI (it is logged). Use the `ps` / `pshold` tokens for scripted PS captures.

## Measured pass, 2026-10-28: the entries region and the tap plate

The corpus's first firmware frames for this surface (batch `rpcs3-gaps-2026-10-28`, section 16 of
`PARITY-SCORECARD.md`) gave the two composites their first Tier B numbers and named the causes. This pass
fixed them against the frames themselves, not against the scorecard's summary of them, and re-scored on
`C:/dev/out/native-psmenu/re-ps3-native.exe` (sha256 `DE4378BB9EB746BC2723E2D3F38D0AAE054709EAE61836BFE544C3A5155F6953`).
The references are `out/parity-references/ps-menu__original__day__1080p.png` (byte-identical to
`frames/a21-ps-menu-1.png`) and `out/parity-references/info-indicator__original__day__1080p.png` (= `frames/a17-tap-1.png`).

### 1. Two corrections to the record before anything was changed

**The entries pitch is 40 px, not 43.** SCORECARD 16.5 read the threshold-88 band tops (511 and 554) as the
pitch. Those tops are not the same feature: the focused row's bloom raises two rows above its cap, so its
band starts at 511 while its glyphs start at 514. Cross-correlating the shared prefix "Turn Off the " over
x 831-958 gives a maximum at a 40 px shift (cosine 0.971 at 40, 0.919 at 39, 0.807 at 43), and the cap tops
of the two 'T' glyphs are 514 and 554 at threshold 230. The native's pitch was already 40; only its offset
was wrong, which is why the block read 3-6 px high. Applying 43 would have moved the rows apart.

**The entries are left-aligned, and the focused row is larger.** Both rows' ink starts at x 831/832, so the
two rows are not centred on the same axis. The shared prefix "Turn Off the " measures 156 px on the focused
row and 146 px on the unfocused one, and their caps are 18 px and 17 px: the selected entry is one size
larger (~6.8%), not merely brighter. The unfocused row is white (255 core), not the 160 grey this build drew.

### 2. The PS menu, measured (1920x1080, `a21-ps-menu-1.png`)

| element | measured |
|---|---|
| clock | ink x 1613..1802, y 166..185 (the menu's own clock, not the info bar's y 94..115 position) |
| header rule | 1 px at y 199, full width, luma 175 over black |
| entries | left edge 831; focused ink 831..1072, cap 514..531, descender to 537; unfocused 831..1088, 554..571 (threshold 150) |
| pitch | 40 px (see 1) |
| focus bloom | 106 luma 3 px above the cap, 59 at 6 px, 27 at 9 px, 10 at 12 px -> Gaussian sigma ~5 px |
| slot numbers | x 137-141 / 180-188 / 223-232 / 267-277, y 805-819 |
| slots | 17x17 at x 132 / 176 / 221 / 265, y 828-844; active RGB (160,13,13), inactive (30,30,30) |
| `Controller` | ink x 311..421, y 803..821; pad glyph x 448..478, y 804..825; `1` x 496..502, y 805..822 |
| `Charge Level` | ink x 311..457, y 843..865 (descender); dash x 484..492, y 853..855 |
| panel ink rect | x 132..502, y 803..865 |
| footer rule | 1 px at y 880, full width, luma 175 |
| legend | X x 726..743, `Enter` x 762..814, O x 1155..1174, `Back` x 1192..1240, all y 897..916 |

The panel was at y 675-739 (slots 682-702, labels 675-739). It is now drawn at the console's rects above:
labels at x 311 (size 24), numbers above the slots, the pad glyph between `Controller` and `1`, the charge
dash centred on x 488, the slots at 17 px with the measured colours. The footer legend moved from x 856/1010,
y 850-890 to the measured x 726/1155, y 897-916, and the two full-width rules were added.

**The catalogue's `ps menu footer` crop does not contain the footer.** `[140,650,600,130]` is y 650-780;
the console's panel is y 803-865 and its legend y 880-916. With the panel now where the console has it, both
sides are black in that crop and the region scores Infinity / 1.0 - an honest result, not agreement. The
exact rects (reported for a future catalogue change, which this pass's fence does not allow):
panel `[120,790,400,90]` (ink x 132-502, y 803-865), legend `[700,875,560,50]` (ink x 726-1240, y 880-916).

### 3. The tap indicator: the plate, and why it had never appeared

**The plate.** The console frame's plate is a rounded box at **x 1191..1324 (134 wide), y 81..126 (46 tall)**,
corner radius ~4 px, flat RGB **(53,49,41)** in the settled frame (samples (1195,95), (1255,95), (1310,105)
all read the same). There is no separate border or drop shadow beyond one anti-aliased edge pixel. Its
contents: pad glyph ink x 1200..1229, y 94..115; `1` x 1239..1245, y 95..112; `-` x 1294..1302, y 105..107.
SCORECARD 16.5's "plate at x 1185-1310, y 90-125" is not the frame's edge (it is a dark-pixel subset
measured at a lower threshold); the frame's own edges are the numbers above and are what the build draws.
The plate is drawn opaque at the measured colour: the fill is flat to under a level across a background that
varies by ~3 levels behind it, so any alpha left in the settled frame is below the frame's own quantisation,
and the fade is the measured envelope below.

**Why no plate had ever been seen.** The `ps` script token never reached the tap handler. `PsWatcher::poll()`
gets no HID sample while the pad gate is on and deliberately does not treat a missing sample as a release
("a controller dropout is not a release"), so the injected release was invisible and the watcher's 1 s rule
fired instead. The probe (`C:/dev/out/psfix/tap-probe-ps.log`, 2026-10-28) records it exactly:

    script: token ps (tap)
    script: PS press injected
    script: PS release injected
    hold after 1001 ms
    event: hold ...
    event: system menu opened rows=2 ...

The parity route grabs 300 ms after the token and quits at 600 ms, i.e. before the 1001 ms hold, which is
why every previous capture measured an empty indicator and why the Tier C evidence never noticed: there was
no tap line to miss. The token now drives the tap handler the watcher would call, at the press instant
(`View::replayPsTap`, view.cpp; `ps_watcher.cpp` is outside this pass's write fence). A real pad's tap still
goes through the watcher, and `pshold` still does.

**Timing, measured.** The console burst (`burst/info-indicator-tap/`, MANIFEST 4) has PS pressed at 100 ms
capture time: the plate starts 155 ms after the press, is at full opacity 66 ms later, stays at full luma
from 321 ms to 2322 ms (2001 ms), and is back to the background 66 ms after that (2288 ms after the press;
2.39 s in capture time). The build implements exactly that envelope (`View::psHudOpacity`: delay 155,
fade 66, hold 2001, fade 66) and publishes it in the UI-layer signature so the cached layer cannot freeze
the plate. The trace below is the implementation's own frame trace (`XMB_FRAME_TRACE`) on a run whose tap
line reads `at=5041 ms`; the UI layer rebuilds only when the opacity changes, so the bursts are the
envelope:

| interval | rebuild frames | meaning |
|---|---|---|
| +3 ms | 1 | the tap frame, opacity 0 (no plate drawn yet) |
| +157..+222 ms | 13 | fade in, 66 ms |
| nothing | 0 | the 2001 ms hold (plate on screen, layer cached) |
| +2221..+2286 ms | 14 | fade out, 66 ms |

A grab at +2900 ms has **0 dark pixels** in the plate rect; the parity grab at +300 ms has the plate at
RGB (53,49,41) and 5,535 dark pixels in it. Because the grab lands inside the 2 s full-opacity hold, the
capture is deterministic: the plate is at opacity 1, not at a point on a fade.

### 4. Scores, before and after (same catalogue crops, `work/ab-compare.ts` = `work/parity.ts` metrics)

| state / region | before | after | budget | now |
|---|---:|---:|---|---|
| ps-menu / `ps menu entries` | 19.39 dB / 0.9396 | **30.89 dB / 0.9857** | 26.0 / 0.95 | PASS (+4.89 dB over budget) |
| ps-menu / `ps menu footer` | 18.54 / 0.8914 | **Infinity / 1.0000** | 18.0 / 0.88 | PASS (both sides black; see 2) |
| ps-menu / frame | 22.37 / 0.9583 | **28.82 / 0.9918** | 18.0 / 0.88 | PASS |
| info-indicator / `controller indicator` | 16.83 / 0.7718 | **22.37 / 0.9277** | 18.0 / 0.88 | PASS |
| info-indicator / frame | 17.97 / 0.8243 | 18.02 / 0.8257 | 18.0 / 0.88 | **FAIL** (SSIM 0.8257) |
| info-indicator / `info bar` (not gating) | 14.48 / 0.7644 | 14.48 / 0.7644 | - | FAIL |

Runs: before `out/parity/run-20261028-081050` (scorecard `scorecard-20261028-081124`), after
`out/parity/run-20261028-082434` (scorecard `scorecard-20261028-082507`). Pins are the corpus defaults
(`uiClock 2026-10-25T12:32:00`), so the clock strings differ from the references' `10/28 10:0x` plates by
construction; neither gating region contains the clock.

**What still differs in `ps menu entries`** (30.89 dB): 88.5% of the remaining squared error is in the
focused row's band. The unfocused row is within 1 px on every edge (x 832..1087 vs 831..1088, y 554..571);
the focused row's cap matches (513..537 at threshold 150) but its advances are ~1.5% short - its words start
at 831/892/935/984 against the console's 831/895/939/987, so "System" ends 4 px left. Two smaller terms: the
bloom's outer tail is ~2 px narrower (luma 16 vs 27 at 9 px above the cap), and the glyphs are FreeType
coverage against the console's PAF rasterisation. The panel, clock, rules and legend are outside this crop.

**What still differs in `controller indicator`** (22.37 dB): the plate itself matches (rect, colour, corner
radius, and the three content boxes within 1 px); the region's mean luma is 80.8 native against 66.7
console because the XMB background behind and beside the plate is this build's day-wave render, not the
console's. The plate is ~5,800 px of a 20,400 px crop; the background is the rest. The pad glyph is 31x20
against the console's 30x22 (`tex_controller`'s visible bounds have a 1.55 aspect, the console's glyph 1.36),
one pixel each way, and the `1` sits 1 px right (1240..1246 vs 1239..1245).

**info-indicator's frame gate** is still the whole-frame surface (background, band, clock), not this
indicator: its before/after is 17.97 -> 18.02 dB, and the SSIM budget (0.88) is above what the canonical
root frame reaches at all (0.8225). The plate cannot move a 2.07 Mpx frame by more than a few hundredths;
the state passes its *region* gate, which is the claim this surface owns.

### 5. Gate and fence

`work/ab-layers.ts --capture --exe C:/dev/out/native-psmenu/re-ps3-native.exe`: **PASS 6/6** layers within
budget, with the text-presence line `1677 bright pixels in the clock crop (minimum 900) PASS`; frame
18.48 / 0.8327. No named state needed re-baselining: the only states that draw the PS menu or the tap plate
are the two above, and both references are untouched.

Files: `src/view.cpp`, `src/view.h` (this pass). No main.cpp, assets, dialogs, CMake, harness, parity tool,
budget, reference or catalogue change. The scripted-tap root cause lives in `src/ps_watcher.cpp`, which is
outside this pass's fence; the view-side fix is labelled as such above.
