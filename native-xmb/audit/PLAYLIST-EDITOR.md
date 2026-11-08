# Playlist two-pane editor (F3)

Date: 2026-11-08. Code: `View::drawPlaylistEditor` / `playlistEditorInput` / `openPlaylistEditor` (`src/view.cpp`), data
and saves in `Shell::playlistInfo / playlistLibraryFiles / playlistEdit{Add,Move,Remove}` (`src/shell.cpp`), storage
`PlaylistStore::moveItem / addAll / removeItem` (`src/playlists.*`, unchanged).

## Evidence

| Claim | Source | Status |
|---|---|---|
| The editor is two lists named `edit_src_lst` (available content) and `edit_dst_lst` (the playlist) on pages `expage_select_list` and `expage_edit_list` | strings of the decrypted 4.93 `playlist_plugin.prx` (`fw493/vsh_module`), plus `--rco-tree playlist_plugin` (pages with `onInit/onCancel/onActivate` handlers `PL_Edit::OnOpen/OnActivate/OnCancel`, `PL_Dst::OnSelect*`) | measured (strings and tree) |
| The RCO holds no layout for the lists: `expage_edit_list` has no children; `expage_select_list` has one Text, `msg_select_playlist` (that is the Add to Playlist target chooser, already in use) | `--rco-tree "playlist_plugin@..."`, 11 objects in all | measured |
| The textures the plugin owns: `tex_playlist`, `tex_info_playlist(_shadow)` 46x46, `tex_cate_music` / `tex_cate_photo` 100x100, `tex_split_line` 43x1, `tex_arrow_r` 31x29, `tex_arrow_u` 25x12 | `--rco-images`, `--export-image` | measured |
| Entry point is an **Edit** option (`msg_edit`): explore_plugin says "To add content to this playlist, select [Edit] from the options menu." (`msg_to_add_content_playlist_select_edit`), and refuses with `msg_error_cannot_edit_playlist_no_track` / `_no_image` when there is nothing to pick from | `--rco-text explore_plugin_full` | measured (strings); the option being on both the playlist row and its items is INFERRED |
| Help lines per state: source pane `%E: Add / Options / Play|View`, playlist pane `%E: Move / Delete / Play|View|Pause`, `msg_help_enter` `%E: Enter`, `msg_help_close_playlist` | `--rco-text playlist_plugin` | measured (strings) |
| Glyph code points in those lines: Cross is the `%E` token, `U+F882` Delete, `U+F883` Options, `U+F88F` Play and Close. The same `U+F882` / `U+F883` pair Group Content and Options in explore_plugin (`msg_folder_class_with_icon`, `msg_option_with_icon`), so F882 = Square and F883 = Triangle; `U+F88F` = Start (`msg_help_start`, `msg_icon_start`) | byte dump of the text export | F883 = Triangle is TRACED by the Options string; F882 = Square and F88F = Start are INFERRED from the PS3 layout |
| `msg_quit_playlist_edit_ask` is the only exit string, with `msg_yes` / `msg_no`; no save string exists | `--rco-text playlist_plugin` | measured; "asked on every Circle, edits already saved" is INFERRED (no Ghidra pass was made) |
| Cross picks a playlist row up and moves it | task brief and the `Move` help text; the drop key (Cross, `msg_help_enter`) is INFERRED | INFERRED |

Not traced: Ghidra was not used. The plugin builds the lists in code, so pane positions, row height, the
header contents and which key drops a picked row are not in the RCO. Everything under "Layout" is INFERRED.

## Behaviour (as built)

* Opens from **Edit** on a Playlists-folder row or on an item inside a playlist; with no library file of the kind it
  shows the firmware's refusal string inline instead. An empty playlist now says explore_plugin's Edit hint.
* Source pane (left): every local file of the playlist's kind under the chosen library folder, by name. Files already
  in the playlist are dimmed. Cross adds the focused file (an already listed one says so). Triangle opens an Options
  plate with Add All (`msg_add_all_playlist`); Cross there adds every missing file in one save.
* Playlist pane (right): Cross picks the row up, Up/Down move it in a preview, Cross drops it (one saved `moveItem`),
  Circle puts it back. Square deletes the row (the file stays on disk).
* Left/Right switch panes, Circle asks `msg_quit_playlist_edit_ask` (No by default; Yes closes). Every change is saved
  at once through `PlaylistStore`, and an open playlist page is rebuilt.
* Move Up / Move Down option rows are gone (the editor replaces them). The folder-level Add All row stays: explore_plugin
  carries `msg_add_all_playlist` itself, so it is an equivalent firmware entry (INFERRED).
* Not built: Play/View/Pause from the editor (Start); its help line is left out rather than shown dead.

## Layout (INFERRED, 1920x1080)

Panes x 140..900 and 1020..1780; header icon + count at y 138..204 (`tex_cate_*` and `tex_info_playlist`, "Playlist (%1)"
from `msg_playlist_playlist_name`); `tex_split_line` at y 230; rows from y 252, 56 px apart, 12 visible; `tex_arrow_r`
between the panes; `tex_arrow_u` marks rows off screen and the picked row; help line at y 978. Row plate and Options
plate are plain translucent shapes, not firmware textures.

## Verification (2026-11-08)

Scratch data `C:\dev\out\f3` (copy of `native-playlists`, three more WAVs); build `C:\dev\out\f3-build`. `--script`
runs with `SQUARE` (= Delete key) added to `main.cpp`'s token table.

* Entry, already-listed refusal, add Delta, pick Charlie and move it down, drop, Square, Triangle > Add All, Circle
  > Yes: playlist file `[Alpha, Charlie, Bravo]` became `[Alpha, Bravo, Delta, Charlie, Echo, Foxtrot]`, matching the
  expected order step by step; the page behind showed "6 Tracks".
* Pick-up then Circle restored the order; a second pick-up and drop saved one move.
* Item menu: Play / Edit / Delete / Information. Empty library: the refusal text appears inline.
* Frames: `C:\dev\out\f3\cap\` (`b2`, `c1`..`c9`, `d1`, `d4`, `e1`..`e3`).
