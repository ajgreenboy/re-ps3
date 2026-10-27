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
