# Native XMB — handoff: what remains for an exact match

**Goal.** A C++ program whose output is identical to the PS3 4.93 XMB. Every layer comes from the firmware's own
programs, parameters and assets, read from the user's local dev_flash. Where porting isn't practical, a layer is
reimplemented, and its result is measured against the reference video (`nDL5YJt2aaA`, 60 fps, 1080p).

`audit/PIPELINE.md` maps each layer to its firmware source. `audit/VIDEO-REFERENCES.md` records every measurement.

## Ground rules (unchanged)

- Firmware, decrypted modules, extracted assets, captures, recordings, console identity and the VFS overlay never go
  into Git. Commit only source, docs and approved screenshots.
- Use only the local firmware. Scripts use Bun/TypeScript; no Python without approval.
- Commit as `Al <ajgreenboy@gmail.com>` to the private origin `ajgreenboy/re-ps3`. Other agents also commit here:
  run `git status` first and commit only your own files.
- Build with `native-xmb/build.ps1`. It fails with LNK1104 while `re-ps3-native.exe` is running; stop the process
  first. The build log is UTF-16.
- Verify with `--script` (keys, `grab=`, `burst=<prefix>@KEY`), never by eye alone. Compare against frames of the
  reference video at the same state.

## Where it stands

| Layer | State |
|---|---|
| Background, ribbon, HDR glare | Firmware Cg programs (RSX→GLSL) with traced inputs. Ribbon geometry is fitted, not traced from spline.elf |
| Particles | Firmware shaders; the motion is a CPU rebuild of the SPU particles.elf |
| Category icons | Firmware glass shader port; clock/season RGB and preset luminance now derived from firmware; full parity remains open |
| Item icons, labels, columns, dialogs, info bar, options panel, OSK | Qt drawing with firmware RCO textures and strings; layout, sizes, opacity and timing measured from the reference |
| Text | Native-resolution GL glyph atlas with Qt shaping/rasterization; firmware rasterizer parity and glow parameters remain open (phase 2) |
| Motion | One measured ease (25%/frame at 60 fps, τ 58 ms); 150 px column cross-fade on category moves |
| Host settings | System Information, network list, video/audio output and master volume read and write the real PC |

## Phases to 100%

### Phase 1: category icon renderer (in progress)
- Firmware: `icons.qrc` quad.vpo + icons_{full,cheap,no}_ss[_no_shad].fpo + mask.fpo, ICONS.mnu (+ day, night, yoake,
  higure, boot and music overrides), texenv.dds, amb.bmp, `icontex.qrc` normal maps and `dif`.
- The uniform packing in custom_render_plugin is VMX code that Ghidra decompiles poorly (apply = FUN_000473b4, draw =
  FUN_00040ce8). Read the values instead from a real run. The local RPCS3 build (`C:\dev\rpcs3`, rebuilt with
  `build-rpcs3.bat`) dumps each distinct fragment program ≥2000 bytes at draw time when `XMB_FP_DUMP=<dir>` is set:
  `.ucode` with patched constants, `.vc` vertex constants and `.txt` texture units. Boot
  `bin\dev_flash\vsh\module\vsh.self`, then run `re-ps3-native --decode-fp icons:lib/icons/icons_full_ss.fpo@<dir>` to
  get the named uniform values per draw.
- Then add an `IconRenderer` (like `Hdr`/`Ribbon`): draw a quad per icon with the firmware programs, with the
  framebuffer behind it as `_BackgroundMap`. Pick the ICONS.mnu override from the clock as the day/night code does.
  Match against icon crops from the reference.

### Phase 2: text renderer
- Active from 2026-10-25 at the user's direction. See `audit/TEXT-RENDERER.md` for the implementation, recovered
  PAF coverage/style behavior, RCO Text offsets, local evidence paths and remaining uncertainties.
- GL atlas now handles menu/subtitle/dialog/info/OSK text, with batched glyphs and separate coverage/halo.
  Native size snaps at the viewport scale; resizing clears the caches. Qt shaping preserves fallback and bidi.
  This is the renderer foundation, not a completed port of the PS3 rasterizer.
- Build independently with `build.ps1 -OutputDirectory ../out/native-phase2` when running from native-xmb;
  this avoids build-log/executable locks from simultaneous Phase 1 work. Scripted captures support `size=1920x1080`.
- The firmware draws text through paf (libfont with FreeType-style rasterisation of `dev_flash/data/font` SCE-PS3 Rodin
  faces) and applies glow and shadow from RCO text attributes.
- Work: find the glyph rasteriser settings (hinting, gamma, size snapping) and the glow/shadow parameters in
  paf/explore_plugin. Render glyphs into a GL atlas at native resolution instead of QPainter text. Compare with 3×
  crops of reference labels: focused, unfocused, subtitle and dialog text.

### Material and background gaps reported by the user (2026-10-25)
- `_ChangingColor` now samples amb.bmp at firmware clock/season coordinates; `_LuminanceParams` now derives
  from the control-point curve scaled by preset GLASS. Both reproduce the prior night capture numerically;
  daytime capture parity is still pending. See `audit/ICON-MATERIAL-CLOCK.md` for proof and limitations.
- Open-column item icons still take the Qt path, including original RCO images. Route the appropriate firmware
  material inputs through the item renderer after its asset/UV mapping is understood.
- Background is visibly brighter than RPCS3 at the same hour, which also brightens unfocused icons through
  `_BackgroundMap`. Traced with `XMB_BG_SCENE=<png>` (dumps the pre-tone-map HDR scene, first frame only): the
  excess is already in `back_colours0`'s scene, not the HDR tone map. At 19:30 the native scene's lower bands rise
  toward the bottom (luma ~44-58) while RPCS3's background sits lower there (~39-52, peaking at the ribbon). Suspects
  are the night-palette inputs (`_NightTime`/`_NightBrightness`/`_MonthTime` or the night texture decode), not the
  night exposure. No correction verified yet.

### Phase 3: item, column and dialog drawing through the firmware's own layout
- Initial trace: system_plugin infobar/infotext and OSK templates mostly have zero/dynamic geometry. Do not
  replace measured pixels with those zeros. `basic_plugins.prx` FUN_00007c7c owns infobar initialization;
  its final configuration goes through vshcommon_28549FD0 and runtime PAF setters. Local read-only export:
  `out/native-reference/phase3-infobar-init.c`. Exact geometry projection remains to trace.
- Most of the 2D XMB is explore_plugin/system_plugin driving paf widgets from RCO objects: positions, scales and
  animations live in the RCO object trees (`rco-behavior` dump) and plugin code. The current values are measured
  from the video.
- Work: decode RCO object/anim attributes (objattribdef/animattribdef ini are already extracted) and the
  explore_plugin item layout constants. Replace measured numbers with traced ones: column x, row pitch, icon sizes,
  focus scale, label offsets, fade curves, the options panel and the dialog layout.
- Item icons: the firmware's icon glow/focus effect and the "PS" frame for game icons.

### Phase 4: motion and timing from firmware
- Replace the measured ease with the explore_plugin animation code (it may be a per-frame lerp of 0.25; confirm).
  Category/column/page transitions, the item-above-focus passage, the delay before help text appears (~0.5 s at
  rest), info-bar clock updates, the scroll-acceleration rule for held keys.

### Phase 5: remaining firmware renderers and states
- Ribbon geometry from spline.elf (SPU) instead of fitted constants; particles from particles.elf exactly, including
  the wind response.
- Boot: coldboot animation and sound (ICONS.mnu coldboot1–5 overrides, LINE/HDR boot presets); the "gameboot"
  sequence when launching.
- Theme system: other colour themes, `.p3t` themes, wallpaper mode as the firmware composites it.
- Screen states: screensaver/dim, Power off, user login screen, notifications (tex_notification_*), the in-game
  XMB (xmb_ingame).

### Phase 6: content and features
- Remaining XMBML categories and items that are placeholders ("Unavailable"), driven by the firmware XML.
- Media playback UI (photo/music/video controls from photoviewer/videoplayer/audioplayer RCOs).
- PSN-dependent indicators stay off unless a real state exists; don't fake online features.

### Phase 7: verification harness
- Automated A/B: render named states with `--script` and compare them with frames extracted from the reference
  (PSNR/SSIM on crops). Add a threshold per layer so regressions are visible.
- A second reference: capture the same states from RPCS3 running the real XMB (same firmware) at 1080p. This is a
  pixel-exact target with no video compression.

## Useful entry points

- `src/view.cpp`: scene, layout, UI layer (MSAA FBO), background/ribbon/HDR/particles orchestration.
- `src/shell.cpp`, `src/settings_menu.cpp`: XMB model, pages, actions. `src/host.cpp`: real-PC settings.
- `src/rsx_program.cpp`: RSX→GLSL translator for firmware Cg binaries (`--dump-cg`, `--decode-fp`).
- `src/assets.cpp`: RCO/QRC/GIM/DDS/TGA decoding, `bestImage`, fonts.
- Local analysis (not committed): `out/native-reference/` holds the decompiles (`lines/*.c`, `icons_vtable.c`), the
  translated shaders (`cg-icons/`), `qrc-inventory.txt` and `icons/` (ICONS.mnu files).
