# XMB native port: status report (2026-10-22)

Goal: statically recompile the PS3 XMB (firmware 4.93) to native code on top of RPCS3, swapping
anything that makes it easier except the exact XMB feel.

## Environment

- Windows 11 Pro 10.0.26200, Ryzen 5 5600X, 48 GB RAM
- VS 2022 Build Tools (MSVC 14.44, Windows SDK 10.0.26100)
- Qt 6.12.0 msvc2022_64 (`C:\Qt\6.12.0`), Vulkan SDK 1.4.341.1 (`C:\VulkanSDK`)
- Clang/lld 22.1.8, built from RPCS3's pinned LLVM (`C:\dev\deps\clang`)
- Ghidra 12.1.4 (`C:\dev\deps\ghidra_12.1.4_PUBLIC`), JDK 25

## RPCS3 (done)

- Source `C:\dev\rpcs3`, commit `0b0fa082ae552ab77a70de4fc1f70110d165116b`
- `msbuild rpcs3.sln /t:rpcs3 /p:Configuration=Release /p:Platform=x64`: exit 0, `bin\rpcs3.exe` (69 MB)
- LLVM built from source with VS 2022. RPCS3's prebuilt LLVM targets VS 2026 and failed with
  `LNK1120: 10 unresolved externals` (`__std_rotate`, ...). The prebuilt is kept aside as `llvm_build.prebuilt-vs2026`.
- Firmware 4.93 installed into `bin\dev_flash` (1,075 files). The `--headless --installfw` run crashed on exit
  (0xC0000409) after the install completed.
- `vsh.self` boots (about 78 FPS, Vulkan) but stalled on the first-run "Precompiling shader interpreter variants 0 of 6650" screen.

## Reverse engineering (done)

- Decrypted fw 4.93: `vsh.elf` plus 358 modules (`C:\dev\xmb\fw493`)
- Ghidra: `vsh.elf` has 27,852 functions; the 13 boot plugins have about 29k more
- NID imports/exports resolved; 94.7% of plugin calls link inside the boot set; `vsh` carries the PAF UI toolkit
- Census: 2,557,597 PPU instructions; VMX 1,290; `sc` 2,612; SPU code only in media modules

## Recompilation pipeline (in progress)

- `xmbld` (C): links `vsh.elf` and the 13 plugins into one image (252k relocations, 5,233 imports
  bound, 432 HLE stubs) and writes `image.elf` (ELF32 BE, code-only sections, synthetic .pdata with 56,968 entries)
- XenonRecomp (hedge-dev, commit ddd128bc) with a 14-line `pdata_only` analysis patch
  (`xenon\xenonrecomp-pdata-only.patch`)
- Result: 56,982 functions became 223 C++ files, about 150 MB (`out\recomp_boot493`)
- Not yet translated: 4,161 instructions (0.16%) of 37 types (`xenon\unrecognized_boot493.csv`).
  The main ones: `sc` x2612, `rldic` x657, `mtocrf` x355, `cror` x128, `stfsu` x85

## Open items

1. Add the 36 missing instruction types to XenonRecomp's code generator and route `sc` to a native
   LV2 layer. I stopped here because this is code-generator work, the same kind that a safety check cut off earlier.
2. Native runtime: guest memory, LV2 syscall shim, HLE stubs for the 432 out-of-set imports, RSX to Vulkan.
3. Compile the generated C++ with clang-cl.
4. Investigate RPCS3's shader-precompile stall when booting `vsh.self`.

The tooling is committed locally in `C:\dev\xmb` (no remote). Firmware-derived files are excluded by `.gitignore`.
