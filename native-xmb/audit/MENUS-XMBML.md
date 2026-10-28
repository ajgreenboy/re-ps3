# Firmware-driven menus, per-user profiles and list actions (Phase 3)

Status: 2026-10-27. **3.1** (generalised XMBML column reader) and **3.6** (per-user preferences) are
implemented in `src/xmbml_view.{h,cpp}` and `src/preferences.{h,cpp}` and proven by a local CLI built
from the same translation units. **3.2** (options-menu breadth) is designed in
[OPTIONS-MENU.md](OPTIONS-MENU.md) section 10. The shell wiring for 3.1/3.6 is reported as exact hunks
in section 7 because `shell.cpp`/`shell.h` belong to the Phase 1 (Jellyfin) session; the applied build
therefore still draws the hardcoded columns, which is why the layer gate shows no visual change
(section 8).

ASCII note: firmware strings that carry a trademark sign or a star glyph are transliterated here
(`PSP)`, `Debug Settings`); the resolved titles themselves are the RCO's exact UTF-8 text. The reader
returns whatever the RCO holds, unchanged.

---

## 1. What the firmware actually says a column is

Evidence: `/dev_flash/vsh/resource/explore/xmb/category_*.xml` from the local 4.93 dev_flash, read with
`readXmbDocument()` (bounded, DTD-rejecting, duplicate-view-rejecting) and the new resolver. The
`--rco-text` option of the shipping build dumped the English table of every category container, so
every title below is the firmware's own string, not a translation or a guess.

A column is the `root` view's ordered list of `<Query>` nodes. Each query names a *pixmap view*:

- `src="#seg_fixed_items"` - a view in the same file;
- `src="sel://localhost/nplogin?category_friend.xml#seg_fixed_items"` - the selector plugin's view,
  still defined in the same file (the query fields after `#view` are the selector's arguments);
- `src="xmb://localhost/%flash/xmb/category_sysconf.xml#seg_game"` - a view addressed through another
  XMBML document (here the same file);
- `src="xcb://localhost/query?..."`, `src="xil://localhost/list?t=game"`,
  `src="ese://localhost/query?type=install_game"`, `src="user://localhost/users"`,
  `src="now://localhost/q?..."`, `src="chat://localhost/roomlist?"` - a plugin that produces its rows
  at run time and defines no view here.

The view a query names has `<Attributes><Table key="...">` blocks and `<Items>`. **The `<Items>` are the
rows the column shows**, in document order, and they mix two node kinds:

- `<Item class="type:x-xmb/xmlsysconf" key="update" attr="update"/>` - a leaf row;
- `<Query class="type:x-xmb/folder-pixmap" key="game" attr="game" src="..."/>` - a child folder row.
  The Settings column writes Game Settings, Accessory Settings and Network Settings exactly this way,
  *between* the leaf rows, and the labels live in the parent view's table (`game`, `device`, `network`).

That interleaving is why the old query-level reader could only see the Network column: Network's root
queries each expand to one row, while Settings' single query expands to eighteen rows plus three child
folders. `readCategoryView()` is unchanged and still serves the Network integration; the generalised
`resolveCategoryColumn()` walks the interleaved list.

Row order is document order, never alphabetical and never the shell's own order. The Settings column
proves it: the definition's order is System Update, Game Settings, Video Settings, Music Settings, Chat
Settings, System Settings, Theme Settings, Date and Time, Power Save, Accessory, Printer, Display,
Sound, Security, Remote Play, Network - the console's own order, which the shipped hardcoded list
happens to match.

## 2. The reader

### 2.1 Types

- `xmb::QueryKind` - the src scheme: `Local`, `Selector` (sel), `SystemQuery` (ese), `ContentBrowser`
  (xcb), `ContentList` (xil), `UserProvider` (user), `NowPlaying` (now), `Chat` (chat),
  `ExternalDocument` (xmb), `Unknown`. `queryKindName()` is for logs and audits.
- `xmb::FieldSource` - where a field came from: `None`, `Xmbml` (a resource key named it; an icon name
  needs no lookup), `Rco` (a title/info key resolved through the container's English table),
  `Fallback` (the caller's last-resort label). `fieldSourceName()`.
- `xmb::XmbmlColumnRow` - one visible row: `order`, `id`, `queryKey`, `itemKey`, `itemClass`, `viewId`,
  `source`, `provider`, `query`, `container`, `titleRsc`/`iconRsc`/`infoRsc`, `titleKey` (the key the
  title actually came from), `title`/`icon`/`detail`, `titleSource`/`iconSource`/`detailSource`,
  `moduleName`/`moduleAction`, and the flags `hasTable`, `inGameOnly`, `folder`, `dynamic`, `nested`.
  `kind()` maps the item class to `RowKind` with the same rule the query-level row uses (one shared
  function, so the two readers cannot drift).
- `xmb::XmbmlFallback` - the per-column last resort, keyed by the firmware row key: literal
  `title`/`icon`/`detail`, plus `titleRsc`/`iconRsc` for labels the firmware builds in a plugin rather
  than in the view's table (Network's Remote Play and Download Management rows: `msg_psp_connection`,
  `msg_download_control`). A `titleRsc` is resolved through the same container and locale table as the
  definition's own keys, so the text is still the firmware's.
- `xmb::XmbmlResolveOptions` - `flashRoot`, `viewId` (default `root`), `container` (default
  `explore_category_<name>`), `idAliases` (firmware key -> the id this build already uses),
  `fallbacks`, `rowLimit` (default 64, clamped to 128).
- `xmb::XmbmlResolvedColumn` - the rows plus the counters the caller logs and the audit cites:
  `queries`, `expanded` (queries whose view carried more than one row), `skipped`, `untitled`,
  `fallbackRows`, `pluginRows`, `rcoRows`, `duplicateIds`, and `error`.
- `resolveCategoryColumn(category, textResolver, options)` and `categoryColumnPath(category, options)`.

### 2.2 Rules

1. `category` accepts `sysconf`, `category_sysconf.xml` or a path; with `flashRoot` set a bare name
   resolves under `<flashRoot>/vsh/resource/explore/xmb/`.
2. Read the document once. Any error (missing file, DTD, duplicate view id, node overflow, XML error,
   absent view, no row at all) sets `error`, which is the **per-column fallback** signal: the caller
   keeps its built-in rows and logs the reason. Nothing is thrown, nothing is written.
3. For each root `<Query>` in document order: a query without a key is counted `skipped`. Its src is
   split into provider scheme and `#view` fragment. A view present in the document is `Local`; a query
   with no local view is a provider row (`dynamic`).
4. A view with `<Items>` produces one row per `<Item>`/`<Query>` child, in document order. A child
   `<Query>` becomes a single folder row (`folder`, `nested`) - it is not expanded; the next level is
   the page the shell opens. A view with no items produces one row from the query itself.
5. The table is looked up by the node's `attr`, then its `key`; a single-table view is unambiguous and
   may fall back to that table. `title_rsc`/`info_rsc` go through the caller's resolver (the shell
   passes `Assets::text`, i.e. the RCO's English table); `icon_rsc` is a texture name and needs no
   lookup.
6. A row the definition leaves untitled is resolved in this order: the fallback's `titleRsc` (plugin
   label, still RCO text, counted in `pluginRows`), then the fallback's literal `title` (counted in
   `fallbackRows`), else it stays untitled and is counted in `untitled` - the caller decides to skip it.
   The same order applies to the icon (`iconRsc`, then `icon`).
7. The native id is the `idAliases` entry for the row's key, else the key without the firmware's `seg_`
   prefix. Repeated ids are counted (`duplicateIds`) rather than silently de-duplicated. Both phases of
   this pass resolve the columns with zero duplicates.
8. `rowLimit` bounds rows; a definition with more rows than that is truncated with `skipped` counted.

The reader logs nothing: the caller owns the log lines and the disposition (listed, listed but inert,
skipped with a reason). That is deliberate - the shell already has one logging style
(`xmbml: ...`), and a library that prints cannot be reused by the CLI in section 3.

## 3. Resolved row tables

Reproduce with the local CLI (built outside the repository, from the same sources):

```powershell
# 1. the shipping build dumps the English RCO tables (this is Assets::text's own data)
& C:\dev\out\native-menus\re-ps3-native.exe --dev-flash C:\dev\rpcs3\bin\dev_flash `
    --rco-text "explore_category_user,explore_category_sysconf,explore_category_photo,explore_category_music,explore_category_video,explore_category_game,explore_category_network,explore_category_psn,explore_category_friend,explore_category_tv,explore_plugin_full@C:\dev\out\native-menus\menus-rco-text.txt"

# 2. the CLI resolves every column through the same xmbml_view.cpp
& C:\dev\out\native-menus\phase3-check\phase3-check.exe columns C:\dev\rpcs3\bin\dev_flash `
    C:\dev\out\native-menus\menus-rco-text.txt C:\dev\out\native-menus\phase3-report

# 3. and drives Preferences through the profile round trip
& C:\dev\out\native-menus\phase3-check\phase3-check.exe profiles C:\dev\out\native-menus\phase3-report
```

Full output: `C:\dev\out\native-menus\phase3-report\columns.txt` and `columns.json` (machine-readable,
one object per row with its disposition). The tables below are that output, condensed to the columns
that matter (`--` is an empty `src`, i.e. a local `#view` row); `src` is the query scheme, `kind` is
`RowKind` from the item class, `titleSrc` is the `FieldSource`.

### 3.1 Settings (`category_sysconf.xml`, sha256 2d286e4e1a30ada8..., view `root`)

Counters: queries 1, expanded 1, skipped 0, untitled 0, fallback 0, pluginRsc 0, rco 18,
duplicateIds 0.

```
#  key         id          title                      icon           title key                   src  kind      titleSrc
0  update      update      System Update              tex_update     msg_update                  --   unknown   rco
1  edy         edy         Edy Viewer                 tex_edyviewer  msg_edyviewer               --   unknown   rco
2  game        game        Game Settings              tex_game       msg_game_setting            --   unknown   rco        folder nested
3  bd_dvd      video       Video Settings             tex_bddvd      msg_video_setting           --   unknown   rco
4  music       music       Music Settings             tex_music      msg_music_setting           --   unknown   rco
5  chat        chat        Chat Settings              tex_chat       msg_chat_setting            --   unknown   rco
6  console     system      System Settings            tex_console    msg_console_setting         --   unknown   rco
7  theme       theme       Theme Settings             tex_thema      msg_theme_settings          --   unknown   rco
8  date_time   date        Date and Time Settings     tex_datetime   msg_datetime_setting        --   unknown   rco
9  power       power       Power Save Settings        tex_power      msg_power_saving_setting    --   unknown   rco
10 device      device      Accessory Settings         tex_pd         msg_pd_setting              --   unknown   rco        folder nested
11 print       print       Printer Settings           tex_printer    msg_printer_settings        --   unknown   rco
12 display     display     Display Settings           tex_display    msg_display_setting         --   unknown   rco
13 sound       sound       Sound Settings             tex_sound      msg_sound_setting           --   unknown   rco
14 security    security    Security Settings          tex_security   msg_security_setting        --   unknown   rco
15 remote      remote      Remote Play Settings       tex_remote     msg_remoteplay_setting      --   unknown   rco
16 network     network     Network Settings           tex_network    msg_net_setting             --   unknown   rco        folder nested
17 console_qa  console_qa  (*) Debug Settings         tex_update     msg_tool_debug_settings     --   unknown   rco        folder nested
```

`(*)` the firmware string starts with a star glyph, transliterated here.

Column the shell would install: rows 0-16 (16 rows) in that order, ids aliased `console`->`system`,
`bd_dvd`->`video`, `date_time`->`date`, so every existing page id and theme icon keeps working. Rows 1
(`edy`) and 17 (`console_qa`) are skipped and logged:

| skipped | reason |
|---|---|
| edy | the Japan-only Edy Viewer; the console gates it in the sysconf plugin and this build has no page for it |
| console_qa | Debug Settings: QA consoles only |

Two facts this table settles: the shipped 16-row Settings list is exactly the firmware's list in the
firmware's order, and the two rows it omits are the two the console itself does not show a retail user.

### 3.2 Photo (`category_photo.xml`, sha256 4b7c8e768ada2ded..., view `root`)

Counters: queries 9, expanded 0, skipped 0, untitled 7, fallback 1, pluginRsc 0, rco 1, duplicateIds 0.

```
#  key                id            title                     icon                 title key            src  kind        titleSrc
0  scan               scan          Search for Media Servers  item_tex_dlna_scan   msg_scan_mediaserver sel  module      rco
1  hakoniwa           hakoniwa      -                         -                    -                    sel  unknown     none       UNTITLED
2  screenshot         screenshot    -                         -                    -                    sel  unknown     none       UNTITLED
3  gameexit           gameexit      -                         -                    -                    sel  ingame-exit none       ingame UNTITLED
4  gameDir            library       Photo Library             photo_tex_album_pic  -                    xcb  provider    fallback   dynamic
5  seg_dlna_device    dlna_device   -                         -                    -                    xcb  provider    none       dynamic UNTITLED
6  seg_data_device    data_device   -                         -                    -                    xcb  provider    none       dynamic UNTITLED
7  mgmt               mgmt          -                         -                    -                    --   unknown     none       UNTITLED
8  seg_hdd_contents   hdd_contents  -                         -                    -                    sel  provider    none       dynamic UNTITLED
```

Column the shell would install: row 4 (`gameDir`, the HDD content query) as `library`, titled and
iconed from this column's fallback because the definition leaves provider rows untitled - their rows
are the photos themselves. Then this build's own two rows, `options` (Photo Settings) and `choose`
(Choose Media Folder). Rows 0-3 and 5-8 are skipped with the reasons the CLI prints (`search for media
servers` = a DLNA client this build does not have; `hakoniwa`/`screenshot`/`mgmt` = the definition
leaves them untitled because their labels are built by plugins; `dlna_device`/`data_device` = no DLNA
client and no optical/card reader; `hdd_contents` = this build's local library row is `gameDir`;
`gameexit` = in-game XMB only).

### 3.3 Game (`category_game.xml`, sha256 758067206446a7d3..., view `root`)

Counters: queries 18, expanded 0, skipped 0, untitled 11, fallback 0, pluginRsc 0, rco 7,
duplicateIds 0.

```
#  key                  id             title                              icon                title key                        src  kind        titleSrc
0  psp2appdata          vita           PS Vita System Application Utility tex_album_icon      msg_vita_application_utility     --   unknown     rco    folder nested
1  gamedata             data           Game Data Utility                  tex_album_icon      msg_gamedata_utility             --   unknown     rco    folder nested
2  sdvmc                memory         Memory Card Utility                item_tex_ps12util   msg_memory_card_utility          --   unknown     rco    folder nested
3  sdps2                ps2            Saved Data Utility (PS2)           item_tex_ps3util    msg_savedata_utility_ps2         sel  unknown     rco    folder nested
4  sdpsp                psp            Saved Data Utility (minis/PSP)     item_tex_psputil    msg_savedata_utility_minis_psp   --   unknown     rco    folder nested
5  sdps3                ps3            Saved Data Utility (PS3)           item_tex_ps3util    msg_savedata_utility_ps3         --   unknown     rco    folder nested
6  seg_welcome          welcome        -                                  -                   -                                sel  provider    none   dynamic UNTITLED
7  gameexit             gameexit       -                                  -                   -                                sel  ingame-exit none   ingame UNTITLED
8  manual               manual         Software Manual                    tex_manual          msg_soft_manual                  sel  ingame-exit rco    ingame
9  seg_device           device         -                                  -                   -                                xcb  provider    none   dynamic UNTITLED
10 seg_device_bdv       device_bdv     -                                  -                   -                                xcb  provider    none   dynamic UNTITLED
11 seg_device2          device2        -                                  -                   -                                xcb  provider    none   dynamic UNTITLED
12 seg_add_content_device add_content_device -                            -                   -                                xcb  provider    none   dynamic UNTITLED
13 seg_mv_content_device mv_content_device  -                            -                   -                                xcb  provider    none   dynamic UNTITLED
14 gameDir              gameDir        -                                  -                   -                                xil  provider    none   dynamic UNTITLED
15 seg_hdd_install      hdd_install    -                                  -                   -                                ese  provider    none   dynamic UNTITLED
16 seg_pkg_install      pkg_install    -                                  -                   -                                ese  provider    none   dynamic UNTITLED
17 gameDir2             gameDir2       -                                  -                   -                                sel  provider    none   dynamic UNTITLED
```

Column the shell would install: rows 0-5 (vita, data, memory, ps2, psp, ps3) in that order, then this
build's `store` row; installed titles are appended after that by `addInstalledApps()`, so the visible
order is unchanged from today's hardcoded column. Each mapped row is the firmware's own:

| firmware key | native id | firmware title |
|---|---|---|
| psp2appdata | vita | PS Vita System Application Utility |
| gamedata | data | Game Data Utility |
| sdvmc | memory | Memory Card Utility |
| sdps2 | ps2 | Saved Data Utility (PS2) |
| sdpsp | psp | Saved Data Utility (minis/PSP) |
| sdps3 | ps3 | Saved Data Utility (PS3) |

Skipped and logged: `seg_welcome` (welcome provider, no native welcome screen), `gameexit`/`manual`
(in-game XMB only; `manual` is the "Software Manual" row and is the one titled row this build skips on
purpose), the five disc rows and `seg_add_content_device`/`seg_mv_content_device` (no optical drive),
`gameDir`/`gameDir2` (the installed-content scan owns those rows), `seg_hdd_install`/`seg_pkg_install`
(this build installs through Steam, not the console package queue).

Two label deltas to know about: the firmware strings say `Saved Data Utility (minis/PSP)` and
`Saved Data Utility (PS3)` with a trademark sign after PSP and PS3, and today's hardcoded labels omit
it - applying the hunk changes those two strings. And the firmware's icon for `vita` and `data` is
`tex_album_icon`, the generic album/folder texture, while this build ships `item_tex_vitaformat` and
`item_tex_ps3game_folder`; the hunk keeps the shipped textures for exactly those two rows (see 4.3).

### 3.4 Music, Video, Friends, Network, Users, PSN, TV

Music (`category_music.xml`, b1db7204722c14dd...): queries 11, untitled 10, fallback 1, rco 0.

```
#  key              id           title           icon                  title key  src  kind      titleSrc
0  seg_now          now          -               -                     -          now  provider  none      dynamic UNTITLED
1  seg_dlna_scan    dlna_scan    -               -                     -          sel  provider  none      dynamic UNTITLED
2  gameexit         gameexit     -               -                     -          sel  ingame-exit none    ingame UNTITLED
3  gameDir          library      Music Library   music_tex_album_pic   -          xil  provider  fallback  dynamic
4  seg_dlna_device  dlna_device  -               -                     -          xcb  provider  none      dynamic UNTITLED
5  seg_sacd_device  sacd_device  -               -                     -          xcb  provider  none      dynamic UNTITLED
6  seg_cdda_device  cdda_device  -               -                     -          xcb  provider  none      dynamic UNTITLED
7  seg_dsd_device   dsd_device   -               -                     -          xcb  provider  none      dynamic UNTITLED
8  seg_data_device  data_device  -               -                     -          xcb  provider  none      dynamic UNTITLED
9  mgmt             mgmt         -               -                     -          --   unknown   none      UNTITLED
10 seg_hdd_contents hdd_contents -               -                     -          sel  provider  none      dynamic UNTITLED
```

Kept: `gameDir` -> `library`; then native `options`, `choose`. `seg_dlna_scan` is a *provider* row in
Music (its view lives in category_photo.xml, so the fragment names no view in this document) and the
Scan row the Music column shows is the photo file's row - evidence that cross-document fragments are
provider rows for this reader, not local views.

Video (`category_video.xml`, 821f2d5cb94807db...): queries 16, untitled 15, fallback 1, rco 0. Kept:
`gameDir` -> `library`; native `continue` (prepended, Continue Watching), `options`, `choose`. Skipped:
`bddatamgmt` (BD data management), `seg_dlna_scan`, `mgmt` (video editor management), `gameexit`,
`seg_welcome`, the seven device rows (`seg_dlna_device`, `seg_bdmav_device`, `seg_bdc`,
`seg_avchd_device`, `seg_dvdv_device`, `seg_add_content_device`, `seg_mv_content_device`,
`seg_data_device`), `seg_hdd_install`, `seg_hdd_contents`.

Friends (`category_friend.xml`, 8043177c20abca85...): queries 12, expanded 1, untitled 8, rco 6.

```
#  key          id          title                    icon                  title key             src   kind      titleSrc
0  Ignore       block       Block List               item_tex_IgnoreList   msg_ignore_list       sel   unknown   rco    folder nested
1  new_friend   add         Add a Friend             item_tex_NewFriend    msg_friend_register   sel   module    rco
2  Played       players     Players Met              item_tex_Players      msg_play_with         sel   unknown   rco    folder nested
3  new_chat     chat        Start New Chat           item_tex_NewAvc       msg_start_new_chat2   sel   module    rco
4  chat_room    chatroom    Chat Room (Text Only)    item_tex_ChatRoomText msg_text_chatroom     sel   unknown   rco
5  seg_chat     chatrooms   -                        -                     -                     chat  provider  none     dynamic UNTITLED
6  message_box  messages    Message Box              item_tex_Messages     msg_msg_box           sel   unknown   rco    folder nested
7  me           me          -                        -                     -                     sel   unknown   none     UNTITLED
8  named        named       -                        -                     -                     xcb   provider  none     dynamic UNTITLED
9  presence3    presence3   -                        -                     -                     xcb   provider  none     dynamic UNTITLED
10 presence6    presence6   -                        -                     -                     xcb   provider  none     dynamic UNTITLED
11 presence5    presence5   -                        -                     -                     xcb   provider  none     dynamic UNTITLED
12 presence4    presence4   -                        -                     -                     xcb   provider  none     dynamic UNTITLED
13 presence2    presence2   -                        -                     -                     xcb   provider  none     dynamic UNTITLED
```

Kept: rows 0,1,2,3,4,6 -> block, add, players, chat, chatroom, messages. The firmware's order puts
Block List before Add a Friend: that is the definition's `<Items>` order (a `<Query key="Ignore">`
first, then `<Item key="new_friend">`, then `<Query key="Played">`), not a reading mistake, and it is
a visible reorder against today's `add, players, chatroom, chat, messages, block`. The Steam friend
rows the shell appends keep their place (the six `presence*`/`named` provider queries are skipped).

Network (`category_network.xml`, 6f5b84e4ddccf2c2...): queries 7, untitled 2, pluginRsc 2, rco 3.

```
#  key        id      title                      icon            title key             src  kind        titleSrc
0  manual     manual  Online Instruction Manuals tex_onlinemanual msg_onlinemanual      --   browser     rco
1  PRemo      PRemo   Remote Play                tex_premo       msg_psp_connection    sel  remoteplay  rco
2  browser    browser Internet Browser           tex_browser     msg_browser           --   browser     rco
3  kensaku    kensaku Internet Search            tex_kensaku     msg_internet_search   --   module      rco
4  seg_dlctrl dlctrl  Download Management        tex_dl_manage   msg_download_control  ese  download    rco    dynamic
5  gameexit   gameexit -                          -               -                     sel  ingame-exit none   ingame UNTITLED
6  gameDir    gameDir -                          -               -                     xcb  provider    none   dynamic UNTITLED
```

This is the regression proof for the whole reader: the generalised resolver reproduces the shipped
Network column row for row, title for title, **including the two labels the firmware builds in a
plugin** (`msg_psp_connection`, `msg_download_control`), which the shipped
`Shell::resolveNetworkColumn()` carried in a hand-written map. Same five rows, same icons. The
`pluginLabels` and `lastResort` maps in the shell are therefore redundant once the hunk in section 7
routes this column through the same options struct.

Users (`category_user.xml`, 34b99e2f0dc11c23...): 3 queries - `seg_poweroff` (`#seg_poweroff`,
item class `type:x-xmb/xmlpoweroff`, no table: the label is built by the user plugin), and the two
`user://localhost/create` + `user://localhost/users` provider rows, all untitled. **Per-column
fallback**: the column keeps this build's own rows; the user list belongs to `Profiles`.

PSN (`category_psn.xml`, 972d0bc102ac1f7a...): 8 queries, 1 titled row (`trophy` ->
"Trophy Collection"), everything else provider rows (`sel://localhost/fnregist`, welcome, `xil://list?t=psn`,
`xcb` PSN games, `seg_commerce_new`/`seg_plus` views without tables). **Per-column fallback**: no PSN
account, trophy or store backend exists, so the built-in sign-in/trophy/store rows stay.

TV/Video Services (`category_tv.xml`, 4732c69f2bda694f...): 3 queries, none titled (`seg_welcome`,
`gameexit`, `xil://list?t=tv`). There is no static row to build a column from. **Per-column fallback**:
the built-in "My Channels" row stays.

## 4. The column policy (what a definition cannot decide)

A definition says which rows exist and what they are called. It cannot say what *this build* can do
with them, and it cannot supply a label a plugin builds. That is the small per-column table the hunk
carries: id aliases, fallback labels, a keep set, and a skip reason per firmware key. Four
dispositions:

| disposition | when | example |
|---|---|---|
| list | the firmware row maps to a native action | every Settings row; Game's utilities; `gameDir` -> the local library |
| list-unavailable | the firmware row is the column's own content but this build has no backend, and listing the reason is more honest than hiding the row | Network: manual, Remote Play, browser, search |
| skip(reason) | the row cannot be produced (a provider with nothing to list), is in-game-only, or its label is built by a plugin this build does not have; the reason is logged once per row at `XMB_XMBML_LOG=1` | Photo's screenshots/playlists/DLNA devices; Game's disc rows |
| per-column fallback | nothing in the definition can be honoured (Users, PSN, TV) | the built-in rows stay, with the resolved table still logged for the audit |

Why the Photo/Music/Video columns skip rows Network lists as Unavailable: the firmware itself shows
nothing for an empty provider query (that is what `xcb`/`xil` do when the device answers with no
items), and the two titled non-provider rows in those columns (`Search for Media Servers`, `Photo
Gallery`) are the whole of a DLNA/console-app surface this build does not have. Adding them as dead
rows in front of a working library is worse UX than the console, not more honest - the honesty is in
the log line and in this table. Network is the opposite case: those five rows *are* the column, which
is why the shipped build lists them as inert with a reason.

### 4.1 Native rows around the firmware rows

- Photo/Music/Video: `library` (from `gameDir`) then the build's own `options` + `choose` rows.
- Video: `continue` (Continue Watching) is prepended, because it must stay the first row.
- Game: the six utilities from the definition, then `store`, then the installed-title scan
  (`addInstalledApps()` appends at the end, so the final order matches today).
- Friends: the six definition rows, then the Steam friend rows the shell appends (`rebuildFriends`).
- Users: not driven by the definition (per-column fallback); `rebuildUsers()` keeps owning it.

### 4.2 Ids

The row's native id is the alias if one is declared, else the key minus `seg_`. Aliases exist only to
keep ids this build already uses for pages, settings and theme icons (`console`->`system`,
`bd_dvd`->`video`, `date_time`->`date`, `psp2appdata`->`vita`, `gamedata`->`data`, `sdvmc`->`memory`,
`sdps2`->`ps2`, `sdpsp`->`psp`, `sdps3`->`ps3`, `gameDir`->`library`, friend keys -> `add`/`players`/
`block`/`chat`/`chatroom`/`messages`). Renaming a native row now means changing one string here, not a
catalogue.

### 4.3 Icons

An icon is the definition's `icon_rsc`, except where the definition names the generic
`tex_album_icon` (Game's `vita` and `data` rows): the firmware resolves that through the theme's album
icon, and this build ships more specific named artwork for those two rows. The hunk keeps the shipped
texture for them and takes the firmware's icon everywhere else. The theme-icon pass
(`themeEntryIcons`) still overrides by id afterwards, unchanged.

Two consequences worth stating, because both are easy to get wrong when applying the hunk:

- `Entry::container` becomes the column's own RCO (`explore_category_sysconf`, ...) instead of
  `explore_plugin_full`. That is safe: `Assets::bestImage()` resolves a texture *by name* across every
  RCO in `vsh/resource` (the same path the Friends rows already rely on with
  `item_tex_NewFriend` in `explore_category_friend`), so the container only has to be a plausible
  home for the name. The preload pass logs a warning for a name the named container lacks and the draw
  path still finds it.
- A row that maps to a built-in entry keeps that entry's `detail` only until the definition supplies an
  `info_rsc`; the Settings rows therefore gain their own `msg_*_explanation` text on the Information
  page ("Adjusts settings for games."), which is the firmware's wording rather than this build's.

## 5. Profiles (3.6)

### 5.1 Key namespace

Every per-user setting is stored under

```
profile/<id>/<key>
```

where `<key>` is the setting name this build always used (`audio/volume`, `appearance/colour`,
`library/video`, `media/resume`, ...) and `<id>` is the local profile id from `Profiles`
(`profiles.json`: a lower-case UUID without braces). A small console-wide set stays unprefixed and is
shared by every profile:

```
system/name, clock/timeZone, display/fullscreen, power/idleMinutes, display/screensaverMinutes
```

Callers keep using the *logical* key; `value()`, `contains()`, `setValue()` and `setValues()` resolve
it against the active profile. `Preferences::absoluteKey()` exposes the resolution (used by the test
and by this audit).

### 5.2 File format and migration

```json
{"version":1,"values":{"appearance/colour":3,"audio/volume":25,"system/name":"TESTBOX"}}
{"version":2,"active":"<id>","values":{"profile/<id>/appearance/colour":3,"system/name":"TESTBOX"}}
```

A version-1 file is migrated on load: shared keys stay bare, every other key moves under the reserved
profile `local`, `active` becomes `local`, and the file is rewritten as version 2 once (the read handle
is closed first; `QSaveFile` renames, and Windows refuses the rename while another handle holds the
file - that was the one bug the round-trip test caught). A file that cannot be read or validated keeps
its bytes and turns the object read-only, exactly as before.

`local` is a valid profile id that `Profiles` never issues (its ids are UUIDs), so it can only ever
hold the pre-profile settings. It is kept after a switch: it is the only copy of the migrated state.

### 5.3 The switch

`Preferences::setProfile(id, copyFrom = {})` is the only way the active profile changes:

1. `id` must be 1..40 characters of `[a-z0-9-]`, at least one alphanumeric, no leading/trailing dash,
   else nothing is written and `failed(message)` fires;
2. switching to the active id is a successful no-op;
3. if the target has no stored value, it is seeded: from an explicit `copyFrom`, or - only when the
   current profile is the reserved `local` - from `local` itself. That is the one-time migration from
   the single shared set. A brand-new profile otherwise starts from the defaults, which is what the
   console's "Create New User" does. A copy never overwrites a value the target already has and never
   deletes the source's;
4. the values and the new `active` id are committed atomically; any failure leaves memory unchanged
   and reports the reason;
5. `profileChanged(id)` is emitted after the commit so the shell can re-apply its settings view.

Bounds: at most 16 profiles may hold values (the bound `Profiles` enforces); a switch that would
create a 17th fails cleanly, as does `forgetProfile()` on the active profile or on `local`.

`forgetProfile(id)` removes only `profile/<id>/*` and refuses the active profile and `local`.

### 5.4 Round-trip evidence

`phase3-check profiles` (same `preferences.cpp` the shell links, plus `moc`):

```
PASS v1 load: per-user value survives
PASS v1 load: shared value survives
PASS v1 load: active profile is the reserved pre-profile set  [local]
PASS v1 file rewritten as version 2
PASS v1 file: per-user key moved under profile/local/
PASS v1 file: shared key stays unprefixed
PASS v1 file: no bare per-user key remains
PASS absoluteKey resolves against the active profile
PASS switch to profile A adopts the pre-profile set
PASS profile A keeps the migrated colour
PASS profile A keeps the migrated volume
PASS switch is atomic on disk: active names A
PASS profile A values are stored under profile/<id>/
PASS switch to profile B
PASS profile B starts from the default colour
PASS profile B sees the shared system name
PASS profile B writes its own colour
PASS profile B writes the shared system name
PASS switch back to profile A
PASS profile A colour restored
PASS profile A volume restored
PASS shared name is the same in every profile
PASS reopen: active profile is A  [11111111-1111-1111-1111-111111111111]
PASS reopen: A's colour is still 3
PASS reopen: profile list carries local, A and B (sorted)  [1111...,2222...,local]
PASS reject an upper-case profile id
PASS reject an empty profile id
PASS reject an out-of-range volume
PASS reject an unknown key
PASS reject a non-integer colour
PASS rejected writes leave the file unchanged  [6]
PASS refuse to forget the active profile
PASS refuse to forget the pre-profile set
PASS 13 more profiles fit under the 16-profile bound  [13]
PASS a 17th profile with values is refused
PASS the refusal leaves the active profile alone and reports why  [Up to 16 local profiles can store settings.]
PASS forget an inactive profile
PASS forgotten profile has no values
```

38 checks, 0 failures (`profiles.txt`). A/B differ (`appearance/colour` 3 vs 5, `audio/volume` 25 vs
default), the shared name is the same in both, and switching back restores A's stored values - the file
itself is the state of record (`reopen:` checks read a second `Preferences` from the same directory).

### 5.5 Shell wiring for profiles

The hunk (section 7.2) adopts the profile at construction, re-switches on `Profiles::changed`, and
re-applies the settings view. The Users column needs no new action: `Action::SelectUser` already calls
`profiles_.select(entry.value)`, which emits `changed`, which the new connection turns into
`preferences_.setProfile(id)` + `applyPreferences()`. Switching a profile then changes the theme,
wallpaper, clock format, audio volume, media libraries and resume points, without a restart and without
touching another profile's values.

## 6. Options-menu breadth (3.2)

Designed in [OPTIONS-MENU.md](OPTIONS-MENU.md) section 10: `Display All` (honest on a folder page that
hides nothing, so absent until a page actually groups rows), `Copy`/`Move` for local libraries with a
bounded pre-flight, a progress page and a real cancel, `Delete` with a second-level panel confirmation,
the data model, the twelve writes-on-disk safety rules, and the firmware rows that must stay absent.

## 7. Shell hunks (exact)

`shell.cpp`/`shell.h` are owned by the Phase 1 session and were **not edited**. These are the hunks
this work needs, written against the tree as read at 2026-10-27 20:2x. The line numbers are that read's;
`shell.cpp` is being edited by that session, so anchor each hunk by the quoted text, not the number.

### 7.1 shell.h - one declaration

After `void refreshContent();` (line 174):

```cpp
    // Firmware-driven columns (Phase 3.1, audit/MENUS-XMBML.md): every column whose firmware definition
    // this build can honour is resolved from vsh/resource/explore/xmb/category_<name>.xml, in the
    // definition's own order, with its own strings. A column the definition cannot serve - or that this
    // build has no backend for - keeps its built-in rows and logs the reason. Called once after the
    // built-in initialisers, before the theme-icon and normal-map passes (which address rows by id).
    void resolveCategoryColumns();
```

Nothing else in `shell.h` changes.

### 7.2 shell.cpp - the constructor, after `resolveNetworkColumn();`

```cpp
    // Phase 3.1: the other columns, from the same definitions the Network column proved.
    resolveCategoryColumns();
```

### 7.3 shell.cpp - delete the old Settings-only projection

The block that starts `// Bind the current native settings projection to original XMBML resource keys.`
(line 1047) and ends with the `catalog: settings resource unavailable` warning (line 1063) is
superseded: `resolveCategoryColumns()` resolves the Settings column through the same tables, with the
firmware's order and all 18 rows. Delete it. The normal-map pass that follows (line 1067) and the
theme-icon pass (line 1089) keep working because the ids are aliased to the existing ones.

### 7.4 shell.cpp - the column policy and its applier

Insert directly after `Shell::resolveNetworkColumn()` (line 376):

```cpp
// --- Firmware-driven columns for the remaining categories (Phase 3.1) ------------------------------
// A definition says which rows exist, in which order, and what they are called. It cannot say what
// this build can do with them, nor supply a label a plugin builds, so each column carries a small
// native table: the ids this build already uses (pages, settings, theme icons), the last-resort label
// for a row the definition leaves untitled, the rows to list, and the reason for every row left out.
// Every skip is logged once, with its reason, at XMB_XMBML_LOG=1; a column that cannot be resolved at
// all keeps its built-in rows (source "fallback") exactly as the Network column does.
// audit/MENUS-XMBML.md section 3 prints the resolved row table for each column and the disposition of
// every row, from the same reader.
namespace {
struct ColumnSpec {
    QString category;            // "category_sysconf"
    QString columnId;            // this build's Category::id
    QHash<QString,QString> aliases;
    QHash<QString,XmbmlFallback> fallbacks;
    QSet<QString> keep;          // firmware keys to list; empty = every row the definition titles
    QHash<QString,QString> skip; // firmware key -> why this build leaves it out
    QStringList prepend, append; // built-in rows to place before/after the firmware rows, by id
};
QVector<ColumnSpec> columnSpecs() {
    const auto fb=[](const QString& title,const QString& icon) {
        XmbmlFallback fallback;fallback.title=title;fallback.icon=icon;return fallback;
    };
    QVector<ColumnSpec> specs;
    {   // The Settings column: all 18 rows are the firmware's, in the firmware's order.
        ColumnSpec spec;
        spec.category=QStringLiteral("category_sysconf");spec.columnId=QStringLiteral("settings");
        spec.aliases={{"console","system"},{"bd_dvd","video"},{"date_time","date"}};
        // The two rows a retail console does not show: the Japan-only Edy Viewer and QA Debug Settings.
        spec.skip={{"edy",QStringLiteral("the Japan-only Edy Viewer: the console gates it in the sysconf plugin")},
                   {"console_qa",QStringLiteral("Debug Settings: QA consoles only")}};
        specs.push_back(spec);
    }
    for(const auto& id:QStringList{"photo","music","video"}) {
        // The local library is the definition's gameDir content query. The definition leaves it
        // untitled because its rows are the content itself; this build has a single library row, so
        // the label and icon come from the fallback and the source says so.
        ColumnSpec spec;
        spec.category=QStringLiteral("category_")+id;spec.columnId=id;
        spec.aliases={{"gameDir","library"}};
        spec.fallbacks.insert(QStringLiteral("gameDir"),fb(id=="photo"?QStringLiteral("Photo Library"):
                                                          id=="music"?QStringLiteral("Music Library"):
                                                                     QStringLiteral("Video Library"),
                                                          id=="photo"?QStringLiteral("photo_tex_album_pic"):
                                                          id=="music"?QStringLiteral("music_tex_album_pic"):
                                                                     QStringLiteral("video_tex_album_default")));
        spec.keep={QStringLiteral("gameDir")};
        spec.skip={{"seg_dlna_scan",QStringLiteral("Scan for Media Servers: a DLNA client this build does not have")},
                   {"seg_hakoniwa",QStringLiteral("Photo Gallery: no native counterpart")},
                   {"seg_screenshot",QStringLiteral("Screenshots: the label is built by the screenshot plugin; screenshots are ordinary files here")},
                   {"seg_now",QStringLiteral("the now:// provider: playback state is this build's own")},
                   {"seg_sacd_device",QStringLiteral("SACD device rows: no optical drive")},
                   {"seg_cdda_device",QStringLiteral("CDDA device rows: no optical drive")},
                   {"seg_dsd_device",QStringLiteral("DSD device rows: no optical drive")},
                   {"seg_bddata_mgmt",QStringLiteral("BD data management: no Blu-ray drive")},
                   {"seg_editingvideo_mgmt",QStringLiteral("Video Editor: no native counterpart")},
                   {"seg_welcome",QStringLiteral("the welcome provider: no native welcome screen")},
                   {"seg_dlna_device",QStringLiteral("DLNA media server rows: no DLNA client")},
                   {"seg_data_device",QStringLiteral("removable-media rows: no optical or card reader")},
                   {"seg_playlist_mgmt",QStringLiteral("Playlists: the label is built by the playlist plugin and this build has no playlist store")},
                   {"seg_hdd_contents",QStringLiteral("the HDD contents provider: this build's local library row is gameDir")},
                   {"seg_hdd_install",QStringLiteral("install list: this build installs through Steam")}};
        if(id=="video")spec.prepend={QStringLiteral("continue")};
        spec.append={QStringLiteral("options"),QStringLiteral("choose")};
        specs.push_back(spec);
    }
    {   // Game: the six utility rows the definition titles, then this build's store and installed titles.
        ColumnSpec spec;
        spec.category=QStringLiteral("category_game");spec.columnId=QStringLiteral("game");
        spec.aliases={{"psp2appdata","vita"},{"gamedata","data"},{"sdvmc","memory"},
                      {"sdps2","ps2"},{"sdpsp","psp"},{"sdps3","ps3"}};
        spec.keep={"psp2appdata","gamedata","sdvmc","sdps2","sdpsp","sdps3"};
        spec.skip={{"seg_welcome",QStringLiteral("the welcome provider: no native welcome screen")},
                   {"seg_gameexit",QStringLiteral("in-game XMB only")},
                   {"seg_manual",QStringLiteral("the Software Manual row is in-game only")},
                   {"seg_device",QStringLiteral("disc rows: no optical drive")},
                   {"seg_device_bdv",QStringLiteral("disc rows: no optical drive")},
                   {"seg_device2",QStringLiteral("disc rows: no optical drive")},
                   {"seg_add_content_device",QStringLiteral("disc content rows: no optical drive")},
                   {"seg_mv_content_device",QStringLiteral("disc content rows: no optical drive")},
                   {"gameDir",QStringLiteral("the HDD install query: addInstalledApps() appends this build's titles")},
                   {"gameDir2",QStringLiteral("selected-item provider: addInstalledApps() appends this build's titles")},
                   {"seg_hdd_install",QStringLiteral("install list: this build installs through Steam")},
                   {"seg_pkg_install",QStringLiteral("package install list: this build installs through Steam")}};
        spec.append={QStringLiteral("store")};
        specs.push_back(spec);
    }
    {   // Friends: the definition's six rows, then the Steam friend rows the shell appends.
        ColumnSpec spec;
        spec.category=QStringLiteral("category_friend");spec.columnId=QStringLiteral("friends");
        spec.aliases={{"new_friend","add"},{"Played","players"},{"Ignore","block"},
                      {"new_chat","chat"},{"chat_room","chatroom"},{"message_box","messages"},
                      {"seg_chat","chatrooms"}};
        spec.keep={"new_friend","Ignore","Played","new_chat","chat_room","message_box"};
        spec.skip={{"seg_chat",QStringLiteral("the chat:// room list: this build has no PSN chat rooms")},
                   {"me",QStringLiteral("the signed-in-user row: this build's own profile rows carry that state")},
                   {"named",QStringLiteral("person provider: this build appends its own Steam friend rows")},
                   {"presence2",QStringLiteral("person provider by presence: Steam friend rows")},
                   {"presence3",QStringLiteral("person provider by presence: Steam friend rows")},
                   {"presence4",QStringLiteral("person provider by presence: Steam friend rows")},
                   {"presence5",QStringLiteral("person provider by presence: Steam friend rows")},
                   {"presence6",QStringLiteral("person provider by presence: Steam friend rows")}};
        specs.push_back(spec);
    }
    return specs;
}
// Why a firmware row this build has no action for is still worth listing: the reason the row exists.
QString unavailableReason(const XmbmlColumnRow& row) {
    switch(row.kind()) {
    case RowKind::ModuleAction:
        return QStringLiteral("the firmware runs the plugin ")+(row.moduleName.isEmpty()?QStringLiteral("unknown"):row.moduleName)+
            QLatin1Char(':')+(row.moduleAction.isEmpty()?QStringLiteral("unknown"):row.moduleAction)+
            QStringLiteral(", which this build does not have");
    case RowKind::WebBrowser:return QStringLiteral("the firmware hands this row to the PS3 Internet Browser; this build has none and the definition carries no URL");
    case RowKind::RemotePlay:return QStringLiteral("Remote Play needs a PS3 and a registered PSP/Vita");
    case RowKind::DynamicProvider:return QStringLiteral("the row is produced by the console's ")+row.provider+QStringLiteral(":// provider at run time");
    case RowKind::DownloadControl:return QStringLiteral("the console's download plugin lists its queue");
    default:break;
    }
    return QStringLiteral("this build has no native action for the firmware's item class");
}
}
void Shell::resolveCategoryColumns() {
    const bool verbose=qEnvironmentVariableIsSet("XMB_XMBML_LOG") && qEnvironmentVariable("XMB_XMBML_LOG")!=QStringLiteral("0");
    for(const auto& spec:columnSpecs()) {
        const auto category=std::find_if(categories_.begin(),categories_.end(),
            [&spec](const Category& c){return c.id==spec.columnId;});
        if(category==categories_.end())continue;
        // The built-in rows are the fallback *and* the source of the native action: a resolved row
        // keeps the action, value, setting and detail this build already implements for that id.
        QHash<QString,Entry> builtin;
        for(const auto& entry:category->entries)builtin.insert(entry.id,entry);
        const auto keepBuiltin=[&](const QString& reason) {
            qWarning().noquote()<<"xmbml:"<<spec.columnId<<"column keeps its built-in rows:"<<reason;
            for(auto& entry:category->entries)entry.source=QStringLiteral("fallback");
        };
        XmbmlResolveOptions options;
        options.flashRoot=assets_.root();
        options.idAliases=spec.aliases;
        options.fallbacks=spec.fallbacks;
        QElapsedTimer timer;timer.start();
        const auto column=resolveCategoryColumn(spec.category,
            [this](const QString& container,const QString& key){return assets_.text(container,key);},options);
        if(!column.ok()){keepBuiltin(column.error+" ("+column.path+")");continue;}
        QVector<Entry> rows;
        int skipped=0;
        for(const auto& row:column.rows) {
            const auto key=row.itemKey.isEmpty()?row.queryKey:row.itemKey;
            if(row.inGameOnly) {
                if(verbose)qInfo().noquote()<<"xmbml:"<<spec.columnId<<"skip"<<key<<"(in-game XMB only)";
                ++skipped;continue;
            }
            const auto why=spec.skip.constFind(key);
            if(why!=spec.skip.cend()) {
                if(verbose)qInfo().noquote()<<"xmbml:"<<spec.columnId<<"skip"<<key<<"("<<*why<<")";
                ++skipped;continue;
            }
            if(!spec.keep.isEmpty() && !spec.keep.contains(key)) {
                if(verbose)qInfo().noquote()<<"xmbml:"<<spec.columnId<<"skip"<<key<<"(not in this column's keep set)";
                ++skipped;continue;
            }
            if(row.title.isEmpty()) {
                qWarning().noquote()<<"xmbml:"<<spec.columnId<<"row"<<key<<"has no title (no title_rsc, no plugin label, no fallback)";
                ++skipped;continue;
            }
            const auto existing=builtin.constFind(row.id);
            Entry entry;
            if(existing!=builtin.cend())entry=*existing;
            else entry=item(row.id,row.title,row.icon,column.container);
            entry.title=row.title;
            // The definition's icon, unless it is the generic album texture the firmware resolves
            // through the theme: those two Game rows keep this build's named artwork.
            if(!row.icon.isEmpty() && row.icon!=QLatin1String("tex_album_icon"))entry.texture=row.icon;
            // The definition's info_rsc when it has one, else why the row is inert, so Information
            // always explains itself instead of showing a dead row with no reason.
            if(!row.detail.isEmpty())entry.detail=row.detail;
            else if(existing==builtin.cend())entry.detail=unavailableReason(row);
            entry.source=fieldSourceName(row.titleSource);
            if(verbose)qInfo().noquote()<<"xmbml:"<<spec.columnId<<"row"<<entry.id<<"key="+key
                <<"title=\""+entry.title+"\""<<"texture="+entry.texture
                <<"action="+QString::number(int(entry.action))<<"source="+entry.source
                <<"folder="+(row.folder?QStringLiteral("true"):QStringLiteral("false"));
            rows.push_back(std::move(entry));
        }
        if(rows.isEmpty()){keepBuiltin(QStringLiteral("no row of the definition could be listed"));continue;}
        QVector<Entry> final;
        for(const auto& id:spec.prepend)if(builtin.contains(id))final.push_back(builtin.value(id));
        for(auto& row:rows)final.push_back(std::move(row));
        for(const auto& id:spec.append)if(builtin.contains(id))final.push_back(builtin.value(id));
        category->entries=final;
        qInfo().noquote()<<QStringLiteral("xmbml: %1 column resolved %2 rows from %3 (queries %4, expanded %5, sha256 %6): "
                                          "listed %7, skipped %8, rco titles %9, plugin labels %10, fallback titles %11 in %12 ms")
            .arg(spec.columnId).arg(final.size()).arg(QFileInfo(column.path).fileName()).arg(column.queries)
            .arg(column.expanded).arg(column.sha256.left(12)).arg(rows.size()).arg(skipped)
            .arg(column.rcoRows).arg(column.pluginRows).arg(column.fallbackRows).arg(timer.elapsed());
    }
}
```

Notes on that hunk:

- `item(...)` is the existing private helper; the `Entry` it returns already sets the container and the
  theme-hook id is filled later by the theme-icon pass, which runs after this call by id.
- No new member, no new signal, no change to input handling: the rows are ordinary `Entry`s in the
  ordinary `Category`.
- Network is deliberately not in `columnSpecs()`: it already has its own resolver with its own audit
  history. The table in 3.4 shows the generalised reader agrees with it row for row, so folding it in
  is a later, mechanical change (pass its `pluginLabels` as `fallbacks` with `titleRsc`/`iconRsc`).
- Users/PSN/TV are not in `columnSpecs()` at all: they are the per-column fallback case, documented in
  3.4 and logged by the absence of a "resolved" line.

### 7.5 shell.cpp - profile adoption in the constructor

Immediately after `QDir().mkpath(dataDirectory);` (line 990), i.e. **before** `migrateResumePoints()`
so the resume-point migration reads the adopted profile's values:

```cpp
    // Phase 3.6 (audit/MENUS-XMBML.md): settings are per local profile now. setProfile() adopts the
    // pre-profile set once on the first run after the upgrade (it copies profile/local/*), and is a
    // no-op afterwards. It runs before migrateResumePoints() so the resume-point migration sees the
    // adopted media/resume value instead of an empty one.
    preferences_.setProfile(profiles_.activeId());
    // A profile switch is a normal Profiles signal; re-resolve every setting through the new profile
    // and repaint. No restart, and no other profile's values are touched.
    connect(&profiles_,&Profiles::changed,this,[this]{
        if(!preferences_.setProfile(profiles_.activeId()))return;
        applyPreferences();
        rebuildUsers();
        qInfo().noquote()<<"shell: switched to local profile"<<profiles_.activeName();
        emit changed();
    });
```

`Preferences::profileChanged` needs no handler: `applyPreferences()` is called here, and every other
settings read goes through `preferences_.value()` which resolves the new profile. A surface that caches
a value must re-read it in `applyPreferences()` (the existing ones do).

### 7.6 Not needed

- No change to `Action`, `Entry`, `Category`, `Page`, the view, the pad, or the harness.
- No change to `Profiles` (`profiles.cpp` already stores users and the active id, and emits `changed`).

## 8. Verification

### 8.1 Build

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-menus
```

`re-ps3-native.exe` built from this tree (`C:\dev\out\native-menus\re-ps3-native.exe`, 2026-10-27
20:40:44, 3,124,736 bytes; the same tree also built green at 20:31:10) with `xmbml_view.cpp`,
`preferences.cpp` and the untouched `shell.cpp`; the build is clean at `/W4 /permissive- /utf-8` - no
warning comes from either file this pass changed (the warnings the build prints are in `icons.cpp`,
`view.cpp` and `main.cpp`, all outside this fence). The tree is shared with two other sessions, so the
binary also contains their in-flight `icons`/`view`/`text_renderer`/`jellyfin_shell` edits - see 8.3 for
what that does to the named-state gate.

### 8.2 Reader and profiles

The CLI in section 3 compiles `xmbml_view.cpp`, `catalog.cpp`, `preferences.cpp` and a `moc` of
`preferences.h` unmodified, links Qt6Core, and runs both modes:

```
columns exit=0     profile rows:  18 Settings, 9 Photo, 18 Game, 11 Music, 16 Video, 12 Friends,
                                 7 Network, 3 Users, 8 PSN, 3 TV;  0 duplicate ids
profiles exit=0    38 checks passed, 0 failed
```

The CLI lives outside the repository (`C:\dev\out\native-menus\phase3-check\`, source `main.cpp` plus
the moc output) because `CMakeLists.txt` is outside this pass's write fence; nothing in the repository
depends on it.

**In the real binary.** The build above also migrates a version-1 preferences file at startup, which is
the one path the CLI cannot prove (the CLI constructs `Preferences` directly, the app does it inside
`Shell`). With a fresh data directory holding

```json
{"version":1,"values":{"appearance/colour":3,"audio/volume":25,"system/name":"TESTBOX"}}
```

running `re-ps3-native.exe --dev-flash ... --data-dir <dir> --script "wait,quit"` exited 0, logged
`preferences: migrated version 1 to profile local ( 3 values)`, and left

```json
{"active":"local","values":{"profile/local/appearance/colour":3,
                            "profile/local/audio/volume":25,"system/name":"TESTBOX"},"version":2}
```

That is the state the section 7.5 hunk then hands to the active `Profiles` id on first run.

### 8.3 Gate

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --all --exe C:/dev/out/native-menus/re-ps3-native.exe
```

Full log of the last run: `C:\dev\out\native-menus\gate-final.log` (build 2026-10-27 20:40:44).

**Layer gates: green.** Stored baseline pair, every layer exactly at its baseline:

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---:|---:|---:|---|
| frame | 18.20 | 18.20 | +0.00 | .8388 | .8388 | +.0000 | PASS |
| top background | 19.02 | 19.02 | +0.00 | .9251 | .9251 | +.0000 | PASS |
| category icon row | 20.13 | 20.13 | +0.00 | .9022 | .9022 | +.0000 | PASS |
| item label row | 11.86 | 11.86 | +0.00 | .3791 | .3791 | +.0000 | PASS |
| clock text | 10.33 | 10.33 | +0.00 | .3504 | .3504 | +.0000 | PASS |
| info bar | 13.40 | 13.40 | +0.00 | .4973 | .4973 | +.0000 | PASS |

Canonical capture, same run: frame 18.28 / .8288 (+0.26 dB, -0.0013), top background 20.22 / .9336,
category icon row 20.89 / .8966 (-0.16 dB, **+0.0016**), item label row 11.94 / .3909, clock text
11.39 / .4105, info bar 14.78 / .6117 - `PASS: 6/6 layers within budget`.

**Named states: red in the last two runs, for a reason outside this pass.** `states: 0/4 pass`, each
failure a full-frame PSNR of 48.82-50.59 dB at SSIM 0.9996 against the stored native reference (gate:
55 dB / .9995). Every cropped region in those states is `inf` / 1.0000. The cause is visible in the
difference against `out/native-reference/layers/steam-game-root.png`: every changed pixel lies in
`x 112..1212, y 247..325` - the category icon band, 13,003 pixels (0.63% of the frame). That band is
`src/icons.cpp`/`src/icons.h`, which the parallel Phase 2 session modified at 20:35:14/20:35:25 (and
`view.cpp` at 20:37:25), i.e. between the build that was green and the rebuilds that are not. The same
band's SSIM against the *firmware* reference improved (+0.0016), which is what a category-icon material
change is supposed to do.

The honest sequence:

1. `--all` on the 20:31:10 build - which contained this pass's `xmbml_view`/`preferences` changes and no
   icon edit - was **green**: `PASS: 6/6 layers within budget`, `states: 4/4 pass`, `whole set: PASS`.
   (Application log of that run was overwritten by the later runs; the layer table and state results are
   as printed above for the current run, since the layer gate is unmoved.)
2. Rebuilding after 20:35 picked up the other session's in-flight icon material, and the named-state
   self-baseline (which requires a 55 dB match to a deliberately stored native render) fails by 5-6 dB
   in the icon band alone. The reference dates from 19:27, before that change.

Nothing in this pass can move a pixel: `xmbml_view` is a data reader used only by the CLI (the shell
does not call it yet) and `preferences` changes only what is stored. The reference refresh belongs to
whoever lands the icon material change, per that state's own note ("refresh a reference deliberately
after an intended rendering or layout change with `--states --save-reference`"). Re-running `--all`
after that slice lands is the confirming step, not something this pass should paper over.

**No visual change is expected or observed from this pass**: the hunks in section 7 are not applied, so
the running shell still installs the hardcoded initialisers. The column tables in section 3 are the
proof that the wiring, when applied, is faithful.

### 8.4 What this pass did not verify

- The hunk's rendering: applying 7.4 changes row order/labels in Friends (Block List first) and two
  Game labels (trademark sign), and adds firmware strings for rows that were hardcoded. Those are
  visible deltas and would need a fresh named-state capture and a re-baseline - deliberately not done
  here, because the shell is another session's fence.
- A profile switch on screen: the CLI proves the storage and the switch; the Users column's switch
  path is the existing `Action::SelectUser` plus 7.5, and no capture was taken of it.
- A different locale: the resolver takes the caller's text resolver, so a locale is whatever table the
  caller reads. `Assets::text` serves the English table only today; the RCO holds other languages and
  wiring one is a separate change, not a reader change.

## 9. Files touched

- `native-xmb/src/xmbml_view.h`, `native-xmb/src/xmbml_view.cpp` - the generalised reader
  (`QueryKind`, `FieldSource`, `XmbmlColumnRow`, `XmbmlFallback`, `XmbmlResolveOptions`,
  `XmbmlResolvedColumn`, `resolveCategoryColumn`, `categoryColumnPath`); `readCategoryView` is
  unchanged for the Network integration.
- `native-xmb/src/preferences.h`, `native-xmb/src/preferences.cpp` - the `profile/<id>/` namespace, the
  version-1 -> version-2 migration, `setProfile`/`forgetProfile`/`profileIds`/`absoluteKey`, the shared
  key set, bounds and validation.
- `native-xmb/audit/MENUS-XMBML.md` - this document.
- `native-xmb/audit/OPTIONS-MENU.md` - section 10 appended (3.2 design).

Not touched: `shell.cpp`, `shell.h`, `view.cpp`, `view.h`, `jellyfin_*`, `icons.cpp`, `main.cpp`,
`CMakeLists.txt`, `profiles.cpp`, `settings_menu.cpp`, the harness and every reference/threshold file.
