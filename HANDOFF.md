# Handoff: XMB native port

**Goal:** run the PS3 XMB (firmware 4.93) natively on Windows as a statically recompiled program, usable as an
HTPC launcher. The XMB's look and feel must be exact; everything around it (media, network, PSN, browser,
Blu-ray, SPU work) may be swapped for native code or stubbed. RPCS3 main is the reference for every PS3
semantic. Live view: `powershell -ExecutionPolicy Bypass -File C:\dev\xmb\status.ps1`.

---

## 1. Current state (2026-10-23)

| Stage | State | Evidence |
|---|---|---|
| RPCS3 Release build | Done | `C:\dev\rpcs3\bin\rpcs3.exe`, commit `0b0fa082` |
| Firmware 4.93 in RPCS3 | Done | `C:\dev\rpcs3\bin\dev_flash` (1,075 files) |
| XMB boots in RPCS3 | Done | Cold start reaches the XMB at 60 FPS with Shader Mode "Async Recompiler (multi-threaded)" (the interpreter modes stall in precompile); DualSense bound as Player 1 in `bin\config\input_configs\global\Default.yml` |
| Firmware decrypted | Done | `C:\dev\xmb\fw493` (vsh.elf + 358 modules) |
| Ghidra analysis | Done | vsh.elf 27,852 functions; 13 boot plugins ~29k |
| Image linking (xmbld) | Done | `C:\dev\xmb\out\boot493\image.bin`, `image.elf`, CSV manifests |
| Translation (XenonRecomp) | Done | 56,982 functions, 0 unrecognized instructions |
| Compile generated C++ | Done | 224/224 objects, 63 MB, `C:\dev\xmb\out\recomp_obj` |
| Record-form CR0 gaps | Done | CR0 set for `clrldi.`/`rldicl.`/`rldicr.`/`rotldi.`/`sld.`/`srd.`; 0 "RC bit enabled" lines |
| Phase 3 milestone | Done | 60 s run: 62,800 calls, 46 guest threads, RSX context + first command buffer submit, 1920x1080 display buffers; main thread matches RPCS3 for 961 calls (then a timing race); log `out\xmbrt\xmbrt_phase3.log` |
| Native runtime | Phase 4 in progress | `C:\dev\xmb\xmbrt`: LV2 kernel (threads, sync, events, memory, fs overlay, prx, sys_config, AV vuart) plus the GL renderer `rsx_gl.cpp`; all 39 modules load and frames present, no draws yet; see "Phase 4 as built" |
| Wider image | Done | 39 modules (13 boot plugins + 25 vsh/sys modules vsh loads at boot), 71,328 functions, 857 PS3 jump tables, 280 objects |
| Link + first boot | Done | `out\xmbrt\xmbrt.exe` enters vsh at 0x10230; first call `sys_mutex_create` from vsh's TLS init |
| Reference trace (phase 2) | Done | `out\trace\rpcs3_trace.txt` (200,000 calls with args and returns), normalised `out\trace\rpcs3.norm`; RPCS3 patch `rpcs3-xmb-trace.patch` |
| Module inventory | Done | `out\trace\modules_loaded.txt`: 46 modules load before the XMB is up, most of them outside the 13-plugin set |

## 2. Folder map

| Path | Contents |
|---|---|
| `C:\dev\xmb` | Project repo (local git, no remote): tooling, scripts, REPORT.md, this file |
| `C:\dev\xmb\recomp` | `xmbld.c`, `elf_be.c/.h`, `build.bat`, `runtime\ppu.h` (unused by the XenonRecomp path) |
| `C:\dev\xmb\ghidra_scripts` | Ps3ResolveImports, Ps3Exports, Ps3Functions, Ps3Census, DumpStats |
| `C:\dev\xmb\xenon` | `xmb_boot.toml`, `compile-recomp.ps1`, `xenonrecomp-pdata-only.patch` (14 lines), `xenonrecomp-xmb.patch` (full working tree, 450 lines), `unrecognized_boot493.csv` |
| `C:\dev\xmb\fw493` | Decrypted modules (`vsh_module\`, `sys_external\`). Not in git |
| `C:\dev\xmb\boot` | The 13 boot-path `.prx` files. Not in git |
| `C:\dev\xmb\rea` | Ghidra outputs: `functions\*_functions.csv`, `plugins\`, `vsh_imports/exports.csv`, `link_boot_set.csv`. Not in git |
| `C:\dev\xmb\ghidra_work` | Ghidra projects `vsh493w`, `boot493/boot`. Not in git |
| `C:\dev\xmb\out\boot493` | `image.bin`, `image.elf`, `modules.csv`, `imports.csv`, `exports.csv`, `hle.csv` |
| `C:\dev\xmb\out\recomp_boot493` | XenonRecomp output: `ppc_recomp.*.cpp`, `ppc_func_mapping.cpp`, `ppc_recomp_shared.h`, `ppc_config.h`, `ppc_context.h` |
| `C:\dev\xmb\out\recomp_obj` | Compiled objects + `build.ninja` |
| `C:\dev\rpcs3` | RPCS3 source and build (reference implementation for LV2, sysutil, RSX) |
| `C:\dev\deps\XenonRecomp` | hedge-dev XenonRecomp @ `ddd128bc` + local changes (uncommitted; saved as `xenon\xenonrecomp-xmb.patch`); exe in `build-clang3\` |
| `C:\dev\deps\clang` | clang / clang-cl / lld 22.1.8 |
| `C:\dev\deps\ghidra_12.1.4_PUBLIC` | Ghidra (settings redirected to `C:\dev\xmb\ghidra_home`) |
| `C:\Qt\6.12.0\msvc2022_64`, `C:\VulkanSDK\1.4.341.1` | RPCS3 build deps |
| `C:\Users\Albert\Downloads\PS3UPDAT.PUP` | Firmware 4.93 (SHA-1 AE2A4F49EC6C09D124FED20F84231CE27D10C7B7) |

## 3. Reproduce the existing pipeline

1. `C:\dev\xmb\build-llvm.bat`, then `C:\dev\xmb\build-rpcs3.bat`
2. Decrypt: `rpcs3.exe --decrypt <files>` (output lands next to input; work on copies)
3. Ghidra headless, language `PowerPC:BE:64:A2ALT-32addr`, with the scripts in `ghidra_scripts`
   (`JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-25.0.4.101-hotspot`)
4. `C:\dev\xmb\recomp\build.bat`, then
   `C:\dev\xmb\build\xmbld.exe --functions C:\dev\xmb\rea\functions C:\dev\xmb\out\boot493 C:\dev\xmb\fw493\vsh_module\vsh.elf C:\dev\xmb\boot\*.prx`
5. `C:\dev\xmb\build-xenonrecomp.bat`, then from `C:\dev\xmb\xenon`:
   `C:\dev\deps\XenonRecomp\build-clang3\XenonRecomp\XenonRecomp.exe xmb_boot.toml C:\dev\deps\XenonRecomp\XenonUtils\ppc_context.h`
6. `powershell -ExecutionPolicy Bypass -File C:\dev\xmb\xenon\compile-recomp.ps1`

### Guest address map (from xmbld)

| Range | Contents |
|---|---|
| `0x00010000`–`0x006F0000` | vsh.elf (fixed addresses; two PT_LOAD segments) |
| `0x01000000`–`0x018C7xxx` | 13 boot PRX, each 64 KB aligned (bases in `modules.csv`) |
| `0x018D0000` | HLE function descriptors (8 bytes each; 432 used, room for 4,096) |
| `0x018D8000` | HLE code addresses (4 bytes each; never executed, used as IDs) |
| `0x018E0000` | `.pdata` (image.elf only; not guest data) |
| after `0x0194F440` | XenonRecomp function lookup table (`PPC_IMAGE_BASE + PPC_IMAGE_SIZE`) |
| to be allocated | stacks, heap (sys_memory), TLS, RSX local/IO memory |

---

## 4. Remaining work: specification

### 4.0 Fix the three record-form gaps (small)

- `clrldi.` (rldicl with Rc=1) at 0x257EBC, `sld.` at 0x5F660C and 0x5F6EC8: XenonRecomp warns "RC bit enabled but
  no comparison was generated". Emit the CR0 update (signed 64-bit compare of the result with 0, SO from XER),
  as in the `rldic.` case.
- **Done when:** the regenerate log has no "RC bit enabled" lines.

### 4.1 Runtime skeleton (`C:\dev\xmb\runtime`, new; clang-cl, C++20, CMake + Ninja)

- **Guest memory:** reserve 4 GB with `VirtualAlloc(MEM_RESERVE)` as XenonRecomp's `base`, commit on demand.
  Load each segment of `image.bin` (format: `XMBIMG01`, count, then `{be32 addr, be32 size, be32 flags, bytes}`).
- **Function table:** populate XenonRecomp's lookup table from `PPCFuncMappings` (in `ppc_func_mapping.cpp`) so
  indirect calls (`bctrl` through OPDs) resolve. HLE code addresses (`0x018D8000 + 4*i`) map to native thunks.
- **Threads:** one host thread per PPU thread, each with its own `PPCContext`, guest stack (allocated in guest
  memory, 16-byte aligned, back-chain zeroed), r1 = stack top, r2 = module TOC, r13 = TLS pointer.
- **Entry:** read the OPD at vsh.elf's `e_entry` (code + TOC), set r2, call the mapped function. Registers and the
  argv/envp block follow `ppu_load_exec` (r3 argc, r4 argv, r5 envp, r6 envc, r7 thread id, r8–r10 TLS, r11
  e_entry, r12 malloc_pagesize). RPCS3 does not run the boot plugins' `module_start` before vsh: vsh loads them
  itself through `sys_prx_*`, so those syscalls must hand back the prelinked modules and call their start OPDs.
- **As built (phase 1):** `xmbrt\build-xmbrt.ps1` compiles the runtime against the generated `ppc_context.h` and
  links with lld-link. Run `out\xmbrt\xmbrt.exe out\boot493 fw493\vsh_module\vsh.elf`. The call log
  (`XMBRT_LOG`, default `xmbrt.log`) has one line per syscall/HLE call; the run stops after `XMBRT_MAX_CALLS`
  (10000) calls or `XMBRT_IDLE_SECONDS` (15) without one. The lookup table covers the whole image span, because
  the HLE stubs sit past `PPC_CODE_BASE + PPC_CODE_SIZE`. Stacks are at 0xD0000000 and user memory at 0x30000000,
  as in RPCS3, so addresses in the two traces line up.
- **Faults:** vectored exception handler that reports the faulting guest address and the last guest function
  (keep a per-thread "current function" breadcrumb in debug builds).
- **Done when:** the binary links with all 224 objects, loads the image, enters vsh's entry point, and the first
  unimplemented syscall/HLE call is reported by name instead of crashing.

### Phase 2 as built: reference traces

- RPCS3 carries a local tracer (`rpcs3-xmb-trace.patch`: `PPUModule.cpp`, `PPUInterpreter.cpp`, `lv2.cpp`). It logs
  syscalls whose `cia` is in vsh or a boot plugin, and import calls (a taken `bcctr` leaving those modules, with
  the return matched at `bclr`). Addresses are translated to xmbld's layout; every module load/unload is logged.
- Capture: set PPU Decoder to "Interpreter (static)" (the tracer needs it), then run RPCS3 with
  `XMB_TRACE=out\trace\rpcs3_trace.txt`, `XMB_TRACE_MODULES=out\boot493\modules.csv`, `XMB_TRACE_MAX=200000`.
  The XMB still reaches the menu at 60 FPS under the interpreter. Switch the decoder back to LLVM afterwards.
- Tooling, `xmbrt\xmbtrace.ts` (bun): `names` builds `out\nid_names.csv` (6,519 NIDs from RPCS3's REG_FUNC
  registrations, hashed like `ppu_generate_id`); `rpcs3` / `xmbrt` normalise either log to one line format with
  repeats collapsed; `diff` prints the first divergence on a thread.
- **Scope finding:** vsh statically links liblv2, so most of its work is direct syscalls. It also loads ~33 modules
  outside the boot set (libfs_utility2, sys_audio, libsre, qglbase, mms*, custom_render_plugin, paf_web, raf,
  x3_*, ...), and calls into them through import stubs and function pointers. These are Sony framework code, not
  APIs to reimplement, so they have to join the recompiled image (as RPCS3 runs them LLE). Decoders on the
  ffmpeg list (libac3dec, libat3dec, libavcdec, libatrac3plus, libjpgdec, libgifdec, libtiffdec, libsmvd4) can
  stay HLE per the swap rule.

### Phase 3 as built: wider image and LV2 kernel

- **Image:** `boot_extra\` holds the 25 extra modules (decrypted copies; 3 from `/dev_flash/sys/internal` via
  `rpcs3.exe --decrypt`). Ghidra function lists for them are in `rea\functions`; `rea\functions_patched` adds
  ranges for jump-table cases Ghidra missed. Relink with
  `build\xmbld.exe --functions rea\functions_patched out\boot493 fw493\vsh_module\vsh.elf boot\*.prx boot_extra\*.prx`.
  Kept out (swap rule): decoders (ac3/at3/avc/atrac3plus/jpg/gif/tiff/smvd4), libsre (cellSpurs), sacd, libmtp,
  mintx_client, libasfparser2.
- **Jump tables:** `xenon\gen-switch-tables.ts <boot493> <functions dir> xenon\xmb_switch_tables.toml [<patched dir>]`
  finds GCC PIC tables (`lis/addi` or TOC-loaded base, `lwzx/extsw/add/mtctr/bctr`), extends functions whose
  cases lie past Ghidra's end, and writes XenonRecomp `[[switch]]` entries. Iterate patch + relink until it
  reports 0 functions to extend. Known gaps: 2 tables whose base register is set far earlier (0x2B05C,
  0x132206C) and 4 switches whose cases sit in a range below the function entry (0x1BC3F30, 0x1C40DC,
  0x1D53674, 0x1EFB08) still fall back to an indirect call / `return`.
- **PS3-vs-Xbox translation fixes (all in the patch; each was a real boot blocker):** `mfocrf` honours its
  field mask (was hard-coded to cr6, so every PS3 `lwarx/stwcx.` loop spun); jump tables dispatch on `ctr`
  (the computed target) instead of an index register vsh often reuses, with an indirect fallback instead of
  `__builtin_unreachable`; `dcbz` clears 128 bytes (Cell line size); out-of-function conditional/ctr branches
  and function spans that fall off their end continue as tail calls (`PPC_CALL_UNRESOLVED`, resolved by the
  runtime); the function-table address comes from the runtime (`PPC_FUNC_TABLE_FROM_RUNTIME`), so relinks
  don't need a full rebuild. Function discovery adds OPD-only targets (`xenon\gen-opd-functions.ts`, ~25k
  C++ methods) and every `bl`/`b` target XenonRecomp flags (`// ERROR`), iterated to a fixed point; xmbld
  dedupes `.pdata` starts. `compile-recomp.ps1` removes stale `ppc_recomp.N.cpp` and tracks header deps.
- **XenonRecomp patch additions:** CR0 for 64-bit record forms and `mulld.`, `vandc`/`vrlw`/`vaddsws`/`lvebx`,
  conditional `tw` as a no-op, hex fallback function names, and `mftb` as `PPC_MFTB()` → runtime `ppc_mftb()`
  (80 MHz, matching `sys_time_get_timebase_frequency`; XenonRecomp's default `__rdtsc()` runs ~40x fast).
- **Kernel (`xmbrt\k_*.cpp`):** one big kernel lock with per-thread sleep/wake like RPCS3's lv2_obj; IDs shaped like
  RPCS3's (type byte << 24 | n << 8). Threads are host threads with a 16 MB reserved host stack;
  `sys_ppu_thread_exit` unwinds with a C++ exception. Memory areas follow what RPCS3 gave vsh (64 KB pages at
  0x10000000, 1 MB at 0x20000000, mmapper from 0x30000000, RSX context 0x40000000, local memory 0xC0000000,
  stacks 0xD0001000). PRX "loading" maps names to prelinked modules, and restores pristine data on reload.
- **Filesystem overlay:** reads from `C:\dev\rpcs3\bin\<mount>`, writes to `out\xmbrt\vfs\<mount>` (copy on
  first write); `/dev_flash` read-only. RPCS3's install is never modified.
- **sys_uart (`k_uart.cpp`):** the AV manager: PS3AV replies as RPCS3 sends them, HDMI plug/HDCP event sequencing.
- **Null RSX (`k_rsx.cpp`):** sys_rsx context/driver info/reports as in RPCS3, a 59.94 Hz vblank thread posting
  0xFED-style vblank events, and a FIFO walker handling jumps/calls, reference, semaphores and GCM
  flip/queue/user-command methods. Drawing methods are skipped until phase 4. Logs "rsx: first command
  buffer submit" when vsh first advances `put`.
- **HLE:** cellSpurs and cellAdec succeed without doing work (swap rule); everything else is ENOSYS.
- **Loop:** `xmbrt\build-xmbrt.ps1`, then `xmbrt\run-xmbrt.ps1` (runs, normalises, diffs against
  `out\trace\rpcs3.norm` on the main thread).

### Phase 4 as built (in progress): function coverage and the GL renderer

- **Function coverage.** The vsh/sys modules loaded after boot needed far more entries than Ghidra found:
  - `xenon\find-import-stubs.ts` lists every import stub (`li r12,0; oris; lwz; std r2,40(r1)`).
  - `xenon\scan-branch-targets.ts` lists `bl`/out-of-function `b`/`bc` targets that start no function, straight
    from the image. Iterate it with `gen-opd-functions.ts <boot493> <functions> <targets file>` until it
    reports 0. The first pass found 6,887 entries in 7 rounds, with no translation round trips.
  - `gen-opd-functions.ts` (OPD mode): the last module's bound was wrong (it skipped x3_mdimp9). OPDs that land
    inside a merged Ghidra body are now accepted when the target begins with `stdu r1`/`mflr r0`.
  - `iterate-errors.sh` (XenonRecomp `// ERROR` targets, one translation per round) is the slow fallback.
  - `rebuild.sh` does relink, switch-table iteration, translate, compile and runtime build in one step.
- **PRX variable imports.** xmbld now binds `num_var` imports (vnids/vstubs) and attribute-0x2000 function
  refs with RPCS3's `ppu_patch_refs` rules. Before this, code like `lis r6,0; lwz r0,28(r6)` read address 0x1C.
  222/228 bind; the 6 unbound ones come from vmclib, libvideoSubsystem and mintx_client (not in the image).
- **Renderer (`xmbrt\rsx_gl.cpp`).** OpenGL 4.6 compatibility context on the FIFO thread, Win32 window
  (1280x720, close = exit). Flips from PPU threads are handed to the FIFO thread (`rsx_gl_poll`).
  - Colour surfaces are GL textures keyed by local offset, with depth/stencil renderbuffers. Flip blits the
    surface at the display buffer offset.
  - CLEAR_SURFACE, vertex programs (TRANSFORM_PROGRAM/CONSTANT), and fragment programs from memory
    (half-word swapped, inline constants read each draw into `fc[]`) are translated to GLSL. The translation
    follows RPCS3's decompilers, without VP branches and without fp16/fp32 register aliasing.
  - Vertex arrays are converted to float4 on the CPU. Index arrays, inline arrays, immediate indices,
    textures (swizzled/linear, DXT, remap via GL swizzle, render-to-texture lookup), blend, alpha, depth,
    stencil, cull and scissor are handled.
  - Not yet: MRT, NV3089/NV0039 blits, mipmaps, vertex textures, two-sided colour, fog.

### 4.2 LV2 syscall layer (`ctx.syscall`, number in r11, args r3–r10, result r3)

- Implement on demand: log every unimplemented syscall number once (name it from RPCS3's
  `rpcs3/Emu/Cell/lv2/lv2.cpp` table) and return `CELL_ENOSYS`.
- **Expected first wave** (verify against the log): `sys_process_*` (getpid, get_sdk_version, exit),
  `sys_ppu_thread_*` (create, exit, join, yield, get_id), `sys_mutex_*`, `sys_cond_*`, `sys_lwmutex_*`,
  `sys_lwcond_*`, `sys_semaphore_*`, `sys_event_queue/port/flag_*`, `sys_timer_*`, `sys_time_*` (tick, timebase),
  `sys_memory_*` and `sys_mmapper_*` (allocations in guest space), `sys_prx_*` (load/start of already-linked
  modules returns their IDs), `sys_fs_*` (open/read/stat/opendir mapped to host folders, see 4.4), `sys_tty_write`.
- Map sync objects onto Win32 primitives; keep IDs in per-type handle tables like RPCS3's `idm`.
- **Reference:** `C:\dev\rpcs3\rpcs3\Emu\Cell\lv2\*.cpp` for argument layouts, error codes and edge cases.
- **Done when:** vsh runs past process init and plugin start without unimplemented-syscall stops.

### 4.3 HLE for out-of-set imports (432 functions, `out\boot493\hle.csv`)

- Group by library: media (cellPamf, cellDmux, cellAdec, cellVdec, libvideoSubsystem, avc_util, msmw*, mms*),
  cellSpurs, ime/osk, network/NP (np_*, sceNpOauth, cellHttp leftovers), others (vmclib, faust_widget, raf).
- **Policy:** stub everything to return success/"not available" first, logging calls. Then implement only what the
  boot path really uses. Media goes to ffmpeg (already in `C:\dev\rpcs3\3rdparty\ffmpeg`), network/PSN stays stubbed.
- **Names:** use RPCS3's NID tables (Ps3ResolveImports logic) so logs show function names.
- **Done when:** the XMB reaches its main loop with only "stubbed" log lines from non-essential libraries.

### 4.4 Filesystem (VFS)

- `/dev_flash` maps to `C:\dev\rpcs3\bin\dev_flash` (read-only), and `/dev_hdd0` to a project folder (e.g.
  `C:\dev\xmb\vfs\dev_hdd0`, seeded from RPCS3's). `/dev_usb000` and `/app_home` map to configurable folders.
- Preserve PS3 semantics: case sensitivity, `CELL_FS_*` error codes, stat layout (RPCS3 `sys_fs.cpp`).
- **Done when:** vsh loads its resources (rco/qrc, fonts, sounds, xml) from dev_flash without errors.

### 4.5 sysutil and system services the XMB needs

- Video out (`cellVideoOut*`: fixed 1080p/720p config), audio out (`cellAudioOut*`, `_cellAudio` via XAudio2/WASAPI),
  pad input (`cellPad`, mapped from XInput/SDL3 gamepads + keyboard), system parameters (language, date format,
  nickname from a config file), `cellSysutil` callbacks (exit game, system menu), time zone, user profiles (dev_hdd0/home).
- **Reference:** `C:\dev\rpcs3\rpcs3\Emu\Cell\Modules\cellSysutil.cpp`, `cellVideoOut.cpp`, `cellPad.cpp`, `cellAudio.cpp`.

### 4.6 Graphics (RSX to Vulkan), the critical path for "exact feel"

- The XMB draws through gcm (`cellGcmSys`) command buffers. Two options:
  - **(a) Recommended first:** embed RPCS3's RSX backend. Build `rpcs3_emu`'s RSX + Vulkan pieces as a library,
    feed it the gcm command buffer and RSX memory regions from guest memory, and present to a native window.
  - **(b) Later:** a slimmer gcm-to-Vulkan translator covering only the commands/shaders the XMB uses
    (capture them with RPCS3's RSX capture while it runs the XMB).
- Must cover the fragment/vertex programs used by PAF (the wave background, icon effects, text), and texture formats.
- **Done when:** the XMB frame renders pixel-comparable to RPCS3's output (screenshot diff against
  `capture-rpcs3.ps1` captures).

### 4.7 SPU usage on the boot path

- vsh.elf embeds one SPU program, and cellSpurs is imported. Identify what the XMB uses SPU for at boot
  (likely audio mixing or decode). Replace it with native code (per the "swap anything" rule); do not translate SPU ISA.

### 4.8 Launcher integration (HTPC goal)

- Game column: hook the game-boot path (`game_ext_plugin` / sysutil exit-to-game) to launch
  `C:\dev\rpcs3\bin\rpcs3.exe --no-gui <game>` or any native executable from a configurable list.
- Startup: fullscreen borderless window, gamepad-only operation, start with Windows (optional).

## 5. Milestones and acceptance

| # | Milestone | Acceptance |
|---|---|---|
| M1 | Runtime links and enters vsh | Process starts, the first unimplemented syscall/HLE is named in the log |
| M2 | Boot path without stops | All module_start calls return; vsh main loop runs |
| M3 | First frame | The XMB background renders through Vulkan |
| M4 | Exact feel | Side-by-side with RPCS3: same layout, animations, sounds, input timing |
| M5 | Launcher | Selecting a game launches it (RPCS3 or a native exe) and returns to the XMB |

## 6. Known issues and gotchas

- Any xmbld change that alters image bytes (relocations, import binding, variable refs) needs a full
  `rebuild.sh`. XenonRecomp bakes `lis`/`addi` immediates into the C++, so a relink alone leaves the
  translated code pointing at the old addresses (a run showed `lis r9,0; lwz r4,0(r9)` after the image was fixed).

- REA (reverse-engineer-anything) rejects PPC64 ELFs, so drive Ghidra headless directly.
- Child processes of the Claude desktop app get AppData redirected (MSIX), which breaks Ghidra's OSGi cache. That's
  why `launch.properties` is edited.
- Use forward slashes in CMake `-D` paths. Ninja paths need `$:` for drive letters.
- RPCS3's prebuilt LLVM needs VS 2026's STL. With VS 2022, build LLVM from source (`build-llvm.bat`).
- `rpcs3.exe --headless --installfw` crashes on exit (0xC0000409) after a completed install.
- XenonRecomp named functions without a symbol `sub_<decimal>` while the mapping uses hex, which left 15
  functions unresolved at link time. Fixed in the patch (`recompiler.cpp`, fallback name now `sub_{:X}`).
- XenonRecomp: ~12,000 "Unable to decode" lines are jump-table data inside function spans (harmless). Its
  analysis needs `pdata_only = true` with this input; without it, it loops forever on split functions.
- In this environment Claude Code's auto-mode permission check blocked some long-running tool runs; add a
  PowerShell permission rule if an agent should run the pipeline unattended.
- Firmware and anything derived from it stays out of git (`.gitignore`).
