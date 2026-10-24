---
task: Native PS3 XMB and operating-system service compatibility
project: xmb
effort: E3
phase: execute
progress: in-progress
mode: algorithm
started: 2026-10-23
updated: 2026-10-23
---

## Problem

**Active direction:** the user changed the goal to a complete native C++ XMB
recreation. Its current criteria and evidence are in [native-xmb/ISA.md](native-xmb/ISA.md)
and [native-xmb/HANDOFF.md](native-xmb/HANDOFF.md). The firmware compatibility
criteria below preserve the earlier execution track and are not the native app's
completion checklist.

Firmware 4.93 VSH renders parts of XMB in the native Windows runtime, but recovery,
navigation, SPURS execution and service teardown expose missing code coverage and
incomplete OS semantics. Registered functions and loaded modules are insufficient
evidence that the corresponding feature works.

## Vision

Open the native XMB, navigate its categories, use supported local media and settings,
and shut down or restart predictably. Its visible presentation should match the PS3.
Each OS dependency has a known implementation, evidence and remaining limitations.

## Out of Scope

Distributing Sony firmware or derived binaries is outside this source project.
Online service operation cannot be established by a local boot. Destructive storage
operations require separate user authorization before interactive execution.

## Constraints

- Firmware 4.93 and recorded RPCS3 behavior are the current reference.
- Preserve the user's filesystem overlay and original RPCS3 lower tree.
- Build native C++ with the existing Windows toolchain; use Bun/TypeScript for tooling.
- Distinguish firmware bindings, native handlers, compatibility placeholders and missing services.
- Follow the user's requested build and boot workflow; do not introduce additional test suites.

## Goal

Run native XMB with reliable navigation, rendering, local service behavior and lifecycle
transitions. Maintain a reproducible inventory of the modules, imported functions,
callbacks and OS services it requires, resolving observed failures with firmware and
runtime evidence.

## Criteria

- [x] ISC-1: The linked module inventory is machine readable.
- [x] ISC-2: Reference boot modules absent from the image are enumerated.
- [x] ISC-3: Imports routed to host handlers are enumerated by library.
- [x] ISC-4: Literal syscall registrations are enumerated after registration overrides.
- [x] ISC-5: The inventory explicitly distinguishes registration from semantic correctness.
- [x] ISC-6: Firmware functions with one-byte bodies are enumerated by module.
- [x] ISC-7: Refreshed function metadata preserves previously recovered entries.
- [x] ISC-8: Refreshed VSH main extension metadata includes the full F00 routine.
- [x] ISC-9: The switch at 01880F70 includes target 01880FE0.
- [x] ISC-10: The regenerated native executable links successfully.
- [x] ISC-11: A fresh native boot reaches a visible XMB category.
- [ ] ISC-12: Navigation through the formerly failing VSH switch completes without missing dispatch.
- [ ] ISC-13: A native boot runs ten minutes without fatal termination.
- [ ] ISC-14: Recovery database rebuilding returns to a usable menu.
- [ ] ISC-15: Guest reboot operation 8201 triggers the normal launcher restart path.
- [ ] ISC-16: Guest reboot parameter fields consumed by startup are decoded and applied.
- [ ] ISC-17: AC3/SPURS queue signaling completes without TASK_ERROR_SRCH in a captured run.
- [ ] ISC-18: PPU/SPU reservation behavior has evidence against task-mask corruption.
- [ ] ISC-19: USB callback teardown completes during an observed guest shutdown.
- [ ] ISC-20: Audio service teardown completes during an observed guest shutdown.
- [ ] ISC-21: Category icons and labels are visibly complete against a reference capture.
- [ ] ISC-22: Background rendering matches the reference capture.
- [x] ISC-23: Keyboard directional input changes the selected category correctly.
- [ ] ISC-24: Cross and Circle actions complete a menu open/close cycle.
- [ ] ISC-25: Audio output is observed on the host device.
- [ ] ISC-26: Process-spawn-dependent services have a working process or documented supported fallback.
- [ ] ISC-27: Absent reference media modules have per-module execution requirements recorded.
- [ ] ISC-28: Native filesystem changes persist across a runtime restart.
- [ ] ISC-29: Settings changes persist across a runtime restart.
- [ ] ISC-30: Network initialization completes without a hot unsupported-service loop.
- [ ] ISC-31: Anti: missing plugin interfaces are never reported as successful loads.
- [ ] ISC-32: Anti: a successful host placeholder is never counted as a verified feature.
- [x] ISC-33: A captured boot invokes crypto RNG with a successfully initialized output buffer.
- [x] ISC-34: Both open-PSID query paths use the same emulated-console identity.
- [x] ISC-35: The emulated-console identity survives a runtime restart.
- [x] ISC-36: Device and main-report DMA contexts have explicit bounded address mappings.
- [x] ISC-37: F12 produces a diagnostic snapshot without terminating the runtime.
- [x] ISC-38: The wider refreshed boot-module function metadata is included in a successful build.
- [x] ISC-39: Concise PoC and future-phase handoffs record entry state, next work and exit criteria.
- [x] ISC-40: Original screenshots document observed startup stages and Game/Video/Music navigation.
- [ ] ISC-41: Floating render targets preserve the guest surface format and correct visible output.
- [x] ISC-42: The project source history, tooling, docs and screenshots exist in the requested private GitHub repository.
- [x] ISC-43: Direct-branch recovery scans the same function spans emitted into .pdata and includes the JPEG failure target.
- [ ] ISC-44: Texture-unit remap and sampling state applies to uploaded images and cached render targets without cross-unit mutation.

## Test Strategy

Existing builds, recorded traces and the requested interactive native boot supply
evidence. No new automated test suite is authorized by this artifact.

| Criteria | Evidence/probe | Pass condition |
|---|---|---|
| 1–6 | `bun xenon/inventory-os.ts`, generated JSON/Markdown | Each stated inventory exists with limitations |
| 7 | Merge preview and preserved original rows | Existing entries retained |
| 8–9 | Refreshed CSV and switch TOML | Full body and named case target present |
| 10 | Existing component build logs | Compiler and linker exit successfully |
| 11–14, 21–24 | Native window observation and session logs | Named interaction visibly completes |
| 15–20 | Existing diagnostic hooks and lifecycle logs | Named transition completes with expected status |
| 25 | Native audio observation | Audible expected output |
| 26–27 | Per-module implementation ledger and runtime evidence | Each required behavior has evidence |
| 28–30 | Requested boot interactions and logs | Persistence/initialization observed |
| 31–32 | Loader source and coverage report review | Failure propagated; coverage claims remain qualified |

## Features

| Work | Satisfies | Depends on |
|---|---|---|
| Firmware and host dependency inventory | 1–6, 27, 32 | Manifests and reference trace |
| Complete function analysis and translation | 7–12 | Existing Ghidra projects |
| Recovery and reboot lifecycle | 14–16, 19–20, 28 | Filesystem, USB, audio, launcher |
| SPURS task execution | 17–18, 25 | Genuine firmware libsre and reservation semantics |
| Rendering and input | 21–24 | RSX pipeline, resources, PAF, HID |
| Wider service compatibility | 26–30 | Inventory and observed callers |

## Decisions

- 2026-10-23: Extended native secure services with Windows CNG and a per-runtime-profile
  identity file. RPCS3 provides the secure-RNG ABI/package/error definitions; its crypto
  RNG function is still a TODO stub. Native crypto RNG fills the requested writable
  range; undocumented crypto-engine create/destroy operations remain unsupported.
- 2026-10-23: Reproduced the old Video window with a clock still at21:04 at host16:17.
  Process survival alone does not meet the intended long-run stability criterion.
  Added a nonterminating diagnostic snapshot and bounded semaphore history rather
  than treating a surviving window as proof of progress.
- 2026-10-23: RPCS3's RSX synchronization routines define device DMA contexts and GL
  completion before label publication. Added those mappings/barriers; whether they
  resolve the observed810 semaphore waits remains to be measured by the new build.

- 2026-10-23: Seeded from HANDOFF.md, existing sources, manifests, traces and user direction.
  The repository has no README. Existing historical handoff checkpoints remain evidence,
  but are not evidence that the newest binary satisfies the unchecked runtime criteria.
- 2026-10-23: Latest missing address 01880FE0 is a switch target in VSH main extension,
  corroborated by decompilation and the recovered signed-offset table. It is not a
  separate dynamically constructed callback.

## Changelog

- Conjecture: isolated missing navigation addresses were missing callback functions.
  Refuted by: refreshed F00 function body and table at 01880F74.
  Learned: post-export analysis must finish before function serialization.
  Criterion now: complete bodies plus switch recovery precede additional entry patches.

## Verification

- ISC-43: `work/branch-span-refresh-build.log` converged after six discovery passes;
  generated mapping includes01A323B0 and no generated unresolved call to that
  address remains.503 generated and23 native objects compiled/linked. This is
  static coverage evidence; execution of that JPEG branch remains unobserved.
- ISC-44: Per-unit samplers and shader remap uniforms are implemented. Diagnostic
  boot `20261023-171608-926` boot1 rendered Game -> Video without a recorded shader
  compile/link error. A simultaneous shared-image/different-remap case has not
  been separately observed, so this criterion remains pending.

- ISC-33: Diagnostic boot `20261023-162424-995`, trace65129–65131 and122915–122917:
  sys984 requested16 bytes and returned0. Service log confirms successful Windows CNG.
  Sys865 package2 requested64 bytes at sequence18384 and returned0.
- ISC-34: Source review: sys872 and appliance-info19005 both call ss_copy_open_psid.
  Runtime sys872 returned0 at sequences26910 and26912. The appliance-info19005 path
  was verified by source inspection, not a separate runtime query.
- ISC-35: Boot `20261023-162149-565` created a16-byte identity file at16:22:01;
  boot `20261023-162424-995` logged loaded persistent identity. File length and
  modification time remained unchanged. Identity bytes were not printed.
- ISC-36: Compiled k_rsx.cpp contains bounded device56616660/56616661 and main-report
  BAD68000 mappings matching the checked-out RPCS3 source. Specific device-DMA traffic
  has not been separately observed.
- ISC-37: F12 emitted snapshot408024calls,48382draws,696flips without termination;
  native window subsequently remained visible and advanced its displayed clock to21:27.
- ISC-38: Wider metadata merge completed; generated build503/503 and native link23
  runtime objects completed successfully in work/component-body-refresh-build.log.
- Final normal boot `20261023-162941-020` rendered the Game menu at21:31 and was left
  open with detailed call tracing disabled. Earlier diagnostic boot also reached
  Music and advanced the displayed clock through21:29; snapshots reached105548draws
  and1488flips. First rebuilt boot's black-screen teardown remains unexplained.

- 2026-10-23 inventory: 46 linked modules, eight recorded reference modules absent,
  411 host bindings across 53 libraries, 290 literal syscall registrations.
- 2026-10-23 refreshed VSH extension: 365 Ghidra functions; merged previously recovered
  entries remain present. Switch recovery now reports 1,185 tables and explicitly
  includes 01880FE0. Native recompilation completed:503 generated +22 runtime objects.
- 2026-10-23 boot `20261023-160129-611`: startup notices rendered; Game list rendered;
  keyboard Left visibly selected Video and displayed its list. Runtime left open.
  Full visual accuracy, repaired-case execution coverage and long-run stability remain
  unchecked. First-use service diagnostics are visible in normal boot logs.
