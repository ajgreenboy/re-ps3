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
(`audioplayer_plugin.prx` was not disassembled here) and the Visual Player is **not drawn**: it needs a PCM tap from the
decoder, which this build does not have, and animating levels without one would be invented.

Opening the XMB control panel (B2) keeps the cover block and fades the bottom chrome out with the panel's own 150 ms ease.

## Files

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

Not verified: an MP3 with embedded cover art (the folder-picture path and the default art were exercised; the embedded-picture
branch is Qt's `CoverArtImage` and reads null for the WAV fixtures), real-time audibility, and any frame against the console.

## Open

- Positions, the idle timeout and the status-plane timing need console frames or a trace of `audioplayer_plugin.prx`.
- The 14-button player panel (`control_panel_page`), `del_dialog_page`, the visualizer and the volume page of this plugin are not
  built; B2 stands in for the panel.
