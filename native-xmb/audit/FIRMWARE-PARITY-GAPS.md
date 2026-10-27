# FIRMWARE-PARITY GAPS - PS3 4.93 XMB vs native-xmb

Assessment only. No source file was modified for this document. Written 2026-10-26.
Scope: what the 4.93 firmware does in rendering, layout, motion, menus, text, behaviour and
audio, and what this C++/Qt recreation still lacks.

## Evidence classes used below

- **M** = measured or traced: read from a firmware binary/RCO/MNU, or captured from a real
  firmware run (RPCS3) and recorded with an address, constant or frame in the audits.
- **F** = fit or assumption: a curve, constant or geometry inferred from reference frames or
  from an earlier port, not traced to a firmware call.
- **R** = recorded run: the project's own audit trail claims a local run. Not re-executed for
  this document.

The register below does not carry a column of these tags; each row cites the audit or source line
that records the evidence, and any value that is a fit rather than a trace is named as such in the row
and gathered again in "What is measured and what is only fitted" (section 2.5).

Firmware modules, decrypted plugins, RCOs, RSX captures and reference frames stay local and are
not committed. In this checkout `out/native-reference/` (the stored RPCS3 reference frame that
`work/ab-layers.ts` scores against) is absent; the layer numbers quoted below are the recorded
baselines in `work/ab-layers.thresholds.json`. A fresh `--capture` run needs that reference
regenerated from RPCS3 or supplied with `--ref`.

## 1. What "firmware parity" means here, and the current score

Parity here does not mean "it looks like the XMB". It means every visible and behavioural element
is produced from the firmware's own data - the RSX programs and their MNU parameters, the RCO object
trees and XMBML documents, the paf widget semantics, the SPU simulations and the measured timings -
and is verified against a matched firmware state, with fits clearly separated from traces. The
recreation scores, honestly, **about 47%** of that target (range 40-50 depending on how strictly
"verified against a matched capture" is required). Weighted by surface (weights rendering 35%,
layout and motion 20%, menus and screens 20%, behaviours 15%, audio 10%): rendering 55% (background,
HDR/glare, ribbon and cold boot are firmware ports; icon material and the whole glyph pipeline are
reconstructions), layout and motion 70% (nearly all positions and both ease modes are measured, but
several are fits), menus and screens 35% (the columns and pages are hand-built approximations of
XMBML, and the Triangle options system is absent), behaviours 30% (notifications, background music,
list actions and the in-game overlay do not exist), audio 40% (four original cues and five theme
roles exist; there is no background music and no measured mixing). The score is capped well below
100% by things no PC can reproduce, listed in section 4.

## 2. Gap register

Size: S = bounded slice, days, evidence already local. M = needs one new capture or trace plus
code, one to two weeks. L = missing subsystem or unresolved firmware mechanism, weeks or more.

### 2.1 Rendering

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| R1 | Background gradient, monthly palettes, day curve | `lines.qrc` back_colours0, `BACKGROUND.mnu`, custom_render_plugin clock map | Ported: `src/view.cpp:319-336`, `:382-408` (backgroundTime); boot alpha `:613-615` | `_DayTime` curve is a cosine fit that reproduces the captured 1168.77 at 10:00 but is not traced (comment `src/view.cpp:386-395`). Native background still reads brighter than RPCS3 at the same hour (`audit/PIPELINE.md:342`). Closes with: RPCS3 frame at the pinned 10:00 clock plus an `XMB_FP_DUMP` of back_colours0; gate "top background" (20.24 dB / 0.9344) | M |
| R2 | HDR tone map and glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, `HDR.mnu` | Ported: `src/hdr.cpp:90-177`, presets day/night/coldboot1 | Boot composite is not reproduced: RPCS3 runs LinesController and its pre-exposure path with no ToneMap pass, so its boot frame is near black until ~2.2 s and ends darker (display 67 vs 83 native in the same region). Closes with: a boot capture of LinesController's pre-exposure LUTs plus a region-luma comparison (`audit/PIPELINE.md:230-233`) | M |
| R3 | Ribbon geometry, cloth, FFD, presets | custom_render_plugin cloth (PPU), `ffd_shader1`, `spline.elf` (SPU), `LINE1.mnu` | Ported end to end: `src/ribbon.cpp`, drawn at `src/view.cpp:336`; stages verified | Boot preset blend is not ported: the capture fits TIMESTEP 3.69 between LINE1's 4 and coldboot1's 2, and qgl_base's animation player is untraced (`audit/PIPELINE.md:118-120, 150-153`). gameboot/welcome/music presets are not scheduled because those flows do not exist | M |
| R4 | Particles pass 1: simulation, quads, material | `particles.elf` (SPU), `PARTICLES.mnu`, `particles_quads.vpo/.fpo` | Firmware-shaped pool now in `src/particles.cpp:10-49` (1536 cap, 48-byte stride, fixed 0.0088883 s step, Park-Miller seed), one indexed draw `:334-341` | Six firmware inputs are fitted or absent: emission volume (`:42-50`, no emitter asset exists), BROWNIAN per-step kick (`:31`), `_Color`/`_Gamma` values, spot-light packing, qgl material mode 3 blend (still additive, `:334`), and the fresnel/life-bounds/darkness alpha chain (replaced by `depthFade^2`, `:97`). `proc_iridescent.tga` is not bound; the tint is a cosine (`:121-124`). Closes with: `XMB_FP_SERIES` of the particle draw (ucode 1968) decoded by `--decode-fp`, plus a vertex-array capture for stride/emitter | L |
| R5 | Particles pass 2 (`particles_second`) | `particles_second.vpo/.fpo`, `_Glare` packing, drawn into the GlareSource chain | No second program. Its radial halo is folded into the same quad (`src/particles.cpp:125-128`) and the same dust is drawn again into the glare source through `Hdr::finish`'s `extraGlare` hook (`src/view.cpp:373-377`, `src/hdr.cpp:122-124`) | Real second program, its own geometry and `_Glare` uniforms; the folded weight is a fitted 0.045, not GLARE 0.159705. Closes with: `XMB_FP_SERIES` of particles_second plus a glare-source dump (`XMB_BG_SCENE` already exists for the scene side) | M |
| R6 | Particles spotlight / darkness / depth of field | `_LightPack[4]`, `_Darkness`, `_Focus`, `_FocusCurves`, `_NearControl`, `_LifeBounds*`, `_FrontFacingQuaternion` | Only a fixed key-light direction and the three size bands exist (`src/particles.cpp:68-97`) | Near/far darkness, the DoF focus curves, near-facing quaternion, life-bounds box. Closes with: captured vertex constants (vpo slots 459-467) matched against the preset values in `audit/PARTICLES.md:111-128` | L |
| R7 | Category icon material | `icons.qrc` quad.vpo, `icons_no_ss_no_shad.fpo`, `mask.fpo`, `ICONS.mnu`, `texenv.dds`, `amb.bmp`, `icontex` normals | Opaque reconstructed material is the normal path (`src/icons.cpp:100-119`, selected at `:153-155`); the firmware glass program runs only behind `XMB_ICON_FIRMWARE` / `XMB_ICON_UCODE` / `XMB_ICON_DEBUG` | Refraction, environment rotation, two specular lights, the luminance curve and `_Ambiant.w` on alpha are not in the normal path. Brightness tints RGB because that measured 0.85 dB better with an opaque surface (`audit/ITEM-ICONS.md:217-228`, source comment `src/icons.cpp:111-115`). Original's tint follows the monthly palette by a fitted 0.45 strength (`src/view.cpp:294-308`). Closes with: an RPCS3 open-column/page-open capture at a matched day clock plus 3x icon crops, scored as a new "icon interior" layer | L |
| R8 | Item icons: glass vs opaque | Same program; captured per-draw `_Eye` / `_Ambiant.w` (`audit/ITEM-ICONS.md:47-56`) | icontex item icons queue to the same opaque renderer (`src/view.cpp:928-931`); the CookieCutter background cut is skipped in the opaque branch (`src/icons.cpp:220`) | The refracted, env-lit look of a page column. Evidence that decides it: run the glass path against a matched RPCS3 page capture and compare icon crops; the alpha experiment already sets the bar at +0.85 dB to beat | M |
| R9 | Second item-icon size class (1.1022x) | Captured draws: 85.554 and, in a transition, 203.700 (`audit/ITEM-ICONS.md:72-75`, 91) | Absent: one size pair for every level and state (`src/view.cpp:277-279`) | Whether it is a level scale or a page-entry scale. Closes with: a scripted page-open RPCS3 capture with input timing, decoded with `--decode-fp`, per frame | M |
| R10 | Item-icon sizes at level > 0 and the above-focus offset | Not captured | Level ignored (`src/view.cpp:277-279`); the 95 px deeper-level above-focus offset is inferred (`src/view.cpp:891`) | One folder-page capture with rows above the selection; decode `_Eye.w` and `_Eye.y` per draw | S |
| R11 | Non-icontex item icons and game artwork | paf planes for RCO images; ICON0 as file artwork with its own RCO geometry | Qt painting with measured sizes 60/104, 40/56, 18/39 (`src/view.cpp:904`), fitted to the video | RCO-object-driven position/size, the firmware "PS" frame for game icons, and ICON0 plane geometry. Closes with: RCO object attributes for those images plus a matched capture | M |
| R12 | Text: hinting and metrics | `libfontFT.prx` `cellFontFT_65E27CB3 @ 0x6aa4` loads outlines with flags 10; paf 26.6 advances and kerning | Flags 10 matched (`src/text_renderer.cpp:106`, `src/font_raster.cpp`); shaping and glyph positions still come from `QTextLayout`/`QGlyphRun` (`src/text_renderer.cpp:168-171`) | One metric authority. Advances, elision and caret still derive from Qt's platform engine. Closes with: FreeType 26.6 advance sums for matched faces and a matched string-extent capture; gate "clock text" (13.51 / 0.6131) and "item label row" (11.94 / 0.3910) | L |
| R13 | Text: coverage gamma and overlap | Coverage copied with no curve; compare-and-keep-larger copy rule (`FUN_004056d8`, `FUN_004057a0`) | No gamma; alpha-over compositing `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` (`src/text_renderer.cpp:197`) | A single-string coverage mask with `GL_MAX`; the display-stage gamma is still UNKNOWN in firmware (`audit/TEXT-RASTER.md:135-158`), so any curve is a fit. Closes with: one overlapping-glyph crop A/B plus a captured pair of glyphs at the same phase | L |
| R14 | Text: glow / halo | No `Text` glow field in the RCO schema; no glow texture for explore; mechanism unknown (`audit/TEXT-RASTER.md:280-326`) | Gaussian halo sigma 2.2, radius 6 (`src/text_renderer.cpp:17`, `:115`), 3.4 s breathing from 0.35 (`src/view.cpp:956`), both from MaXMB console observations | Firmware mechanism and values. Closes with: a console capture resting on one row for 8 s or more, plus the paf property-animation call that drives it | L |
| R15 | Text: subpixel phase | 26.6 phases masked with 0x3f and rounded with a +0x3f bias; `FT_Set_Transform` operands UNKNOWN (`audit/TEXT-RASTER.md:102-133`) | Glyph origins snap to whole output pixels (`src/text_renderer.cpp:218-220`) | Whether the outline is translated by the phase (subpixel rendering) or only the blit origin rounds. Closes with: re-decompile of the transform stores, not a capture | S |
| R16 | Text: elision | Plugin-managed separate `label2_text_ellipsis` Text object positioned from the metric path (`explore_plugin_all.c` around 91929-92360) | `QFontMetrics::elidedText` substitutes U+2026 with platform metrics (`src/view.cpp:951`) | Separate ellipsis draw at the last fitting pen position, using firmware metrics. Closes with: long-label crops plus the recovered placement formula | M |
| R17 | Text: OSK cursor | No cursor object exists in any OSK RCO; caret is code-drawn by the OSK plugin (not decompiled) | Qt metric prefix width, 2x36 px, 500 ms blink (`src/view.cpp:1466-1468`) | Blink period, width, extent, colour and a metric shared with the glyphs. Closes with: an OSK-plugin trace or a console measurement | M |
| R18 | Text: per-style weight and slant | Weight/slant table at VA `0x68e0a8`-`0x68e0c4`, weight applied as `FT_Outline_Embolden` clamped to [-0.07, 0.04] | Not applied per style; Qt bold/italic only (`src/text_renderer.cpp:179-185`) | The style table and the slant channel (which would travel through the unproven transform matrix). Closes with: the same transform disassembly; weights alone are implementable now | M |
| R19 | UI composition, dim and glare on the UI layer | paf UI draw order relative to ToneMap/ApplyGlare not traced | UI layer is a 2x MSAA FBO composited over the finished HDR frame (`src/view.cpp:664-671`); category icons are drawn after HDR finish (`:664`); screensaver dim is a uniform over the whole frame (`:673-677`) | Whether firmware text/icons are inside the tone-mapped scene, and whether the firmware dims with a brightness curve rather than a black overlay. Closes with: an RSX capture of the UI draw order versus ApplyGlare, and the screensaver fade capture (`ISA.md:110`, ISC-45.3) | M |

### 2.2 Layout and motion

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| L1 | RCO/plugin-traced positions vs measured constants | `xmb_plugin_normal.rco` / `xmb_ingame.rco` `xmenu` (posX -395, posY 40), `XMList`, `PhXmBar` defaults, `objattribdef-ps3.ini` | `kColumnX = 565` (`src/view.h:168`), pitch 200, quads 117.13/167.33, rows 290/280, page shift 262 (`src/view.cpp:826-827`, `:890-899`) - 5 of 7 constants confirmed, 186 pitch and row y untraceable (`audit/LAYOUT-TRACE.md:40-48, 99-104`) | The 186 px faint-category pitch, the scene transform that puts the icon row at 290/280, the direct `index * pitch` trace, and level>0 geometry. Closes with: a page-open RPCS3 capture of category positions and a folder-page capture; then decode the bar's move calls | L |
| L2 | Page move duration and mode | paf `PhXmBar` in vsh.elf; a genuine 0.3 s bar constant at `DAT_0068f020` (`audit/PAGE-MOVE.md:93-101`) | `pageMilliseconds = 300`, mode 4 (`src/view.h:45`, `src/view.cpp:592`) | The animate call itself. 300 ms / mode 4 fits the 17-18 frame ramp but no traced call exists, the only 300 ms bar call shows mode 5 in the decompile, and explore_plugin's 300.0f constants are dialog durations, not the page move (`audit/PAGE-MOVE.md:62-81, 119-139`). Closes with: disassembly of the depth transition call site, then a native burst against an RPCS3 burst | M |
| L3 | Category cross-fade | explore_plugin content transitions; reference frames 14.4 s / 95 s | 200 ms mode 5 fade (`fadeEase_`, `src/view.cpp:591`) with the old list offset 150 px (`:975-976`) | Both the fade duration and the 150 px slide are measured from video, not traced. Closes with: an RPCS3 category-change burst analysed by `work/motion-track.ts`, or the transition call in explore_plugin | M |
| L4 | Held-key repeat and help text | paf `PhXmBar` key handler; measured on the real XMB | 300 ms then 66.7 ms, counted from the deadline (`src/view.cpp:1334-1339`); help text drawn on `column.selection` (`:964`); keyboard auto-repeat dropped (`:1250-1255`) | No configurable repeat (Accessory Settings has no Key Repeat Interval/Speed rows, `src/settings_menu.cpp:114-118`) and no acceleration model, which matches 4.93 but leaves the settings backend absent. Closes with: a settings page plus a repeat-interval capture | S |
| L5 | Information-panel clock | paf `PhClock` (passive, 8 fields, minute display); writer not found | Minute-resolution cache invalidation and a Qt analog clock at (1791,104) (`src/view.cpp:981-996`, `:988-995`) | The periodic writer that calls the paf setter, and the bar's paf projection/typography. Closes with: the setter caller in vshmain/vshcommon, or an infobar capture series; gate "info bar" (17.12 / 0.7046) and "clock text" (13.51 / 0.6131) | M |
| L6 | Safe area, overscan and non-1080p projections | Display Area Settings / video output modes | Layout is `min(pixels/1920, pixels/1080)` scaled and centred (`src/view.cpp:658-659`); icon projection re-centres a 1920-wide layout (`src/icons.cpp:156-157`) | Any display-area setting, 720p/4:3 projections, and independent text scaling. Only 1080p was ever captured (`audit/ITEM-ICONS.md:188-190`). Closes with: RPCS3 captures at 720p and with a changed display area | M |
| L7 | Depth, gap and parent-column constants | Captured `_Eye` / distance pairs (2700 and 2857.2) | 2700 + 157.2 x depth, 15 px gap closure, 110/262 shifts (`src/view.cpp:896-899`, `:930`) | 157.2 is arithmetic on two captured distances and the 110 shift implies 109.5 (`audit/ITEM-ICONS.md:100-102`). Closes with: a per-draw eye dump of a page-open sequence; the capture already exists locally in fpdump7 and should be re-read rather than re-shot | S |
| L8 | Wallpaper and Classic composition | Theme wallpaper mode; `BACKGROUND.mnu` classic path | Wallpaper is a host image drawn full-window with a 35/255 black wash (`src/view.cpp:781-787`); Classic hides particles (`:314`) | Firmware wallpaper compositing (scaling, dim, whether the wave stays) and what Classic actually changes. Closes with: console captures of both modes at a fixed theme | M |

### 2.3 Menus and screens

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| M1 | Category columns and items | 28 local `category_*.xml` XMBML files plus explore_plugin resource tables and selector/visibility rules | Ten columns and their items are hardcoded (`src/shell.cpp:569-605`); only Settings titles/details/icon names are projected from `category_sysconf.xml` (`:608-622`) | Every other column's items, ordering, selector visibility and resource text are hand-written. Closes with: one column generated from its XMBML view plus its RCO strings, compared row-by-row against a firmware capture | L |
| M2 | Submenu pages | explore_plugin page templates and XMBML views | Pages are built in code: settings pages, folder pages, Jellyfin and Steam folders (`src/shell.cpp`, `src/settings_menu.cpp:49-144`); depth capped at 32 (`src/settings_menu.cpp:50`) | A generic XMBML page renderer (columns, keys, icons, values) and the firmware's page-entry timing. Closes with: a non-settings firmware page (e.g. Trophy Collection or Game Data) reproduced from its XML and compared | L |
| M3 | Options panel (Triangle) | paf options panel used by explore_plugin for per-item actions; manual pages for sort/group/display-all/delete/copy | Only setting-choice columns render a right-side panel (`src/view.cpp:843-881`); no item-level Triangle menu exists anywhere (`src/shell.h:32` has no such action) | Panel open/close animation, dimming, focus transfer, cancel, and the actual item actions. Closes with: an RPCS3 capture of Triangle on a media list plus the RCO strings/positions of the panel | L |
| M4 | Information panel | `system_plugin.rco` infobar, `PhClock`, message/news/trophy indicators, `tex_notification_*` | Clock text and an analog clock only (`src/view.cpp:981-996`); the controller indicator exists only as the PS-tap HUD (`:1068-1076`) | Message, download, trophy and friend indicators, their layout and their state rules. Closes with: a console capture with a message/trophy present and the indicator RCO objects | M |
| M5 | Messages and dialogs | `system_plugin` msgdialog objects (`tex_msgdialog_bar`, `tex_cross`, `tex_circle`), animation table values 100/0 | Qt-painted dialog: dim, two rules, centred text, single OK hint (`src/view.cpp:1023-1035`) | Multi-button dialogs (confirm/cancel), heading/body split, entry/exit animation, and the error/info variants. Closes with: a firmware dialog capture with two buttons and the animation entries already dumped for system_plugin | M |
| M6 | OSK | `osk_plugin.rco`, `oskpanel_plugin.rco`, `oskfullkeypanel_plugin.rco`, `osk_plugin.prx` | Fixed US 10x4 grid plus two function rows, no prediction content, `Input Mode` inert (`src/view.cpp:1386-1394`, `:1482-1484`) | Language layouts, input modes, prediction, caret semantics; geometry is measured from frame 64 s only. Closes with: an OSK capture in a second language/input mode plus the OSK plugin trace | L |
| M7 | Settings pages | `sysconf_plugin.rco` page schemas and strings | 16 groups (`src/settings_menu.cpp:49-144`); 6 have no working setting and 10 of 12 System rows are unavailable (`:69-78`, `:127-141`); Music and Video are the same borrowed three-row page (`:94-98`) | Original row schemas for Music, Video, System, Network, Chat, Security, Remote Play, Printer; real values where a PC equivalent exists. Closes with: one page per group reproduced row-for-row and captured as a named state | L |
| M8 | PS system menu | Measured RPCS3 capture of a PS hold (`audit/PS-BUTTON.md`) | **Done**: black frame, clock, centred entries, controller slots, Enter/Back legend, Quit Game while a title runs (`src/view.cpp:1040-1067`, `src/shell.cpp` psMenu*) | Remaining: real controller slot/charge data, and Quit Game covers only titles this app launched (Steam handoff is watched through the registry) | S |
| M9 | Cold-boot intro | coldboot.raf, coldboot.jsx, raf.qrc vert0/sce01, anim_coldboot2 events, coldboot_stereo.ac3 | Ported (`src/coldboot.cpp`, `src/view.cpp:606-623`) with script timeline, sound and background boot presets | Epilepsy notice after ShowGUI, the SD actor branch (HD only, `src/coldboot.cpp:277`), the RPCS3 pre-exposure brightness, and the gameboot sequence | M |
| M10 | Photo / music / video overlays | photoviewer, videoplayer, audioplayer RCO panels | Custom host-player overlay: title, queue summary, 4 px progress bar, time, keyboard hints (`src/view.cpp:788-812`), fade model (`src/media_controls.cpp`) | The firmware panels, thumbnails, EXIF, subtitle/track selectors, effect transitions and reserved-position resume UI. Closes with: panel captures plus `ISC-46.6` (`ISA.md:118`) | L |

### 2.4 Behaviours

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| B1 | Triangle options-menu semantics | explore_plugin per-item options; manual "Using the control panel" pages | Not present. Triangle/Y is bound only to media repeat, photo rotate and OSK space (`src/view.cpp:1273-1291`, `:1358-1373`) | The menu model itself (open, focus, cancel, action dispatch). Closes with: a firmware Triangle capture on a list plus the RCO option strings/order | L |
| B2 | Sort, group, display-all, delete, copy | explore_plugin list actions; firmware XMBML action names; manual pages for copy/delete | No action exists (`src/shell.h:32` Action enum; no sort/group/delete/copy code) | Every list action, its confirmation, progress and refresh. Closes with: one action end-to-end (a safe one, e.g. display mode or copy to a scratch folder) against a firmware capture | L |
| B3 | Background music | `music_1`/`welcome_1` background presets, audioplayer, `PARTICLES_SPE` music variant | None. Back stops playback and clears the queue (`src/shell.cpp`, `Shell::back`); no music category icon animation; the music_1 background preset is never scheduled | Any music-while-browsing model, its mini/now-playing surface, the music category icon change and the music background preset. Closes with: a scripted run that keeps audio playing while navigating, plus the music_1 preset values from `PARTICLES.mnu`/`LINE1.mnu` | L |
| B4 | XMB notifications | `system_plugin` `tex_notification_*` objects, notification sound, overlap policy | Not present: no notification string or draw path exists in `src/` | Entry/hold/exit animation, sound, stacking and suppression during playback. Closes with: a trophy or message notification capture plus the RCO objects; then a scripted `notify` state scored as its own crop | M |
| B5 | Auto-off and idle | Power Save semantics; screensaver LUT | Implemented: idle quit (`src/shell.cpp:774-787`), display-awake hold while playing/launching (`src/host.cpp:186`), screensaver dim over the composed frame (`src/view.cpp:673-677`) | Firmware brightness curve, fade timing and wake behaviour are unmeasured, so ISC-45.3 stays open (`ISA.md:110`) | S |
| B6 | Theme (.p3t) coverage | theme RCO/p3t structure: infotable, icontable, bgimagetable, setable, RAF scene, font/colour selections | Still background, icontable and five sound roles (`src/p3t.h:35-38`, `src/shell.cpp:1175-1188`); dynamic RAF scenes explicitly not drawn (`src/p3t.h:13`) | Dynamic theme scenes, the `font` and `color` selections, theme install/list/delete and preview. Closes with: one dynamic theme's RAF played (coldboot.cpp already plays a RAF) and a font/colour theme applied | L |
| B7 | Host settings backends | PS3 settings that map to a PC (display, audio, input) | Real: Windows master volume/mute (`src/host.cpp:152-181`), screen/adapter reports (`:44-56, 90-149`), display-awake hold; display/fullscreen preference | Input configuration (repeat interval/speed, keyboard type, controller assignment), audio device selection, output resolution/refresh, RGB range, deep colour. Closes with: one real backend (e.g. repeat interval) plus a wired settings row and a capture | M |
| B8 | Game and media launch plus return | PS3 launch/quit lifecycle; `xmb_ingame` for the in-game XMB | Tracked `QProcess` with exit report, rescan, focus restore, idle suppression (`src/shell.cpp:134-169`); Steam handoff watched through Steam's registry (`:176-207`); Quit Game in the PS menu | A configured backend preference (emulator path/args, Steam path), in-game XMB/overlay, exclusive-display handoff, and a recorded launch-return capture. Closes with: a backend settings row plus a scripted launch/return state | M |
| B9 | Users and profiles | PS3 user login, per-user settings, delete/rename/avatar | Create/select only, with global preferences and library paths (`src/profiles.h`, `src/shell.cpp`); active user shown only under Trophy/saved-data rows | Rename, delete, avatar, logout, per-user preferences, startup user selection. Closes with: per-profile storage plus a startup selection state | L |
| B10 | PSN and online surfaces | Sony services (sign-in, Store, trophies, What's New, messages) | Honest placeholders: `Action::Unavailable` rows and firmware empty-state strings (`src/shell.cpp:595-604`, `src/settings_menu.cpp:71-78`) | Nothing should be fabricated; this stays a documented unavailability (section 4) | - |

### 2.5 Audio

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| A1 | Interaction cue set and mapping | `system_plugin.rco` sound resources (VAGp v6, 48 kHz stereo ADPCM) | Four cues decoded, cached and played (`src/interaction_audio.cpp:32`, `:300-309`); Move has a 45 ms debounce (`:329`); cues dispatched at `src/shell.cpp:817-832`, `:985` | The original error cue decodes as silence and is not substituted; the action-to-cue matrix is inferred, not measured; audibility/phase never verified. Closes with: a console/loopback recording per action plus a log-to-cue matrix | S |
| A2 | Theme sound roles | p3t `setable`; MaXMB five-role mapping | Five roles (`se_cursor`, `se_decide`, `se_cancel`, `se_optionmenu`, `se_system_ok`) map to six cue slots, Accept falling back to Confirm (`src/interaction_audio.cpp:300-309`, `:271-273`) | Whether the firmware's role set is larger, per-role volume, and mixing with the system cues. Closes with: a theme with all sounds decoded and a role-by-role A/B on a console capture | S |
| A3 | Background music (BGM) | XMB background music (music_1 background preset; audio player) | None | A music source, crossfade with cues, ducking, and persistence while navigating. Closes with: any BGM implementation plus a log/loopback assertion; ab-layers cannot score audio | L |
| A4 | Mixing, volume law and A/V phase | PS3 audio output config, key-tone volume, firmware mix bus | Linear percentage into `QAudioOutput` and the interaction sink (`src/shell.cpp:1169-1171`); no bus, no limiter, no A/V phase measurement | The firmware volume curve, mixing of cues with media, and cue-to-photon alignment. Closes with: a loopback recording at several volumes plus a frame-timestamped cue capture | M |

### What is measured and what is only fitted

The register rows cite the audit that records each value; fits are called out in the row text, and
are collected here because the difference between parity and resemblance is exactly this list:

- Fitted: the page-move 300 ms / mode 4 pair (L2); the category cross-fade 150 px offset (L3); the
  186 px faint-category pitch and the 290/280 icon-row scene transform (L1); the 95 px deeper-level
  above-focus offset (R10); the 1.1022x size class's absence (R9); time-of-day `_DayTime` curve (R1);
  particle emission volume, Brownian kick and depth fade (R4); particle glare weight (R5); the opaque
  icon material's lighting, bevel and mix (R7); Original's 0.45 tint strength (R7); halo sigma/radius
  and breathing (R14); the infobar rectangle and the Qt analog clock (L5); OSK and dialog geometry
  (M5, M6); the media overlay layout (M10); the wallpaper dim wash (L8).
- Traced or measured: background/ribbon/HDR programs and presets; icon quad sizes, pitches, row
  positions, brightnesses, camera distances; focus column x; page-open shift 262; both paf ease modes
  and the 200 ms durations; held-repeat 300/66.7 ms; help-text timing; the four system cues and the
  five theme sound roles; the PS menu layout; cold-boot timeline and sound length.

## 3. Ranked next ten: smallest honest slice, and the evidence it produces

Ordered by fidelity gained per unit of work, using only what the existing scripted-capture harness
(`--script` tokens + `work/ab-layers.ts`) can score. Current canonical-capture numbers, for
reference: frame 18.02 dB / 0.8301 SSIM, top background 20.24 / 0.9344, category icon row
21.05 / 0.8950, item label row 11.94 / 0.3910, clock text 13.51 / 0.6131, info bar 17.12 / 0.7046.

1. **Text coverage compositing (R13), rendering.** Smallest slice: draw one string into an offscreen
   coverage mask with `glBlendEquation(GL_MAX)` and composite once, behind an env flag, changing no
   layout. Evidence: `bun work/ab-layers.ts --capture` produces a direct delta on "item label row"
   and "clock text"; `bun work/ab-compare.ts <ref> <native> 400,425,800,100` isolates one label.
   Expected information: whether max-coverage alone moves the label layer, independent of metrics.
2. **Item-icon reference crops (R8, R9, R10), rendering.** Smallest slice: add two layers ("icon
   interior", "icon edge") and two named states (open Game column; a folder page with rows above the
   selection) to `work/ab-layers.thresholds.json`, then capture. Evidence: fresh numbers for the
   surfaces that currently have no gate; the same states give the level>0 and 1.1022x evidence the
   firmware captures already contain (`fpdump7`) once re-decoded with `--decode-fp`.
3. **Particles second pass and iridescent texture (R5, R4), rendering.** Smallest slice: bind
   `proc_iridescent.tga` as the fragment input and draw the real `particles_second` program into the
   glare source through the existing `Hdr::finish` hook (`src/view.cpp:373-377`). Evidence:
   deterministic `--render-clock 30` captures, gating "top background" plus a new "particle band"
   crop; `XMB_BG_SCENE` shows the pre-tone-map scene so the two passes can be judged separately.
4. **Triangle options panel for one surface (B1, M3), menus.** Smallest slice: one item-level panel
   opened by Triangle on the Game column, with two real actions (Refresh, Information), reusing the
   existing panel geometry. Evidence: a new named state `game-options` captured by script
   (`... ,grab=`) with the panel crop scored as its own layer; the log records the open/close.
5. **XMB notification composite (B4), behaviours.** Smallest slice: draw one static notification from
   the real `system_plugin` assets at a scripted moment, with no queue or sound yet. Evidence: a
   `notify` named state plus a top-right crop layer; the existing "info bar" crop shows whether the
   notification placement collides with the clock.
6. **Page-move trace, then re-time (L2), motion.** Smallest slice: no native change first - disassemble
   the depth transition call site to settle mode 4 versus mode 5. Evidence: pixel scoring cannot prove a
   duration, so the proof is a `--script "burst=<prefix>@ENTER"` burst (12 frames at 12 ms) against an
   RPCS3 burst, plus `XMB_MOTION_LOG=1` for the native 18-frame settle; once re-timed, a new
   `page-open` named state gates the settled position through `ab-layers`.
7. **Layout trace for the open-page state (L1, L7), layout.** Smallest slice: re-read the existing
   captured eye/vertex dumps for a page-open sequence and derive the 186 pitch and the row-y transform
   instead of re-shooting. Evidence: replaces two fitted constants in `src/view.cpp:826-827` and
   gates through "category icon row" (21.05 / 0.8950) after a deliberate baseline refresh.
8. **One XMBML-driven column (M1, M2), menus.** Smallest slice: generate the Network column's rows
   from `category_network.xml` plus its resource strings, keeping the current icons. Evidence: the
   named-state `expect` block (row ids/titles) already used for the Steam states, plus a frame capture
   compared with an RPCS3 Network column.
9. **Settings schema for one group (M7, B7), menus/behaviours.** Smallest slice: rebuild Video
   Settings with the firmware's BD/DVD row set (Unavailable where honest) and wire one real backend
   (e.g. key-repeat interval under Accessory Settings). Evidence: a `settings-video` named state with
   row assertions; the text renderer already draws the labels so the capture also exercises R12/R16.
10. **Info-panel clock writer and bar projection (L5), layout/motion.** Smallest slice: find the paf
    setter caller (PAGE-MOVE's suggested vshmain/vshcommon and `cellRtc` search), then replace the
    measured rectangle with the traced projection. Evidence: "info bar" (17.12 / 0.7046) and "clock
    text" (13.51 / 0.6131) both gate the change; the minute-boundary state can be pinned with
    `--ui-clock`, so the capture is deterministic.

Not in the top ten because the evidence loop is longer or the surface is smaller: BGM (A3), dynamic
theme RAF (B6), in-game overlay (B8), OSK languages (M6), 720p/overscan (L6), elision/caret metric
authority (R16, R17) - the last is cheap to code but needs the PAF metric trace first, and should ride
along with item 1.

## 4. Cannot be closed

These are not laziness or missing reference data; a PC reproduction cannot honestly reproduce them.

1. **Console power state.** "Turn Off the System" on the PS3 removes power; here it closes the
   application and says so (`audit/PS-BUTTON.md:46-51`). Suspending or shutting down the PC is a
   different, destructive operation.
2. **Controller hardware ownership.** Turning a pad off, Bluetooth pairing, vibration and motion
   calibration are owned by Windows/HID drivers; "Turn Off the Controller" can only explain.
   The pad HID path is also untested without hardware (`HANDOFF-PHASES.md:42`).
3. **PSN and everything behind it.** Sign-in, Store purchase/download, trophy sync, What's New,
   messages, friends presence via Sony, Remote Play, cloud saves. The firmware-dependent services are
   disconnected; the ISA forbids fabricating success (`ISA.md:27`).
4. **RSX/GCM bit-exactness.** Exact hardware microcode behaviour - fp16 rounding of H registers,
   fixed-point clamps, RSX alpha-test and blend quirks - can be approximated but not proven identical
   on a desktop GL driver. Parity here is visual, within a measured tolerance, not bit-exact reference
   output.
5. **Cell/SPU timing effects.** Frame-rate-coupled simulation (RPCS3's own boot stalls shift the
   cold-boot timeline by 20-40 ms) and the fixed 60 Hz scheduling of the cloth and particles cannot be
   reproduced as a wall-clock identity on a variable-rate host; only the trajectory can.
6. **Disc and protected media.** BD/DVD playback with AACS, BD-J menus, 1080p24 and 50 Hz output
   modes, deep colour, RGB range and cross-colour reduction are either licensed hardware paths or
   display-pipeline properties the application does not own.
7. **Destructive system utilities.** Format Utility, Restore PS3 System, Backup/Restore, Install Other
   OS and Data Transfer map to operations that would damage the host; they must stay explained
   refusals.
8. **In-game XMB over a real PS3 title.** `xmb_ingame.rco`/`xmb_ingame.prx` run inside the firmware
   over a running game; a native recreation can offer Quit Game and a window-level overlay, not the
   firmware's in-game XMB.
9. **Console identity and account.** Console ID, PSN account binding, activation, and per-console
   saved-data/trophy encryption have no honest PC equivalent.
10. **The firmware's own update path.** System Update against Sony's servers; the native app can only
    update itself.

## 5. Stale documentation found (source contradicts it today)

Each item was checked against the current source, not against another doc.

1. **Particles row, `audit/PIPELINE.md:18`.** Still says the layer is "Replaced: linkev/PlayStation-3-XMB's
   sparkle layer ... additive point sprites ... not a firmware port". Contradicted: `src/particles.cpp`
   is a firmware-shaped pool (48-byte records, 1536 cap, PARTICLES.mnu constants, fixed 0.0088883 s
   step, VBO + index draw), drawn into the HDR scene and again into the glare source
   (`src/view.cpp:372-377`). The row was accurate before 2026-10-26.
2. **`audit/PARTICLES.md:395-421` and `:579-586`.** "Today's `src/particles.cpp` is 2000 additive point
   sprites with no simulation" and the 18-row gap table describing no lifetime, no forces, one pass.
   Superseded by the appended "Applied"/"Tuned" sections at `:587-623` and by the source; the body text
   still reads as current.
3. **Item icons, `audit/PIPELINE.md:339-340`.** "Item icons in the open column still go through Qt."
   Already flagged as contradicted in `audit/ITEM-ICONS.md:106`; the code queues icontex items to
   `IconRenderer` (`src/view.cpp:928-931`), only RCO/artwork/theme images stay Qt.
4. **Labels "still Qt", `audit/PIPELINE.md:22` and `HANDOFF-PHASES.md:37`.** Text now renders through
   the GL glyph atlas with FreeType coverage (`src/text_renderer.cpp`, called at `src/view.cpp:768`);
   only shaping, elision and caret metrics remain Qt. "Qt drawing" is stale; "Qt shaping plus measured
   metrics" is accurate.
5. **The Jellyfin sentence, `HANDOFF-PHASES.md:57`.** "Jellyfin is currently a URL app, not a native
   player." Contradicted by `src/jellyfin_client.cpp`, `src/jellyfin_stream.cpp`, `src/jellyfin_shell.cpp`,
   the `Action::Jellyfin*` routes in `src/shell.cpp`, ISA criteria ISC-41.1 and ISC-46 through 46.5
   marked done (`ISA.md:97, 112-117`), and the same file's own status table at `:23` and `:45`.
6. **Page-move timing.** The claim is honestly hedged in `HANDOFF-PHASES.md:39` ("fitted from RPCS3,
   not traced") and in `audit/PAGE-MOVE.md`, but two places still overstate or misattribute it.
   (a) `HANDOFF-PHASES.md:199-201` files it under "Done 2026-10-25" while the same sentence calls it a
   fit - a "Done" label on an untraced constant. (b) `audit/PIPELINE.md:302-303` says "explore_plugin
   holds several 300 ms float constants" in the context of the page move; `audit/PAGE-MOVE.md:62-81`
   proves those constants are file-delete dialog, `user.info` dialog and list-label durations, and
   `:99-101` shows the only 300 ms bar call passing mode 5 in the decompile. The authoritative
   statement is: fitted 300 ms / mode 4, not traced, and the explore_plugin constants cannot be cited.
7. **Native timing block, `audit/VISUAL-BEHAVIOR.md:139-146`.** Claims `1 - exp(-18 dt)` smoothing,
   repeat "300 ms then every 100 ms", keyboard auto-repeat following host events, and a 16 ms Qt repaint
   timer. Contradicted: `View::PafEase` mode 5 at 200 ms (`src/view.cpp:590-593`), repeat 300 ms then
   67 ms (`:1336`), arrow auto-repeats dropped and normalized (`:1250-1255`), repaint on swap callbacks
   (`audit/ANIMATION-SMOOTHNESS.md:3-9`).
8. **"Not implemented" rows in `audit/VISUAL-BEHAVIOR.md:105-113`.** VIS-35 context menu: a settings-choice
   options panel now exists (`src/view.cpp:843-881`), though item-level Triangle menus do not.
   VIS-36 player control panel: fade/hide controls now exist (`src/media_controls.cpp`,
   `src/view.cpp:800`). VIS-37 dialogs: a styled dialog with rules and button hints exists
   (`src/view.cpp:1023-1035`). VIS-38 information panel: clock and PS-tap HUD exist (`:981-996`,
   `:1068-1076`). VIS-43 startup/shutdown/login: cold boot is ported (`src/coldboot.cpp`) and the PS
   menu covers system off. VIS-39 notifications and VIS-42 artwork previews remain genuinely absent.
9. **Launch/return lifecycle, `audit/FEATURE-COVERAGE.md:481-501`.** "Return lifecycle: entirely absent
   ... no `QProcess` object is constructed anywhere in `src/`" and "idle auto-close keeps running while
   a game is up". Contradicted by `src/shell.cpp:134-207` (tracked `QProcess`, exit report, rescan,
   Steam registry watch, idle suppression). The document's own trailing "Update" at `:649-655` says so,
   but the body text still reads as current.
10. **Media metadata, `audit/FEATURE-COVERAGE.md:363-367`.** "Metadata: absent ... no artist, album ...
    is read anywhere in `src/`." Now title/artist/album are read from the player at `LoadedMedia`
    (`src/shell.cpp:761-765`) for the now-playing line. Library rows still have no tags, so the
    criterion verdict is unchanged but the claim is stale.
11. **`audit/FEATURE-CATALOG.md` statuses.** Already noted as lagging in `FEATURE-COVERAGE.md:549-558`;
    still wrong today, e.g. XMB-109 (Screensaver) is listed "Not implemented" while five options and a
    native dim exist (`src/settings_menu.cpp:108`, `src/view.cpp:673-677`), and the catalogue has no
    entries at all for the native Steam, Jellyfin, import-registry or PS-menu surfaces.
12. **Line-number drift in `audit/TEXT-RASTER.md`.** Elision is cited at `view.cpp:888` (now `:951`),
    the cursor at `view.cpp:1215-1217` (now `:1466-1468`), and "three Qt font sites" no longer covers
    the renderer (`src/text_renderer.cpp:179-185`). The analysis stands; the citations do not.
13. **`ISA.md` criteria state.** ISC-20 still unchecked (`ISA.md:73`) although a tracked launch/return
    path exists, and ISC-6/10/11/12/30 remain open while their sub-behaviours are partly implemented -
    the criteria are broader than the code, but the unchecked list no longer maps one-to-one to
    "absent". Not a false claim, but it will mislead a reader who uses the checklist as a gap list.

Note on sources checked and found current, not stale: `audit/ITEM-ICONS.md` (including its measured
brightness-on-alpha experiment), `audit/CATEGORY-ICON-FIX.md`, `audit/PS-BUTTON.md`,
`audit/MEDIA-CONTROLS.md`, `audit/INFO-BAR.md`, `audit/FONT-MATCHING.md`, `audit/PAGE-MOVE.md`,
`audit/LAYOUT-TRACE.md` and the ribbon/cold-boot sections of `audit/PIPELINE.md`.

## 6. Limits of this assessment

- Source reading plus the recorded measurements in the audits. No build, no window, no RPCS3 run and
  no capture was performed here; every runtime claim is cited to the audit that records the run and is
  therefore of the recorded-run class above.
- The layer scores quoted are the recorded baselines. They gate regressions and are not firmware
  fidelity thresholds; the "item label row" gate at 11.94 dB / 0.3910 in particular is a
  content-difference gate, not a pass.
- Firmware-derived files stay local; only addresses, constants, values and conclusions appear here.
