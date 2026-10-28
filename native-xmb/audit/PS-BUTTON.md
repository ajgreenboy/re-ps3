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
