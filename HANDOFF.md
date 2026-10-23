# Handoff: XMB native port

Current state, where everything is, and how to reproduce each step. Status summary: `C:\dev\xmb\REPORT.md`.
Live view: `powershell -ExecutionPolicy Bypass -File C:\dev\xmb\status.ps1`

## Folders

| Path | What |
|---|---|
| `C:\dev\xmb` | Project repo (local git, no remote). Tooling, scripts, configs, REPORT.md, this file |
| `C:\dev\xmb\recomp` | `xmbld` linker sources (`xmbld.c`, `elf_be.c/.h`), `build.bat`, `runtime\ppu.h` |
| `C:\dev\xmb\ghidra_scripts` | Ghidra scripts: Ps3ResolveImports, Ps3Exports, Ps3Functions, Ps3Census, DumpStats |
| `C:\dev\xmb\xenon` | XenonRecomp config `xmb_boot.toml`, `xenonrecomp-pdata-only.patch`, `unrecognized_boot493.csv` |
| `C:\dev\xmb\fw493` | Decrypted fw 4.93 modules (`vsh_module\`, `sys_external\`). Not in git |
| `C:\dev\xmb\boot` | Copies of the 13 boot-path plugin `.prx` files. Not in git |
| `C:\dev\xmb\rea` | Ghidra outputs: `functions\*_functions.csv`, `plugins\`, `vsh_*.csv`, `link_boot_set.csv`. Not in git |
| `C:\dev\xmb\ghidra_work` | Ghidra projects `vsh493w` and `boot493` (analysed, imports resolved). Not in git |
| `C:\dev\xmb\ghidra_proj` | Original Ghidra project `vsh493` (the one opened in the GUI) |
| `C:\dev\xmb\out\boot493` | xmbld output: `image.bin`, `image.elf`, `modules/imports/exports/hle.csv`. Not in git |
| `C:\dev\xmb\out\recomp_boot493` | XenonRecomp output: 223 `ppc_recomp.*.cpp`, `ppc_func_mapping.cpp`, `ppc_recomp_shared.h`, `ppc_config.h`, `ppc_context.h` |
| `C:\dev\rpcs3` | RPCS3 main, commit `0b0fa082ae552ab77a70de4fc1f70110d165116b`, built; `bin\rpcs3.exe`, `bin\dev_flash` (fw 4.93) |
| `C:\dev\deps\XenonRecomp` | hedge-dev/XenonRecomp at `ddd128bc` plus the local pdata_only patch; built in `build-clang3\` |
| `C:\dev\deps\clang` | clang / clang-cl / lld 22.1.8, built from `C:\dev\rpcs3\3rdparty\llvm\llvm` |
| `C:\dev\deps\ghidra_12.1.4_PUBLIC` | Ghidra; `support\launch.properties` redirects settings/cache to `C:\dev\xmb\ghidra_home` |
| `C:\Qt\6.12.0\msvc2022_64` | Qt used by the RPCS3 build (6.11.1 also present, unused) |
| `C:\VulkanSDK\1.4.341.1` | Vulkan SDK |
| `C:\Users\Albert\Downloads\PS3UPDAT.PUP` | Firmware 4.93 (SHA-1 AE2A4F49EC6C09D124FED20F84231CE27D10C7B7) |
| `C:\Users\Albert\Downloads\rpcs3-v0.0.41-19456-2ce45bd9_win64_msvc` | User's prebuilt RPCS3, untouched; used for `--decrypt` |

## Reproduce

1. RPCS3 build: `C:\dev\xmb\build-llvm.bat`, then `C:\dev\xmb\build-rpcs3.bat` (log `C:\dev\rpcs3\build-release.log`)
2. Decrypt: `rpcs3.exe --decrypt <file.sprx|.self> ...`. The output lands next to the input, so copy the modules to `fw493` first.
3. Ghidra headless (needs `JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-25.0.4.101-hotspot`; the language is `PowerPC:BE:64:A2ALT-32addr`):
   `analyzeHeadless.bat C:\dev\xmb\ghidra_work boot493/boot -process -noanalysis -scriptPath C:\dev\xmb\ghidra_scripts -postScript Ps3Functions.java C:\dev\xmb\rea\functions`
4. Link: `C:\dev\xmb\recomp\build.bat`, then
   `C:\dev\xmb\build\xmbld.exe --functions C:\dev\xmb\rea\functions C:\dev\xmb\out\boot493 C:\dev\xmb\fw493\vsh_module\vsh.elf C:\dev\xmb\boot\*.prx`
5. Recompile: `C:\dev\xmb\build-xenonrecomp.bat`, then (working dir `C:\dev\xmb\xenon`)
   `XenonRecomp.exe C:\dev\xmb\xenon\xmb_boot.toml C:\dev\deps\XenonRecomp\XenonUtils\ppc_context.h`

## Gotchas

- REA (reverse-engineer-anything) rejects PPC64 ELFs, so drive Ghidra directly.
- Processes launched from the Claude desktop app get AppData redirected (MSIX), which breaks Ghidra's OSGi cache; that's why `launch.properties` is edited.
- Use forward slashes in CMake `-D` paths. XenonRecomp must be built with clang-cl; its build uses MSVC's lib/link.
- RPCS3's prebuilt LLVM requires VS 2026's STL. With VS 2022, build LLVM from source.
- The `--headless --installfw` run crashes on exit (0xC0000409) after the install completes.

## Open work

1. Translate the remaining 4,161 instructions in XenonRecomp (list: `xenon\unrecognized_boot493.csv`), including routing `sc` (2,612) to a native LV2 layer.
2. Native runtime: guest memory, LV2 syscalls, HLE for the 432 imports outside the boot set (`out\boot493\hle.csv`), RSX to Vulkan.
3. Compile `out\recomp_boot493` with clang-cl.
4. RPCS3 booting `vsh.self` stalls on the first-run "Precompiling shader interpreter variants 0 of 6650" screen; not investigated.

Firmware and anything derived from it stays out of git (see `.gitignore`).
