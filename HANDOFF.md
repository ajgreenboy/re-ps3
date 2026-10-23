# Handoff: XMB native port

**Short PoC and phase handoffs:** start at [docs/README.md](docs/README.md).
It links the current run/build instructions and Phase4/5/6 handoffs. This file keeps
the detailed chronological investigation; its newest section3a takes precedence
over historical counts/checkpoints elsewhere below.

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
| Native runtime | Phase 4 in progress | 40-module firmware-SPURS build visibly renders PS3 startup dialog (14,969 draws in 45 s). Adding OS services and five firmware components in a 45-module rebuild. Full menu/navigation still pending. See §3a. |
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

## 3a. Next session: start here (2026-10-23)

### Current continuation: JPEG coverage and texture-unit sampling

Previous boot `20261023-165609-569` boot1 later failed at `01A323B0`, LR `01A31DFC`,
thread `01000142`, in libjpgdec. This is a generated conditional branch from a gap
between Ghidra body ranges. `xmbld` emits a contiguous `.pdata` function span,
but the direct-branch scanner had inspected only the disjoint ranges. The scanner
now uses matching spans, found21 missing targets initially and converged after six
passes. The refreshed mapping includes `01A323B0` and generated sources have no
remaining unresolved call to that address. Build evidence:
`work/branch-span-refresh-build.log`; completed503 generated +23 runtime objects.
Refreshed mapping has128473 entries. Normal boot `20261023-171447-817` went black
after UI plugin teardown: F12 recorded117457 calls,5755 draws,245 flips and FIFO
GET=PUT=000103D8. No shader compile/link error or missing-function failure was
logged. Saved screenshot08. Two runtime instances briefly overlapped during the
diagnostic restart; the older stalled process was then closed. Diagnostic boot0
is therefore not an isolated timing comparison.

Renderer follow-up: per-unit GL sampler objects apply filter/wrap even for cached
render targets. Shader remap uniforms replace texture-object swizzle mutations so
different units can sample one image with independent remaps. Float selectors
retain the reference rules. Texture content hashes include pitch; remap changes
do not trigger image reuploads. Runtime logs confirm cached float-target sampling
and initial GL error0. Diagnostic `20261023-171608-926` requested reboot8201; boot1
reached Game and accepted Left to Video. Clock22:17 ->22:18. No shader compile/link
error, missing dispatch or semaphore timeout appeared before F12:318347 calls,
9939 draws,185 flips; FIFO pending (GET1154C, PUT102EC). Screenshots09/10 record
Game/Video. Background/category icons remain wrong. The specific recovered JPEG
branch has not yet been observed executing. Runtime remains open on Video in
tool session77812. No long-run or normal-mode stability claim.

Next rendering lead: RPCS3 GLTexture.cpp treats swizzled texels wider than32 bits
as multiple32-bit texels (width multiplied by block_size/4); our float upload still
swizzles whole8/16-byte texels. Confirm the active texture's linear/swizzled flags,
then correct its address calculation. Translation still emits unrecognized
instruction diagnostics; branch convergence does not establish instruction coverage.

### Latest continuation: private GitHub source snapshot and float render targets

Source/history uploaded to the private `ajgreenboy/re-ps3` repository, branch main.
Initial upload commit `ad98c0a` contains101 tracked files, docs/screenshots, source
tooling and full dependency patches pinned in `docs/DEPENDENCIES.md`. Firmware,
generated firmware outputs, databases, identities and local binaries remain local.

`rsx_gl.cpp` now decodes surface colour format bits0..4 and uses RGBA16F/RGBA32F/
R32F for formats11/12/13. Cache reuse includes the colour format. Framebuffer
completeness is checked before drawing; format13 uses replicated red sampling.
Compiled and linked successfully. Boot `20261023-165609-569` observed formats11
and12 with FBO8CD5 (complete), GL error0. Format13 has not yet been observed.
The boot displayed a recovery prompt, subsequently requested reboot8201 and the
launcher started boot1. Do not infer successful filesystem recovery from that
restart; startup-mode decoding remains incomplete. Boot1 visibly reached Game at
21:57; the background remains black and category icons remain incorrect. Capture:
`docs/screenshots/07-float-surfaces-game.jpg`. Runtime left open at Game.

### Latest continuation: PoC docs, screenshots and floating texture uploads

Short run/build instructions and future phase handoffs are now in `docs/README.md`.
`docs/SCREENSHOTS.md` records six original native-window captures: startup notice,
filesystem-recovery prompt, photosensitivity warning, Game, Video and Music.
Session `20261023-163845-853` used Return then Escape to bypass offered recovery,
then Left twice for Game -> Video -> Music. No filesystem rebuild was executed.
Window remains open on Music. Visible clock advanced from21:43 to21:44.

Rebuilt `rsx_gl.cpp` and linked successfully. Float formats9A/9B/9C/9F now upload
to RGBA16F/RGBA32F/R32F/RG16F with endian conversion and reference remaps. This
boot observed9A,9B,9F uploads with GL error0;9C remains unobserved. Captures still
show black background and incorrect category icons. Next rendering issue:
`surface()` always allocates RGBA8, including floating surface formats; cached
render targets bypass texture upload. Verify format decoding before patching it.
No long-run stability or exact-rendering completion is claimed.

### Continuation: native security services, wider function bodies, RSX synchronization

**Latest checkpoint:** build succeeded **503 generated +23 runtime objects**. Normal
launcher is now running in tool session61534, logs
`out/xmbrt/sessions/20261023-162941-020`. Diagnostic boot immediately before it,
`20261023-162424-995`, visibly navigated **Game -> Video -> Music**, with clock
advancing21:25 ->21:29. Two F12 snapshots:408024calls/48382draws/696flips, then
766382calls/105548draws/1488flips. No semaphore timeout, missing dispatch or SPURS
SRCH was observed in that diagnostic session. This is a bounded positive result,
not a long-run stability claim. Closed diagnostic window normally via Alt+F4.
The final normal session rendered its startup notices, then the **Game menu at
21:31** after Return/Escape. It is left open with numeric tracing disabled.

Secure-service runtime evidence: sys865 package2(64 bytes), sys872(two queries),
sys984(repeated16-byte requests) all returned0 in that trace. Previous boot
`20261023-162149-565` created16-byte console-psid.bin at16:22:01; next boot logged
loaded persistent identity, preserving file length and timestamp. No identity bytes
were printed. Sys976(crypto-engine create),524(IO-error listener),27(process spawn)
remain unsupported; do not equate RNG support with a complete crypto engine.

**Important negative result:** first rebuilt normal boot `20261023-162149-565`
became black after unloading UI modules. F12 showed11299draws/276flips, FIFO GET=PUT,
main waiting in the VSH event dispatcher atLR0043376C. Later presentation continued
without new draws. No unsupported SPU opcode, SRCH, semaphore timeout or shutdown
request was logged. Cause still unknown; trace mode's different scheduling may be
relevant but is not a proven explanation. The next traced boot reached the menu.

`run-xmb.ps1 -TraceCalls` now enables full buffered numeric traces; ordinary launch
keeps them disabled. Do not leave TraceCalls on for indefinite user sessions.
Runtime reports310 registrations; static inventory292 literal registrations. Only
two modules still have one-byte metadata candidates(explore_category_game,msmw2);
the PAF/VSH utility export-body gaps were removed by this rebuild.

- Merged all13 staged `work/functions-refresh` CSVs into `rea/functions_patched`
  with `xenon/merge-function-bodies.ts`:23 added entries; PAF extension6 export bodies
  expanded, VSH utility4 expanded. Full generated build completed503/503 objects,
  zero unresolved direct/switch-tail scan targets. Runtime build completed23 objects;
  log `work/component-body-refresh-build.log`.
- New `xmbrt/k_security.cpp`: sys984 crypto RNG now fills guest output with Windows
  CNG rather than ENOSYS; sys865 validates package2, the256MB bound and secure-service
  error codes from RPCS3 sys_ss.h. Package1 matches the current privileged VSH/root
  package behavior (24 zero bytes). Removed the old unbounded rand() implementation.
  Checks writable committed guest spans before access; zero-size requests are no-ops.
- Sys872 and appliance-info19005 now share one16-byte emulated PSID, persisted in
  `console-psid.bin` next to the executable. Generates it with CNG on first request;
  validates existing size, does not replace invalid identities. Optional
  `XMBRT_CONSOLE_PSID`32hex override. No physical console ID is extracted or logged.
  Crypto-engine create/destroy and process spawning remain unsupported.
- RSX comparison against RPCS3 nv406e/nv4097/RSXThread found missing device DMA and
  main-report DMA contexts. Added bounded mappings and the device+30 zero-write
  flip-sync behavior from nv406e. Semaphore loads/stores now use atomic_ref, and GL
  completion precedes label/reference publication. This is a conservative glFinish
  barrier; performance and rendering impact require observation, not an exact GPU model.
- Semaphore timeouts now include context, offset, FIFO GET and recent16 semaphore
  commands. Acquisition waits also process pending host presentation. Existing
  bounded timeout policy remains; no forced success write was added for arbitrary labels.
- F12 requests an asynchronous **nonterminating** PPU/SPU/RSX diagnostic snapshot.
  Thread registry metadata is now copied under try_lock before thread suspension,
  avoiding live unordered_map iteration during guest thread creation/exit.
- Prior boot `20261023-160129-611` still had a visible Video menu at host16:17, but
  its displayed clock was stuck at21:04 and log had two40300810 waits for1 with0.
  **This is not evidence of a stable16-minute session.** Existing executable PID25524
  was stopped after exact-path verification to permit replacement. Needs new boot.

### System-wide compatibility audit and VSH switch correction (latest)

**Latest completed build/boot:** 503 generated +22 runtime objects linked successfully.
Normal launcher is running in tool session29057, boot logs under
`out/xmbrt/sessions/20261023-160129-611`. Native window rendered startup notices;
Return acknowledged the first notice, Escape returned from the optional filesystem
repair prompt. **Game menu rendered, then Left visibly selected Video with its list.**
No missing-address failure or SPURS SRCH in the captured log as of this checkpoint.
Window left open for user. This does not establish long-run stability or exact visuals.

First-use diagnostics now expose missing sys621(gamepad ycon interface),699(BD
emulation),27(process spawn),872(open PSID),976(crypto engine create),524(IO-error
listener),984(random generation). Three fs_utility_init NIDs use the success fallback.
Actual runtime reports **308 registered syscalls**; static scan counts290 literal
registrations because loop registrations are intentionally outside that scan.
Random-generation ABI in RPCS3 `lv2/sys_crypto_engine.cpp` is `(buffer, u64 size)`;
RPCS3 itself only has a TODO success stub. Existing865 uses rand() and also needs
proper host RNG semantics. No new RNG/PSID implementation was added in this pass.
Latest user-facing output copies: `XMB-OS-inventory.md`, `XMB-host-imports.csv`,
`XMB-observed-service-gaps.txt` in the chat outputs folder. Import inventory includes
RPCS3 source symbol candidates for75/411 host NIDs; remaining internal names unknown.

User wants wider OS reconstruction and a running native XMB. `ISA.md` now records
the project goal, evidence requirements and unchecked runtime criteria.

- Latest stopped normal boot: `out/xmbrt/sessions/20261023-154113-762` reached the Game
  category, then missing dispatch **01880FE0**, LR01565188. Earlier 01881054 is in the
  same routine. **These are switch case targets, not independent callbacks.**
- Reanalyzed `vshmain_ext.prx` in existing Ghidra `components493`; 365 functions.
  The exported F00 function had been serialized as one byte before queued analysis
  completed. It now covers F00–F73 and F9C–11CF. Merged complete bodies into patched
  CSV while preserving previous extra entries. Recovery now finds **1,185 switches**;
  table at01880F74 contains FE0 and1054. Full generated rebuild completed successfully.
- `Ps3Exports.java` now disassembles newly discovered exports before creating functions.
  `Ps3Functions.java` now calls `analyzeChanges` before serializing bodies. Corrected
  function exporter ran successfully on the existing VSH extension database.
- Refreshed all13 original boot PRXs in Ghidra boot493, outputs staged under
  `work/functions-refresh`. **These wider refreshed CSVs have not been merged into
  the active image yet.** Preview under `work/functions-merge-preview`: PAF extension
  has6 expanded export bodies, VSH utility4; total23 additional function entries
  across boot modules. Merge with `bun xenon/merge-function-bodies.ts
  work/functions-refresh rea/functions_patched`, then full component rebuild. Tool
  retains recovered entries/ranges, normalizes overlapping ranges and keeps backups.
- `bun xenon/inventory-os.ts` regenerates `out/os-inventory/inventory.json` and `.md`.
  Current static inventory:46 linked modules,411 host import NIDs across53 libraries,
  290 literal syscall registrations after overrides. Registration is not a semantic
  correctness claim. Reference boot has8 modules absent from linked image:
  libat3dec,libavcdec,libatrac3plus,libsmvd4,libasfparser2,sacd,libmtp,libtiffdec.
  They require per-feature analysis before claiming media functionality.
- Normal boot suppressed numeric syscall/HLE trace lines, including ENOSYS. LV2 and
  HLE dispatch now emit bounded first-use **unsupported service** diagnostics outside
  numeric trace suppression. HLE fallback `hle_ok` also emits **success placeholder**.
  Startup log now says registered rather than implemented syscall count.

Other changes since previous checkpoint:

- `xmbrt/spurs_trace.cpp` optionally wraps genuine firmware cellSpursSendSignal.
  Enable `XMBRT_TRACE_SPURS_TASK=1`: on80410905 it logs task masks before/after and
  caller. Build resolves entry/implementation names from exports NIDE0A6DBE4.
  Link map verifies the wrapper owns the public C++ weak-alias name and calls the
  original generated C implementation. No failing mask capture yet; reservation
  corruption remains a hypothesis, not a confirmed diagnosis.
- Diagnostic black-screen teardown reached **sys379 op8201**, LR000D4E84, r5=600,
  returnedENOTSUP then waited. This is a GameOS LPAR1 reboot request. Runtime now
  validates/preserves its parameter packet and exits75 for normal launcher restart.
  File `out/xmbrt/lpar1-boot-parameters.bin`; **startup-mode fields are not yet decoded
  or applied**. Actual8201restart completion still needs observation.
- USB event-thread shutdown completed in that trace, so that specific black wait
  was beyond the earlier USB549 deadlock. Evidence `work/lpar1-restart-unsupported.*`.

Do not claim complete OS semantics, stable navigation or exact rendering. Latest
native screenshots show category selection and text, with substantial rendering
gaps. AC3/SPURS SRCH remains open independently of the corrected VSH switch.

### Active continuation: missing OS components (45-module rebuild)

#### Black screen after filesystem/database recovery (latest continuation)

User reports reaching recovery/rebuild and then a black screen. The completed 45-module build
compiled **487 generated + 21 runtime objects**. A 90-second boot recorded **12,557 draws and 314 flips**,
then firmware unloaded plugins and graphics services. Main waited to join `libusbd_callback_thread`:
syscall **549 `sys_usbd_event_port_send`** was ENOSYS and syscall 540 slept forever. USB now has a
queued three-u64 event service, blocked-receiver wakeups, and finalize. Syscall **345** now implements
RPCS3's parent-container destruction behavior (reject nonzero r4, destroy parent).

A subsequent boot visibly displayed **"The database on the system storage will be rebuilt."**
It then faulted in MMS at `01BADF10` after loading absent **`x3_mdimp10.sprx`** as a successful stand-in:
the expected plugin interface remained null. Evidence preserved in
`work/mdimp10-null-interface.log` and `.stderr.txt`. The actual decrypted `x3_mdimp10.prx` is now
analyzed in Ghidra `components493`, added to `boot_extra`, and undergoing a **46-module rebuild**.
This is a separate concrete fault from the USB shutdown deadlock.

Loader now returns ENOENT for unavailable `x3_mdimp*` interfaces and propagates failures from list
loads. It tracks live module instances so loading an already live image does not restore pristine
data over running state. Added syscall **328**, validating the extended container allocation's
24-byte permission records and routing to the existing shared-memory allocator; full shared-memory
container budget accounting remains incomplete.

Added **379 `sys_sm_shutdown`** with guest shutdown and reboot operations, following RPCS3. Reboot
requests exit **75**; `run-xmb.ps1` starts a fresh runtime, preserves separate logs per attempt, and
limits repeated restarts to three. It disables diagnostic call/idle limits. This path still needs
observed boot evidence; do not claim a successful recovery restart or usable menu yet.

The initial 46-module rebuild succeeded with **502 generated + 21 runtime objects**. Enabling syscall
328 then exposed an MSMW child-service handshake wait: syscall 27 falsely returned success although
no child process was created. It now returns ENOSYS explicitly; the following boot rendered the
database recovery prompt again. Evidence: `work/media-child-handshake-hang.log` and `.stderr.txt`.
Supporting the actual child-process media service remains future work; do not describe it as running.

That next boot reached `x3_mdimp10` but its start callback `020E01E4` had no translated entry.
`work/rebuild-components.ps1` had only run OPD discovery in extra-target mode, skipping a full descriptor
scan for the new module. It now scans descriptors on each linked image and includes that scan in its
convergence condition. It discovered 68 additional entries overall; the pending build has **128,307
entries**, **1,184 switch tables**, zero unresolved direct/switch targets, and **503** generated objects.
`work/mdimp10-missing-start-entry.log` preserves the failure before this correction.

The **503 generated + 21 runtime** build completed. Its next stop was a truncated import thunk at
`020ECAC4`: Ghidra's range ended at `020ECADB`, halfway through the thunk. Extended that existing
`x3_mdimp10` range through `020ECAFB` (next function begins CAFC); only generated file 474 recompiled.
Evidence: `work/mdimp10-truncated-import-thunk.log`.

**Recovery completion verified:** the following boot completed database rebuilding and called
`sys_sm_shutdown(op=0200)`, exiting **75** as intended. Full trace and stderr are saved in
`work/recovery-completed-reboot.log` and `.stderr.txt`. It did not remain stuck on the black screen.

**First visible XMB menu verified:** `run-xmb.ps1` then reached the Game column, showing Saved Data
Utility, PlayStation Store and "Corrupted Data" entries. Keyboard navigation changed selection.
This is not full visual correctness: category icons/background are incomplete and corrupt-content
entries require investigation. Session logs: `out/xmbrt/sessions/20261023-150825-552/` (stderr contains
diagnostics; boot-0.log contains the same service diagnostics after process exit). A later menu action/initialization stopped at
missing `vshmain_ext` entry **01881054** (LR01565188). Added that entry and its discovered branch targets;
the next rebuild is pending. Do not describe navigation as stable until that boot is observed.

#### External reference supplied by user: ps3RE

Reviewed https://github.com/TheRouletteBoi/ps3RE using its public recursive tree and selected text files.
Index saved at `work/ps3re-tree.json`; selected references under `work/ps3re-reference/`.

- `RCO docs/rco's.txt`: maps RCO IDs to Sony widget classes (PhPlane, PhXmBar, PhXmList, PhScene,
  PhXmItem); identifies DOM type/name queries. Useful for labeling the remaining visual issues.
- `RCO docs/Rco objects dump/`: actual widget hierarchies from Game context menu, Friends, browser,
  PSN sign-in and other screens. Read the Game-context dump; it includes default theme, cameras,
  busy indicator, cursor and notification widgets. This establishes expected scene structure, not
  the cause of a rendering defect.
- `488/VSH/vsh_488_cex.self.idb`: about 96.6 MB, potentially useful for identifying analogous functions
  near our 4.93 unnamed callbacks. Database contents have NOT been opened/exported. README requires
  IDA Pro 6.8 or 7.5; do not claim symbol coverage based only on the filename.
- `446/LV1/LV1 decompilation/sysmgr_ss.fself.elf.c`: read decompilation includes shutdown/reboot and
  AV-set/TSMA notification paths. Other LV1/security-service decompilations are available. These are
  old-firmware behavioral references, not direct implementations of our missing 4.93 LV2 services.
- `085_010/dev_flash085/vsh/module/*symbols.txt`: readable old VSH/SDK symbols (many names truncated).
  Useful naming clues; do not import their addresses into 4.93.

No external code has been copied into runtime sources; downloaded references remain in ignored work/.

**Deeper ps3RE work completed:** downloaded the 4.88 VSH IDB and verified its Git blob SHA-1
`c95243025d8eff60f47d1671d598b11668f304bd` (SHA-256
`af3a2022dac45d6fcb6d77c72e49ab86deb34061ed85a1cd75b525e7c0e8b185`).
No IDA installation was found in standard tool locations. REA session inspection reports no configured
IDA/Ghidra provider; its Windows Ghidra bridge does not admit our PPC target. No REA configuration was
changed. Used a bounded, read-only TypeScript parser instead; no Python executed.

- `work/idb-labels.ts`: IDA1 container v6 / B-tree 2.0 traversal. Verified all **1,713,250** records against
  header count and strict key order. Exports **22,798** address labels, **31,231** address comments and
  **28,359** function/chunk ranges with **zero undecoded function records**. Tail ownership and complete
  discontiguous bodies/types remain undecoded. Raw exports: `work/ps3re-reference/vsh488-labels/`.
- `work/match-vsh-labels.ts`: matches initialized source code to 4.93 executable ELF segments via unique,
  byte-identical 64-byte windows, with full recorded-function byte comparison when ranges are available.
  **4,713 named code matches**, including **714 PAF NID labels**. Function-inclusive pass found **20,594**
  fully byte-identical recorded ranges. Do not infer full behavior equivalence from these matches.
- `work/ps3re-reference/missing-verified-vsh-heads.json`: **51** identical function-range starts absent
  from dispatch. **39** register helpers, **2** conversion routines, **10** anonymous entries. Candidates
  only; small helpers can fall through/shared code. Not automatically added to the build.
- User deliverables under the chat's `outputs/`: `ps3RE-findings.md`,
  `ps3RE-vsh493-matched-labels.csv`, `ps3RE-vsh-entry-candidates.csv`. Metadata only, no firmware bytes.

**Latest runtime after navigation-entry rebuild:** **128,311 entries**, **503 generated + 21 runtime**
objects built successfully. Boot moved past `01881054` but aborted in AC3/SPURS at
`queuePush.c:127`, code **80410905 (CELL_SPURS_TASK_ERROR_SRCH)**. Firmware dump shows task#0 waiting
on a SPURS queue, and an inconsistent/corrupted backtrace. No unsupported SPU opcode preceded it.
Reference `cellSpursSendSignal` returns SRCH when task masks are inconsistent or the task is disabled;
inspect masks and reservation behavior before choosing a fix. Logs:
`out/xmbrt/sessions/20261023-152428-029/boot-0.log` and `.stderr.txt`.
This is still open; do not claim stable menu navigation. No runtime/build process is currently running.

The 40-module firmware-SPURS build **visibly rendered the PS3 shutdown warning dialog**, verified in
the native window with Computer Use. A no-input 45-second checkpoint submitted **14,969 draws, 278 flips,
604 methods**, with FIFO GET=PUT. A later interactive run logged `RAF Error 80023f06`, CPU 212.86% over
30 frames, tore down the graphics SPURS groups, then waited for `SceWaveMain` (sys_audio event queue).
This is not evidence that every missing firmware module caused a crash; follow the concrete failures.

User explicitly requested the supporting OS components required for a full XMB boot. Work now includes:

- **Filesystem:** persistent overlay whiteouts fix guest unlink succeeding while the lower file remains
  visible. This caused metadata DB delete/recreate to fail O_CREAT|O_EXCL with EEXIST/80030406. Added real
  fsync, mount descriptions and block/free-space query outputs. Existing lower-layer data is preserved.
  The HDD mount EEXIST log alone is expected for the already mounted filesystem.
- **Audio hardware:** `k_rsxaudio.cpp` now sends firmware start events, consumes actual DMA ring entries,
  updates indices/timestamps and sends period completions. Start/stop/close/finalize have state. A dynamic
  WinMM sink plays guest serial stream-zero stereo samples. UART AUDIO_MODE/ACTIVE/INACTIVE/MUTE now
  configure it, including audio modes embedded in AVB_PARAM. Multichannel/SPDIF output remains incomplete.
- **Input:** Home=PS, Enter=Cross, Esc=Circle, arrows=D-pad, S=Square, T=Triangle, Space=Start, Tab=Select.
  Keys apply to the native window, merge with controller input and clear on focus loss.
- **Virtual memory:** sys_vm 300–313 now allocate/track VM blocks, validate ranges, resize physical budgets
  and report residency/statistics. Mapped pages remain in host RAM; no swap-file service is modeled.
  sys_process_spawns_a_self2 now returns ENOSYS: no child process is implemented.
  The earlier success stand-in caused callers to wait forever for absent child IPC.
- **Performance diagnostics:** `XMBRT_BUFFERED_TRACE=1` keeps full calls in xmbrt.log with periodic flushes
  and omits their duplicate stderr copy. Other diagnostics still go to both. SPU instruction counts/PCs
  publish every 1,024 instructions rather than using a locked increment for every instruction.
- **Firmware:** added `mintx_client`, `libadec`, `libac3dec`, `libjpgdec`, `libgifdec` to `boot_extra`.
  Ghidra project `components493` produced exports and complete function ranges after a second analysis.
  `libadec_internal.prx` is byte-identical to libadec and was not linked a second time.

Audio start notifications immediately exposed an unrecognized sys_audio switch at old address
`01E95278`, targeting `01E952B8`. The switch scanner now recovers a TOC-loaded table cached in a stack
slot. This is a generator coverage fix, not a runtime bypass.

`work/rebuild-components.ps1` links the image, iterates direct-target and switch discovery to a fixed
point, translates, compiles and builds the runtime. Current discovery converged in 10 rounds:
Initial 45-module build compiled **486 generated + 20 runtime objects**, 87 MB. Its first audio-enabled
boot exercised the hardware rings but hit the sys_audio switch described below. The current dispatch
audit has **45 modules, 124,166 function entries, 1,185 switch tables, zero unresolved direct targets and
zero unresolved switch tail targets**. Expected generated object count is now **487** (runtime **21** with
`k_net.cpp`); check `out/compile.log` and `out/build-xmbrt.log` before claiming the latest rebuild succeeded.
The pre-component image/runtime snapshot is `work/native-first-dialog/`; that executable includes the
new audio worker and needs the newly recovered sys_audio switch, so it is not a frozen first-dialog binary.

Next: finish the rebuild; boot with buffered trace and SPU capture; check metadata DB creation, real audio
group execution, mintx daemon calls and decoder initialization. Reproduce the startup dialog, use Enter
to dismiss it, and reach the menu. Do not claim the full XMB is working until it is visibly navigable.

Latest boot evidence with the new services (before the final switch rebuild):

- VM map syscall succeeds; metadata DB creation error no longer appears.
- Audio opens Windows stereo PCM at 48 kHz, completes actual DMA and repeatedly starts/joins its firmware
  SPU (`01000100`, normal exit `0102`, PC `008A8`). Normal stop logs are now limited to four per thread;
  use `XMBRT_SPU_VERBOSE=1` for all stops.
- `mintx_client` now executes firmware instead of HLE ENOSYS. It exposed a hot loop in missing network
  select. `k_net.cpp` supplies Winsock IPv4 stream/datagram sockets, bind/connect/listen/accept, addresses,
  options, send/receive, interruptible select/poll, abort and basic ioctls. DNS uses the host's configured
  resolver. IPv6/P2P, ancillary message APIs and some interface controls remain incomplete.
- After select support, boot passed 200,000 calls and compiled 20 GL programs. No metadata DB error,
  decoder allocation error, unsupported SPU opcode or RAF performance error was seen before the next
  stop. One daemon still logged `80130121`; do not describe networking as complete.
- The next stop was VSH target `0007D138` from switch `0007D018`. Prefer original Ghidra function bodies
  over generated fragments when finding a switch owner, and extend every fragment containing the same
  bctr through its later cases. Static audit then found 76 switch tail targets without function entries.
  `xenon/scan-switch-targets.ts` now detects these; the rebuild adds entries and repeats discovery before
  compiling. This is wired into both `rebuild.sh` and `work/rebuild-components.ps1`.

### Previous checkpoint: execute firmware SPURS (40 modules)

`libsre.sprx` was present in the RPCS3 module trace but missing from our linked set. It is now decrypted
at `boot_extra/libsre.prx`, analyzed, linked and recompiled. The full rebuild compiled **447 generated
objects plus 20 runtime objects**. `out/boot493/hle.csv` contains no `cellSpurs` imports: they resolve to
firmware libsre at `019C0000` (TOC `019F9AB0`). Later module addresses shifted, so old trace addresses
require module-relative comparison. The current translation log contains many decode warnings from
over-wide function metadata covering data; the build succeeds, but do not describe it as warning-free.

The old 39-module HLE build and image are preserved at `work/spurs-hle-baseline/`. Its 36 draws below
are historical evidence, not the draw count of the current firmware-SPURS build. The reason for using
firmware libsre is that RPCS3's SPURS job-chain HLE is incomplete too; copying its TODO stubs cannot run
RAF's submitted job chains.

Current runtime progress:

- Firmware creates the one-SPU Lines group `04000300` and two-SPU RAF group `04000400`.
- USER image segment sources use offset +16, thread index supplies the high ID byte, and each group
  start restores the LV2 loader, arguments and image segments.
- Added CNTB, SUMB, XSWD, FSCRRD/FSCRWR and MPYA as the firmware reached them. FPSCR reads/writes retain
  writable bits; floating-point arithmetic still uses the existing approximate host implementation.
- Group join permits restart. Group exit requests every peer to stop and waits for all host threads.
- Connect-all-threads assigns the chosen SPU event port to each thread. Write-LS consumes a scalar u64,
  and set-argument updates the four 64-bit arguments used on the next start.
- Added the remaining integer multiply variants and FCMEQ after MPYA. Group RUN events now use their
  own queue; firmware connects EXCEPTION events separately, which must not receive RUN notifications.
- This firmware-SPURS build now submits actual OpenGL draws and advances the main thread into input
  polling. The next stop was indirect target `0039879C` (LR `003988DC`), a VSH switch case.
- `gen-switch-tables.ts` incorrectly let a shorter, later function range hide an overlapping parent
  range. Its interval lookup now falls back to earlier covering ranges. It recovers the switch at
  `0039877C`, including target `0039879C`; 1,147 switch tables are emitted. Regeneration/compile follows.
- Repeated halts at `01320` after an unsupported opcode are secondary failures from restarted incomplete
  execution. Always inspect the first unsupported instruction, not only the end of the log.

Diagnostics: `XMBRT_CAPTURE_SPU_STATE=1` captures the first unsupported instruction or firmware halt
per SPU object as `out/xmbrt/spu-ID-ls.bin` and `spu-ID-regs.txt`. Files can be stale if a later run never
stops; compare modification times. `work/spu-inspect.ts . ../rpcs3 <LS-file>` disassembles the capture
against the local RPCS3 opcode table. Watchdog state rows include each raw SPU's PC and instruction count.
Build only `xmbrt/k_spu.cpp` and relink `out/xmbrt/link.rsp` for these runtime edits; retranslation is unnecessary.

### Historical 39-module HLE baseline

**Continuation checkpoint (2026-10-23).** The complete pipeline rebuilt successfully with 440 generated
objects and 20 runtime objects. Another 34 missing branch targets were added to the patched function CSVs
to a fixed point; `0x018D31F8` now resolves. Changes remain uncommitted on top of `8c708fc`, `de737ba`.

- `xmbrt/k_spu.cpp` now runs the embedded bootstrap SPU. Its firmware sends the actual port-33 replies;
  the fabricated bootstrap completion was removed. USER image segment addresses are at offset 16 of the
  24-byte segment record, and SPU thread IDs use the requested SPU number.
- Fixed preferred-lane order, BI/BISL, four-register destinations/masks, IOHL, SF, branches, immediate
  decoding, integer element operations, shifts, quadword controls, floating operations, MFC list transfers,
  GETLLAR/PUTLLC opcodes/status and channel counts. `XMBRT_SPU_CHECKS=1` enables eight existing instruction checks.
- `cellSpursCreateTask` now loads the task ELF and starts an interpreter thread with task arguments and
  the SPURS local-store context. The `TK` marker at LS `0x1E8`, task allocation block count, task info and
  module ID must be initialized; missing these caused `80410909` and early task exits.
- Event flags now initialize guest data, attach queues, set/clear bits, wake SPU task waiters and block PPU
  waiters. Both task ELFs (runtime addresses `2050B780` and `20461D00`, entry `0x3070`) now repeatedly wake,
  perform DMA work and return to `WaitSignal`. Last complete 30-second checkpoint: 574,245 and 15,399,481
  task instructions, both waiting for signals, no unsupported opcode or guest fault.
- GL now logs an actual submitted draw, after calling OpenGL. Last complete checkpoint: **36 draws,
  75 flips, 599 methods, 11 shader programs**. First draws target small offscreen surfaces; the native
  window remains black. A timed checkpoint exits with code 5 intentionally.

**Current blocker: RAF job chains.** The guard shim still synthesizes one completion event per notified
batch without executing its jobs. The RSX FIFO reaches a self-jump waiting for that work to patch the
command buffer: GET `0040A0A0`, guest address `2060A0A0`, word `2040A0A0`, reference `FFFFFFFF`.
Do not skip this command artificially. Implement the submitted job work.

Job-chain attributes and guest guard records are now initialized. `XMBRT_SPU_JOB_TRACE=1` logs the first
two submitted command arrays and captures pointed-to 256-byte payloads as `out/xmbrt/spurs-job-*.bin`.
Observed chain entry `200F4310`, descriptor size 256. Commands include guard-address-or-`0xF`, literals
`2` and `5`, descriptor pointers ending in `0`, and another pointer tagged `6`. Decode these from runtime
evidence before implementing execution. RPCS3's `spursJobChainEntry` in `cellSpursSpu.cpp` is itself TODO;
its PPU-side chain/guard structures in `cellSpurs.h/.cpp` are still useful references.

Useful diagnostics:

- `XMBRT_SPU_TASK_TRACE=1`: first 16 DMA transfers, last 32 instructions when a task stops.
- `XMBRT_CAPTURE_SPU_TASKS=1`: captures task ELF files under the runtime output directory.
- Watchdog dumps include `spurs state` rows with PC, instruction count and wait state.
- `work/spu-inspect.ts . ../rpcs3 [task.elf]` disassembles with the local RPCS3 opcode table. REA/Ghidra
  rejected SPU ELF architecture, so these listings are ordinary local byte analysis, not REA analysis.

The original bootstrap requirements below remain reference material for syscall completeness.

**Original boot trace before SPU execution was implemented:**

1. The main thread imports a small SPU program embedded in vsh (`_sys_spu_image_import`, src 0x6B9980, size
   0x8D4). It creates a 1-thread group, binds event queue A with `sys_spu_thread_bind_queue`, connects queue B
   on port 0x21 with `sys_spu_thread_connect_event`, and starts the group.
2. A ScePafJob thread posts work to queue A and waits on queue B (`sys_event_queue_receive` at lr 0x0043C33C).
   With no SPU running, the reply never comes. RPCS3 joins that job at trace call 28,280; ours never does.
3. Downstream, sys_audio starts and joins its own group about 2,600 times (one mix batch per run). The
   custom_render plugin (wave) then initialises cellSpurs (first call at RPCS3 trace 56,467). SPURS is HLE'd
   inside RPCS3 and also needs SPU execution.

**What to build** (reference: RPCS3 0b0fa08, `Emu/Cell/SPUInterpreter.cpp`, `SPUThread.cpp`, `lv2/sys_spu.cpp`):

- **New `xmbrt/k_spu.cpp`.** Register its syscalls from `lv2.cpp` and remove the SPU stand-ins from
  `k_extra.cpp`: 157 (image import), 165, 170–173, 178, 181/182, 184, 187/188, 190–194, 251/252.
- **Image import.** Parse the SPU ELF (`PT_LOAD` → COPY + FILL segments). Write the image struct as a KERNEL
  type (1) whose `entry_point` is the object id, as RPCS3 does. Thread initialize also has to accept USER
  (type 0) images whose segment list is in guest memory; sys_audio uses one.
- **IDs.** SPU thread id = `spu_num << 24 | (group_id & 0xFFFFFF)` (the trace shows `0x200` for group
  `0x4000200`). Group ids come from `kobj_add(0x04, …)`.
- **Group start.** Zero the LS, write the thread loader/args at 0x30, deploy the segments, zero the GPRs, and
  put arg N in the preferred doubleword of r3–r6. Send the group RUN event if one is connected. Run each SPU
  thread on its own host thread.
- **Group join** must block the PPU thread with `sleep_thread`/`wake_thread` (`kernel.h`) until every thread
  exits. Cause is 1 for group exit (stop 0x101) or 2 for all threads exiting (stop 0x102); status comes from
  the out mailbox.
- **Channels:**
  - Mailboxes, SigNotify 1/2, decrementer, event mask/stat with the lock-line-lost event, and the MFC
    channels.
  - DMA can complete synchronously.
  - GETLLAR/PUTLLC should compare and copy the 128-byte line under one lock. PPU `stwcx.`/`stdcx.` are host
    CAS calls in the generated C++ (`__sync_bool_compare_and_swap`), so this interoperates.
- **Event protocol (SPU side):**
  - `SPU_WrOutIntrMbox` code < 64 is a send event (reply CELL_OK into the in mailbox); < 128 is a throw event
    (no reply). The event goes to `spup[code & 63]` with source `0xFFFFFFFF53505501`,
    d1 = SPU thread id, d2 = `spup << 32 | low 24 bits`, d3 = out mailbox value.
  - Stop 0x110/0x111 is receive/try-receive on the queue bound to the spuq number in the out mailbox. Reply
    in the in mailbox is `CELL_OK, d1, d2, d3`, or EBUSY/EINVAL.
  - The queues are `KEventQueue` (`k_event.h`). Hold `g_kernel` while touching them; polling for SPU-side
    receive is acceptable at first.

**How to verify:**

1. Rebuild with `xmbrt\build-xmbrt.ps1`; the runtime alone is enough, so no retranslation is needed. Then run
   `$env:XMBRT_DUMP_AT='70'; xmbrt\run-xmbrt.ps1 -TimeoutSeconds 90 -MaxCalls 600000`.
2. The main thread should join the second ScePafJob thread and create the next one, matching RPCS3's
   join/create sequence: `grep "thread_join\|_sys_ppu_thread_create"` on T01000000 in `out\trace\rpcs3.norm`.
3. Then expect `hle cellSpurs` lines from custom_render (0x195Bxxx) and raf (0x1DDDxxx).
4. Done when `rsx: N draws` (logged at dump) is above 0 and the window shows something. Draws go through
   `rsx_gl.cpp`, which is written but has not seen a real draw yet. Expect shader-translation fixes; compile
   errors are logged with their GLSL source.

**After that:** SPURS (custom_render waits on `cellSpursEventFlagWait` about 1,300 times in RPCS3's boot),
then renderer debugging against RPCS3 screenshots, then phase 5 (input via `k_pad.cpp`, audio from the
rsxaudio ring to WASAPI).

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
