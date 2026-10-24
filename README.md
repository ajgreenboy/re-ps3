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
- [Detailed investigation handoff](HANDOFF.md), newest section 3a first
- [Project criteria](ISA.md)
- [Dependency versions and patches](docs/DEPENDENCIES.md)

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
