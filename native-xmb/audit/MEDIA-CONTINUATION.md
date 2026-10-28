# Native media continuation - 2026-10-25

> **Historical pass record - 2026-10-25, banner added 2026-10-28.** This file records one media slice
> (owned playback queue, photo viewport, the controls that reached them). It is not current state: the
> later media work is in [SUBTITLES.md](SUBTITLES.md) (sidecar subtitles and the player panel),
> [MEDIA-CONTROLS.md](MEDIA-CONTROLS.md) (Continue Watching and resume records) and
> [MEDIA-PANELS-FIRMWARE.md](MEDIA-PANELS-FIRMWARE.md) (what the firmware's video/music/photo panels and
> volume display actually are). Superseded claims inside this file: the zoom percentage is a native
> affordance the firmware does not have (no numeric zoom exists - MEDIA-PANELS-FIRMWARE 7.4); the
> "original panel" gap it implies is now specified rather than unknown; and background playback, which
> this pass left absent, is still absent (ledger rows A3/8.2). Current status: the ledger in
> [PHASES-2026-10-28.md](../PHASES-2026-10-28.md) section 1; scored gap: [FIRMWARE-PARITY-GAPS.md](FIRMWARE-PARITY-GAPS.md).

## Implemented

Music/video playback now snapshots the current folder's playable entries into an owned PlaybackQueue. It
advances on end, supports Off/Repeat One/Repeat All, shuffle without replacement within a cycle, previous/next
and bounded previous-track history. A reshuffled cycle avoids immediately repeating its predecessor.
Manual skipping ignores Repeat One. Files that fail are skipped; a fully failing queue stops after a bounded
number of attempts. Deferred callbacks carry a playback generation so Back or a new selection cancels them.
Returning to the list keeps the selected track. Pause/seek remain on the same QMediaPlayer instance.

Preferences: media/repeatMode (0/1/2) and media/shuffle. The previous media/repeat flag keeps its repeat-current
meaning for music/video until a new mode is explicitly chosen; photo slideshow repeat remains independent.
Music/Video Settings expose the new choices. Playback shows track position and mode.

Photos now have a separate geometry component for fit/fill zoom stops, bounded pan, reset and zoom percentage.
Zoom limits/stops and the 5%-viewport pan step adapt MaXMB's photoviewer.js. Opening/rotating a photo resets the
view; source files remain unchanged. Manual zoom/pan pauses a running slideshow. Rotation still transforms
the decoded image in memory. Zoom percentages refer to that bounded decoded image, not original-resolution
detail when the existing decoder downsizes a large source.

## Controls

| Operation | Keyboard | Controller |
|---|---|---|
| Music/video previous / next | [ / ] | L1 / R1 |
| Shuffle | M | Square |
| Cycle repeat | R | Triangle |
| Seek | Left / Right | D-pad Left / Right |
| Photo zoom | + / - | R1 / L1 |
| Photo reset | 0 | Square |
| Photo rotate | Q / E; Up/Down when fitted | Triangle; D-pad Up/Down when fitted |
| Photo pan when overflowing | Arrows | D-pad / mapped left stick |
| Previous/next photo even when zoomed | [ / ] | Reset then D-pad Left/Right |

Script tokens add PREV, NEXT, SHUFFLE, REPEAT, ZOOMIN, ZOOMOUT, RESETVIEW, ROTATELEFT and ROTATERIGHT.

## Evidence and limitations

Default Release and isolated out/native-media builds succeeded. Five local GUI replays exited zero:

- Three-entry queue: first WAV played, corrupt WAV rejected, final WAV played, Off stopped at the end.
- Shuffle/Repeat All: multiple cycles played, with explicit previous/mode/skip actions exercised.
- Repeat One: the same single WAV restarted repeatedly.
- Two corrupt files with Repeat All: exactly two failures, then playback stopped instead of looping.
- Photo: fit at 120%, zoom through 160% to 200%, pan by 54 logical pixels on both axes, reset to the original
  fit rectangle; rotation, next photo and Back completed. Saved native images were inspected.

All fixtures/settings/logs are local under ignored out/native-reference/media-continuation. Diagnostic WAVs
are silent; these runs verify sequencing and decoder/error lifecycle, not speaker audibility. No new formal
test suite was added. Physical-controller operation and sustained playback of varied real media remain open.

MaXMB reference: local commit a746f77906d11390f6a3e12e4f2310a4c45fd399, audio.js queue methods and
photoviewer.js zoom/pan methods. Attribution remains in THIRD-PARTY.md. These adaptations do not establish
original PS3 panel geometry, timing or complete media-player parity. Playlists, recursive databases, metadata,
subtitle/scene-search controls and dynamic theme scenes remain open.

## Agent ownership

Small lower-cost agents produced only playback_queue.h/.cpp and photo_viewport.h/.cpp; a separate bounded
explorer identified source behavior and integration points. Parent integration corrected shuffle initialization,
added player/error lifecycle, controls, settings, builds and runtime observation. No agent edited the icon pipeline.
