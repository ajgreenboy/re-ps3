# Photo Gallery (hakoniwa) - research and design (plan item F4)

Status 2026-11-08: research and design only. No source was changed. Evidence labels: **TRACED** = read directly
from a firmware file or dump; **measured** = sizes and counts taken from exported files; **INFERRED** = a
conclusion drawn from names and strings, not followed through code.

Scratch dumps used below (all regenerable with the commands in section 6): `C:\dev\out\f4\`
(`hknw_plugin.txt`, `hknw_plugin-text.txt`, `photoviewer_plugin*.txt`, `rco-images.txt`, `hknw_strings.txt`,
`img\*.png`).

---

## 1. What the firmware app is

### 1.1 Module, row and launch

| Fact | Evidence | Label |
|---|---|---|
| The Photo column row is `seg_hakoniwa`, a `Query` on `sel://localhost/hakoniwa?category_photo.xml#seg_hakoniwa`, whose view holds one `type:x-xmb/xmlhakoniwa` item. The shop variant swaps it for `seg_hakoniwa_dummy` (`hakoniwa.png` icon, `msg_photo_gallery`, `explore_plugin` `NotifyErrorNoExecute`). | `dev_flash/vsh/resource/explore/xmb/category_photo.xml` | TRACED |
| The app is **`hknw_plugin`**: `vsh/module/hknw_plugin.sprx` (1,526,801 B; decrypted `fw493/vsh_module/hknw_plugin.prx` 3,929,320 B, PPC64 ELF), `vsh/resource/hknw_plugin.rco` (1,185,240 B) and the loose folder `vsh/resource/hknw_plugin/`. "hakoniwa" (box garden) is the internal name; `msg_photo_gallery` = "Photo Gallery". | file listing; `hknw_plugin-text.txt` | TRACED |
| The XMB row looks as in the reference frame `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\col2-photo-r01.png` (the `hakoniwa.png` collage, label "Photo Gallery"). The native row already draws this. | frame; `src/shell.cpp:433`, `:512` | TRACED |
| Nothing inside the app was ever captured. The rig's VFS holds no photos (`INVENTORY.md`, section on content-bearing surfaces), and no frame under `rpcs3-allgraphics-*` shows the gallery itself. | `INVENTORY.md` grep | measured |

### 1.2 The gallery needs a downloaded package

The module carries the URL
`http://hakoniwa.dl.playstation.net/download/hakoniwa/IP9100-NPIA00006_00-VSHMODULE0000029.pkg`, the content id
`IP9100-NPIA00006_00-VSHMODULE0000029`, an `autoDownloadFlag=` key, a `hknwpage_pkginstall` RCO page, an
`install_panel`, and the strings `msg_need_app_software_download_ask` ("To use this feature, you must first download
software"), `msg_update_quit_software_update`, `msg_check_for_update`, `msg_latest_version_installed` (TRACED,
`hknw_strings.txt`, `hknw_plugin-text.txt`). The package installs to `/dev_hdd0/vsh/modules/hakoniwa/`:
`config.bin`, `hknw.txt`, `resource/resource_ver.inf`, `resource/sound/bgm1..10.sgh`, `theme.sgh`, `com_se.sgd`,
`bus.sgd`, `resource/jacket/index.cxml` (TRACED, paths in the module).

**That folder does not exist locally** (`C:\dev\rpcs3\bin\dev_hdd0\vsh` has only `game_pkg`, `task`,
`rtcalarm.dat`; measured). INFERRED consequence: on this firmware image the console would open the download prompt,
not the gallery. The textures named only by bare file name in the module (15 floor types `type0..11_256.gtf` +
`_noise.gtf`, `floor_typeNN_sealMM.gtf`, group seals `baby/child/adult/smile/portrait/landscape/camera/game.gtf`,
`promote/search/reload/signIn/signUp/invite(_shadow).gtf`, `Comment1_L/R.gtf`, `Load_Arrow_dxt.gtf`) and the jacket
frames referenced by `jacket/index.cxml` (`frame_01_l.gtf`, `frame_01_h.gtf`, `frame_01_lo.gim`,
`frame_01_parallax_512.gtf`, ... `frame_10`, `noFrame_noJacket`) are on neither dev_flash nor dev_hdd0. INFERRED: they
ship in that package.

### 1.3 Screens and modes

From module strings (class and mode names), RCO pages and the 1,291-line `hknw_plugin/parameters.txt` (a
preprocessor-style table of `#define NAME value` with Japanese comments; TRACED):

| Area / mode | Evidence | What it is (INFERRED from names, comments, help pictures) |
|---|---|---|
| **Library area** ("For Browsing Imported Photos") | `msg_library_area`, `msg_library_explanation`; modes `LibraryCluster`, `LibraryGrid`, `LibrarySlideshow`, `LibrarySort`, `CLibrarySortState` | A 3D floor seen from above at an angle. Photos lie in **piles** (clusters), one per time group, placed along a sloped, slightly curved line (`PARAM_LINE_GRAD_MIN/MAX` 9..15, `PARAM_CURVE` 0.35, `PARAM_CURVE_POINT` 0.3, `PARAM_MAX_LINE_LENGTH` 0.7 of the screen width), with year labels between piles (`BORDER_YEAR_FONT_SIZE`, `PARAM_DISP_YEAR_PER_UNIT_DX`), coloured border lines and labels per pile (six colours `BORDER_LINE_COLOR_0..5`, `GROUP_TEXT_COLOR_0..5`) and arrows (`ARROW_OPACITY`, `tex_arrow_anim`). `tex_help_pic_library.png` shows exactly this: piles on a light floor, pink date labels, arrows between them, and a zoomed-in pile with large photos. |
| Cluster -> **Grid** | `LibraryGrid`, `CAMERA_ANGLE_GRID 90`, `CAMERA_NEAR/FAR_LIMIT_GRID 11 / 108.6`, `focus_grid.png`, `selection_grid.png`, `SELECT_EFFECT_GRID_COLOR_*` | Entering a pile tilts the camera to straight down (90 degrees) and lays its photos on a grid with a grid focus cursor. |
| Camera in chaos (pile) view | `CAMERA_ANGLE_CHAOS 65`, `_CHAOS_NEAR 80`, near/far `30 / 150`, `CAMERA_SPEED_ZOOM 5`, `QUICK_ZOOM_TIME 0.5`, `QUICK_ZOOM_OUT_TIME 0.8` | Right stick zooms (`msg_help_rstick_zoom`), left stick moves a free focus (`FOCUS_MOVE_SPEED 2.0`, `FOCUS_MOVE_BRAKE 0.85`, `FOCUS_GRAVITY 0.05` = snap to the nearest pile), `focus_cluster.png` + `_shadow` under the focused pile, which floats up (`GROUP_FOCUS_FLOAT_SPEED_UP 3`). |
| **Playlist area** ("For Browsing Select Photos") | `PlaylistGrid`, `PlaylistSlideshow`, `PLAYLIST_SPACE_WIDTH/HEIGHT 715/520`, `CJacketManager`, `CJacketTask`, `jacket/*.vpo/.fpo/.cxml` | User playlists as framed "jackets" on a wood floor (`tex_help_pic_playlist.png` shows framed photos with handwritten-style labels). Jackets rotate (`JACKET_ROTATION_SPEED`). Needs the package's frame textures. |
| **Network / PSN area** ("For Sharing Photos with Friends") | `NetworkGrid`, `NetworkSlideshow`, `CPsnManager`, `CNetSearchManager`, Facebook / Flickr / Google strings, `photo_network_sharing_plugin` | Shared albums over PSN and web services. The services are gone. |
| Grouping ("Group" / `AssortType`) | `msg_time_slot_group_title` Date/Time, `msg_color_group_title` Color (Red/Green/Blue/Black), `msg_number_face_group_title`, `msg_childish_group_title` Age Group (Babies/Children/Adults), `msg_smile_group_title` Expression, `msg_camera_group_title`, `msg_game_group_title`; db fields `Photo:Photo.faceCount`, `Photo:Photo.faceSmile` | Re-pile the library by date, colour, face count, age, smile, camera, game. Face/age/smile rely on face data the console's media db computed. |
| Sort | `msg_newer_order`, `msg_older_order`, `msg_similar_order`, `msg_random` | Newest / oldest / similarity / random. |
| Slideshow | `msg_slideshow`, `msg_slideshow_change`, types Slide / Portrait / Photo Album / Photo Album 2, `msg_slide_speed`, `msg_slideshow_bgm`; module paths to `photoviewer_plugin.sprx/.rco` | INFERRED: the gallery hands slideshows to `photoviewer_plugin`, whose RCO has the same four style strings and whose folder has 3D frame models `photo_M01..M05(_s,_sv).ggfz` for the album styles. |
| Boot | `BOOT_PHYSICS_ICON_NUM 250`, `BOOT_PHYSICS_INIT_FALL_DELAY 10`, `BOOT_CAMERA_INIT_ANGLE 65 -> DEST 50` over `BOOT_CAMERA_MOVE_TIME 20` s, `BOOT_HELP_DISP_TIME 7`, `tex_logo_boot`, box-collision symbols (`VertexAFaceBTests`, `BoxPoint`) | A title logo, photos that fall onto the floor with rigid-body physics, a slow camera sweep, help cards. |
| Menu | `tex_menuItems` (1200x120 strip: quit, tools, help, info, delete, rotate, move, edit, group, sort, slideshow, add-to-playlist, undo, redo), 36 `icon_*` 180x140 tiles, `hknw_menu_*` pages, `msg_piccto_menu_explanation` | Triangle opens a horizontal icon menu. |
| Settings | `msg_floor_type` (Type 1..15), `msg_floor_brightness` (Normal, -1..-5 = `FLOOR_BRIGHTNESS_TYPE0..5`), `msg_pointer_speed`, `msg_sound_key`, fonts 1..7 | Floor look and input. |
| Other | comments (`CCommentTask`, `comment_mark.png`), copy/import, print (`print_plugin`), upload, 2D/3D stereoscopic switch (`msg_3d_2d_switch`, `hknw_page_3d_caution`) | Peripheral features. |

Task classes in the module (TRACED, strings): `CSceneTask`, `CCameraTask`, `CFloorTask`, `CIconTask`, `CFocusTask`,
`CLabelTask`, `CGroupingEffectTask`, `CProjectionEffectTask`, `CInfoBarTask`, `CButtonIconTask`, `CCommentTask`,
`CJacketTask`, `CServiceLogoTask`, plus `CFontManager`, `CSurfaceManager`, `CResourceSurfaceManager`,
`./Misc/CShaderInterface.cpp`. Thumbnail cache paths: `/dev_hdd0/mms/thumbnails`, `/dev_hdd0/mms/hknw/Thumbnail_SS.db`;
`CACHE_THUMBNAIL_LEVEL3 256 // 512` and `LOD_PHOTO_DIST_1..3 34/60/100` give a three-level thumbnail LOD by camera
distance.

Ghidra was not needed: strings, the RCO and `parameters.txt` answer the scope question. Code-level tracing (pile
layout algorithm, camera easing) is listed as an optional step in section 4.

---

## 2. Assets available and whether the existing renderers can draw them

| Asset | Where | measured | Renderable now? |
|---|---|---|---|
| `parameters.txt` (camera, lights, shadow, LOD, layout, colours, timings) | `dev_flash/vsh/resource/hknw_plugin/` | 1,291 lines | Plain text; trivially parsed at run time. The single most useful asset. |
| `photo_shadow_1/2/3.png` | same | 96, 32, 32 px | Yes (PNG -> `QOpenGLTexture`). Three shadow LODs by `SHADOW_PHOTO_DIST_1..3`. |
| `photo_envmap.png` | same | 128x128 | Yes; sheen on photo surfaces. |
| `focus_cluster.png`, `focus_cluster_shadow.png`, `focus_grid.png`, `selection_grid.png`, `circle.png`, `circle_PlayListTarget.png` | same | 112x114, 229x229, 64, 64, 2x61, 2x61 | Yes; focus ring, grid cursor, contour circle strip (`circle.png` is a 1-D radial profile). |
| `projection_mask.png`, `GrainM.png`, `comment_mark.png` | same | 512, 256, 400 px | Yes. `GrainM` is a usable floor grain while the package floors are absent. |
| `jacket/vp_jacket_ezprim(2).vpo`, `fp_jacket_ezprim(2).fpo` | same | 1,760 / 1,104 B | Yes in principle: `src/rsx_program.*` already translates RSX Cg `.vpo/.fpo` to GLSL (used by `src/coldboot.cpp`). Useless without the jacket textures. |
| `jacket/index.cxml`, `frame_01.cxml`, `frame_05.cxml` (`HKNW` cxml) | same | 6,160 / 2,832 B | Parseable, but they point at `.gtf`/`.gim` that are absent. |
| `sound/bus.sgd` | same | 896 B | No SGD player in the build; BGM files are in the package anyway. |
| RCO images: 63 (`tex_menuItems`, `icon_*` tiles, `tex_guidepanel_*` button glyph panel, `tex_help_pic_*`, `tex_help_controller`, `tex_logo_boot`, `tex_arrow_anim(_shadow)` 120x2280 sprite strip, `icon_dialog_*`, `tex_3d_icon`) | `hknw_plugin.rco` | exported to `C:\dev\out\f4\img\` | Yes: exported through the build's own RCO path (`--export-image hknw_plugin:...`), so `View::drawImage(p,"hknw_plugin",name,...)` should resolve them (verify in step G7). |
| RCO strings (English), ~260 `msg_*` | `hknw_plugin.rco` | `C:\dev\out\f4\hknw_plugin-text.txt` | Yes, same string path as other containers. |
| RCO pages: `hknwpage_proto`, guide panel, menu / control panel / menu list / help (two variants), busy messages (library, playlist, network, deleting), opening busy, guide message, progress, title logo, service logo, `hknwpage_xmb_bg_page`, dialogs, 3D caution (24 pages, 184 objects) | `hknw_plugin.rco` | `C:\dev\out\f4\hknw_plugin.txt` | Layout values readable through `src/rco_objects.*`. |
| `hakoniwa.png`, `cinfo-bg-photogallery.jpg`, `cinfo-bgsd-photogallery.jpg` | `explore/icon/` | 180x180; JPEGs 234 KB / 111 KB | Yes; the row already uses `hakoniwa.png`. The `cinfo-bg` JPEG is the gallery's info background. |
| Floors (15 types), floor seals, group seals, jacket frames, BGM, key-tone SE | package `NPIA00006` | **absent** | No. Not on disk; obtaining them means downloading a Sony package, which this plan does not do. |
| Photoviewer slideshow models `photo_M01..M05*.ggfz` | `photoviewer_plugin/` | 25-80 KB | No `ggfz` loader exists. Separate from F4 (they belong to the slideshow styles). |

Existing native code that the gallery can stand on (TRACED in `src/`):

* `View` is a `QOpenGLWidget` with `QOpenGLFunctions_3_3_Core`; GL passes (`Ribbon`, `Coldboot`) render into the
  target FBO, then QPainter overlays (`drawPlaylistEditor`, `drawOsk`, `drawMusicPage`) draw on top. `Coldboot` is
  the template for a self-contained GL scene class (`init(gl, assets)`, `render(gl, w, h, fbo)`).
* `src/rsx_program.*` (Cg -> GLSL), the GTF reader inside `src/coldboot.cpp` (A8 only), `src/rco_objects.*`.
* Photo viewing: `src/photo_viewport.*` (pure zoom/pan geometry), `Shell::showPhoto`, `rotatePhoto`, `stepPhoto`,
  `slideshow_` timer with `photo/interval` and `media/repeat`, pad bindings in `view.cpp:4348-4356`.
* `MediaLibrary` (`src/media_library.*`): one-folder, non-recursive async scan, max 5,000 entries; `MediaEntry` has
  `modified` but no capture date and no thumbnails.
* `--script`, `--render-clock`, `--ui-clock`, `--data-dir`, `--no-intro` for deterministic captures.

---

## 3. Scope options

Sizes are new or changed C++ lines, rough.

### (a) Faithful port - XL, about 10,000-15,000 lines, plus blocked inputs

All three areas (Library, Playlist with jackets, Network), the physics boot, 15 floor types, re-grouping by colour,
faces, age, smile, camera and game, comments, copy, print, 2D/3D, BGM, the four photoviewer slideshow styles.

* Blocked: the floors, seals, jacket frames and BGM are in the NPIA00006 package, which is absent. Getting it means
  downloading a Sony package (needs explicit approval, and its availability today is unverified).
* Face / smile / age groups need a face detector; the console's media db supplied that data.
* Network area: the PSN, Facebook, Flickr and Google flows are dead services.
* No reference frame of any gallery screen exists, and the RPCS3 rig shows nothing without media (do not chase per
  `REFERENCE-GAPS.md`). Fidelity could not be measured.
* Would need code-level tracing of a 3.9 MB module (layout, physics, camera, shaders in `CShaderInterface`).

### (b) Main modes with firmware assets and simplified 3D - L, about 3,000-4,000 lines

Library area only: piles by date on a floor along the firmware's sloped curve, year labels, coloured pile borders and
date labels, free focus with gravity, right-stick zoom between the firmware's near/far limits, pile -> grid (camera to
90 degrees), grid -> the existing full-screen viewer, slideshow from a pile through the existing slideshow, Triangle
menu strip from `tex_menuItems` with the subset that maps to existing features (help, info, rotate, sort newest /
oldest / random, slideshow, quit), guide panel and help cards, title logo. Every constant comes from
`parameters.txt` at run time; every texture from dev_flash or the RCO. Simplified: a plain lit floor (white with
light fall-off, `GrainM` grain) instead of the 15 package floors, no physics boot (a short drop animation instead),
textured quads with shadow sprites instead of whatever geometry the module builds, no playlist/network areas, no
face groups (colour grouping is optional).

### (c) Firmware-styled 2D gallery page - M, about 800-1,200 lines

A QPainter page: date groups as rows of thumbnail stacks, `tex_menuItems` and guide-panel chrome, `msg_*` strings,
then the existing viewer. Cheap, but it is not the gallery: the app's whole identity is the 3D floor of piles, and
the page would invent a 2D layout the firmware never had.

### Recommendation: (b)

1. It is the only option that is both buildable from what is on disk and recognisably the firmware app: the
   distinctive parts (pile layout curve, camera angles and limits, focus physics, shadows, LOD, colours, timings) are
   in plain text in `parameters.txt`, and the sprites and UI chrome are on dev_flash and in the RCO. What (a) adds over
   (b) is mostly locked in the missing package or tied to dead services and a face detector.
2. Its riskiest code is new and isolated (a GL scene class like `Coldboot`), and the existing viewer, slideshow,
   rotation and zoom are reused for the leaf screens, so the change to `shell.cpp` / `view.cpp` stays small.
3. (c) would replace a known-unavailable row with a surface that is not the firmware's, which the fidelity audits
   would then have to flag as invented; (b)'s simplifications are each named and bounded.

Honest limit of (b): without reference frames, fidelity is "firmware constants and assets, layout reconstructed",
not measured. Each simplification is listed in the doc that ships with it.

---

## 4. Implementation plan for (b)

Steps are independent unless a dependency is named. G1-G5 touch **new files only** (plus one `CMakeLists.txt` line each
and, where stated, one `main.cpp` report flag). G6 is the only step that edits `shell.cpp` / `view.cpp`.

Fixture for every step: `C:\dev\out\f4\photos\` with about 120 JPEG/PNG copies spread over 3 years. Dates come from
EXIF where present; for files without EXIF, set `LastWriteTime` with PowerShell `Set-ItemProperty` to spread them over
dated groups. Data dir `C:\dev\out\f4\data`, build `C:\dev\out\f4-build`, frames `C:\dev\out\f4\cap\`.

### G1. Parameter table - new `src/gallery_params.{h,cpp}`

* Parse `hknw_plugin/parameters.txt` from the dev_flash root: `#define NAME value`, strip `//` comments (the file is
  UTF-8 with Japanese comments; values like `5.0//4.0` keep the first number), keep ints and floats, and report any
  name the code asks for that is missing. Typed accessors with the firmware default compiled in as a fallback.
* Flag `--gallery-params <json>`: write every parsed name/value and the count, then exit.
* Verify: the JSON holds the expected number of `#define` lines (cross-check with `grep -c '^#define'`), and spot
  values (`CAMERA_ANGLE_CHAOS 65`, `CAMERA_FAR_LIMIT_GRID 108.6396`, `FOCUS_MOVE_BRAKE 0.85`,
  `FLOOR_TYPE4_TEXT_G 0.1/1.0` -> 0.1) match the file.

### G2. Photo index and layout - new `src/gallery_model.{h,cpp}` (uses G1)

* Index: recursive scan of the Photo library roots on a worker thread (bounded by `BOOT_CONTENTS_LIMIT` and a lower
  practical cap), date = EXIF `DateTimeOriginal` from a small JPEG APP1 reader (new code; Qt reads EXIF orientation
  only), else file time. Cache the index as JSON in the data dir.
* Grouping: by date into piles (day / event gap; the unit spacing follows `PARAM_UNIT_DX`, `PARAM_MIN_DX/DZ`,
  `PARAM_MAX_PER_UNIT_DX`), year breaks where `PARAM_DISP_YEAR_PER_UNIT_DX` allows. Sort newest / oldest / random.
* Layout: pile centres along the sloped curve (`PARAM_LINE_GRAD_MIN/MAX`, `PARAM_CURVE`, `PARAM_CURVE_POINT`,
  `PARAM_LINE_DZ_STEP`, `PARAM_MAX_LINE_LENGTH`); photos inside a pile scattered by `GROUP_WIDTH_RATIO`,
  `GROUP_HEIGHT_RATIO`, `GROUP_RANGE_ROTATION_Y`, `GROUP_RANGE_MUTANT_RATE` and `_MUTANT_ROTATION_Y` with a seeded RNG
  (seed = pile id) so layouts are stable; visible count per pile from `PARAM_PHOTO_VIEW_COUNT_*`. Grid layout for one
  pile. Border colour index cycles 0..5.
* Pure data; no GL. Flag `--gallery-layout <json>` (needs `--data-dir`) writes piles, positions and labels, then exits.
* Verify: two runs give byte-identical JSON; pile count and date labels match the fixture's dates; no two pile
  centres closer than `PARAM_MIN_DX`.
* Optional sub-step G2b: trace the layout routine in the module with Ghidra (`C:\dev\xmb\ghidra_proj`) via the
  `PARAM_CURVE` load sites, and replace the reconstruction where it differs. Label the result TRACED or INFERRED.

### G3. Thumbnail LOD cache - new `src/gallery_thumbs.{h,cpp}`

* Three levels (128 / 256 / 512, after `CACHE_THUMBNAIL_LEVEL*`), chosen per photo by camera distance against
  `LOD_PHOTO_DIST_1..3`, requested around the focus by `REQUEST_LOD_PHOTO_DIST_*`. `QImageReader::setScaledSize` on
  the thread pool, EXIF orientation applied, disk cache under `<data>/gallery/thumbs`, upload to GL textures on the
  GUI thread with a per-frame budget, and an LRU cap.
* Verify: a headless self-test flag (or `--gallery-layout` extended with `--gallery-thumbs`) warms the cache for the
  fixture and reports counts, bytes and time; a second run reports all hits.

### G4. GL scene - new `src/gallery_scene.{h,cpp}` (uses G1-G3; modelled on `Coldboot`)

* Camera: perspective; pile view at `CAMERA_ANGLE_CHAOS` (moving to `_CHAOS_NEAR` when close), grid at
  `CAMERA_ANGLE_GRID`, distance clamped to the matching near/far limits, zoom speed `CAMERA_SPEED_ZOOM`, quick zoom
  `QUICK_ZOOM_TIME` / `QUICK_ZOOM_OUT_TIME`, area change easing from `CAMERA_SPEED_SPACECHANGE_*`.
* Floor: one large quad, `FLOOR_COLOR_*`, light fall-off to `FLOOR_LIGHT_OUT_COLOR_*`, brightness setting from
  `FLOOR_BRIGHTNESS_TYPE0..5`, `GrainM.png` as the grain layer (stand-in for the absent package floors; say so in the
  doc).
* Photos: textured quads with a border in `PHOTO_FRAME_COLOR_*`, lit by `LIGHT_PHOTO_POS/DIFFUSE/AMBIENT/SPECULAR`,
  `photo_envmap.png` sheen; placeholder colour `DEFAULT_PHOTO_COLOR_*` until the thumbnail arrives; shadows from
  `photo_shadow_1..3.png` by `SHADOW_PHOTO_DIST_*`, `SHADOW_PHOTO_DENSITY`, `SHADOW_PHOTO_SCALE_*`.
* Overlays in the scene: contour circles from `circle.png` (`CONTOUR_COLOR_*`, `CONTOUR_RADIUS_RATE`), focus ring
  `focus_cluster(_shadow).png` (`FOCUS_CLUSER_COLOR_*`), grid cursor `focus_grid.png`, selection `selection_grid.png`
  (`SELECT_EFFECT_*`), pile date labels and year labels rendered to textures through the existing text renderer with
  `GROUP_TEXT_COLOR_*` and the size LODs `GROUP_TEXT_LOD_*`, arrows from the `tex_arrow_anim` strip.
* Offscreen flag `--gallery-shot <png>` (with `--data-dir`, `--render-clock`): build the scene for the fixture, render
  one frame into an FBO at 1920x1080, save it, exit. That flag makes G4 verifiable before G6 exists.
* Verify: shots at three zoom distances and in grid mode; repeated shots identical with `--render-clock`; GL errors
  checked after each pass.

### G5. Controller - new `src/gallery_controller.{h,cpp}` (uses G2, G4)

* State machine: Boot (title `tex_logo_boot`, short photo drop instead of physics, camera from
  `BOOT_CAMERA_INIT_ANGLE` towards `_DEST_ANGLE`, help cards for `BOOT_HELP_DISP_TIME`) -> Library piles -> Grid ->
  Photo (hand-off) ; Menu; Quit ask (`msg_quit2_ask`).
* Input: free focus with `FOCUS_MOVE_SPEED`, `FOCUS_MOVE_BRAKE`, `FOCUS_GRAVITY` (snap to the nearest pile inside
  `GROUP_FOCUS_RECT_SPACE`), focused pile floats (`GROUP_FOCUS_FLOAT_SPEED_UP/DOWN`), right stick or L1/R1 zoom, Cross
  enter, Circle back, Triangle menu, Start slideshow, Square select (hold `INTF_ALL_SELECT` = select all).
* Pure logic driven by `tick(dt)` and `press(key)`; testable without GL by a headless flag that replays tokens and
  dumps state JSON.
* Verify: replayed token sequences give the expected focus pile ids and states; the focus settles on a pile within a
  bounded number of ticks at 30 and 144 Hz (frame-rate independence, as `--motion-report` checks elsewhere).

### G6. Integration - edits `src/shell.h`, `src/shell.cpp`, `src/view.h`, `src/view.cpp`, `src/main.cpp`, `CMakeLists.txt`

* `photo:seg_hakoniwa` changes from `Action::Unavailable` to a new `Action::PhotoGallery`; keep the row icon code.
* `View::paintGL`: when the gallery is open, render `GalleryScene` into the target FBO instead of the XMB scene, then
  its QPainter chrome (G7). Route keys and pad edges to the controller while open.
* Grid Cross -> `Shell` opens the existing viewer on the pile's file list at that index; Start -> the existing slideshow
  over the pile; Circle from the viewer returns to the grid with the same focus.
* `main.cpp`: any new `--script` tokens needed (right-stick zoom keys, Start).
* Verify: `--script` from the XMB: Photo column -> Photo Gallery -> wait -> zoom in -> enter pile -> grid move -> Cross
  (viewer) -> Circle -> Start (slideshow) -> Circle -> Triangle (menu) -> quit -> back at the row. Frames to
  `C:\dev\out\f4\cap\`. Empty library: the gallery shows `msg_error_no_image` and returns.

### G7. Chrome and text - new `src/gallery_chrome.cpp` (QPainter, called from the G6 hook)

* Guide panel (`tex_guidepanel_Base` + button glyphs) with the `msg_help_*` strings; menu strip from `tex_menuItems`
  (only the entries that map to working features are enabled; the rest drawn disabled or omitted, documented);
  help pages from `tex_help_pic_library` / `tex_help_controller` and `msg_*`; busy message for indexing; positions
  from the RCO pages via `src/rco_objects.*`.
* Verify: confirm `drawImage(p,"hknw_plugin",...)` resolves (the container may need adding to the asset table);
  frames of menu, help and guide panel; `--image-audit` lists the new container without missing images.

### G8. Documentation and status

* New `audit/PHOTO-GALLERY.md` (what was built, every simplification, every constant source, frames); update
  `audit/FEATURE-STATUS-2026-11-04.md` item 4 and `audit/FIDELITY-SURFACES-MAP.md` row "Photo Gallery".

### Explicitly out of (b)

Playlist area and jackets, Network/PSN area, face/age/smile/camera/game grouping, comments, copy/import, print,
upload, 2D/3D stereo, the 15 package floors and seals, BGM and key tones, physics boot, the four photoviewer slideshow
styles (`ggfz` models; a separate item).

---

## 5. Open questions

* Whether the NPIA00006 package is still served is unverified; nothing was downloaded.
* The cluster layout, camera easing and shaders are reconstructed from constants, not traced (G2b can tighten this).
* `hknwpage_xmb_bg_page` suggests the XMB background may show behind or before the gallery; not traced.

## 6. Commands used

```
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-tree "hknw_plugin@C:/dev/out/f4/hknw_plugin.txt"
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-text "hknw_plugin@C:/dev/out/f4/hknw_plugin-text.txt"
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-tree "photoviewer_plugin@C:/dev/out/f4/photoviewer_plugin.txt"
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --rco-images "C:/dev/out/f4/rco-images.txt"
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --export-image "hknw_plugin:tex_menuItems,...@C:/dev/out/f4/img"
grep -aoE '[[:print:]]{6,}' fw493/vsh_module/hknw_plugin.prx > C:/dev/out/f4/hknw_strings.txt
```
(`re-ps3-native.exe` = `C:\dev\xmb\out\native-dim\re-ps3-native.exe`.)
