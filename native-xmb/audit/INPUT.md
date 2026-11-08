# Input map: keyboard, gamepads, remotes and media keys (PHASES H10)

Date: 2026-11-08. Scope: couch use without a keyboard. Code: `src/media_keys.{h,cpp}` (new), `src/view.cpp`
(`keyPressEvent`, `remoteKeyAction`, `triangleAction`, `pollController`, `pollTriggerSeek`), `src/pad.{h,cpp}`
(XInput slots 0-3, Guide, battery, timed `XMB_PAD_FAKE`).

**CEC is out of scope.** PC GPUs and HDMI ports do not expose CEC; it needs a USB CEC adapter (Pulse-Eight) and
libcec, which this build does not link. The supported remote paths are IR/MCE receivers and Bluetooth HID remotes,
which reach Windows as `WM_APPCOMMAND`, `VK_MEDIA_*`/`VK_BROWSER_*` keystrokes or Media Center keyboard shortcuts.

## 1. How a remote press reaches the XMB

| Form | Taken by | Notes |
|---|---|---|
| `WM_APPCOMMAND` | `MediaKeyFilter` (native event filter, before Qt and `DefWindowProc`) | Returns TRUE when taken. A mouse X1 button also arrives as `APPCOMMAND_BROWSER_BACKWARD` and is treated as Back. |
| `WM_KEYDOWN` `VK_MEDIA_*`, `VK_BROWSER_BACK/HOME`, `VK_PLAY` | `MediaKeyFilter` | Auto-repeat (lParam bit 30) is swallowed, so a held Next does not skip an album; the matching `WM_KEYUP` is swallowed too. |
| Qt key event (`Key_Media*`, `Key_Back`, `Key_HomePage`, `Key_Guide`, `Key_Info`, `Key_Menu`, `Key_Context1`, `Key_ChannelUp/Down`, `Key_AudioForward/Rewind`) and Media Center shortcuts | `View::keyPressEvent` (first branch after the intro) | The path for keys Qt delivers itself, for example `VK_APPS` (Menu) and Ctrl+D. |
| Volume keys (`APPCOMMAND_VOLUME_*`, `VK_VOLUME_*`, `Key_Volume*`) | `VolumeKeyFilter` in `view.cpp` (unchanged) | Not part of this table's remote actions; see section 2. |

One press is one action. A keyboard often delivers both the `VK_*` keystroke and a `WM_APPCOMMAND` for the same key.
The second form of the same action within 100 ms is dropped (the rule `VolumeKeyFilter` already used). With
`XMB_INPUT_TRACE=1` the drop is logged as `remote: trace ... duplicate of a press N ms ago (swallowed)`. A real
`SendInput` of `VK_MEDIA_PLAY_PAUSE` arrived as `WM_APPCOMMAND` first and `VK` second. Both orders are covered.

Declining: a transport key with **no media open** is not taken. The filter returns false, Qt's key-event echo of the
same message is passed on without a second decision (`remoteDeclined_`), and Windows routes the key as it would
without this app, for example to a background media player. With media open, a transport key pressed while another
surface owns input (system menu, keyboard, user page) is **taken and ignored**, because it was meant for this
player. Every decision logs one `remote: <key> from <source> -> <result>` line. Transport keys add a
`remote: after <key> mediaOpen paused position title` line 400 ms later.

## 2. Full mapping

"Keyboard" is the existing desktop binding. "XInput" and "PS pad" are the same bits (`pad.h` uses the XInput
`wButtons` layout for the HID pads): A = Cross, B = Circle, X = Square, Y = Triangle, Back = Select, Start = Start,
LB/RB = L1/R1, LT/RT = L2/R2, Guide = PS.

| XMB action | Keyboard | XInput / PS pad | Remote (`WM_APPCOMMAND`) | Remote (`VK_*` / Qt key) | MCE keyboard shortcut (INFERRED) |
|---|---|---|---|---|---|
| Move | Arrows (firmware repeat 300/67 ms) | D-pad, left stick past 0.5 | - | Arrows | Arrows |
| Confirm (Cross) | Enter | A / Cross | - | Enter (MCE OK) | Enter |
| Back (Circle) | Escape, Backspace | B / Circle | `BROWSER_BACKWARD` (also mouse X1) | `VK_BROWSER_BACK`, `Key_Back` | Backspace |
| Options (Triangle) | T | Y / Triangle | - | `Key_Info`, `Key_Menu` (`VK_APPS`), `Key_Context1` | Ctrl+D (More info) |
| Page up / down (10 rows) | PageUp / PageDown | **L1 / R1 off the player (new)** | `MEDIA_CHANNEL_UP` / `_DOWN` | `Key_ChannelUp` / `_Down` | PageUp / PageDown |
| System menu (PS hold) | F10 | PS held 1 s (HID); **Guide held 1 s (XInput, new)** | `BROWSER_HOME` (toggles: a second press closes it) | `VK_BROWSER_HOME`, `Key_HomePage`, `Key_Guide` | Ctrl+G (Guide) |
| PS tap (indicator, bring XMB forward) | - | PS / Guide tap | - | - | - |
| Play/pause | Space | - | `MEDIA_PLAY_PAUSE` | `VK_MEDIA_PLAY_PAUSE`, `Key_MediaTogglePlayPause` | Ctrl+P |
| Play (only if paused) | - | - | `MEDIA_PLAY` | `VK_PLAY`, `Key_MediaPlay`, `Key_Play` | Ctrl+Shift+P |
| Pause (only if playing) | - | - | `MEDIA_PAUSE` | `Key_MediaPause` | - |
| Stop (leave the player, resume point kept) | Escape (Circle) | B / Circle | `MEDIA_STOP` | `VK_MEDIA_STOP`, `Key_MediaStop` | Ctrl+Shift+S |
| Next / previous item | ] / [ | R1 / L1 in the player | `MEDIA_NEXTTRACK` / `_PREVIOUSTRACK` | `VK_MEDIA_NEXT_TRACK` / `_PREV_TRACK`, `Key_MediaNext` / `_Previous` | Ctrl+F / Ctrl+B |
| Seek +/-10 s | Left/Right in the player | R2 / L2 in the player: video and **now also audio (new)**, one step per press | `MEDIA_FAST_FORWARD` / `_REWIND` | `Key_AudioForward` / `Key_AudioRewind` | Ctrl+Shift+F / Ctrl+Shift+B |
| Shuffle | M | X / Square in the player | - | - | - |
| Repeat | R | (music control panel) | - | - | - |
| Display (pin player chrome) | I | Back / Select in the player | - | - | - |
| Player control panel | S | Start in the player | - | - | - |
| Music control panel (local audio) | T | Y / Triangle | - | Info / Menu | Ctrl+D |
| Keyboard (OSK): key, backspace, space, accept, cancel | Enter, Backspace, text, Ctrl+Enter, Escape | A, X, Y, Start, B | Back = cancel | Back = cancel | - |
| Photo: zoom, reset, rotate | + / -, 0, Q / E | LB / RB, X, Y | Info = rotate right | Info = rotate right | - |
| Volume up / down / mute | volume keys | - (L2/R2 seek; the console binds no volume to them) | `VOLUME_UP/DOWN/MUTE` | `VK_VOLUME_*`, `Key_Volume*` | - |
| Refresh | F5 | - | - | - | - |
| Full screen | F11 | - | - | - | - |

Number keys are not mapped: no XMB surface takes a digit, and 0 is already the photo viewer's reset.

Notes on the gamepad rows:
- **XInput slots 0-3.** Before H10 only slot 0 was read. All four are now merged: buttons OR'd, and the stick and
  each trigger come from the slot that deflects it most. An empty slot is re-probed once a second, because
  `XInputGetState` on an empty slot is slow. A connect or disconnect logs `pad: XInput slot N connected, battery X`.
- **Guide = PS.** Read through `XInputGetStateEx` (xinput1_4.dll, ordinal 100; undocumented, so if it is missing
  there is no Guide). It is merged into `pad::latest()` as bit 0x400, so `PsWatcher` handles it exactly like a
  PlayStation PS button: tap = indicator, 1 s hold = system menu. Like everything else from the pad, it is withheld
  in scripted runs. A focused game that owns the Guide button (Steam or Game Bar) may still see it first. **Not
  verified**: no XInput pad was attached.
- **L1/R1 off the player** page ten rows, the pad form of PageUp/PageDown. The console's own L1/R1 list step is not
  traced (INFERRED).
- **L2/R2** seek in any seekable non-photo media. Before H10 they only worked in video. The music container's hint
  art for L2/R2 is unverified (INFERRED from the video container).
- **Select/Start at the XMB root** do nothing. The console binds nothing there either. They act only in the player
  and the OSK.

## 3. Accessory Settings: controllers and battery

Present before H10: the Accessory rows (Reassign Controllers, Manage Bluetooth Devices, Calibrate, Vibration) show
`Attached: DualSense (USB) 100%` from `host::padText`. It covers Sony HID pads, with the battery taken from the live
`pad::info()`. New in pad.h: `pad::xinputPads()` (slot plus wired/full/medium/low/empty/unknown from
`XInputGetBatteryInformation`). Wiring it into the rows needs the `host.cpp` hunk below. The hunk below was applied to `host.cpp` with this change:

```diff
 QString padText(const Snapshot& state) {
-    if(state.pads.isEmpty())return QStringLiteral("no PlayStation controller");
     QStringList parts;
     for(const auto& pad:state.pads) {
         QString line=pad.name+QStringLiteral(" (")+pad.transport+QLatin1Char(')');
         if(pad.batteryPercent>=0)line+=" "+QString::number(pad.batteryPercent)+QLatin1Char('%');
+        if(!pad.charging.isEmpty() && pad.charging!=QLatin1String("discharging"))line+=" "+pad.charging;
         parts<<line;
     }
+    // XInput (Xbox-layout) pads, slots 0-3 (pad::xinputPads, PHASES H10): four battery levels, not a percentage.
+    for(const auto& x:pad::xinputPads())
+        parts<<QStringLiteral("XInput controller %1 (battery %2)").arg(x.slot+1).arg(x.battery);
+    if(parts.isEmpty())return QStringLiteral("no controller");
     return parts.join(QStringLiteral(", "));
 }
```

## 4. Verification (2026-11-08)

Build: `build.ps1 -OutputDirectory C:/dev/out/h10-build` printed "Built". Test tools are in `C:\dev\out\h10-test\`:
`make-wavs.ts` writes three silent 90 s WAVs and a scratch data dir, `remote-test.ts` and `pad-test.ts` drive the
tests. Input went only to the XMB window each tool started: posted messages to its HWND (found by process id), plus
`SendInput` only after checking that this window was in the foreground.

`bun remote-test.ts` used a scripted run (`--script` Music > Music Library > Alpha.wav, then a pause) with
`XMB_LOG` and `XMB_INPUT_TRACE`. Results (`remote-test.out`, `xmb.log`):

| Input | Log result |
|---|---|
| appcommand `MEDIA_PAUSE`, then again | `-> pause` paused 1; `-> already paused` |
| appcommand `MEDIA_PLAY` | `-> play` paused 0 |
| appcommand `MEDIA_PLAY_PAUSE`; posted `VK_MEDIA_PLAY_PAUSE` | paused 1; paused 0 |
| `VK_MEDIA_PLAY_PAUSE` + `WM_APPCOMMAND PLAY_PAUSE` posted together | one toggle; `appcommand 14 duplicate of a press 0 ms ago (swallowed)` |
| `MEDIA_FAST_FORWARD` x2, `MEDIA_REWIND` | 5824 -> 16272, 17104 -> 27552, 28448 -> 18896 ms |
| `MEDIA_NEXTTRACK`, posted `VK_MEDIA_NEXT_TRACK`, `MEDIA_PREVIOUSTRACK` | Bravo.wav, Charlie.wav, Bravo.wav |
| `BROWSER_HOME`; `MEDIA_PLAY_PAUSE`; `BROWSER_HOME` | `opened the system menu (PS hold); rows 2`; `ignored (another surface owns input)`; `closed the system menu` |
| real `SendInput VK_MEDIA_PLAY_PAUSE` x2 (XMB in front) | one toggle each; the `VK` form was swallowed as the duplicate of the `WM_APPCOMMAND` |
| `MEDIA_STOP` | `stop; mediaOpen now 0` |
| `MEDIA_CHANNEL_DOWN` / `_UP` in the folder | `selection 1 -> 2`, `selection 2 -> 0` |
| `SendInput` Ctrl+D; `VK_BROWSER_BACK` | `info from qt key 0x44 -> Triangle: options opened`; `back ... Circle` |
| `SendInput VK_APPS`; posted `VK_BROWSER_BACK`; appcommand `BROWSER_BACKWARD` | options opened; closed; depth 1 -> 0 |

`bun pad-test.ts r1` ran without `--script`, with `XMB_PAD_FAKE=r1,from=9000,for=300` at the XMB root:
`action=r1 accepted=1`, and a later `MEDIA_CHANNEL_UP` logged `selection 10 -> 0`, so R1 paged ten rows.
`bun pad-test.ts r2` used `XMB_PAD_FAKE=r2=1,from=16000,for=400` while Alpha.wav played: `trigger seek R2 -> 17872 ms`,
an audio seek. No warning lines appeared in any of these logs.

Not verified here: a physical MCE/IR receiver, a Flirc receiver, a Bluetooth remote and an XInput pad (no Guide,
slot merge or battery read on hardware). The attached DualSense was read (`pad: DualSense over USB, battery 100%
(full)`), but its buttons were not pressed. The Media Center shortcut list is INFERRED, not tested against a remote.
The declined path (no media open) was not exercised live, because a declined key goes on to Windows and could reach
another application.

Gate: `bun work/ab-layers.ts --all --exe C:/dev/out/h10-build/re-ps3-native.exe` -> states 12/12 pass, layers 6/6, whole set PASS (exit 0).
