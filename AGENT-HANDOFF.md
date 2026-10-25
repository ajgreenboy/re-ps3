# Agent handoff: PS3 XMB on Windows

**Current routing (2026-10-25):** start at `native-xmb/HANDOFF-PHASES.md`, then
`native-xmb/audit/PIPELINE.md`. The October 24 component inventory below is historical.
`native-xmb/audit/MAXMB-INTEGRATION.md` records the latest handoff review and MaXMB reuse.

Written October 24, 2026 for an agent picking this repository up cold. It orients you and points at the
detailed documents; it does not replace them. When a linked document is newer than this file, the
linked document wins.

## 1. Goal and current direction

The goal is the PS3 XMB (firmware 4.93) running natively on Windows as an HTPC launcher, with the
original look and feel, built from the user's own local firmware assets.

There are two tracks. **Only the second one is active.**

| Track | Location | Status |
|---|---|---|
| Firmware execution: static recompilation of vsh + 46 modules, hand-written LV2 kernel, SPU interpreter, OpenGL RSX | `xmbrt/`, `xenon/`, `recomp/`, root `HANDOFF.md`, `docs/` | **Reference only.** Boots to a navigable XMB, but runs at about 1–10 fps with rendering gaps. Kept for comparison and research. |
| Native C++ recreation (Qt + OpenGL) using original assets read from the firmware | `native-xmb/` | **Active.** Version 0.2: original icons, fonts, strings and sounds; settings, profiles, media and photo services; procedural wave. Far from complete. |

The user switched to the native track on October 23 and has said that simpler native reimplementations
are fine: behaviour and appearance should match the PS3, the internals do not have to.

## 2. Start here, in this order

1. `native-xmb/HANDOFF-PHASES.md`: current layer status, open phases and A/B baseline. Then read
   `native-xmb/audit/HANDOFF.md` for the dated Oct 24 functional inventory.
   It covers what v0.2 contains, evidence, the input map, priorities and phases A to E.
2. `native-xmb/ISA.md`: the completion checklist. An unchecked box stays open until the behaviour has
   actually been observed.
3. `native-xmb/README.md`: build, run and architecture boundaries.
4. `native-xmb/audit/README.md`, `FUNCTIONS.md`, `FEATURE-CATALOG.md`, `VISUAL-BEHAVIOR.md`: the
   per-feature coverage map.
   `native-xmb/audit/VIDEO-REFERENCES.md` holds values measured from public 60 fps XMB footage: the
   day/night background colour curve, ribbon swell speed and height, layout fractions and navigation ease-out
   timing.
5. Only if you need the firmware track: root `HANDOFF.md`, newest section 3a first, then `docs/README.md`.

## 3. Build and run

From `C:\dev\xmb` in PowerShell:

```powershell
& .\native-xmb\build.ps1
& .\run-native-xmb.ps1
```

- **Toolchain:** VS 2022 Build Tools (MSVC 19.44), bundled CMake/Ninja, Qt 6.12.0 MSVC 64-bit, C++20,
  OpenGL 3.3.
- **Output:** `out/native-xmb/re-ps3-native.exe`; session logs in `out/native-xmb/sessions/`.
- **Assets:** read read-only from the sibling RPCS3 install `C:\dev\rpcs3\bin\dev_flash` by default
  (`-FlashRoot` overrides it).
- **Before relinking:** close the running native executable.

Firmware track only: `& .\run-xmb.ps1` runs the recompiled firmware, `& .\xmbrt\build-xmbrt.ps1`
rebuilds the runtime, and `& .\work\rebuild-components.ps1` runs the full translation pipeline.

## 4. Rules that are easy to break

- **Firmware stays private.** Firmware, decrypted modules, extracted assets, recordings, console
  identity (`console-psid.bin`) and the emulated HDD overlay (`out/xmbrt/vfs/`) never go into Git or
  anywhere public. Only source, docs and approved UI screenshots are committed. The remote
  `ajgreenboy/re-ps3` is private; still ask before pushing.
- **Use only the firmware already on this machine.** Don't download any other.
- **Other agents work here too.** Run `git status` before editing and commit only your own files. As of
  this writing, the native track has uncommitted work in `native-xmb/` and `work/`; leave it alone
  unless you are continuing it.
- **Report only what you observed.** A successful build, a surviving window or a logged FPS is not a
  feature or fidelity claim. Don't invent test results, and don't add formal test suites unless the user
  asks.
- **Tooling:** use Bun/TypeScript for scripts (`~/.bun/bin/bun.exe`; never npm, no Python without
  approval) and C++ for application code.
- **Computer Use can't drive RPCS3**, and the firmware-track window `xmbrt.exe` isn't an installed app,
  so it can't be driven either. Use logs, or screenshots and recordings the user supplies. Don't work
  around the RPCS3 block.
- **Online features must never fake anything:** no sign-in, purchases, trophies or messages.

## 5. State of the native track (from its Oct 24 handoff; re-verify before relying on it)

Working, with observed evidence:

- Original font, icons (RCO/GIM, QRC/DDS normal maps, corrected orientation) and English strings.
- Category navigation, a submenu page stack that restores selection, the Theme → Color → Back flow, and
  original interaction sounds decoded from VAG.
- A render loop logging about 62 fps on the RTX 3070. This is a render-loop interval, not measured
  display pacing.

Compiled but not yet observed end to end:

- Versioned preferences with atomic writes, local profiles, a flat media scan, the photo viewer and
  slideshow, and media player controls.

Highest-priority gaps (detail in the audit handoff):

1. Triangle/context panels, player panels, the PS-button overlay and notification queues.
2. Original settings page schemas bound to real capabilities.
3. Independent entry/exit/scroll/focus timelines fitted to the user's recording (entry roughly 270–330 ms,
   Back roughly 70–130 ms in the samples).
4. Original normal-map lighting and the wave/material: the current wave is procedural, not a copy.
5. A media database, queues and playlists.
6. Keyboard and controller repeat timing.

## 6. Firmware track: what is known (only needed if you reopen it)

- **Status:** reaches the XMB menu. Normal boots run at about 1–10 fps. An intermittent SPURS task error
  `80410905` (`queuePush.c:127`, AC3 group) is still unexplained.
- **Frame pacing, as diagnosed on Oct 23 with the tools below:**
  - The FIFO thread is about 90% idle, so the GPU side is not the bottleneck.
  - The main thread spends about 95 ms per frame in custom_render's `cellSpursEventFlagWait`
    (lr `0195B458`) and about 50 ms in the VSH event dispatcher (lr `0043376C`).
  - The SPURS "Lines" SPU finishes its work and exits its group (stop 0x101) each round. The time goes to
    the restart/handshake path and to slow SPU interpretation (about 7 M instructions/s).
  - A natural next step is statically recompiling SPU programs or replacing that workload natively. The
    MIT-licensed [ps3recomp](https://github.com/sp00nznet/ps3recomp) has an SPU lifter and clean-room
    cellSpurs HLE worth studying.
- **Fixed: crash after the DB rebuild.**
  - Cause: VSH creates `/dev_hdd0/tmp/turnoff` at boot and deletes it only on a guest power-off. Killed
    or closed runs left it behind, so every later boot took the "not turned off properly" route: UI
    teardown, recovery offer, reboot op 8201, database rebuild.
  - Fix: `xmbrt/k_fs.cpp` clears the stale marker at startup (`XMBRT_KEEP_TURNOFF=1` keeps it). Three
    consecutive killed boots then showed no UI teardown and no reboot request.
  - Limit: this avoids the recovery route; it does not make the firmware's own database recovery work.
  - Backup of the removed marker: `work/vfs-backup-20261023/turnoff`.
- **Diagnostics added on Oct 23** (all committed):

  | Variable | Effect |
  |---|---|
  | `XMBRT_SYSCALL_TIMING=1` | Wall time per syscall call site on the main thread; `=all` covers every thread. |
  | `XMBRT_SPU_SAMPLE=<hex id>` | Samples one SPU's PC between 20 s and 40 s and saves its local store. |
  | `XMBRT_LOG_HOST_TIDS=1` | Maps host thread IDs to SPU threads. |
  | `XMBRT_GL_RELEASE=finish` | Restores the old blocking GL release path. |
  | `XMBRT_TIMER_PERIOD=default` | Keeps the 15.6 ms Windows timer, for A/B comparisons. |

  - `rsx fifo:` log lines split FIFO time into idle, semaphore wait and command work.
  - `work/ab-run.ps1` runs timed, input-free boots that follow guest reboots.
  - `work/cpu-profile.ps1` gives per-thread CPU, including threads that already exited.
- **Pitfall:** any change to image bytes needs a full retranslation, because XenonRecomp bakes addresses
  into the generated C++. Unattended boots vary from run to run (startup notices, reboot paths), so
  compare like with like.

## 7. When you finish a phase

1. Update the active track's `HANDOFF.md`/`ISA.md` with what was observed and with what remains open.
2. Commit only your own files, with author `-c user.name=Al -c user.email=ajgreenboy@gmail.com`.
3. Keep this file's section 2 pointing at the newest handoff.
