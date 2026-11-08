# re-ps3: a native PS3 XMB recreation (and its companion tooling)

A native C++20/OpenGL recreation of the PlayStation 3 4.93 XMB that runs on a Windows PC, built from
the firmware's own assets and analysed against console captures. It ships two programs:

| Program | What it is | Documentation |
|---|---|---|
| **`re-ps3-native.exe`** | the XMB itself - category bar, item columns, pages, options menus, dialogs, settings, media panels, the OSK, the PS menu, themes and transitions | [native-xmb/README.md](native-xmb/README.md), [INSTALL.md](native-xmb/INSTALL.md) |
| **`xmb-manager.exe`** | a companion tool that sets up emulators, imports games/ROMs as virtual PS3-style packages, and organizes the library the XMB launches | [MANAGER.md](native-xmb/MANAGER.md) |

Both build from one CMake project and share one Qt runtime; the manager writes the same
`apps.json` registry the XMB reads, so an imported game appears as a Game-column tile and starts
through its mapped emulator.

## Status (2026-11-04)

The recreation is **buildable, gated and measured**, not "finished". The honest snapshot:

| Measure | Current result | Where the detail lives |
|---|---|---|
| Canonical render gate | **6/6 layers PASS**, frame 18.84 dB / 0.8456 SSIM against the matched console frame, text-presence guard green | [HANDOFF.md](native-xmb/HANDOFF.md) section 4 |
| Regression states | **12/12 PASS** against deliberate self-baselines | same |
| Behavioural oracles (Tier A) | **35/35** capturable states | [audit/PARITY-SCORECARD.md](native-xmb/audit/PARITY-SCORECARD.md) |
| Matched fidelity (Tier B) | 0 states pass every gating region, 2-3 informative, 23 real region failures - the remaining differences are named and measured | [audit/FIDELITY-VERDICT.md](native-xmb/audit/FIDELITY-VERDICT.md) |
| Element-level fidelity | 375 audited elements: **44.8 % match, 23.2 % differ, 17.1 % absent, 14.9 % unverified** | same verdict |
| Surface breadth | **125 firmware surfaces: 23 reachable, 40 partial, 62 absent** (browser, store, trophies, messaging, save data, full players) | [audit/FIDELITY-SURFACES-MAP.md](native-xmb/audit/FIDELITY-SURFACES-MAP.md) |
| Settings | **143 firmware rows, 68 showing real host values, 0 absent**; live machine data in System Information | [audit/SETTINGS-COVERAGE.md](native-xmb/audit/SETTINGS-COVERAGE.md) |
| Manager | **120/120 core assertions**, desktop UI + 25-verb CLI, import proven end-to-end into the XMB | [MANAGER.md](native-xmb/MANAGER.md) |

What works well: the frame and bar geometry, icons and typography, transitions, the settings pages,
the OSK, dialogs, media panels, the PS menu, themes, the Manager's import/organization/backup tools.
What is visibly unfinished: the background/ribbon exposure residuals, some player panels, and the
surfaces listed above that have no native page yet. The unprovable set (PSN services, disc/BD rows,
in-game XMB, notifications, media players, volume HUD) is documented with the artifact that would
settle each one instead of being guessed.

## Quick start

Requirements: Windows 10/11 x64, Visual Studio 2022 with the C++ workload, a Qt 6 MSVC kit
(6.8+; the build picks the newest installed kit), and a local PS3 4.93 `dev_flash` directory
(the app never ships firmware assets).

```powershell
# build both programs into out\integration
& .\native-xmb\build.ps1

# the XMB (points at the local firmware flash)
& .\run-native-xmb.ps1

# the companion manager (same data directory by default)
& .\out\integration\xmb-manager.exe
```

`run-native-xmb.ps1` resolves its own paths and passes `--dev-flash`, `--data-dir` and
`--vsh-modules` for you; `native-xmb/build.ps1 -OutputDirectory <dir>` builds anywhere. The XMB reads
`<exe dir>\data` unless `--data-dir` is passed, which is the same default the manager uses.

First run of the manager: **Emulators > Detect installed** (it finds portable installs such as
`C:\dev\rpcs3`), check the **Platform map**, then **Import** ROMs/ISOs/folders. Imports become
`<data>\pkg\<id>\` virtual packages plus an `apps.json` entry; press F5 in the XMB to refresh the
Game column.

## Repository layout

| Path | Contents |
|---|---|
| `native-xmb/` | the application: `src/` (94 C++ files), `manager/` (Manager core, CLI, UI, tests), `audit/` (fidelity and firmware audits), `CMakeLists.txt`, current documents |
| `native-xmb/archive/` | superseded dated handoffs and phase plans, with an index of what each was replaced by |
| `work/` | verification tools (`ab-layers`, `parity`, transition harness, release check) and fidelity probes |
| `out/` | local build and capture output (gitignored; `out/integration` is the current build) |
| `fw493/vsh_module/`, `rea/`, `ghidra_scripts/` | decrypted VSH modules and RE analysis assets the native fidelity work reads (local only, gitignored or sourced) |
| `run-native-xmb.ps1` | the launcher, kept at the root because it resolves its own paths from `$PSScriptRoot` |

## Verification instruments

The project measures itself instead of trusting screenshots:

- `work/ab-layers.ts` - the canonical render gate (six content-neutral layers + text-presence guard)
  and the twelve named-state regression references; `--strict` re-gates annotated layers.
- `work/parity.ts` - the matched-fidelity corpus: catalogue states, Tier A behavioural oracles, Tier
  B region budgets, Tier C traces, `contentDiffers` annotations with mandatory reasons.
- `work/transition-compare.ts` - frame-by-frame console-vs-native transition verdicts with
  side-by-side GIFs.
- `work/release-check.ps1` - credential, firmware-data, dump, broken-link and dead-commit gate.

## Documentation

- [Native application README](native-xmb/README.md) - architecture, build, controls.
- [Active handoff](native-xmb/HANDOFF-2026-11-08.md) - current state, what landed, open work.
- [Plan of record](native-xmb/audit/PHASES-2026-11-07.md), [ISA](native-xmb/ISA.md).
- [Manager](native-xmb/MANAGER.md), [install/packaging](native-xmb/INSTALL.md),
  [write fences](native-xmb/WORKSPACES.md).
- [Fidelity verdict](native-xmb/audit/FIDELITY-VERDICT.md) and the six domain audits it consolidates.
- [Audit index](native-xmb/audit/README.md).
- [Archived dated documents](native-xmb/archive/README.md).

## Source snapshot and honesty

The repository preserves the project history, native runtime, analysis tooling, dependency patches
and documentation. **Local firmware, decrypted/derived images, generated firmware C++, analysis
databases, console identities, API keys and build outputs are excluded.** The XMB needs a local
`dev_flash`; the native fidelity work additionally reads the decrypted VSH modules and RE analysis
assets under `fw493/vsh_module/`, `rea/` and `ghidra_scripts/`, which stay local and gitignored.

Measurements are labelled by provenance throughout the audits: **traced** (from a console frame,
RCO object, MNU table or decoded program), **fit** (a native approximation with its removal
criterion), or **unprovable** (with the artifact that would settle it). Where a game is imported
through the Manager, a real `.pkg` install is deliberately **not** supported - this is a PC
recreation, so ROMs/ISOs/folders become virtual packages instead, and encrypted retail packages are
refused with an explanation rather than faked.

No redistribution license is assigned by this upload. Refer to dependency licenses for
third-party components and patches.
