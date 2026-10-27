# XMBML-driven Network column

Written 2026-10-26. Closes the "one column XMBML-driven" slice of the menus gap: register row M1 in
`audit/FIRMWARE-PARITY-GAPS.md:89` and ranked item 8 at `:180-183`. Scope: the Network category only.
Every other column is still the hand-written list in `src/shell.cpp`; this one is now generated from the
firmware's own view definition, which is the proof of the mechanism row 8 asks for.

Status: Network rows are generated from `category_network.xml` (titles, icons, order, ids and item
classes), with the built-in rows kept as the documented fallback. No Network action was invented: every
row resolves to `Action::Unavailable`, because none of the five has a PC equivalent this build can
honestly perform (see "Item kind to native action").

## The definition used

`dev_flash/vsh/resource/explore/xmb/category_network.xml`, sha256
`6f5b84e4ddccf2c29aeb06a666ddb558c1a26a9bcf72b00edc093d412ee55177`, 3012 bytes. The local inventory
(`--inventory`) reads the same file with 0 errors and 4 literal `&` query separators, which
`readXmbDocument` adapts in memory only.

`category_network.xml` `View id="root"` names the column's rows as seven `<Query>` nodes, in display
order. Each `src` is either a local view (`#seg_...`) or a plugin query whose node is produced at run
time (`sel://`, `ese://`, `xcb://`); everything after `#` names the pixmap view that carries the row's
resources.

| # | Query key | src | view it names | item class in that view |
|---|---|---|---|---|
| 1 | `seg_manual` | `#seg_manual` | `seg_manual` | `type:x-xmb/xmlwebbrowser` |
| 2 | `seg_premo` | `sel://localhost/nopremo?category_network.xml#seg_premo` | `seg_premo` | `type:x-xmb/xmlpremo` |
| 3 | `seg_browser` | `#seg_browser` | `seg_browser` | `type:x-xmb/xmlwebbrowser` |
| 4 | `seg_kensaku` | `#seg_kensaku` | `seg_kensaku` | `type:x-xmb/module-action` (module `kensaku_plugin`, action `kensaku_google`) |
| 5 | `seg_dlctrl` | `ese://localhost/query?type=download_control` | none (plugin query) | - |
| 6 | `seg_gameexit` | `sel://localhost/ingame?path=category_network.xml#seg_gameexit&type=network` | `seg_gameexit` | `type:x-xmb/xmlgameexit` |
| 7 | `gameDir` | `xcb://localhost/query?limit=2048&table=MMS_MEDIA_TYPE_HDD&...` | none (plugin query) | - |

The resource tables the row text comes from:

- `seg_manual` -> Table `manual`: `title_rsc` `msg_onlinemanual`, `icon_rsc` `tex_onlinemanual`.
- `seg_browser` -> Table `browser`: `title_rsc` `msg_browser`, `icon_rsc` `tex_browser`.
- `seg_kensaku` -> Table `kensaku`: `title_rsc` `msg_internet_search`, `icon_rsc` `tex_kensaku`.
- `seg_premo` -> the view is only `<Item class="type:x-xmb/xmlpremo" key="PRemo" />`: no Table, so the
  base file defines the item but not its label. The 4.93 shop variant of the same column
  (`category_network_shop.xml`, same dev_flash) does carry it: `title_rsc` `msg_psp_connection`,
  `icon_rsc` `tex_premo`.
- `seg_dlctrl` -> no view: the `ese://` download provider builds the node. `explore_category_network`
  ships `msg_download_control` ("Download Management") and the `tex_dl_manage` image (33 named images in
  that RCO), so the plugin's label is recoverable from the firmware's own strings.

Titles are the firmware's English RCO strings, not literals in the C++: the same `title_rsc` -> RCO
lookup the Settings projection already uses (`src/shell.cpp:738-751`).

## Resolution rules

`src/xmbml_view.cpp:34` (`readCategoryView`):

1. Read the file through `readXmbDocument` (catalog.h). That reader is already bounded: 8 MiB file cap,
   500 000 node cap, DTD rejected, duplicate view ids rejected, XML errors reported. This code adds a
   32-row cap and never writes to the firmware tree.
2. Take the `root` view's `<Query>` nodes in document order. A `<Query>` without a `key` is skipped
   (no stable identity); any non-`Query` child is not a column row.
3. Resolve the view named after `#`; from that view take the first `<Item>`'s `class`, `attr` and `key`,
   then its `<Table>` (by item attr, then query attr, then the view's only table). The query's own `attr`
   names the *parent* view's attribute table (`seg_browser`'s `focus_priority`), so the item attr is
   tried first.
4. Return `title_rsc`, `icon_rsc`, `info_rsc`, `module_name`, `module_action`, the provider scheme and the
   in-game flag. `RowKind` classifies the item class (`xmbml_view.cpp:25`).

`src/shell.cpp:142` (`Shell::resolveNetworkColumn`) then resolves the strings and builds the `Entry`
rows, logging the source of each title: `xmbml` (a `title_rsc` from the view table), `rco` (a label the
firmware builds in a plugin, resolved through the RCO string it ships), `builtin` (last-resort literal
from the previous list) or `fallback` (the whole column kept its old rows). It is called once from the
constructor at `src/shell.cpp:735`, before the theme-icon pass, so the new ids still receive their
`icon_*` theme entries (`src/shell.cpp:774`).

The native id is the firmware's query key without the `seg_` namespace: `manual`, `premo`, `browser`,
`kensaku`, `dlctrl`. `gameDir` keeps its key and is not a row (see below).

## Resolved rows

From the build (`XMB_DUMP_ENTRIES`, `out/native-reference/xmbml-network/net-dump-final.json`):

| # | id | title | action | source | texture |
|---|---|---|---|---|---|
| 1 | `manual` | Online Instruction Manuals | `Unavailable` (0) | `xmbml` | `tex_onlinemanual` |
| 2 | `premo` | Remote Play | `Unavailable` (0) | `rco` | `tex_premo` |
| 3 | `browser` | Internet Browser | `Unavailable` (0) | `xmbml` | `tex_browser` |
| 4 | `kensaku` | Internet Search | `Unavailable` (0) | `xmbml` | `tex_kensaku` |
| 5 | `dlctrl` | Download Management | `Unavailable` (0) | `rco` | `tex_dl_manage` |

Row 0 is the first query in the file, so the order is the firmware's. The five textures are the same
ones the hand-written list used, and all five exist in `explore_category_network` at 128x128 (the
decoded asset log confirms each).

The `XMB_XMBML_LOG=1` diagnostic reports each row with its kind and the action decision
(`out/native-reference/xmbml-network/net-log-final.txt`):

```
xmbml: network row manual key=seg_manual class=type:x-xmb/xmlwebbrowser title="Online Instruction Manuals" titleSource=msg_onlinemanual texture=tex_onlinemanual action=0 source=xmbml reason="the firmware hands this node to the PS3 Internet Browser; this build has no browser and the XMBML carries no URL to hand to the host one"
xmbml: network row premo key=seg_premo class=type:x-xmb/xmlpremo title="Remote Play" titleSource=rco texture=tex_premo action=0 source=rco reason="Remote Play needs a PS3 and a registered PSP/Vita; the PC equivalent is a different, destructive remote-desktop path"
xmbml: network row browser key=seg_browser class=type:x-xmb/xmlwebbrowser title="Internet Browser" titleSource=msg_browser texture=tex_browser action=0 source=xmbml ...
xmbml: network row kensaku key=seg_kensaku class=type:x-xmb/module-action title="Internet Search" titleSource=msg_internet_search texture=tex_kensaku action=0 source=xmbml reason="the plugin the firmware runs (kensaku_plugin:kensaku_google) is part of the PS3 browser, which this build does not have"
xmbml: network row dlctrl key=seg_dlctrl class=- title="Download Management" titleSource=rco texture=tex_dl_manage action=0 source=rco reason="the row is produced by the console's ese:// provider at run time; this build has no such provider"
xmbml: network skip seg_gameexit (in-game only: this build has no in-game XMB)
xmbml: network skip gameDir (the installed-content scan owns these rows)
xmbml: network column resolved 5 rows from category_network.xml (queries 7, sha256 6f5b84e4ddcc): parsed in 0.45 ms, resolved in 3.48 ms
```

## Item kind to native action

`RowKind` (`src/xmbml_view.h:11-18`) is the firmware's item class mapped to a capability, and
`shell.cpp:177` maps that to an `Action` plus a logged reason. The mapping is explicit so the next
column can reuse it; here is why each decision is `Unavailable` rather than a fabricated success:

| kind | firmware behaviour | native action | why |
|---|---|---|---|
| `WebBrowser` (`manual`, `browser`) | hands a URL to the PS3 Internet Browser | `Unavailable` | there is no PS3 browser here, and the URL is built by the browser plugin, not by the XMBML: pointing `Action::OpenUrl` at the host browser would invent a target the firmware data does not contain |
| `ModuleAction` (`kensaku`, `kensaku_plugin:kensaku_google`) | the plugin drives the browser's search page | `Unavailable` | same browser dependency |
| `RemotePlay` (`premo`) | registers a PSP/Vita over the console's Remote Play service | `Unavailable` | needs console hardware; the PC equivalent (remote desktop) is a different, destructive operation |
| `DynamicProvider` (`dlctrl`, `ese://download_control`) | the download plugin lists the console's download queue | `Unavailable` | this build has no download queue and must not show a fabricated one |
| `InGameExit` (`seg_gameexit`) | only the in-game XMB shows it | skipped | this build has no in-game XMB |
| `gameDir` | `xcb://` lists installed network-category content | not a row | `addInstalledApps()` already appends those rows in the same position (`src/shell.cpp:71-133`), by the firmware's own `CATEGORY` codes `CB`/`WT` |

The example kind->action pairs the register suggests (a browser handoff, `Action::SettingsPage`) were
checked: this build has no browser action at all, and Network has no settings rows, so neither applies.
The mechanism is exercised end to end; the missing piece is a Network service a PC can honestly perform,
not the plumbing.

## Rows the definition names but this build does not show

- `seg_gameexit` - in-game only (`sel://localhost/ingame`). Logged as skipped.
- `gameDir` - installed network content; appended by the existing scan instead of being turned into one
  row. Logged as delegated.
- `seg_premo`'s selector: the base file routes it through `sel://localhost/nopremo`. That selector's
  visibility semantics were not traced, so it is not evaluated: the row is kept. The view the query
  names still defines the item, and the shop variant of the same 4.93 column shows it unconditionally,
  so keeping it matches the previous build and the column's firmware order. This is recorded as a limit,
  not as a trace.

## Fallback

The built-in list is kept, and the reason logged, when:

- the file is missing (`XMBML definition not found: ...`),
- the reader reports an error (malformed XML, DTD, node/size overflow, duplicate view id),
- the `root` view is absent or defines no `Query` row,
- no row could be titled from XMBML, RCO or the last-resort literals.

Evidence, with a scratch dev_flash whose only change is a truncated `category_network.xml` (the real
tree is never modified; the scratch tree is junctions to it plus an 87-byte copy of the file):

```
xmbml: network column keeps its built-in rows: Line 1: Premature end of document. .../vsh/resource/explore/xmb/category_network.xml (definition parsed in 0.23 ms)
xmbml: network fallback row manual ( Online Instruction Manuals ) source=fallback
xmbml: network fallback row remote ( Remote Play ) source=fallback
xmbml: network fallback row browser ( Internet Browser ) source=fallback
xmbml: network fallback row search ( Internet Search ) source=fallback
xmbml: network fallback row downloads ( Download Management ) source=fallback
```

and with the file removed:

```
xmbml: network column keeps its built-in rows: XMBML definition not found: .../vsh/resource/explore/xmb/category_network.xml ... (definition parsed in 0.05 ms)
```

`XMB_DUMP_ENTRIES` after the fallback lists the same ids, titles and actions as before the change
(`manual`, `remote`, `browser`, `search`, `downloads`, all `Unavailable`, `source=fallback`). The warning
is unconditional; only the per-row source lines need `XMB_XMBML_LOG=1`.

## Diagnostic

- `XMB_XMBML_LOG=1` - one line per resolved row (id, query key, item class, title, `titleSource`,
  texture, action, source, reason), the skipped queries, and the summary line with the definition's
  sha256 and timings.
- `XMB_DUMP_ENTRIES=<file>` - the catalogue dump now carries a `source` field on generated rows only
  (`src/shell.cpp:610`), so the Network rows say `xmbml`/`rco`/`fallback` and every other column is
  unchanged.
- The Windows build has no console: add `XMB_LOG=<file>` to capture the same messages (the existing
  local-diagnosis path in `src/main.cpp:78`). Redirecting stderr (`2> file`) also works but PowerShell
  truncates long native stderr lines, which cuts the reason text.

## Startup cost

The definition is read once, in the `Shell` constructor. There is no per-frame or per-refresh cost.

- Parse (`readCategoryView`): 0.23-0.45 ms over the runs recorded here; 0.05 ms when the file is absent.
- Whole block (parse + the RCO English-string table read for `explore_category_network`): 0.9-3.5 ms;
  the final build measured 0.45 ms parse, 3.48 ms resolved.
- Process-start to `catalog: settings projection resolved`, 7 runs each, alternating scratch dev_flash:
  XMBML present median 78.8 ms (77.2-84.4), definition broken median 78.3 ms (77.2-81.1): +0.5 ms
  median, inside run-to-run spread.
- First frame (`native display refresh`) in the same build: 1036 ms, so the added work is under 0.3% of
  startup and does not move the user-visible start.

## Regression evidence

- Layer gate, `bun work/ab-layers.ts --capture --exe C:/dev/out/native-xmbml/re-ps3-native.exe`:
  PASS 6/6 (frame 17.95/0.8286, top background 20.24/0.9344, category icon row 20.91/0.8947, item label
  row 11.94/0.3913, clock text 11.39/0.4105, info bar 14.78/0.6117).
- Named states: `--states` PASS 4/4 (steam-game-root, steam-all-games, steam-store-sections,
  steam-not-installed), every region at inf/1.0000 against its stored reference.
- PS menu, options panel and notifications: captures on the final build show
  `shell: PS menu opened`, `shell: options opened category= network depth= 0 entry= manual rows= 2
  Information|Close`, and a notification with `notificationTitle=Download complete`,
  `notificationIcon=tex_notification_info`, `notificationRemainingMs` counting down.
- Catalogue dump diff against the previous build (`native-lead`): 8 of 10 categories byte-identical in
  id/title/action. `user` differs only in the random profile UUID created in a fresh data directory;
  `network` differs only in ids (firmware keys) and the new `source` field. Network titles, order,
  actions and icons are unchanged, so the column looks the same while being generated.
- Network capture with `--render-clock 20 --background-clock 2026-10-25T10:00:00 --focus-time 2000
  --ui-clock 2026-10-25T12:32:00` and one RIGHT from the Game column:
  `out/native-reference/xmbml-network/network-col-final.png`, state sidecar
  `category=network, entryCount=5, entryId=manual`.

## Limits

- No RPCS3 Network-column capture exists locally, so this is not a pixel A/B against the console; the
  evidence is the definition, the resolved dump and the rendered column. Ranked item 8's "compared with
  an RPCS3 Network column" half is still open.
- English RCO text only. Another language would need `Assets::text`'s language selection, which the
  Settings projection does not have either.
- `sel://localhost/nopremo` (Remote Play visibility) is not evaluated, as recorded above.
- `seg_dlctrl`'s label is the plugin's RCO string, not an XMBML `title_rsc`; both plugin-built rows say
  `source=rco` for that reason.
- The five rows still do nothing on Cross (`Action::Unavailable`, the same honest refusal as before).
  The deliverable here is the column mechanism, not a Network feature.
- The row ids changed from the build's earlier `remote`/`search`/`downloads` to the firmware keys
  `premo`/`kensaku`/`dlctrl`; the theme-icon table was updated to match (`src/shell.cpp:774`) and nothing
  else in `src/` referenced the old ids.
- The register in `audit/FIRMWARE-PARITY-GAPS.md` was not edited (outside this change's write fence);
  M1 stays open until a second column and its page templates follow.

## Files touched

- `src/xmbml_view.h`, `src/xmbml_view.cpp` (new): the bounded, read-only XMBML column reader and
  `RowKind` classification.
- `src/shell.cpp`: `Shell::resolveNetworkColumn()` (`:142`), the constructor call (`:735`), theme-icon
  ids for the firmware keys (`:774`), `source` in the entry dump (`:610`).
- `src/shell.h`: `Entry::source`, `Shell::resolveNetworkColumn()`.
- `CMakeLists.txt`: the new source pair.
- `audit/XMBML-NETWORK.md`: this file.

Not touched: `view.cpp`/`view.h`, `main.cpp`, `settings_menu.cpp`, `preferences.cpp`, `catalog.cpp`
(used as-is), the harness, and anything under the firmware the app reads.

**Update (2026-10-27):** seg_dlctrl is no longer Unavailable - the row is classified as RowKind::DownloadControl from its ese://...type=download_control provider and maps to Action::SteamDownloads, which opens the Steam download queue page (see STEAM-DOWNLOADS.md section 10). The other four rows stay Unavailable for the reasons above.
