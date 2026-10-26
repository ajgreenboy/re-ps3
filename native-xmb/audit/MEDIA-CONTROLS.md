# Native media controls presentation model

`MediaControls` is a small C++ state model for playback-control visibility,
opacity, clock formatting, and bounded seek targets. It does not draw the
overlay or call the player. The view can use `state(nowMs)` to drive its own
QPainter presentation, while Shell remains responsible for playback actions.

## Presentation behavior

- `opened(nowMs)` starts playback presentation with controls fully visible.
- `activity(nowMs)` restarts the three-second visible interval.
- `setPaused(true, nowMs)` keeps controls fully visible. Resuming starts a new
  three-second interval.
- While playing, controls fade linearly over 250 ms after the visible interval,
  then report hidden with zero opacity.
- Backward timestamps are clamped to the greatest timestamp already observed.
- `formatTime` returns `MM:SS` below one hour, `H:MM:SS` at or above one hour,
  and `--:--` for unknown negative times.
- `seekTarget` clamps to zero and to a known nonnegative duration, and saturates
  safely for overflowing deltas. A negative duration means duration is unknown.

These timings describe this native proof-of-concept model. The available
reference material does not establish exact firmware overlay timing or layout;
those details remain pending reference evidence.

## Integrated 2026-10-26

View now uses this model for video controls. Keyboard, controller and pointer activity reveal the controls; pause/loading keeps them visible. Music metadata and photo controls remain visible. Shell routes ten-second seeks through the bounded helper and ignores unseekable sources. Elapsed/duration labels handle hours.

Release build passed. A generated local 30-second uncompressed AVI played through the actual Qt backend: controls were visible initially, hidden after 4.4 seconds, and visible when paused at 4.8 seconds. Left seek landed at zero and Right at 10000 ms while paused. A forced 301000 ms idle age produced a paused-video frame pixel-identical to the awake paused frame (PSNR Infinity, SSIM 1). Disposable native probes also passed 3000/3125/3250 ms visibility boundaries, pause/resume/input, hour formatting and extreme seek deltas.

Local evidence: out/native-reference/phase-continuation/video-*.png plus JSON sidecars, video.log and probe.log. No new formal test suite or fixture media was committed. Original player RCO panels and measured firmware fade curves remain open.
