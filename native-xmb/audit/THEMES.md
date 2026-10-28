# THEMES - what a 4.93 .p3t contains, what the reader now exposes, and the RAF verdict

Written 2026-10-27. Scope: the theme layer owned by `src/p3t.cpp` and `src/p3t.h`. `src/shell.cpp`,
`src/view.cpp`, `src/settings_menu.cpp` and `src/main.cpp` were read but **not modified**; the exact wiring hunks
are in section 4. No firmware, theme, capture or derived image is committed.

Evidence classes used in this document:

- **M** = measured locally in this run: bytes read from the fixture, values printed by `--p3t-dump`, or a gate
  actually executed. Commands and outputs are in section 5.
- **F** = fit or inference: a mapping taken from the PSDevWiki or a count-inference that was not independently
  traced to firmware.
- **R** = recorded run from an earlier audit (cited, not re-run here).

Local fixtures:

| Fixture | Path | SHA-256 | Role |
|---|---|---|---|
| Air Paint, the firmware's own 4.93 theme | `C:\dev\rpcs3\bin\dev_flash\vsh\resource\theme\01.p3t` | `BB2059AB8FF0708470FB272B486576C3F3FD774D0BE46396F7BBF4FA447922CA` | the real static theme parsed below |
| coldboot.raf | `C:\dev\rpcs3\bin\dev_flash\vsh\resource\coldboot.raf` | `6B08A53DA5E2388985E616C53DEAFF7CCE7AB16982B4FE31C9985B745E217A68` | the only local RAF scene |
| synthetic P3TF fixture | `C:\dev\out\fixture\fixture.p3t` | `4E32FDAEAADF38D09F4A99F3BF3500C5D89282835A080AAD35F62CAE5589CCD7` | exercises pointer/notification/setable/info fields Air Paint omits |
| synthetic RAFO fixture | `C:\dev\out\fixture\fixture.raf` | `831F806CCF09A21A8F7B1AB8F9090E20E9E21E840667C6B144A7AFDD147FBA22` | exercises the EA03 animation-header/timeline path coldboot.raf does not have |

The synthetic files are local verification artifacts under `out/`, not repository content and not firmware
replacements.

## 1. What Air Paint actually contains

### 1.1 Container facts (M)

`01.p3t` is 2,016,992 bytes. Read straight from its header:

| Field | Value |
|---|---|
| Magic | `P3TF` |
| Header version word | 272 |
| Section 0 (element tree) | offset 64, size 6,432 |
| Section 1 (id table) | offset 6,496, size 1,352 |
| Section 2 (string table) | offset 7,856, size 421 |
| Section 3 (int array) | offset 8,288, size 0 |
| Section 4 (float array) | offset 8,288, size 0 |
| Section 5 (packed files) | offset 8,288, size 2,008,704 |

The 13 header words are read at `4 + 4*i`; the six `(offset,size)` pairs start at word 1. Sections need not
start immediately after the header (Air Paint's tree starts at 64, not 56), so consumers must use the values.
The parser now records these as `P3t::container()`, `P3t::containerVersion()` and `P3t::scene().sections`.

### 1.2 Element-type table (M)

The dump prints 92 elements and 241 attributes:

```
p3t-trace: container P3TF version 272 elements 92 attributes 241 tree
[bgimage=2,bgimagetable=1,color=1,font=1,icon=63,icontable=1,info=1,infotable=1,localizedinfo=20,theme=1]
```

Status words: **Consumed** means the native UI uses it today, **Partial** means only part of it is used, and
**Ignored** means the generic tree kept it but no native code consumed it. The "After" column records what this
slice changed.

| Element | Count | Attributes seen | Before this slice | After this slice | Native consumer / note |
|---|---:|---|---|---|---|
| `theme` | 1 | none | Consumed as the tree root | Consumed | `P3t::elements()`, `--p3t-dump` |
| `infotable` | 1 | none | Partial (container) | Partial | only its `info` child is read |
| `info` | 1 | `icon`, `iconsize`, `mtime`, `name`, `preview`, `previewsize` | Partial: `name`/`author` consumed, `preview` decoded only by `--p3t-dump`; comment/url/genre/version/icon/authoricon had no typed access | Partial, more exposed: all text fields plus `themeIcon()` / `themeAuthorIcon()` | `name()`, `author()`, `preview()`, new getters; see section 2 |
| `localizedinfo` | 20 | `locale`, `name` | Ignored by the UI (generic tree only) | Exposed by `localizedNames()` | no language setting in the native UI |
| `icontable` | 1 | none | Partial | Partial | container |
| `icon` | 63 | `id`, `size`, `src` | Consumed | Consumed | `icons()` -> `Shell::Theme::icons` -> `View` theme-icon lookup; all 63 GIM pictures are 65,664 bytes inflated |
| `bgimagetable` | 1 | none | Partial | Partial | container |
| `bgimage` | 2 | `hd`, `sd` | Partial: the first `hd` (else `sd`) is the still wallpaper; the second is ignored | Partial plus `backgrounds()` exposes all of them; `background()` still returns the first | `View::loadWallpaper()`; the rule that chooses between two bgimages is open (F) |
| `bgimage` + `anim` | 0 | - | Ignored: `dynamic()` only answered yes/no | Parsed by `scene()`; still not drawn | RAF section 3 |
| `font` | 1 | `selection=0` | Ignored by the UI (generic tree only) | Exposed as `fontSelection()` / `fontSelectionName()` | wiring hunk 4.1 |
| `color` | 1 | `selection=8` | Ignored by the UI (generic tree only) | Exposed as `colorSelection()` / `colorSelectionName()` | wiring hunk 4.2 |
| `setable` | 0 | - | Consumed when present | Consumed | `sounds()` |
| `se` | 0 | `id`, `src` / `left` + `right` | Consumed, but only five roles are mapped | Consumed; `soundRoles()` lists every role, not only the five | `InteractionAudio`; see audit/INTERACTION-AUDIO.md and FIRMWARE-PARITY-GAPS A2 |
| `pointertable` | 0 | - | Ignored | Exposed | no native custom cursor, so no consumer |
| `pointer` | 0 | `id`, `src`, `base_x`, `base_y` | Ignored | Exposed as `pointers()` (image plus hotspot) | no native custom cursor |
| `notification` | 0 | `src` | Ignored | Exposed as `notification()` | `View::drawNotification` still composes the system_plugin plaque; see 4.3 |
| **total** | **92** | **241** | | | |

Air Paint carries no sound effects, no pointers and no notification picture, which is why the synthetic fixture
in section 5 matters: it exercises those three parsers with real GIM data borrowed from Air Paint's icontable.

Two detail findings behind the table:

- Air Paint's `info` names the theme `"Air Paint"`, has `mtime="20120808173344"` and no `comment`, `url`,
  `genre`, `authoricon` or `author`. The absence is normal; the getters return empty, not failure.
- The theme has **two** `bgimage` children, each with `hd` and `sd`. The native keeps only the first. Whether
  the second is a time-of-day or variant background is not stated in the file; that selection rule is F/open.

### 1.3 Attribute kinds (M)

The reader supports the same eight kinds the format uses. Air Paint exercises only four of them; the RAF
matrix shows why the float/vector/ref paths exist.

| Kind | Meaning | Air Paint | coldboot.raf | Reader handling |
|---:|---|---:|---:|---|
| 1 | int | 67 | 77 | `Attribute::Kind::Int` |
| 2 | float | 0 | 5 | `Attribute::Kind::Float` |
| 3 | string (offset into string table) | 22 | 6 | `Attribute::Kind::String` |
| 4 | int array `[index,count]` | 0 | 0 | `Attribute::Kind::Ints` |
| 5 | float array `[index,count]` | 0 | 61 | `Attribute::Kind::Floats` |
| 6 | packed file `[offset,size]` | 69 | 9 | `Attribute::Kind::File` |
| 7 | object id (offset into id table, after a 4-byte back reference) | 83 | 31 | normalised to `Kind::String` |
| 8 | reference to an object id | 0 | 27 | normalised to `Kind::String` |

The 83 kind-7 values in Air Paint are the 63 `icon id` attributes plus 20 more; by count those are most likely
the 20 localised `name` values (F - the per-element split was not printed). The reader normalises both kinds to
text, so `Element::text("id")` / `text("name")` work without a second API.

### 1.4 The appearance selections

The PSDevWiki P3T page (M-F) defines the two elements Air Paint carries:

- `<font selection="0"/>`: 0 original, 1 rounded, 2 pop.
- `<color selection="8"/>`: 0 follows the month, 1..12 pins that month's palette.

Air Paint is `font=0` (Original) and `color=8` (August), and its `info mtime` is 2012-08-08. The two agree,
which is a useful cross-check of the month mapping (F, but a strong one). The file carries **no RGB menu or
background colours**; a 4.93 theme selects a firmware palette, and the RGB values still come from
`qgl/lines`' monthly textures. "Theme colours" in this format means this selection, not literal colours.

## 2. What this slice exposes

All additions are additive: no existing signature or behaviour changed. The only behavioural change is that
`P3t::read`/`P3t::load` also accept a RAF container (`_RAF` + zlib or raw `RAFO`), because a RAF uses the same
element tree; this is what lets the existing `--p3t-dump` print a `.raf` scene tree.

New public API in `src/p3t.h`:

| API | Meaning |
|---|---|
| `comment()`, `url()`, `genre()`, `infoVersion()` | remaining `info` text fields |
| `themeIcon()`, `themeAuthorIcon()` | `info/icon` and `info/authoricon` GIMs (the theme's own pictures) |
| `localizedNames()` | `localizedinfo` locale -> localised theme name |
| `fontSelection()`, `fontSelectionName()` | raw selection and the Original/Rounded/Pop name |
| `colorSelection()`, `colorSelectionName()` | raw selection and the Original/month name |
| `pointers()` | every `pointer`: decoded GIM plus `base_x`, `base_y` |
| `notification()` | the theme's notification plaque GIM |
| `soundRoles()` | every `se` id in tree order (the existing `sounds()` already decoded every id, not only five) |
| `backgrounds()` | every `bgimage`'s `hd` (else `sd`) picture in tree order |
| `container()`, `containerVersion()` | the container facts from 1.1 |
| `hasScene()`, `scene()` | the parsed RAF scene from section 3 |

`P3t::Raf` reports the scene as data: `sections`, `version`, `sceneCount`, `camera`, `lights`, `actors`,
`models` (geometry/skeleton IDs and sizes, per-model animations with EA03 duration/fps/frames/joints),
`textures` (GTF id/type/format/width/height/bytes), `materials` (effect and the colour/normal/specular map
bindings) and `script` (VSMX id/file/bytes/version/instruction count/name count). It does **not** decode
keyframes, interpret the script or upload anything.

### 2.1 What wiring each getter would change visually

- **`colorSelection()`**: pins the monthly palette. Air Paint's `8` means August regardless of the host date.
  If the user's Theme Color is Original this changes the category-icon tint (and, when no still theme
  background is drawn, the background wave/month texture). With Air Paint's still background, only the icon
  and default-folder tint can change, because wallpaper mode bypasses the background shader. If the user has
  chosen one of the six fixed Theme Colours, that explicit choice should win; see 4.2.
- **`fontSelection()`**: switches the text face. The native `Assets` already registers three firmware styles in
  the order `RD-R-LATIN` (Rodin), `SR-R-LATIN` (Seurat), `VR-R-LATIN` (VAG Rounded) and already routes
  `appearance/font` through `Assets::fontFamily(int)`. Air Paint's `0` is the current default, so it would
  change nothing; a theme with `1`/`2` would change every label's glyph shapes and metrics. Which of Seurat and
  VAG Rounded is the firmware's "rounded" versus "pop" face is not proven locally (F; see 4.1).
- **`localizedNames()`**: the theme chooser would show the name for the active UI language instead of
  `info/name`. The native app has no UI-language setting, so today it can only use `en`/the default.
- **`themeIcon()` / `themeAuthorIcon()`**: a theme browser/detail page could show the theme's own picture and
  its author's. No such page exists; Theme Settings lists Original and the chosen theme by name.
- **`comment()` / `url()` / `genre()` / `infoVersion()`**: theme details, same missing page.
- **`notification()`**: the theme's plaque could replace the system_plugin nine-slice in
  `View::drawNotification`. That is not a one-line wire: the theme picture is a single image stretched at its
  centre (per the P3T documentation), so it needs a new centre-stretch blit with fixed corners. This is render
  work and was deliberately not attempted.
- **`pointers()`**: would set a custom host cursor with the supplied hotspot. The native app never calls
  `setCursor`, so there is no consumer and the OS cursor remains correct today.
- **`soundRoles()` / `sounds()`**: already consumed. Extra roles beyond the five documented ones would need a
  role-to-action mapping in `InteractionAudio`; Air Paint has none and no theme with extra roles is local.
- **`backgrounds()`**: a future time/variant rule could choose among the two Air Paint pictures. No consumer
  yet; using the second without that rule would be a guess.
- **`scene()`**: the RAF verdict in section 3. Parsing it changes no pixels.

## 3. RAF scenes

### 3.1 Container and timeline (M)

A dynamic theme stores the scene under `bgimage/anim` as a RAF file. The container is:

```
"_RAF" + u32 BE inflated size + zlib stream  ->  "RAFO"
RAFO: u32 magic, u32 version (272 here), six (offset,size) sections at 8:
  0 element tree, 1 object names, 2 key strings, 3 ints, 4 floats, 5 file data
```

The element tree is the same 28-byte header plus 16-byte attributes as a `.p3t`; kinds 2, 5, 7 and 8 become
important here (floats, float vectors, object ids and references). The RAF scene has one camera, up to two
lights, up to 128 actors, models with Edge geometry/skeleton/animations, GTF textures and one VSMX script
(the limits are from the RAF documentation, F; the element counts below are M).

**There is no single timeline in the RAF header.** Time arrives from two places:

1. **Animation headers.** A model's `.anim` file starts with `EA03`; bytes 4 and 8 are duration and fps, and
   the header carries joint/frame/set counts. A model element also carries `start`/`end` seconds. This is
   what `Raf::Animation` and `Raf::timelineStart/timelineEnd` report.
2. **The VSMX script.** `coldboot.jsx` (and a theme's own script) is compiled PlayStation JavaScript. It moves
   actors and lights, changes colours and starts timers; the timeline is code, not a track list. `Raf::script`
   reports only the VSMX header (version, instruction count, name count), never a decoded event list.

### 3.2 coldboot.raf, measured (M)

```
p3t-trace: container _RAF version 272 elements 46 attributes 216 tree
[_texture=6,actor=6,camera=1,file=9,file-table=1,geometry=1,light=2,material=6,material-table=1,
 model=1,model-table=1,raf=1,scene=1,script=1,skeleton=1,texture=6,texture-table=1]
p3t-trace: scene container _RAF version 272 embedded false compressed true sections 6 tree 46 / 216
scene 1 actors 6 [logo,footer,blur,logo_sd,footer_sd,blur_sd] models 1 materials 6 textures 6
lights 2 camera camera animations 0 [] timeline 0..0 script coldboot.jsx vsmx true
instructions 518 names 35
```

The element table adds the file sizes: `plane.edge` 708 B, `plane.skel` 48 B, three HD GTF textures of
486,656 B, three SD GTF textures of 57,088 B and `coldboot.jsx` 4,848 B. The camera is perspective
(`type=0`, `yfov=0.927292`, `znear=0.01`, `zfar=1000`); the lights are a point light and an ambient light;
all six materials use effect `sce01`.

`animations 0` and `timeline 0..0` are not a parser bug: coldboot.raf contains no `.anim` files. Its timing
lives in the script and in `custom_render_plugin`'s events (BootBG2 0 s, NormalBG2 4 s, ShowGUI 5.5 s,
Finished 10 s), which `src/coldboot.cpp` already hardcodes. The synthetic RAFO fixture proves the animation
path with one EA03 header: `animations 1 [anim[0..2 30fps 75fr]] timeline 0..2` (section 5).

### 3.3 Can `src/coldboot.cpp` be reused?

**Verdict: not as a renderer. It is a one-scene port, and its reusable parts are already represented in the new
`P3t::Raf` reader.** Concretely:

Reusable:

- The container/section/element-tree reading (coldboot's private `parseRaf`, lines 49-131) is the same logic
  now generalised in `P3t::read`/`readScene`; a future renderer can consume `P3t::Raf` instead of duplicating it.
- The firmware `raf.qrc` path: `vert0.vpo` + `sce01.fpo`, the camera/light/actor application and the
  cubic-bezier `Track` evaluator (coldboot.cpp 37-42, 318-361) are a working proof for the `sce01` effect.
- The GTF header interpretation for the B8 logo textures (coldboot.cpp 82-105) is a start for texture upload.

Not reusable without new work:

1. **Edge geometry.** `parsePlane` (137-157) special-cases coldboot's 4x4 plane and neither decodes the Edge
   compressed index stream nor handles segments, vertex streams (float/half/U8/X11Y11Z10), skinning or
   multiple models. A theme's `.edge` is arbitrary.
2. **Skeleton and animation.** There is no `.skel`/`.anim` decoder. `Raf` now reports their headers and
   timeline bounds, but not keyframes; skinning needs both.
3. **VSMX interpreter.** `readScript` (163-186) pattern-matches eight `name = float` assignments in
   coldboot.jsx. A theme script uses arbitrary PlayStation JavaScript: `setPosition`, `setColor`,
   `setRotation`, timers, time of day, conditionals. Rendering the scene without the script would freeze the
   actors at their initial pose.
4. **Materials and effects.** coldboot sets one effect's uniforms by hand (`sce01`). Theme materials name
   `pure_texture`, `pure_texture_alpha_0_depth_1`, `basic_lighting`, `basic_lighting_edge_lit`,
   `basic_lighting_alpha_add` and other variants. Each needs its shader, depth/alpha/additive state and
   normal/specular maps.
5. **Textures.** coldboot uploads B8 linear textures only. Theme textures are DXT1/3/5, A8R8G8B8 (possibly
   swizzled) and B8, with mipmaps; the normal and specular maps are separate material inputs.
6. **Composition.** coldboot draws three actors with alpha blending into a target and disables depth. A theme
   scene needs depth, zsort, the background/overlay split and a full-screen composite behind the XMB, plus
   the still background fallback when the scene cannot start.
7. **Scale and lifecycle.** Up to 128 actors, 64 models, 8 animations per model, 15 MB of textures; start,
   pause, skip, device loss and error fallback. coldboot is a 10-second, 3-drawn-actor, fixed-size scene.

What it would take: a new renderer module that consumes `P3t::Raf`, with (a) an Edge geometry/skeleton/anim
decoder, (b) a VSMX interpreter or a deliberately documented subset, (c) effect selection and GTF decode,
(d) composition/lifecycle. The animation runtime is the hard part, not the file format. Size **L (weeks)**,
not a slice. A static first frame for one specific theme is possible in days once geometry/texture/material
work, but a still image is not dynamic-theme support and should not be shipped as if it were; a half-rendered
animation would be worse than the current honest fallback to the still background.

## 4. Exact wiring hunks (outside the write fence)

These are proposals only. They were not applied; `src/shell.cpp`, `src/shell.h`, `src/view.cpp` and
`src/settings_menu.cpp` were read-only for this slice. Line numbers are as read on 2026-10-27 and will drift.

### 4.1 Font selection

The theme's PS3 font selection maps to the already-registered style slots. The safest console-like rule is to
seed the existing `appearance/font` preference when the theme is selected, so the user can still change Font
afterwards.

`src/shell.h` (Theme struct, near line 393):

```diff
-    struct Theme { QString path,name; QImage background; QHash<QString,QImage> icons; };
+    struct Theme { QString path,name; QImage background; QHash<QString,QImage> icons; int fontSelection=-1,colorSelection=-1; };
```

`src/shell.cpp` (`Shell::loadTheme`, after the icons line at 2409):

```diff
     theme_.background=p3t.background();theme_.icons=p3t.icons();
+    theme_.fontSelection=p3t.fontSelection();theme_.colorSelection=p3t.colorSelection();
+    // PS3 themes install their font selection; seed the existing preference so Font can still be changed.
+    if(p3t.fontSelection()>=0)preferences_.setValue("appearance/font",p3t.fontSelection());
     interactionAudio_.setThemeSounds(p3t.sounds());
```

`preferences_.setValue` emits `changed`, which calls `applyPreferences`; `loadTheme` returns early because the
path is already current, so this does not recurse. The alternative is a direct override in
`src/settings_menu.cpp:183`, which makes the theme win for as long as it is selected:

```diff
-QString Shell::fontFamily()const{return assets_.fontFamily(preferences_.value("appearance/font").toInt());}
+QString Shell::fontFamily()const{
+    const int themeFont=theme_.fontSelection;
+    return assets_.fontFamily(themeFont>=0?themeFont:preferences_.value("appearance/font").toInt());
+}
```

Caveat (F): `Assets::fontFamily(1)` is Seurat and `fontFamily(2)` is VAG Rounded. The firmware calls the
options "rounded" and "pop"; which slot is which needs one console theme/face A/B. Air Paint selects 0, so
this hunk changes nothing for the bundled theme.

### 4.2 Colour selection

A theme colour is a palette pin, not an RGB value. Apply it only while the user's Theme Color is Original, so
an explicit user colour still wins.

`src/shell.h` (public accessor, near line 130):

```diff
     QImage themeIcon(const QString& id) const { return id.isEmpty()?QImage{}:theme_.icons.value(id); }
+    int themeColorSelection() const { return theme_.colorSelection; }
```

`src/view.cpp` (`View::renderBackground`). First the tint:

```diff
     const int selectedColour=std::clamp(shell_.theme(),0,5);
+    const int themeColour=shell_.themeColorSelection();
     QVector3D iconTint=colours[selectedColour];
     if(selectedColour==0) {
@@
-        const float fraction=float(date.day()-1)/date.daysInMonth();
-        iconTint=monthTint(date.month())*(1-fraction)+monthTint(date.month()%12+1)*fraction;
+        if(themeColour>=1 && themeColour<=12)iconTint=monthTint(themeColour);
+        else {
+            const float fraction=float(date.day()-1)/date.daysInMonth();
+            iconTint=monthTint(date.month())*(1-fraction)+monthTint(date.month()%12+1)*fraction;
+        }
     }
```

Then the background month (same function, near line 404):

```diff
         if(shell_.theme()>0 && shell_.theme()<int(std::size(fixedMonth)))
             when=QDateTime(QDate(when.date().year(),fixedMonth[shell_.theme()],1),when.time());
+        else if(themeColour>=1 && themeColour<=12)
+            when=QDateTime(QDate(when.date().year(),themeColour,1),when.time());
         if(when.date().month()!=texturesMonth_)loadMonthTextures(when.date().month());
```

For Air Paint `colorSelection=8`, this pins August: the category icons take August's palette, and, when the
XMB wave is drawn instead of the theme wallpaper, the month texture follows. With Air Paint's still
background, the visible change is the icon/folder tint only. Not applied, so the capture stays pixel-stable.

### 4.3 Notification picture (data plumbing only; do not wire the draw yet)

The theme's notification picture is a single plaque stretched at its centre, not a nine-slice; replacing
`drawNineSlice(p,"system_plugin","tex_notification",...)` in `View::drawNotification` is rendering work.
The data plumbing, if that work is scheduled:

```diff
 // src/shell.h Theme
-    struct Theme { QString path,name; QImage background; QHash<QString,QImage> icons; int fontSelection=-1,colorSelection=-1; };
+    struct Theme { QString path,name; QImage background; QHash<QString,QImage> icons; int fontSelection=-1,colorSelection=-1; QImage notification; };
+    const QImage& themeNotification() const { return theme_.notification; }
 // src/shell.cpp loadTheme
+    theme_.notification=p3t.notification();
```

### 4.4 No consumer yet

`localizedNames()`, `themeIcon()`, `themeAuthorIcon()`, `comment()`, `url()`, `genre()`, `infoVersion()`,
`pointers()` and `backgrounds()` have no existing surface to wire into. They are exposed so a theme browser,
a language setting or a time-of-day background rule can use them without parsing the file again. Adding those
surfaces is UI work, not a one-line hunk.

## 5. Verification

### 5.1 Build (M)

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-themes
...
[8/8] Linking CXX executable re-ps3-native.exe
Built C:\dev\out\native-themes\re-ps3-native.exe
```

The only warnings are pre-existing ones in `main.cpp` (`_wfopen` C4996) and other files; `p3t.cpp` builds
without warnings.

### 5.2 Air Paint dump (M)

```
$env:XMB_P3T_TRACE='1'
& C:\dev\out\native-themes\re-ps3-native.exe --p3t-dump
  "C:\dev\rpcs3\bin\dev_flash\vsh\resource\theme\01.p3t@C:\dev\out\theme-dump-final3"

p3t-trace: container P3TF version 272 elements 92 attributes 241 tree
[bgimage=2,bgimagetable=1,color=1,font=1,icon=63,icontable=1,info=1,infotable=1,localizedinfo=20,theme=1]
p3t-trace: info name Air Paint author  comment  url  genre  infoVersion  localized 20
[da,de,en,en-gb,es,fi,fr,it,jp,ko,nl,no,pl,pt,pt-br,ru,sv,tr,zh_s,zh_t] themeIcon 64x64
authorIcon 0x0 preview 480x270
p3t-trace: font 0 (Original) color 8 (August) roles [] pointers [] notification 0x0
p3t-trace: scene none
p3t: Air Paint by  - 92 elements, background QSize(1920, 1080)  , 63 icons, 0 sounds
```

The dump's `elements.txt` is byte-identical to the pre-change dump (SHA-256 match), which is the parser-only
proof: the element tree did not move.

### 5.3 coldboot.raf dump (M)

```
& C:\dev\out\native-themes\re-ps3-native.exe --p3t-dump
  "C:\dev\rpcs3\bin\dev_flash\vsh\resource\coldboot.raf@C:\dev\out\raf-dump-final3"
```

The full trace is quoted in 3.2. The elements file lists 46 elements, including all 6 actors, the camera, both
lights, the `plane` model with `plane.edge`/`plane.skel`, all 6 `sce01` materials, all 6 GTF textures, and
`coldboot.jsx`.

### 5.4 Synthetic coverage fixtures (M)

Built locally by `C:\dev\out\make-theme-fixture.ts` and `C:\dev\out\make-raf-fixture.ts` (outside the repo):

```
p3t-trace: info name Fixture Theme author Native XMB audit comment Parser coverage fixture
url https://example.invalid/theme genre Audit infoVersion 1.0 localized 2 [en,fr]
themeIcon 128x128 authorIcon 128x128 preview 0x0
p3t-trace: font 1 (Rounded) color 10 (October) roles [se_cursor,se_decide]
pointers [pointer_arrow@20,5(128x128)] notification 128x128
p3t-trace: scene none
p3t: Fixture Theme by Native XMB audit - 13 elements, background QSize(0, 0)  , 0 icons, 2 sounds
```

That exercises the infotable text fields, localized names, font/colour selections, pointer hotspot and image,
notification image and two sound roles with decoded WAVs (100 bytes each). The RAF fixture prints:

```
p3t-trace: scene container RAFO version 1 embedded false compressed false sections 6 tree 11 / 21
scene 1 actors 1 [actor] models 1 materials 1 textures 0 lights 0 camera
animations 1 [anim[0..2 30fps 75fr]] timeline 0..2 script none vsmx false instructions 0 names 0
```

This proves the `EA03` header and `start`/`end` timeline path. It is synthetic; no real dynamic-theme RAF is
local, which is the main limitation in section 6.

### 5.5 Pixel gate (M)

```
cd C:\dev\xmb
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture
  --exe C:/dev/out/native-themes/re-ps3-native.exe

layer              crop                PSNR     base   delta    SSIM    base    delta  gate
frame              0,0,1920,1080      18.28    18.02   +0.26  0.8285  0.8301  -0.0016  PASS
top background     0,0,700,200        20.22    20.24   -0.01  0.9336  0.9344  -0.0008  PASS
category icon row  0,240,1920,120     20.89    21.05   -0.16  0.8939  0.8950  -0.0011  PASS
item label row     400,425,800,100    11.94    11.94   +0.00  0.3909  0.3910  -0.0001  PASS
clock text         1570,80,260,50     11.39    11.39   +0.00  0.4105  0.4105  +0.0000  PASS
info bar           1329,75,591,58     14.78    14.78   +0.00  0.6117  0.6117  +0.0000  PASS

PASS: 6/6 layers within budget
```

The parser-only change does not move pixels through a theme: no theme is selected in the canonical capture, so
none of the new getters is called by the draw path. The small frame/row deltas are inside the existing 0.50 dB /
0.0040 SSIM budget.

## 6. Limits of this slice

- **Only one real theme and one real RAF are local.** Air Paint is a static theme; coldboot.raf is the boot
  intro, not a dynamic theme. The RAF reader and animation-header path are proven on those plus synthetic
  fixtures, but no real dynamic-theme RAF was parsed. A user theme with `bgimage/anim` should be the next
  test input before trusting the scene numbers.
- **Font style identity is F.** The three registered styles are Rodin, Seurat and VAG Rounded; the firmware's
  "rounded"/"pop" labels are not mapped to them by a local trace. Air Paint selects Original, so the bundled
  case is safe; a theme selecting 1 or 2 needs a console A/B.
- **The colour selection mapping is F from the PSDevWiki**, cross-checked only by Air Paint's `mtime` and
  `selection=8`. How strongly the filter applies to the side menu, default icons and folder tint is not
  measured.
- **No language setting exists**, so `localizedNames()` is data without a consumer. The same is true for the
  theme/author icons and the two bgimages.
- **`XMB_P3T_TRACE` is a diagnostic**, not a normal logging path. It decodes the preview, theme icons,
  pointers and notification when set; do not enable it in timed runs. It is read through the existing
  `--p3t-dump` CLI so no `main.cpp` change was needed.
- **The parser now accepts a `.raf` as a theme container.** No UI path offers one (the file dialog filters
  `*.p3t`); if one is forced, `P3t::load` returns a tree with no `info`/`bgimage`, and `Shell::loadTheme`
  treats it as a theme with no name/background. The intended use is `--p3t-dump` analysis only.
- **No rendering research was attempted.** The RAF section is a feasibility verdict, not a renderer. The
  still-background fallback in the shell remains the honest behaviour for dynamic themes.

## 7. References

Local, read for this slice:

- `src/p3t.cpp`, `src/p3t.h` - the reader and the new getters.
- `src/shell.cpp` (`Shell::loadTheme`, 2400-2412), `src/shell.h` (Theme, 127-130, 392-394).
- `src/view.cpp` (`View::renderBackground`, 368-429; `View::drawNotification`, 953-1002).
- `src/settings_menu.cpp:183` (`Shell::fontFamily`), `src/assets.cpp:90-92` (three font styles).
- `src/coldboot.cpp` (`parseRaf` 49-131, `parsePlane` 137-157, `readScript` 163-186, `render` 318-361).
- `audit/FIRMWARE-PARITY-GAPS.md` rows B6 and A2; `HANDOFF-PHASES.md` themes row; `audit/MAXMB-INTEGRATION.md`;
  `audit/INTERACTION-AUDIO.md`.

External, consulted for the format semantics (the first two through the Internet Archive because the live wiki
blocks scripted fetches):

- PS3 Developer Wiki, "PlayStation 3 Theme (P3T)":
  https://web.archive.org/web/20240116124452/https://www.psdevwiki.com/ps3/PlayStation_3_Theme_(P3T)
  (font 0/1/2, colour 0/1..12, pointer `base_x`/`base_y`, notification stretch, setable roles).
- PS3 Developer Wiki, "Rich Appearance Format (RAF)":
  https://web.archive.org/web/20250313171103/https://www.psdevwiki.com/ps3/Rich_Appearance_Format_(RAF)
  (container sections, camera/lights/actors/models/animations/textures, VSMX script, scene limits).
- PS3 Developer Wiki, "XMB Fonts":
  https://web.archive.org/web/20250313170940/https://www.psdevwiki.com/ps3/XMB_Fonts
  (Rodin, Seurat, VAG Rounded; the original/rounded/pop setting).
- DeckyXMB `py_modules/p3t.py` and `py_modules/raf.py` (BSD 3-Clause), the reader lineage:
  https://raw.githubusercontent.com/Callum-Bell/DeckyXMB/main/py_modules/p3t.py and
  https://raw.githubusercontent.com/Callum-Bell/DeckyXMB/main/py_modules/raf.py
- MaXMB `src-tauri/src/p3t.rs` (MIT), the native reader's direct port source:
  https://github.com/MaxiBono/MaXMB

## 8. Theme pictures now use the shared GIM decoder (2026-10-27)

Sections 1-7 measured the theme reader as it was. One decoder finding remained: `P3t::gim` was a second,
weaker GIM parser. It returned a null image whenever an indexed picture (format 0x4/0x5/0x6) had no palette
block, which is exactly the palette-less coverage-mask form the RCO decoder reads for the 4.93 OSK masks
(`audit/TEXTURE-COVERAGE.md` 1.3). A user theme whose icon, pointer or notification is such a mask therefore
rendered nothing, even though the same bytes in an RCO decoded.

### 8.1 Change - M

`P3t::gim` now delegates to `Assets::decodeGimImage`, so there is one GIM reader in the process. The private
parser and its two helpers (`unswizzle`, `expand`) were removed. The shared decoder covers what the theme
reader did (RGBA8888, INDEX4/8/16 with a palette) and adds the palette-less indexed mask form plus DXT1/3/5.
A payload that is not a readable GIM returns a null image with `assets: GIM picture skipped - <reason>` rather
than failing silently; empty attributes (a theme without a preview, say) still return null without a warning.

The one behaviour the private reader had and the shared decoder does not is little-endian PSP GIM ("MIG.");
PS3 themes and RCOs are big-endian `.GIM1.00`. No local theme carries a little-endian picture, so this is a
documented removal rather than a measured regression.

### 8.2 Air Paint and a mask fixture - M/R

`vsh/resource/theme/01.p3t` is still the only retail theme local. It carries 65 GIM pictures, all format 0x3:

```
$env:XMB_P3T_TRACE='1'
re-ps3-native.exe --p3t-dump "C:\dev\rpcs3\bin\dev_flash\vsh\resource\theme\01.p3t@<dir>"
```

| Picture slots | Before | After |
|---|---:|---:|
| 63 icons + `info/icon` 64x64 + `info/preview` 480x270 | 65 / 65 | 65 / 65 |
| 2 backgrounds (PNG/JPEG through `QImage::fromData`) | 2 / 2 | 2 / 2 |

The 65 exported PNGs of the before and after dumps are pixel-identical; delegating does not move real theme
pixels. The theme cannot exercise the bug (0 indexed pictures), so a local fixture built from real firmware
data does: `out/make-theme-mask-fixture.ts` packs the extracted `LABEL_UP`, `KEYTOP_COPY`,
`MODE_GUIDE_ALPHABET`, `LIST_WINDOW_FOCUS` and `EN_LABEL_SPACE` format-0x5 masks (all palette-less) plus one
genuine Air Paint format-0x3 icon into a P3TF theme.

| Mask fixture picture slots | Before | After |
|---|---:|---:|
| 4 icons + `info/icon` + `info/authoricon` + pointer + notification | **1 / 8** | **8 / 8** |

The after trace reads `themeIcon 72x54 authorIcon 42x42`, `pointer_arrow@7,9(140x44)` and
`notification 29x30`; the decoded `icon_mask_label_up.png` is white with 15 alpha levels and is
pixel-identical to the same firmware mask exported through the RCO path. Counts and raw log lines are in
`audit/TEXTURE-COVERAGE.md` 7.2.

### 8.3 What is still open

* No real theme with a palette-less indexed picture is local; the fix is proven on real firmware pictures in a
  synthetic P3TF container.
* A palette-bearing indexed theme picture (a type-0x05 palette block beside the image) has no local example
  either; the shared decoder's palette path is proven only by the synthetic fixture in `audit/GIM-FORMATS.md`.
* Little-endian PSP GIMs no longer decode (8.1). No local theme uses that form.
* The theme's decoded pictures still reach only the surfaces section 3 lists: background, icons, preview and,
  through the dump, the notification/pointer pictures. This change fixes what is decodable, not which surface
  draws it.

