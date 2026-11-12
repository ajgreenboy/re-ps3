# Full-window mapping and operation-guide alpha

Verified locally on Windows, 2026-11-10. Firmware: `C:/dev/rpcs3/bin/dev_flash`.
All firmware exports, captures, state sidecars and scratch settings remain outside the repository in
`%TEMP%/opencode/xmb-render-fix`.

## Options hint black rectangle

Reproduced on the Users column's User row at `--focus-time 2000`.
`before-wide.png` and the enlarged `hint-before.png` show an opaque black rectangle around the guide.
The triangle itself is the imagefont glyph, drawn by `View::drawOptionHint`, not a glass category icon.
The guide calls `drawNineSlice` for `system_plugin:tex_operation_guide_base_shadow` underneath its base.

`XMB_GIM_TRACE=1` identifies both guide images as DXT5 (GIM format 0xA).
The raw shadow's decoded 50x50 image has alpha 255 everywhere, including its black corner texels.
Its RGB carries coverage (0..56). The base has real alpha (0..217).
Treating the shadow as ordinary source-over RGBA therefore draws opaque padding, regardless of the
valid alpha in the triangle glyph and base. Neither the uniform cache nor the straight-alpha glass
icon upload/blend path supplies this texture; changing them would not fix this rectangle.

`Assets::loadImage` now converts this exact container/name's opaque RGB mask to black ink with RGB
luminance as alpha, once before caching. Textures with existing translucent alpha are left alone.
The general DXT/GTF decoders, GL texture formats, glass shaders and blend functions are unchanged.
The same corrected cached asset serves the Auto-Off guide. No replacement texture was added.

Final texture dump: corner RGBA `(0,0,0,0)`, alpha range 0..56; base still 0..217.
`final-wide.png`, `final-fullscreen.png`, `after-54.png` show the guide without the black rectangle.

## Empty space / inconsistent aspect handling

The window already supports fullscreen: kiosk sets the screen/geometry and calls `showFullScreen`;
ordinary launch honors `display/fullscreen`, while `--start-windowed` deliberately overrides it.
The initial 960x540 resize is the windowed fallback, not a forced fullscreen size. The default
fullscreen preference is false; the user's actual stored preference is not known or changed.
The final scripted `full` grab confirms actual 1920x1080 fullscreen on this machine.

Independently of window mode, the UI/backdrops/gallery/boot overlays used a centered minimum-axis
scale. At 1600x720 this reserves 160 pixels at each side for the 1280-wide fitted design. The glass
pass also centered a height-scaled viewport (and disagreed with the UI on narrower-than-16:9 windows).

`DesignSpace` now maps `(0,0)..(1920,1080)` directly to the entire output using separate x/y scales,
without cropping. Shared callers cover UI (including dialogs), backdrops, gallery chrome, boot text,
game-boot logo and Auto-Off guide. Glass icons use the full viewport and cut the refraction background
with matching x/y scales. The 3D boot logo keeps the design aspect before viewport stretch, and the
in-game captured backdrop draws into the whole window. Wave/background passes already fill the viewport.
At 16:9 both scales equal the previous scale and the former offsets are zero.

## Reproduction / evidence

Representative invocation (PowerShell, from the repository root):

```powershell
$d='%TEMP%/opencode/xmb-render-fix'
$env:XMB_LOG="$d/repro.log"
$env:XMB_TEXTURE_DUMP=$d
& out/native-xmb/re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash `
  --data-dir "$d/data-repro" --start-windowed --no-intro --focus-time 2000 `
  --render-clock 20 --ui-clock 2026-10-26T20:00:00 `
  --script "size=1600x720,wait,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,wait,grab=$d/repro-wide.png,quit"
```

Visually inspected:
- `before-wide.png`: 1600x720, centered menu layout and black Options border.
- `final-wide.png`: 1600x720, full-width menu mapping, no black Options border.
- `after-54.png`: 1000x800, full-height/width mapping, no letterbox, hint visible.
- `after-options.png`: 1000x800, Triangle options band reaches the window top/bottom.
- `final-fullscreen.png`: 1920x1080, normal design layout, corrected guide alpha.
- `final-dialog.png`: actually the User Information page, not a modal dialog; full-width mapping.

`final.log` records successful framebuffer saves at 1920x1080, 1600x720 and 1280x720.
The pixel-regression gate was not run. Build succeeded with the requested build.ps1 command:
`Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe`.
