# Sidecar subtitles and audio tracks in the video player

Status: implemented 2026-10-26 (task T3 of `audit/HTPC-IDEAS.md`). Written after the last source
edit. Every number below came from a run of the built binary
(`C:\dev\out\native-subs\re-ps3-native.exe`, 23:21:38, later than every edited source file), from
the log those runs wrote, or from the JSON sidecar each capture writes.

## 1. What this is, and what it follows in the firmware

Three things, all reachable with a pad:

1. **Discovery and parsing** of the subtitle files that sit next to the video
   (`<name>.srt`, `<name>.<tag>.srt`, `<name>.vtt`, `<name>.<tag>.vtt`), in `src/subtitles.cpp`.
2. **Rendering** of the active cue through the XMB's own text pipeline (the GL glyph atlas in
   `src/text_renderer.*`), over the video and clear of the player's controls.
3. **A player panel on Triangle** listing `Subtitles Off`, one row per discovered file, the audio
   tracks the player itself reports, and Repeat/Shuffle - which moves Repeat off the Triangle
   button, as `audit/HTPC-IDEAS.md` T3 asks.

The firmware grounds the panel's *contents*, not its shape. `videoplayer_plugin.rco`'s own video
control panel is a button row whose group
(`videoplayer_cp_button_group`, 25 children in the object dump at
`out/native-reference/rco-behavior/resources.json`) contains

- `videoplayer_cp_audio_change_button` (posX -3),
- `videoplayer_cp_subtitle_button` (posX -2),
- `videoplayer_cp_repeat_button` (posX 0, posY -1),

so subtitle choice, audio-track choice and repeat are exactly the three things the console's
in-player panel offers. What is **not** traced is that button row's projection, so the native panel
borrows the visual language already proven for the item-level Triangle menu (a right-hand strip,
23 px rows on a 40 px pitch, the focused row glowing, 200 ms mode-5 slide-in;
`audit/OPTIONS-MENU.md`) instead of inventing a second overlay. That is a fitted choice, recorded
as such. The subtitle text style is fitted too: the local references do not show a PS3 playing a
sidecar subtitle, so "centred lower third, white on a dark outline" is a console convention, not a
measurement.

## 2. Discovery rules

`xmb::subtitles::discover(mediaPath)` looks at the media's own directory only - no recursion, no
parent, nothing written.

| case | name accepted | shown as |
|---|---|---|
| untagged | `<base>.srt`, `<base>.vtt` (extension case-insensitive) | `Subtitles: On (SRT)` / `On (VTT)`; this is the default track and sorts first |
| tagged | `<base>.<tag>.srt` / `.vtt`, tag = 1-24 chars of letters, digits, `.`, `_`, `-` | a known ISO-639 tag becomes its English name (`en`, `eng` -> `English`), the rest of the tag is kept in brackets (`en.forced` -> `English (forced)`), an unknown tag is shown verbatim (`part2`) |

Bounds and refusals, so a wrong or hostile file cannot cost anything:

- at most **16** candidate files per media file;
- at most **4 MB** per file, checked with `QFile::size()` *before* reading;
- a candidate whose tag part is not tag-shaped is ignored (e.g. `<base>.my subtitle.srt`);
- `.ass`, `.ssa`, `.sub`, `.idx`, `.smi`, `.sami`, `.ttml`, `.dfxp`, `.sbv`, `.lrc`, `.mpl` are
  named and ignored, not silently skipped;
- a candidate that cannot be opened, is over the cap, or parses to **zero** usable cues is ignored
  with its reason.

Every ignored candidate produces one line, always (not only under the trace flag), because "I put
my .ass next to the film and nothing happened" is the report this answers:

```
subtitle: ignored Cue Test.ass: ASS subtitles are not supported (SRT and WebVTT only)
subtitle: ignored Cue Test.de.srt: file is 5242880 bytes, above the 4194304-byte limit
subtitle: ignored Cue Test.it.srt: no usable cue in 46 block(s)     (a 3.4 MB MP4 renamed to .srt)
```

## 3. Parsing rules

`parseFile` reads the bounded bytes, decodes them, splits on `\n`, groups lines into blocks
separated by blank lines, and treats the first line containing `-->` in a block as its timing line.

| concern | rule |
|---|---|
| encoding | UTF-8; UTF-8 with BOM; UTF-16LE and UTF-16BE with BOM; anything else decodes as UTF-8 with replacement characters rather than failing |
| line endings | CRLF and lone CR both become LF |
| timestamps | `h:mm:ss,fff`, `hh:mm:ss.fff`, `mm:ss.mmm`, `ss`; `,` or `.` before the fraction; 1-3 fraction digits; hours optional |
| junk around the timing line | text before the arrow is cut at its last space (an SRT index on the same line), text after the arrow is cut at its first space (WebVTT `align:start position:10%`, SRT `X1:..`) |
| WebVTT headers | `WEBVTT`, `NOTE`, `STYLE`, `REGION`, `X-TIMESTAMP-MAP` blocks are skipped and not counted as cues; a cue identifier is just the line before the timing line |
| inline markup | `<i>`, `<b>`, `<font ...>`, `<v Bob>`, `<c.class>` tags are stripped; ASS override blocks (`{\an8}`) are stripped; `&amp; &lt; &gt; &quot; &nbsp; &#39;` are decoded |
| dropped, counted, non-fatal | a block with no timing line; an unparseable timestamp; `end <= start`; empty text after cleaning; anything past the cue cap |
| bounds | 20,000 cues, 1,200 characters per cue, 6 lines per cue |
| ordering | cues are stable-sorted by start time |
| cue lookup | binary search on start, then a short backwards walk for overlaps; the interval is half-open `[start, end)` |

The parsed counts are reported under `XMB_SUBTITLE_LOG=1`, one line per usable file, e.g.

```
subtitle: sidecar Cue Test.srt srt 3 cues 249 bytes; skipped 0 ; name On (SRT)
subtitle: sidecar Cue Test.en.srt srt 3 cues 401 bytes; skipped 2 ; name English
subtitle: sidecar Cue Test.fr.vtt vtt 2 cues 224 bytes; skipped 0 ; name French
```

`Cue Test.en.srt` is the tolerance fixture: UTF-8 BOM, CRLF, five blocks of which one has
`end == start` and one has no timing line at all - 3 cues kept, 2 dropped, exit still clean.
`Cue Test.fr.vtt` has a `WEBVTT` header, a `NOTE` block, a cue identifier and `align:start
position:10%` settings, and keeps both cues.

## 4. Rendering

`View::drawSubtitle` draws the cue returned by `Shell::subtitleCueText()`, which is the cue covering
`QMediaPlayer::position()` at the instant the frame is composed. The lookup runs on every frame the
view draws rather than on `positionChanged`, because that signal's cadence belongs to the backend;
the *log* line is emitted only when the cue number actually changes.

- **Band**: `QRectF(210, 650, 1500, 200)`, flags `AlignHCenter|AlignBottom|TextWordWrap`, 32 px, so
  the cue wraps upward from a fixed bottom edge and up to four lines fit.
- **Style**: white `(248,248,248)` on four 2 px diagonal offsets of black alpha 215, then the fill
  with the renderer's own one-pixel shadow. A console subtitle reads on a bright frame; a plain
  white string over this build's colour-bar test clip does not.
- **Pipeline**: `TextRenderer::draw` - the same glyph atlas and FreeType coverage the menus use,
  with the QPainter fallback (`p.drawText`) kept for the case where the atlas is not ready. Nothing
  about the cue is drawn with QPainter-only text on a normal run.
- **Why the band stops at y 850**: the media overlay draws the queue summary at y 895, the progress
  bar at y 941, the title at y 970 and the key hints at y 1020. A cue therefore cannot cover them,
  and that is measured (section 8.3), not asserted.
- **Independent of the controls' fade**: the cue is drawn outside the `mediaControls_.state(now)`
  opacity scope, so it stays at full strength after the controls have faded. `cue-second.png` is
  the proof: at 6.958 s the controls are gone and the cue is fully drawn.
- **Suppression**: a dialog (`message_` non-empty) suspends the cue, because the dialog's own black
  plate would otherwise have text over it; the PS menu paints the whole frame black later in
  `drawScene`, so the cue is covered without a special case, and it returns when the menu closes
  (captured). The volume HUD already hides while the player panel is open, through the panel rule
  in `drawScene`.
- **Track changes**: choosing a different file re-renders the next frame's cue; the cue number
  restarts at 0 for the new track, and the log shows the transition (section 8.3).

**Seeks.** Discovery runs **once per file that starts playing**, not once per `LoadedMedia`: the
ffmpeg backend reports `LoadedMedia` again on every `setPosition`
(`QFFmpegMediaPlayer::setPosition` calls `mediaStatusChanged(QMediaPlayer::LoadedMedia)`), so the
handler compares the source path with the file the current tracks belong to and skips the re-scan on
a seek. Without that guard a Left/Right re-parsed every sidecar and forgot a refused audio track.
Run C proves the guard and the cue lookup together:

```
subtitle: sidecar Cue Test.srt srt 3 cues 249 bytes; skipped 0 ; name On (SRT)
...                       (one discovery block for the whole playback, two seeks included)
subtitle: cue 2 at 5525 ms (5500-10000 ms) SECOND CUE: the same file, a later timestamp.
subtitle: cue cleared at 0 ms
subtitle: cue 1 at 533 ms (500-5000 ms) FIRST CUE: the player found this sidecar file.
```

| capture | `mediaPosition` | `subtitleCue` |
|---|---:|---:|
| `seek-before.png` | 6041 ms | 2 |
| `seek-after.png` (one Left) | 981 ms | 1 |
| `seek-zero.png` (another Left) | 960 ms | 1 |
| `seek-playing.png` | 4373 ms | 1 |

So the cue follows the position across a seek, which is what "correct timing against
`mediaPosition()`" means here.

## 5. The panel

Triangle while a video plays (and only then: photos keep Triangle as Rotate, music keeps it as
Repeat) opens the XMB's right-hand panel on `Shell::buildPlayerOptions()`:

| row | command | what Cross does |
|---|---|---|
| `Subtitles Off` | `SubtitlesOff` | the choice becomes Off; marked when it is current |
| `Subtitles: <label>` (one per discovered file) | `SubtitleTrack` | that sidecar becomes the cue source; the marker moves to it |
| `Subtitles: none found` (only when there is no sidecar) | `SubtitlesOff` | **disabled**, with the reason under the list |
| `Audio <n>: <label>` (one per track the player reports) | `AudioTrack` | asks the player to switch, then verifies its own read-back |
| `Audio: not reported` (only when the player reports none) | `AudioTrack` | **disabled**, with the reason |
| `Repeat: Off/One/All` | `RepeatMode` | the same function the R key calls |
| `Shuffle: Off/On` | `ToggleShuffle` | the same function the M key and Square call |

Behaviour:

- The panel opens focused on the **current choice**, not the first row, so a stray Cross cannot turn
  subtitles off.
- The current choice carries a small filled marker; the focused row glows (a disabled row gets a
  weaker halo, so it is still visibly the selected row while its title stays dim).
- The focused row's note is drawn under the list: the subtitle file name and its parsed cue count
  (`Cue Test.fr.vtt - 2 cues, all blocks read`), or the reason a row cannot be used.
- **Disabled rows are listed, not hidden**, so the reason is reachable on the couch; Cross on one
  plays the error cue and logs `shell: option unavailable <title> <reason>` without changing
  anything. Run D proves the frame does not change (bit-identical, section 8.4).
- Up/Down move, Cross chooses, Circle closes, Triangle closes (the same `toggleOptions` path). The
  panel owns input: `keyPressEvent` and `pollController` handle it before anything else, exactly as
  for the item-level menu, so Left/Right cannot seek and Up/Down cannot scroll the XMB while it is
  up.
- Choosing a track keeps the panel open so the marker moves on the spot; Circle or Triangle closes
  it.
- **It does not disturb the video controls' fade logic.** The media overlay still calls
  `mediaControls_.setPaused(...)` and reads `mediaControls_.state(now).opacity` exactly as before;
  the panel only draws over the finished overlay. A key press while the panel is up still counts as
  activity, i.e. it refreshes the controls' 3 s window like any other input - the one thing it does
  not do is seek or pause. Measured: a Left press with the panel open changed nothing at all (frame
  bit-identical, section 8.3).
- Over video the strip alone is not enough contrast, so the player panel lays its strip on a dark
  plate (alpha 158 -> 120 across the width). The item-level panel keeps the plain strip, so the
  panel shown in `audit/OPTIONS-MENU.md` is unchanged.

## 6. Audio tracks: what the backend can and cannot do

The panel lists **only** what `QMediaPlayer::audioTracks()` returns, and marks
`QMediaPlayer::activeAudioTrack()`.

Observed on this machine and this build (Qt 6.12.0, `ffmpegmediaplugin.dll`):

```
audio: player reports 1 track(s); active 1 AnyLanguage (AAC)
```

That is the whole truth for both test sources on this machine: the ffmpeg backend reports one audio
track for the generated 20 s clip, with a language value of `AnyLanguage` and codec `AAC`. There is
no second track to switch to, and the panel says so on the row's note ("The player reports one
audio track, so there is no second track to switch to.").

What the backend does with a request, from the Qt 6.12 sources of the version this build links
(`code.qt.io/qt/qtmultimedia.git`, fetched 2026-10-26):

- `QFFmpegMediaPlayer::setActiveTrack(TrackType, int)` calls
  `PlaybackEngine::setActiveTrack(...)`, which calls `MediaDataHolder::setActiveTrack(...)`,
  discards the codec context, the renderer, the stream decoder and the demuxer, and recreates them -
  so a switch is implemented, and playback continues from the same position;
- `QFFmpegMediaPlayer::activeTrack(TrackType)` returns `PlaybackEngine::activeTrack(type)`, which
  returns `m_media.activeTrack(type)` - the stored selection, read back from the same object the
  setter wrote.

So the read-back this build performs is a real check of **whether the player accepted the
selection**. It is not proof that a different stream became audible: that would need a multi-track
fixture and an ears-on check. This is the honesty the task asks for, stated plainly:

- **Proven here**: the backend reports the track list and the active index; the panel marks it; a
  request is verified against `activeAudioTrack()` and, if the read-back does not follow within
  400 ms, the row is disabled with `This media backend did not switch: it still reports track N.`
  logged as a warning too. That reject state is remembered per file for the session.
- **Not proven here**: switching between two or more audio tracks. There is no multi-track file on
  this machine and no ffmpeg binary to make one (searched; only `ffmpeg.dll` shared libraries
  exist), so the switch path itself was not exercised. The disable/refusal machinery *is* exercised
  end to end, by the other disabled row (section 8.4), which uses the same rendering and the same
  refusal branch in `optionsActivate`.
- The Windows Media Foundation plugin is also deployed (`windowsmediaplugin.dll`), and Qt would
  choose it in builds/settings that ask for it; nothing here claims what it supports.

## 7. State and logging

State lives in `Shell` and is per process:

| field | meaning |
|---|---|
| `subtitleTracks_` | the usable sidecars discovered for the file now playing |
| `subtitleTrackIndex_` | the chosen index, `-1` = Off |
| `subtitleCueNumber_` | 1-based cue at the player position, `0` = none |
| `subtitleChoice_` | media path -> sidecar path (`""` = Off), **this session only**; the same file comes back on the same choice, a file never seen starts on its default sidecar |
| `audioTracks_` / `activeAudioTrack_` | what the player reports and its own active index |
| `audioRejected_` | track index -> the read-back failure, remembered while that file plays |

Nothing is written to disk, and nothing is written next to the user's media.

`XMB_SUBTITLE_LOG=1` turns on the trace. With it unset, the only subtitle lines are the
ignored-file warnings, so a normal log stays quiet.

```
subtitle: ignored <file>: <reason>
subtitle: sidecar <file> <srt|vtt> <n> cues <bytes> bytes; skipped <n> ; name <label>
subtitle: no sidecar beside <file>
subtitle: auto-selected <file> (first time this file plays; Subtitles Off is remembered if chosen)
subtitle: session choice for this file is <file|Off>
subtitle: track <file> (<label>)
subtitle: off
subtitle: cue <n> at <ms> ms (<start>-<end> ms) <first 80 chars>
subtitle: cue cleared at <ms> ms
audio: player reports <n> track(s); active <n|not reported> <labels>
audio: switched to track <n> <label>
audio: cannot switch to track <n> - the player still reports <n>
```

The capture sidecar (`<grab>.png.json`) gained the fields T3 asked for plus the ones a reader needs
to check them without pixels:

| field | value |
|---|---|
| `subtitleFile` | absolute path of the chosen sidecar, `""` when Off |
| `subtitleLabel` | its panel label (`On (SRT)`, `English`, `French`) |
| `subtitleTrackCount` | usable sidecars discovered for the file |
| `subtitleCue` | 1-based cue number at the capture's position, `0` = none |
| `subtitleCueText` | the cue's text, first 160 characters |
| `audioTrack` | 1-based active index, `0` = the backend names none |
| `audioTrackCount` | tracks the player reports |
| `audioTrackName` | the active track's label |

## 8. Verification

### 8.1 Build

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-subs
```

Passed; `re-ps3-native.exe` 2026-10-26 23:21:38, later than every edited source file. No new
compiler warning from `subtitles.cpp`, `shell.cpp` or the new `view.cpp` code (the three warnings in
the build are pre-existing, at `view.cpp:338/466/601`).

### 8.2 Fixture and scripts

Everything under `C:\dev\out\native-subs\fixture-media`, never committed:

| file | what it is |
|---|---|
| `Cue Test.mp4` | a copy of `out/native-reference/video-library/Top Level Clip.mp4` (20 s, one AAC track) |
| `Cue Test.srt` | the untagged sidecar: cues at 0.5-5.0, 5.5-10.0, 10.5-19.9 s with three different sentences |
| `Cue Test.en.srt` | tagged `en`: BOM + CRLF, one `end == start` block, one block with no timing line (2 of 5 dropped) |
| `Cue Test.fr.vtt` | tagged `fr`: WebVTT header, NOTE, cue identifier, cue settings |
| `Cue Test.ass` | unsupported format, ignored with a reason |
| `Cue Test.de.srt` | 5 MB of filler, over the size cap |
| `Cue Test.it.srt` | the MP4 renamed to `.srt` (3.4 MB of binary), parses to zero cues |
| `No Subtitle.mp4` | the same clip with no sidecar |

Data directory: a copy of `out/native-reference/store/video-data` with `library/video` pointed at
the fixture folder. All captures use `--dev-flash C:/dev/rpcs3/bin/dev_flash`, the pinned clocks
`--render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-26T21:50:00
--focus-time 2000`, `XMB_LOG` and `XMB_SUBTITLE_LOG=1`.

Navigation to the page in every script is `9 LEFT, 4 RIGHT, ENTER` from the Game root (the category
order dumped with `XMB_DUMP_ENTRIES` is user, settings, photo, music, video, tv, game, network, psn,
friends, so 9 LEFT clamps to 0 and 4 RIGHT lands on Video; `ENTER` opens `Video Library`). **No new
script token was needed**: `TRIANGLE`, `UP`, `DOWN`, `ENTER` and `ESC` drive the panel through the
real key path.

Run A1 (cue timing, panel, WebVTT track, PS menu):

```
full,wait,wait,LEFT x9,wait,wait,RIGHT x4,wait,wait,ENTER,wait,wait,wait,wait,
ENTER,wait x6,grab=cue-first.png,wait x15,grab=cue-second.png,
TRIANGLE,wait,wait,grab=panel.png,DOWN,DOWN,wait,wait,grab=panel-french.png,
ENTER,wait,wait,wait x10,grab=panel-french-cue.png,
pshold,wait,wait,wait,wait,grab=psmenu-cue.png,ESC,wait,wait,grab=after-psmenu-cue.png,quit
```

Run A2 (Off, session memory, no-sidecar file, disabled row):

```
full,wait,wait,LEFT x9,wait,wait,RIGHT x4,wait,wait,ENTER,wait,wait,wait,wait,
ENTER,wait x5,TRIANGLE,wait,grab=panel-opening.png,UP,wait,grab=panel-off-focus.png,
ENTER,wait,grab=panel-off.png,ESC,wait,wait,grab=no-cue.png,
ESC,wait,wait,ENTER,wait x4,grab=remembered-off.png,
ESC,wait,wait,DOWN,wait,ENTER,wait x5,grab=no-sidecar.png,
TRIANGLE,wait,wait,grab=panel-none.png,DOWN,wait,wait,grab=panel-none-focus.png,
ENTER,wait,grab=panel-none-refused.png,TRIANGLE,wait,grab=panel-triangle-closed.png,quit
```

Run B (a frozen frame with the cue, for pixel comparison):

```
full,wait,wait,LEFT x9,wait,wait,RIGHT x4,wait,wait,ENTER,wait,wait,wait,wait,
ENTER,wait x6,ENTER,wait,wait,grab=paused-cue.png,
TRIANGLE,wait,wait,grab=paused-panel.png,LEFT,wait,wait,grab=paused-panel-left.png,
UP,wait,ENTER,wait,wait,grab=paused-off-panel.png,ESC,wait,wait,grab=paused-off.png,quit
```

Run C (two seeks, to show discovery runs once per file and the cue follows the position):

```
full,wait,wait,LEFT x9,wait,wait,RIGHT x4,wait,wait,ENTER,wait,wait,wait,wait,
ENTER,wait x6,wait x14,grab=seek-before.png,
LEFT,wait,wait,grab=seek-after.png,LEFT,wait,wait,grab=seek-zero.png,
wait x10,grab=seek-playing.png,quit
```

Run D (a Cross on a disabled row, on a frozen frame):

```
full,wait,wait,LEFT x9,wait,wait,RIGHT x4,wait,wait,ENTER,wait,wait,wait,wait,
DOWN,wait,ENTER,wait x4,ENTER,wait,wait,
TRIANGLE,wait,wait,DOWN,wait,wait,grab=disabled-focus.png,
ENTER,wait,grab=disabled-refused.png,quit
```

### 8.3 Read-back, and pixel evidence

State at each capture (sidecar read back, not inferred):

| capture | `mediaPosition` | `subtitleCue` | `subtitleFile` | inside a cue? |
|---|---:|---:|---|---|
| `cue-first.png` | 1920 ms | 1 | `Cue Test.srt` | yes, cue 1 is 500-5000 |
| `cue-second.png` | 6933 ms | 2 | `Cue Test.srt` | yes, cue 2 is 5500-10000 |
| `panel.png` | 8192 ms | 2 | `Cue Test.srt` | yes; panel open, focus on the current choice |
| `panel-french.png` | 9792 ms | 2 | `Cue Test.srt` | yes, before the switch |
| `panel-french-cue.png` | 14144 ms | 2 | `Cue Test.fr.vtt` | yes, the WebVTT cue is 11000-19500 |
| `psmenu-cue.png` | 16021 ms | 2 | `Cue Test.fr.vtt` | yes, but the frame is the black PS menu |
| `after-psmenu-cue.png` | 17280 ms | 2 | `Cue Test.fr.vtt` | yes, cue back |
| `panel-opening.png` | 2282 ms | 1 | `Cue Test.srt` | yes; the panel opened on the current choice (run A2) |
| `panel-off-focus.png` | 3178 ms | 1 | `Cue Test.srt` | yes, before choosing Off |
| `panel-off.png` | 4138 ms | 0 | `""` | **yes, cue 1 spans 500-5000**: Off proven with a cue due |
| `no-cue.png` | 5376 ms | 0 | `""` | no: falls in the 500 ms gap between cues 1 and 2, shown for completeness |
| `remembered-off.png` | 7104 ms | 0 | `""` | **yes, cue 2 spans 5500-10000**: the session memory proven with a cue due |
| `no-sidecar.png` | 1834 ms | 0 | `""` | the file has no sidecar (`subtitleTrackCount` 0) |
| `panel-none-focus.png` | 4333 ms | 0 | `""` | same file, panel open on the disabled row |

Log, in order (A1 then A2, abridged):

```
subtitle: ignored Cue Test.ass: ASS subtitles are not supported (SRT and WebVTT only)
subtitle: ignored Cue Test.de.srt: file is 5242880 bytes, above the 4194304-byte limit
subtitle: ignored Cue Test.it.srt: no usable cue in 46 block(s)
subtitle: sidecar Cue Test.srt srt 3 cues 249 bytes; skipped 0 ; name On (SRT)
subtitle: sidecar Cue Test.en.srt srt 3 cues 401 bytes; skipped 2 ; name English
subtitle: sidecar Cue Test.fr.vtt vtt 2 cues 224 bytes; skipped 0 ; name French
subtitle: auto-selected Cue Test.srt (first time this file plays; ...)
audio: player reports 1 track(s); active 1 AnyLanguage (AAC)
subtitle: cue 1 at 533 ms (500-5000 ms) FIRST CUE: the player found this sidecar file.
subtitle: cue cleared at 5034 ms
subtitle: cue 2 at 5525 ms (5500-10000 ms) SECOND CUE: the same file, a later timestamp.
shell: playback panel opened rows= 7 subtitles= 3 audio= 1 Subtitles Off|Subtitles: On (SRT)|Subtitles: English|Subtitles: French|Audio 1: AnyLanguage (AAC)|Repeat: Off|Shuffle: Off
subtitle: track Cue Test.fr.vtt (French)
subtitle: cue 2 at 11029 ms (11000-19500 ms) Deuxieme replique du fichier WebVTT.
```

Then, from run A2:

```
subtitle: session choice for this file is Off
subtitle: off
subtitle: no sidecar beside No Subtitle.mp4
shell: playback panel opened rows= 5 subtitles= 0 audio= 1 Subtitles Off|Subtitles: none found|Audio 1: AnyLanguage (AAC)|Repeat: Off|Shuffle: Off
shell: option unavailable Subtitles: none found No .srt or .vtt file beside this video. Embedded subtitle streams are not switched yet.
```

Pixel evidence, with the project's own comparator (`work/ab-compare.ts`), on the **frozen** frame
of Run B (paused at 1984 ms, so both frames show the same video picture):

```
paused-cue.png vs paused-off.png  210,650,1500,200   PSNR 18.21 dB   SSIM 0.9259   cue band differs
paused-cue.png vs paused-off.png  110,890,1700,140   PSNR Infinity   SSIM 1.0000   controls untouched
paused-cue.png vs paused-off.png  0,0,1920,600       PSNR Infinity   SSIM 1.0000   rest of the frame untouched
paused-panel.png vs paused-panel-left.png  0,0,1920,1080  PSNR Infinity  SSIM 1.0000  a Left with the panel open did nothing
```

That is the strongest form this harness can report: with subtitles Off, only the cue's own band
changes; the controls band, the upper frame, and the whole frame while the panel absorbs a Left
press are bit-identical.

### 8.4 The disabled row

Run D pauses `No Subtitle.mp4` at 1386 ms, opens the panel, moves onto the disabled
`Subtitles: none found` row and presses Cross. The log records the refusal with the full reason, and
the two frames are bit-identical:

```
shell: option unavailable Subtitles: none found No .srt or .vtt file beside this video. Embedded subtitle streams are not switched yet.
disabled-focus.png vs disabled-refused.png  0,0,1920,1080  PSNR Infinity  SSIM 1.0000
```

Both sidecars report the same state (position 1386 ms, paused, `subtitleCue` 0,
`subtitleTrackCount` 0), so a Cross on a row this build cannot honour changes nothing at all. This is
the same branch (`if(!option.enabled)`) and the same drawing (`enabled=false` -> dimmed title +
reason note + weak focus halo) the audio rejection uses, which is why the mechanism is claimed as
exercised even though the audio case had no multi-track fixture to trigger it.

### 8.5 PS menu

`psmenu-cue.png` is the console's black system menu - no cue, no panel - and `after-psmenu-cue.png`
shows the French cue and the open panel back on the same frame content. The cue is covered because
the PS menu paints the frame after the media branch, not because of a special case.

### 8.6 Gates

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-subs/re-ps3-native.exe
```

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---|---:|---:|---|
| frame | 18.29 | 18.02 | +0.27 | .8295 | .8301 | -0.0006 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +0.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -0.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +0.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +0.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +0.0000 | PASS |

`PASS: 6/6 layers within budget`. Named states, same executable:
`bun work\ab-layers.ts --states --exe ...` -> `states: 4/4 pass`, every region `inf / 1.0000`, so no
named state changed. Both gates were re-run on the final binary (23:21:38) after the last source
edit, with the same result. No harness file, reference or threshold was edited.

The item-level panel was also re-captured after the drawing generalisation
(`item-panel.png`, `item-panel-sort.png`): the folder page still offers
`Play|Information|Sort By|Close`, the Sort submenu still draws its own heading above
`Name (A-Z)|Name (Z-A)|Close`, and that panel keeps the plain strip (no dark plate, no marker).

## 9. What is not done, and why

1. **Embedded subtitle streams** (`QMediaPlayer::subtitleTracks`) are not listed or switched. The
   ffmpeg backend does support subtitle track selection, but the panel would then need to render
   backend-delivered bitmaps and the local fixture set has none; the disabled row says so rather
   than leaving the user guessing.
2. **Jellyfin subtitles** are not fetched. Streamed media has no user directory beside it, so
   discovery finds nothing; the panel's note says "Streamed media: sidecar subtitles are read for
   local video files only." The server-side VTT path is a follow-on.
3. **A real audio-track switch** is unproven (section 6). The code and the honest failure path are
   in place; the fixture is not.
4. **The subtitle style and the panel's shape are fitted**, not traced (section 1). There is no
   setting for size, position or a timing offset; a user who needs them has no row for them, and
   none was invented.
5. **No pre-roll/post-roll offset, no styling tags per cue** (`{\i1}` is stripped, not applied).
   Italic/bold from `<i>`/`<b>` is dropped with the rest of the markup.
6. **Caps**: 4 MB per file, 20,000 cues, 6 lines and 1,200 characters per cue, 16 candidate files.
   A file that exceeds them is refused or truncated with a counted reason, never silently.
7. **Font coverage** is the XMB's own; an unsupported script falls back through the same
   `TextRenderer` fallback faces the menus use, and was not exercised here.

## 10. Files touched

- `native-xmb/src/subtitles.cpp`, `native-xmb/src/subtitles.h` (new): discovery, SRT/WebVTT parsing,
  cue lookup, panel labels.
- `native-xmb/CMakeLists.txt`: the new source pair.
- `native-xmb/src/shell.h`: `OptionCommand` gains `SubtitlesOff`, `SubtitleTrack`, `AudioTrack`,
  `RepeatMode`, `ToggleShuffle`; `MenuOption` gains `index`, `enabled`, `active`, `note`;
  `AudioTrackInfo`; the public subtitle/audio API and the private state and helpers.
- `native-xmb/src/shell.cpp`: `prepareSubtitles`, `applySubtitleTrack`, `subtitleFile`,
  `subtitleLabel`, `subtitleCueText`, `subtitleCueNumber`, `chooseSubtitleTrack`,
  `refreshAudioTracks`, `chooseAudioTrack`, `buildPlayerOptions`, `refreshPlayerOptions`; the
  player-panel branch in `toggleOptions`; the new commands in `optionsActivate` (including the
  disabled-row refusal); `resetOptions` clears the player-panel flag; the `LoadedMedia` hook that
  discovers sidecars and refreshes the audio list; `back()` clears the playback-scoped state while
  keeping the per-file choice; `XMB_SUBTITLE_LOG`.
- `native-xmb/src/view.h`: `drawSubtitle`.
- `native-xmb/src/view.cpp`: `drawSubtitle`; the cue draw and the in-player panel draw in the media
  branch of `drawScene`; `drawOptionsPanel` generalised with the marker column, dimmed disabled
  rows, a focus-following heading and note, and the dark plate (item and settings call sites keep
  their old rendering); Triangle opens the player panel for video and keeps Repeat for music;
  the media hint line now reads `R Repeat   Triangle Options`.
- `native-xmb/src/main.cpp`: the `subtitleFile`/`subtitleLabel`/`subtitleTrackCount`/`subtitleCue`/
  `subtitleCueText`/`audioTrack`/`audioTrackCount`/`audioTrackName` fields in the grab sidecar.
- `native-xmb/audit/SUBTITLES.md`: this document.

Not touched: `settings_menu.cpp`, `preferences.cpp`, `xmbml_view.*`, `host.*`, `pad.*`,
`media_controls.*`, `jellyfin_*`, the verification harness, its thresholds and its references.
Captures, fixtures and logs live under `out/native-subs/` and are not committed.

**Note (2026-10-27):** Continue Watching is now the first row of the Video column, so the repro scripts that say 9 LEFT, 4 RIGHT, ENTER reach Video Library only with one extra DOWN before ENTER. Those runs were captured before the row existed; the evidence in this file is otherwise unchanged.
