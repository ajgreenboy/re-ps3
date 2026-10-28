# Workspace handoff: scene

**Mission**: own the world behind the XMB - the procedural background, the ribbon wave, HDR and glare, particles,
the cold-boot intro, theme scenes and the RSX program translation layer. Everything here is firmware-program
territory: values come from `.mnu`/RCO/ELF data, not from a designer's taste.

## Owns (write)

`src/hdr.*`, `src/ribbon.*`, `src/particles.*`, `src/coldboot.*`, `src/p3t.*`, `src/rsx_program.*`,
`src/fragment_capture.*`, `src/reference_frames.*`.

## Must not edit

`view.*` (the ui workspace consumes your render calls), `shell.*`, text/icons, services, `main.cpp`,
`CMakeLists.txt`, `work/**`.

## Boundaries

- The render entry points are called from `View::paintGL`/`renderBackground` in `view.cpp`: keep the existing
  signatures (`Hdr::finish`, `Ribbon::render`, `Particles::render`/`renderGlare`, `Coldboot::render`,
  `P3t::load`). A new entry point is fine; a changed one needs a hunk in the ui workspace.
- `Particles::init(gl, Assets&)` is the preferred overload; `init(gl)` resolving `--dev-flash` itself is legacy.
- The glare source hook (`Hdr::finish(..., extraGlare)`) is shared with particles pass 2; coordinate before
  changing its contract.

## Build and run

```powershell
& .\native-xmb\build.ps1 -OutputDirectory C:\dev\out\ws-scene
& C:\dev\out\ws-scene\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <scratch> `
    --render-clock 30 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00 --focus-time 2000
```

`--measure-reference <file>` renders the background offscreen and writes statistics comparable to
`audit/VIDEO-REFERENCES.md`; `--decode-fp`, `--dump-cg`, `--p3t-dump` are the diagnostic CLIs.

## Verification: what green means here

- `bun work\ab-layers.ts --capture --exe C:/dev/out/ws-scene/re-ps3-native.exe` - frame and **top background**
  layers are yours (baselines ~18.28/0.8285 and 20.22/0.9336); other layers must not move.
- Determinism: two `--render-clock 30` captures hash-equal. Particles log step/live/nonfinite/checksum under
  `XMB_PARTICLE_LOG=1`; two runs at the same clock must print the same checksum.
- Preset coverage: day/night/yoake/higure plus the cold-boot presets each need their own capture; a single
  daytime frame does not verify a preset.
- A change that moves the named states needs a deliberate reference refresh from tooling, after acceptance.

## Current state (2026-10-27)

Landed:

- HDR chain: firmware Cg programs translated to GLSL, tone map + glare mip chain, cold-boot presets
  ([../audit/PIPELINE.md](../audit/PIPELINE.md)).
- Ribbon: ported end to end (plugin cloth, `ffd_shader1` lattice, `spline.elf` FFD/camera/bicubic sheet, LINE1
  presets following the plugin's clock keyframes and boot schedule).
- Particles: firmware-shaped pool simulation at the traced 8.8883 ms step; **pass 1 in the HDR scene and pass 2
  as its own `particles_second` program pair** into the glare source; the real `proc_iridescent.tga` from
  `lines.qrc` drives the hue; fresnel alpha chain; deterministic.
- Cold boot: 4.93 intro (RAF scene, script timeline, sound, boot presets), skippable, `--no-intro`.
- Themes: the parser now exposes the full 4.93 `.p3t` contents (92 elements/241 attrs in Air Paint), including
  font/colour selection, pointers, notification, backgrounds and RAF/RAFO scene parsing. Verdict recorded:
  dynamic theme scenes are a new L-sized subsystem, not a slice ([../audit/THEMES.md](../audit/THEMES.md)).

Open (with evidence in the audits):

- **Particle finish**: positional spot, life-bounds/near-far darkness, the untraced `_Color`/`_Gamma`, blending
  mode question, emission volume (the one item the audit calls genuinely untraced).
- **Preset verification**: each preset's capture and its per-layer numbers; the 300 ms page move and the
  open-page recession are now traced (see tooling), which removes two fits from this area's overlap.
- **Dimmed-icon glass is blocked on the background brightness layer** - the ui workspace measured that glass at
  full brightness wins, but dimmed glass inherits a still-too-bright background. That is a scene problem first.
- **Theme colour selection** wiring (month palette override) and the second background image rule.
- **UI glare**: measured and rejected for now (ui workspace decision, recorded); if a firmware UI drawn
  pre-threshold is ever found it becomes a scene+ui change.

## Evidence index

[../audit/PARTICLES.md](../audit/PARTICLES.md), [../audit/PIPELINE.md](../audit/PIPELINE.md),
[../audit/VIDEO-REFERENCES.md](../audit/VIDEO-REFERENCES.md), [../audit/CATEGORY-ICON-FIX.md](../audit/CATEGORY-ICON-FIX.md),
[../audit/ICON-MATERIAL-CLOCK.md](../audit/ICON-MATERIAL-CLOCK.md), [../audit/THEMES.md](../audit/THEMES.md),
[../audit/ANIMATION-SMOOTHNESS.md](../audit/ANIMATION-SMOOTHNESS.md).

## Gotchas already paid for

- Determinism is a contract: `--render-clock` must produce a pure function of time + seed; never integrate from
  wall time or frame count.
- A particle change moves the frame layer by a few hundredths of a dB; report it, do not hide it behind a
  reference refresh.
- `p3t`/`particles` were edited by several sessions; re-read before writing.
- The theme RAF subsystem is a trap: a half-rendered static frame is worse than the honest still background.
  If it is funded, plan it as its own phase, not a slice.

## First tasks

1. P5: per-preset verification (day/night/yoake/higure + boot) with three pinned clocks each; record the
   per-layer table.
2. Particle finish: spot position, darkness/life bounds, blending mode - each measured, kept only if it improves
   the particle crop against the stored reference.
3. Background brightness investigation for the dimmed-glass blocker: measure the reference's unfocused-icon
   luminance against the native background at the same clock, and report the delta the icon class inherits.
4. Theme colour selection + second background rule, with the month palette trace from `_ChangingColor`/amb.bmp.

## Report format

Branch/commit, build dir, per-layer numbers (frame/top background at minimum), determinism hashes, preset list
captured, kept vs reverted with the measured reason, files touched, reference-refresh needs.
