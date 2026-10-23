# Phase 4 handoff — stability and visual fidelity

Status: active. This continues the existing Phase 4 in the detailed handoff.

## Entry checkpoint

The native build reaches XMB and accepts category navigation. The supported build
and evidence are summarized in `docs/POC.md`. Most visible background/category icons
are incomplete; a surviving window alone does not establish an active guest.

## Work next

Current rebuild: `work/branch-span-refresh-build.log`. A later run of the previous
binary failed at JPEG target `01A323B0` (LR `01A31DFC`, thread `01000142`). The direct
branch scanner now matches the contiguous `.pdata` spans built by `xmbld`, rather
than only scanning Ghidra's disjoint body ranges. Discovery converged in six
passes and generated mapping now includes the JPEG target. Full build completed:
503 generated objects and23 runtime objects. That specific JPEG branch has not
yet been observed executing in the rebuilt boot.

The renderer sampling follow-up uses a sampler object per texture unit and shader
channel-remap uniforms. Uploaded images and cached render targets share this path;
binding an image on another unit no longer rewrites its image swizzle/filter/wrap.
Diagnostic `20261023-171608-926` boot1 reached Game -> Video, clock22:17 ->22:18,
with no observed shader compile/link error or missing dispatch before F12.
The [new captures](../SCREENSHOTS.md) still show incorrect background/icons.
Normal boot `20261023-171447-817` went black after plugin teardown (5755 draws,
245 flips, FIFO GET=PUT); preserve both results. Next inspect wide-texel float
deswizzling against RPCS3 GLTexture.cpp, which swizzles in32-bit blocks.

Latest renderer change: float textures 9A/9B/9C/9F now use native floating-point
OpenGL storage with guest-endian word conversion and reference remaps. The updated
renderer compiled and linked; session `20261023-163845-853` observed first uploads
of 9A, 9B and 9F with GL error zero. 9C is not yet observed. The
[screenshots](../SCREENSHOTS.md) still show incomplete background/category icons.
Follow-up: surface-format decoding is confirmed as bits0..4 in RPCS3's
`rsx_decode.h`. Render targets now use RGBA16F/RGBA32F/R32F for guest formats11/12/13,
and cached surfaces reallocate when their colour format changes. The renderer
compiled and linked. Boot `20261023-165609-569` observed formats11 and12 with complete
framebuffers (`8CD5`) and GL error zero. Format13 remains unobserved. Boot1 reached
Game; its follow-up screenshot still shows incorrect background/category icons.
Next inspect cached render-target sampling (which bypasses texture remap/filter
setup), shader translation and surface contents across reuse.

1. Reproduce the black-screen transition in normal mode. Capture F12 before/after.
2. Compare draw/flip counters, FIFO GET/PUT and main-thread state. Distinguish guest
   UI teardown from a blocked GPU, absent dispatch and SPURS task failure.
3. Investigate the intermittent `80410905` SPURS task error using the existing
   task-mask wrapper. Reservation corruption is a hypothesis, not a diagnosis.
4. Compare one visual defect at a time against RPCS3: texture formats/remaps,
   shader instructions, render targets, blending and resource selection.

## Code and evidence

| Area | Start at |
|---|---|
| Graphics | `xmbrt/k_rsx.cpp`, `xmbrt/rsx_gl.cpp` |
| SPU / SPURS | `xmbrt/k_spu.cpp`, `xmbrt/spurs_trace.cpp` |
| Live snapshots | F12; `xmbrt/main.cpp`, `xmbrt/k_thread.cpp` |
| Function coverage | `xenon/gen-opd-functions.ts`, branch/switch scanners |
| Analysis | Ghidra projects under `ghidra_work/`, scripts under `ghidra_scripts/` |

The task-mask wrapper is enabled by `XMBRT_TRACE_SPURS_TASK=1`. It observes genuine
firmware; do not force a successful signal or write arbitrary semaphore values to
hide a failed wait. GL completion barriers are conservative and may affect speed.

## Exit criteria

- A normal-mode session navigates multiple categories and continues advancing its clock.
- A recorded ten-minute session has no fatal dispatch, SPURS failure or stalled UI.
- The reported recovery-to-black path is explained and its corrected transition observed.
- Category icons, background, text and menu placement match reference captures.
- Evidence includes the binary/build checkpoint, mode, input sequence and limitations.

## Handoff to Phase 5

Record the remaining visual defects, supported recovery/reboot behavior, performance
observations and known service gaps. Keep a normal-mode baseline available while
expanding OS/media support. Phase 5 may investigate dependencies concurrently; it
does not make Phase 4 complete.
