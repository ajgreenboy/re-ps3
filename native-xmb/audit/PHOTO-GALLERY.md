# PHOTO-GALLERY - the recreated hakoniwa library area (F4, scope (b))

Status 2026-11-08: **built (G1-G7), not yet exercised against the real console look.** The Photo column's Photo
Gallery row now opens a native 3D gallery area - piles on a floor, focus/zoom, a grid per pile, and a hand-off to the
existing photo viewer - built to the scope in `PHOTO-GALLERY-DESIGN.md` §4 option (b) ("main modes with firmware
assets and simplified 3D"). Labels: **TRACED** (firmware module/file), **INFERRED** (a reading of a traced name/comment,
not followed through `hknw_plugin.prx` code), **M** (measured on local files). The design analysis is
`PHOTO-GALLERY-DESIGN.md`; this file records what landed.

No firmware or theme content is shipped. Every constant is read at run time from the local dev_flash
(`vsh/resource/hknw_plugin/parameters.txt`); every texture from `vsh/resource/hknw_plugin/*.png` and the RCO.

## 1. What landed, by step

| Step | What | Files | Source of the constants |
|---|---|---|---|
| G1 | Parameter table: parses `parameters.txt` (`#define NAME value`, `//` comments) at run time; no firmware value is compiled in. A missing name is never guessed (0 + recorded); `require()` gates the code paths that need a set. | `gallery_params.{h,cpp}` | TRACED (the file itself) |
| G2 | Photo index and layout: recursive scan of the Photo library roots; piles (clusters by time line) and the floor placement; `GallerySort` newest/oldest/random. | `gallery_model.{h,cpp}` | TRACED names, layout reconstructed (design doc G2) |
| G3 | Thumbnail LOD cache: 128/256/512 levels read from the `CACHE_THUMBNAIL_LEVEL1..3` comments; warmed on demand; bounded. | `gallery_thumbs.{h,cpp}` | TRACED (the comment integers) |
| G4 | GL scene (modelled on `Coldboot`): one lit floor quad, the piles' photo quads with soft shadows, contour/date/year labels, the between-pile arrows, the focus ring and the grid cursor; one shader with mode variants. | `gallery_scene.{h,cpp}` | TRACED names; camera/easing reconstructed |
| G5 | Controller: Boot -> Piles -> Grid state machine, free focus with snap-to-pile (FOCUS_MOVE_SPEED/BRAKE/GRAVITY), pile float on focus (GROUP_FOCUS_FLOAT_SPEED_UP/DOWN), key/analog zoom, Cross enter / Circle back / Triangle menu / Start slideshow, quit ask on leaving. Pure logic (`tick(dt)` + `press(key)`), headless-replayable. | `gallery_controller.{h,cpp}` | per-line TRACED/INFERRED labels in the file |
| G6 | Integration: `photo:seg_hakoniwa` -> `Action::PhotoGallery` -> `View::openGallery`; `paintGL` renders the gallery instead of the XMB while open; keys/pad route to the controller; the photo viewer steps through one pile's files (`Shell::openGalleryPhotos`, `galleryPaths_`). | `shell.{h,cpp}`, `view.{h,cpp}`, `main.cpp`, `CMakeLists.txt` | n/a |
| G7 | Chrome and text (QPainter): title, pile counts, the focus ring, the guide/menu panel and the error/busy states. | `gallery_chrome.cpp` | RCO positions via `rco_objects`; some placement INFERRED |

Verification hook: `--gallery-replay` (tokens LEFT/RIGHT/UP/DOWN/ENTER/ESC/TRIANGLE/START/ZOOMIN/ZOOMOUT, wait=ms)
runs the controller headlessly over the scanned roots, writes the state after each token as JSON, and measures the
focus/grid settle times at 30/60/144 Hz (frame-rate independence, the same check `--motion-report` does elsewhere).
`--gallery-layout` (from G2) and `--gallery-thumbs` (from G3) remain.

## 2. Simplifications (what the firmware has that this port does not)

Scope (b) drew a line; these are absent by design and are named in the UI honestly where relevant:

- **The floor** is one lit quad. The firmware's 15 package floors (the NPIA00006 download) and their seals/age/face
  groupings are absent.
- **Network/PSN area**, comments, copy/import, print, upload, 2D/3D stereo, BGM and key tones. The **Playlist area**
  was added on 2026-11-12 (jackets from the Photo column's playlists, the module's traced placement spiral, R1 / L1);
  see `PHOTO-GALLERY-PLAYLIST.md` for what is traced, inferred and still absent (jacket frames, angles, editing).
- **Physics boot**: the module drops photos with a rigid-body simulation and a ~20 s camera sweep; the port shows a
  short fall of the shown photos plus the title logo (`tex_logo_boot`), a port choice, not a firmware value.
- **The four photoviewer slideshow styles** (`ggfz` models) are a separate item.
- **Menu strip**: only the `tex_menuItems` entries that map to a working feature are enabled; the rest are drawn
  disabled or omitted.

## 3. Constant sources (the honest line)

Every numeric constant the controller and scene use is read from `parameters.txt` at run time; the mapping of each
name to a behaviour is labelled in `gallery_controller.cpp`/`gallery_scene.cpp` as TRACED (name + comment read from the
file) or INFERRED (a reading of the name/comment, not followed through `hknw_plugin.prx`). Two values are port choices
and are marked so in the code: the field of view (parameters.txt has none) and the boot fall. The cluster layout,
camera easing and shader constants are reconstructed from the parameter names, not traced from the module - the design
doc's G2b (a Ghidra pass over `hknw_plugin.prx`) can tighten these and is still open.

## 4. Verification

- Build: `build.ps1` passes with the six gallery files in the source list.
- Headless: `--gallery-layout`, `--gallery-thumbs` and `--gallery-replay` are wired but have not been run against a
  photo library on this machine yet; run them before trusting the replay numbers.
- Interactive: not yet run on the TV. The G6 verification script from the design doc (Photo -> Photo Gallery -> zoom ->
  enter -> grid move -> Cross viewer -> Circle -> Start slideshow -> Circle -> Triangle menu -> quit) is the gate to
  run next, with frames to `C:\dev\out\f4\cap\`.
- `hknw_plugin` image resolution through `View::drawImage` is not yet confirmed end-to-end (design doc G7 verify).

## 5. Still open

1. The G6/G7 interactive verification and frames above.
2. G2b: trace the cluster layout/camera/shaders in `hknw_plugin.prx` with Ghidra (design doc §5).
3. `hknwpage_xmb_bg_page` (the XMB background behind the gallery) is not traced.
