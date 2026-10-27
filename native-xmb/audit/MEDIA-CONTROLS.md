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
## Resume points (2026-10-26)

Leaving playback with Back stores the position in the `media/resume` preferences string (a bounded JSON map
of file id -> milliseconds, `src/shell.cpp`); reopening the same file seeks there on `LoadedMedia` and shows
"Resuming at m:ss". A file that reaches end of media is removed from the map, so a finished title starts from
the beginning. Positions within ten seconds of the start or fifteen of the end are not stored on long media;
clips of a minute or less use a tenth in from each end instead.

Verified with the 20 s clip `Native Direct.mp4`:
- play -> Back at 8.8 s -> `preferences.json` `media/resume` = `{"27125549437132b89122bd51":8768}`;
- reopen -> log `media: resumed at 8768 ms`, position advancing from there;
- reaching end of media -> the map returns to `{}`.

## Continue Watching (2026-10-26)

Status: implemented (task T5 of `audit/HTPC-IDEAS.md`). This section supersedes the *storage format* described
above and keeps its *behaviour*: the thresholds, the seek on `LoadedMedia` and the clearing at end of media are
unchanged, and the round trip above still measures the same. Written after the last source edit; every number
below came from a run of `C:/dev/out/native-continue/re-ps3-native.exe` (2026-10-26 23:57), the 20 s clip
`C:/dev/xmb/out/native-reference/video-library/Top Level Clip.mp4`, a data directory that is a copy of
`out/native-reference/store/video-data`, and the pinned clocks
`--render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-26T21:50:00 --focus-time 2000`.
Captures, logs, data directories and the run script are under `out/native-continue/` and are not committed.

### 1. The record, and why the old one had to change

The old value knew a number and a hash: `{"<id>": milliseconds}`. Nothing could be **listed** from it, because
it stored neither the path nor a title, and the id is a one-way hash of the path. The new value (same
`media/resume` key, one string) is a map of records:

```json
{"59e69a3e0a2a2c78651d8ab2":{"at":1793077100,"dur":20000,"ms":10709,
  "path":"C:/dev/xmb/out/native-reference/video-library/Top Level Clip.mp4",
  "title":"Top Level Clip.mp4"}}
```

| field | meaning | bound enforced by `Preferences::valid` |
|---|---|---|
| the key | first 24 lower-case hex characters of the SHA-1 of the path - the same algorithm and truncation the old map used, which is what makes an old entry matchable | 16-40 hex characters accepted on load |
| `path` | the file the position belongs to, forward slashes, exactly as the media scan writes it | 512 characters |
| `ms` | position in milliseconds | 1 .. 86,400,000 |
| `dur` | duration in milliseconds, written only when the player reported one | 1 .. 86,400,000 |
| `title` | the row's title when it was remembered | 200 characters |
| `at` | unix seconds of the last update | up to 4,102,444,800 (2100-01-01) |

`dur` is the one field beyond the four the task named. It is here because the page is asked to print
"position/duration when duration is known", and a record without it could only ever print the position. It is
optional: a migrated record has no `dur` until that file plays again, and its row reads `Resuming at 00:09`
instead of `Resuming at 00:09 of 00:20 (00:11 left)`.

`Preferences::valid` (`src/preferences.cpp`) now validates the **shape**, not just the string length: the
document must be an object of at most 64 entries, every key must be hex of that length, every value must be
either a record (with only the five known fields, each of its own type and bound, `path` and `title` clean of
anything longer than their limits) or - for an upgraded data directory - the old bare millisecond count. Both
shapes must stay valid, because a preferences load keeps only the values that validate: rejecting the old shape
would throw the user's resume points away before the migration could resolve them. The bounds are chosen so 64
records fit inside the 65,536-byte string bound (64 x (512 + 200 + ~80)).

### 2. Migration rules, and the dropped entries

`Shell::migrateResumePoints()` runs once, at load (the `Shell` constructor, before any page can ask):

1. A numeric value is the old shape. It becomes a record with **no path** (`{"ms":...,"at":...}`), marked
   pathless. The time it is given is the migration time, because the old format carried none and an invented
   earlier time would be a history this build does not have.
2. Every pathless record is **resolved** by hashing the playable files under the *configured* media libraries
   (`library/video`, `library/music`, `library/photo`) with the same truncation the writer uses, so a match is
   exact and never a guess. Nothing else on disk is read. The walk is bounded: 5,000 files, 12 directory
   levels, a visited set so a directory link cannot loop, and it stops as soon as every wanted id is found.
3. A resolved id keeps its key and its milliseconds and gains `path` (and `title` from the file name when the
   record has none). An unresolved id is **dropped**, and the log names the id, the position and the reason.
4. The map is re-bounded to 64 and written back only if something changed. A data directory already in the
   new shape is not rewritten at all (the function returns before touching the file).

Dropped-entry evidence. **Run M** starts from the exact bytes the earlier build stored, plus one id that
matches nothing - `data-m` is a copy of `out/native-reference/store/resume-data` (`library/video` points at
`out/native-reference/resume-media`, which holds `Native Direct.mp4`, one file):

```
--- preferences.json before the run ("media/resume")
{"27125549437132b89122bd51":8768,"deadbeefdeadbeefdeadbeef":12345}

--- log, at load
continue: resolving 2 pathless resume point(s) against 1 media library folder(s); 1 file(s) hashed in 0 ms
continue: migrated pathless resume point 27125549437132b89122bd51 (8768 ms) -> C:\dev\xmb\out\native-reference\resume-media\Native Direct.mp4
continue: dropped pathless resume point deadbeefdeadbeefdeadbeef (12345 ms): 1 file(s) under 1 configured media library folder(s) were hashed and none matched
continue: resume points migrated - 1 resolved, 1 dropped, 1 remembered (2 of them came from the old format)

--- preferences.json after (the same run; dur 20000 arrived from the replay below)
{"27125549437132b89122bd51":{"at":1793077282,"dur":20000,"ms":10026,
  "path":"C:/dev/xmb/out/native-reference/resume-media/Native Direct.mp4","title":"Native Direct"}}
```

The resolved record is not merely written: the page lists it and Cross resumes it. The same run, one step later:
`continue watching: page opened with 1 row(s)`, sidecar `entryId: continue:27125549437132b89122bd51`,
`entryTitle: Native Direct`, then `media: resumed at 8768 ms` and `mediaPosition: 9728` in the next capture -
the old entry's own 8,768 ms, carried across the format change.

**Run N** is the same old map with **no media library configured at all**, which is the second drop reason:

```
continue: dropped pathless resume point 27125549437132b89122bd51 (8768 ms): no media library is configured, so the file behind this id could not be found
continue: resume points migrated - 0 resolved, 1 dropped, 0 remembered (1 of them came from the old format)
```

So every legacy entry ends in one of two states: a real, listable row, or removed with a logged reason. A third
branch drops a value that is neither a record nor a number; `Preferences::valid` rejects such a value before
the shell ever sees it, so that branch is a guard rather than an observed path (Run X, section 6).

### 3. Where the surface lives, and why

**The Video column, as its first row**: `Continue Watching | Video Library | Video Settings | Choose Media
Folder` (`v1-video-column.png`, sidecar `category: video, depth: 0, entryCount: 4, entryId: continue`).

- It is a plain `Entry` in the Video category's own list (`categories_[4].entries`), exactly like the three rows
  it joins. No new category, no new page kind, and **no new `Action` member**: the row reuses
  `Action::Library`, which already means "open a collection of this column's own content" and is labelled
  `Open` by `activateLabel()`; `Shell::activate()` recognises this one row *by id* and builds the page from the
  resume map instead of reading a folder. Dispatch by row id is what the Download Management rows already do
  (`download:`), so the action enum, the label table, the kind table and the information page needed no new
  member for it.
- Rejected alternative: a header row on the Video Library page. That page's rows only exist after a directory
  scan - `MediaLibrary::loaded` clears `page.entries` and rebuilds them - so a header would need a new page kind
  and would be wiped by every F5.
- First row, because it is the surface a couch user wants first, and because it is the one row of the column
  that works before any library folder has been chosen.
- **One consequence, stated rather than hidden:** the column's default row is now Continue Watching, so a
  script that navigates `4 RIGHT, ENTER` lands on it and the Video Library needs one extra `DOWN`. The repro
  scripts quoted in `audit/SUBTITLES.md` and `audit/OPTIONS-MENU.md` therefore need that one `DOWN`. No gate,
  named state, threshold or reference changed (section 7).

### 4. The page

- **Rows**: newest first (`at` descending), at most **20**, and nothing whose `at` is older than **30 days**. A
  record with no path is never listed. Age-hidden entries are **hidden, not deleted**: the stored map keeps its
  own 64-entry bound, so an entry that falls off the page survives until the cap evicts it and replaying the
  title refreshes it. Neither limit is a preference; both are constants in `src/shell.cpp`.
- **Row content**: the stored title (or the file's base name), the same `video_tex_album_default` glyph the
  library's file rows use, and one small line under the title:
  `Resuming at 00:10 of 00:20 (00:10 left)`, or `Resuming at 00:10` when no duration was recorded.
- **A stale row** (the path no longer exists) is listed **disabled**: `Entry::disabled` dims its icon, its title
  and its focus glow (`src/view.cpp`). Every other row leaves the flag false, so the three terms multiply by
  exactly 1 and the rest of the frame is unchanged - the named-state gate still reads PSNR infinity / SSIM
  1.0000 (section 7). Its line reads `Missing - this file is not on disk any more. Triangle removes it.`
- **Empty**: the message page `Continue Watching / Nothing to continue.`, the same surface Download Management
  uses for its empty state. It is reached two ways and both are **bit-identical** (SHA-256 `85c2323004f9...`):
  opened with nothing remembered (`a1-empty.png`) and re-shown when the last row is removed
  (`b4-empty-after-remove.png`).
- F5 rebuilds the page in place (`refreshDirectory`), so a file that has gone or come back is re-read.

### 5. Actions

| input | where | what happens |
|---|---|---|
| Cross on a row | `activate()` -> `openFile()` | the same media path Cross on a library row uses: the page's rows become the playback queue and the saved position is applied on `LoadedMedia` |
| Triangle | `buildOptions()` | `Play | Information | Remove from Continue Watching | Close`; the label is `Play` because the row is `Action::File` |
| Triangle on a stale row | `buildOptions()` | `Information | Remove from Continue Watching | Close` - no `Play`, because the shell cannot play it. Cross on the row itself reports the reason rather than the generic "not available in the native recreation" line |
| Cross on `Remove from Continue Watching` | `optionsActivate()` -> `removeContinueEntry()` | clears that record from `media/resume`, rebuilds the page in place, and closes it with `Nothing to continue.` when the last row goes |
| Cross on `Information` | `showEntryInformation()` | its own page: `Kind`, `Path`, `Position`, `Duration`, `Recorded` (date and time of `at`), `Remembered as` (the id, so a reader can find it in `preferences.json`), and `State` (`the file is on disk and resumes at this position` / `the file is no longer on disk; Triangle removes this row`) |

`Remove from Continue Watching` is offered only on the Continue Watching page (the row id is the gate), because
nothing else in the XMB owns that state.

### 6. Evidence

**Run A** - the surface before anything is remembered, then play and Back (`data-a`):

```
continue watching: nothing to continue                       (a1-empty.png: "Nothing to continue.")
media: directory result 3 entries; truncated false error false
continue: stored 10709 ms of 20000 ms for Top Level Clip.mp4  (a2-playing.png: mediaPosition 10416 / 20000)
```

`preferences.json` before that run has **no** `media/resume` key; after it:

```
"media/resume": "{\"59e69a3e0a2a2c78651d8ab2\":{\"at\":1793077100,\"dur\":20000,\"ms\":10709,
  \"path\":\"C:/dev/xmb/out/native-reference/video-library/Top Level Clip.mp4\",\"title\":\"Top Level Clip.mp4\"}}"
```

**Run B** - the page, Cross, the panel, Remove, the empty state (same data directory):

```
continue watching: page opened with 1 row(s)
   b1-page.png     depth 1, entryCount 1, entryId continue:59e69a3e0a2a2c78651d8ab2, title "Top Level Clip.mp4"
media: resumed at 10709 ms
   b2-resumed.png  mediaPosition 11733 / 20000 (the clip's own timecode reads 00:00:11.7)
continue: stored 12074 ms of 20000 ms for Top Level Clip.mp4   (Back after the replay)
shell: options opened category= video depth= 1 entry= continue:59e69a3e0a2a2c78651d8ab2 rows= 4 Play|Information|Remove from Continue Watching|Close
shell: options selection 2 Remove from Continue Watching
continue: removed Top Level Clip.mp4 (C:\dev\xmb\out\native-reference\video-library\Top Level Clip.mp4) from Continue Watching; 0 still remembered
continue watching: the list is empty; the page closed
   b4-empty-after-remove.png  depth 0, entryId continue - "Nothing to continue."
```

`media/resume` after Run B is `{}`.

**Run D** - `Information` on a remembered row: `shell: options information title= Top Level Clip.mp4 rows= 7
(continue watching)`, and `d1-information.png` shows `Kind Video`, the full path, `Position 00:10`,
`Duration 00:20`, `Recorded 2026/10/26 23:52`, `Remembered as 59e69a3e0a2a2c78651d8ab2`,
`State the file is on disk and resumes at this position`.

**Run E** - a finished file is still removed (`data-e`): `continue: stored 10416 ms of 20000 ms`, then on the
replay `media: resumed at 10416 ms`, then at the end of the clip
`continue: cleared the resume point for Top Level Clip.mp4; 0 still remembered`, and `media/resume` returns to
`{}`.

**Run F** - nothing below the thresholds is stored (`data-f`): a capture during playback reads
`mediaPosition 725, mediaDuration 20000, mediaOpen true`; Back there produces **no** `continue:` line at all and
`preferences.json` keeps no `media/resume` key. The thresholds themselves are the code that was already there
(`src/shell.cpp`, ten seconds in and fifteen from the end, a tenth for clips of a minute or less) - unchanged.

**Run G** - the page's two limits, from a hand-built map of 25 records (one of them 40 days old):
`continue watching: 20 row(s) listed; 1 older than 30 days, 4 past the 20-row cap, 0 missing on disk`, page
`depth 1, entryCount 20` (`g1-capped.png`).

**Run C** - the stored map's own bound, from a hand-built map of 64 records plus one new position:
`continue: dropped resume point 400000000000000000000063 (the map is capped at 64)` - the least recently
updated entry, not whichever key sorted first - and the map holds 64 records afterwards, with the new clip's
record newest. This is the one intended change to the old cap: still 64 entries, but evicted by `at`.

**Run X** - the validator, not the shell, is what refuses a bad value. A hand-written
`"media/resume": "{\"zzzzzzzzzzzzzzzzzzzzzzzz\":5000}"` (a key that is not hex) is not loaded, so the page says
`continue watching: nothing to continue` and no migration runs on it. The file is left as it was, because
preferences are only rewritten on a set.

**Run S** - a remembered file that has gone (`data-s`, `library/video` pointing at a scratch folder holding a
copy of the clip; the copy is renamed away between the two runs):

```
continue: stored 9792 ms of 20000 ms for Gone Clip.mp4            (run S1, then the file is moved)
continue watching: 1 row(s) listed; 0 older than 30 days, 0 past the 20-row cap, 1 missing on disk
   s1-stale-row.png    the dimmed row and "Missing - this file is not on disk any more. Triangle removes it."
   s2-stale-refused.png  Cross: "Gone Clip.mp4 / Missing - this file is not on disk any more..."
shell: options opened category= video depth= 1 entry= continue:7bfa152381bf2a610cd53db1
      rows= 3 Information|Remove from Continue Watching|Close
shell: options selection 1 Remove from Continue Watching
continue: removed Gone Clip.mp4 (C:\dev\out\native-continue\stale-media\Gone Clip.mp4) from Continue Watching; 0 still remembered
continue watching: the list is empty; the page closed
```

so a stale entry is visible, refused with its reason, and removable - and `media/resume` ends at `{}`.

Sidecars (whole set, `out/native-continue/`): `a1-empty.png` and `b4-empty-after-remove.png` share SHA-256
`85c2323004f9...`; `b1-page.png` `877df7910d19...`, `b2-resumed.png` `3e007980646f...`,
`b3-options.png` `725322c6f277...`, `d1-information.png` `2c76cee52f24...`, `s1-stale-row.png`
`e297a2be368b...`, `s3-stale-options.png` `887d61edb0fe...`, `g1-capped.png` `b1efebe57369...`,
`m1-migrated-page.png` `d88c581ee6a9...`, `m2-migrated-resume.png` `e5dbda51b739...`,
`v1-video-column.png` `2b2cf6c6cc16...`.

### 7. Gates

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-continue
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-continue/re-ps3-native.exe
```

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---|---:|---:|---|
| frame | 18.29 | 18.02 | +0.27 | .8295 | .8301 | -.0006 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget` - every number identical to the ones recorded in `audit/OPTIONS-MENU.md` and
`audit/SUBTITLES.md`.

`bun work\ab-layers.ts --states --exe C:/dev/out/native-continue/re-ps3-native.exe` -> `states: 4/4 pass`, every
region `inf / 1.0000`: no named state changed. None of the named states is a Video-column frame, so the new row
appears in none of their captures. No harness file, reference or threshold was edited; the navigation
consequence in section 3 is reported rather than patched into the harness.

### 8. What is not done, and why

1. **Jellyfin resume** is not read or written. Streamed titles have no local path, so they have no id and no
   record; the page lists local files only, and a Jellyfin position would need the server's
   `UserData/PlaybackPositionTicks` (row 8 of `audit/HTPC-IDEAS.md`).
2. **Music** positions are stored by the same path (the store call is shared), and the row's `Kind` follows the
   column it is listed in, but the page is only reachable from the Video column. There is one list, not one per
   media kind.
3. **No editing**: a position can be forgotten, not adjusted, and there is no per-title menu beyond Play,
   Information and Remove.
4. **30 days and 20 rows are constants**, not settings. Making them settings would add two preference keys
   whose only consumer is this page.
5. **The age filter hides rather than deletes** (section 4). A resume point unused for a month disappears from
   the page but stays in the map until the 64-entry cap evicts it; if that is the wrong trade, the change is one
   line in `continueWatchingRows()` plus a write.
6. **The page has no artwork**: each row uses the column's own film-strip glyph. Poster or thumbnail art is row
   9 of `audit/HTPC-IDEAS.md` and would need a metadata source this build does not have.
7. **The migration cannot resolve an entry whose file moved**: it matches by path hash, so a renamed file is a
   new id and the old position is dropped with the log line shown above. Nothing is guessed from file names or
   sizes.

### 9. Files touched

- `native-xmb/src/shell.h`: `Entry::disabled`, `OptionCommand::RemoveResume`, `Page::continuePage`, the
  Continue Watching API (`migrateResumePoints`, `continueWatchingRows`, `openContinueWatching`,
  `reloadContinueWatching`, `removeContinueEntry`, `rememberedResumeCount`) and the new
  `storeResumePosition(path, ms, duration, title)` signature.
- `native-xmb/src/shell.cpp`: the record helpers (`resumeIdFor`, `resumeMap`, `resumeJson`,
  `resumeMilliseconds`, `resumeRecord`, `resumePoints`, `boundResumeMap`, `resolveResumePaths`),
  `resumePositionFor`/`storeResumePosition`/`clearResumePosition`, `migrateResumePoints` and its call in the
  constructor, the page (`continueWatchingRows`, `openContinueWatching`, `reloadContinueWatching`,
  `removeContinueEntry`, `rememberedResumeCount`), the Video column's new first row, the `entry.id=="continue"`
  branch in `activate()`, `buildOptions`, `optionsActivate`, `entryKind`, `showEntryInformation`,
  `subtitleText`, the `Action::Unavailable` message and the F5 branch in `refreshDirectory`.
- `native-xmb/src/preferences.cpp`: the `media/resume` validator (both shapes, every field and its bound).
- `native-xmb/src/view.cpp`: the disabled-row dimming - one factor on the icon alpha, one on the label alpha
  and one on the focus glow, each exactly 1 for every other row.
- `native-xmb/audit/MEDIA-CONTROLS.md`: this section.

Not touched: `view.h` (no change was needed), `main.cpp` (the existing `grab=` sidecar already carries
`entryId`, `entryTitle`, `entryCount`, `mediaPosition` and `mediaDuration`, which is what this feature needed to
be checkable), `settings_menu.cpp`, `xmbml_view.*`, `subtitles.*`, `host.*`, `pad.*`, `CMakeLists.txt`, the
verification harness, its thresholds and its references.