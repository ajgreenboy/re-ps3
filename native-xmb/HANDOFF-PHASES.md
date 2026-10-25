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
| Background, HDR glare | Firmware Cg programs (RSX→GLSL) with traced inputs |
| Ribbon | Ported end to end (2026-10-25): plugin cloth, ffd_shader1 lattice, spline.elf FFD/camera/bicubic sheet, each stage matched to RPCS3 captures; LINE1 presets follow the plugin's clock keyframes and boot schedule (audit/PIPELINE.md "Ribbon port", "Ribbon presets") |
| Particles | linkev/PlayStation-3-XMB's sparkle layer (point sprites, closed-form motion; not a firmware port); the boot's GLOBAL ALPHA fades them in |
| Category icons | Original uses firmware glass; fixed colours use an opaque normal-map material to resolve dark interiors. See audit/CATEGORY-ICON-FIX.md; full parity open |
| Item icons, labels, columns, dialogs, info bar, options panel, OSK | Qt drawing with firmware RCO textures and strings; layout, sizes, opacity and timing measured from the reference |
| Text | Original font faces rasterized with FreeType and recovered load flags 10 into the 2x GL atlas; label glow/idle fade are MaXMB observations; PAF metrics, pixel phase and full parity remain open (audit/FONT-MATCHING.md) |
| Motion | One measured ease (25%/frame at 60 fps, τ 58 ms); 150 px column cross-fade on category moves |
| Cold boot | 4.93 intro ported: coldboot.raf scene and script, boot sound, anim_coldboot2 events, background boot presets; skippable, `--no-intro` (audit/PIPELINE.md "Cold boot") |
| Themes | `.p3t` still background, icons and five menu-sound roles; RAF scenes and font/colour open |
| Input | Keyboard, XInput, and DualShock 3/4 / DualSense over HID (`src/pad.cpp`; HID untested without a controller) |
| Installed titles | dev_hdd0 games and games.yml disc copies start in the RPCS3 next to dev_hdd0 (`--no-gui`) |
| Host settings | System Information, network list, video/audio output and master volume read and write the real PC |

## In flight (2026-10-25)

- Third-party code (MaXMB, DeckyXMB, linkev/PlayStation-3-XMB) is credited in `THIRD-PARTY.md`.

## Phases to 100%

### Phase 1: category icon renderer (done for category icons; item icons in the open column still Qt, see below)
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
- Latest continuation: `audit/FONT-MATCHING.md`. Matched original faces now use FreeType with recovered
  NO_HINTING|NO_BITMAP flags; source identity is checked by head+cmap tables. PAF metric/pixel parity remains open.
- Active from 2026-10-25 at the user's direction. See `audit/TEXT-RENDERER.md` for the implementation, recovered
  PAF coverage/style behavior, RCO Text offsets, local evidence paths and remaining uncertainties.
- GL atlas now handles menu/subtitle/dialog/info/OSK text, with batched glyphs and separate coverage/halo.
  Native size snaps at the viewport scale; resizing clears the caches. Qt shaping preserves fallback and bidi.
  This is the renderer foundation, not a completed port of the PS3 rasterizer.
- Corrected the visible dark duplicate behind labels/clock: a restrained provisional shadow replaces unconverted
  RCO template offsets, and whole-string shadows render before foreground glyphs. See audit/TEXT-RENDERER.md.
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
- **Resolved 2026-10-25: the "8x night brightness" was a clock-matching error, not a rendering bug.** The earlier
  comparison used `--background-clock 00:00` (midnight) against an RPCS3 capture whose console clock was actually
  **~10:00 daytime**. Dumping the real draw constants with RPCS3's `XMB_FP_DUMP` (`RSXThread.cpp` local patch) shows
  `back_colours0` receives `_NightDayBlend=1.0` (full day), `_DayTime≈1168` (near noon), `_NightTime=2200` at draw time —
  i.e. the console was in daytime, while the native was pinned to midnight. The native clock mapping itself is correct:
  `_NightTime=fmod(t+0.5,1)*2400` reproduces the firmware's `2200` at `t=0.4167` (10:00). The native day background at
  10:00 measures top-band luma ~102 (bright gold day), as expected. Remaining (separate, smaller) item: the native
  `_DayTime` curve is the fitted approximation (`~1125` vs firmware `~1169` at 10:00, ~4%), already noted as fitted in
  `VIDEO-REFERENCES.md`; the day path otherwise matches. No night-brightness correction is warranted.

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
- Done 2026-10-25: the menu ease is paf's own. The cross bar is vsh.elf's `PhXmBar`; its focus moves are 200 ms
  paf animations in acceleration mode 5, a per-frame ease with k = 0.2206 that lands on frame 12
  (audit/PIPELINE.md "Menu motion"; `View::PafEase`).
- Done 2026-10-25, measured on the real XMB in RPCS3: the held-key repeat is 300 ms to the first repeat, then every
  4 frames (66.7 ms), with no acceleration. The help text has no delay: it is drawn on the selected row from the frame
  the move starts. The category move fits the same 200 ms mode-5 ease. All three are implemented
  (audit/PIPELINE.md "Measured on the real XMB").
- Open: page move (into or out of a column) durations, still assumed 200 ms; info-bar clock updates.

### Phase 5: remaining firmware renderers and states
- Ribbon geometry from spline.elf is done. Next: particles from particles.elf exactly, including the wind response.
  The same tools apply: `work/spu/` disassembler, the RPCS3 SPU DMA log, buffer watch and local-store snapshots
  (`rpcs3-rsx-capture.patch`).
- Ribbon presets: the measured boot durations (3.82 s, 2.8 s) differ from the plugin's call arguments (3 s, 7.5 s);
  qgl_base's animation player is not traced.
- Boot: the cold-boot intro is ported (`src/coldboot.cpp`, audit/PIPELINE.md "Cold boot"): coldboot.raf decoded
  (RAF container, Edge plane, GTF textures, coldboot.jsx VSMX), drawn with raf.qrc vert0/sce01, the script's timeline,
  coldboot_stereo.ac3 and anim_coldboot2's events (BootBG2 0 s, NormalBG2 4 s, ShowGUI 5.5 s, Finished 10 s), checked
  against an RPCS3 capture. The logo is the RAF's PS symbol + "PS3" + "PlayStation 3"; the RCO's tex_ps3logo/tex_scelogo
  stay at alpha 0. Open: the epilepsy notice after ShowGUI, the SD actors, the boot brightness curve of RPCS3's
  LinesController/pre-exposure path, and the "gameboot" sequence when launching.
- Theme system: other colour themes, wallpaper mode as the firmware composites it. `.p3t` themes: still background,
  icons and menu sounds are in (README "PS3 themes", audit/MAXMB-INTEGRATION.md). Sounds use MaXMB's five-role
  mapping and per-role system fallback. Still open: dynamic RAF scenes and the `font` / `color` selections.
- Screen states: screensaver/dim, Power off, user login screen, notifications (tex_notification_*), the in-game
  XMB (xmb_ingame).

### Phase 6: content and features
- Remaining XMBML categories and items that are placeholders ("Unavailable"), driven by the firmware XML.
- Media playback UI (photo/music/video controls from photoviewer/videoplayer/audioplayer RCOs).
- Folder playback queues, repeat modes, shuffle, bounded failed-file skipping and photo zoom/pan/reset are now
  implemented and observed in local replays (audit/MEDIA-CONTINUATION.md). Original player panels and broader
  library/playlist semantics remain open.
- PSN-dependent indicators stay off unless a real state exists; don't fake online features.

### Phase 7: verification harness
- Automated A/B: render named states with `--script` and compare them with frames extracted from the reference
  (PSNR/SSIM on crops). Add a threshold per layer so regressions are visible.
- **Tooling landed (2026-10-25):** `--render-clock <seconds>` freezes the background/ribbon/particles/icon animation
  time (so two scripted runs are bit-near identical, PSNR ~81 dB run-to-run), and `bun work/ab-compare.ts <ref.png>
  <native.png> [crop x,y,w,h]` prints PSNR/SSIM (whole frame or a crop). `bun work/ab-shot.ts <ref.png> <tokens> [--crop
  x,y,w,h] [--flash dir] [--exe path] [--bg-clock ISO] [--render-clock sec]` runs the native to a named state and scores
  it in one command. First settled-root baseline against a live RPCS3 capture at the matched day clock: whole frame
  **18.20 dB / 0.8388 SSIM**, category-icon row **20.13 dB / 0.9022**, top background **19.03 dB / 0.9248**. These are
  starting numbers, not thresholds yet.
- A second reference: capture the same states from RPCS3 running the real XMB (same firmware) at 1080p. This is a
  pixel-exact target with no video compression.
- Include a clip resting on one row for 8 s or more: it settles the breathing label glow and idle label fade, which
  are taken from MaXMB and not yet confirmed on our references (audit/TEXT-RENDERER.md).
- Recommended as the next phase: every other phase then gets a pass/fail number.

## Useful entry points

- `src/view.cpp`: scene, layout, UI layer (MSAA FBO), background/ribbon/HDR/particles orchestration.
- `src/shell.cpp`, `src/settings_menu.cpp`: XMB model, pages, actions. `src/host.cpp`: real-PC settings.
- `src/rsx_program.cpp`: RSX→GLSL translator for firmware Cg binaries (`--dump-cg`, `--decode-fp`).
- `src/assets.cpp`: RCO/QRC/GIM/DDS/TGA decoding, `bestImage`, fonts.
- Local analysis (not committed): `out/native-reference/` holds the decompiles (`lines/*.c`, `icons_vtable.c`), the
  translated shaders (`cg-icons/`), `qrc-inventory.txt` and `icons/` (ICONS.mnu files).
