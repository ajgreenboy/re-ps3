# PHOTO-GALLERY-PLAYLIST - the hakoniwa Playlist area ("For Browsing Select Photos")

Status 2026-11-12: traced in part, first area built. The Photo Gallery (`PHOTO-GALLERY.md`, design in
`PHOTO-GALLERY-DESIGN.md`) now has the firmware's second area next to the Library area: the user's photo playlists as
jackets on the floor, placed by the module's own collision spiral, entered with R1 and left with L1, each jacket opening
a grid of its photos. The Network / PSN area stays unbuilt on purpose (dead services).

Labels: **TRACED** = read from `hknw_plugin.prx` code or data (address given) or from the RCO / dev_flash files;
**measured** = counted or read off local files; **INFERRED** = a reading of names, comments or the help picture, not
followed through code. Every INFERRED behaviour in the code carries the same label in a comment.

No firmware, texture or capture is in the repository. Constants are read from dev_flash `parameters.txt` at run time;
textures from dev_flash and the RCO.

## 1. Sources and method

| Source | How | Notes |
|---|---|---|
| `fw493/vsh_module/hknw_plugin.prx` (decrypted, 3,929,320 B) | Imported into a scratch Ghidra project (`C:\dev\out\galpl\ghidra\hknw`, PowerPC:BE:64:A2ALT-32addr, the same loader settings as the vsh493 projects; hknw_plugin was in none of them). Headless scripts from `C:\dev\xmb\ghidra_scripts` plus two scratch ones (float-constant references, callers). | Addresses below are Ghidra addresses; the file offset of the constant pool is address + 0x100 (measured). |
| `vsh/resource/hknw_plugin.rco` | `--rco-text`, `--rco-tree`, `--rco-images`, `--export-image` of this build | Strings, pages, images. |
| `vsh/resource/hknw_plugin/parameters.txt` | read | The names and comments. |
| `vsh/resource/hknw_plugin/jacket/*.cxml` | strings | The jacket table. |
| `tex_help_pic_playlist` (RCO, 420x236) | exported and viewed | The only picture of the area. |

## 2. What the module tells about parameters.txt

* **TRACED: the module never opens `parameters.txt`.** Neither "parameters" nor any `#define` name appears among the
  module's 9,933 printable strings (measured with a strings pass), so the file is a copy of a build header shipped as
  data. The values are compiled in.
* **TRACED: the compiled values match the file for every playlist constant checked, times 0.01** (the file's own
  `COORD_UNIT` comment on `CAMERA_FAR_LIMIT_GRID`): the constant pool at `0x1cfab4..0x1cfac8` holds `-3.575, 3.575,
  -2.6, 2.6, 0.4, 0.55` = `PLAYLIST_SPACE_WIDTH/2`, `PLAYLIST_SPACE_HEIGHT/2`, `NEW_PLAYLIST_DIST_Z`,
  `NEW_PLAYLIST_DIST_X` x 0.01, and `0x1ce720` holds `1.086396` (`CAMERA_FAR_LIMIT_GRID` x 0.01). The port keeps the
  file's units (x 100 of the module's), as the Library area already does; only ratios matter for the layout.
* So reading the file at run time stays correct for these names, but a value that differs between the file and the
  build would not be noticed; this is an open risk for names not checked here.

## 3. Jacket placement (TRACED, `FUN_001a12e0`)

`FUN_001a12e0` is called in a loop by `FUN_001a1a68` (a method of the class with vtable `0x300be8`) until it returns 0;
each call places one jacket that is not yet placed (`FUN_001a1274` returns the first jacket whose flag
`(+0x38 >> 57) & 1` is clear) at a requested position passed in a vector register:

1. If the requested point lies outside the rect `x in [-3.575, 3.575]`, `z in [-2.6, 2.6]` (`PLAYLIST_SPACE_*`), it
   returns 0 and places nothing.
2. Otherwise the jacket's position is set to the point, then, while `FUN_000eff1c` reports an overlap with an already
   placed jacket, the position walks a **square spiral**: direction `d` cycles 0..3, legs of length 1, 1, 2, 2, 3, 3,
   ... (two legs per length). Per step: `d=0: z -= 0.4`, `d=1: x -= 0.55`, `d=2: z += 0.4`, `d=3: x += 0.55`
   (`FUN_000a6cb4` wraps `vsubfp` at `0xa6c40`, `FUN_000af82c` wraps `vaddfp` at `0xaf718`; both act on vector element
   0 = x or 2 = z).
3. A step that would leave the rect is not taken; the walk stops there.
4. On stop (no overlap, or the edge), the flag is set (`FUN_0013b4b0(jacket, 1)`) and the position is written to the
   media database as `PhotoList:PhotoList.jacketPosition` (string at `0x1c7c58`; the module also keeps
   `PhotoList.jacketAngle`, `.jacketType`, `.jacketName`).

The overlap test (`FUN_000eff1c` -> `FUN_000ef854` -> `FUN_000ef688`) is an axis-aligned rect test of the two jackets'
centres with half sizes `0.5 * width` and `0.5 * height` read from the jacket at `+0xf0` / `+0xf4`
(`FUN_0013b4d8` / `FUN_0013b4e0`; constants `-0.5, 0.5` at `0x1ce3c4`; the `0.008` at `0x1ce3cc` only pads the reported
overlap). The jacket's rotation is not part of the test. `PLAYLIST_ATARI_HANTEI_PARAM` (1.0, "1.0 = as before") was
not found in this path.

Where the requested point comes from was not traced (the callers reach `FUN_001a1a68` through the vtable).

**Port:** `GalleryController::placeJackets` is this algorithm, step for step, with the file's values. INFERRED parts:
every jacket is requested at the space centre (`PLAYLIST_SPACE_CENTER_X/Z`), in the store's order, and nothing is
persisted (this port's playlist store has no position field), so the layout is rebuilt identically each time. With 7
playlists the spiral gives (measured, `--gallery-replay`): (0,0), (0,-40), (-55,-40), (-55,0), (-55,40), (0,40),
(55,40).

## 4. Jacket size, look and the package

* **TRACED: the jacket's width/height come from its model.** The setters `FUN_0013b4cc` (size), `FUN_0013b4e8`,
  `FUN_0013b4f4` are called after `FUN_00112a24` (a jacket-table lookup) with the result multiplied by a scale
  (e.g. `0x103900..0x103924`, `0x1080f0..0x108118`, `0x109b1c..0x109b44`).
* **TRACED: the jacket models are package data.** `jacket/index.cxml` (`HKNW` cxml, version 1.00) lists 38 jacket types
  (`noFrame_noJacket`, `frame_01`, `frame_02`, `frame_10`, `frame_03`, `frame_03B`, ..., `woodHanger`, `flowerShower`,
  `paper_birthday`, `stage_locomotive`, `cloth1`, ...) each with `_l.gtf` / `_h.gtf` textures and a `.cxml`; only
  `frame_01.cxml` and `frame_05.cxml` are on dev_flash, and they name `.gim` models, parallax / shadow `.gtf` files and
  the shader set `vp_jacket_ezprim`, `vp_jacket_normalmap`, `fp_jacket_normalmap_specL(_rgb)`, `fp_jacket_ezshadow`
  (only the two `ezprim` programs are on dev_flash). The module loads the table from
  `/dev_hdd0/vsh/modules/hakoniwa/resource/jacket/index.cxml`, i.e. the NPIA00006 download (see the design doc 1.2).
  `frame_01.cxml` has layer types `image-frame-static` and `image-thumbnail-auto`.
* **Port (INFERRED / port choice):** no frame can be drawn. A jacket is the playlist's first photo as a card (the
  library's photo shader and frame colour) at 0.7 of a placement step wide (`NEW_PLAYLIST_DIST_X` x 0.7, keeping the
  spiral's cells apart as in the help picture), lying straight (no stored angle). An empty playlist is a card of the
  module's `DEFAULT_PHOTO_COLOR_*`. `JACKET_ROTATION_SPEED` / `_FRICTION` (rotating a jacket) are not used.

## 5. The area, its input and its chrome

| Fact | Evidence | Label |
|---|---|---|
| Areas run Library, Playlist, PSN, left to right; L1 / R1 step between them | RCO strings: `msg_help_go_to_playlist_area_r1` and `_l1`, `msg_help_go_to_library_area_l1` only (no `_r1`), `msg_help_go_to_psn_area_r1` only (no `_l1`); `msg_go_to_left_area` / `msg_go_to_right_area` | strings TRACED, order INFERRED from which suffixes exist |
| Area names and explanation | `msg_playlist_area` "Playlist Area", `msg_playlist_explanation` "For Browsing Select Photos", guide objects `hknw_guidepanel_text_playlist` / `_library` / `_network`, help page `hknw_menu_help1_Playlist` with `tex_help_pic_playlist` | TRACED (RCO) |
| A busy page during the change | `hknw_busy_to_playlist_message_page` with `hknw_busy_message_text_to_playlist` = `msg_playlist_area` | TRACED (RCO); its timing INFERRED |
| Guide glyphs | `tex_guidepanel_L1`, `_R1` (76x52), `tex_Arrow_LR1` (40x26) | TRACED (RCO images) |
| Module states for the area | strings `PlaylistJacket` (`0x969d4`), `PlaylistGrid` (`0x9456c`), `PlaylistMenu` (`0x99928`), `PlaylistAdd` (`0x92ae8`), `PlaylistSlideshow` (`0x9bf94`), classes `CJacketManager`, `CJacketTask`, jobs `CPlaylistCreateJob`, `CPlaylistLoadJob`, `CPlaylistSaveJob`, `CPlaylistSearchJob`, `CInsertToPlaylistJob`, page `hknwpage_jacket_menu_page` | TRACED names; the states' input handling was not decoded (`FUN_000958d8` is a phase machine over `+0xbc`, not followed further) |
| Camera limits | `CAMERA_NEAR_LIMIT_PLAYLIST 30`, `CAMERA_FAR_LIMIT_PLAYLIST 125`, `CAMERA_NEAR_LIMIT_GRID_PLAYLIST 11` (1.25 is loaded at `0x102f38`, `0x1072ac`, `0x10919c`, `0x109d28`) | values TRACED; which load is which INFERRED |
| Label size and offset | `PLAYLIST_TEXT_SIZE 2.5` ("ratio to the library space"), `PLAYLIST_TEXT_POS_Z 1.9`; `FUN_001a0488` loads both (`2.5f` at `0x1cfa7c`, the double `1.9` at `0x1cfc20`) | TRACED loads; how they are applied INFERRED |
| Floor | `FLOOR_PLAYLIST_COLOR_* 1.0`, `FLOOR_WRAP_COUNT_PLAYLIST 40`; the wood floor of the help picture is a package floor | names TRACED; package floor absent |
| Help picture | `tex_help_pic_playlist`: framed photos at several angles on a wood floor, a handwritten-style label under each in pink, cyan, orange | measured (viewed) |

**Port:** R1 from the Library area's floor view (not in a grid, no overlay) enters the Playlist area; L1 from the
Playlist area's floor view returns; R1 in the Playlist area does nothing (PSN not built). The change is a black veil
with the entered area's name (`msg_playlist_area` / `msg_library_area`) that fades out over
`PLAYLIST_VIEW_SPACE_FADE_OUT` and in over `PLAYLIST_VIEW_SPACE_FADE_IN` seconds (INFERRED use of those names). Each area
keeps its own focus and camera distance across changes. The Playlist area opens at `CAMERA_FAR_LIMIT_PLAYLIST` on the
first jacket (INFERRED).

Inside the area (all INFERRED, the jacket state's input is not traced): the left stick moves the library's free focus
with snap to the nearest jacket; the d-pad moves to the nearest jacket on that side (off-axis distance weighted double);
the focused jacket lifts and carries the gallery's `focus_grid.png` frame (the library's `focus_cluster` ring reads as a
grey slab at a jacket's size); Cross opens the jacket's photos as a grid (the library grid, `CAMERA_NEAR_LIMIT_GRID_PLAYLIST`
as its near limit), Cross on a cell opens the existing photo viewer, Start runs the existing slideshow over the
playlist (the module's `PlaylistSlideshow` is not traced, so this is the Library's slideshow), Circle leaves the grid,
Circle on the floor asks to quit (as in the library), Triangle opens the library's menu strip with Sort disabled (the
module's own `PlaylistMenu` entries are not traced). The labels use `GROUP_TEXT_COLOR_n` cycling by jacket (INFERRED
from the help picture's colours). The guide panel shows Cross / Circle / Triangle / Start and, on the right,
`tex_guidepanel_L1` with `msg_help_go_to_library_area_l1` (its leading private-use L1 glyph removed).

Pad mapping change: L1 / R1 used to zoom in the gallery (a port choice); they now change the area, as the firmware
strings say. Zoom stays on the right stick (an XInput pad's right stick is now read too), `+` / `-` and
PageUp / PageDown. Keyboard and `--script`: `[` / `]` (`PREV` / `NEXT`) are L1 / R1; `--gallery-replay` takes `L1`, `R1`
and loads the jackets from the first `<data-dir>/playlists/*.json`.

The earlier stand-in (commit 6af77f1: a "Playlists" entry replacing `add_to_playlist` in the menu strip and a 2D list
overlay with invented "Playlists" / "No playlists" texts) is removed; menu item 11 is `add_to_playlist` again
(disabled).

## 6. What is drawn, what is not

Drawn: the floor with the playlist floor colour; one jacket per photo playlist at the traced spiral positions (cover =
first photo, thumbnails through the existing LOD cache; empty playlist = default-colour card); jacket shadows
(`SHADOW_JACKET_DENSITY`); the name under each jacket; the lifted focused jacket with its frame; the jacket's grid; the
header (area name, focused playlist, photo count, grid position); the guide panel with the L1 hint; the area-change veil.

Not drawn / not built:
- Jacket frames, the 38 jacket types, the jacket shaders and the wood floor (package NPIA00006, absent).
- Jacket angles, moving and rotating jackets (`JACKET_ROTATION_*`, menu "move"), persisted jacket positions.
- Creating, renaming, deleting playlists and adding photos from the gallery (`PlaylistAdd`, `msg_add_to_playlist`,
  `msg_help_create_new_playlist`, `msg_delete_playlist_ask`, `circle_PlayListTarget.png`); the playlists come from the
  Photo column's Playlists folder.
- The module's `PlaylistMenu`, `PlaylistSlideshow` and the photoviewer slideshow styles.
- `PLAYLIST_VRECT_*`, `PLAYLIST_SHOW_SPACE_SAFE_VALUE`, `PLAYLIST_VIEW_SPACE_SAFE_VALUE` (view-rect culling and
  clamping of the camera to the space), `LOD_JACKET_*`, `SHADOW_JACKET_DIST_*`.
- **The Library area's R1 hint** (`msg_help_go_to_playlist_area_r1`): drawing it changes the pinned
  `photo-gallery-piles` / `photo-gallery-grid` frames, so it waits for an owner-approved reference refresh. R1 itself
  works there.
- The PSN / Network area.

## 7. Verification (2026-11-12)

Build: `build.ps1` "Built", zero warnings. Scratch data: `C:\dev\out\galpl\data` (gate seed `v2/photo` plus a playlists
file of 7 photo playlists over the read-only fixture `C:\dev\out\gal\photos`: 3, 7, 11, 15, 19, 22 and 0 items; made by
`C:\dev\out\galpl\make-data.ts`).

Headless (`--gallery-replay`, `C:\dev\out\galpl\replay2.json`): RIGHT, R1 -> area `playlist`, 7 jackets, camera (0,0)
at 125; UP -> Family (0,-40); LEFT -> Best of the City (-55,-40); ENTER -> grid of 11 at distance 11; RIGHT -> 2/11;
START -> slideshow act with 11 files; ESC -> floor; L1 -> area `library` with the library focus (pile 1) and distance 40
restored; R1 -> back on Best of the City. Settle times stay frame-rate independent (focus 1.633 / 1.617 / 1.618 s, grid
0.5 / 0.5 / 0.493 s at 30 / 60 / 144 Hz).

Frames viewed (`C:\dev\out\galpl\cap\`, from `--script` runs of the full app; not committed):
- `3-jackets.png`: the Playlist Area header, "Summer Trip 3 photos", six jacket cards in the spiral around the lifted,
  framed Summer Trip, labels in red / green / blue / orange, one card still waiting for its thumbnail (pale), the Empty
  playlist's pale card at the right, the guide with the L1 glyph and "Go to Library Area".
- `4-focus.png`: Best of the City lifted and framed at the centre after UP, LEFT; Family to the right, Work Party and
  Summer Trip below; header "Best of the City 11 photos".
- `5-grid.png` (first run, before the jacket frame replaced the grey `focus_cluster` slab; the grid path did not change):
  Best of the City's 11 photos on the grid, the cursor on cell 1, header "Best of the City 11 photos 1 / 11".
- `2-veil.png`: the area change: the floor dimmed under the veil with "Playlist Area" centred (grabbed in the fade-in
  half; the fade-out half was over before the grab).
- `6-back.png`: back in the Library Area after L1: "2023/10/14 2 photos Newest First", the piles as before.

Gate: `bun work/v2-fixtures.ts --check` ready; `photo-gallery-piles` and `photo-gallery-grid` run alone, each twice,
PASS at PSNR inf / SSIM 1.0000 on every region. No gate state was added for the Playlist area: the gate's data seed has
no playlists file, and a new state would need a seed and a reference made by the owner.

## 8. Open

1. Decode the `PlaylistJacket` state's input (focus movement, zoom, the jacket menu) and replace the INFERRED rules.
2. Trace who requests the jacket start point (the vtable callers of `FUN_001a1a68`), and the jacket size scale.
3. Persist jacket positions and angles (the module's `PhotoList.jacketPosition` / `.jacketAngle`) in the playlist store.
4. Check the remaining `parameters.txt` names against the compiled constants (section 2).
5. With an approved reference refresh: the Library area's R1 hint; then pin a `photo-gallery-playlist` state with a
   seeded playlists file.
