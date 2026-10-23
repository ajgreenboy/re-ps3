# re-ps3: native XMB proof of concept

Experimental PS3 firmware 4.93 XMB reconstruction for Windows using static PPC
recompilation, native OS services and an OpenGL renderer.

## Start here

- [PoC, current checkpoint and phase handoffs](docs/README.md)
- [Run and build instructions](docs/POC.md)
- [Native boot screenshots](docs/SCREENSHOTS.md)
- [Detailed investigation handoff](HANDOFF.md), newest section 3a first
- [Project criteria](ISA.md)
- [Dependency versions and patches](docs/DEPENDENCIES.md)

The current build reaches interactive Game, Video and Music menus. Rendering,
recovery, SPURS stability and wider OS/media support remain incomplete.

## Source snapshot

This repository preserves the project history, native runtime, recompilation and
analysis tools, dependency patches, documentation and screenshots. Local firmware,
decrypted/derived images, generated firmware C++, analysis databases, console
identities and build outputs are excluded. A clone alone cannot run XMB.

The build currently expects the existing Windows dependency layout beside this
project: `deps/XenonRecomp`, `deps/clang`, and `rpcs3`. Some scripts still reference
the original development paths; portability remains future work. Supply local
firmware and analysis inputs and follow the detailed handoff before rebuilding.

No redistribution license is assigned by this upload. Refer to dependency
licenses for third-party components and patches.
