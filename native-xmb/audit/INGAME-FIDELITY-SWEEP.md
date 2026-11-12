# In-game XMB fidelity sweep

**2026-11-10 follow-up:** the opt-in DirectComposition implementation in section 7 supersedes
the earlier live-capture/out-of-scope statements below. The default is still a frozen capture.

2026-11-10; native base `e3697dc36c7905b0ded13de8e21b05635f7d7cde`.
RPCS3 source checkout `C:/dev/rpcs3`, revision
`0b0fa082ae552ab77a70de4fc1f70110d165116b`. RPCS3 paths below are relative
to that checkout's `rpcs3/`. **Source trace, video observation and inference
are deliberately separate. This is not an end-to-end firmware/game capture.**

## 1. What is actually available

- `C:/dev/rpcs3/bin/rpcs3.exe`, Qt dependencies and `dev_flash` exist.
- Recursive `EBOOT.BIN` search under `bin` returned no files. `bin/games`
  contains only the automatic-detection notice and shortcuts directory;
  `dev_bdvd` is empty; `dev_hdd0/game/TEST12345/USRDIR` is empty.
  Thus there is no runnable local game here, demanding or otherwise.
- `bin/config/input_configs/global/Default.yml:1-3` selects DualSense Pad #1.
  This file does not specify a PS mapping. No physical PS-button exercise was
  performed. The older audit's Keyboard/Backspace profile is no longer current.
- Important correction to the proposed measurement method: running a game
  directly in RPCS3 and pressing PS opens **RPCS3's replacement Home Menu**,
  not Sony's `xmb_ingame`. `Input/pad_thread.cpp:660-695` checks `!is_vsh`;
  `:923-944` creates `rsx::overlays::home_menu_dialog`. Its 85% backdrop alpha
  and 150 ms fade (`Emu/RSX/Overlays/HomeMenu/overlay_home_menu.cpp:24-39`)
  must **not** be used as firmware constants. A bounded search for
  `xmb_ingame` in RPCS3's `rpcs3/` source found no literal references.
  Booting firmware VSH is different from demonstrating a game running under
  firmware VSH; neither the latter nor a firmware-to-game-to-VSH return was
  observed here.

## 2. Exit protocol versus rendered exit

### RPCS3 source trace (not a firmware animation trace)

1. Opening the Home Menu queues `DRAWING_BEGIN` (0x0121),
   `SYSTEM_MENU_OPEN` (0x0131), then `BGMPLAYBACK_PLAY`, and marks the menu
   active: `Emu/Cell/Modules/cellSysutil.cpp:324-342`. The enum values are
   `cellSysutil.h:138-142`. These are queued callbacks, **not rendering
   commands**; dispatch is `cellSysutil.cpp:295-307`.
2. RPCS3's Exit Game menu action schedules `GracefulShutdown(true, true)`
   and returns `page_navigation::stay`:
   `Emu/RSX/Overlays/HomeMenu/overlay_home_menu_main_menu.cpp:134-143`.
   It does not take the Resume/screenshot fade-out branch. That branch alone
   sets the closing fade and calls `close` on completion
   (`overlay_home_menu.cpp:106-130`). Do not claim that the 150 ms fade is
   Sony's Quit Game transition, or even that RPCS3 takes it for Exit Game.
3. `Emu/System.cpp:3327-3349` resumes paused emulation, logs the request and
   queues `REQUEST_EXITGAME` (0x0101). Frozen/savestate shutdown or no
   registered callback goes straight to `Kill`. The sender also aborts the
   LV2 watchdog (`cellSysutil.cpp:290-292`). There is **no explicit
   MENU_CLOSE/DRAWING_END-before-EXIT ordering** in `GracefulShutdown`.
4. The asynchronous reaction wait polls at 16 ms, initially allowing 1000 ms;
   a changed sysutil read counter grants another 5000 ms
   (`System.cpp:3364-3400`). This counter is evidence that callbacks were
   consumed, not proof that a particular title completed a save. Exiting PPU
   threads can extend the late timeout by 250 ms each (`:3413-3430`).
   Failure to finish ultimately schedules `Kill` (`:3439-3458`).
5. A cooperative title can call `_sys_process_exit`, which schedules
   `Emu.Kill()` (`Emu/Cell/lv2/sys_process.cpp:368-389`). Shutdown eventually
   resets state, marks `stopped` and invokes `on_stop`
   (`System.cpp:4266-4293`). In Big Picture mode a separate callback boots
   Big Picture (`:4282-4289`); that is not Sony's XMB.
6. Ordinary menu dismissal invokes the pad menu's close callback
   (`Input/pad_thread.cpp:938-944`), which sends `BGMPLAYBACK_STOP`,
   `SYSTEM_MENU_CLOSE`, `DRAWING_END` and clears active
   (`cellSysutil.cpp:345-359`). This is the **return-to-game protocol**;
   it does not establish the exact ordering during termination teardown.
7. The native app's `WM_CLOSE` reaches a different entry point: RPCS3's
   `rpcs3qt/gs_frame.cpp:1206-1252` optionally asks its own Exit Game question,
   leaves fullscreen for that question, then issues asynchronous graceful
   shutdown and hides as configured; RSX closes the window later. A native
   Yes is not permission to silently change RPCS3's confirmation preference.

**Conclusion:** these sources establish the cooperative exit/state protocol,
not what Sony VSH draws between confirmation and home XMB. There is no honest
exact firmware render sequence derivable from these functions alone.

### Actual console footage

Source: Sony/SCEA's **PLAYSTATION 3 Firmware 2.40 demo #1 - XMB**, Eric Lempel,
2008-06-30, <https://www.youtube.com/watch?v=QnaxkncuPj8>.
The accessible 640x360 video-only format 134 is 11.63 MiB, 30000/1001 fps.
It is a camera filming a TV, not a lossless HDMI capture; firmware is 2.40,
not the local 4.93 resources. WebFetch supplied only the page shell; the
in-app browser was disconnected. A bounded yt-dlp download provided frames.
Local video SHA-256:
`9ae8bb683619bddd6196492e40c3f867e2a22bc1a3c694ecb06d653ca406e856`.

The quit-first confirmation is visible at approximately **84-90 s**; a
closing/wait screen at **91-94 s**; the screen is **black at 95-101 s**;
the home wave returns at **102 s**. This is an operation that requires
quitting, rather than an isolated test of the Quit Game row. It establishes
that a real close can have a blank interval; it does **not** establish a
universal delay, exact fade curve or frame at which Yes was pressed.

Native comparison: `src/shell.cpp:2562-2601` confirms, posts cooperative
`WM_CLOSE`, waits up to 10 s, then `terminate`, then `kill` 3 s later (Steam
uses `+app_stop`). While waiting it retains the in-game scene. The finished
handler (`:575-598`) clears in-game state and silently returns to the home
scene; `src/view.cpp:4398-4407` drops topmost/capture state. There is no
dedicated blank/wait transition. **This differs from the observed sequence.**
No arbitrary six-second black timer or fabricated fade has been added:
a useful future fix needs an explicit closing state, blocked navigation,
failure/timeout recovery, and short/slow cooperative-title tests. A blind
blackout would hide RPCS3's second confirmation or a game's unsaved-data
question. Silent success remains preferable to the removed invented
"game was closed" post-quit dialog.

## 3. Dim estimate and implemented change

All local evidence is under
`%TEMP%/opencode/` (not committed):

- `ps3-xmb-demo.mp4`: source video.
- `ps3-demo-035.png` through `ps3-demo-040.png`: before/after samples.
- `ps3-demo-094.png`, `ps3-demo-095.png`, `ps3-demo-101.png`,
  `ps3-demo-102.png`: exit sequence samples.
- `ps3-demo-open.png`, `ps3-demo-detail.png`: timestamped contact sheets.
- `measure-ingame-dim.py`, `ps3-demo-dim.json`: reproducible measurements.
  Python with cv2/numpy is staged only in temporary `fidelity-python/`.

The GT5 Prologue scene is paused before PS at 36 s; the same unobstructed
right-hand sky and road remain visible under XMB at 37-40 s. In the 640x360
video, use rectangles `(x,y,w,h)` sky `(345,80,25,20)`, road
`(330,150,35,25)` and wall `(45,100,30,100)` outside the TV. Compute the
median of `Y' = .2126 R' + .7152 G' + .0722 B'` (encoded RGB / 255).
The camera increases exposure after PS: divide each after/before game-region
ratio by the after/before wall ratio. No sRGB linearization is asserted.

| Time | Wall gain vs 36 s | Retained sky | Retained road |
|---|---:|---:|---:|
| 37 s | 1.0000 | 0.1805 | 0.2114 |
| 38 s | 1.4267 | 0.1957 | 0.1923 |
| 39 s | 1.6334 | 0.1979 | 0.1969 |
| 40 s | 1.6409 | 0.2015 | 0.2000 |

At 36 s sky/road/wall medians are 0.9147/0.4269/0.3137. The adjacent
35 s frame agrees closely, and the bezel/wall at 37 s agree with 36 s,
making the immediate pair especially useful. Rounded result: **retain
approximately 20%, darken approximately 80%**. The previous 50% placeholder
is much brighter than this footage.

`inGameDim()` now defaults to **0.8**, retaining clamped `XMB_INGAME_DIM`
overrides. This is a **display-referred, low-confidence firmware-alpha
estimate**, not a measurement of a firmware blend register. Unknown TV gamma,
camera tone mapping/black offset, compression, lighting and firmware version
prevent a calibrated physical luminance or shader-alpha claim. The numeric
patch-to-patch spread is not a confidence interval. A paired HDMI capture of
current firmware should supersede it. Do not present it as "PS3 uses alpha
0.8" or as an RPCS3 capture.

## 4. Performance investigation

No demanding title is available, so the reported sustained slowdown remains
**unreproduced**, not disproved by the earlier 143 fps static-stub result.

| Path | Code evidence | Finding |
|---|---|---|
| Entry capture | `src/view.cpp:4331-4337`; `src/game_capture.cpp:315-322` | Synchronous `captureGameWindow(hwnd,1500)` spawns an MTA worker then **joins the GUI thread**. This can stall PS entry; it cannot explain sustained default-mode slowness after entry. |
| Timeout scope | `src/game_capture.cpp:125-153,155-187,198-201` | 1500 ms bounds only the condition-variable frame wait, **not the total operation**: device creation, GPU readback `Map`, teardown and thread join can take additional time. Prior "1.5 s bound" wording is too strong. |
| Default backdrop | `src/view.cpp:4462-4487` | Image flip/GL upload occurs only on changed `QImage::cacheKey`; otherwise one textured triangle, uniform updates and binds. No per-frame window capture or CPU dimming. |
| Live capture | `src/view.cpp:4346-4347,4409-4454` | Opt-in `XMB_INGAME_LIVE=1`; serialized asynchronous capture, 250 ms timer restarted after completion, generation/HWND/PID guards. Each sample still creates a fresh D3D device/session/staging texture, reads back and later uploads to GL. This is expensive infrastructure churn, not a persistent live-overlay pipeline. No overlapping capture storm was found. |
| Composition | `src/view.cpp:4293-4295,4351-4353` | Topmost set on entry, not every frame. Possible independent-flip/MPO/composition changes and shared-GPU contention require game-side frame pacing/PresentMon evidence; no quantitative DWM cost established here. |
| Remaining frame work | `src/view.cpp:1037-1071,1073-1094` | Home wave skipped in-game; normal UI animation/signature/cache machinery still runs. Motion rebuilds the supersampled UI. A static stub is not representative of navigation over a GPU-bound game. |

No speculative threading rewrite or performance claim is attached to the dim
change. Next measurement: a real borderless title's frame times before/after
PS, entry latency separately, native swap/paint traces, and live-capture off/on.
Persistent WGC GPU sharing and **DirectComposition live-overlay are out of
scope**. The default remains an opaque window with a frozen captured frame.

## 5. Fidelity gap table

"Matches" is limited to the cited property, not blanket console parity.

| Item | Evidence | Verdict / action |
|---|---|---|
| Dim factor | Sony demo 36-40 s; ROI table above; `view.cpp` `inGameDim` | **Differs -> estimated correction**: 50% to 80% darkening. Exact 4.93 alpha **unmeasured**. |
| Quit transition | Sony demo 84-102 s; RPCS3 `System.cpp:3327-3458`; native `shell.cpp:575-598,2562-2601` | **Differs**: native holds captured scene then immediately resumes home. Explicit closing/wait/blank state deferred; exact firmware timing unknown. |
| Cooperative exit | RPCS3 `cellSysutil.h:138`, `System.cpp:3341`, `sys_process.cpp:368-389`; native `shell.cpp:2582-2600` | **Matches intent**, not transport: title is asked to exit before escalation. Native WM_CLOSE is not a PS3 sysutil callback. RPCS3's extra confirm can obstruct this. |
| Backdrop | `INGAME-XMB-DESIGN.md` F3/F15; Sony demo 36-43 s; native `view.cpp:4340,4461-4488` | **Matches** game rather than home wave; **differs** frozen captured window versus firmware game surface. True live composition deferred. |
| Help line | Firmware `xmb_ingame:msg_help_back_to_game`, design F11; `shell.cpp:2560`; demo 41-43 s | **Matches** resource-backed PS Back to Game Screen wording; exact placement/scale **unmeasured** in this low-resolution footage. Earlier Circle attribution must not be mistaken for the PS gaiji actually in the resource. Native Circle return is an additional policy. |
| Quit Game rows | Firmware XMBML selectors, design F6-F8; native policy note at design lines 286-290 | **Differs intentionally**: owner requested running-content column only, not all seven firmware columns. Not reverted. |
| Confirmation | Firmware `impose_plugin:msg_game_quit_ask`, design F12; native `shell.cpp:2562-2567` | **Matches** Yes/No-before-close wording/intent; exact timing and default focus not console-measured here. No invented post-quit success page. |
| Notifications | Firmware `system_plugin` textures, design F13; native `notify_overlay.*` | Asset/overlay implementation exists; real-game visibility, z-order and exclusive-mode behaviour **unmeasured**. |
| Open/close animation | Sony demo 36-40 s; replacement RPCS3 overlay `overlay_home_menu.cpp:39,106-130,158-167` | **Unmeasured** firmware fade curve. RPCS3's 150 ms replacement-menu animation is not evidence for Sony timing. Native has no measured backdrop fade. |

## 6. Verification

Only `src/view.cpp` dim default/comments and misleading timeout comments in
`src/game_capture.cpp/.h` changed in code. No firmware, games,
source footage, executables or captured console frames are committed.
The `ingame-xmb` self-reference was re-saved locally after this deliberate
in-game rendering change; it is an ignored output, not a firmware reference.
No other reference was re-saved.

- Required build: **Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe**.
  Existing deprecation/shadow warnings remain; no compile/link failure.
- `bun work/ab-layers.ts --state ingame-xmb --save-reference --exe
  C:\dev\xmb\out\native-xmb\re-ps3-native.exe`: reference updated **1/1**;
  sidecar `depth=0 category=game rows=14`, with running-title/in-game assertions.
- Final `bun work/ab-layers.ts --all --exe
  C:\dev\xmb\out\native-xmb\re-ps3-native.exe`: **whole set PASS**, exit 0:
  stored layers **6/6**; canonical capture **5 pass + 1 informative**
  (content-different item-label region); named states **22/22**. In-game
  frame/game-region/guide are pixel-identical to the new self-reference
  (PSNR infinity, SSIM 1.0000). Full output: temporary
  `ingame-fidelity-gate.log`. The first all-state invocation hit the tool's
  120 s timeout; the completed rerun used a 600 s limit. The subsequent
  successful rebuild included the comment-only timeout-contract clarification.

## 7. Opt-in live DirectComposition overlay (2026-11-10)

**Landed:** `src/live_overlay.*`, `View::enterInGame/drawInGameBackdrop/paintGL/stopLiveOverlay`.
`XMB_INGAME_LIVE=1` now selects true DWM composition, replacing the old 250 ms WGC polling.
Default/unset remains the captured-frame path. One initial WGC capture is retained for fallback.

**SDK recipe (TRACED):** paths below are relative to
`C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/`:

- Top-level `WS_POPUP`, `WS_EX_NOREDIRECTIONBITMAP` (0x00200000), `TOPMOST`, `NOACTIVATE`,
  `TOOLWINDOW`: `um/winuser.h:2886,2845,2897,2850`. No layered-window Qt alpha blit is used
  for presentation. The native window targets the recorded game's physical monitor rectangle.
- Hardware D3D11 device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT` (0x20), following
  `game_capture.cpp:125`: `um/d3d11.h:15007`. `IDXGIFactory2::CreateSwapChainForComposition`,
  BGRA8, sample count 1, two buffers, `DXGI_SCALING_STRETCH`,
  `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` (3), `DXGI_ALPHA_MODE_PREMULTIPLIED` (1):
  `shared/dxgi.h:271`, `shared/dxgi1_2.h:545,1761`.
- `DCompositionCreateDevice` -> `CreateTargetForHwnd` -> `CreateVisual` ->
  visual `SetContent(swapChain)` -> target `SetRoot(visual)` -> `Commit`:
  `um/dcomp.h:67,266,273,486,403,253`. Upload with `UpdateSubresource`, then `Present(0,0)`.

**Integration:** the existing GL renderer draws **both the columns and the dim** into its
premultiplied framebuffer. The background clears to `(0,0,0,inGameDim())`; DWM supplies the
live game underneath. Resolve the widget's 4x MSAA into a single-sample RGBA8 FBO before
BGRA readback (direct MSAA `glReadPixels` fails with `GL_INVALID_OPERATION`, 0x502).
For the live surface only, `IconRenderer` uses separate RGB/alpha blend factors, with
source-over coverage rather than squared source alpha punching holes in the dim. The
legacy path is retained otherwise: the glass shader samples target alpha as well as RGB.
The CPU bridge flips/uploads that image to the persistent composition swap chain. Qt's main
window is opacity zero but retains activation, keyboard/controller input and the existing
navigation. Its 16 ms input timer also requests live paints: an invisible Qt window can stop
emitting `frameSwapped`. No column-compositing step is deferred; no game pixels are polled.

**Fallback/lifetime:** exclusive, minimized and invalid classifications decline live mode.
Creation, allocation, GL readback, DXGI resize/present or device-removal failures log a reason,
destroy the overlay, restore Qt opacity, and retain the captured/plain-dark backdrop. Circle/PS
return, title self-exit and View destruction also destroy the window and restore opacity.
The exclusive classification remains a heuristic, not a guarantee for every Vulkan/OpenGL FSE title.

**Measured, not a demanding-game benchmark:** 1920x1080, same borderless D3D11 stub and
monitor, static columns, `XMB_PS_LOG=1 XMB_OVERLAY_COST=1 XMB_FRAME_TRACE=<file>`.
CPU paint samples exclude the first/last 120 in-game frames; live intervals use 12-24 s;
stub times use the per-second frame averages at game time 11-16 s. CPU times are dispatch,
not GPU/present completion. The scratch stub only adds QPC frame timing to the existing fixture.

| Metric | Flag off (frozen capture) | Flag on (live overlay) |
|---|---:|---:|
| CPU paint median / p95 | 0.185 / 0.337 ms | 6.329 / 8.098 ms |
| CPU bridge median | 0 ms | 5.862 ms |
| Paint interval median / p95 | unpaced; not a presentation measurement | 16.000 / 16.802 ms |
| Stub frame average | 4.167 ms | 4.167 ms |

Host caveat: the opaque Qt control's callbacks became unpaced (sub-ms intervals), and its
desktop GDI captures did not reliably include that window. Its scripted framebuffer grabs
were correct/frozen. Do not treat its desktop pair or callback cadence as physical presentation
evidence; the cause was not diagnosed here. The live desktop pair below is visually verified.

This is a substantial XMB CPU regression despite the unchanged lightweight stub rate.
**Keep the flag off by default.** A zero-copy GL/D3D shared-texture bridge with explicit GPU
ownership/fences is the next optimization; the current MSAA resolve/readback/upload is not
appropriate evidence for performance over a GPU-bound real game. Real-title input arbitration,
true non-FSO FSE, HDR/mixed-DPI/multi-adapter pacing and monitor hotplug remain owner validation.

**Verification/evidence (local, not committed):**
`%TEMP%/opencode/xmb-dcomp-{on,off,fse,end}/` contains isolated
data, PS logs, frame traces, stub logs, scripted `layer1.png/layer2.png` plus sidecars, and
desktop composites `desktop1.png/desktop2.png`. Scripted grabs capture the alpha XMB layer,
**not** DWM's final game composite. The live desktop pair, 1.580 s apart, is the live oracle:
29,260 changed pixels in the unobstructed game bar ROI (x=1000..1919, y=650..839).
The same ROI stays frozen (**zero** changes) in the flag-off scripted framebuffer pair.
White game pixels are `(51,51,51)` under the live dim; columns/help remain readable.
The scripted layer's alpha range is 204..255: the dim stays at 0.8, with opaque UI coverage.
The stub logs foreground=0 while live, foreground=1 after Circle. The FSE stub reports qUNS=3,
`mode=exclusive`, and `live overlay declined ... captured-frame fallback`. An 11 s self-exiting
stub logs exit 0, overlay stopped and topmost cleared; no transparent owner is stranded.
An MSAA readback failure during implementation also exercised the captured-frame fallback.

Reproduce locally with the scratch `xmb-live-test.ts` (`on`, `off`, `fse`, `end`) and
`xmb-dcomp-analyze.ts` in that same temporary directory. No fixtures, binaries, firmware,
desktop screenshots or baseline images are committed.

Final verification after restricting the alpha correction to the live surface:
**Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe**. Required
`bun work/ab-layers.ts --all --exe C:\dev\xmb\out\native-xmb\re-ps3-native.exe`:
**whole set PASS**, exit 0; stored layers 6/6, canonical capture 5 pass + 1 informative,
named states 22/22. The in-game game-frame/guide regions remain pixel-identical to the
existing self-reference (SSIM 1.0000). **No references were refreshed.** Full output:
`%TEMP%/opencode/xmb-dcomp-gate-final.log`.
