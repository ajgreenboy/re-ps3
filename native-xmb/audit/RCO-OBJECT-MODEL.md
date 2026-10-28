# RCO object model and materials: the runtime reader (P1)

Scope: P1.1 (RCO object model), P1.3 (materials from MNU/override files) and the generic layer the later
parity phases read instead of hardcoding. P1.2 (XMBML view types), P1.4 (fonts/glyphs) and P1.5 (texture
decode parity) are separate items and are only referenced here where they consume this reader.

Method: the reader parses the firmware's bytes at run time; nothing below is a copied value. Every number,
name, string and reference in the dumps is decoded from `dev_flash/vsh/resource/*.rco` and from the MNU
files inside the QGL archives. Firmware data stays out of the repository: the dumps quote object names,
property values and a few English strings only.

Evidence classes used here:

- **RCO** - the reader's own output (`--rco-tree`) or the object tables in the container.
- **MNU** - a material file read through `Assets::qglFile` (the QGL/QRCF archive path) or a preset under
  `out/native-reference/lines/` for cross-checking when this document was written.
- **NATIVE** - the current C++ implementation, cited by file and symbol (line numbers shift because the
  tree is shared with other agents).
- **UNPROVEN** - nothing local decides it; section 10 says what would.

---

## 1. What P1.1/P1.3 now have

| Item | State after this task |
|---|---|
| P1.1 object model | `src/rco_objects.{h,cpp}`: every object and animation entry of any `vsh/resource/*.rco`, with type, name, parent/children, depth, offset, stored size and typed properties |
| P1.1 "transforms the console applies" | Not applied here: the reader exposes the stored `posX/posY/posZ/scale*/redScale...` values and the parent tree; the transform composition is P2's job and consumes these fields |
| P1.3 ICONS.mnu / PARTICLES.mnu / background presets | `Materials` (same header): one API for `icons()`, `particles()`, `particlesUi()`, `particlesSpe()`, `background()`, `line1()`, `hdr()` and any `mnu(archive,path)` |
| P1.3 text material | The firmware has **no text MNU** in the local tree (section 5.3); the text material is the RCO `Text` object's own fields (`size`, gradient colours, spacing, shadow), exposed by `Materials::textMaterial()` |
| Generic dump for later phases | `--rco-tree <container>[,<container>...][@file]`, `*` for every RCO (section 6) |

The five containers this document treats as the local XMB set are `explore_plugin_full`, `system_plugin`,
`msgdialog_plugin`, `xmb_plugin_normal` and `sysconf_plugin`; section 6 dumps the first four (the fifth has
2247 objects and is quoted by counts only). Their header facts, as the reader decodes them:

| Container | Version | Compression | File bytes | Table bytes | Objects | Animations |
|---|---|---|---|---|---|---|
| `explore_plugin_full` | 0x130 | 1 (zlib body) | 2421192 | 333812 | 87 | 0 |
| `system_plugin` | 0x130 | 1 (zlib body) | 296356 | 62020 | 59 | 17 |
| `msgdialog_plugin` | 0x130 | 0 (raw) | 257900 | 257900 | 2 | 0 |
| `xmb_plugin_normal` | 0x130 | 1 (zlib body) | 19724 | 7208 | 26 | 0 |
| `sysconf_plugin` | 0x130 | 1 (zlib body) | 1440648 | 1082572 | 2247 | 0 |

---

## 2. The container format the reader decodes

The RCO is big-endian. The reader uses the same header and the same compressed-body rule as
`Assets::readContainer` (the existing asset path), so an RCO opened twice is decoded identically; it reads
the file itself only because `Assets` does not expose the object/image trees.

| Header offset | Meaning | Reader use |
|---|---|---|
| 0x00 | magic `FRP\0` (0x46525000) | reject anything else |
| 0x04 | version (0x130 in the 4.93 tree) | reported |
| 0x0C | compression nibble at bits 4..7: 0 raw, 1 zlib | selects the body rule |
| 0x10 | offset of the packed-section header (0xa4) | - |
| 0x18 | language tree root | text table |
| 0x24 | image tree root (0xFFFFFFFF when the RCO has no images) | image names |
| 0x2C | font tree root (0xFFFFFFFF when the RCO has no fonts) | `font` reference names |
| 0x30 | object tree root (0xFFFFFFFF when empty) | object walk |
| 0x34 | animation tree root (0xFFFFFFFF when empty) | animation walk |
| 0x38 / 0x3C | localized text payload offset / length (in the file) | text strings |
| 0x40 / 0x44 | label section offset / length (in the table section) | object, image and text-row names |
| 0xA4 / 0xA8 / 0xB0 | packed size / expanded size / zlib stream | body inflate |

For a compressed RCO the table section is `file[0..0xA4) + inflate(file[0xB0..0xB0+packed))`; every
offset in the remainder of this document is relative to that layout, not to the file. `msgdialog_plugin`
is stored raw, so its table section is the file itself - both paths are exercised by the dumps.

---

## 3. Object and animation trees

### 3.1 Entry layout

Each entry starts with a 40-byte header; the stored bytes after it are the entry's typed property block.

| Entry offset | Size | Meaning | Reader use |
|---|---|---|---|
| +0 | u16 | type id: high byte 0x08 objects, 0x09 animations; low byte indexes the attribute schema | type name and property layout |
| +4 | u32 | label-section offset of the entry name (0xFFFFFFFF = anonymous) | `name` |
| +12 | u32 | stored size in bytes, 0 = use 40 + schema bytes | entry extent |
| +16 | u32 | direct child count | walk |
| +20 | u32 | next-sibling distance (0 = the next entry starts at this subtree's end) | walk |
| +40 | .. | property block, `stored size - 40` bytes | typed properties |

The header words at +2, +8 and +24..+39 are not interpreted; the walk does not need them.

### 3.2 The attribute schema

Property layout comes from the format's attribute definition (Rcomage's `objattribdef-ps3.ini` /
`animattribdef-ps3.ini`, MIT). The schema is a *format* definition - field order and field width per type
id - not firmware data; every value is read from the entry's bytes. It is embedded in
`rco_objects.cpp` as a generated table (`work/rco-schema-gen.ts` regenerates it) because the local
firmware ships no schema of its own.

- 32 object types (0x800 ObjectTree, 0x801 Page, 0x802 Plane, 0x803 Button, 0x804 XMenu, 0x806 XList,
  0x807 Progress, 0x808 Scroll, 0x809 MList, 0x80C XItem, 0x80D Text, 0x80E ModelObject, 0x80F Spin,
  0x812 Group, 0x816 Clock, 0x819 Icon, ... 0x81F EditBox) and 12 animation types (0x900 AnimTree,
  0x901 Animation, 0x902 MoveTo, 0x903 Recolour, 0x904 Rotate, 0x905 Resize, 0x906 Fade, 0x907 Delay,
  0x908 FireEvent, 0x909 Lock, 0x90A Unlock, 0x90B SlideOut).
- 728 object fields and 43 animation fields in total. `RcoObjects::objectTypes()` returns the list;
  `RcoObjects::schemaSource()` states the provenance.

### 3.3 Walk rules and bounds

Children start at `entry + storedSize`; each child's next sibling is found by adding its own +20 distance,
and when that distance is 0 the next child begins where the previous child's subtree ended. A malformed
tree cannot run away: the reader bounds every step at 64 MiB file, 32 MiB table section, 100000 entries,
depth 128, 65536 children, 65536 text rows, 4096 images and 64 languages. A structural violation fails that
container (`Container::ok == false`, `error` set) instead of returning a partial tree; the failure is
cached, so a broken container is not re-read.

### 3.4 Types found in the XMB-relevant containers

Counted by the reader from the object trees (same counts the local inventory reports):

| Container | Object types (count) |
|---|---|
| `explore_plugin_full` | ObjectTree 1, Page 15, Plane 36, MList 6, Button 19, Text 6, Progress 1, Meter 1, CheckboxGroup 1, Scroll 1 |
| `system_plugin` | ObjectTree 1, Page 9, Plane 29, Text 19, Group 1; animations: AnimTree 1, Animation 8, Fade 8 |
| `msgdialog_plugin` | ObjectTree 1, Page 1 |
| `xmb_plugin_normal` | ObjectTree 1, Page 4, XMenu 1, XMList 10, Plane 8, Text 2 |
| `sysconf_plugin` | ObjectTree 1, Page 331, Plane 588, XList 16, MList 161, MItem 761, Text 218, Scroll 30, IList 17, IItem 54, CheckboxGroup 9, Button 17, Group 6, EditBox 11, Progress 15, Clock 5, Spin 4, ItemSpin 1, Edit 2 |
| `explore_category_*` | ObjectTree 1, Page 7-11, Plane 10-29, MList 6, and per category Button/Text/Progress/Meter/CheckboxGroup/Scroll |

The whole 122-RCO inventory holds 8070 object entries and 165 animation entries; the reader parses all of
them with zero failures (section 7).

---

## 4. Typed property model

### 4.1 Storage types

| Letter | Schema type | Width | `Value` | Dump form |
|---|---|---|---|---|
| `f` | float | 4 | `kind=Float`, `real` | `%g` 8 significant digits |
| `i` | int | 4 | `kind=Int`, signed 32-bit in `integer` | decimal |
| `u` | unk | 4 | `kind=Unknown`, raw 32-bit in `integer` | `0x` hex |
| `r` | ref | 8 | `kind=Ref`, tag + target | resolved (4.2) |
| `e` | event | 8 | `kind=Event` | resolved (4.2) |
| `g` | image | 8 | `kind=Image` | image name |
| `t` | text | 8 | `kind=Text` | `text#N:label="string"` |
| `m` | model | 8 | `kind=Model` | resolved |
| `o` | object | 8 | `kind=Object` | object name |

`u` is kept raw because the attribute definition does not say what those words mean; printing them as
floats would invent a type. The entry bytes past the schema fields are exposed as
`Container::floats(entry)` (the "array"/tail view) and the dump prints them as `tail=[...]` - that is where
an entry stores more than its schema names (animation payloads, for example).

### 4.2 References

An 8-byte reference is two big-endian u32 words. The first is a table tag; the second is an offset or index
inside that table. Decoding was established against known objects, not guessed:

| Tag | `RefKind` | Target space | Evidence |
|---|---|---|---|
| `0x0401` | Text | index into the text table | `text_wait.text` -> `text#313` -> label `msg_wait` = "Please wait..." ; `cmdbtn_select_all.unknownRef29` -> `text#291` -> `msg_select_all` |
| `0x0402` | Image | absolute offset of an image-tree entry | `cmdbtn_select_all.image` = 0x16C -> `tex_button`; `.shadow` = 0x1E4 -> `tex_button_shadow`; `.image2` = 0x1A8 -> `tex_button_focus` |
| `0x0405` | Font | absolute offset of a font-tree entry | `hknw_plugin` `hknw_guidepanel_text_L.font` = 0x1050 -> `fontstyle_light` |
| `0x0407` | Object | absolute offset of an object-tree entry | `poweroff_select.unknownRef47` = 0x39B08 -> `poweroff_yes`; `netconf_plugin` `unknownObject45` fields |
| `0x0409` | Object | object-tree entry, animation scope | `system_plugin` `anim_cross_show` -> Fade `object` = 0x91B8 -> `cross_shadow` (0x91B8 = 37304) |
| `0x0400` | Id | not identified locally (4.5) | raw value kept |
| `0xFFFF0000`/`0xFFFFFFFF` | Null | unset | every unused `text`/`font`/event field |

`Container::resolve()` writes the human-readable target: image name, object name, `text#N:label`, `id:0xN`,
`null`, or `ref:tag:value` for anything else. Consumers that need structure use `Value::refKind()` and
`Value::target`.

### 4.3 Text table

The language tree is at 0x18. Its first entry with kind 1 carries the text index: `u16 format` (0 UTF-8,
1 UTF-16BE, 2 UTF-32BE), `u32 rowCount`, then `rowCount` rows of `{u32 labelOffset, u32 length, u32
offset}`. The English payload is the language chunk with id 1 in the 0x38/0x3C section (zlib per language in
a compressed RCO). A row's `label` is its label-section name; its `text` is the decoded English string -
this is the same data `Assets::text()` returns, with the row index exposed so a Text object's `text`
property can be resolved without guessing a label.

### 4.4 Image table

The image tree is at 0x24: 0x0401 entries, label at +4, next-sibling distance at +20, first entry at
root+0x28. `Container::imageName(offset)` maps a reference's target to the image name; `images()` returns
every entry. The pixel bytes are not decoded here (P1.5); `Assets::image()` already does that by name.

### 4.5 The unresolved 0x0400 tag

Tag 0x0400 carries values that are neither text indices, label offsets nor object offsets in the
containers where this was checked:

- `xmb_plugin_normal`: six events and one ref use 0x0400 with values 4, 0x18, 0x2C, 0x48, 0x7C and 0x60.
  Its text table has 6 rows, so 4 could be an index but 0x18/0x2C/0x48/0x7C/0x60 cannot; the label
  section is 0x548 bytes at 0x196C and `label(0x60)` is "k_to_browser", a suffix of a longer name, so it is
  not a label offset either; the object tree starts at 0xC20, so 0x60 is not an object offset.
- `explore_plugin_full`: the same tag appears on XMenu `unknownRef*` fields with values that do land
  inside its text table (e.g. 0x38 -> `msg_psp`), which is why the tag is *not* mapped to Text on that
  coincidence alone.

The reader therefore reports these as `Id` with the raw target. Nothing in this task depends on them; the
events (`onLoad`, `onCancel`, ...) are kept as opaque ids so a later trace can attach meaning without the
reader having invented one.

---

## 5. Materials and constants reader

### 5.1 Where the MNU files live

`.mnu` files are entries inside the QGL archives (`vsh/resource/qgl/*.qrc`), not in `dev_flash` itself:

| Archive | Files |
|---|---|
| `icons.qrc` | `ICONS.mnu`, `override/<preset>/ICONS.mnu` for 21 presets (black, bright, coldboot1-5, day, gameboot1-5, higure, initial_setting, music_1-2, night, welcome_1-2, yoake) |
| `lines.qrc` | `BACKGROUND.mnu`, `LINE1.mnu`, `PARTICLES.mnu`, `PARTICLES_UI.mnu`, `PARTICLES_SPE.mnu`, `HDR.mnu`, and per override preset (black, bright, coldboot1-2, day, gameboot1-5, higure, initial_setting, music_1, night, welcome_1-2, yoake) the four it carries |

Format: `#MNU_1.0`, then `NAME:type:value` per line; `float` and `int` are numeric, any other type is kept
as text. Names are verbatim (uppercase for ICONS/HDR/BACKGROUND, lowercase for PARTICLES), so a consumer
uses the firmware's own key.

### 5.2 API

`Materials` (declared with the object reader, implemented in the same pair):

```cpp
Materials materials(assets, objects);
const Materials::Mnu& icons       = materials.icons("night");     // override/night/ICONS.mnu
const Materials::Mnu& particles   = materials.particles("day");   // override/day/PARTICLES.mnu
const Materials::Mnu& ui          = materials.particlesUi();      // PARTICLES_UI.mnu
const Materials::Mnu& spe         = materials.particlesSpe();     // PARTICLES_SPE.mnu
double  specular = particles.number("specular power", 0.0);       // absent -> the caller's fallback
QStringList keys = particles.keys();                              // stored order
QStringList presets = materials.presets("lines", "PARTICLES.mnu"); // override sets that carry one
```

Every read goes through `Assets::qglFile` and is cached per `(archive, path)`; an absent key is absent
(the caller supplies the fallback), so "the firmware does not set this" is distinguishable from "the
firmware sets it to N".

### 5.3 The text material

There is **no text material MNU** in the local firmware. Every `.mnu` entry in all eight QGL archives was
enumerated for this task (`icons`, `lines`, `icontex`, `raf`, `rhm`, `store`, `canyon`, `earth`): the only
files are the ones listed in 5.1 plus the store/panel, canyon and earth material sets. The firmware's text
material is carried by each RCO `Text` object: `size`, `topRed/topGreen/topBlue`,
`bottomRed/bottomGreen/bottomBlue`, `spacingHorizontal`, `spacingVertical`, `shadowX/shadowY`,
`shadowPerspective`, `shadowRed/shadowGreen/shadowBlue/shadowAlpha`, and the `font` reference.
`Materials::textMaterial(container, object)` returns exactly those fields
(`TextMaterial{size, top, bottom, spacingHorizontal, spacingVertical, shadow, shadowX, shadowY,
shadowPerspective, fontReference}`).

Measured example - `system_plugin` `page_btnnavi/btnbar/cross_shadow/cross_label`: `size=0` (the plugin
default), gradient RGB (1,1,1), spacing 0, shadow offset (4,-5), shadow colour (0.25,0.25,0.25) alpha 1.
That is the material behind the audit's "shadow offset 4/-5 at 0.25" note in `DIALOGS-FIRMWARE.md`.

---

## 6. The dump CLI

```
--rco-tree <container>[,<container>...][@file]
```

`*` (or `all`) dumps every `vsh/resource/*.rco`. Without `@file` each line goes through `qInfo` (set
`XMB_LOG=<file>` to capture it, as with the other analysis CLIs); with `@file` the text is written there.
Each container starts with a header line (version, compression, file/table bytes, entry counts, measured
read+parse time) and the run ends with a summary line. `--rco-images` and `--rco-text` are unchanged.

Line format:

```
<2 spaces per depth><Type> <name> @<table offset> +<property bytes>B <field>=<value> ...
```

Names are label-section names (`(anon)` when an entry has none); values are typed as in section 4;
references are resolved (`image=tex_cross`, `text#313:msg_wait="Please wait..."`,
`object=circle_shadow`, `font=fontstyle_light`, `id:0x38`, `null`); strings are escaped to ASCII
(`\uXXXX`) so a dump can be quoted in an ASCII document; bytes past the schema fields print as
`tail=[...]` floats.

### 6.1 `msgdialog_plugin` (raw, uncompressed)

```
# rco-tree msgdialog_plugin version=0x130 compression=0 file=257900 tables=257900 objects=2 animations=0 read=0.43ms
ObjectTree (anon) @16324 +0B
  Page page_msgdialog @16364 +36B unknownInt0=17891328 onInit=id:0x0 onCancel=null onContextMenu=null onActivate=id:0x20
```

### 6.2 `system_plugin` (compressed; 59 objects + 17 animations)

```
# rco-tree system_plugin version=0x130 compression=1 file=296356 tables=62020 objects=59 animations=17 read=0.89ms
ObjectTree (anon) @34596 +0B
  Page page_default_theme @34636 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page page_wallpaper_theme @34712 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane wallpaper_theme_plane @34788 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=65543 unknownInt21=65544 unknown22=0x10000 image=null unknownInt24=0
  Page page_dummyload @34928 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane dummyload1 @35004 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=65543 unknownInt21=65544 unknown22=0x10000 image=null unknownInt24=0
    Plane dummyload2 @35144 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=65543 unknownInt21=65544 unknown22=0x10000 image=null unknownInt24=0
  Page page_infobar @35284 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane infobar @35360 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0xb unknown18=0x274 unknown19=0x0 unknownInt20=7 unknownInt21=627 unknown22=0x0 image=null unknownInt24=0
      Plane infoicon_shadow @35500 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x279 unknown18=0x27a unknown19=0x0 unknownInt20=635 unknownInt21=636 unknown22=0x0 image=null unknownInt24=0
      Plane infoicon @35640 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x277 unknown18=0x278 unknown19=0x0 unknownInt20=635 unknownInt21=636 unknown22=0x0 image=null unknownInt24=0
      Text infotext1 @35780 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x27e unknown18=0x27d unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777473 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=20 unknownInt57=3 unknown58=0x0
      Text infotext2 @36052 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3276800 onLoad=null unknown17=0x281 unknown18=0x27d unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=33554433 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=20 unknownInt57=3 unknown58=0x0
  Page page_busy @36324 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane busy_icon_shadow @36400 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x294 unknown18=0x295 unknown19=0x0 unknownInt20=662 unknownInt21=663 unknown22=0x0 image=tex_busy_shadow unknownInt24=0
    Plane busy_icon_anim1 @36540 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x290 unknown18=0x291 unknown19=0x0 unknownInt20=658 unknownInt21=659 unknown22=0x0 image=tex_busy unknownInt24=0
    Plane busy_icon @36680 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x290 unknown18=0x291 unknown19=0x0 unknownInt20=658 unknownInt21=659 unknown22=0x0 image=tex_busy unknownInt24=0
      Plane busy_short @36820 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=8 height=36 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_busy_s unknownInt24=0
      Plane busy_long @36960 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=8 height=36 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_busy_l unknownInt24=0
  Page page_btnnavi @37100 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Group btnbar @37176 +88B posX=0 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0xb unknown18=0x283 unknown19=0x0 unknownInt20=7 unknownInt21=642 unknown22=0x0
      Plane cross_shadow @37304 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x285 unknown18=0x289 unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_cross_shadow unknownInt24=0
        Plane cross_icon @37444 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x28c unknown18=0x28d unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_cross unknownInt24=0
        Text cross_label @37584 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x28e unknown18=0x28f unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777217 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Plane circle_shadow @37856 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x286 unknown18=0x289 unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_circle_shadow unknownInt24=0
        Plane circle_icon @37996 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x28c unknown18=0x28d unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_circle unknownInt24=0
        Text circle_label @38136 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x28e unknown18=0x28f unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777217 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Plane triangle_shadow @38408 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x287 unknown18=0x289 unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_triangle_shadow unknownInt24=0
        Plane triangle_icon @38548 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x28c unknown18=0x28d unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_triangle unknownInt24=0
        Text triangle_label @38688 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x28e unknown18=0x28f unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777217 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Plane rectangle_shadow @38960 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x284 unknown18=0x289 unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_rectangle_shadow unknownInt24=0
        Plane rectangle_icon @39100 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x28c unknown18=0x28d unknown19=0x0 unknownInt20=650 unknownInt21=651 unknown22=0x0 image=tex_rectangle unknownInt24=0
        Text rectangle_label @39240 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x28e unknown18=0x28f unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777217 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
  Page page_notification @39512 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane notification_prerender @39588 +100B posX=0 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x4 unknown18=0x5 unknown19=0x0 unknownInt20=7 unknownInt21=8 unknown22=0x0 image=null unknownInt24=0
      Text notification_prerender_00a @39728 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_01a @40000 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_02a @40272 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_03a @40544 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_04a @40816 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_05a @41088 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_0b @41360 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_prerender_1b @41632 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=20 unknownInt57=3 unknown58=0x0
      Text notification_prerender_2b @41904 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=22 unknownInt57=3 unknown58=0x0
    Plane notification_a @42176 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=2 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0xdac unknown18=0xdad unknown19=0x0 unknownInt20=3502 unknownInt21=6294959 unknown22=0x0 image=tex_notification unknownInt24=3
      Plane notification_icon_shadow_a @42316 +100B posX=1 posY=-1 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x5e0db3 unknown18=0x5e0db4 unknown19=0x0 unknownInt20=3505 unknownInt21=3506 unknown22=0x0 image=null unknownInt24=0
      Plane notification_icon_a @42456 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0xdb3 unknown18=0xdb4 unknown19=0x0 unknownInt20=3509 unknownInt21=3510 unknown22=0x0 image=null unknownInt24=0
      Text notification_message_a @42596 +232B posX=1 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0x5f0db3 unknown18=0x610db4 unknown19=0x0 unknownInt20=3504 unknownInt21=3512 unknown22=0x0 text=null font=null unknownInt27=16843009 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=21 unknownInt57=3511 unknown58=0x0
    Plane notification_b @42868 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0xdac unknown18=0xdad unknown19=0x0 unknownInt20=3502 unknownInt21=3503 unknown22=0x0 image=tex_notification unknownInt24=3
      Plane notification_icon_shadow_b @43008 +100B posX=1 posY=-1 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x5e0db3 unknown18=0x5e0db4 unknown19=0x0 unknownInt20=3505 unknownInt21=3506 unknown22=0x0 image=null unknownInt24=0
      Plane notification_icon_b @43148 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0xdb3 unknown18=0xdb4 unknown19=0x0 unknownInt20=3509 unknownInt21=3510 unknown22=0x0 image=null unknownInt24=0
      Text notification_label0_b @43288 +232B posX=1 posY=0.5 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0x5f0db3 unknown18=0x630db4 unknown19=0x0 unknownInt20=3504 unknownInt21=21 unknown22=0x0 text=null font=null unknownInt27=16777473 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=21 unknownInt57=3 unknown58=0x0
      Text notification_label1_b @43560 +232B posX=1 posY=-0.375 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0x5f0db3 unknown18=0x630db4 unknown19=0x0 unknownInt20=3504 unknownInt21=20 unknown22=0x0 text=null font=null unknownInt27=16777473 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=20 unknownInt57=3 unknown58=0x0
      Text notification_label2_b @43832 +232B posX=1 posY=-0.5 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0x5f0db3 unknown18=0x630db4 unknown19=0x0 unknownInt20=3504 unknownInt21=22 unknown22=0x0 text=null font=null unknownInt27=16777473 unknownInt29=16777216 size=0 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=22 unknownInt57=3 unknown58=0x0
  Page page_pointcursor @44104 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane cursor @44180 +100B posX=0 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane shadow_plane @44320 +100B posX=4 posY=-5 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=-65536
      Plane cursor_plane @44460 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=-65536
  Page page_autooff_guide @44600 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
AnimTree (anon) @44676 +0B
  Animation anim_circle_hide @44716 +0B
    Fade (anon) @44756 +20B object=circle_shadow duration=100 accelMode=0 transparency=0
  Animation anim_circle_show @44816 +0B
    Fade (anon) @44856 +20B object=circle_shadow duration=100 accelMode=0 transparency=1
  Animation anim_cross_hide @44916 +0B
    Fade (anon) @44956 +20B object=cross_shadow duration=100 accelMode=0 transparency=0
  Animation anim_cross_show @45016 +0B
    Fade (anon) @45056 +20B object=cross_shadow duration=100 accelMode=0 transparency=1
  Animation anim_triangle_hide @45116 +0B
    Fade (anon) @45156 +20B object=triangle_shadow duration=100 accelMode=0 transparency=0
  Animation anim_triangle_show @45216 +0B
    Fade (anon) @45256 +20B object=triangle_shadow duration=100 accelMode=0 transparency=1
  Animation anim_rectangle_hide @45316 +0B
    Fade (anon) @45356 +20B object=rectangle_shadow duration=100 accelMode=0 transparency=0
  Animation anim_rectangle_show @45416 +0B
    Fade (anon) @45456 +20B object=rectangle_shadow duration=100 accelMode=0 transparency=1
```

### 6.3 `explore_plugin_full` (compressed; 87 objects)

```
# rco-tree explore_plugin_full version=0x130 compression=1 file=2421192 tables=333812 objects=87 animations=0 read=3.79ms
ObjectTree (anon) @222584 +0B
  Page expage_explore_bg @222624 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_explore_list @222700 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_explore_fg @222776 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_menu @222852 +36B unknownInt0=17891328 onInit=null onCancel=id:0x4 onContextMenu=id:0x1c onActivate=null
    Plane plane_menu0 @222928 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu0_BG0 @223068 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu0_BG1 @223208 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      MList plane_menu0_LowerMenu @223348 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0x101f6 unknown18=0x101f7 unknown19=0x101f8 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x7 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x0 unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=null unknownRef43=id:0xa0 unknownRef45=id:0x88 unknownRef47=null unknownRef49=id:0x50 unknownRef51=id:0x6c
      MList plane_menu0_UpperMenu @223596 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0x101f9 unknown18=0x101fa unknown19=0x101fb unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x6 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x0 unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=null unknownRef43=id:0xa0 unknownRef45=null unknownRef47=id:0xbc unknownRef49=id:0x50 unknownRef51=id:0x6c
    Plane plane_menu1 @223844 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=128 height=272 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu1_BG0 @223984 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu1_BG1 @224124 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      MList plane_menu1_LowerMenu @224264 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0x101f6 unknown18=0x101f7 unknown19=0x101f8 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x6 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x0 unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=id:0xd8 unknownRef43=id:0xa0 unknownRef45=id:0x88 unknownRef47=null unknownRef49=id:0x50 unknownRef51=id:0x6c
      MList plane_menu1_UpperMenu @224512 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0x101f9 unknown18=0x101fa unknown19=0x101fb unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x6 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x1fc unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=id:0xd8 unknownRef43=id:0xa0 unknownRef45=null unknownRef47=id:0xbc unknownRef49=id:0x50 unknownRef51=id:0x6c
    Plane plane_menu2 @224760 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=128 height=272 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu2_BG0 @224900 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane plane_menu2_BG1 @225040 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=65536 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      MList plane_menu2_LowerMenu @225180 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1114112 onLoad=null unknown17=0x101f6 unknown18=0x101f7 unknown19=0x101f8 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x6 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x0 unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=id:0xd8 unknownRef43=null unknownRef45=id:0x88 unknownRef47=null unknownRef49=id:0x50 unknownRef51=id:0x6c
      MList plane_menu2_UpperMenu @225428 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=2162688 onLoad=null unknown17=0x101f9 unknown18=0x101fa unknown19=0x101fb unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0x6 unknown24=0x0 unknown25=-1 unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x0 unknown30=0x0 unknownRef32=null unknownRef33=id:0x38 unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=id:0xd8 unknownRef43=null unknownRef45=null unknownRef47=id:0xbc unknownRef49=id:0x50 unknownRef51=id:0x6c
  Page info_list @225676 +36B unknownInt0=17891328 onInit=id:0xf4 onCancel=id:0x128 onContextMenu=null onActivate=id:0x10c
    Plane info_base_plane @225752 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
  Page expage_fcopy_dialog @225892 +36B unknownInt0=17891328 onInit=null onCancel=id:0x140 onContextMenu=null onActivate=null
    Plane thumb_info_base @225968 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane filedialog_listbase @226108 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1c0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane plane_src_cmdbtn @226248 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x101c6 unknown18=0x101c7 unknown19=0x0 unknownInt20=65992 unknownInt21=65993 unknown22=0x0 image=null unknownInt24=0
      Button cmdbtn_select_all @226388 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#291:msg_select_all="Select All" unknownEvent31=id:0x190 unknownEvent33=id:0x1bc unknownEvent35=id:0x1e4 unknownEvent37=id:0x168 unknownEvent39=null unknownEvent41=null unknownEvent43=cmdbtn_deselect_all unknownEvent45=null unknownInt47=0
      Button cmdbtn_deselect_all @226616 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#292:msg_deselect_all="Clear All" unknownEvent31=id:0x20c unknownEvent33=id:0x1bc unknownEvent35=id:0x1e4 unknownEvent37=id:0x168 unknownEvent39=null unknownEvent41=cmdbtn_select_all unknownEvent43=cmdbtn_decide_src unknownEvent45=null unknownInt47=0
      Button cmdbtn_decide_src @226844 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#68:msg_fix="OK" unknownEvent31=id:0x23c unknownEvent33=id:0x1bc unknownEvent35=id:0x1e4 unknownEvent37=id:0x168 unknownEvent39=null unknownEvent41=cmdbtn_deselect_all unknownEvent43=null unknownEvent45=null unknownInt47=0
    Text text_info_msg @227072 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3145728 onLoad=null unknown17=0x1d0 unknown18=0x1d2 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=65556 unknownInt57=3 unknown58=0x0
    Text text_wait @227344 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3145728 onLoad=null unknown17=0xac unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=text#313:msg_wait="Please wait..." font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=65556 unknownInt57=3 unknown58=0x0
    Plane plane_exist_dimmer @227616 +100B posX=0 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=7 unknownInt21=8 unknown22=0x0 image=null unknownInt24=0
    Plane plane_exist_dialog_btn @227756 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0xac unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Button cmdbtn_rename @227896 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=text#286:msg_different_name_save="Rename and Save" unknownEvent31=id:0x268 unknownEvent33=id:0x294 unknownEvent35=id:0x2bc unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=cmdbtn_overwrite unknownEvent45=null unknownInt47=-1
      Button cmdbtn_overwrite @228124 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=text#287:msg_overwrite_save="Overwrite and Save" unknownEvent31=id:0x2e4 unknownEvent33=id:0x294 unknownEvent35=id:0x2bc unknownEvent37=null unknownEvent39=null unknownEvent41=cmdbtn_rename unknownEvent43=id:0x310 unknownEvent45=null unknownInt47=-1
      Button cmdbtn_nosave @228352 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=text#288:msg_save_no="Do Not Save" unknownEvent31=id:0x338 unknownEvent33=id:0x294 unknownEvent35=id:0x2bc unknownEvent37=null unknownEvent39=null unknownEvent41=cmdbtn_overwrite unknownEvent43=id:0x310 unknownEvent45=null unknownInt47=-1
    Plane plane_exist_chkbase @228580 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Button cmdbtn_chk_applyall @228720 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=text#290:msg_apply_all="Apply to All" unknownEvent31=id:0x364 unknownEvent33=id:0x294 unknownEvent35=id:0x2bc unknownEvent37=null unknownEvent39=null unknownEvent41=id:0x390 unknownEvent43=null unknownEvent45=null unknownInt47=-1
  Page expage_fcopy_exdlg @228948 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_fcopy_msgdlg @229024 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_fdelete_dialog @229100 +36B unknownInt0=17891328 onInit=null onCancel=id:0x3b8 onContextMenu=null onActivate=null
    Plane filedel_dialog_listbase @229176 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane filedel_dialog_icon @229316 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane filedel_dialog_info @229456 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane filedel_dialog_msgdialog @229596 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
    Plane fdel_plane_src_cmdbtn @229736 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x101c6 unknown18=0x101c7 unknown19=0x0 unknownInt20=65992 unknownInt21=65993 unknown22=0x0 image=null unknownInt24=0
      Button fdel_cmdbtn_select_all @229876 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#291:msg_select_all="Select All" unknownEvent31=id:0x40c unknownEvent33=id:0x43c unknownEvent35=id:0x468 unknownEvent37=id:0x3e0 unknownEvent39=null unknownEvent41=null unknownEvent43=fdel_cmdbtn_deselect_all unknownEvent45=null unknownInt47=0
      Button fdel_cmdbtn_deselect_all @230104 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#292:msg_deselect_all="Clear All" unknownEvent31=id:0x494 unknownEvent33=id:0x43c unknownEvent35=id:0x468 unknownEvent37=id:0x3e0 unknownEvent39=null unknownEvent41=fdel_cmdbtn_select_all unknownEvent43=fdel_cmdbtn_decide_src unknownEvent45=null unknownInt47=0
      Button fdel_cmdbtn_decide_src @230332 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=tex_button shadow=tex_button_shadow image2=tex_button_focus unknownRef29=text#68:msg_fix="OK" unknownEvent31=id:0x4c4 unknownEvent33=id:0x43c unknownEvent35=id:0x468 unknownEvent37=id:0x3e0 unknownEvent39=null unknownEvent41=fdel_cmdbtn_deselect_all unknownEvent43=null unknownEvent45=null unknownInt47=0
    Text fdel_text_info_msg @230560 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3145728 onLoad=null unknown17=0x1d0 unknown18=0x1d2 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=1 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=65556 unknownInt57=3 unknown58=0x0
  Page expage_filelist @230832 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page expage_message_dialog @230908 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page control_panel_page @230984 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane control_panel_base @231060 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x10198 unknown18=0x10199 unknown19=0x0 unknownInt20=65946 unknownInt21=65947 unknown22=0x0 image=tex_cp_panel unknownInt24=3
      Plane control_play_status_group @231200 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
        Plane plane_play_status @231340 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_play_status unknownInt24=0
        Plane plane_prev_status @231480 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_prev_status unknownInt24=0
        Plane plane_next_status @231620 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_next_status unknownInt24=0
        Plane plane_pause_status @231760 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_pause_status unknownInt24=0
        Plane plane_stop_status @231900 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_stop_status unknownInt24=0
        Plane plane_searchb_status @232040 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_searchb_status unknownInt24=0
        Plane plane_searchf_status @232180 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1a8 unknown18=0x1a9 unknown19=0x0 unknownInt20=65962 unknownInt21=65963 unknown22=0x0 image=tex_searchf_status unknownInt24=0
        Plane plane_repeat_status @232320 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1ac unknown18=0x1ad unknown19=0x0 unknownInt20=65966 unknownInt21=65967 unknown22=0x0 image=tex_repeat_status unknownInt24=0
        Plane plane_1repeat_status @232460 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1b0 unknown18=0x1b1 unknown19=0x0 unknownInt20=65970 unknownInt21=65971 unknown22=0x0 image=tex_1repeat_status unknownInt24=0
        Plane plane_shuffle_status @232600 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x1b4 unknown18=0x1b5 unknown19=0x0 unknownInt20=65974 unknownInt21=65975 unknown22=0x0 image=tex_shuffle_status unknownInt24=0
      Plane control_progress_bar_group @232740 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
        Progress progress_bar_playing_time @232880 +120B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=200 height=6 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=131072 onLoad=null unknown17=0x19c unknown18=0x19d unknown19=0x0 unknownInt20=65950 unknownInt21=65951 unknown22=0x0 unknownFloat23=100 unknown24=0x0 unknownRef25=null unknownRef27=null unknownRef29=null
        Text label_playing_time @233040 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3276800 onLoad=null unknown17=0x1a0 unknown18=0x1a1 unknown19=0x0 unknownInt20=65954 unknownInt21=65955 unknown22=0x0 text=null font=null unknownInt27=33554433 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=22 unknownInt57=2312 unknown58=0x0
      Plane control_button_group @233312 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
        Button msg_prev_with_icon_button @233452 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_prev shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_next_with_icon_button @233680 +188B posX=-1 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_next shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_pause_button @233908 +188B posX=-2 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_pause shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_stop_button @234136 +188B posX=-3 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_stop shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_repeat_button @234364 +188B posX=-4 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_repeat shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_shuffle_button @234592 +188B posX=-5 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_shuffle shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
        Button msg_volume_control_button @234820 +188B posX=-6 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x7d01a4 unknown18=0x101a5 unknown19=0x0 unknownInt20=422 unknownInt21=423 unknown22=0x0 image=tex_cp_volume shadow=null image2=tex_cp_focus unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=null unknownEvent41=null unknownEvent43=null unknownEvent45=null unknownInt47=0
  Page volume_page @235048 +36B unknownInt0=17891328 onInit=id:0x4f4 onCancel=null onContextMenu=null onActivate=null
    Meter volume_level_meter @235124 +140B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1048576 onLoad=null unknown17=0x1b8 unknown18=0x1b9 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0xfffffffc unknown24=0x4 unknown25=0x0 unknown26=0x4000000 unknown27=0x51c unknownRef28=null unknownRef30=null unknownRef32=null unknownRef34=null
  Page notification_page @235304 +36B unknownInt0=17891328 onInit=id:0x540 onCancel=null onContextMenu=null onActivate=null
    Text notification_text @235380 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3145728 onLoad=null unknown17=0x1ba unknown18=0x1bb unknown19=0x0 unknownInt20=444 unknownInt21=445 unknown22=0x0 text=null font=null unknownInt27=65537 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=20 unknownInt57=446 unknown58=0x0
  Page poweroff_page @235652 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane poweroff_base @235728 +100B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      CheckboxGroup poweroff_select @235868 +208B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x10000 unknown18=0x10000 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknown23=0xa unknown24=0x0 unknown25=0xffffffff unknown26=0x0 unknown27=0x0 unknown28=0x0 unknown29=0x417 unknown30=0x0 unknownRef31=null unknownRef33=null unknownRef35=null unknownRef37=null unknownRef39=null unknownRef41=null unknownRef43=null unknownRef45=null unknownRef47=poweroff_yes unknownRef49=null unknownRef51=null
        Scroll scroll_audio_out_select_output_format @236116 +140B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=8 height=172 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 unknownFloat23=100 unknownFloat24=100 unknown25=0x0 unknownRef26=null unknownRef28=null unknownRef30=null unknownRef32=null unknownRef34=null
      Button poweroff_yes @236296 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x10246 unknown18=0x10248 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=null unknownEvent39=poweroff_no unknownEvent41=poweroff_select unknownEvent43=null unknownEvent45=null unknownInt47=0
      Button poweroff_no @236524 +188B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x10247 unknown18=0x10248 unknown19=0x10000 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null shadow=null image2=null unknownRef29=null unknownEvent31=null unknownEvent33=null unknownEvent35=null unknownEvent37=poweroff_yes unknownEvent39=null unknownEvent41=poweroff_select unknownEvent43=null unknownEvent45=null unknownInt47=0
      Text text_audio_out_select_output_format @236752 +232B posX=0 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x10000 unknown18=0x10c01 unknown19=0x10000 unknownInt20=66604 unknownInt21=66605 unknown22=0x0 text=text#266:msg_turnoff_system_ask="Do you want to turn off the system?" font=null unknownInt27=65537 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0x0 unknownInt56=65556 unknownInt57=3 unknown58=0x0
```

### 6.4 `xmb_plugin_normal` (compressed; XMenu/XMList/Text)

```
# rco-tree xmb_plugin_normal version=0x130 compression=1 file=19724 tables=7208 objects=26 animations=0 read=0.80ms
ObjectTree (anon) @3104 +0B
  Page page_xmb_bg @3144 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
  Page page_xmb @3220 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    XMenu xmenu @3296 +132B posX=-395 posY=40 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=id:0x4 unknown17=0x10101 unknown18=0x10102 unknown19=0x10103 unknownInt20=0 unknownInt21=0 unknown22=0x0 menus=10 onEnter=id:0x18 onContext=id:0x2c unknownEvent28=id:0x7c onLabelLoad=id:0x48 unknownRef32=id:0x60
      XMList list_user @3468 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_sysconf @3528 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_photo @3588 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_music @3648 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_video @3708 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_tv @3768 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_game @3828 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_network @3888 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_psn @3948 +20B unknown0=0x0 image=null unknownRef3=null
      XMList list_friend @4008 +20B unknown0=0x0 image=null unknownRef3=null
  Page page_xmb_indicator @4068 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
    Plane indicator @4144 +100B posX=0 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x0 unknown18=0x0 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
      Plane indi_base @4284 +100B posX=0 posY=-74 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=560 height=82 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=1179648 onLoad=null unknown17=0xc unknown18=0x5000d unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_indi_bg unknownInt24=3
      Plane indi_adjust @4424 +100B posX=-26 posY=0 posZ=0 redScale=0 greenScale=0 blueScale=0 alphaScale=0 width=0 height=0 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70290 unknown18=0x70291 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 image=null unknownInt24=0
        Text clock @4564 +232B posX=0 posY=-12 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3276800 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=33554433 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=19 unknownInt57=3 unknown58=0x0
        Plane message @4836 +100B posX=-244 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=32 height=32 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_indi_message unknownInt24=0
        Plane chat @4976 +100B posX=-274 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=32 height=32 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_indi_chat unknownInt24=0
        Plane online @5116 +100B posX=-342 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=32 height=32 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_indi_online unknownInt24=0
          Text online_num @5256 +232B posX=14 posY=2 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=0 height=0 depth=1 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=3211264 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=0 unknownInt21=0 unknown22=0x0 text=null font=null unknownInt27=16777217 unknownInt29=16777216 size=10 topRed=1 topGreen=1 topBlue=1 bottomRed=1 bottomGreen=1 bottomBlue=1 spacingHorizontal=0 unknown37=0x0 unknown38=0x0 unknown39=0x0 spacingVertical=0 shadowX=4 shadowY=-5 shadowPerspective=0 shadowRed=0.25 shadowGreen=0.25 shadowBlue=0.25 shadowAlpha=1 unknown48=0x0 unknown49=0x0 unknown50=0x0 unknownFloat51=1 unknownFloat52=1 unknownFloat53=1 unknownFloat54=1 unknown55=0xffffffff unknownInt56=22 unknownInt57=3 unknown58=0x0
        Plane avatar @5528 +100B posX=-376 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=32 height=32 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=null unknownInt24=0
        Plane plus @5668 +100B posX=-410 posY=0 posZ=0 redScale=1 greenScale=1 blueScale=1 alphaScale=1 width=32 height=32 depth=0 scaleWidth=1 scaleHeight=1 scaleDepth=1 iconOffset=0 onLoad=null unknown17=0x70000 unknown18=0x70000 unknown19=0x0 unknownInt20=458752 unknownInt21=458752 unknown22=0x0 image=tex_indi_plus unknownInt24=0
  Page page_xmb_fg @5808 +36B unknownInt0=17891328 onInit=null onCancel=null onContextMenu=null onActivate=null
```

## 7. Cost measurements

Measured with `QElapsedTimer` in the `--rco-tree` handler (read + inflate + walk per container, inside one
process, with the OS file cache warm after the first run - these are steady-state application costs, not
first-ever-disk-read costs):

| Measurement | Result |
|---|---|
| construct `RcoObjects` (no I/O, no parse) | 0 ms, 0 bytes |
| first `container("msgdialog_plugin")` | 0.26-0.43 ms, 257900 B, 2 objects |
| first `container("xmb_plugin_normal")` | 0.39-0.80 ms, 19724 B, 26 objects |
| first `container("system_plugin")` | 0.52-0.98 ms, 296356 B, 59 objects + 17 animations |
| first `container("explore_plugin_full")` | 3.79-4.52 ms, 2421192 B, 87 objects |
| first `container("sysconf_plugin")` (largest tree) | 4.89 ms, 1440648 B, 2247 objects |
| repeat `container("system_plugin")` in the same process | 0.00 ms, 0 bytes (cache hit, no re-read) |
| the 13-container startup set (below) | 14.68 ms, 4767780 B, 465 objects |
| all 122 RCOs | 61.65 ms, 20039542 B, 8070 objects, 165 animations |
| all 122 + the 8358-line dump write | 199 ms total |

The startup set is `system_plugin`, `explore_plugin_full`, `xmb_plugin_normal`, `msgdialog_plugin` and the
nine `explore_category_*` containers the columns resolve through.

**Startup cost in the shipped app: 0 ms.** No render-path code constructs `RcoObjects` yet (this task's
write fence excludes `shell.cpp`/`view.cpp`), so the lazy reader performs no I/O at startup. The
13-container figure is what a later startup consumer would pay once it starts reading the object trees; a
consumer that reads one container pays only that container's line above. Text and image tables are decoded
on first access, not at parse time, so they are not in these numbers unless the dump asks for them - the
dumps above do, and the totals include it.

The cache is one entry per container name (parsed tree + inflated table section + lazy caches), so a second
request for the same container is a hash lookup: 0.00 ms measured.

---

## 8. Values that must now come from this reader

The rule for P1: a value that exists in an RCO or an MNU file is read, not typed. These are the current
hardcoded sites in the renderer (line numbers as of this writing; the tree is shared, so cite the symbol
when the line moves). None of them was changed in this task - the files are other agents' fences.

| Value | Firmware source (reader call) | Hardcoded site today |
|---|---|---|
| particle physics: DELTA TIME, EMIT VEL MIN/MUL/VAR, EMIT CONE ANGLE, EMIT NEG PROB, EMIT PER FRAME, EMIT PROB, AGING SPEED/VARIANCE, FRICTION, GRAVITY, BROWNIAN SCALE, SPIN TIME SCALE | `materials.particles(preset).number("delta time")` etc. | `src/particles.cpp` `kDeltaTime`, `kEmitPerFrame`, `kEmitVelMin`, `kEmitVelMul`, `kEmitVelVar`, `kEmitConeDegrees`, `kEmitNegProb`, `kAgingSpeed`, `kAgingVariance`, `kFriction`, `kGravity`, `kBrownian`, `kSpinTimeScale` (17-33) |
| particle photometry: LAMBERT COEFF, SPECULAR COEFF, SPECULAR POWER, EXPOSURE, COLOR CONTROL | `materials.particles(...)` | `src/particles.cpp` 146-147, 220-221 (8.0, 74.74, 0.0390458) |
| particle material: FRESNEL, IRIDESCENT EXP, GLOBAL ALPHA, GLARE, GLARE P1/P2, GLARE SCALE, SIZE MIDDLE/FAR, NEAR/FAR FOCUS | `materials.particles(...)` | `src/particles.cpp` 69, 96-97, 109, 119, 184-188, 197, 202, 219-222 |
| particle spot direction | `SPOT POS X/Y/Z` | `src/particles.cpp` 87 and 180 (see 9.1) |
| PARTICLES_UI gust/input constants (ICON WIND, DPAD SCALE, DSHAKE/RSHAKE) | `materials.particlesUi()` | no consumer yet (interaction audio/gust code) |
| PARTICLES_SPE music variant (DELTA TIME, GLARE, SPECULAR POWER, SIZE MIDDLE, GLOBAL ALPHA) | `materials.particlesSpe()` | no consumer (see 9.2) |
| ICONS.mnu per preset (LIT1/LIT2 angles, ATTN*, AMB*, REFR*, EXPOSE, GLASS, TIME COLOR MIX, ...) | `materials.icons(preset)` | `src/icons.cpp:12-19` (a second MNU parser) and `:97-102` (presets); values match |
| BACKGROUND.mnu line gradient 3/4 | `materials.background(preset)` keys `3 RED`... | `src/ribbon.cpp:14` (`colour3`, `colour4`); values match |
| BACKGROUND.mnu NIGHT BLEND | `materials.background("day").number("NIGHT BLEND")` | `src/view.h:194` `kNightBlend=.499947`; value matches |
| BACKGROUND.mnu NIGHT WHIT BIAS (day/night presets) | `materials.background("day")...` | `src/view.cpp:493` `.486059f`; value matches |
| BACKGROUND.mnu coldboot1 NIGHT WHIT BIAS | `materials.background("coldboot1")` | `src/view.cpp:411` `coldboot1NightBias=.513834f`; value matches |
| HDR.mnu presets (EXPOSURE, WHITE LEVEL, GLARE LEVEL, GLARE THRESH, GAUSSIAN RAD R/G/B, GLARE SUM POW, TEX SIZE, TEX MAX MIP) | `materials.hdr(preset)` | `src/hdr.cpp:9-13` (`night`, `day`, `coldboot1`, `texSize`, `texMaxMip`); values match |
| LINE1.mnu presets (DAMPING, LENGTH, TENSION, TIMESTEP, FFD*, POS/ANG, ...) | `materials.line1(preset)` | `src/ribbon.cpp:19-26` readMnu + `readPreset`; values match |
| text material: size, top/bottom gradient, spacing, shadow offset/colour, font reference | `materials.textMaterial(container, textObject)` | label sizes and shadow values in `src/view.cpp` text calls and `src/text_renderer.cpp` (shadow offset 0, alpha 0.2, black; see 9.4) |
| dialog plate colours, bar texture, button glyph+label, hint row | `objects.container("system_plugin")` image refs + `assets_.text("system_plugin", "msg_ok")` etc. | `src/view.cpp` message drawing (see `audit/DIALOGS-FIRMWARE.md` section 4; that surface is another agent's fence) |
| every dialog/list/panel geometry, colour and string an RCO declares | this reader's object tree (P2 consumes it) | not wired yet |
| RCO object names and types for the menu/panel work | this reader | not wired yet |

The reader also replaces three private MNU parsers (`icons.cpp:12`, `ribbon.cpp:19`, and the constants in
`hdr.cpp`/`particles.cpp`): one API, one cache, one place where a missing key is distinguishable from a
zero value.

---

## 9. Hardcoded values that contradict the firmware

Only contradictions found by reading the RCO/MNU sources against the current constants. Rendering was not
changed in this task.

### 9.1 Particle light direction (`src/particles.cpp:87`, `:180`)

- Native: `lightDir=normalize(vec3(0.55,0.42,0.72))`, comment "PARTICLES.mnu SPOT POS: upper-right key light".
- Firmware: `PARTICLES.mnu` (base and every preset that carries it) has `spot pos x:4.16`,
  `spot pos y:2.63`, `spot pos z:-7.6`. Normalised, that is (0.4594, 0.2904, -0.8393).
- All three components differ and the z sign is inverted. The firmware's spot sits *inside/behind* the
  dust band (particles live at z -3..-13.5 and the spot is at z -7.6), so the native's `+z` key light
  cannot be the direction to that spot. What the firmware's vpo does with the spot (normalise
  `spot - particle` per vertex, or use a fixed direction) is not decoded here; what is certain is that the
  native vector is not the normalised `SPOT POS` the comment claims. This is a shading-direction
  contradiction, not a rounding difference.

### 9.2 PARTICLES_SPE.mnu is never read

`PARTICLES_SPE.mnu` (the music variant) differs from the base file in five keys: `delta time` 0.00346295
(vs 0.0088883), `glare` 0.0832176 (vs 0.159705), `specular power` -29.3657 (vs 35.2904), `size middle`
0.0218883 (vs 0.0536233), `global alpha` -0.555603 (vs 1). No code path reads it, so any surface the
firmware draws with SPE (the music background preset per `HANDOFF-PHASES`/`PARTICLES.md`) would run at the
base values. Whether the plugin actually schedules SPE for `music_1` is not proven locally (P5's job).

### 9.3 Particle depth-of-field band (`src/particles.cpp:96`, `:184`)

- Native: `defocus=clamp(abs(depth-6.12)/7.4,0,1)`, comment "NEAR/FAR FOCUS bands".
- Firmware: `NEAR FOCUS` 6.12435 (used, truncated to 6.12) and `FAR FOCUS` 12.8327 (never used). The
  near-to-far span is 12.8327 - 6.12435 = 6.70835, not 7.4. The 7.4 denominator and the `*0.12`, `*1.15`
  gains have no MNU key and are fits; the doc should keep calling them fits, but the "NEAR/FAR FOCUS
  bands" comment claims a provenance the numbers do not have.

### 9.4 Text shadow material (`src/text_renderer.cpp` shadow uniforms)

- Native: composite pass sets `shadowColor` (0,0,0), `shadowAlpha` .2 and `shadowOffset` (0,0) for the
  per-tile path and a `reach`-derived offset for the union path (`src/text_renderer.cpp:421-423` and
  `:439-441`).
- Firmware: every XMB `Text` object carries `shadowX=4`, `shadowY=-5`, `shadowPerspective=0`,
  `shadowRed/Green/Blue=0.25`, `shadowAlpha=1.0` (measured on `system_plugin` `infotext*`, `cross_label`
  and every `notification_prerender_*`; `explore_plugin_full` `fdel_text_info_msg` is the same 4/-5 at
  0.25 per `DIALOGS-FIRMWARE.md`).
- So the RCO's text shadow is a 4/-5 px offset in 25% grey at full alpha; the renderer draws a 0-offset
  black shadow at 20% alpha. P3 owns the text pipeline, but the material source is this reader

### 9.5 Values that match (no contradiction, still duplicated)

These were checked value by value and agree with their MNU source today; they are duplicates, not errors:
`ICONS.mnu` day/night/yoake/higure reads in `icons.cpp`; `BACKGROUND.mnu` 3/4 gradient, NIGHT BLEND,
NIGHT WHIT BIAS (0.486059 day/night, 0.513834 coldboot1); `HDR.mnu` night/day/coldboot1 and TEX SIZE/MAX
MIP; `PARTICLES.mnu` base physics and photometry. Section 8 lists the sites.

---

## 10. Known unknowns

1. **Tag 0x0400** (event and ref words): table unidentified, kept as `Id`. Evidence in 4.5.
2. **Tag semantics vs field type**: `videoeditor_plugin.rco` `video_editor_caption_menu_list_item0.text`
   is a schema `text` field carrying tag 0x0402, which resolves to image `tex_vc_mpeg2`. The reader trusts
   the tag (target space) over the field type, which is the only rule the data supports; what the firmware
   draws for that field is unknown.
3. **Attribute schema provenance**: the field order/widths come from Rcomage's definition (MIT), not from a
   Sony file in the local tree. It is a format definition, not a value source. An entry whose type id is
   outside the table needs its stored size (the reader rejects it otherwise, as the reference parser does).
4. **Interpreting `u` (unk) words**: kept raw. Several look like flags, small ids or packed pairs
   (`unknownInt20`/`unknownInt21` on Planes, `unknown22`); mapping them is P2's tracing work.
5. **What a `0x0409` object ref scopes**: animations use it for their target object and it resolves to the
   right plane; whether 0x0409 means "same object, animation scope" is inferred, not documented.
6. **Font tree semantics**: the `font` ref resolves to a font *style* entry name (`fontstyle_light`,
   `fontstyle_default`); which TTF face and metrics that selects is P1.4, not this reader.
7. **No TEXT.mnu**: the text material is per Text object. If a future firmware tree carries a text MNU, the
   `Materials::mnu(archive, path)` path already reads it without a code change.

---

## 11. Verification and files

Build and gate (the parity gate this task must not move):

```
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-rco
bun work\ab-layers.ts --capture --exe C:/dev/out/native-rco/re-ps3-native.exe
```

- Build: succeeds (`re-ps3-native.exe`), no new warnings from the added files.
- `ab-layers.ts --capture`: **PASS 6/6** (frame, top background, category icon row, item label row,
  clock text, info bar within budget). This task adds no pixel path; the reader is not called by the
  renderer yet.
- Dumps: all 122 RCOs parse with zero failures (section 7), 8070 objects and 165 animations - the same
  counts the reference inventory (`out/native-reference/rco-behavior/resources.json`) reports for those
  files, which cross-checks the walk against an independent parser.

Reproduce a dump:

```
C:\dev\out\native-rco\re-ps3-native.exe --dev-flash <dev_flash> --rco-tree "system_plugin@system.txt"
C:\dev\out\native-rco\re-ps3-native.exe --dev-flash <dev_flash> --rco-tree "*@all.txt"
```

Files touched by this task:

- new `src/rco_objects.h`, `src/rco_objects.cpp` (reader + `Materials`)
- `CMakeLists.txt` (the new pair added to the existing target, nothing else)
- `src/main.cpp` (the `--rco-tree` option and its handler only; `--rco-images`/`--rco-text` untouched)
- new `audit/RCO-OBJECT-MODEL.md` (this document)
- `work/` helpers: `rco-schema-gen.ts` (regenerates the embedded schema table), `rco-obj-probe.ts`,
  `rco-ref-probe.ts`, `rco-ref-probe2.ts`, `rco-tag-scan.ts` (format probes used for the tag table),
  `rco-schema-objects.inc`, `rco-schema-animations.inc`

