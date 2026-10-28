# Triangle options menu (item-level)

Status: implemented for one surface, 2026-10-26. See section 6 for the one open gate issue, which is
caused by a concurrent change outside this work.

## 1. What was missing

`audit/FIRMWARE-PARITY-GAPS.md` M3/B1 and top-ten item 4: Triangle on a selected item opened nothing.
The shell had an `Action` enum, `activate()` and `back()`, but no menu model, no per-item options and no
focus transfer, and `View::drawOptionsPanel` existed only for settings choice columns (a synthetic page
whose rows are all `SetSetting`).

Firmware behaviour taken as the target (manual evidence): Triangle opens and hides an options list; it
is a panel on the right, not a new column or page; the listed items vary by category (Play/Start/View,
Eject Disc, Delete, Copy, Information, Add to Playlist, Display All, Sort By, Group Content, Delete
Multiple, Copy Multiple); Circle closes.

## 2. First surface: a local folder page (Video Library)

Chosen because it is provable from the existing `--script` harness and because both a folder row and a
file row exist there:

- Data directory `out/native-reference/store/video-data` already points `library/video` at
  `out/native-reference/video-library` (Movies, Shows, Top Level Clip.mp4).
- The page is a real `Page` (`pages_.last().directory` non-empty), so its rows are `Action::Folder` or
  `Action::File` entries that carry a real path in `Entry::value`.
- Every command offered maps to code that already runs from Cross on the row.

The Game column (Steam Store / All Games) is the next surface; section 7 lists what changes there.

## 3. The options, and what each one does

The list is derived from the selected entry in `Shell::buildOptions()`, so every content surface gets
the same panel; the surface only decides which labels and rows are honest.

| Option | Shown when | What Cross does |
|---|---|---|
| `Open` / `Play` / `View` / `Start` | entry action is not `Unavailable` | Calls `Shell::activate()`: the entry's own action, exactly as Cross on the row. Label by kind: `Open` for Folder/Library/SteamFolder/JellyfinFolder/store rows, `Play` for File (video/music), Launch, LaunchSteam, LaunchApp, JellyfinPlay, `View` for a photo file and for Information rows, `Start` otherwise. |
| `Information` | always | Closes the panel and shows the existing information page (title, icon, two rules, `Key: value` rows) built from the entry: `Kind:` (Folder / Video / Music / Image / Game / Steam game / Settings / Unavailable / Item), `Detail:` when the entry has one, then `Path:` for a folder/file/library entry (`Value:` otherwise). |
| `Sort By` | only on a real directory page with 2+ rows | Opens a second level of the same panel: `Name (A-Z)`, `Name (Z-A)`, `Close`. Choosing a sort reorders the page's own entries (folders first, then titles, case-insensitive, stable) and keeps the previously selected row selected. It is the page list that changes, not a facade. |
| `Close` | always | Closes the panel (same as Circle when there is no submenu). |

Deliberately absent, and why:

- **Display All**: on this build a page already lists every row it has; nothing is grouped, filtered or
  hidden, so Display All would be a no-op and is not offered.
- **Delete, Copy, Delete Multiple, Copy Multiple, Group Content, Add to Playlist, Eject Disc**: no safe
  native implementation exists (delete/copy would touch the user's media files; the disc and playlist
  surfaces do not exist). Not shown rather than shown-and-inert.
- **Play/Start on an `Unavailable` row**: omitted, so a row the shell cannot act on only offers
  Information and Close.

## 4. Input handling

While `Shell::optionsOpen()` is true the panel owns input; the XMB behind it receives nothing:

- Keyboard: Up/Down move inside the panel; Return/Enter is Cross; Escape/Backspace is Circle (step back
  from a submenu, then close); `T` is Triangle (hide the whole menu). `T` is the Triangle binding added
  for the XMB: `R` was already Repeat on media and `Q`/`E` were photo rotate, and no key opened the
  item menu before this change. `TRIANGLE` is the `--script` token for it (`Qt::Key_T`).
- Controller: D-pad Up/Down move, Cross runs, Circle steps back/closes, Triangle dismisses. In normal
  navigation Triangle (`XINPUT_GAMEPAD_Y`) opens the panel except during media playback, where Triangle
  keeps its repeat function. The controller path is coded but was not exercised: no pad is attached to
  this machine, and `work/ab-layers.ts` has no controller injection.
- Gating: `keyPressEvent` handles the panel before the arrow/D-pad paths, and `pollController` returns
  from its own panel block before the normal navigation block, so arrows/Cross/Circle/Triangle cannot
  also move the XMB. A LEFT while the panel was open was captured to prove it (section 6, state still
  `category=video depth=1 selection=0`).
- The panel cannot open while a message/information page, media, the PS menu or a folder scan is up, and
  not on a settings choice column (that column already is the options panel).

## 5. Rendering

`View::drawOptionsPanel` was generalised instead of duplicated: it now takes a list of titles, a focus
position, an alpha, a slide amount and an optional heading. The settings choice columns pass their
`Column` titles/position exactly as before, and the item menu passes `Shell::options()` and the
`optionsPanelEase_`/`optionsOpenEase_` paf curves.

- Same visual language as the settings panel: translucent white strip from x 1330 to the right edge, a
  bright left edge, options at 23 px on a 40 px pitch centred on y 505, the focused option glows.
- The item panel slides in from the right over 200 ms (paf accel mode 5, the same curve as the focus
  moves) and moves between rows on the same curve. A submenu draws its name (`Sort By`) above the list.
- The panel is drawn only while `Shell::optionsOpen()`; it is not pushed onto `pages_`, so it is not a
  level, `depthPosition_` is untouched, and the page under it keeps its layout.
- Existing named states are unaffected because none of them opens the panel; the UI signature only
  gains the panel term while it is open or still sliding.

## 6. Verification

Build (run after the last source edit; `re-ps3-native.exe` 2026-10-26 21:24:40, later than every edited
source file):

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-options
```

Capture executable: `C:/dev/out/native-options/re-ps3-native.exe`, flash `C:/dev/rpcs3/bin/dev_flash`,
data directory a copy of `out/native-reference/store/video-data` (preferences point `library/video` at
`out/native-reference/video-library`).

### 6.1 Open, move down, Information, close

Script (exact):

```
full,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,RIGHT,RIGHT,RIGHT,RIGHT,wait,wait,ENTER,wait,wait,wait,wait,wait,TRIANGLE,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-open.png,DOWN,wait,ENTER,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-info.png,wait,ESC,wait,wait,grab=C:/dev/xmb/out/native-reference/options/options-closed.png,quit
```

Log (`XMB_LOG`), in order:

```
shell: options opened category= video depth= 1 entry= C:/dev/xmb/out/native-reference/video-library/Movies rows= 4 Open|Information|Sort By|Close
script: grab .../options-open.png QSize(1920, 1080) saved
shell: options selection 1 Information
shell: options information title= Movies rows= 2
script: grab .../options-info.png QSize(1920, 1080) saved
script: grab .../options-closed.png QSize(1920, 1080) saved
```

Captures (local, not committed):

- `out/native-reference/options/options-open.png`: folder page with the panel on the right,
  `Open / Information / Sort By / Close`, glow on Open.
- `out/native-reference/options/options-info.png`: information page: Movies icon/title, rules,
  `Kind  Folder`, `Path  C:\dev\xmb\out\native-reference\video-library\Movies`, `Back` hint.
- `out/native-reference/options/options-closed.png`: the folder page again, no panel.

### 6.2 Input isolation, submenu, back, sort applied, Triangle closes

Script (exact):

```
full,wait,wait,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,wait,wait,RIGHT,RIGHT,RIGHT,RIGHT,wait,wait,ENTER,wait,wait,wait,wait,wait,TRIANGLE,wait,LEFT,wait,grab=C:/dev/xmb/out/native-reference/options/panel-after-left.png,DOWN,DOWN,wait,ENTER,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/options/sort-menu.png,ESC,wait,wait,grab=C:/dev/xmb/out/native-reference/options/back-to-items.png,DOWN,DOWN,wait,ENTER,wait,wait,DOWN,wait,ENTER,wait,wait,grab=C:/dev/xmb/out/native-reference/options/sorted.png,TRIANGLE,wait,wait,TRIANGLE,wait,grab=C:/dev/xmb/out/native-reference/options/closed-triangle.png,quit
```

Log:

```
shell: options opened category= video depth= 1 entry= .../Movies rows= 4 Open|Information|Sort By|Close
(grab panel-after-left.png: state category=video depth=1 selection=0 -> LEFT did not reach the XMB)
shell: options selection 1 Information
shell: options selection 2 Sort By
shell: options sort menu opened
(grab sort-menu.png)
shell: options back to item list
(grab back-to-items.png)
shell: options selection 1 Information
shell: options selection 2 Sort By
shell: options sort menu opened
shell: options selection 1 Name (Z-A)
shell: options sorted page video-library Z-A 3 rows
(grab sorted.png: rows now Shows, Movies, Top Level Clip.mp4; state selection=1 keeps Movies)
shell: options opened ... rows= 4 ...
shell: options closed
(grab closed-triangle.png: no panel)
```

`Sort By` is therefore a real action on the page's entries, and Circle from the submenu returns to the
item list (the panel stays open) while Triangle closes everything.

### 6.3 Play on a video file

Script tail: open the folder page, `DOWN,DOWN,TRIANGLE`, grab, `ENTER`, grab. Log:

```
shell: options opened category= video depth= 1 entry= .../Top Level Clip.mp4 rows= 4 Play|Information|Sort By|Close
shell: options activate Play entry= .../Top Level Clip.mp4
(script: state ... mediaOpen:true, mediaPaused:false, mediaDuration:20000, mediaPosition:1322)
```

Cross on the `Play` row ran `Shell::activate()`, i.e. the same media path Cross on the row uses.

### 6.4 Settings choice panel regression

Script: `LEFT` x5 to Settings, six `DOWN` to Theme Settings, `ENTER`, `ENTER` on Theme. The existing
choice panel still renders in the same place with the same style (`settings-panel.png`, one row
`Original`), so the `drawOptionsPanel` generalisation kept that path intact.

## 7. Gate result

Mandated command:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-options/re-ps3-native.exe
```

Final result on the build above (`re-ps3-native.exe` 2026-10-26 21:24:40, later than every edited source):

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---:|---:|---:|---|
| frame | 17.95 | 18.02 | -0.07 | .8286 | .8301 | -.0015 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget`. No harness file, reference or threshold was edited by this work.

Two notes on that number, for honesty:

- The clock and info-bar capture baselines in `work/ab-layers.thresholds.json` were refreshed to the
  American clock format (MM/DD, 12-hour) by the clock/info-bar work, per that file's revisionNote; the
  pinned 12:32 renders as `10/25 12:32 PM` in the gate log. The panel is not involved: it draws nothing
  in the idle Game-root state, which is what this capture is.
- An intermediate run of the same command, taken before that baseline refresh landed, failed only
  `clock text` (11.39 vs the then-current 13.51) and `info bar` (14.78 vs 17.12) because of exactly that
  format change. The same capture with preferences pinning the recorded DD/MM + 24-hour format scored
  13.61/.6226 clock and 17.21/.7114 info bar, matching the revisionNote's stated reproduction as well.
  That history is noted so a reader with the old thresholds knows why the numbers moved.

## 8. Extending the panel to other surfaces

The panel is surface-agnostic. To add options for the next surface (Game column's All Games page,
Jellyfin, Steam Store sections):

1. Add a command to `OptionCommand` (`src/shell.h`) and a row in `Shell::buildOptions()` under the
   condition that makes it honest (e.g. `!pages_.last().directory.isEmpty()` already gates Sort By).
2. Handle the command in `Shell::optionsActivate()`. Panel navigation, drawing, slide-in, input gating,
   logging, the `TRIANGLE` script token and the sound cues are already shared.
3. If a new option needs a second level, follow `OptionCommand::SortMenu`: push the current list onto
   `optionsParent_`, set `optionsNested_` and `optionsTitle_`, and Circle will step back.

Known differences by surface today:

- **Steam folders** (All Games, Not Installed, collections) have no `Page::directory`, so they get
  `Open` (SteamFolder rows) or `Play` (game rows), `Information` and `Close`, but no `Sort By`. If a
  sort is wanted there it should sort `steamFolderEntries()` and is a separate, explicit change.
- **Game column root** rows are folders/store rows, so the panel there is `Open`/`Information`/`Close`.
- **Display All** stays out until a surface actually groups or hides rows.

## 9. Files touched

- `native-xmb/src/shell.h`: `OptionCommand`, `MenuOption`, public panel API, private state and helpers.
- `native-xmb/src/shell.cpp`: `toggleOptions`, `optionsMove`, `optionsActivate`, `optionsClose`,
  `dismissOptions`, `buildOptions`, `activateLabel`, `entryKind`, `showEntryInformation`,
  `sortPageByName`; all log through `qInfo()` (`shell: options ...`).
- `native-xmb/src/view.h`: `optionsPanelEase_`, `optionsOpenEase_`, `optionsWasOpen_`.
- `native-xmb/src/view.cpp`: generalised `drawOptionsPanel`, item panel draw (open only), paf curves and
  UI signature term, keyboard and controller blocks, `T` binding.
- `native-xmb/src/main.cpp`: `TRIANGLE` script token (`Qt::Key_T`).
- `native-xmb/audit/OPTIONS-MENU.md`: this document.

Not touched: `settings_menu.cpp`, `preferences.cpp`, `INFO-BAR.md` (another agent's fence), and the
verification harness.

**Note (2026-10-27):** Continue Watching is now the first row of the Video column, so the repro scripts that say 9 LEFT, 4 RIGHT, ENTER reach Video Library only with one extra DOWN before ENTER. Those runs were captured before the row existed; the evidence in this file is otherwise unchanged.

---

## 10. Phase 3.2 design - Display All, Copy/Move, Delete

Status: **design only**, 2026-10-27. No code from this section is in the tree: the panel lives in
`shell.cpp`, which the Phase 1 (Jellyfin) session owns during this pass. Everything below is written so
it can be applied as a hunk; the parts that touch `shell.h` are listed as declarations in 10.5.

Where this section disagrees with section 3's "deliberately absent" bullets, this section wins:
Display All becomes a *conditional* row (never present on today's pages), and Copy/Move/Delete become
real actions on the configured local libraries. Everything else in that list stays absent (10.7).

### 10.1 Shape: the panel, not a modal, not a new surface

The firmware shows Delete's confirmation as a dialog. This build has no modal widget by design (a
modal would stop `pollController` and the XMB behind it), so the confirmation is a **second level of
the existing options panel** - the same mechanism `Sort By` already uses: push the current list onto
`optionsParent_`, set `optionsNested_` and `optionsTitle_`, and Circle steps back to the item list.
Nothing in `view.cpp` changes; the panel already draws a heading and a note line.

The transfer itself needs a surface that survives the panel closing and blocks a second transfer. It
gets a **page** (`Page::transfer`), like the Jellyfin sign-in and the download queue: rows are
ordinary `Entry`s, Cross/Circle work through the normal path, and the capture sidecar can assert on
`pageKind() == "transfer"`. The page repaints from a 100 ms `QTimer` that reads the transfer state; no
signal flood from the worker.

### 10.2 Display All

The firmware's `Display All` toggles a list between "grouped" and "every row". It is honest only when
something is actually grouped or hidden. This build never groups or hides rows on a folder page: a
page is the folder's entries plus nothing else (`openDirectory()` -> `MediaLibrary::scan()` ->
`Page::entries`).

Design: **no Display All row on a folder page today.** The row is added only when a page carries a
group/filter state, and then it does exactly one thing: rebuild `Page::entries` from the unfiltered
scan (`refreshDirectory()` with a cleared filter) and put the previously selected row back under the
cursor. The rule for the row:

```
shown only when pages_.last().filtered is true     (new Page field, default false)
label "Display All"
Cross -> pages_.last().filtered = false; refreshDirectory(); restore selection by id
```

Until a surface sets `filtered` (a future Group Content / by-collection view), the row must not be
offered: a no-op menu entry is worse than an absent one, and the section 3 note already says why.

### 10.3 Copy and Move

**Scope (the whole honesty of these two rows).** Both act on rows of a page whose `Page::directory` is
non-empty and whose canonical path is inside one of the three configured media libraries
(`library/photo`, `library/music`, `library/video`). Nothing else can carry them:

- a Jellyfin page, Steam folder, Steam Store page, download page and Continue Watching page have no
  `Page::directory`, so they inherit no Copy/Move/Delete row;
- a row whose file has gone since the page was scanned (a stale page) is refused by the pre-flight
  below, not attempted;
- a path outside the libraries (a hand-edited library entry, a symlink pointing out) is refused.

Copy copies a file or a folder; Move renames in place when source and destination are on the same
volume and falls back to copy-then-delete when they are not. A folder copy is recursive with the
bounds in 10.6; a folder move on one volume is one `QFile::rename`.

**Destination.** After Cross on `Copy`/`Move`, the panel becomes a destination list (`optionsNested_`,
heading `Copy "<name>" to` / `Move "<name>" to`):

| row | destination |
|---|---|
| `<parent folder name>` | the page's own parent folder (the "here" option; refuses when the row is already in it) |
| `Photo Library` / `Music Library` / `Video Library` | each *configured* library root for this column's media kind, labelled with its folder name |
| `Choose Folder...` | the existing native folder dialog (`emit requestLibrary()`, main.cpp's QFileDialog), then start |
| `Cancel` | closes the submenu |

A destination is refused (disabled row with `note`) when: it does not exist; it is the source; it is
inside the source; the destination path already exists; free space is below the plan's total + 5%.

**Transfer page.** `Page{title = "Copying \"<name>\"", transfer = true, entries = {progress, current, cancel}}`:

```
row 0  title "42%  of 1.2 GB"          (updated by the timer)
row 1  title "<current file name>"     (detail = destination folder)
row 2  title "Cancel"                  (Cross sets the cancel flag; row 1 shows "Cancelling...")
```

The worker is `QtConcurrent::run` (Qt6::Concurrent is already linked and used by `MediaLibrary`) over a
plan built on the UI thread. It copies in 1 MiB blocks so Cancel is honoured inside a large file, not
only between files. Progress is published through atomics (`bytesDone`, `filesDone`, `filesTotal`,
`currentFile`) that the timer reads; the page's title is regenerated from them and `touch()` repaints.

On completion the page closes (pop), `refreshDirectory()` runs, and a notification reports
`Copied <name>` / `Moved <name>` with the file count. On failure the page closes with the panel's
`message_`-style information page naming the step and the path that failed, and the destination tree
the operation created is removed (10.6 rule 8).

On cancel: the worker stops at the next 1 MiB boundary, removes the destination file it was writing,
removes the destination tree it created, then the page closes with `Copy cancelled` and a log line
listing what was removed. A move never deletes a source file before its destination copy exists and
matches in size, so a cancelled move leaves the source complete.

### 10.4 Delete

Delete is offered on the same scope as Copy/Move (10.3), and refuses rather than guesses:

- a **file**: deleted with `QFile::remove()` after the confirmation;
- a **folder**: deleted only when it is empty (`QDir::removeRecursively()` on an empty directory, or
  `QDir::rmdir`), otherwise the row is listed disabled with the note "This folder is not empty." - no
  recursive delete from a couch remote in this pass (10.7);
- a row with no path, or a path outside the libraries: no Delete row at all.

Confirmation is the panel's second level (10.1):

```
heading  Delete "<name>"?
row 0    Delete
row 1    Cancel
note     "This deletes the file from the disk. It cannot be undone."
```

Cross on `Delete` closes the panel, performs the removal, clears the file's resume point
(`clearResumePosition(path)` so Continue Watching cannot point at a ghost), rescans the page, and
notifies `Deleted <name>`. A failure keeps the row, reports the OS reason (`QFile::errorString`) and
does not rescan. Cancel returns to the item list, then Circle closes.

### 10.5 Data model and state (all in `shell.h` / `shell.cpp`)

`shell.h`:

```cpp
enum class OptionCommand { /* existing */ ...,
    // A folder page that hides nothing gets no Display All row; the command exists for the page that
    // eventually groups rows (10.2). Copy/Move/Delete are per-row; the *Confirm/Sort* pattern is reused.
    DisplayAll, CopyMenu, MoveMenu, DeleteMenu,
    CopyTo, MoveTo,           // rows on the destination submenu; `MenuOption::value` = destination path
    CopyCancel,               // the transfer page's Cancel row
    DeleteConfirm };

struct MenuOption { /* existing fields */ QString value; };  // one added field: the destination path

enum class TransferMode { Copy, Move };
struct TransferPlan {
    QString source, destination;                 // absolute, canonical, both inside a library
    QVector<QPair<QString,QString>> files;       // source -> destination, in walk order
    quint64 totalBytes = 0;
    TransferMode mode = TransferMode::Copy;
    bool sameVolume = false;
};
struct TransferState {
    TransferPlan plan;
    quint64 bytesDone = 0;
    int filesDone = 0, filesFailed = 0;
    QString currentFile, error;
    bool running = false, cancelled = false, move = false;
    std::shared_ptr<std::atomic_bool> cancel;    // the worker's only way to be stopped
};
```

`shell.h` private additions:

```cpp
    // ---- Options: list actions (Phase 3.2, audit/OPTIONS-MENU.md section 10) ----
    void buildCopyMoveMenu(bool move);      // the destination submenu
    void startTransfer(const QString& destination, bool move);
    void cancelTransfer();
    bool transferPageOpen() const;
    void refreshTransferPage();             // the 100 ms timer body
    bool libraryScopeFor(const QString& path, QString* root) const;  // 10.6 rule 1
    bool planTransfer(const QString& source, const QString& destination, bool move,
                      TransferPlan& plan, QString& reason) const;
    void finishTransfer(bool ok);
    void deleteEntry(const Entry& entry, bool confirmed);
    Page* transferPage_ = nullptr;          // points into pages_ while the page is up
    TransferState transfer_;
    QTimer transferTimer_;
```

`Page` gains `bool transfer = false;` and `bool filtered = false;` (the latter reserved for 10.2).

No new signals. `pageKind()` gains `if (page.transfer) return "transfer";` so the capture sidecar can
assert the transfer state without reading pixels.

### 10.6 Writes-on-disk safety rules

These are the rules the implementation must satisfy, in the order they are checked. They are the
reason several firmware rows stay absent rather than being approximated.

1. **Scope by configuration.** A path is actionable only when `QFileInfo(path).canonicalFilePath()`
   is a strict prefix-match of one of the configured `library/<kind>` roots (compared after
   `QDir::cleanPath`, case-insensitively on Windows, with a separator boundary so `C:\media2` is not
   inside `C:\media`). A stale page or a hand-edited library therefore cannot widen the scope.
2. **Never follow a link.** Refuse a source or destination that is a symlink or a Windows reparse
   point (`FILE_ATTRIBUTE_REPARSE_POINT`), and refuse to delete a directory that is a mount point
   (`QStorageInfo::root()`). Links are how a "copy" turns into a write outside the library.
3. **Never overwrite.** The destination must not exist for a copy or a move. There is no merge and no
   replace in this pass; the firmware's own copy dialog also refuses an existing target.
4. **Never write outside the libraries.** The destination must satisfy rule 1 as well - Copy/Move is
   a library-to-library action, not a file manager.
5. **Refuse degenerate pairs.** source == destination, destination inside source (folder), source
   inside destination.
6. **Bound before writing.** At most 5000 files, at most 8 levels below the source, a known total size
   (every file's size readable), and `QStorageInfo(destination).bytesAvailable()` at least the total
   plus 5%. A plan that fails a bound is refused before anything is created.
7. **The source is sacred until the destination is complete.** A copy writes only the destination. A
   move renames on one volume; across volumes it copies each file, verifies the byte size, and only
   then removes that source file. The source tree is removed bottom-up, empty-directory only.
8. **Cancel removes what this operation created.** The destination file in flight and the destination
   tree are removed; the source is untouched. A cleanup that fails is logged with the exact path, so
   an orphan is never silent.
9. **No recursive delete** (10.4): an empty folder only. Deleting a media tree from a TV remote needs a
   stronger confirmation model than the panel has today, and a mistake is unrecoverable.
10. **State follows the disk.** After any successful write: `refreshDirectory()`, and for a delete or a
    move-away, `clearResumePosition(source)` so Continue Watching cannot offer a file that is gone.
11. **Only Qt file APIs, no shell.** `QFile`, `QDir`, `QFileInfo`, `QStorageInfo`; no `cmd`, no
    `system()`, no string-built command line. Paths are logged; file contents never are.
12. **One transfer at a time.** `transfer_.running` guards the entry point; a second Copy/Move while
    one runs is refused with "A transfer is already running."

### 10.7 Firmware rows that must stay absent

| row | why it is not offered |
|---|---|
| `Display All` | nothing on a folder page is grouped or hidden (10.2); it returns only with a grouping view |
| `Copy Multiple`, `Delete Multiple` | there is no multi-select model in the XMB (one focused row), and inventing one is a new input surface |
| `Group Content` | no groups exist; the firmware's grouping is a plugin feature with no native store |
| `Add to Playlist` | no playlist store exists (the definition's playlist views carry no titles and this build has no playlist index) |
| `Eject Disc` | no optical drive integration, and none is planned (PHASES section 6.5) |
| `Copy to USB Device` / device rows | no removable-device enumeration; the destination list is limited to configured library folders and the native folder dialog |
| `Rename` / `Properties` | the firmware's item actions do not include them; `Information` already shows the path and kind |
| `Upload` (photo) / `Register` / `Publish` | no service backend; the Network column's honesty rule applies |

Each absent row is documented here rather than rendered inert, because a disabled menu row that can
never become enabled is noise. (The panel does list disabled rows where the reason is *temporary* -
e.g. "This folder is not empty" - which is a different case.)

### 10.8 Verification plan for the hunk (not run - no code)

Fixture: a scratch copy of the existing video library (`out/native-reference/video-library`) with
`Movies`, `Shows`, `Top Level Clip.mp4`, plus a 200 MB synthetic file so Cancel is observable.

| check | script | assertion |
|---|---|---|
| panel on a file row | open Video Library, Triangle | rows `Play | Information | Sort By | Copy | Move | Delete | Close` |
| no rows off a library page | Game column root, Triangle | no Copy/Move/Delete |
| delete confirms, cancels | Triangle, Delete, Cancel | the panel returns to the item list; file still on disk |
| delete removes | Triangle, Delete, Delete | file gone, page rescanned, notification, resume point cleared |
| copy same volume | Triangle, Copy, choose parent/library | file at destination, source intact, notification, page rescanned |
| copy cancel | Triangle, Copy, start, Cancel after 10% | destination tree removed, source intact, log says what was removed |
| move across volumes | Triangle, Move, other library on another drive | source gone only after the copy completes; byte sizes equal |
| destination exists | copy onto an existing name | row disabled with the reason; nothing written |
| outside scope | a page whose library points at a junction | no Copy/Move/Delete rows; log line names the refusal |
| one at a time | start a copy, Triangle again | "A transfer is already running." |

Named states to add afterwards (Phase 5.1): `transfer-running`, `transfer-cancelled`,
`delete-confirm`, with `pageKind` assertions and the existing PSNR self-baseline.

### 10.9 Files this design would touch

- `native-xmb/src/shell.h`: `OptionCommand` additions, `MenuOption::value`, `TransferMode`,
  `TransferPlan`, `TransferState`, `Page::transfer`/`Page::filtered`, the private helpers and the timer.
- `native-xmb/src/shell.cpp`: `buildOptions` (the new rows under their conditions),
  `optionsActivate` (the new commands), `buildCopyMoveMenu`, `startTransfer`, `cancelTransfer`,
  `refreshTransferPage`, `finishTransfer`, `planTransfer`, `libraryScopeFor`, `deleteEntry`,
  `pageKind`/`valueText`/`subtitleText` for the transfer page, and the pre-flight/safety helpers.
- `native-xmb/audit/OPTIONS-MENU.md`: this section.

Not touched: `view.cpp`/`view.h` (the panel already draws headings and notes; the transfer page is an
ordinary page), `main.cpp` (the existing `requestLibrary` dialog is the folder picker), the harness,
`preferences.cpp` (a delete clears a resume point through the existing public API), and
`CMakeLists.txt` (Qt6::Concurrent is already linked).
