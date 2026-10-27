# XMB notifications (the top-right pop-up composite)

Status: implemented, 2026-10-26. Closes the B4 row and top-ten item 5 of
`FIRMWARE-PARITY-GAPS.md` for one composite, a queue with lifetimes, two honest producers and a scripted
capture path. Written after the last source edit; every number below was produced by a run of the built
binary, not by reading source.

## 1. What was missing

`FIRMWARE-PARITY-GAPS.md` B4: "XMB notifications - `system_plugin` `tex_notification_*` objects,
notification sound, overlap policy. Not present: no notification string or draw path exists in `src/`."

That was accurate. The shell had no queue and the view had no composite. What the firmware actually
composes, and what this build now draws, is below.

## 2. Firmware evidence found (all local, all re-read this session)

### 2.1 The object tree

`out/native-reference/rco-behavior/resources.json` (122 resources, 8,070 objects, 0 errors),
`vsh/resource/system_plugin.rco`, offset 39512:

```
Page   page_notification                              children 3
Plane  notification_prerender    parent page_notification   children 9
Text   notification_prerender_00a .. _05a, _0b, _1b, _2b   (nine prerendered text objects)
Plane  notification_a            parent page_notification   children 3
Plane  notification_icon_shadow_a  parent notification_a     posX 1, posY -1
Plane  notification_icon_a         parent notification_a     posX 0, posY 0
Text   notification_message_a      parent notification_a     posX 1, posY 0
Plane  notification_b            parent page_notification   children 5
Plane  notification_icon_shadow_b  parent notification_b     posX 1, posY -1
Plane  notification_icon_b         parent notification_b     posX 0, posY 0
Text   notification_label0_b       parent notification_b     posX 1, posY  0.5
Text   notification_label1_b       parent notification_b     posX 1, posY -0.375
Text   notification_label2_b       parent notification_b     posX 1, posY -0.5
```

`notification_a` carries `height 2`; the others carry 0. `notification_message_a` is a Text with
`shadowX 4, shadowY -5, shadowRed/Green/Blue 0.25`.

So there are **two variants**: a one-line variant (icon + `notification_message_a`) and a three-line
variant (icon + three labels). The text column sits at `posX 1`, i.e. one icon width to the right of the
icon centre. Those object-tree numbers drive the native layout.

`page_notification`'s own rect is not in the RCO: every plane's `posX/posY/width/height` is 0 and only
`scaleWidth/scaleHeight` is 1, exactly like the information bar's `page_infobar`. paf projects the page
from `vsh.elf`.

### 2.2 The layout/animation code

`phase3-infobar-layout.c:106-183` is `FUN_0000773c` from `basic_plugins.prx` (the module that hosts the
notifications and the infobar; the Ghidra project's file is `/boot/basic_plugins.prx`). Re-reading its
constants out of the module's own `.data` (`basic_plugins.prx`, first PT_LOAD file offset 0xf0 maps to
VA 0, so VA 0x47fc0 is file offset 0x480b0):

| address | value | use in the function |
|---|---|---|
| `DAT_00047fc0` | 0.5 | text row height: `min(lines,3) * 0.5` is written to the message Text's height |
| `DAT_00047fd0` | 0.0 | the plate's default height offset |
| beyond 3 lines | `(min(lines,6) - 3)` | the plate grows by 1 unit per row past the third, up to six |
| `DAT_00047fcc` | 2100.0 | passed as `FUN_00004e94(2100.0, widget, 100)` after the layout |
| `DAT_00048014` | 2100.0 | the same argument on the teardown path (`FUN_00008248`) |
| `DAT_00048020` | 1000.0 | sibling constant in the same block |

The variant selector is a state byte at `+0x20`: 0 selects `notification_a` and the one-line message,
anything else `notification_b`. The plane's position is written twice per show with opposite
offsets, which is the entry/exit slide the native composite imitates; the exact argument (2100) is a
paf duration or delay whose semantics are **not traced**, so the native timing uses the project's traced
paf curve instead (section 4.2).

`phase3-infobar-layout.c:3-33` (`FUN_00006c60`) is the infobar's own text-fitting path
(`infotext1`, measured against the page's own width) - useful confirmation that this module owns both
bars, and that `page_infobar` and `page_notification` are separate pages.

### 2.3 The textures

They are in `system_plugin.rco` and load through the existing `Assets::image(container,name)` path; no
new decoder was needed. Exported locally with the app's own `--export-image` for inspection:

| texture | size | what it is |
|---|---|---|
| `tex_notification` | 64x64 | the plaque: a dark rounded tile with a lighter 1 px border |
| `tex_notification_info` | 100x100 | the information glyph (white on transparent) |
| `tex_notification_caution` | 100x100 | caution glyph |
| `tex_notification_friend` | 100x100 | friends glyph |
| `tex_notification_headset` | 100x100 | headset glyph |
| `tex_notification_keypad` | 100x100 | keypad glyph |
| `tex_notification_mediasever` | 100x100 | media server glyph |
| `tex_notification_music` | 100x100 | music note glyph |
| `tex_notification_psbutton_insensitive` | 100x100 | PS button (insensitive) glyph |
| `tex_notification_settings` | 100x100 | settings glyph |
| `tex_notification_trophy_bronze` | 100x100 | bronze trophy |
| `tex_notification_trophy_silver` | 100x100 | silver trophy |
| `tex_notification_trophy_gold` | 100x100 | gold trophy |
| `tex_notification_trophy_platinum` | 100x100 | platinum trophy |

**Missing and documented, not invented:** `notification_icon_shadow_a/_b` exist as objects, but
`system_plugin.rco`'s 81 named images contain **no** `tex_notification*_shadow` texture, and the RCO
object dump does not record which image a Plane binds. The shadow plane is therefore not drawn; the
glyph is drawn once, over the plaque's own contrast. `notification_prerender_*` (nine Text objects) is
a glyph-rasterisation cache, not a visible element, and is not reproduced.

## 3. Queue and lifetime (Shell)

`src/shell.h` declares the model and `src/shell.cpp:823-872` implements it:

```
struct Notification { QString title, detail, icon; qint64 expiresAt; };
QVector<Notification> notifications_;   // front = the pop-up on screen
static constexpr qint64 notificationLifetimeMs = 4000;
static constexpr int    notificationQueueLimit = 4;
```

- **Order**: FIFO. The first event is shown first; the rest wait behind it and each gets its own
  4000 ms once it reaches the front (`promoteNotification()`, `shell.cpp:851`). A burst of three events
  therefore shows for 4 + 4 + 4 s, not 4 s total. On the real console the pop-up is also a serialised
  bar, so FIFO is the honest choice rather than newest-first.
- **Lifetime clock**: `notificationClock_`, a `QElapsedTimer` started in the constructor, so a `--render-clock`
  override or a slow host frame rate cannot stretch or shrink a notification's life. The expiry check
  is a 100 ms `QTimer`; it expires **at most one entry per tick** (a queued entry has no expiry before
  it is promoted, so a loop would have drained the whole queue at the first timeout - that bug was
  caught by the queue capture and fixed).
- **Bounded**: at 4 entries (one shown, three waiting) the **arriving** event is dropped and logged. The
  visible pop-up is never cut short for a newcomer.
- **State signal**: enqueue, show and expire all `emit changed()`, so the view's `uiRevision_` moves and
  the UI layer rebuilds. Nothing polls the shell from the render thread.
- **Lifetime source**: 4000 ms is a fit to the observed console behaviour (the firmware's own
  `DAT_00047fcc = 2100.0` argument is untraced in meaning and is not used).

## 4. View composite

### 4.1 Geometry (fitted, and why)

`View::drawNotification` (`src/view.cpp:779`) builds the composite from the firmware pieces:

| element | source | native placement |
|---|---|---|
| plaque | `tex_notification`, 64x64, drawn as a 9-slice (`View::drawNineSlice`, `view.cpp:750`) so the corners keep their pixel radius | 400 x 84 px (two lines) or 400 x 58 px (one line), right edge at x 1896, top at y 145 |
| icon | `tex_notification_<kind>`, 100x100 | side = plaque height - 12 (72 px two-line, 46 px one-line), centre 13 px in from the plaque's left edge, vertically centred |
| title | `notification_label0_b` / `notification_message_a` | 22 px, `QColor(245,245,245)`, starts one icon width right of the icon (object `posX 1`) |
| detail | `notification_label1_b` | 18 px, `QColor(206,206,206)`, under the title |

The **placement is fitted**, not traced: paf projects the page from `vsh.elf` and that projection is not
recovered. Inputs to the fit: the measured information panel (x 1329..1920, y 75..133 from
`ab-rpcs3-root.png`, `audit/INFO-BAR.md`), the object-tree ratios above (one icon width between the icon
and the text; the icon filling the plaque), and a 12 px gap under the information bar so the two never
collide. The plaque's right edge sits 24 px in from the frame edge; the info-bar band is flush, so this
is a deliberate, visible difference from the one measured surface.

### 4.2 Motion (traced paf curve)

`View::notificationEase_` is the same `PafEase` the rest of the XMB uses: **200 ms, accel mode 5**
(`FUN_003981ec` / `FUN_00398748`, `audit/PIPELINE.md` "Menu motion"). The composite slides in from 48 px
right of its resting position while its opacity goes 0 -> 1, and slides back out the same way when the
shell's entry expires; the last content is latched in `notificationShown_` so the exit still draws the
text after the shell has dropped it. A new notification arriving while one is leaving retargets from the
current value, which is exactly what the paf evaluator does.

`XMB_NOTIFY_LOG=1` also traces the curve from the view, one line per frame while it runs, so the timing
is measurable without scoring pixels:

```
notify: view slide/fade 0.0763 x 44.34 px
notify: view slide/fade 0.1946 x 38.66 px
...
notify: view slide/fade 0.9468 x 2.55 px
```

(the curve is the mode-5 fraction table: 0.076, 0.195, 0.293, 0.365 ... and lands on the target when the
frame-snapped 200 ms duration ends).

### 4.3 Input and the PS menu

- The composite reads no keys and installs no event filter: it is drawing only. Proven end to end below
  (a LEFT while a notification is up still moves the category).
- It is drawn last, above the XMB, but **only when the PS menu is closed**:
  `if(!shell_.psMenuOpen() && notificationEase_.value>.002 && ...) drawNotification(...)`
  (`view.cpp:1155-1158`). With the menu open the frame stays the console's black screen. The notification
  is not discarded: it reappears, with the remaining lifetime, when the menu closes (captured).

## 5. Producers (honest, this build's own events)

| event | where | text | icon |
|---|---|---|---|
| a tracked title returned (RPCS3 launch) | `src/shell.cpp:149-161`, in the `QProcess::finished` handler | `<title>` / "Returned to the XMB" | `tex_notification_info` |
| the same title crashed | same handler, `CrashExit` branch | `<title>` / "This title ended unexpectedly." | `tex_notification_caution` |
| a Steam handoff returned | `src/shell.cpp:218`, in `pollSteamGame()`'s return path | `<title>` / "Returned to the XMB" | `tex_notification_info` |
| a media library was rescanned | `src/shell.cpp:757-762`, in the `MediaLibrary::loaded` handler, only when `refreshDirectory()` set `directoryRefresh_` (`shell.cpp:1445`) | `<category> Library updated` / `N items` | `tex_notification_music` for Music, `tex_notification_info` otherwise |

No network event is fabricated. The launch-return pop-up has **no firmware counterpart** - the console
does not notify when a game quits - so it is labelled a native event; the library-rescan pop-up
corresponds to the console's own "background work finished" class of notification but is raised by this
build's F5 refresh, not by a PSN or copy operation.

Deliberately not wired: friend sign-in and trophy events. Steam presence changes and trophy state are
read-only mirrors of another client; a pop-up would claim the console observed an event it did not
(`FIRMWARE-PARITY-GAPS.md` section 4.3).

## 6. Script token and trace

`--script` gains one token (`src/main.cpp:575-586`):

```
NOTIFY=<title>[|<detail>[|<icon>]]
```

The icon is either a `tex_notification_<kind>` suffix (`trophy_gold`, `music`, `friend`, ...) or the full
texture name; anything unrecognised falls back to `tex_notification_info` rather than requesting a
texture that is not there. Commas separate script tokens, so a title cannot contain one. The token calls
`Shell::notify()`, i.e. exactly the path the real producers use.

`XMB_NOTIFY_LOG=1` turns on the trace (opt-in; with it unset the only "notify" lines are the script
token's own log and the grab sidecar field):

```
notify: enqueue <title> | <detail> <texture> queue 1 / 4
notify: show <title> <texture> 4000 ms
notify: expire <title>
notify: drop <title> ... (queue full, 4 shown or waiting)
notify: view slide/fade <ease> x <px> px      (per frame while the composite moves)
```

The grab sidecar (`<grab>.png.json`) gains `notificationCount`, `notificationRemainingMs`,
`notificationTitle` and `notificationIcon`, so a capture can assert the state without pixels.

## 7. Verification

Build:

```powershell
& C:\dev\xmb\native-xmb\build.ps1 -OutputDirectory C:\dev\out\native-notify
```

All captures below use the build above, `--dev-flash C:/dev/rpcs3/bin/dev_flash`, a **copy** of
`out/native-reference/store/ui3-data` as the data directory (the copy keeps the shared seed from being
written to - the app saves window geometry on quit), and the pinned clocks
`--render-clock 20 --background-clock 2026-10-25T10:00:00 --ui-clock 2026-10-25T12:32:00`.

### 7.1 Script token: show, then expire

Script (exact, one line; the full command also carries `--dev-flash`, `--data-dir` and the three clocks):

```
full,wait,wait,NOTIFY=Message received|From Albert,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/notify-show.png,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/notify-expired.png,quit
```

Log (`XMB_NOTIFY_LOG=1`):

```
notify: enqueue Message received | From Albert tex_notification_info queue 1 / 4
notify: show Message received tex_notification_info 4000 ms
script: notify Message received From Albert
script: grab .../notify-show.png QSize(1920, 1080) saved
script: state QJsonObject({... "notificationCount":1, "notificationIcon":"tex_notification_info",
  "notificationRemainingMs":2703, "notificationTitle":"Message received" ...})
notify: expire Message received
script: grab .../notify-expired.png QSize(1920, 1080) saved
script: state QJsonObject({... "notificationCount":0, "notificationIcon":"", 
  "notificationRemainingMs":0, "notificationTitle":"" ...})
```

Captures (local, not committed):

- `out/native-reference/notify/notify-show.png`: the plaque at rest under the information bar, info
  glyph, `Message received` / `From Albert`.
- `out/native-reference/notify/notify-expired.png`: the same frame with no composite.

Non-collision with the information panel, measured on the same pair with the project's own comparator:

```
bun work/ab-compare.ts notify-expired.png notify-show.png 1329,75,591,58
  PSNR Infinity dB  SSIM 1.0000        <- the "info bar" layer crop is bit-identical
bun work/ab-compare.ts notify-expired.png notify-show.png 1480,130,420,120
  PSNR 15.02 dB  SSIM 0.7636           <- the composite's own region differs, as it must
```

Two captures that differ only by whether a notification is up leave the information bar's 591x58 crop
pixel-identical, which is the strongest form of "does not collide" this harness can report.

### 7.2 Queue: three events, one at a time, 4 s each

Script (exact):

```
full,wait,wait,NOTIFY=First Steps|Gold Trophy|trophy_gold,wait,grab=C:/dev/xmb/out/native-reference/notify/queue-trophy.png,NOTIFY=Music Library updated|12 items|music,NOTIFY=Friend signed in|Albert is online|friend,wait,grab=C:/dev/xmb/out/native-reference/notify/queue-second.png,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/queue-third.png,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/queue-drained.png,quit
```

```
notify: enqueue First Steps | Gold Trophy tex_notification_trophy_gold queue 1 / 4
notify: show First Steps tex_notification_trophy_gold 4000 ms
script: state {... "notificationCount":1, "notificationRemainingMs":3019 ...}
notify: enqueue Music Library updated | 12 items tex_notification_music queue 2 / 4
notify: enqueue Friend signed in | Albert is online tex_notification_friend queue 3 / 4
script: state {... "notificationCount":3, "notificationRemainingMs":1457, "notificationTitle":"First Steps" ...}
notify: expire First Steps
notify: show Music Library updated tex_notification_music 4000 ms
script: state {... "notificationCount":2, "notificationRemainingMs":503, "notificationTitle":"Music Library updated" ...}
notify: expire Music Library updated
notify: show Friend signed in tex_notification_friend 4000 ms
notify: expire Friend signed in
script: state {... "notificationCount":0 ...}
```

`queue-trophy.png` is the one-line variant with the real gold trophy glyph; `queue-second.png`,
`queue-third.png`, `queue-drained.png` follow the queue through.

### 7.3 Real producer (a): a tracked title launched and returned

A stand-in title is registered the way an imported game app is (`apps.json`, `kind: game`,
`category: game`) with `executable: C:\Windows\System32\cmd.exe`, `arguments: ["/c","exit 0"]`, title
`Stand-In Title`. The Game column then carries it as `custom:standin-title` at row 2 (dumped with
`XMB_DUMP_ENTRIES`), and ENTER runs `Shell::activate() -> Action::LaunchApp -> startTracked()` - the same
tracked `QProcess` path a launched title takes.

Script (exact): `full,wait,wait,DOWN,DOWN,wait,wait,ENTER,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/standin-return.png,quit`

```
launch: C:/Windows/System32/cmd.exe /c exit 0 (tracked)
returned from Stand-In Title exit 0 normal
notify: enqueue Stand-In Title | Returned to the XMB tex_notification_info queue 1 / 4
notify: show Stand-In Title tex_notification_info 4000 ms
script: grab .../standin-return.png QSize(1920, 1080) saved
script: state {... "selection":2, "entryId":"custom:standin-title",
  "notificationCount":1, "notificationRemainingMs":2415, "notificationTitle":"Stand-In Title" ...}
```

### 7.4 Real producer (b): a media library rescan

Data directory: a copy of `out/native-reference/store/video-data` (it points `library/video` at
`out/native-reference/video-library`). Script (exact): open Video Library, then `REFRESH` (F5), then grab.

```
full,wait,wait,LEFT,LEFT,wait,wait,ENTER,wait,wait,wait,wait,REFRESH,wait,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/library-updated.png,quit
```

```
media: directory result 3 entries; truncated false error false      <- the folder opening
media: directory result 3 entries; truncated false error false      <- the F5 rescan
notify: enqueue Video Library updated | 3 items tex_notification_info queue 1 / 4
notify: show Video Library updated tex_notification_info 4000 ms
script: grab .../library-updated.png QSize(1920, 1080) saved
script: state {... "category":"video", "depth":1, "notificationCount":1,
  "notificationRemainingMs":2391, "notificationTitle":"Video Library updated" ...}
```

Only the rescan notifies - opening a folder for the first time does not - which is why the same page
scan appears twice in the log but produces one notification. `library-updated.png` shows it over the
open page.

### 7.5 Input isolation, and the PS menu

```
full,wait,wait,NOTIFY=Message received|From Albert,wait,LEFT,wait,grab=C:/dev/xmb/out/native-reference/notify/input-left-while-shown.png,quit
```

```
notify: show Message received tex_notification_info 4000 ms
script: state {... "category":"tv", "categoryIndex":5, "notificationCount":1,
  "notificationRemainingMs":2396 ...}
```

The category moved Game -> TV while the pop-up stayed up: the composite does not take input.

```
full,wait,wait,pshold,wait,wait,wait,wait,NOTIFY=Download finished|Update installed,wait,grab=C:/dev/xmb/out/native-reference/notify/ps-menu-open.png,ESC,wait,wait,grab=C:/dev/xmb/out/native-reference/notify/ps-menu-closed.png,quit
```

```
shell: PS menu opened
notify: enqueue Download finished | Update installed tex_notification_info queue 1 / 4
notify: show Download finished tex_notification_info 4000 ms
script: grab .../ps-menu-open.png QSize(1920, 1080) saved      <- black PS menu, no composite
script: state {... "notificationRemainingMs":3288 ...}
script: grab .../ps-menu-closed.png QSize(1920, 1080) saved    <- composite returns, 1769 ms left
```

## 8. Gate result

Mandated command:

```powershell
& "$env:USERPROFILE\.bun\bin\bun.exe" work\ab-layers.ts --capture --exe C:/dev/out/native-notify/re-ps3-native.exe
```

| layer | PSNR | baseline | delta | SSIM | baseline | delta | gate |
|---|---:|---:|---:|---|---:|---:|---|
| frame | 17.95 | 18.02 | -0.07 | .8286 | .8301 | -.0015 | PASS |
| top background | 20.24 | 20.24 | +0.00 | .9344 | .9344 | +.0000 | PASS |
| category icon row | 20.91 | 21.05 | -0.14 | .8947 | .8950 | -.0003 | PASS |
| item label row | 11.94 | 11.94 | +0.00 | .3913 | .3910 | +.0003 | PASS |
| clock text | 11.39 | 11.39 | +0.00 | .4105 | .4105 | +.0000 | PASS |
| info bar | 14.78 | 14.78 | +0.00 | .6117 | .6117 | +.0000 | PASS |

`PASS: 6/6 layers within budget`. The canonical capture runs
`full,wait,wait,wait,grab,quit` in a fresh data directory, raises no notification (no producer fires in
that window), so every layer is unchanged to the last digit.

Named states, same executable: `bun work\ab-layers.ts --states --exe ...` -> `states: 4/4 pass`, every
region `inf / 1.0000`. No named state contains a notification, and no reference or threshold was
touched.

## 9. Gaps this pass leaves open

1. **The page projection.** The plaque's on-screen rect and the 24 px right inset are fitted to the
   measured information panel, not traced. Closing it needs the paf page projection in `vsh.elf`
   (the `page_notification` equivalent of `vshcommon_28549FD0`, the infobar's geometry function).
2. **The plaque growth rule.** `min(lines,3) * 0.5` and `+1 per row past three` are traced constants, but
   the unit they scale (icon width? 0.5 px?) is not; the native composite therefore grows in pixels by
   content, and never shows more than two text lines (title + detail) of the three firmware label slots.
3. **The `2100` argument** on the show and teardown paths is untraced in meaning. If it is a hold time,
   the native 4000 ms lifetime is a deliberate deviation, not parity.
4. **The icon shadow plane** (`notification_icon_shadow_a/_b`) is not drawn: no matching texture exists in
   `system_plugin.rco`'s image list and the Plane's image binding is not in the object dump. If a texture
   is missing entirely (a dev_flash without `system_plugin`), `drawNineSlice` draws no plaque and
   `drawNotification` logs `notify: texture missing ...` once and draws the title/detail over the frame
   with the plaque's own left margin - the composite is built from whatever is present, never from
   invented artwork. The textures were inspected from a local export
   (`out/native-reference/notify-tex`, produced with the app's own `--export-image`) that is not committed.
5. **The notification sound** is not played. `system_plugin` carries the cue (the interaction-audio pass
   decodes five cues plus `snd_trophy`), but the notification's own cue and its mixing are not traced, and
   inventing a mapping would be a claim this build cannot support.
6. **`notification_prerender_*`** (nine prerendered Text objects) is a rasterisation cache. The native
   text renderer draws the strings directly; reproducing the prerender cache would change nothing visible.
7. **Per-type suppression.** The console lets the user disable message and trophy notifications
   (`msg_notification_message`, `msg_trophy_notification` in `sysconf_plugin` text). This build has no
   settings row for either, and its producers are the native ones above, so nothing is suppressed.

## 10. Files touched

- `native-xmb/src/shell.h`: `Notification`, the public queue API (`notify`, `notification`,
  `notificationCount`, `notificationRemainingMs`), the queue state, `directoryRefresh_`.
- `native-xmb/src/shell.cpp`: `notify`, `promoteNotification`, `expireNotifications`,
  `notificationRemainingMs`, the constructor wiring; producers in `startTracked`'s finished handler,
  `pollSteamGame`'s return path and the `MediaLibrary::loaded` handler; `refreshDirectory` marks a rescan.
- `native-xmb/src/view.h`: `notificationEase_`, `notificationShown_`, `drawNotification`, `drawNineSlice`.
- `native-xmb/src/view.cpp`: the ease retarget/advance and signature term, the `XMB_NOTIFY_LOG` curve
  trace, `drawNineSlice`, `drawNotification`, and the draw call guarded by `!shell_.psMenuOpen()`.
- `native-xmb/src/main.cpp`: the `NOTIFY=` script token and the notification fields in the grab sidecar.
- `native-xmb/audit/NOTIFICATIONS.md`: this document.

Not touched: `settings_menu.cpp`, `preferences.cpp`, `view`'s existing draw paths, the verification
harness (`work/ab-layers.ts`, its thresholds and references), and any other audit file.
