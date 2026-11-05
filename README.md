# re-ps3: native C++ XMB recreation

The active track is a native C++20 XMB recreation using original assets from the
user's local firmware. Qt supplies desktop/text/media integration and OpenGL
renders the application. The earlier firmware recompilation runtime remains a
reference. Full XMB feature and visual parity are still in progress.

## Start here

- [Native application: build, run and architecture](native-xmb/README.md)
- [Active native handoff](native-xmb/HANDOFF.md)
- [Native completion criteria](native-xmb/ISA.md)
- [PoC, current checkpoint and phase handoffs](docs/README.md)
- [Run and build instructions](docs/POC.md)
- [Native boot screenshots](docs/SCREENSHOTS.md)
- [XMB Manager (companion tooling app)](native-xmb/MANAGER.md)
- [Detailed investigation handoff](HANDOFF.md), newest section 3a first
- [Project criteria](ISA.md)
- [Dependency versions and patches](docs/DEPENDENCIES.md)
- [Archived dated handoffs and phase plans](native-xmb/archive/README.md)

The native foundation renders the main categories, uses the original firmware
font and RCO/GIM icons, loads original Settings text, and provides independent
animated navigation. Most services remain unfinished. Earlier firmware-runtime
recovery, SPURS and rendering issues remain recorded in its handoff.

```powershell
& .\native-xmb\build.ps1
& .\run-native-xmb.ps1
```

## Source snapshot

This repository preserves the project history, native runtime, recompilation and
analysis tools, dependency patches, documentation and screenshots. Local firmware,
decrypted/derived images, generated firmware C++, analysis databases, console
identities and build outputs are excluded. The native app needs an installed Qt
MSVC kit and local firmware assets. The older recompilation track additionally
needs generated firmware code and its analysis inputs.

The build currently expects the existing Windows dependency layout beside this
project: `deps/XenonRecomp`, `deps/clang`, and `rpcs3`. Some scripts still reference
the original development paths; portability remains future work. Supply local
firmware and analysis inputs and follow the detailed handoff before rebuilding.

No redistribution license is assigned by this upload. Refer to dependency
licenses for third-party components and patches.
## Repository layout

| Path | Contents |
|---|---|
| `native-xmb/` | the native XMB application: `src/` (94 files), `manager/` (the XMB Manager), `audit/` (fidelity and firmware audits), `CMakeLists.txt`, and the current documents (`README`, `INSTALL`, `MANAGER`, `HANDOFF`, `HANDOFF-PHASES`, `ISA`, `WORKSPACES`) |
| `native-xmb/archive/` | superseded dated handoffs and phase plans (see the README there) |
| `tools/` | build and capture scripts (`build-*.bat`, `capture-rpcs3.ps1`, `rebuild.sh`, `iterate-errors.sh`) |
| `patches/` | RPCS3 patches the capture pipeline needs |
| `docs/` | earlier-track documentation, phase notes, screenshots, dated reports |
| `out/` | local build and capture output (gitignored; `out/integration` is the current build) |
| `work/` | local verification tools and probe output (`.gitignore` whitelists the tooling) |
| `fw493/`, `boot/`, `boot_extra/`, `rea/`, `xenon/`, `recomp/`, `xmbrt/` | firmware assets and the earlier recompilation track |
| `run-native-xmb.ps1`, `run-xmb.ps1`, `status.ps1` | entry points kept at the root because they resolve their own paths from `$PSScriptRoot` |
