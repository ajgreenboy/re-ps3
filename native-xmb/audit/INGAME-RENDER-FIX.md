# In-game rendering, silent quit, and aspect-correct fill (2026-11-10)

Built on `b67ad99`. Verification uses an imported Win32 stand-in, not a real game.

## Changes

- `View::drawInGameBackdrop` previously constructed a `QOpenGLPaintDevice` and `QPainter`, then drew
  the captured `QImage` and a dimming rectangle on **every frame**. This is Qt's OpenGL paint engine,
  not proof of a software rasterizer; the confirmed waste is repeated painter/image submission instead
  of an explicitly resident texture. The image's `cacheKey()` now controls a single GL texture upload
  per changed capture. Unchanged frames draw one fullscreen triangle; the fragment shader applies the
  same quantized `XMB_INGAME_DIM` factor. Upload/deletion occurs only with the widget context current.
  `XMB_PS_LOG=1` logs each upload, not each draw.
- The existing live worker remains single-flight, with a single-shot 250 ms timer restarted after
  completion. No extra render loop was added. Empty captures clear to `(8,8,12)`; they cannot reuse a
  previous title's texture. The normal UI/icon passes receive the default FBO, device viewport,
  texture unit zero, no bound backdrop program/texture/VAO, and blending/depth/scissor disabled.
- The tracked-process completion handler and Steam stop watcher used `showMessage(...,
  "The game was closed.", MessageKind::Page)`: a dismissible titled page, **not a notification**.
  Those pages and normal-return notification toasts are removed. The pre-quit firmware Yes/No box,
  cooperative close/grace/kill stages, and unexpected-crash diagnostics remain.
- `DesignSpace` previously used `width/1920` and `height/1080` independently. It now uses their
  maximum for both axes and centered offsets. UI/text/dialogs, GL icons and their refraction cuts,
  coldboot/gameboot, gallery, notifications, background/ribbon/particles, and painter backdrops
  share crop-fill coordinates. GL scene projections remain 16:9 and use the matching cropped
  viewport; already-rendered FBO composites still use the full output viewport (no double crop).
  Captured game images use centered, aspect-preserving texture-coordinate crop-fill. At 16:9,
  design-space scale/offsets are identical to before. Cropping the clock/help at extreme aspects
  is intentional; fitting all UI would require bars or a separate responsive layout.

## Reproduction and evidence

Scratch root (logs, frame traces, PNGs and JSON sidecars; not committed):
`%TEMP%/opencode/ingame-perf`.

Manifest imported through `--import-apps apps.json --data-dir <root>/data`:

```json
{"version":1,"apps":[{"id":"perf-stub","title":"Performance Stub","kind":"game","category":"game","executable":"C:/dev/out/ps-standin.exe","arguments":["%TEMP%/opencode/ingame-perf/stub.log"]}]}
```

All GUI runs use `--dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <root>/data --no-intro`,
`XMB_PS_LOG=1`, `XMB_LOG=<run>.log`, an empty `XMB_STEAM_ROOT`, and `XMB_STEAM_ARTWORK=0`.
The steady runs additionally use `XMB_FRAME_TRACE=<run>-frames.log`.
Script tokens are 300 ms apart. Recipe: `full`, 10 waits, `DOWN,ENTER`, 12 waits, `ps`, 55 waits,
`grab=<run>.png,pshold`, 7 waits, `ENTER`, 3 waits, `grab=<run>-confirm.png,LEFT,ENTER`,
10 waits, `grab=<run>-return.png,quit` (final steady run uses 6 final waits).

### Performance

1920x1080 XMB, 626x353 captured stand-in window, default still capture. Measure only elapsed
13-23 seconds in `XMB_FRAME_TRACE`, after capture/entry and before menu/quit:

| Run | swap callbacks/s | paint CPU mean | median | p95 |
|---|---:|---:|---:|---:|
| `before` (original executable) | 143.7 | 0.388 ms | 0.295 ms | 1.108 ms |
| `after` (texture fix, before crop-fill) | 143.9 | 0.360 ms | 0.263 ms | 1.127 ms |
| `final-steady` (all fixes) | 143.8 | 0.331 ms | 0.245 ms | 1.057 ms |

The display was already near its 144 Hz limit; this proves modest CPU savings and no pacing regression
on this small static stand-in, **not** an FPS improvement on a demanding game. These are CPU dispatch
and Qt swap callbacks, not GPU completion or physical presentation measurements. A separate `final`
run was excluded from timing: its log explicitly records Task Switching/OpenCode taking foreground
and the expected title-session render pause. `final-steady` has no such interruption in the interval.
Exactly one backdrop upload is logged in the still run.

`live.log`: `XMB_INGAME_LIVE=1 XMB_INGAME_DIM=0.25`, multiple new capture keys/uploads, screenshots
`live-a.png`, `live-b.png`, `live-wide.png`, and a steady log interval of 141.83 fps. Each update still
comes through the bounded worker/timer. Titlebar sample `(300,20)` is about 183 with dim 0.25 versus
about 123 with default dim 0.5. The stand-in is static, so this verifies image delivery/re-upload,
not changing real-game content or GPU load.

### Quit and fallback

`final-confirm.png` shows the pre-quit Yes/No prompt. After Left/Enter chooses Yes, `stub.log`
records `WM_CLOSE`, destruction, exit; `final-steady.log` reports cooperative completion.
`final-return.png` / `final-steady-return.png` show the normal XMB with no completion page;
sidecars have `titleRunning=false`, `inGame=false`, `pageKind=root`, `notificationCount=0`.

For `fallback.log`, minimize the stand-in with Win32 `ShowWindow(hwnd,SW_MINIMIZE)` before the
scripted PS tap. Log: `mode=minimized capture=failed ... backdrop=plain dark`. `fallback.png`
has `(8,8,12)` at `(300,20)`, no wave, no stale image, and the ordinary in-game UI. Quit remains
cooperative. Steam's silent completion was inspected in code, not run against a real Steam title.

### Aspect ratio

`--start-windowed --render-clock 10 --script` with `size=1600x720`, waits, `grab=wide.png`,
`size=1000x800`, waits, `grab=tall.png`, `full`, waits, `grab=full.png`, `quit`.
Both non-16:9 grabs fill edge-to-edge and show unsquashed text/circular artwork. Measured blue
Steam disc bounds: **90x90** in the wide grab, **79x80** in the tall grab (raster rounding).
`live-wide.png` also exercises the captured backdrop and icons at 1600x720. Fullscreen is 1920x1080.
Gallery and coldboot non-16:9 behavior was aligned by code inspection, not exhaustively replayed.

Build command: the requested `native-xmb/build.ps1 -OutputDirectory C:/dev/xmb/out/native-xmb`.
Result: `Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe`.

## Update, 2026-11-12 (render-parity sweep 2): contain-fit instead of crop-fill

`DesignSpace` now takes the minimum of `width/1920` and `height/1080`. The crop-fill above cut the clock, the lower
item rows, the music player's bar and hints and the video control panel off a 2560x1080 window, and both sides off a
900x1000 window (grabs under `out/par2` of that sweep). With contain-fit the whole 1920x1080 design area is always on
screen and undistorted; a window that is not 16:9 shows black bars. At 16:9 the transform is unchanged, so every
pinned state is unaffected. The captured game image is letterboxed the same way.
