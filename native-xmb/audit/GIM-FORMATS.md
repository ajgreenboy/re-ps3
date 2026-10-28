# GIM pixel formats 4/5/6: indexed decoding and the 4.93 OSK masks

Scope: the native asset decoder in `src/assets.cpp` (`Assets::decodeGim`) and the fourteen
OSK textures that reported `Unsupported GIM pixel format 0x5`. The decoder now implements
the documented INDEX4/INDEX8/INDEX16 palette path and the palette-less form the firmware
actually ships. Written 2026-10-27.

Evidence classes: **M** = measured from a firmware file or a decoder run (offset, byte,
pixel), **F** = fit or inference, **R** = recorded run. Firmware bytes stay local; this
document carries names, offsets, constants and results only.

## 1. What failed, and what the bytes say

The OSK layout declares 52 named textures in `src/osk_layout.cpp` (`kTextures`). Before
this change 38 decoded and 14 failed:

```
LABEL_UP LABEL_DOWN LABEL_LEFT LABEL_RIGHT
EN_LABEL_DELETE EN_LABEL_SPACE EN_LABEL_RETURN EN_LABEL_MODE EN_LABEL_CLOSE
KEYTOP_COPY KEYTOP_PASTE
MODE_GUIDE_ALPHABET MODE_GUIDE_ALPHABET_CAP
LIST_WINDOW_FOCUS
```

All fourteen are GIM pixel format **0x5**; the other 38 are 0x3 (RGBA8888) or 0xA
(DXT5). The failure message came from the decoder's final gate, not from a missing
palette lookup.

The important finding is that these fourteen pictures **carry no palette block**. Every
one is a root -> picture -> image block chain and nothing else:

```
LABEL_UP (4448 bytes)
  @0x0010 root    type=0x2 size=4432 data=0x10
  @0x0020 picture type=0x3 size=4416 data=0x10
  @0x0030 image   type=0x4 size=4400 data=0x10
    format=0x5 order=0 72x54 bpp=8 pitch_align=16
    pixels @0x80, 4320 bytes, ends exactly at the picture end (0x1160)
```

The same shape holds for the other thirteen (EN_LABEL_* 140x44, MODE_GUIDE_* 29x30,
LIST_WINDOW_FOCUS 42x42). A scan of every GIM picture in every local
`dev_flash/vsh/resource/*.rco` (2967 pictures, 224 of them format 0x5) found **no palette
block at all** in any of them. So the palette-block hypothesis in the task brief does not
hold for this firmware dump: there is nothing to read for these fourteen.

What the bytes are instead is visible in the value histogram and in the decoded shape:

| Texture | Distinct byte values | First levels |
|---|---|---|
| LABEL_UP | 15 | 0, 16, 32, 48, 64, 80, 96, 112, 128, 159, 175, 191, 207, 239, 255 |
| MODE_SYMBOL | 16 | 0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255 |
| MODE_GUIDE_ALPHABET | 17 | 0, 16, 32, 48, ... 254 |
| KEYTOP_COPY | 5 | 0, 64, 128, 191, 255 |

The values rise monotonically from the background into the glyph, in 16 or 17 steps: they
are **coverage**, i.e. an anti-aliased 8-bit alpha mask, not arbitrary palette indices.
Rendered as grayscale the fourteen produce legible shapes: an up triangle, the words
"Backspace", "Space", "Line Break", "Input Mode", "Enter", a copy icon, a clipboard icon,
lowercase "a", uppercase "A", and a soft rounded focus square. Composited as white with
alpha=value they are white masks on transparency; the OSK draw tints them per key.
`LIST_WINDOW_FOCUS` is the only one whose shape spans the whole texture (a soft ring);
the rest place the glyph at the right or lower-right of the nominal cell, which is the
position they occupy on the key face.

Corroboration against the console frame: the reference arrow keys in
`C:/dev/out/osk-fw/s1/f0060-0257.png` put the up glyph at x 34..52, y 27..37 inside the
72x54 cell. The decoded mask's white bbox is x 37..58, y 32..43; scaled by the 66x50 face
inset (0.917, 0.926) that is x 34..53, y 30..40, within 3 px of the reference in y and
exact in x, so the glyph is the same one and the remaining offset belongs to the draw.

### 1.1 Why format 0x5 can be both indexed and a mask

The GIM image header's format value is Sony's enum and 0x5 is INDEX8 in the published
format lists. The header in these files is nevertheless a picture (`level_type` 1 at
header +0x28) with no palette sibling; the engine samples the 8-bit plane as an alpha
mask. A palette-bearing INDEX8 picture would have a type-0x05 sibling inside the same
picture block, which this decoder now reads. Both forms are therefore supported: the
palette is used when present, and a picture with no palette is decoded as coverage.

## 2. GIM block layout as decoded here - M

Header: 16 bytes, `.GIM1.00` then the platform field (`00 'PSP' 00` in these files).

Every block header is 16 bytes, big-endian:

| Offset | Type | Field |
|---|---|---|
| +0x00 | u16 | block type: 0x02 root, 0x03 picture, 0x04 image, 0x05 palette |
| +0x02 | u16 | unused |
| +0x04 | u32 | block size, including this 16-byte header |
| +0x08 | u32 | unused |
| +0x0C | u32 | data offset, relative to the block start (0x10 for the blocks here) |

Root and picture blocks point at their first child with the data offset; leaf blocks
(image, palette) carry their own total size. Picture children are adjacent: the image
block, then any palette block. Only the first image/frame is decoded; mipmaps and extra
frames are skipped by their own size.

### 2.1 Image and palette header (0x30 bytes at block + data offset)

| Offset | Type | Meaning | Firmware values |
|---|---|---|---|
| +0x00 | u16 | header size | 0x30 |
| +0x02 | u16 | reference | 0 |
| +0x04 | u16 | **pixel/palette format** | 0x3, 0x5, 0x8, 0x9, 0xA |
| +0x06 | u16 | pixel order (0 normal, 1 PSP) | 0 |
| +0x08 | u16 | width, or palette entry count | 72 ... 256 |
| +0x0A | u16 | height, or palette height (1) | 54 ... 1 |
| +0x0C | u16 | bits per pixel / bpp align | 8, 32 |
| +0x0E | u16 | **pitch alignment in bytes** | 16 for 0x3/0x5, 4 for 0xA |
| +0x10 | u16 | height alignment | 1 (4 for DXT) |
| +0x12 | u16 | dimension count | 2 |
| +0x14 | u32 | reserved | 0 |
| +0x18 | u32 | index start | 0x30 |
| +0x1C | u32 | **pixel/palette data offset from the header** | 0x40 |
| +0x20 | u32 | plane size / block data size | e.g. 0x1120 |
| +0x24 | u32 | plane mask | 0 |
| +0x28 | u16 | **level type: 1 picture, 2 palette** | 1 or 2 |
| +0x2A | u16 | level/mipmap count | 1 |
| +0x2C | u16 | frame type | 3 |
| +0x2E | u16 | frame count | 1 |
| +0x30 | u32 | frame offset | 0x40 |

The first pixel/colour therefore begins at `block + 0x10 + u32(header + 0x1C)`, which is
block + 0x60 in the firmware layout (image block at +0x30 for the first picture: data at
+0x40, header to +0x70, 16 bytes of frame-offset padding, pixels at +0x80).

Row stride is `align_up(ceil(width * bits / 8), pitch_align)`, so 8-bit rows are padded to
16 bytes for all fourteen textures: 72 px -> 80 bytes, 140 -> 144, 29 -> 32, 42 -> 48.
The last row ends exactly at the image block end (no slack), which is the bound check the
decoder uses.

## 3. Pixel formats found in the local 4.93 tree - M

A scan of the 89 `dev_flash/vsh/resource/*.rco` files that carry images (2967 GIM pictures):

| GIM format | Meaning | Pictures | Decoder |
|---|---|---|---|
| 0x3 | RGBA8888 | 1864 | existing, unchanged |
| 0x8 | DXT1 | 31 | existing, unchanged |
| 0x9 | DXT3 | 6 | existing, unchanged |
| 0xA | DXT5 | 842 | existing, unchanged |
| 0x5 | INDEX8 | 224 | **new**; all 224 palette-less coverage masks |
| 0x4 | INDEX4 | 0 | **new**, synthetic fixture only |
| 0x6 | INDEX16 | 0 | **new**, synthetic fixture only |
| 0x7 | INDEX32 | 0 | still unsupported, clear error |

There is no format 0x0, 0x1, 0x2 (16-bit true colour) picture in the local RCOs, so those
remain unsupported with the same clear error (they are not part of this task).

## 4. The palette block layout the decoder implements - M/F

The layout is the one documented for GIM and reproduced by independent readers
(`gimconv_v` reads colours at block + 0x50): a palette block is a normal block of type
0x05 whose data is an image header with `level_type = 2` (palette). The decoder reads:

```
block header  16 bytes   type 0x05, size, data offset
palette header 0x30      same field layout as 2.1
colours        count * entry bytes at header + u32(+0x1C)
```

* entry format from header +0x04: 0x0 RGBA5650, 0x1 RGBA5551, 0x2 RGBA4444, 0x3 RGBA8888;
* count is `width * height` from +0x08/+0x0A (normally 256 x 1);
* entries are big-endian; 2 bytes for 0x0..0x2, 4 bytes for 0x3 (R,G,B,A byte order,
  the same order the RGBA8888 image path uses);
* the palette block must fit the picture container and its own block: the start offset
  must be at least header + 0x30, and `count * entryBytes` must not run past the block.

Observed firmware pictures have no type-0x05 block (section 1), so this path is verified
with a synthetic fixture rather than with a real picture. The fixture is a copy of
`oskfullkeypanel_plugin.rco` under `C:/dev/out/gim-fixture`: five image entries are
repointed at appended GIMs built with a real palette block. The 16-bit and RGBA palettes
and the three index widths all decode pixel-exact:

| Fixture picture | Index | Palette | Expected | Result |
|---|---|---|---|---|
| BUTTON_BASE_WHITE | 8-bit | 4 x RGBA8888 | red, green, blue, white@128 | exact |
| BUTTON_BASE_B | 4-bit | 4 x RGBA5650 | red, green, blue, white | exact |
| BUTTON_BASE_G | 4-bit | 4 x RGBA5551 | red, green, blue, white | exact |
| BUTTON_BASE_O | 4-bit | 4 x RGBA4444 | red, green, blue, white@136 | exact |
| BUTTON_BASE_E | 16-bit | 2 x RGBA8888 | red/green alternating | exact |

INDEX4 packs two pixels per byte with the high nibble as the left pixel; this is the
documented normal order, exercised only by the fixture. INDEX16 indices are big-endian
u16. Row padding is the image's own pitch alignment.

An out-of-range check is part of the path: an INDEX8 fixture picture with index 7 and a
4-entry palette fails the image with
`GIM palette index 7 exceeds 4 entries` and no PNG is written.

## 5. Decoder behaviour changes - M

`Assets::decodeGim` now:

* keeps the RGBA8888 (0x3) and block-compressed (0x8, 0x9, 0xA) paths byte-for-byte;
* adds INDEX4/INDEX8/INDEX16 (0x4/0x5/0x6): reads the image header, finds a type-0x05
  palette among the picture's direct children, decodes it, then maps indices through it
  with the row stride/padding above;
* when an indexed picture has no palette block, decodes the index as coverage:
  `qRgba(255,255,255,coverage)` with coverage = index (8-bit), index*17 (4-bit) or
  index>>8 (16-bit). This is the form the fourteen OSK textures use; the RGB is neutral
  white so the OSK draw can tint them;
* fails with the format id for anything else:
  `Unsupported GIM pixel format 0x...` (unchanged), and
  `Unsupported GIM palette format 0x...` for an unknown palette entry format;
* bounds every read: the block header must sit inside the file and the block, the header
  inside the block, the pixel offset after the header, index rows inside the image block,
  the palette start after its header, the entry table inside the palette block, and every
  index inside the palette (a single clear error, not a silent read);
* stays cheap: one linear pass over the index rows and one over the palette; no allocation
  beyond the output image, palette vector and row lookup.

The optional trace is `XMB_GIM_TRACE=1`: for every decoded picture (for example through
`--export-image`) it logs one header line and one result line, with the texture name:

```
gim: LABEL_UP format=0x5 order=0 72x54 align=16 mode=index palette=absent (coverage mask)
gim: LABEL_UP result 72x54
```

The read-only diagnostic used for this document:

```powershell
$env:XMB_GIM_TRACE='1'
$env:XMB_LOG='C:\dev\out\native-gim\gim-trace.log'
C:\dev\out\native-gim\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash `
  --export-image "oskfullkeypanel_plugin:LABEL_UP,oskfullkeypanel_plugin:EN_LABEL_SPACE@C:\dev\out\native-gim\osk"
```

`--export-image` already existed and goes through `Assets::image`, so the diagnostic runs
the real decoder, the real RCO path and the real cache. No `main.cpp` change was needed.

## 6. Verification - R

Build: `& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-gim` ->
`C:\dev\out\native-gim\re-ps3-native.exe`.

All 52 OSK textures exported in one run, exit 0, 52 PNGs; the 14 indexed pictures now
decode, the other 38 are unchanged:

| Format | Count | Textures | Status |
|---|---|---|---|
| 0x3 RGBA8888 | 7 | BUTTON_BASE_BLUE_GRAY, MODE_BASE, KB_MODE_BASE, SCROLL_BASE, SCROLL_KNOB, MENU_FOCUS, POINTER | 7/7 |
| 0xA DXT5 | 31 | the key faces, focus rings, candidate/menu/panel slices, BASE_NOTIFY, ... | 31/31 |
| 0x5 INDEX8 | 14 | the table in section 1 | 14/14 |
| **Total** | **52** | | **52/52** |

The fourteen, with their decoded result:

| Texture | Size | Palette | Result | Alpha |
|---|---|---|---|---|
| LABEL_UP | 72x54 | absent | white up triangle | 94 opaque, 31 partial, 3763 transparent |
| LABEL_DOWN | 72x54 | absent | white down triangle | legible |
| LABEL_LEFT | 72x54 | absent | white left triangle | legible |
| LABEL_RIGHT | 72x54 | absent | white right triangle | legible |
| EN_LABEL_DELETE | 140x44 | absent | "Backspace" | legible |
| EN_LABEL_SPACE | 140x44 | absent | "Space" | 161 opaque, 275 partial, 5724 transparent |
| EN_LABEL_RETURN | 140x44 | absent | "Line Break" | legible |
| EN_LABEL_MODE | 140x44 | absent | "Input Mode" | legible |
| EN_LABEL_CLOSE | 140x44 | absent | "Enter" | legible |
| KEYTOP_COPY | 72x54 | absent | copy icon | legible |
| KEYTOP_PASTE | 72x54 | absent | clipboard icon | legible |
| MODE_GUIDE_ALPHABET | 29x30 | absent | lowercase "a" | 37 opaque, 112 partial, 721 transparent |
| MODE_GUIDE_ALPHABET_CAP | 29x30 | absent | uppercase "A" | legible |
| LIST_WINDOW_FOCUS | 42x42 | absent | soft rounded focus square | 156 opaque, 960 partial, 648 transparent |

Visual evidence (checkerboard, alpha compositing):
`C:/dev/out/native-gim/sanity-label_up.png`,
`sanity-en_label_space.png`, `sanity-mode_guide_alphabet.png`,
`sanity-list_window_focus.png`, and the fourteen-cell
`sanity-contact-sheet.png`. Every non-transparent pixel is pure white (255,255,255), so
the coverage is the alpha channel and the shapes are legible.

Regression gate:
`bun work/ab-layers.ts --capture --exe C:/dev/out/native-gim/re-ps3-native.exe`
-> PASS 6/6 (frame 18.30 dB / 0.8323, top background 20.25 / 0.9363, category icon row
20.92 / 0.8990, item label row 11.95 / 0.3927, clock text 11.39 / 0.4105, info bar
14.78 / 0.6117), exit 0.

## 7. Limits and open points

* No firmware picture with a palette block exists in the local 4.93 dump, so the palette
  path is proven against the synthetic fixture only. A different firmware (PSP, or a
  theme package with an indexed GIM) would be the natural cross-check.
* INDEX4 high-nibble-first is the documented normal order and the fixture asserts it; no
  real 4-bit picture was available to confirm.
* INDEX16 entry order is big-endian like every other 16-bit field in this container.
* Palette entry formats 0x0..0x3 are supported; a palette block whose level type is not
  2, whose order is not 0, or whose entry count is 0 or above 65536 is rejected.
* INDEX32 (0x7) and 16-bit true colour (0x0..0x2) remain unsupported and still fail with
  `Unsupported GIM pixel format 0x...`.
* The fourteen masks are coverage-only. Their placement on the key face and their tint
  are the OSK draw's business, not the decoder's; the decoded images carry white RGB and
  the mask in alpha.

## 8. Files touched for this work

| File | Change |
|---|---|
| `src/assets.cpp` | palette helpers, `readGimPalette`, `decodeIndexed`, indexed branch and `XMB_GIM_TRACE` in `decodeGim`; signature takes the image name |
| `src/assets.h` | `decodeGim(bytes, name={})` declaration and its comment |
| `audit/GIM-FORMATS.md` | this document |
| `audit/OSK-FIRMWARE.md` | section 14: closes section 12.4 and points here |
| `work/osk/gim-inspect.ts` | extract/dump a GIM block tree from an RCO |
| `work/osk/gim-scan.ts` | inventory GIM formats and palette blocks across RCOs |
| `work/osk/gim-mask-check.ts` | palette presence and coverage-level report per format-5 picture |
| `work/osk/gim-preview.ts` | render a raw GIM under candidate interpretations |
| `work/osk/gim-palette-fixture.ts` | build the synthetic palette RCO fixture |
| `work/osk/gim-sanity.ps1` | checkerboard/contact-sheet evidence images |
