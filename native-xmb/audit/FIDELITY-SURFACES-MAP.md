# FIDELITY SURFACES MAP

The surface-completeness audit of the native C++/Qt6 recreation against the real PS3 4.93 XMB: **which
pages/screens/overlays the firmware has, which of them the native build actually draws, and which of them
the console evidence has never captured.**

- Date: 2026-10-29 (America/Chicago). Tree: `C:\dev\xmb` at `07a4fe4` (one untracked file,
  `work/fidelity-sweep.ts`; nothing else modified).
- Read-only pass. The only file written is this one. No source, no contract, no catalogue, no capture, no
  git command.
- Answers one question and nothing else: **surface completeness**, not pixel fidelity. A surface can be
  `reachable` here and still fail every Tier B budget in `PARITY-SCORECARD.md`; a surface can be `absent`
  here and its geometry perfectly measured in `GEOMETRY-ROOT.md`. Those are different axes and this
  document is careful never to mix them.
- The instrument is the page, not the pixel. `GRAPHICS-INVENTORY.md` counts **60 distinct graphics**;
  `FEATURE-CATALOG.md` counts **307 guide topics**; `PARITY-SCORECARD.md` counts **39 states**. **No
  document in this tree has ever counted the surfaces themselves**, which is why 62 absent pages could sit
  next to a Tier A score of 35/35 without contradiction. This is that count.

---

## 0. What a surface is, and how this count was made

### 0.1 The rule (stated so a later pass can re-derive every number)

> A **surface** is one page, screen or overlay a user can reach: something that can be *on screen by
> itself*. One surface per page class the firmware implements, collapsed only where
> **(i)** the variants differ by language or account-state condition only (the 24 `seg_cam_*` views in
> `category_psn.xml` are one sign-up surface, not twenty-four);
> **(ii)** a wizard's steps are one surface to the user (netconf's 43 pages are *the Network setup
> wizard*; `software_update_plugin`'s 29 are *the System Update wizard*);
> **(iii)** debug/QA pages are excluded as not user-reachable in retail (`page_tool_*` alone is 57 of
> sysconf's 331 pages, and `#seg_console_qa` is a QA segment view in `category_sysconf.xml`).

A surface is **expected** when the firmware defines it. A surface the build has that the firmware does
not is counted separately as **native-only** and never folded into the expected denominator.

### 0.2 What was enumerated, and from where

| Evidence | Count this pass | Source |
|---|---:|---|
| XMBML documents | 28 files | `C:\dev\rpcs3\bin\dev_flash\vsh\resource\explore\xmb\*.xml` |
| XMBML `<View>` elements | **207** (the audits say 206 - see 7.1) | regex over all 28 documents |
| XMBML view ids distinct per file | 146 | same; ids are namespaced per document (`root`, `seg_fixed_items` repeat across files) |
| XMBML `<Query>`/`<Item>` rows | **794** (the audits say 788 - see 7.1) | regex over all 28 documents |
| RCO containers | 122 | `C:\dev\out\rco-dump\all.txt` (`# rco-tree` headers) |
| RCO `Page` objects | **1,174** | same dump, `  Page ` lines |
| RCO object/tree entries | 8,070 | `native-xmb\audit\README.md` §"What the evidence establishes" |
| Console stills cited here | 162 (AG) + 22 (DAY) + 3 (GAPS) + ~150 in the two `ref` batch subdirectories | `out\rpcs3-allgraphics-2026-10-28\frames\`, `out\rpcs3-day-2026-10-28\stills\`, `out\rpcs3-gaps-2026-10-28\stills\`, `out\rpcs3-ref[-2]-2026-10-28\` (those two batches also hold 1,263 and 22,400 PNGs counting bursts) |
| Catalogue states | **39** in the file (the audits' prose says 38; the extra is `dialog-box`). The documents' own split, at 38: 35 capturable, 5 of them native-only, 3 with no backend | `work\parity-catalogue.json`; `PARITY-SCORECARD.md` §2 |

The 1,174 page objects are the firmware's own surface inventory. The count below (125 expected) is what
survives the rule in 0.1: the difference is debug pages, per-language variants, wizard steps, in-game
duplicates of the same column, and the 85+ plugin pages that are only reachable *from* another surface
already counted (a print job's 27 pages are the print wizard; the video editor's 35 are the editor).

### 0.3 The three statuses, defined so they cannot be argued

| Status | Test | Example |
|---|---|---|
| **reachable** | the page exists and is drawn at full depth of content; the user can get to it by a pad/key route in the shipped build and something other than a placeholder is on it | `Settings > Theme Settings` (4/4 rows real) |
| **partial** | the page is drawn but a named part of it is missing: rows that carry no value, a message page standing in for a list, one panel instead of three, a label column that differs | `Settings > System Settings` (26 rows, 7 real); `Video player` (player + time bar, no control panel) |
| **absent** | no page. The row either answers with the generic message page (`Action::Unavailable`, `src\shell.cpp:1694-1699`) or the row does not exist at all | every PSN page; the Internet Browser; Game Data Utility |

A row that answers with the firmware's own *empty-state* string ("No players were found.") is **partial**,
not absent: the firmware draws that same message for an empty list, so the surface's empty state is the
console's. A row that answers with the build's generic "This feature is not available in the native
recreation yet." is **absent**: that sentence is this build's, not the firmware's.

### 0.4 Frame citation shorthand

| Tag | Path |
|---|---|
| `AG/` | `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` |
| `DAY/` | `C:\dev\out\rpcs3-day-2026-10-28\stills\` |
| `R1/` | `C:\dev\out\rpcs3-ref-2026-10-28\` (`dialogs/`, `osk/`, `settings/`, `media/`) |
| `R2/` | `C:\dev\out\rpcs3-ref2-2026-10-28\` (`menus/`, `depth2/`, `bands/`, `tails/`, `error/`) |
| `GAPS/` | `C:\dev\out\rpcs3-gaps-2026-10-28\stills\` |
| `PS/` | `C:\dev\out\rpcs3-ps\frames\` |

Row labels and rects quoted below are the ones the inventories measured
(`out\rpcs3-allgraphics-2026-10-28\INVENTORY.md`), spot-checked this pass against
`AG/col-users-r00.png`.

---

## 1. The totals

### 1.1 The count

| Measure | Value |
|---|---:|
| Firmware surfaces enumerated (expected) | **125** |
| — reachable | **23** (18.4 %) |
| — partial | **40** (32.0 %) |
| — absent | **62** (49.6 %) |
| Native-only surfaces (not in the firmware) | **8** page kinds / row sets (§4), none counted in the 125. The TV column is *firmware* and is counted as expected |
| Expected surfaces the console has never captured | **72** (57.6 % of expected) |

**One half of the PS3 OS is not drawn, and a different, overlapping half has never been photographed.**
Only 53 of the 125 expected surfaces have both a native page and a console frame.

### 1.2 The same numbers, by column

| # | Column | Surfaces | reachable | partial | absent | never captured |
|--:|---|--:|--:|--:|--:|--:|
| 1 | Root category row | 1 | 1 | 0 | 0 | 0 |
| 2 | Game | 9 | 0 | 1 | 8 | 8 |
| 3 | Video | 7 | 0 | 1 | 5 | 3 |
| 4 | Music | 6 | 0 | 1 | 4 | 2 |
| 5 | Photo | 6 | 0 | 1 | 4 | 2 |
| 6 | TV / Video Services | 3 | 0 | 1 | 2 | 3 |
| 7 | Network | 7 | 0 | 2 | 5 | 6 |
| 8 | PlayStation Network | 9 | 0 | 1 | 8 | 8 |
| 9 | Friends | 10 | 0 | 5 | 5 | 9 |
| 10 | Users | 5 | 0 | 3 | 2 | 3 |
| 11 | Settings | 33 | 6 | 13 | 14 | 15 |
| 12 | System-level (shared) | 32 | 16 | 11 | 5 | 13 |
| | **total** | **128** | **23** | **40** | **62** | **72** |

(128 rows above = 125 expected + the 3 native-only pages; the 23/40/62 columns are expected-only, which
is why they sum to 125.)

### 1.3 What the shape says

- **The column roots are all drawn and only one of them matches the console's row set.** Nine of the ten
  column roots are `partial`; Settings is `reachable` and is the only column whose row count, order and
  labels match the frames. That is one defect, not nine: the runtime resolves only Network and the
  Settings column from XMBML (`src\shell.cpp:265-374`, `1069-1071`); the other eight columns are
  hand-written tables (`src\shell.cpp:1013-1059`) whose row sets were never reconciled with the frames.
- **Everything behind a Game/PSN/Network/Friends row is absent.** Every absent column row is either a row
  that exists and answers `Action::Unavailable` (`src\shell.cpp:1694-1699`) or a row the build never
  creates at all (What's New, PlayStation Plus, the Video Editor, Photo Gallery, the media-server rows).
- **Settings and the system overlays are where the work went.** All 23 reachable surfaces are in Settings
  (6), the system overlays (16) and the root row (1); those two tables also hold 24 of the 40 partial
  ones. The build is a very good XMB *chrome* and an almost empty XMB *content*.
- **The console evidence is thinner than the page inventory.** 72 expected surfaces have no frame, and 26
  of those are unreachable on this rig by construction (network, PSN, disc, media DB), so no future batch
  closes them.

---

## 2. The columns

Notation: **fw source** = the XMBML document#view and the RCO page that implements it; **frame** =
`captured` (with the file) or **uncaptured**; **native** = R/P/A with the code path.

### 2.1 Root (1 surface)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| XMB root: the horizontal category row | `xmb_plugin_normal.rco` `page_xmb`; the ten `explore_category_*.rco` containers are its items | `captured` `AG/col-game-r00.png`, one anchor per column | **R** `src\view.cpp:1472` `drawScene`, `Shell::category()` | **10 icons against 9.** The build draws `user, settings, photo, music, video, tv, game, network, psn, friends` (`src\shell.cpp:1013-1059`); the console draws the same list without `tv`. The TV column is a firmware column the *console's state* omits, so this is a state difference, not an invention - but every root/column frame is compared against a 10-icon row. Unfocused categories carry no label on the console; the build draws the enter/leave pair (`AG/INVENTORY.md` §3, commit `0bc5281`). |

### 2.2 Game (9 surfaces: 1 root + 8 pages)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Game column (depth 0) | `category_game.xml#root` (23 views, 23 items) | `captured` `AG/col-game-r00..r06.png` | **P** `src\shell.cpp:1038-1042` + `addInstalledApps()` `src\shell.cpp:195` | Console rows: Vita App Utility / Game Data Utility / Memory Card Utility / Saved Data Utility (minis/PSP) / Saved Data Utility (PS3) / PlayStation Store / Corrupted Data (the installed title). Build rows: the same six utilities **plus `Saved Data Utility (PS2)`** (a row the console does not draw with no PS2 data) and the installed-title scan appended *after* `store`. Six of the seven console labels match (the PSP row drops `minis/`); every row is inert. |
| Game Data Utility | `category_game.xml#seg_gamedata`; `gamedata_plugin` (3 pages) | uncaptured | **A** `Action::Unavailable` | The firmware's page lists installed game data with size and a Delete/Information options menu. Nothing behind the row. |
| Saved Data Utility (PS3) | `#seg_sdps3`; `ps3_savedata_plugin` (8 pages) | uncaptured | **A** | Copy/Delete/overwrite-confirm pages all absent; the options menu's Copy/Delete rows are listed but not selectable (`src\shell.h:96`). |
| Saved Data Utility (PS2) | `#seg_sdps2` via `sel://localhost/ps2emusave` | uncaptured (row not drawn on this console) | **A** | Row itself is native-only vs the frames. |
| Saved Data Utility (minis/PSP) | `#seg_sdpsp` | uncaptured | **A** | |
| Memory Card Utility (VMC) | `#seg_mcutility` → `sdvmc_2nd`, `seg_newvmc`, `sdvmc_copy_newvmc`; `vmc_savedata_plugin` (13 pages) | uncaptured | **A** | Create/format/convert/import pages all absent. |
| PS Vita System Application Utility | `#seg_psp2appdata` + 4 format variants | uncaptured | **A** | |
| PlayStation Store (from Game) | `#seg_welcome` / installed store app; `newstore_plugin` (4 pages) | uncaptured | **A** | The Game column's store row is `Action::Unavailable`; the build's storefront is the native-only Steam browser (§4), a different surface. |
| Package install / Add Content | `#seg_pkg_install` (`ese://localhost/query?type=install_pkg`), `#seg_add_content_device`, `#seg_mv_content_device` | uncaptured | **A** | No install-from-storage surface at all. |

**Game verdict: 0 of 8 pages.** This is the most lopsided column in the build: the seven rows are drawn
at the console's exact sizes and are all dead.

### 2.3 Video (7 surfaces: 1 root + 5 pages + 1 native-only)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Video column (depth 0) | `category_video.xml#root` (8 views) + `category_video_bdu.xml` (7) + `category_video_bdponly.xml` | `captured` `AG/col2-video-r00..r03.png` | **P** `src\shell.cpp:1026-1036` | Console: BD Data Utility / Search for Media Servers / Video Editor & Uploader (**3**). Build: Continue Watching / Video Library / Video Settings / Choose Media Folder (**4**). **No row is shared by label.** The build substitutes a filesystem library browser for the MMS-driven console rows (`MENUS-XMBML.md` 4.1; `REFERENCE-GAPS.md` correction). |
| BD Data Utility | `#seg_bddatamgmt`, `#seg_bddatacommon` | `captured` as a **negative**: `AG/page3-video-row0.png` - activating it draws no page (empty VFS) | **A** | Console behaviour is "inert with nothing to manage"; the build has no row for it at all, so the state cannot even be reproduced. |
| Search for Media Servers | `#seg_dlna_scan` (`xcb` DLNA query); `dlna_plugin` | `captured` `AG/page3-video-row0..1`, `AG/page2-video-item1` (empty body) | **A** | Skipped by policy (`MENUS-XMBML.md` 4.1 "skip(reason)"), logged not drawn. |
| Video Editor & Uploader | `sel://localhost/videoedit` `#seg_editingvideo_mgmt`; `videoeditor_plugin` (**35 pages**) | uncaptured | **A** | The largest single unimplemented plugin in the Video column: trim, add-video, BGM, captions, save menu, upload service select/login/title/uploading/complete. |
| Video Editor save/upload flow | `video_editor_save_menu_*`, `video_editor_upload_*` (18 of those 35) | uncaptured | **A** | Counted separately from the editor surface because it is a distinct multi-screen flow on the console. |
| BD/DVD disc playback | `bdp_plugin` (13 pages: control panel, display, jump, setting, auth, msgbox) + `bdp_disccheck_plugin` + `bdp_storage_plugin` | uncaptured (no BD drive is exposed to the PC) | **A** | Not just the player: the whole disc UI (pop-up menu control panel, angle, audio/subtitle pickers). |
| Video Library page | native-only (no firmware page) | - | **N** `src\shell.cpp:1598-1605` → `openDirectory()` | The build's workhorse video surface; the firmware equivalent is the MMS item list. |

**Video verdict: 0 of 6 firmware pages.**

### 2.4 Music (6 surfaces: 1 root + 4 pages + 1 native-only)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Music column (depth 0) | `category_music.xml#root` (7 views) | `captured` `AG/col-music-r00..r01.png` | **P** `src\shell.cpp:1025` | Console: Search for Media Servers / Playlists (**2**). Build: Music Library / Music Settings / Choose Media Folder (**3**). No row shared. |
| Search for Media Servers | `#seg_dlna_scan` | `captured` `AG/page3-music-row0`, `AG/page2-music-item0` (empty body) | **A** | |
| Playlists | `#seg_playlist_mgmt` (4 views across music/photo) + `playlist.xml` + `playlist_plugin` (5 pages: select, edit, create-new, msg) | `captured` **by class**, from the Photo column: `AG/page2-photo-item1.png` (artwork + `Create New Playlist`) | **A** | The page class is captured, the Music instance is not. The build has no playlist model at all - `playback_queue` is a session queue, not a stored playlist. |
| Now Playing / music mini-player | `#seg_now` (`now://localhost/q?id=XMB_MODULE_MUSIC`); `audioplayer_plugin` (11 pages: full-screen, back screen, control panel, title info, progress, play status, volume, 3 dialogs) | uncaptured | **A** | The build's music panel (`src\view.cpp:2737`) is a *different* surface - a control strip, not the console's full-screen page + control panel + title-information page. |
| Import Music from CD | `page_music_config_import_musiccd`, `musicbrowser_plugin` (4 pages) | uncaptured | **A** | |
| Music Library page | native-only | - | **N** `openDirectory()` | |

**Music verdict: 0 of 4 firmware pages.**

### 2.5 Photo (6 surfaces: 1 root + 4 pages + 1 native-only)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Photo column (depth 0) | `category_photo.xml#root` (11 views) | `captured` `AG/col-photo-r00..r02.png` | **P** `src\shell.cpp:1024` | Console: Search for Media Servers / Photo Gallery / Playlists (**3**). Build: Photo Library / Photo Settings / Choose Media Folder (**3**, none shared, and Photo Settings is a native-only page). |
| Search for Media Servers | `#seg_dlna_scan` | `captured` `AG/page3-photo-row0`, `AG/page2-photo-item0` | **A** | |
| Photo Gallery | `sel://localhost/hakoniwa` `#seg_hakoniwa` (+ `#seg_hakoniwa_dummy`) | uncaptured | **A** | A whole Sony app surface (3D gallery) with no native trace. |
| Playlists | `#seg_playlist_mgmt` | `captured` `AG/page2-photo-item1.png` | **A** | See 2.4. |
| Screenshot | `sel://localhost/screenshot` `#seg_screenshot`; `screenshot_plugin` (2 pages) | uncaptured | **A** | The screenshot viewer/browser (game screenshots saved to the XMB) is absent; the build takes no screenshots. |
| Photo Library page | native-only | - | **N** | |

**Photo verdict: 0 of 4 firmware pages.**

### 2.6 TV / Video Services (3 surfaces)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| TV column (depth 0) | `category_tv.xml#root` (3 views) | **not drawn on this console** (9-column row confirmed on all nine anchors, `AG/INVENTORY.md` §3) | **P** `src\shell.cpp:1037` | The column exists in the firmware and is populated by `xil://localhost/list?t=tv` (registered services). With none registered vsh omits the category; the build draws it always with one row. `REFERENCE-GAPS.md` 4.1 is right that it is structural, and `GRAPHICS-INVENTORY.md` §1 is right that it is a real column-set difference - but neither says the firmware *has* the column, which is why this is `partial` and not native-only. |
| My Channels / service list | `#gameDir` (`xil t=tv`), `seg_welcome` (`sel://localhost/welcome?type=tv`) | impossible to capture on this rig | **A** | The single row `My Channels` is `Action::Unavailable`. |
| TV service preview | `sel://localhost/welcome?type=tv` | impossible | **A** | |

**TV verdict: the column is the one thing the build has that the console's state does not, and the pages
behind it are absent.**

### 2.7 Network (7 surfaces: 1 root + 6 pages)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Network column (depth 0) | `category_network.xml#root` (6 views) | `captured` `AG/col-network-r00..r03.png` | **P** `src\shell.cpp:265-374` | Build: 5 rows (manual, Remote Play, browser, search, Download Management). Console: **4** (manual, Remote Play, browser, search). The fifth is `seg_dlctrl`, which the firmware only draws when the download query returns rows. Four of the five are inert. |
| Online Instruction Manuals | `#seg_manual` (`type:x-xmb/xmlwebbrowser`); `ingame_manual_plugin` | uncaptured | **A** | `RowKind::WebBrowser` → `Action::Unavailable` (`src\shell.cpp:309-311`). |
| Remote Play | `sel://localhost/nopremo` → `#seg_premo`; `premo_plugin` (8 pages: brightness, infobar, btnnavi, subdisplay) | uncaptured | **A** | `RowKind::RemotePlay` → Unavailable (`src\shell.cpp:316-318`). |
| Internet Browser | `#seg_browser`; `webbrowser_plugin` (**42 pages**) + `webrender_plugin` (**43 pages**) | uncaptured (the browser opens offline; no batch ever pressed it) | **A** | The single largest missing plugin surface in the firmware (85 page objects): chrome, top bar, status bar, bookmark/history lists, tab view, and 25 options pages. |
| Browser options / bookmarks / history | `opt_*_page` (25 in `webbrowser_plugin`, 24 in `webrender_plugin`) | uncaptured | **A** | Counted separately from the browser surface because it is a nested menu system with its own pages. |
| Internet Search | `#seg_kensaku`; `kensaku_plugin` | uncaptured | **A** | A browser handoff with a search template. |
| Download Management | `#seg_dlctrl` (`ese://localhost/query?type=download_control`); `download_plugin` (9 pages) | **not drawn on this console** (no queue, no network) | **P** `Action::SteamDownloads` `src\shell.cpp:805` | The build lists Steam's own queue on a native page. That is a substitute surface, not the firmware's (no select-device, headers, basic-auth, progress or instant-play pages). |

**Network verdict: 0 of 6 pages; the one working row is a substitute.**

### 2.8 PlayStation Network (9 surfaces: 1 root + 8 pages)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| PSN column (depth 0) | `category_psn.xml#root` (44 views - the largest document) | `captured` `AG/col-psn-r00..r04.png` | **P** `src\shell.cpp:1049` | Console: Sign Up / What's New / Trophy Collection / PlayStation(R)Plus / PlayStation(R)Store (**5**). Build: Sign In / Trophy Collection / PlayStation Store (**3**). Two console rows absent, one console row replaced by a different one, all inert. The console's full-width PSN banner band is also a column-level graphic the build does not draw. |
| Sign Up / account creation | `#seg_regist` (`sel://localhost/fnregist`); `regcam_plugin`, `auth_plugin` (4), `np_eula_plugin` (2), `eula_net/hcopy/cddb` | uncaptured | **A** | The 24 `seg_cam_*` condition variants are this one surface. |
| Sign In / account management | `npsignin_plugin` (6 pages: netcheck, goldcheck, signin, signing-in, wait, background) | uncaptured | **A** | The build's `Sign In` row answers `Action::Unavailable`; the catalogue scores this state `message-only` and `REFERENCE-GAPS.md` §7 item 5 files it do-not-chase. |
| What's New | `#seg_welcome` (`sel://localhost/welcome?type=psn`); `hknw_plugin` (**24 pages**) | uncaptured | **A** | Nothing in `src\` mentions What's New. A 24-page plugin with a library/playlist/comment/busy model, absent entirely. |
| Trophy Collection | `#seg_trophy`; `np_trophy_plugin` (9) + `np_trophy_ingame` (4) + `friendtrophy_plugin` (10) | uncaptured | **A** | The row exists and answers a message; `PHASES-2026-10-28.md` 6.3 records the verdict (no backend, `TROPUSR.DAT` encrypted) and that the honest-wording hunk is unapplied. |
| PlayStation(R)Plus | `sel://localhost/nogold` → `#seg_plus`, `#seg_plus_shop` | uncaptured | **A** | No row in the build at all. |
| PlayStation(R)Store | `#seg_commerce_new`, `#seg_commerce`, `#seg_commerce_shop`; `newstore_plugin` (4) + `newstore_effect` | uncaptured | **A** | |
| PSN download list | `download_list.xml` (19 views), `videodownloader_list.xml` (4), `upload_list.xml` (8) | uncaptured | **A** | The PSN download queue and its upload lists (photo/music/video to network storage). |
| Profile / avatar editor | `profile_plugin` (8 pages: main, detail, base, change-color, confirm, progress, error) | uncaptured | **A** | Avatar + profile colour, the PSN identity surface. |

**PSN verdict: 0 of 8.** This is the emptiest column in the build: three inert rows where the console
draws five, and eight documented app surfaces behind them.

### 2.9 Friends (10 surfaces: 1 root + 9 pages)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Friends column (depth 0) | `category_friend.xml#root` (10 views) | `captured` **rowless**: `AG/col-friends-r00..r04.png` (`AG/INVENTORY.md` 5.9) | **P** `src\shell.cpp:1050-1058` | **6 rows against 0.** The build's six rows are the firmware's `seg_fixed_items`, which is behind `sel://localhost/nplogin` - i.e. they exist only for a *signed-in* console. On a signed-out console the firmware draws an empty column, which is what the frame shows. So the build renders a login state the console has never been in, and renders it inert. |
| Add a Friend | `#seg_fixed_items` → `friendml_plugin` (4 pages) | uncaptured | **A** | |
| Players Met | `msg_play_with` row; no dedicated plugin page (list from the presence query) | uncaptured | **P** | The row shows the firmware's own `msg_error_no_player` ("No players were found.") - the console's empty state, correctly. No list behind it. |
| Chat Room (Text Only) | `#seg_chat` (`chat://localhost/roomlist`); `avc2_text_plugin` (11 pages: room list, member list, voice, volume, chat selector) | uncaptured | **A** | |
| Start New Chat | `sel://localhost/newchat` `#seg_new_chat`; `friendim_plugin` (**19 pages**) + `friendim_plugin_game` (14) | uncaptured | **A** | The text-message composer (send/recv/select/friend list/templates/attach/room list/legal/grief). 19 pages, nothing in `src\`. |
| Message Box | `sel://localhost/npaccount` `#seg_message_box` | uncaptured | **P** `src\shell.cpp:1982-1989` | The page exists with the firmware's three rows (Received / Sent / Create Message); every one answers "There are no messages." - the console's empty state, no message model. |
| Block List | `msg_ignore_list` row | uncaptured | **P** | Firmware's `msg_error_not_registered` message; no list. |
| Friends list / online friends | `#named`, `presence2..6` queries; `friendlist_page` in friendim | uncaptured | **P** `rebuildFriends()` `src\shell.cpp:980` | The build lists Steam friends in this column (native substitute rows), with presence states. A real surface, but not the firmware's PSN list, and its avatars are unapplied (`PHASES` 6.2). |
| Friend profile / trophy compare | `friendtrophy_plugin` (10 pages: compare title, info title, cmp flag, info group, share) | uncaptured | **A** | |
| AVC video chat room list | `sel://localhost/avcroom` `#seg_avcroom`; `avc2_game_plugin`, `avc_game_plugin` (12 pages) | uncaptured | **A** | Video chat. Nothing in `src\`. |

**Friends verdict: 0 of 9 pages.** Five of the ten surfaces are `partial` - the column plus four list
pages that render the firmware's own empty-state string (Players Met, Message Box, Block List) or a Steam
substitute (the friend list); the other five are absent.

### 2.10 Users (5 surfaces: 1 root + 4 pages)

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Users column (depth 0) | `category_user.xml#root` (4 views) + `category_user_login.xml` (2) | `captured` `AG/col-users-r00..r02.png` | **P** `src\shell.cpp:1014`, `rebuildUsers()` `src\shell.cpp:3013` | Console: **Turn Off System / Create New User / \*User** (3). Build: **Create New User / \*User** (2). The first console row is missing; its action survives only inside the PS menu (`Turn Off the System`, `src\shell.cpp:1730`). |
| Turn Off System | `#seg_poweroff`; `poweroff_plugin` (4 pages: template, filelist dialog, install dialog, wait) | row `captured` (`AG/col-users-r00.png`); the power-off page is uncaptured | **A** as a row (`src\shell.cpp` has no such row) | The action is reachable only through the PS hold menu, and there it opens a native-only confirm box (see 3, S17), not `poweroff_plugin`'s page. |
| Create New User (name page) | `user://localhost/create`; `user_plugin` (2 pages: `page_create`, `page_create_dialog`) | `captured` `AG/page-user-create.png` (+ `-after-osk`), `R1/osk/user-create-dialog.png` | **P** `src\view.cpp:145` (`requestNewUser` → `openOsk`) | **The console's page is skipped.** The firmware draws a centred `Enter a user name.` page with a 1 px field prefilled `User 1`, an `OK` button and an `X Edit / O Back` legend; the build goes straight from Cross on the row to the OSK. Two of the three graphics in `GRAPHICS-INVENTORY.md` #60 have no native surface. |
| User list / switch user | `user://localhost/users` (`user_plugin page_create_dialog`) | uncaptured (only one user exists, so no list page was opened) | **P** `Action::SelectUser` `src\shell.cpp:1534` | Users are *rows*, not a page. Switching works and is measured (`e204e29`), but the console's user-selection page (avatar list, per-user last-played) does not exist. |
| Login page / user login column | `category_user_login.xml#root` | uncaptured | **A** | The pre-login XMB column state (the console's boot-time user picker). |

### 2.11 Settings (33 surfaces)

Level 0 and the 16 level-1 pages are **all present**, and every firmware row now exists (the settings
phase, `487895e`; `SETTINGS-COVERAGE.md` 8.3: 143 rows, **68 real / 75 placeholder / 0 absent**). The
column is therefore the most complete part of the build and the only one whose row count matches the
console exactly.

| Surface | Firmware source | Console frame | Native | Gap note |
|---|---|---|---|---|
| Settings column (depth 0) | `category_sysconf.xml#root` → `#seg_fixed_items` (18 tables) | `captured` `AG/col-settings-r00..r15.png` | **R** `settingsColumnEntries()` `src\settings_menu.cpp:1383`, applied `src\shell.cpp:1069-1071` | 16 rows, 16 carry a value, order and help lines match the frames (fixed in `487895e`: the second label line is drawn on all three `child=segment` rows). |
| System Update | `#seg_fixed_items` row → `software_update_plugin` (29 pages) | `captured` `R1/settings/system-update-page.png`, `R1/settings/system-update-page-2.png`, `R2/error/sysupdate-page-rest.png` | **P** `src\settings_menu.cpp:1971` | 2 rows, 0 real: both rows explain that this build has no update channel. The 29-page wizard behind them is absent. |
| Game Settings | `#seg_game` (segment view) | `captured` `R1/settings/page-game-settings.png` | **P** | 6 rows, 0 real. |
| Video Settings | `page_bdvd_config` (30 pages in sysconf) | `captured` `R1/settings/page-video-settings.png`, `R2/tails/video-page-top.png` + `video-down01..22` | **P** | 19 rows, 0 real - the host has no optical drive, so every BD/DVD row is a labelled reason. |
| Music Settings | `page_music_config` (10 pages) | `captured` `R1/settings/page-music-settings.png` | **P** | 5 rows, 1 real (Output Frequency). |
| Chat Settings | `page_chat_config` (2 pages) | `captured` `R1/settings/page-chat-settings.png` | **P** | 3 rows, 2 real (camera, audio input). |
| System Settings | `page_console_config` (~35 pages) | `captured` `R1/settings/page-system-settings.png`, `R2/tails/system-page-top.png` + `system-down01..20` | **P** | 26 rows, 7 real. The page's own sub-pages (System Information, Console Information, About PS3) are the `D5` info page only. |
| Theme Settings | `page_theme_config` (6 pages) | `captured` `AG/page-settings-theme.png`, `R1/settings/page-theme-settings.png`, `DAY/page-settings-theme__...png` | **R** | 4 rows, 4 real; four choice bands drawn from the RCO. |
| Date and Time Settings | `page_date_time_config` (6 pages) | `captured` `R1/settings/page-date-time-settings.png`, `date-time-*.png`, `dateformat-list-*`, `timeformat-list-*` | **R** | 6 rows, 6 real. |
| Power Save Settings | `#console_auto_power_off_option` | `captured` `R1/settings/page-power-save-settings.png` | **P** | 2 rows, 1 real; the firmware's two-timer level is one 5-item list. |
| Accessory Settings | `#seg_device` (segment view), `deviceconf_plugin` (29 pages) | `captured` `R1/settings/page-accessory-settings.png`, `accessory-*.png` | **P** | 14 rows, 7 real (the four host-parameter rows included). The 29-page device wizard is absent. |
| Printer Settings | `page_print_config`; `print_plugin` (27 pages) | `captured` `R1/settings/page-printer-settings.png` | **R** | 2 rows, 2 real (host printer enumeration). The 27-page print manager is absent. |
| Display Settings | `page_display_config` (6 pages) | `captured` `R1/settings/page-display-settings.png`, `display-page-rest.png` | **P** | 6 rows, 3 real; no output-resolution sub-flow. |
| Sound Settings | `page_sound_config` (3 pages) | `captured` `R1/settings/page-sound-settings.png` | **P** | 3 rows, 2 real; the audio-device picker page is absent. |
| Security Settings | `page_security_config` (5 pages) | `captured` `R1/settings/page-security-settings.png` | **P** | 6 rows, 0 real - values were deliberately *removed* rather than invented. No PIN/password flow. |
| Remote Play Settings | `page_remote_config` (20 pages) | `captured` `R1/settings/page-remote-play-settings.png` | **P** | 5 rows, 0 real; no registration chain. |
| Network Settings | `#seg_network`, `page_network_config`, `netconf_plugin` (**43 pages**) | `captured` `R1/settings/page-network-settings.png`, `DAY/page-settings-network__...png`, `R2/depth2/network-settings-page.png` | **R** | 5 rows, 4 real; the connection test and the status list work. The 43-page setup wizard is absent. |
| Network connection status list (depth 2) | `page_connection_settings_list` / `page_network_config_information` | `captured` `AG/page-network-status-list.png`, `R2/depth2/statuslist-rest.png` + `statuslist-row2..6` | **R** | 13 rows, 13 real. |
| Set Date and Time page | `page_date_time_config_set_date_time` | uncaptured | **A** | `SETTINGS-COVERAGE.md` 2.3: the row opens a read-only info page. |
| Display output configuration flow | `page_display_output_config_*` (9 pages: port/tv select, signal type, resolution select, resolution check, TV type, display size, setting list, saved) | uncaptured | **A** | No page; `TV Type` was removed as a row. |
| Format Utility chain | `page_console_disc_utility_{select,ask,proceed,finish}` | uncaptured | **A** | Unavailable. |
| Restore Default Settings chain | `page_console_setting_init_{top,proceed,finish}` | uncaptured | **P** `Action::ResetPreferences` `src\shell.cpp:1701` | Collapsed to one confirm box that performs a real, confirm-gated reset of this profile's settings. The three firmware pages (with their itemised list of what is reset) do not exist. |
| Restore PS3 System chain | `page_console_init_ps3_{top,choice_type,proceed,finish}` | uncaptured | **A** | Unavailable. |
| Format HDD chain | `page_console_format_hdd_*` (10 pages) | uncaptured | **A** | No row at all. |
| Security PIN / Change Password | `auth_plugin` `page_auth_plugin_password_number_input`, `page_auth_plugin_password_number_change_completion` (the PIN flow lives in auth, not sysconf) | uncaptured | **A** | Unavailable. |
| Printer manager / selection | `page_print_manager*`, `page_print_select_*` (12 pages) | uncaptured | **A** | Unavailable. |
| Remote Play registration chain | `page_remote_config*` (24 pages: register / list / custom AP / remote boot / audio) | uncaptured | **A** | Unavailable. |
| Device configuration sub-pages | `page_device_config_*` (10 of `deviceconf_plugin`'s 29: key bind, camera, mouse, pointer, keyboard type, key repeat) | uncaptured | **A** | Unavailable; the Accessory page's rows are host parameters instead. |
| Sound output device picker | `page_audio_out_*` (5 pages: connect method, port, HDMI, output format, confirm) | uncaptured | **A** | Replaced by a host report row. |
| About PlayStation 3 | `page_aboutps3` | uncaptured | **A** | Unavailable. |
| Network setup wizard | `netconf_plugin` (43 pages) | uncaptured | **A** | Refused by policy (`SETTINGS-COVERAGE.md` §4: a wizard that writes the host's network configuration is forbidden). |
| Backup / data transfer utility | `page_backup_utility_*` (20 pages) | uncaptured | **A** | No backup or restore surface. |
| System Update wizard | `software_update_plugin` (29 pages) | uncaptured | **A** | Only the first page is drawn (2 rows, no value). |

---

## 3. System-level surfaces (32)

These are the surfaces that belong to no column: the chrome, the overlays, the players, the messages.

| # | Surface | Firmware source | Console frame | Native | Gap note |
|--:|---|---|---|---|---|
| S1 | Info bar (plate + clock + analogue clock + five indicator slots) | `system_plugin` `page_infobar`; `indi_base`, `clock` | `captured` `AG/infobar-game-row1-at-rest.png` + every column frame | **R** `src\view.cpp:1472` (`tex_indi_bg` nine-slice, 1329,75 591x58) | Only the online slot can light (`PHASES` 3.3); the message/chat/avatar/plus slots have no state and stay empty, which is the honest form. |
| S2 | PS-tap controller indicator | `system_plugin` `page_infobar` controller plane; `impose_plugin` pad glyphs | `captured` `GAPS/info-indicator__original__day__1080p.png`, `PS/02-after-tap.png` | **R** `Shell::psTapped()` `src\shell.cpp:1717` | |
| S3 | Idle button-hint plate ("Triangle Options") | `system_plugin` `page_btnnavi` | `captured` `AG/col-game-idle-hint.png`, `col-settings-idle-hint.png`, `col-video-idle-hint.png` | **A** | **No draw path.** The hint-row component exists for dialogs and the player strip (`src\view.h:414`, `src\view.cpp:2717`), but nothing draws the bottom-right idle plate after a few seconds. `GRAPHICS-INVENTORY.md` #19/#20 has no native counterpart despite two dedicated frames. |
| S4 | Notification pop-up | `system_plugin` `page_notification` (`tex_notification` plate + 14 glyphs) | **uncaptured** (no producer on this rig: `REFERENCE-GAPS.md` 4.3) | **P** `Shell::notify()` `src\shell.cpp:1323`, `View::drawNotification()` `src\view.cpp:1105` | Works, with a queue and two producers; the plaque is a fit, not a trace (`fbeb992`: a 130-file/836-frame sweep found no console plaque), and none of the 14 glyph classes has a real trigger. |
| S5 | Busy indicator | `system_plugin` `page_busy` | uncaptured | **P** `src\view.cpp:1704` | A "Loading…" text where the console draws its animated busy glyph. |
| S6 | Item options menu (Triangle) | `explore_plugin_full` (15 pages: `expage_menu`, `info_list`, `control_panel_page`, `volume_page`, `notification_page`, `poweroff_page`, the file/delete/message dialogs) | `captured` `AG/menu-options-open..row4.png`, `R1/dialogs/options-*`, `R2/menus/game-menu-rest.png`, `DAY/options-panel__...png` | **R** `Shell::buildOptions()` `src\shell.cpp:1870` | Two groups at 40 px pitch with the 199 px break, gate badge, chevrons - all measured and drawn. Row *breadth* is the limit (`PHASES` 3.2: Sort By and Information only; no Display All, no Copy/Move/Delete). |
| S7 | Options nested submenu | same, `msg_group_content` child list | `captured` `AG/menu-submenu-group-content.png` | **R** `optionsNested_` `src\shell.h:450` | |
| S8 | Item information page (D5) | `explore_plugin_full` `info_list` | `captured` `R1/dialogs/information-page-01.png`, `-02.png` | **R** `showEntryInformation()` | |
| S9 | Delete confirmation dialog | `expage_fdelete_dialog` (D6) + `filedel_dialog_msgdialog` | `captured` `GAPS/dialog__original__day__1080p.png`, `AG/dlg-confirm-1800.png` | **A** | The `Delete` row is *listed but not selectable* (`src\shell.h:96` "this build has no copy or delete backend yet"). The box surface exists (S11) but no delete route reaches it. |
| S10 | Copy / move / progress dialogs | `filecopy_plugin` (5), `data_copy_plugin` (4), `download_plugin` progress pages | uncaptured | **A** | No file-operation surface of any kind. |
| S11 | Message box (D1, two-button) | `explore_plugin_full` `expage_message_dialog`; `msgdialog_plugin` | `captured` `GAPS/dialog__...png`, `AG/dlg-*` | **R** `Shell::showConfirm()` `src\shell.cpp:1392`, `MessageKind::Box` | Plate 319x176, Yes/No with No focused, `X Enter / O Back`; the metadata block under the plaque is still not drawn (`4144fed`). |
| S12 | Message page (D4) | same | `captured` `AG/page3-music-row0.png`, `R2/error/ictest2-*`, `DAY/page-network-test-error__...png` | **R** `MessageKind::Page` | Rules y199/y879, centred body, back hint. |
| S13 | Inline message (D3) | same | `captured` by measurement (`DIALOGS-FIRMWARE.md`: "There are no content items." at x630, y504) | **R** `MessageKind::Inline` | |
| S14 | Error-code page (D2, scrollable Suggested Actions) | same, `msg_errorcode_*` family | **uncaptured** and unreachable (`REFERENCE-GAPS.md` §7 item 1) | **P** `Shell::showErrorPage()` `src\shell.cpp:1403` | The frame class is implemented; the *content* is this build's own (no firmware code produces a PS3 error code on this rig), so its body is unverifiable by construction. |
| S15 | Info/table page (D5, depth 2) | `page_connection_settings_list` and the sysconf info pages | `captured` `AG/page-network-status-list.png`, `R2/depth2/statuslist-*` | **R** `MessageKind::Info` + `src\dialogs.cpp` table painter | 13-row, 40 px-pitch, right-aligned label / left-aligned value grid. |
| S16 | System message dialog (`msgdialog`) | `msgdialog_plugin` `page_msgdialog` | uncaptured | **P** | The Box is used as the surrogate; the plugin's own layout and its system-message variants are not distinguished. |
| S17 | PS menu (PS hold) | `system_plugin` + `xmb_plugin_normal` `page_xmb_fg`; no RCO page of its own | `captured` `GAPS/ps-menu__original__day__1080p.png`, `PS/03-hold-06.png` | **R** `Shell::psHeld()` `src\shell.cpp:1724`, `psMenuItems()` `:1730` | Three rows with Quit Game conditional, exactly the console's set; the black frame, entry band and `X Enter / O Back` are drawn. The controller/charge panel is placeholders (`PHASES` 4.3). |
| S18 | Volume HUD | `explore_plugin_full` `volume_page`; `videoplayer_volume_page`, `audioplayer_volume_page` | **uncaptured** (needs a BD remote / IR; `REFERENCE-GAPS.md` 4.4) | **R** `View::drawVolumeHud()` `src\view.cpp:2547` | A home-theatre surface with no console cell. Plate + ten lvmeter segments now share one opacity (`07a4fe4`). |
| S19 | Music player full-screen page + control panel | `audioplayer_plugin` (11 pages) | uncaptured | **P** `View::drawMusicPanel()` `src\view.cpp:2737` | The build draws a control strip over the XMB, not the console's full-screen page, title-information page, progress page or play-status page. |
| S20 | Video player full-screen + control panel | `videoplayer_plugin` (15 pages: control panel, display infobar, scene search, time, status, information, change icon, volume, logo, buffering, simple message, av settings, playend menu, store) | uncaptured | **P** `src\view.cpp:1666-1700`, `drawVideoTimeBar()` `:2630`, `drawVideoHelpStrip()` `:2688` | Plays and draws a time bar and a help strip; no control panel page, no scene search, no information page, no playend menu, no av-settings page. |
| S21 | Photo viewer + control panel + slideshow | `photoviewer_plugin` (**22 pages**: front/back screen, control panel, slideshow, special, information, effect mode, zoom, trimming, wallpaper, exif info, multiangle, slide menu) | uncaptured | **P** `src\photo_viewport.cpp`, `src\view.cpp:1666` | Full-screen photo with fit/fill, zoom, pan and rotate and a slideshow timer; none of the 22 pages (control panel, EXIF, effects, trim, wallpaper). |
| S22 | BD player control panel | `bdp_plugin` (13 pages) | uncaptured | **A** | |
| S23 | On-screen keyboard | `oskpanel_plugin` `panel_base_page`; `osk_plugin` `phonekey8` | `captured` `AG/osk-default.png`, `AG/osk-key-down.png`, `R1/osk/osk-default-focus.png`, `DAY/osk__...png` | **R** `View::openOsk()` `src\view.cpp:3341`, `drawOsk()` `:3444`, `src\osk_layout.cpp` | Panel, title bar, field, caret, 4x10 grid, both function strips, 7x7 symbol palette and the prediction column, all from the firmware layout. |
| S24 | OSK variants (full-key panel, phone keypad, clipboard/prediction windows) | `oskfullkeypanel_plugin` (6 pages), `oskpanel_plugin` (6) | partially captured (the default panel only) | **P** | One OSK shape; the full-key panel and the phone keypad are not selectable surfaces. |
| S25 | Text-entry page (user name) | `user_plugin` `page_create` | `captured` `AG/page-user-create.png` | **P** | Skipped - see 2.10. |
| S26 | Choice band | `sysconf_plugin` `page_*_config` choice lists (`page_theme_config_theme/color/background/font`); the band geometry is `explore_plugin_full`'s choice layout | `captured` `AG/page-theme-list.png`, `page-color-list.png`, `page-background-list.png`, `page-font-list.png`, `R2/bands/*` | **R** `View::choiceBandFor()` `src\view.cpp:1397` | Bands built from the RCO lists (63 lists / 442 items). |
| S27 | Depth-1 page class | every `expage_explore_*` page | `captured` every `R1/settings/page-*` frame | **R** `View::drawScene()` | Title bar, y199 rule, focused row at 505, help line, artwork tile, left chevron, label column x626. |
| S28 | Depth-2 page class | `page_connection_settings_list`, info pages | `captured` `R2/depth2/ics-page-01.png` | **R** | Rule pair, 40 px grid, right-aligned labels at 939 / values at 979, `O Back` only. |
| S29 | Screensaver / auto-off display-off | `idle_plugin` `page_dialog_base`; `system_plugin` `page_autooff_guide` | uncaptured | **P** `src\view.cpp:938-945` | The build dims to 55 % and keeps rendering (`SESSION-POWER.md` 4.1); the console's display-off + auto-off guide page is not drawn. |
| S30 | Cold boot (the SCE intro) | `custom_render_plugin` `page_coldboot`, `page_coldboot2` | `captured` outside these batches: `C:\dev\xmb\xmb_phase0_coldboot.png` | **R** `src\coldboot.cpp` | RAF scene, VSMX timeline, ac3 sound, blur/fade schedule. |
| S31 | Game boot (title splash) | `custom_render_plugin` `page_gameboot`, `page_gameboot_bg`; `game_plugin` `page_gameboot` | uncaptured | **A** | `PHASES` P6.4: `gameboot1-5`/`welcome` not scheduled. |
| S32 | In-game XMB + impose dialogs | `xmb_ingame.rco`; `impose_plugin` (15 pages: error red/blue, confirm Y/N, exit game, controller setting, pad battery, clock, btnnavi) | uncaptured | **P** `src\ps_watcher.cpp`, `PS-INGAME-HOOK.md` | The XMB can be brought forward over a tracked title and `Quit Game` works; there are no in-game column variants (`seg_gameexit`), no impose dialogs, no "Quit Game?" confirmation page. |

---

## 4. Native-only surfaces (what the build has that 4.93 does not)

These are not counted in the 125 and must never be scored against a console frame;
`work/parity-catalogue.json` marks five of them `noFirmwareReference: true`.

| Surface | Where | Note |
|---|---|---|
| Video Library / Music Library / Photo Library pages | `openDirectory()` `src\shell.cpp:2623` | The three working media surfaces. The firmware's equivalent is the MMS item list reached from `gameDir` (`xil://localhost/list`), which the build does not read. |
| Continue Watching page | `openContinueWatching()` `src\shell.cpp:2814` | Catalogue state `continue-watching`. No firmware counterpart. |
| Photo Settings page | `src\settings_menu.cpp:1976` | 3 native rows; the sysconf inventory has no such page. |
| Steam Store browse (3 pages) | `Action::SteamStoreBrowse` `src\shell.cpp:1613` | Native storefront, not the PS Store. |
| Steam Download Management page | `openSteamDownloads()` `src\shell.cpp:805` | Occupies the firmware's `seg_dlctrl` row. |
| Jellyfin sign-in / views / folder / search pages | `openJellyfinSignInPage()` `src\shell.cpp:3077` | Four native-only page kinds (catalogue: `page-jellyfin-signin`). |
| Steam friend rows in the Friends column | `rebuildFriends()` `src\shell.cpp:980` | A substitute for the PSN friend list. |
| App editor dialog (Import Games and Apps) | `src\app_editor.cpp` | A Qt modal, not an XMB page (`APP-EDITOR.md`). |

One surface straddles the line and is deliberately **not** in this table: the **TV / Video Services
column**. The firmware defines it (`category_tv.xml`, 3 views) and the console's state omits it
(`xil://localhost/list?t=tv` has no registered service), so it is counted as an expected `partial`
surface in 2.6 - not as an invention, and not as a scoreable console page set either.

---

## 5. The 72 expected surfaces the console has never captured

Completeness for these is **unproven**: the native build may be right or wrong and no frame can say.
**26 of the 72 are unreachable on this rig by construction (marked †)**, so no future batch changes them.

| Column | never captured | which |
|---|---:|---|
| TV | 3 | column root †, My Channels †, service preview † |
| Game | 8 | Game Data Utility, Saved Data Utility (PS3), (PS2), (minis/PSP), Memory Card Utility, Vita App Utility, PlayStation Store, package install |
| Video | 3 | Video Editor & Uploader, editor save/upload flow, BD/DVD disc playback † |
| Music | 2 | Now Playing page, Import Music from CD |
| Photo | 2 | Photo Gallery, Screenshot |
| Network | 6 | Online Instruction Manuals, Remote Play, Internet Browser, browser options/bookmarks/history, Internet Search, Download Management † |
| PSN | 8 | Sign Up †, Sign In †, What's New †, Trophy Collection †, PlayStation Plus †, PlayStation Store †, PSN download list †, profile/avatar editor † |
| Friends | 9 | Add a Friend †, Players Met †, Chat Room (Text Only) †, Start New Chat †, Message Box †, Block List †, friend list †, friend profile/trophy compare †, AVC room list † |
| Users | 3 | Turn Off System's power-off page, user list page, login page |
| Settings | 15 | Set Date and Time, display output flow, Format Utility, Restore Default, Restore PS3, Format HDD, Security PIN, printer manager, Remote Play registration, device configuration, sound device picker, About PS3, network setup wizard, backup utility, System Update wizard |
| System | 13 | notification, busy, copy/move, error-code page †, msgdialog, volume HUD †, music player, video player, photo viewer, BD player †, screensaver/auto-off, game boot †, in-game XMB/impose |

**What that means in practice:** the two halves of the OS the project can actually verify are Settings
(+13 of its sub-flows unverified) and the chrome. Every media player, every notification, every game
utility and every PSN/Friends page is unproven *even where a native page exists*.

Also worth stating, because it inverts a common assumption: **the Internet Browser is not
network-gated.** It opens offline on the console and was simply never pressed - it is capturable today,
and it is the largest single un-implemented surface in the firmware.

---

## 6. The most important missing surfaces, ranked

Ranked by how much of the PS3 OS experience the surface carries: page-object weight in the firmware,
how often a real user met it, and whether its absence leaves a *class* of interaction (not one screen)
unrepresented.

| # | Surface | Weight in the firmware | Why it ranks here |
|--:|---|---|---|
| 1 | **Internet Browser** | `webbrowser_plugin` 42 + `webrender_plugin` 43 pages, of which 25 and 24 are the options menu | The only general-purpose OS surface on the console: bookmarks, history, tabs, encoding, security settings, file upload/download. 85 page objects, the largest single plugin. Nothing in `src\` can render a web page. |
| 2 | **PlayStation Store** | `newstore_plugin` 4 + `newstore_effect` + 3 commerce views + the whole `seg_commerce` chain | The console's shop, and the reason the PSN column exists. The build's Steam browser is a different product, not a recreation. |
| 3 | **Trophy Collection** | `np_trophy_plugin` 9 + `np_trophy_ingame` 4 + `friendtrophy_plugin` 10 = 23 pages | A system-wide feature with its own icon, sync, per-title pages, compare-with-friend and in-game overlay. The row exists and answers a sentence. |
| 4 | **Friends messaging + chat** | `friendim_plugin` 19 (+14 in-game) + `avc2_text_plugin` 11 + `friendml_plugin` 4 | The social half of the XMB: compose, attach, templates, room list, member list, voice, block list. The Friends column draws six rows and none of these pages. |
| 5 | **Save-data management** (Game Data / Saved Data PS3 / PSP / PS2 / Memory Card) | `ps3_savedata_plugin` 8 + `vmc_savedata_plugin` 13 + `gamedata_plugin` 3 = 24 pages | Copy, delete, overwrite-confirm, VMC create/format/convert/import. Four of the Game column's seven rows lead here and all four are dead. |
| 6 | **Media players** (video, music, photo) | `videoplayer_plugin` 15 + `audioplayer_plugin` 11 + `photoviewer_plugin` 22 = 48 pages | Every media surface the console has: control panels, info pages, scene search, effects, EXIF, slideshow menus. The build plays media and draws a strip. |
| 7 | **Video Editor & Uploader** | `videoeditor_plugin` 35 pages | A whole authoring app (trim, add clips, BGM, captions, save, upload with service login and title edit) behind one Video row. |
| 8 | **Settings sub-flows** (15 pages families) | `netconf` 43, `deviceconf` 29, `software_update` 29, `print` 27, `page_backup_utility_*` 20, format/restore chains | The build's settings *rows* are the best in the project, but every wizard behind them - network setup, device config, update, printer, backup, format - is absent, so no destructive or multi-step operation can be performed. |
| 9 | **Copy / move / progress dialogs** | `filecopy_plugin` 5, `data_copy_plugin` 4, `download_plugin` 9 | The build lists `Copy` and `Delete` and cannot perform either; there is no progress or cancel surface in the entire app. |
| 10 | **Notification pop-up (real producers)** | `system_plugin` `page_notification`, 14 glyph classes | Drawn, but never triggered by anything the console triggers it with, and its plaque is a fit. The most-seen transient in normal PS3 use. |
| 11 | **Volume HUD** | `explore_plugin_full` `volume_page` + two player volume pages | The single most-pressed overlay in couch use; drawn natively, never compared. |
| 12 | **In-game XMB + impose dialogs** | `xmb_ingame.rco` 4 + `impose_plugin` 15 | The console's behaviour while a game runs: in-game columns, pad-battery notice, error panels, "Quit Game?" confirmation. The build brings the XMB forward and stops there. |
| 13 | **What's New** | `hknw_plugin` 24 pages | An entire Sony service app with a library, playlists, comments and a service logo, occupying a PSN row the build does not have. |
| 14 | **Photo Gallery** | `sel://localhost/hakoniwa` + `photo_network_sharing_plugin` 4 | The console's showcase app, on a Photo row the build does not have. |
| 15 | **Playlists** | `playlist_plugin` 5 + 4 XMBML views across music/photo | Stored, named, ordered media collections - a data model the build has no equivalent of (its queue is session-only). |
| 16 | **Sign Up / Sign In / account management** | `regcam`, `auth` 4, `np_eula` 2, `npsignin` 6 | Three PSN rows' worth of account surfaces; the build's single `Sign In` row is inert. |
| 17 | **Disc / BD playback + BD data utility** | `bdp_plugin` 13, `bdp_disccheck`, `bdp_storage` | The console's disc home. Structural (no drive on a PC), but it is a whole input class. |
| 18 | **Screenshot viewer** | `screenshot_plugin` 2 + `#seg_screenshot` | The build cannot take a screenshot or show one. |
| 19 | **Idle button-hint plate** | `system_plugin` `page_btnnavi` | Small, but it is a **drawn surface with two dedicated frames and no code path**: the cheapest closed gap on this list. |
| 20 | **Game boot** | `custom_render_plugin` `page_gameboot*`; the five stages `PHASES` P6.4 calls `gameboot1-5` | The title-launch sequence: the console's most recognisable transient after the cold boot. |

Rows 1-6 are the answer to "how much of the OS is missing": those six families alone are **218 of the
firmware's 1,174 page objects**, and four of them (browser, store, trophy, friends messaging) hold **9 of
the 62 absent surfaces**, with save-data adding 5 more. Rows 1-9 are the families where a whole *class* of
interaction is missing, not one screen.

---

## 7. Where the audits disagree - with the firmware, or with each other

Each entry names the document, the claim, and the correction with its evidence. These are the places a
later pass must not quote uncritically.

1. **`README.md` / `PARITY-SCORECARD.md` / `parity.ts --catalogue`: "206 views across 28 XMBML
   documents" and "788 item/query definitions".** Measured this pass: **207** `<View>` elements and
   **794** `<Query>`/`<Item>` elements in those 28 files (no duplicate view id inside any single
   document; ids repeat *across* documents, so a global id map yields 146). Both counts are one and six
   short respectively and no document says why; the per-file counts are in §0.2. Nothing downstream
   depends on either difference, but both are quoted as denominators in three places.

2. **`GRAPHICS-INVENTORY.md` §1: "The native build draws a TV column between Video and Game; that is a
   real column-set difference".** True against the frames, but the same document's framing implies the
   TV column is an invention. The firmware **defines** it (`category_tv.xml`, 3 views) and populates it
   from `xil://localhost/list?t=tv`; `REFERENCE-GAPS.md` 4.1 says so. The correct statement is: the
   column is firmware, the *state* omits it, the build's error is drawing it unconditionally with a dead
   row - not drawing it. This is why §2.6 marks it `partial` rather than native-only.

3. **`MENUS-XMBML.md` 3.1-3.4 present the eight columns as firmware-resolved, but the runtime does not
   use those tables.** `PHASES-2026-10-28.md` 1.2 (item 3.1) says it plainly - "The shell policy hunk
   (MENUS-XMBML 7.1-7.4, `resolveCategoryColumns`) is not in `src`; only Network and the old Settings
   projection run at runtime" - and `src\shell.cpp:1013-1059` confirms it. **The consequence is never
   stated anywhere:** the Photo, Music, Video, Game, PSN, Friends and Users columns in the shipped build
   are hand-written tables whose row sets disagree with every console frame (see 2.2-2.10). That is the
   single most consequential documentation gap this pass found, because it makes the ten `column-*`
   catalogue states look like firmware-derived surfaces when they are not. Only `column-network` is
   `pageModel: xmbml` in the catalogue; `column-settings` and `column-friends` are `native+xmbml`; the
   other six columns say `native` - which is honest, but no document reconciles the *labels*, and three
   columns (Photo, Music, Video) share **zero** row labels with their frames.

4. **`PHASES-2026-10-28.md` 1.2 P2.2: "Screen by screen is 30 firmware states, 5 of which still have no
   reference", against `REFERENCE-GAPS.md` §0's "8 states with no reference anywhere".** The 8 is the
   accurate figure at `6a735e8`/`61d4e8c` and the 5 is the older count before `column-tv`,
   `page-download-management` and `notification` were re-examined. The two numbers appear three sections
   apart in documents that cite each other.

5. **`REFERENCE-GAPS.md` §0: "catalogue states 38".** `work\parity-catalogue.json` ships **39** states
   (`dialog-box` was added after that paragraph). `PARITY-SCORECARD.md` §2 and `PHASES` P0.1 also say 38.
   The denominators the documents use (35 capturable, 30 firmware-referenced, 5 native-only) are the
   38-state figures and would each rise by one if `dialog-box` were counted; nothing in this audit
   depends on which, but three files state a count their own data file contradicts.

6. **`REFERENCE-GAPS.md` §3.1 files `media-player-panel` as group (b) "one file copy away"; the appended
   correction reverses it to group (c).** The correction is right and is proven with the 47,749-line
   RPCS3 log (the media columns come from `metadata_db_hdd`, not a directory scan). But §3.1 is still the
   section a reader lands on, and `PHASES` P0.3 quotes the corrected version. Any new plan should cite the
   correction only.

7. **`SETTINGS-COVERAGE.md` §2.1/2.2 (40 real / 69 placeholder / 34 absent) is superseded by §8.3
   (68 / 75 / 0) at `487895e`.** Both tables are in the same file; §2.2's headline ("34 absent") is
   still cited by `PHASES` in places. The page inventory (§1) is stable; the coverage column is not.

8. **`GRAPHICS-INVENTORY.md` §1 note 5 and `ITEM-ICONS.md` treat the Friends column as rowless without
   saying why.** The firmware's six Friend rows are behind `sel://localhost/nplogin` (`seg_fixed_items`),
   so they exist only for a signed-in console. The build draws them always (`src\shell.cpp:1050-1058`).
   Neither document says the row set is login-state-dependent, which is what turns "6 against 0" from a
   bug into a state difference - and it is still a defect, because the build renders a state the console
   has never been in.

9. **`GRAPHICS-INVENTORY.md` §4 says the PS menu and the PS-tap indicator "did not appear in this
   session" and their frames "are not part of this batch".** Correct for that batch - but both are
   captured elsewhere (`GAPS/`, `PS/`), so the pair must not be carried as uncaptured in any later
   summary. The catalogue's `ps-menu` and `info-indicator` states still show no reference in
   `parity-reference.json` even though `GAPS/` holds 1080p stills for both: **the frames exist and were
   never imported.** That is a bookkeeping debt, not a rig limit, and it is worth two states.

10. **Every document counts graphics, topics or states; none counts surfaces.** `GRAPHICS-INVENTORY.md`
    says "21 surfaces" in one table and 60 graphics in another, and its "surface" numbering (1-21) is a
    *capture batch* numbering, not an inventory of the firmware's pages: it omits every PSN page, every
    Game utility, the browser, the store, the friends pages and the settings sub-flows. Read as a surface
    census it undercounts the firmware by roughly six to one (21 capture surfaces against 125 firmware
    surfaces), and that is the single reason the 62 absent surfaces were invisible in a tree that
    believed it had enumerated the surface set.

---

## 8. What would move each number

Only the levers, narrowly, because this document is read-only and proposes no work.

| Number | Lever |
|---|---|
| 23 reachable | Land the `MENUS-XMBML` 7.4 column policy (would make the ten `column-*` roots firmware-derived; does not by itself make one page reachable), then implement one backend per family - save data (5 Game pages), the browser (1 page + 1 menu system), trophies (1 page) |
| 40 partial | The settings value layer (`host.cpp`) for the 75 placeholder rows; the three media control panels; the notification producers; the user-name page; the idle hint plate |
| 62 absent | By column (§1.2): 14 settings sub-flows, 8 PSN, 8 Game, 5 Network, 5 Friends, 5 system overlays, 5 Video/Music/Photo, 2 Users, 2 TV. Three plugins carry eight of them alone (save-data, browser, friends messaging) |
| 72 unproven | 46 are capturable on this rig with routes that already exist in the manifests; 26 are not (`†` in §5). The cheapest are the game utilities (four Crosses from the Game column), the Internet Browser (one Cross), the settings sub-flows that are only refused by policy, and the import of the ps-menu / info-indicator stills that already exist |
| 8 native-only page kinds | Not a debt: `noFirmwareReference: true` on the five catalogue states among them |

The honest summary of the axis: **the build recreates the XMB's shell and its settings app with real
fidelity, and it does not recreate the console's applications.** All ten columns are drawable and not one
of them has the console's row set; every surface that requires a backend - a store, a trophy system, a
browser, a saved-data manager, a media player panel, a friend list - is a sentence on a message page.
