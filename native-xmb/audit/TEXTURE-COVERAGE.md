# Texture coverage: every named image in the 4.93 UI art, and what the recreation draws

Scope: what fraction of the UI art the native recreation needs can be decoded by
`Assets::image` / `Assets::decodeGim` after `e90b196` (indexed and palette-less GIM support), what
still fails, and what the recreation does with the art it can now read. Written 2026-10-27.

Evidence classes, as in `audit/GIM-FORMATS.md`: **M** = measured from a firmware file, an export or a
decoder run; **F** = fit or inference; **R** = recorded run (a command and its result). Firmware bytes
stay local: pixels and logs under `out/`, nothing derived from the firmware enters the repo beyond
names, offsets, counts and results.

Evidence base: the sweep and every frame here were captured against
`out/native-gim/re-ps3-native.exe`, built 2026-10-27 22:15:40 from `src/assets.cpp` (22:09:39, the
decoder under audit). `src/assets.cpp` and `src/assets.h` did not change after that. Other files did:
`src/view.cpp`, `src/main.cpp`, `src/osk_layout.cpp` and `src/settings_menu.cpp` were edited by a
concurrent session between 22:18 and 22:39, after the capture; the OSK keytop draw was rewritten twice
in that window. All line numbers below were re-checked against the tree at 22:55 and are anchors, not
guarantees; the symbol names are the durable reference. One finding changed with those edits (2.2: the
`KEYTOP_*` face draw was fixed after the captured binary was built). The coverage numbers are unaffected
because the decoder sources are untouched.

**Headline.** Every named image in every local dev_flash 4.93 RCO decodes: **3174 / 3174 (100%)**, zero
failures, across 7 container/pixel-format classes. That includes the 224 palette-less format-0x5
pictures that `e90b196` added. The recreation's own consumption is **155 RCO names + 10 qgl DDS names,
all decoding**. The remaining fidelity gap is therefore no longer a decoder question: it is four draw
questions - the information-panel plate still draws the wrong asset, the controller indicator row is
not built, the OSK's decoded masks are declared but only two of them reach the screen, and
`bestImage` swaps one item icon for a different artwork of the same name.

## 1. The sweep - M/R

Every named image in every `vsh/resource/*.rco` was pushed through the real `Assets::image` path
(`src/assets.cpp:318`, which calls `decodeGim` for GIM entries at `src/assets.cpp:353`), one app run per
container, and every decoded image was written to a PNG. No new diagnostic was needed: `--export-image`
and `XMB_GIM_TRACE=1` already exist, and the sweep is a driver around them.

```powershell
# inventory: the RCO image tables and each GIM picture header (the fields Assets::readContainer reads)
bun work/tex-coverage.ts scan C:\dev\rpcs3\bin\dev_flash C:\dev\out\tex

# the sweep: 89 app runs, one per image-bearing container, real Assets::image + PNG export
bun work/tex-coverage.ts sweep C:\dev\rpcs3\bin\dev_flash ^
  C:\dev\out\native-gim\re-ps3-native.exe C:\dev\out\tex

# the aggregate tables again, from the recorded run
bun work/tex-coverage.ts report C:\dev\out\tex
```

`work/tex-coverage.ts` spawns
`<exe> --dev-flash <flash> --export-image "container:name,container:name,...@<dir>"` with
`XMB_GIM_TRACE=1` and `XMB_LOG=out/tex/logs/<container>.log`, then reads the log's `gim:` header line,
`assets: decoded` line and `assets: <container>/<name> <error>` warning per image. `plan.json` holds the
container-level view (entry format, compression, GIM header); `results.json` holds the decode outcome
per image.

### 1.1 Containers and images - M

| Measure | Value |
|---|---|
| RCO containers under `vsh/resource` | 122 |
| Containers with an image table | 89 |
| Containers with no images | 33 |
| Named images | 3174 |
| Decoded | **3174** |
| Failed | **0** |
| App runs (one per container) | 89, all exit 0 |
| PNGs written | 3174 (13.2 MiB) |
| Decoded area | 38.5 Mpx |

The 33 image-less containers are menus and plugins whose geometry is planes and text only
(`ap_plugin`, `msgdialog_plugin`, `search_service`, `user_info_plugin`, ...). `msgdialog_plugin`
carrying no images is expected: the message box's art lives in `system_plugin` (`src/dialogs.cpp:174`).

### 1.2 Breakdown by pixel format - M

GIM pixel formats and the two non-GIM container payloads, with the decoder status of each:

| Container payload | GIM format | Meaning | Images | Decoded | Area |
|---|---:|---|---:|---:|---:|
| GIM | 0x3 | RGBA8888 | 1931 | 1931 | 16.4 Mpx |
| GIM | 0xA | DXT5 | 842 | 842 | 9.2 Mpx |
| GIM | 0x5 | INDEX8 | 224 | 224 | 1.0 Mpx |
| GIM | 0x8 | DXT1 | 31 | 31 | 6.9 Mpx |
| GIM | 0x9 | DXT3 | 6 | 6 | 0.4 Mpx |
| RCO entry 0x0, stored | - | PNG | 136 | 136 | 3.0 Mpx |
| RCO entry 0x1, stored | - | JPEG | 4 | 4 | 1.6 Mpx |
| **Total** | | | **3174** | **3174** | **38.5 Mpx** |

Of the 3034 GIM entries, 2967 are zlib-compressed in the RCO and 67 are stored; both decode. The four
JPEG entries (`eula_net_plugin:BackGround`, `newstore_plugin:BackGround`, `regcam_plugin:BackGround`,
`videoeditor_plugin:tex_matsu`) decode through `QImage::fromData`, so they additionally depend on Qt's
`qjpeg` image-format plugin being shipped (`out/native-gim/imageformats/qjpeg.dll` here) - a
deployment fact, not a decoder fact.

**Failures by pixel format id: none. Failure list: empty.** `out/tex/report.txt` prints the list and the
reason histogram; a non-empty list is one line per image in the form
`FAIL <container>:<name> <format> <size> -- <decoder message>`. The same shape was what the sweep
produced before the driver passed a container prefix per item (see 1.5), and what
`Unsupported GIM pixel format 0x5` looked like before `e90b196` (`audit/GIM-FORMATS.md:14-23`).

### 1.3 The 224 format-0x5 pictures - M

The sweep confirms and sharpens the `GIM-FORMATS.md` finding:

| Measure | Value |
|---|---|
| Containers holding format-0x5 pictures | 2: `oskfullkeypanel_plugin` (138), `oskpanel_plugin` (86) |
| Pictures with a type-0x05 palette block | **0** |
| Size classes | 72x54, 140x44, 144x30, 29x30, 42x42 |
| Exported pixels with RGB != (255,255,255) where alpha > 0 | **0 of 224 pictures** |
| Pictures with more than two distinct alpha levels | 222 |
| Distinct alpha levels, minimum / maximum | 1 / 154 |
| Fully transparent pictures | 2 (`TEXTURE_NONE` 144x30, `BLANK_TEXTURE` 144x30) |

So all 224 decode as white coverage with the mask in alpha, and 222 of them carry a real anti-aliased
ramp. The two single-level pictures are deliberately blank placeholders, named as such by the firmware.

The 138 in `oskfullkeypanel_plugin` are the 14 names in `osk_layout.cpp`'s `kTextures` that used to
fail (`LABEL_UP/DOWN/LEFT/RIGHT`, `EN_LABEL_DELETE/SPACE/RETURN/MODE/CLOSE`, `KEYTOP_COPY/PASTE`,
`MODE_GUIDE_ALPHABET`, `MODE_GUIDE_ALPHABET_CAP`, `LIST_WINDOW_FOCUS`) plus 124 localised variants: the
OSK ships `DA_/DU_/FI_/FR_/GE_/IT_/JP_/KO_/NO_/PL_/PO_/RU_/SP_/SW_/TR_/CH_/CP_` label sets,
`FUNCTION_*` panel art, `KB_MODE_GUIDE_*` and `MODE_GUIDE_*` mode glyphs. The 86 in `oskpanel_plugin`
are the same idea for the alternate symbol-palette keyboard, which this build does not draw.

### 1.4 Runtime corroboration - R

The app's own OSK layout assertion, from the sweep's OSK run
(`out/tex/osk-run4.log`), asserts the same thing from inside the process:

```
osk: layout from firmware firmware: rco=oskfullkeypanel_plugin (132 objects, 203 images, 65 strings, 193344 B);
     module=absent (0 B, sha256 ); rows=committed; palette=committed; captions=RCO text table;
     textures=52/52 named, 52 decoded; geometry=measured reference (RCO panel planes width=0)
```

`src/osk_layout.cpp:864-872` makes `texturesDecoded` a real assertion (it fails if a future container
adds a format the decoder cannot read), so a regression here fails a build-time check, not a screenshot
review.

A scripted root render (`out/tex/run.log`) decoded 113 images with 0 failures; a scripted tour of all
ten columns (`out/tex/tour.log`) decoded 115 with 0 failures. No `Unsupported GIM pixel format` line
appears anywhere in the sweep.

### 1.5 Tool notes - R

Two `--export-image` behaviours the driver has to respect, both found the hard way and worth recording
because they cost a silent false negative:

* Every comma-separated item needs its own `container:` prefix. `main.cpp:488-497` splits each item on
  `:` and counts anything else as a failure, so `"c:a,b,c"` exports `a` and skips `b` and `c` with exit
  code 1 and no message. The first sweep run reported 89 decoded / 3085 "failed" for this reason alone.
  With `"c:a,c:b,c:c"` the same run reports 3174 / 0.
* An image name containing `/` decodes but cannot be saved (`assets_.image("qgl/lines",
  "textures/month_bg/rgb/10.dds")` decodes to 64x32 and then fails `image.save` because the nested
  directory does not exist). Decode status is still visible in the log; the PNG is not.

Neither needs a code change for this audit.

## 2. What the recreation actually consumes - M/R

The consumed set is the (container, name) pairs the sources name:

| Source | What it names |
|---|---|
| `src/shell.cpp:1012-1060` | the ten columns and their item rows: category icons (`qgl/icontex` `2n`..`8n`, `35n`, `xmb_plugin_normal:tex_psn`) and item icons from `explore_plugin_full`, `explore_category_sysconf/network/friend` |
| `src/view.cpp` | info bar + volume HUD (`system_plugin:tex_infobar`), notification plaque and its 13 glyphs (`system_plugin:tex_notification*`), category/item draw (`bestImage`), month backgrounds (`qgl/lines`) |
| `src/dialogs.cpp` | message box plate and bars (`system_plugin:tex_msgdialog_bg`, `_bg2`, `_bar`), hint glyphs (`system_plugin:tex_cross`, `tex_circle`) |
| `src/icons.cpp:165-166` | the glass icon shader's diffuse and environment maps (`qgl/icontex:dif`, `qgl/icons:widgets/other/texenv.dds`) |
| `src/osk_layout.cpp:429-482` | the 52 OSK textures |

Checked against the sweep with

```powershell
bun work/tex-coverage.ts consumed C:\dev\out\tex
```

which reports:

| Container | Consumed names | Decoded |
|---|---:|---:|
| `explore_plugin_full` | 26 | 26 |
| `explore_category_sysconf` | 16 | 16 |
| `explore_category_network` | 5 | 5 |
| `explore_category_friend` | 11 | 11 |
| `xmb_plugin_normal` | 6 | 6 |
| `xmb_ingame` | 11 | 11 |
| `system_plugin` | 28 | 28 |
| `oskfullkeypanel_plugin` | 52 | 52 |
| **RCO total** | **155** | **155** |
| `qgl/icontex` (DDS path, not an RCO) | 10 | 10 (checked by export) |

Every consumed name exists in its container and decodes. The per-surface detail:

| Surface | Asset | Decodes today | Before this change | What the real texture changes |
|---|---|---|---|---|
| Information-panel plate | `system_plugin:tex_infobar` (1x125 RGBA8888) | yes, but **wrong asset** | drawn as a 12% strip inside an invented black 20/255 rounded rect with a 4 px radius and a 1 px 90/255 white border (`src/view.cpp:1467-1470`) | the firmware plate is `xmb_plugin_normal:tex_indi_bg`, a **24x24 RGBA8888 9-slice tile that also decodes today**; using it gives the measured interior polarity (interior darker than the frame, 5.1%), the top/bottom/left white ramps, no right edge, and the real corner shape (`audit/ITEM-MENU-INFO-FIRMWARE.md:465-471`; `audit/FIRMWARE-PARITY-GAPS.md:153,196`) |
| Volume HUD | `system_plugin:tex_infobar` | yes, same wrong asset | same invented plate, reused verbatim (`src/view.cpp:1672-1678`) | same as above; the HUD would share one plate draw with the info bar instead of a second invented one |
| Controller / status indicator row | `xmb_plugin_normal:tex_indi_message`, `tex_indi_chat`, `tex_indi_online`, `tex_indi_plus` (32x32, DXT5) | **yes - and unused** (0 source references) | nothing is drawn; only the PS-tap HUD exists | the five 32x32 slots the firmware composes after the plate (`audit/ITEM-MENU-INFO-FIRMWARE.md:471` delta 7) |
| Busy indicator | `system_plugin:tex_busy` 36x36, `tex_busy_s`/`tex_busy_l` 8x36, `tex_busy_shadow` 46x46 | **yes - and unused** | nothing | the info panel's busy state (`page_busy`, `audit/ITEM-MENU-INFO-FIRMWARE.md` B) |
| Notification plaque | `system_plugin:tex_notification` 64x64 DXT5 | yes | real texture, fitted rect: 400x(58/84) at y145, 16 px nine-slice inset (`src/view.cpp:1015-1019`) | nothing in the decode; the rect is an untraced fit (`src/view.cpp:1006-1007` says so) |
| Notification glyphs | `system_plugin:tex_notification_*` 100x100 DXT5, 13 glyphs (plus the plaque above) | yes | real textures, real list (`src/shell.cpp:1318-1328`), drawn at plaque height - 12 | nothing: this surface is already the firmware's own art |
| Message box plate / bars | `system_plugin:tex_msgdialog_bg` 12x12 DXT5, `tex_msgdialog_bg2` 12x12 DXT1, `tex_msgdialog_bar` 960x1 RGBA8888 | yes | real textures sampled to one pixel / drawn 1:1, inside an invented 960-wide box rect (`src/dialogs.cpp:173-209`) | the box rect stays a fit (no local frame shows a modal box, `audit/DIALOGS-FIRMWARE.md:667`); the textures are already used correctly |
| Hint glyphs | `system_plugin:tex_cross`, `tex_circle` 30x30 RGBA8888 | yes | real textures | already correct; `tex_triangle`/`tex_rectangle` are unused but decode |
| Category icons | `qgl/icontex` `2n`,`3n`,`4n`,`5n`,`6n`,`7n`,`8n`,`35n` 128x128 DDS | yes (DDS path) | real normal maps, no substitute | nothing; `ICONS.mnu` drives material parameters, not pixels |
| PSN category icon | `xmb_plugin_normal:tex_psn` 128x128 | yes | real, chosen over the 46x46 copy via `bestImage` | nothing; this is the case `bestImage` was built for |
| Item icons | `explore_plugin_full` / `explore_category_*` set | yes | real, but three names are substituted by artwork size (2.1) | see 2.1: one of the three swaps a different artwork |
| Settings rows | `qgl/icontex:23n` (glass bullet) | yes | **invented**: every row uses the glass bullet instead of the firmware's row icon (`src/settings_menu.cpp:60-67`) | the 16 `explore_category_sysconf:tex_*` row icons decode; `audit/SETTINGS-FIRMWARE.md:543` (D21) needs a draw size hunk (18 px unfocused / 28 px focused) as well |
| OSK | 52 names in `oskfullkeypanel_plugin` | yes, 52/52 | faces procedural from four measured stops (`src/view.cpp:2097-2110`), badges real (DXT5 art), captions text, arrows font glyphs, keytops procedural | see 2.2: the 14 newly decoded masks are still mostly **not drawn at all**; the keytops' wrong face draw (the captured binary) was fixed by a concurrent edit in this session |
| Boot intro | RAF/GTF textures inside the boot animation container | not an RCO image | - | out of scope: `src/coldboot.cpp:80-129` reads CellGcm textures, not RCO images |

### 2.1 The same-name upgrades `bestImage` performs - R

`bestImage` (`src/assets.cpp:360-384`) swaps any drawn RCO image smaller than 96 px for the largest
same-name, same-aspect copy in any RCO. A scripted tour of the ten columns (`out/tex/tour.log`) records
exactly three substitutions:

```
assets: explore_plugin_full/tex_music QSize(46, 46) -> using explore_category_sysconf QSize(128, 128)
assets: explore_plugin_full/tex_photo QSize(46, 46) -> using xmb_ingame QSize(128, 128)
assets: explore_plugin_full/tex_video QSize(46, 46) -> using xmb_ingame QSize(128, 128)
```

`out/tex/evidence-substitution.png` puts each source next to the copy actually drawn:

| Pair | Verdict |
|---|---|
| `explore_plugin_full:tex_video` -> `xmb_ingame:tex_video` | **safe**: the same filmstrip glyph, 46 px upscaled to 128. Same for `tex_photo` (camera) |
| `explore_plugin_full:tex_music` -> `explore_category_sysconf:tex_music` | **wrong artwork**: the plain music note is replaced by the settings row's note-with-wrench. `explore_category_sysconf` sorts before `xmb_ingame`, and both copies are 128x128, so the strict `>` area test keeps the first one it meets |

So the correct asset decodes (`explore_plugin_full:tex_music`, 46x46) and is not used. The Music column's
"Music Settings" row therefore draws the System Settings family icon. This is a decoder-service policy
bug, not a format failure; the smallest honest fix is a bounded change in `assets.cpp` (below). No hunk is
included here because the smallest correct policy change is a few lines in `bestImage`, not a one-liner.

### 2.2 The OSK: 14 masks decoded, two drawn - M/R

`src/view.cpp:2088-2089` still says the 14 names "switch over by themselves when the decoder lands".
They do not, because `View::drawOsk` never reads `key.faceTexture` (grep: `faceTexture` appears only in
`osk_layout.cpp`), so:

| Decoded mask | Drawn today (tree at 22:55) | Evidence |
|---|---|---|
| `KEYTOP_COPY`, `KEYTOP_PASTE` | **yes**, as a tinted mask over the measured blue face (the prediction-button block in `View::drawOsk`). In the captured binary they were drawn as the whole button face, untinted (regression, see below); two concurrent edits at 22:36 and 22:39 replaced that with a tinted glyph at the card's own size | native OSK frame `out/tex/osk-native4.png` for the old draw |
| `LABEL_UP/DOWN/LEFT/RIGHT` | **no**: the arrow keys draw their caption glyph (a font triangle) over the procedural face, because `faceTexture` is unused | `out/tex/evidence-osk-keys.png` |
| `EN_LABEL_DELETE/SPACE/RETURN/MODE/CLOSE` | **no**: the captions are RCO text strings (`captions.backspace` etc.); no code path reads these textures | `src/osk_layout.cpp:364-375` |
| `MODE_GUIDE_ALPHABET`, `_CAP` | **no**: the mode cell draws `page->modeCaption` text (`src/view.cpp:2425`) | - |
| `LIST_WINDOW_FOCUS` | **no** | - |

The keytop case is worth recording even though it is now fixed, because it is the failure mode to watch
for when a coverage mask is wired: the decode flipped `drawOsk`'s own branch
(`if(!face.isNull()){p.drawImage(rect,face);continue;}`), which skipped the procedural pill and drew the
white mask as the key face. Measured on the captured frame: the copy/paste buttons read a neutral
`126,126,126` over a 36x28 box with pure-white glyph pixels, where the reference reads a blue
`135,162,167` with a dark glyph. Before the decode those two buttons took the procedural branch and were
closer to the reference. A mask is a glyph, not a face: the concurrent fixes tint the mask to the measured
ink colour and draw it at its own card size, which is the right shape.

The badge rows, the arrow keys and the copy/paste pair are compared at `out/tex/evidence-osk-badges.png`
and `out/tex/evidence-osk-keys.png`; the reference is `out/osk-fw/s1/f0060-0257.png`.

So the OSK masks have changed what is *decodable*, and only partly what is *correct*: two masks reach the
screen (now as glyphs), four arrow faces and the five caption masks still do not. The open work is item 3
in section 4.

### 2.3 Decodable art nothing draws - M

Unused but decoded, with the firmware object that wants it:

| Family | Names | Firmware consumer |
|---|---|---|
| Info-panel indicator | `xmb_plugin_normal:tex_indi_message/chat/online/plus` 32x32, `xmb_ingame` copies | five 32x32 slots on `page_xmb_indicator` |
| Info-panel plate | `xmb_plugin_normal:tex_indi_bg` 24x24, `xmb_ingame:tex_indi_bg` | `indi_base` 560x82 (drawn 591x58) |
| Busy state | `system_plugin:tex_busy`, `_s`, `_l`, `_shadow` | `page_busy` |
| Battery | `system_plugin:tex_battery_indicator` 30x30 | the PS-tap indicator (visible in `out/rpcs3-ps/frames/01-tap-02.png`) |
| Cursors | `system_plugin:tex_pointer_*` 53x54 (6) | pointer themes |
| Common widgets | `system_plugin:tex_default_*` (21), `tex_common_dialog`, `tex_operation_guide_base`, `tex_optionmenu_*`, `tex_arrow_*` | dialog and settings widgets |
| Settings row icons | `explore_category_sysconf:tex_*` (16) | the settings column rows (`audit/SETTINGS-FIRMWARE.md:543`, D21) |
| Symbol-palette OSK | `oskpanel_plugin` 86 masks + 148 images | the alternate keyboard panel |

The `xmb_ingame` 128x128 set (`tex_user`..`tex_friend`) is the in-game menu's category row; the
recreation uses `qgl/icontex` normal maps for the XMB row instead, and `xmb_ingame` only as a
`bestImage` source (2.1).

## 3. What still fails, and what each would take - M/F

Nothing in this firmware fails. Every format id the dump contains has a working path; format ids the
decoder does not implement have no picture here to fail on. The honest list of remaining holes:

| Hole | Format id | Example in this dump | What it would take |
|---|---|---|---|
| 16-bit true colour | 0x0 RGBA5650, 0x1 RGBA5551, 0x2 RGBA4444 | **none** (0 pictures) | a 2-byte-per-pixel branch beside the RGBA8888 path (`src/assets.cpp:300-311`) with the same pitch alignment; the colour helpers already exist (`gimColour565/5551/4444`, `src/assets.cpp:36-46`) |
| 32-bit indices | 0x7 INDEX32 | **none** | a 4-byte big-endian index read in `decodeIndexed` plus an index bound that can exceed the 65536-entry palette guard (`src/assets.cpp:74`) |
| Palette-bearing indexed | 0x4/0x5/0x6 with a type-0x05 sibling | **none**: 0 of 224 format-5 pictures has a palette block | nothing to build; the path exists and is proven only against the synthetic fixture (`audit/GIM-FORMATS.md:152-191`) |
| PSP pixel order | any, `order=1` | **none**: all 3034 pictures are `order=0` | an unswizzle before decode (the theme decoder already has one, `src/p3t.cpp:597`); would fail today with `Unsupported GIM pixel order` (`src/assets.cpp:271`) |
| Other palette entry formats | palette header format > 0x3 | **none** | extend the 0x0-0x3 table in `readGimPalette` (`src/assets.cpp:69-71`) |
| Unknown RCO image compression | entry format 0/1/5 with compression > 1 | **none** | `Unsupported RCO image compression` (`src/assets.cpp:352`); would need the corresponding codec |
| Theme packages (a *second* GIM decoder) | palette-less indexed inside a `.p3t` | no local theme with one | `P3t::gim` returns a null image when an indexed picture has no palette (`src/p3t.cpp:599-600`). A user theme whose icon is a coverage mask renders nothing. The smallest fix is the same coverage fallback `decodeGim` now uses, or delegating to `Assets::decodeGim` |
| Genuine non-images | - | `oskfullkeypanel_plugin:TEXTURE_NONE`, `oskpanel_plugin:BLANK_TEXTURE` (144x30, fully transparent) | nothing: these are blank placeholders and decode to a transparent image, which is correct |

One decoder behaviour worth stating because it bounds the confidence above: `readGimPalette`
(`src/assets.cpp:54-92`) scans only the picture's own children, so a palette block outside the picture
container would be ignored and the picture would decode as coverage instead of colours. No local file
exercises that shape (0 palettes found), so it is a theoretical limit, not a defect.

## 4. Adoption order

Ranked by how much of the visible frame each changes, with the draw site that would own it and the
reference frame that proves it matters. All of these assets decode today (section 1).

1. **`xmb_plugin_normal:tex_indi_bg` (24x24, RGBA8888) - the information-panel plate.**
   Draw site: the info-bar block in `View::drawScene` (`src/view.cpp:1461-1470`) and
   `View::drawVolumeHud`
   (`src/view.cpp:1672-1678`), replacing `system_plugin:tex_infobar` with a nine-slice into the measured
   591x58 rect. Reference: `out/rpcs3-ref-2026-10-28/dialogs/root-clock-mmdd-12h.png` (the plate is in
   every frame) and the crop evidence at `out/tex/evidence-infobar.png`, which puts the reference crop,
   the 24x24 tile and the 1x125 gradient side by side. This is the largest single win because the bar is
   on every screen, and the measured delta is already written down
   (`audit/ITEM-MENU-INFO-FIRMWARE.md:465-471`: wrong asset, inverted fill, wrong border, missing
   indicator row).

2. **`xmb_plugin_normal:tex_indi_message/chat/online/plus` (32x32) - the indicator row.**
   Draw site: the same `View::drawScene` info-bar block, at the firmware's fixed offsets from the clock. Reference:
   `out/rpcs3-ps/frames/01-tap-02.png` shows the panel gaining a controller glyph and a battery
   indicator on PS tap - the state the recreation currently draws with the PS HUD only
   (`audit/FIRMWARE-PARITY-GAPS.md:196`).

3. **`oskfullkeypanel_plugin:LABEL_UP/DOWN/LEFT/RIGHT` (72x54 masks) - the arrow keys' faces.**
   Draw site: `View::drawOsk` (`src/view.cpp:2358-2382`), whose `faceTexture` is never read; the arrows
   currently draw a font triangle caption over the procedural face. Reference:
   `out/osk-fw/s1/f0060-0257.png` (the triangle fills the left of the key face, no caption), with the
   comparison at `out/tex/evidence-osk-keys.png`. `KEYTOP_COPY/PASTE` were the same class of fix and
   landed in two concurrent edits during this audit (the prediction-button block of `View::drawOsk`),
   which is the shape to copy: tint the mask, do not use it as the face.

4. **`explore_plugin_full:tex_music` (46x46) - stop the same-name swap that changes the artwork.**
   Draw site: the policy in `Assets::bestImage` (`src/assets.cpp:360-384`), which serves
   `View::drawImage`/`drawItemGlyph`. Reference: `out/rpcs3-ref-2026-10-28/media/music-column-empty.png`
   shows the Music column's own note art, and `out/rpcs3-ref-2026-10-28/nav/restore-column.png` shows the
   wrench-disc family that belongs to Settings rows. `out/tex/evidence-substitution.png` shows the two
   artworks. The smallest bounded fix is to accept a same-name upgrade only when the two images are
   pixel-identical after a nearest-neighbour downscale (or to restrict the search to containers in a
   known-list); either keeps `tex_video`/`tex_photo` upgrades and drops the `tex_music` one.

5. **`explore_category_sysconf:tex_*` (16 row icons, 128x128) - the settings rows.**
   Draw site: `src/settings_menu.cpp:60-67` (row construction) plus the item-glyph size in
   `View::drawItemGlyph` (`src/view.cpp:963`), per `audit/SETTINGS-FIRMWARE.md:543` (D21: `tex_sett` or
   the row's `icon_rsc`, 18 px unfocused / 28 px focused). Reference:
   `out/rpcs3-ref-2026-10-28/nav/restore-column.png` and `out/rpcs3-xmb-cap/nav-settings.png`, where every
   Settings row carries the brass wrench disc rather than a glass bullet.

Not on the list, deliberately: the notification pop-up and message box already draw the firmware's own
textures, and their remaining gap is untraced geometry, not art; the 86 `oskpanel_plugin` masks belong
to a keyboard this build does not have.

## 5. Commands and files

| Purpose | Command |
|---|---|
| Inventory (RCO tables + GIM headers) | `bun work/tex-coverage.ts scan C:\dev\rpcs3\bin\dev_flash C:\dev\out\tex` |
| Full decode sweep | `bun work/tex-coverage.ts sweep C:\dev\rpcs3\bin\dev_flash C:\dev\out\native-gim\re-ps3-native.exe C:\dev\out\tex` |
| Aggregate tables | `bun work/tex-coverage.ts report C:\dev\out\tex` |
| Consumed-set cross-reference | `bun work/tex-coverage.ts consumed C:\dev\out\tex` |
| One container by hand | `$env:XMB_GIM_TRACE='1'` then `re-ps3-native.exe --dev-flash <flash> --export-image "oskfullkeypanel_plugin:LABEL_UP@<dir>"` |
| qgl DDS path check | `re-ps3-native.exe --dev-flash <flash> --export-image "qgl/icontex:1n,qgl/icontex:2n,...@<dir>"` |
| OSK state for a frame | `re-ps3-native.exe --dev-flash <flash> --data-dir <tmp> --no-intro --background-clock <iso> --render-clock 30 --script "pause=800,ESC,pause=800,LEFT,...,ENTER,pause=2500,grab=<png>,quit"` |
| Column tour (finds `bestImage` swaps) | same, with `XMB_LOG` set and a LEFT/RIGHT tour |

Recorded evidence under `out/tex/`: `plan.json`, `results.json`, `report.txt`, `logs/<container>.log`,
`png/<container>/<name>.png`, `run.log`, `tour.log`, `osk-native4.png`, `osk-run4.log`,
`evidence-substitution.png`, `evidence-infobar.png`, `evidence-osk-badges.png`,
`evidence-osk-keys.png`.

Files touched by this audit:

| File | Change |
|---|---|
| `native-xmb/audit/TEXTURE-COVERAGE.md` | this document |
| `work/tex-coverage.ts` | new analysis helper: `scan`, `sweep`, `report`, `consumed` |

No rendering source was changed. No decoder fix was applied.

## 6. Limits

* Coverage is measured as "the decoder returned pixels", which is not the same as "the right pixels".
  The 224 masks were checked against their own alpha statistics and against the console frames for the
  14 OSK names (`audit/GIM-FORMATS.md:66-70`, and 2.2 above); the other 2950 images were spot-checked
  (category icons, item icons, the info-bar tile, notification glyphs, the OSK art), not compared
  one by one to a console frame. A mis-decoded DXT picture would not be caught by this sweep.
* The sweep exercises `Assets::image`, not `bestImage`; the three substitutions in 2.1 come from a
  scripted tour and are therefore limited to the names those ten columns actually draw.
* The 4 JPEG-backed images depend on Qt's `qjpeg` plugin at deployment.
* No theme package with an indexed GIM exists locally, so the `P3t::gim` hole in section 3 is measured
  from the code path, not from a failing file.
* The OSK reference frame is a video extraction (`out/osk-fw/s1/f0060-0257.png`) at 1920x1080; the
  native frame compared against it was grabbed at 960x540 because the stored window geometry is 960x540,
  so the comparison crops were scaled 2x. The comparison is shape and colour, not a pixel score.

## 7. Decoder policy fixes (2026-10-27, after the sweep)

The sweep measured coverage; two policies still served the wrong picture. `bestImage` replaced
`explore_plugin_full:tex_music` with `explore_category_sysconf:tex_music` (section 2.1, item 4), and the theme
reader was a second GIM parser that returned no picture for palette-less indexed art (section 3). Both are now
fixed in the decoder files. No draw site changed. Evidence classes as above: **M** measured, **R** recorded
run, **F** fit.

### 7.1 `bestImage`: a same-name copy is an upgrade only when it is the same picture - M/R

The rule lives in `src/assets.cpp` (`sameArtNames`, `artSignature`, `compareArtwork`, `bestImage`). A source
smaller than 96 px is replaced only when two independent gates both pass:

1. **Name gate (intent).** Only `tex_psn`, `tex_video` and `tex_photo` may be upgraded across containers. An
   explicit list is required because intent is not a pixel property: `tex_music`'s glass copy in
   `xmb_ingame` passes the pixel gate (its silhouette correlates 0.94 with the flat note) yet it is a different
   icon set's art. A name enters this list only after the exported pictures were compared by eye.
2. **Pixel gate (evidence).** For an allow-listed name the candidate must match the source after both
   pictures are cropped to their own alpha bounds (alpha > 8) and resampled to 32x32 premultiplied:
   alpha correlation >= 0.80, alpha-silhouette IoU >= 0.75, premultiplied-colour correlation >= 0.80, and
   alpha-bbox aspect ratios within 12%. The candidate must also be strictly larger in area.

A rejected copy never replaces the source. Every decision is logged at `qInfo`; `XMB_BESTIMAGE_TRACE` adds
each candidate and its numbers (7.3). The measured pairs, from the exported PNGs through the same signature
the C++ uses:

| Source -> candidate | alpha corr | coverage IoU | colour corr | aspect | decision |
|---|---:|---:|---:|---:|---|
| `explore_plugin_full:tex_music` 46 -> `explore_category_sysconf:tex_music` 128 | -0.06 | 21% | -0.06 | 1.38 | reject: different artwork (wrench overlay) |
| `explore_plugin_full:tex_music` 46 -> `xmb_ingame:tex_music` 128 | 0.94 | 93% | 0.94 | 1.00 | reject: name gate (glass icon set, not the column's flat note) |
| `explore_plugin_full:tex_video` 46 -> `xmb_ingame:tex_video` 128 | 0.87 | 85% | 0.87 | 0.95 | accept |
| `explore_plugin_full:tex_photo` 46 -> `xmb_ingame:tex_photo` 128 | 0.96 | 94% | 0.96 | 0.98 | accept |

The decision lines for the reported bug, from a scripted Music-column run (`out/bestimg/tour-music-*.log`,
`src/assets.cpp` at 22:52; the "before" binary was built from the tree before the fix):

```
before: assets: explore_plugin_full/tex_music QSize(46, 46) -> using explore_category_sysconf QSize(128, 128)

after:  assets: bestImage: source explore_plugin_full/tex_music 46x46, same-art allow-list no
        assets: bestImage: candidate explore_category_sysconf/tex_music 128x128 reject
        assets: bestImage: candidate metrics explore_category_sysconf/tex_music alpha corr -0.06, coverage IoU 21%, colour corr -0.06, aspect 1.38
        assets: bestImage: candidate xmb_ingame/tex_music 128x128 reject
        assets: bestImage: candidate metrics xmb_ingame/tex_music alpha corr 0.94, coverage IoU 93%, colour corr 0.94, aspect 1.00
        assets: bestImage: explore_plugin_full/tex_music 46x46 keeping source (name not in the same-art allow-list; 2 larger same-name copies rejected: ...)
```

The frame agrees: in `out/bestimg/tour-music-before.png` the Music Settings row draws the note with the
settings wrench; in `out/bestimg/tour-music-after.png` it draws the plain note. Both frames are the same
column, clock and state; the row crops are side by side in `out/bestimg/evidence-music-row.png`. The two
verified upgrades are unchanged in the same run:

```
assets: bestImage: explore_plugin_full/tex_video 46x46 -> using xmb_ingame 128x128, same artwork confirmed (alpha corr 0.87, coverage IoU 85%, colour corr 0.87, aspect 0.95)
assets: bestImage: explore_plugin_full/tex_photo 46x46 -> using xmb_ingame 128x128, same artwork confirmed (alpha corr 0.96, coverage IoU 94%, colour corr 0.96, aspect 0.98)
```

A tour of all ten columns with `XMB_BESTIMAGE_TRACE=1` reports six decisions in total: the two accepted
upgrades, the rejected `tex_music`, and three images with no larger same-name copy (`user_tex_load`,
`user_tex_login`, `explore_category_friend:item_tex_Profile_Onlines`). No other artwork changes.

### 7.2 Theme pictures decode through the shared GIM reader - M/R

`P3t::gim` (`src/p3t.cpp`) no longer parses GIM itself; it calls `Assets::decodeGimImage`, the same decoder the
RCO path uses. The old reader returned a null image whenever an indexed picture (format 0x4/0x5/0x6) had no
palette block, so a theme icon, pointer or notification in the palette-less coverage-mask form rendered
nothing even though `Assets::decodeGim` already read that form for the OSK (section 1.3). The shared decoder
also adds the DXT formats the theme reader never supported.

`vsh/resource/theme/01.p3t` (Air Paint, the only retail theme local): 92 elements, 2 bgimages, 63 `icon`
pictures, `info/icon` and `info/preview`. All 65 GIM pictures are format 0x3 and all 65 decode both before and
after; the 2 backgrounds are PNG/JPEG through `QImage::fromData` and are unaffected. 65 of 65 exported PNGs
are pixel-identical between the two dumps, so delegating changed no theme pixels for real content.

This theme carries no indexed picture, so it cannot exercise the fix (`out/theme-gim-scan.ts`: `formats=[0x3]`
for all 65). A local fixture therefore carries real firmware pictures: `out/make-theme-mask-fixture.ts` packs
the extracted `LABEL_UP`, `KEYTOP_COPY`, `MODE_GUIDE_ALPHABET`, `LIST_WINDOW_FOCUS` and `EN_LABEL_SPACE`
format-0x5 masks (all with no palette block) plus one genuine format-0x3 icon from Air Paint into a valid P3TF
theme (`out/mask-fixture/mask-theme.p3t`). Picture-slot decode counts:

| Theme | Before | After |
|---|---|---|
| `01.p3t` GIM pictures (63 icons + theme icon + preview) | 65 / 65 | 65 / 65 |
| `01.p3t` backgrounds (PNG/JPEG) | 2 / 2 | 2 / 2 |
| mask fixture (4 icons + theme icon + author icon + pointer + notification) | **1 / 8** | **8 / 8** |

The fixture's `--p3t-dump` traces, same command:

```
before: p3t-trace: info ... themeIcon 0x0 authorIcon 0x0 preview 0x0
        p3t-trace: ... pointers [pointer_arrow@7,9(0x0)] notification 0x0
        p3t: Mask Fixture ... 1 icons, 0 sounds

after:  p3t-trace: info ... themeIcon 72x54 authorIcon 42x42 preview 0x0
        p3t-trace: ... pointers [pointer_arrow@7,9(140x44)] notification 29x30
        p3t: Mask Fixture ... 4 icons, 0 sounds
```

The decoded `icon_mask_label_up.png` is 72x54, white with 15 alpha levels, and is pixel-identical to the same
mask exported from its RCO (`out/tex/png/oskfullkeypanel_plugin/LABEL_UP.png`): the theme path and the RCO
path now run the same code.

### 7.3 Diagnostics and verification - R

`bestImage` decision trace, for one named image or for every call. The shell starts in the Game column
(index 6), so three LEFTs reach Music:

```powershell
$env:XMB_BESTIMAGE_TRACE='tex_music'   # or '1' / 'all' for every call, or a comma-separated name list
re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash --data-dir <tmp> --no-intro `
  --background-clock 2026-10-25T10:00:00 --render-clock 20 `
  --script "full,wait,wait,LEFT,pause=800,LEFT,pause=800,LEFT,pause=2500,grab=<png>,quit"
```

`XMB_GIM_TRACE=1` is unchanged for RCO pictures; theme pictures are decoded by the same function, so the same
trace prints their headers and results. A theme payload that is not a readable GIM warns
`assets: GIM picture skipped - <reason>` instead of failing silently.

Per-layer gate (`work/ab-layers.ts --capture --exe C:/dev/out/native-decode/re-ps3-native.exe`):

| Layer | Before | After |
|---|---|---|
| frame | 18.28 dB / 0.8322 | 18.29 dB / 0.8342 |
| top background | 20.25 / 0.9363 | 20.25 / 0.9363 |
| **category icon row** | **20.93 / 0.8990** | **20.93 / 0.8990** |
| item label row | 11.95 / 0.3925 | 11.95 / 0.3925 |
| clock text | 11.39 / 0.4128 | 11.39 / 0.4500 |
| info bar | 14.77 / 0.6113 | 14.98 / 0.7639 |
| result | PASS 6/6 | PASS 6/6 |

The icon row and the item label row are bit-identical before and after. The only pixels that moved are inside
the info-bar crop (which also holds the clock text), and they are not attributable to these files: a concurrent
session was editing the text/rendering sources while the two binaries were built (`src/text_renderer.cpp`
22:45:03, `src/view.cpp` 22:53:37), and only those two layers moved. The canonical capture is the Game column,
where every item icon is >=96 px and `bestImage` returns before reaching the new policy, and the theme reader
is not on the draw path. A rebuild after the concurrent edits reproduced the after numbers exactly.

Two captures with `--render-clock 20` and pinned clocks are byte-identical:
`E8AE6E6FE2C58FE2D84B191D6C25392ADC4C598A56ECC276ACAE829275179AAD` for both runs.

### 7.4 Files and limits

| File | Change |
|---|---|
| `native-xmb/src/assets.cpp` | same-art name list, alpha-bounds signature, artwork comparison; decision/reason logging; `XMB_BESTIMAGE_TRACE`; `decodeGimImage` |
| `native-xmb/src/assets.h` | `bestImage` policy contract, `decodeGimImage` declaration |
| `native-xmb/src/p3t.cpp` | `P3t::gim` delegates to the shared decoder; removed the private GIM reader |
| `native-xmb/src/p3t.h` | `gim` contract note |
| `native-xmb/audit/TEXTURE-COVERAGE.md` | this section |
| `native-xmb/audit/THEMES.md` | section 8: the theme decoder finding |

Remaining holes, all measured rather than assumed:

* **No real theme with an indexed picture exists locally.** The only retail theme (`01.p3t`) is 100%
  format 0x3, so the fix is proven on a local fixture carrying real firmware masks, not on a user theme. A
  user theme whose icons are palette-less masks is the same code path; it is still an unobserved real file.
* **A palette-bearing GIM in a theme** (format 0x4/0x5/0x6 with a type-0x05 palette block) has no local
  example in either an RCO or a theme; the shared decoder's palette path is proven only by the synthetic
  fixture in `audit/GIM-FORMATS.md`.
* **Little-endian GIM ("MIG.") payloads no longer decode.** The private theme reader accepted the PSP byte
  order; the shared PS3 decoder rejects it. No local theme or RCO carries one, and PS3 content is big-endian,
  but this is a behaviour removal, not a no-op by construction.
* **The allow-list is a judgement, not a measurement.** `tex_video` and `tex_photo` are accepted on the
  audit's same-art verdict plus a matching silhouette; their 128 px copies are a glossier finish of the same
  subject. A strict pixel rule could call them different artwork too. They are kept explicitly because the
  audit verified them as harmless, and the signature confirms the shape; `tex_music` is excluded because its
  wrench variant is unambiguously a different picture.

