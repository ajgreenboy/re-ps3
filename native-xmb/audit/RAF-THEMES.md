# RAF-THEMES - animated (RAF) theme support, F6 remainder

Status 2026-11-08: **parse and report only. No RAF scene is drawn.** A dynamic theme still shows its still background
(`bgimage` hd/sd picture), exactly as before. This file records what is known, how it is known, and what is left.
Evidence tags: TRACED (firmware module/address), M (measured on a local file), INFERRED.

No firmware or theme content is shipped. The reader works on a user-supplied `.p3t` (or a bare `.raf`); reports and
dumps go to local folders outside the repository.

## 1. What landed

| Piece | Where | Tag |
|---|---|---|
| `.p3t` container + element tree; `bgimage/anim` located and its RAF container (`_RAF`+u32 BE size+zlib, raw `RAFO`, bare zlib) unpacked | `P3t::read`, `readScene` in `src/p3t.cpp` | M (Air Paint, coldboot.raf, synthetic fixtures) |
| Scene model: one camera, lights, actors (position/rotation/scale/uv/colour), models, EA03 animation headers, GTF texture headers, material map bindings, VSMX script header | `P3t::Raf` in `src/p3t.h` | M on coldboot.raf; EA03 path on a synthetic RAFO only |
| `--raf-report <theme.p3t\|scene.raf>@<report.json>` (this pass): the whole parsed scene graph as JSON, with actor -> material -> effect -> colour map bindings resolved, and a `renderer` block saying nothing is drawn. Exit 0 when a scene parsed, 3 when the theme has none, 2 on read errors | `P3t::rafReportJson`, `src/main.cpp` | M |
| `XMB_P3T_TRACE=1` one-line summary through `--p3t-dump` | `P3t::trace` | M |
| The fixed coldboot RAF renderer (3 HD actors, `raf.qrc` `vert0.vpo`/`sce01.fpo`, hardcoded `coldboot.jsx` timeline) | `src/coldboot.cpp` | M / TRACED (see `coldboot.h` header) |

Verified this pass with `--raf-report` on `coldboot.raf` (real: 6 actors, 2 lights, camera, `sce01` effect, VSMX script
with 518 instructions, no `.anim` files) and on the synthetic fixture (1 EA03 header, 30 fps, 75 frames, timeline 0..2).
The static fixture `.p3t` reports `hasScene=false` and exits 3. **No real dynamic-theme RAF was available locally**, so
nothing here has been checked against one; the theme-embedded path (`bgimage/anim`) is proven only by the synthetic
theme fixture, not by a shipped dynamic theme. Run `--raf-report` on a real dynamic theme first; its `effects`,
`textures[].format` and `script` blocks decide the work order below.

## 2. Why nothing is drawn (the honest gap)

Parsing the container is the easy part. The motion is not in the container:

1. **Time lives in the script (INFERRED from coldboot.raf, M for its content).** There is no keyframe track list in the
   tree; `coldboot.raf` has zero `.anim` files and its whole timeline is VSMX code. Without the interpreter the actors
   would sit at their initial pose. Drawing that and calling it a dynamic theme would be a fake.
2. **Edge geometry** (`.edge`): compressed index stream, segments, several vertex stream types. `coldboot.cpp`'s
   `parsePlane` special-cases a 4x4 plane. Arbitrary theme meshes need a real decoder.
3. **Skeleton and `.anim`** (EA03): only the header (duration, fps, joints, frames) is read; keyframes are not.
4. **VSMX interpreter**: compiled PlayStation JavaScript; `setPosition`/`setColor`/timers/time-of-day. `coldboot.cpp`
   pattern-matches eight assignments, which does not generalise.
5. **Effects/materials**: only `sce01` is wired. Themes name others (`pure_texture*`, `basic_lighting*`); each needs its
   Cg program from `raf.qrc` (or the equivalent) plus blend/depth state.
6. **Textures**: GTF DXT1/3/5, A8R8G8B8 (maybe swizzled) and B8 with mipmaps; coldboot uploads B8 only.
7. **Composition and lifecycle**: depth/zsort, background layer under the XMB, fallback to the still background on any
   failure, pause/resume.

Detail for items 2-7 is in `THEMES.md` section 3.3; that analysis stands.

## 3. Remaining work, in order

1. Run `--raf-report` on 2-3 real dynamic themes the user supplies (needs the user's files; never commit them). Record the
   effects, texture formats, model count and script size distribution here, replacing INFERRED with M.
2. Decode VSMX far enough to list the calls a theme script makes (a disassembler mode, `--raf-report` extension).
   Decide the subset from real scripts, not from coldboot alone.
3. GTF decode for the formats found in step 1 (the RCO/GIM DXT paths are reusable for DXT).
4. Edge geometry decode, then a first static frame of a real theme behind the XMB, labelled as a still until the script
   runs. Only after step 2 does it become playback.
5. Interpreter subset + `Track` evaluator reuse from `coldboot.cpp`, effect programs, lifecycle and fallback.
6. A pinned-state gate scenario (V2) once a frame renders.

Size: unchanged from F6, **L**. This pass is the groundwork that makes step 1 possible without a debugger.
