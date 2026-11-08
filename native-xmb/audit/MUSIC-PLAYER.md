# Music player full-screen page (S19, plan item F9) - 2026-11-08

Surface B1 of [MEDIA-PANELS-FIRMWARE.md](MEDIA-PANELS-FIRMWARE.md): `audioplayer_plugin.rco`'s own page, the one the console
shows while a track plays. The XMB control panel (B2, Triangle) was already drawn and is unchanged; this pass replaces the
generic bottom band the build used for music (queue line, title, ASCII hint sentence, flat bar) with the page below.

Tags used: **TRACED** (read from the container or code), **measured** (taken off a frame), **INFERRED** (a fit or a guess).

## Evidence

| Source | What it gave |
|---|---|
| `re-ps3-native --rco-tree "audioplayer_plugin@..."` (75 objects, 0 animations) | page set, object names, texture bindings, sizes. TRACED |
| `--rco-text` (60 strings) | `msg_track_number_value` "Track %1", `msg_tracks_count_value` "%1 Tracks", `msg_track_count_value` "%1 Track", `msg_help_control_panel_home_menu` (a gaiji + ": Control Panel" / gaiji + ": Home Menu"). TRACED |
| `--export-image` of `tex_track_frame`, `tex_default_music_cover`, `tex_track_number*`, `tex_codec_mp3`, `tex_pcm`, `*_status` | texture sizes: frame 240x240, cover 162x162, digits 46x68, codec labels 104x24 (mp3) / 110x28 (pcm), play/pause 74x74, repeat/shuffle 98x74, repeat-one 72x36. measured |
| Reference frames | **none.** `rpcs3-allgraphics`, `rpcs3-day`, `rpcs3-gaps`, `rpcs3-ps`, `rpcs3-ref*`, `native-reference` hold the XMB music column and its menus only; no frame has the player running (the rig cannot play a track under the capture harness, same limit as `REFERENCE-GAPS.md` 4.4). |

Consequence: the container fixes the **objects, sizes, strings and art**. Every object in it carries position (0,0,0) (the
plugin places them at run time; same finding as MEDIA-PANELS-FIRMWARE 4.1), so **every position below is INFERRED**.

## What the container says (TRACED)

- `full_screen_page` (empty), `back_screen_page` -> `back_screen_plane` (`tex_back_screen` 1x1, alphaScale 0).
- `title_information_page` -> `plane_jacket_music_cover`, `plane_track_number1`, `plane_track_number2`, `text_contents_title_name`,
  `text_artist_name` (both size 10). There is **no album text object**.
- `play_progress_page` -> `text_track_number`, `text_current_time`, `play_progress_group_play_bar` (eight `text_*_time` cells),
  `plane_codec` (default `tex_codec_mp3`), `audioplayer_display_time_bar` (Progress, height 20), `play_progress_group_str_kind`
  (`text_ap_num/co/sp/sl/hy`).
- `play_status_page` -> ten status planes (play, prev, next, pause, stop, searchb, searchf, repeat, 1repeat, shuffle).
- `control_panel_page` -> 14 buttons + `button_description_label` (the player's own panel; B2 is what this build draws for it).
- `del_dialog_page`, `err_dialog_page`, `quit_confirm_dialog_page`, `full_screen_message_page`, `audioplayer_volume_page`.
- The container has **no progress art** and **no visualizer art** (the only visualizer asset is the `tex_cp_vislizer*` button icon).

## What is drawn now

`View::drawMusicPage()` (`src/view.cpp`), called from the media branch of `paintGL` when `mediaAudioOnly()`:

| Element | Source | Placement |
|---|---|---|
| frame | `tex_track_frame` drawn at its own 240x240 (margin included) | x360 y360. INFERRED |
| cover | embedded picture (`QMediaMetaData::CoverArtImage` then `ThumbnailImage`), else `folder`/`cover`/`front`/`albumart` `.jpg/.jpeg/.png` beside the file, else `tex_default_music_cover`; 162x162, aspect-fit, centred in the frame | centred in the frame. INFERRED |
| track number | `tex_track_number0..9`, 46x68, last two digits of the queue position | x660 y360. INFERRED |
| title / artist | the tags (title falls back to the file's list title), 36 px / 27 px, elided at 1100 px | x660, y444 / y494. INFERRED |
| album | tag, 24 px | x660 y534. **Native addition** (no firmware object) |
| codec label | `tex_codec_mp3` (mp3), `tex_pcm` (wav/aif/aiff), `tex_aac` (aac/m4a), `tex_wma`; none for other suffixes | bottom-aligned with the frame. INFERRED mapping |
| status planes | pause plane while paused; play plane for 1.5 s after a start or track change, fading 400 ms; shuffle / repeat / repeat-one planes while the mode is on | right-aligned at x1810, y360. INFERRED (the container has no animations) |
| track line | `msg_track_number_value` left, `msg_tracks_count_value` right | y895, 21 px. INFERRED |
| time bar | `View::drawVideoTimeBar` (videoplayer's `tex_progress_bar`, 20 px, 16x16 time cells) | y941. The audio container ships no bar art, so the sibling player's is borrowed. INFERRED |
| button guide | `msg_help_control_panel_home_menu`, each line led by its gaiji from the image font (Triangle, PS) | y1026. string TRACED, placement INFERRED |

Idle style (INFERRED throughout): while playing, with the panel closed and no dialog, after **8 s** without input
(`Shell::idleMilliseconds()`) the bottom chrome and status planes fade out over 600 ms and a black plane
(`back_screen_plane`, alpha 0.55) fades in over the background; cover, number, title, artist, album and codec stay.
Any input restores it. Paused playback never idles. The firmware's own idle timer and its visualizer are **not traced**
(`audioplayer_plugin.prx` was not disassembled here); the visualizer is a simplified stand-in, see "Visualizer" below.

Opening the XMB control panel (B2) keeps the cover block and fades the bottom chrome out with the panel's own 150 ms ease.

## Visualizer (S19 follow-up, 2026-11-08)

### What the firmware has (evidence)

| Source | Finding | Tag |
|---|---|---|
| `audioplayer_plugin.rco` | one string, `msg_visualizer_with_icon` = "Visual Player" (control-panel button 14 = `msg_visualizer_with_icon_button`, art `tex_cp_vislizer*`, 70x70 measured); no visualizer texture, plane, animation or timing in any page of the container. The same string is in `_dummy`, `_mini`, `_util` and `sacd_plugin` | TRACED |
| `audioplayer_plugin.prx` (decrypted, `fw493/vsh_module`) | string table (351 strings) holds `AudioPlayerPlugin::OnVisualizerButtonPush` and the module name `soundvisualizer_plugin`; the player hands off to that plugin when the button is pushed. Nothing about an idle timer or a visualizer style list | TRACED (names only; handler not disassembled) |
| `soundvisualizer_plugin.rco` / `.prx` (31 KB) | 14 objects: `page_soundvisualizer0/1/2`, a seat plane, two blank buttons, a list of four items all titled "SoundvisualizerPage1", strings "SoundvisualizerPage1/2", "LeftBtn Focus", "back/ok/yes/no". Textures: `btn_blank*`, `btn_check*`, `btn_normal`. The `.prx` strings are `OnPageInit`, `OnPushEnter`, `HelpPageExit`, `custom_render_plugin`, `system_plugin`, `vshcommon`: a developer sample page that links the custom renderer, no spectrum code, shaders or parameters. `sv_pseudoaudioplayer_plugin.rco` is its twin | TRACED |
| Music Settings (`sysconf_plugin`) | `msg_music_setting` rows are crossfade, volume normalisation, character set, audio-CD import: **no visualizer choice**, no visualizer style or idle-timeout setting. Screensaver (`msg_screensaver`) is the console-wide one | TRACED |
| `vsh/resource` | no visualizer shader/texture files (`qgl/*.qrc` are the XMB's own canyon/earth/lines art; `hknw_plugin/jacket/*.fpo` belong to the Hakkonen jacket plane) | TRACED (negative) |
| Reference frames | none of the Visual Player | none |

Conclusion: the 4.93 Visual Player is a separate engine reached from the control panel's 14th button; this firmware tree
carries no description of what it draws (the plugin shipped here is a stub), and no frames exist. So the firmware's look
and its idle trigger cannot be reproduced from evidence. What the build draws is a **simplified visualizer driven by real
audio levels**; its look is **INFERRED**, its audio path is real.

### What is drawn (INFERRED look, measured audio)

**Off by default**: the page draws exactly as before (no bars, no glow, no tap). The firmware reaches the visualizer through
the control panel's Visual Player button (`AudioPlayerPlugin::OnVisualizerButtonPush`), so the build does too:

- **Button**: the XMB music control panel (Triangle, B2) gained an 8th cell after Volume, drawn with audioplayer_plugin's own
  `tex_cp_vislizer` (70x70, centred in the 90x60 cell under `tex_cp_focus`). Focused, it is named under the plate with the
  container's `msg_visualizer_with_icon` ("Visual Player"; the string ends in a button gaiji that only the image font can
  draw, so the label keeps the words). The art is TRACED; the 8th-cell placement is INFERRED (the player's own 14-button
  `control_panel_page` is not built, B2 stands in for it).
- **Toggle**: Enter on the button flips `View::musicVisualizerOn_` (log line `media: Visual Player on|off`). The flag lasts
  for the playback session: it resets to off when playback stops (`mediaAudioOnly()` goes false), not on a track change. INFERRED
  (the firmware's hand-off to `soundvisualizer_plugin` and its return are not traced).
- **Tap**: `MusicTap::request()` is called only while the flag is on and playback is running, so with the visualizer off no
  buffer is queued and no FFT runs (the `QAudioBufferOutput` stays attached but its slot returns at once). Turning it on clears
  the tap's history.

`View::drawMusicVisualizer` (called from `drawMusicPage` after the idle dim plane, before the cover frame), while it is on
and playing:

- **40 spectrum bars**, log-spaced 50 Hz .. 16 kHz, x 110 .. 1810, baseline y850, up to 240 px high (x1.45 in the idle
  style), 72 % of each 42.5 px pitch wide. White at the baseline fading to pale blue, alpha 0.34 normally and 0.84 in the
  idle style; a 3 px peak cap per bar that falls at 0.45 / s. Bars attack in ~40 ms and fall over ~180 ms.
- **Cover glow**: a radial white-blue gradient centred on the 240x240 frame whose radius (200 .. 310 px) and alpha follow
  the track loudness (RMS of the newest 512 frames).
- Fades in/out over ~250 ms with play / pause; paused or stopped playback shows nothing.
- Nothing is drawn over the text: the bars end below the codec label even at full height in the idle style.

With the visualizer on, the idle style (chrome fades, black plane dims) makes it the prominent element; whether the firmware shows
a visualizer after its own idle timeout is not known (not traced), so nothing starts it automatically.

### Audio path (measured on this build)

- `src/musicvis.{h,cpp}`: `MusicTap` attaches a Qt 6.8 `QAudioBufferOutput` to the music `QMediaPlayer` (`Shell::musicTap()`).
  Its `audioBufferReceived` slot runs on the decoder thread and only counts and forwards the (shared) buffer; a worker
  thread (low priority) mixes to mono (UInt8 / Int16 / Int32 / Float inputs), keeps a 2048-sample Hann window with a hop
  of 512, runs a radix-2 FFT and converts band peaks to 0..1 over a -62 .. -8 dB range with a +2 dB/octave tilt.
- Bounded memory: at most 6 buffers queued to the worker (more are dropped and counted), a history of 96 windows (~1.1 s) under a mutex,
  nothing allocated per frame on the GUI thread. Analysis runs only while the page asked for levels in the last 500 ms,
  so video and the XMB cost nothing. The GUI picks the window nearest the player position (newest if none is within 400 ms);
  a backwards jump in media time clears the history.
- Timing check (`XMB_MUSICVIS_TRACE=1`, 44.1 kHz stereo WAV, ~90 fps window): draw cost 0.19-0.20 ms avg, 0.58 ms worst;
  ~20 analysed buffers per second, 0 dropped. Audio / picture alignment is by media timestamp; the output device latency
  (not measured) shifts it by a few tens of ms at most and is not compensated.

## Files

- `src/musicvis.h`, `src/musicvis.cpp` (new, in `CMakeLists.txt`): the PCM tap and spectrum analysis.
- `src/shell.h`, `src/shell.cpp`: `musicTap()` accessor and the tap's construction beside the player (`player_` outlives it).
- `src/view.h`, `src/view.cpp`: `drawMusicVisualizer` and its smoothing state, called from `drawMusicPage`.

- `src/view.cpp`, `src/view.h`: `drawMusicPage`, the idle ease, the band routing; `drawVideoTimeBar` / `drawTimeCells` gained an
  optional `opacity` (default 1, video draws unchanged).
- `src/shell.cpp`, `src/shell.h`: `mediaTrackTitle/Artist/Album/CoverArt/Codec/TrackNumber/TrackCount/Shuffle` accessors and
  `readMediaTags()`, called on `LoadedMedia` and on `metaDataChanged`; tags are cleared on every new queue item. Jellyfin items
  keep the old behaviour (no cover read).

## Verification

Scratch library `C:\dev\out\f9\libA` (three 40 s WAVs with RIFF INFO title/artist/album, plus a generated `folder.png`) and `libB`
(the untagged playlists WAVs, no cover), built by `C:\dev\out\f9\mkdata.ts`. Frames in `C:\dev\out\f9`:

| Frame | Shows |
|---|---|
| `n-start.png`, `m-start.png` | tagged track at 0:00: frame + folder cover, "1", title/artist/album, PCM label, play plane, Track 1 / 3 Tracks, bar, guide |
| `m-mid.png` | 0:04, play plane faded, bar progressing |
| `n-mid.png` | shuffle (`SHUF`) and repeat-one planes on |
| `b-pause.png` | untagged file: title falls back to "Alpha.wav", `tex_default_music_cover`, pause plane |
| `n-panel.png`, `b-panel.png` | Triangle: B2 panel over the page, bottom chrome gone, block intact |
| `n-idle.png`, `m-idle.png` | after the 8 s timeout (script `idle=12000`): chrome gone, page darkened |

Visualizer frames (`C:/dev/out/vis`, data from `mk.ts`: a 40 s 44.1 kHz stereo WAV with kick, hats, chord and a sweeping lead), scripted:

| Frame | Shows |
|---|---|
| `q1-default.png` | default page, playing: no bars, no glow (identical layout to before) |
| `q2-panel.png` | Triangle, focus on the new 8th button: `tex_cp_vislizer` under the focus plate, "Visual Player" under the panel |
| `q3-on.png` | after Enter and closing the panel: bars and cover glow moving with the audio |
| `q4-off.png` | after toggling off again: page back to the default, no bars |

Log of that run: `media: Visual Player on`, `musicvis:` lines at 2 s (draw avg 0.15-0.16 ms, worst 1.0 ms, ~21 buffers/s analysed, 0 dropped), then `media: Visual Player off` and no more `musicvis:` lines (the tap is idle).
Earlier always-on frames (`p1`/`p2`, `p-idle1`/`p-idle2`) showed the idle style (bars brighter and taller) and are superseded by the default-off behaviour.

Not verified: an MP3 with embedded cover art (the folder-picture path and the default art were exercised; the embedded-picture
branch is Qt's `CoverArtImage` and reads null for the WAV fixtures), real-time audibility, and any frame against the console.

## Open

- Positions, the idle timeout and the status-plane timing need console frames or a trace of `audioplayer_plugin.prx`.
- The 14-button player panel (`control_panel_page`), `del_dialog_page` and the volume page of this plugin are not
  built, B2 stands in for the panel (with the Visual Player button added as an 8th cell). The firmware's own visualizer look, its idle trigger and its exit path are untraced.
