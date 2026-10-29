# TRANSITION-HARNESS.md - the frame-by-frame transition instrument

Written 2026-10-28. Tool: `work/transition-compare.ts` (bun) plus `work/transition-probes.json`
(probe definitions) and `work/transition-render.py` (raster composition, bundled Python + Pillow).
No app source was edited for this pass: `src/view.cpp`, `src/shell.cpp`, `src/main.cpp`, the build
files, `harness/`, `gates/`, budgets, references and git were not touched.

What it does: takes two frame sequences of the same XMB transition (console reference vs native
capture), reduces each frame to the same named probes, aligns the two by matched progress, prints a
t(delta) table with PASS/WARN/FAIL per metric, scores each side, and writes the human recordings
(side-by-side GIF, contact sheet, mp4 when ffmpeg exists).

## 1. Quick start

```
cd C:\dev\xmb
bun work/transition-compare.ts --list                  # transitions, probes, notes
bun work/transition-compare.ts                         # compare all, print tables
bun work/transition-compare.ts -t page-open --measure   # per-frame records, both sides
bun work/transition-compare.ts -t cat-right --compare   # one transition
bun work/transition-compare.ts --capture --force        # drive native-xmb, 3 attempts each
bun work/transition-compare.ts --render                 # GIF + contact sheet (+ mp4)
bun work/transition-compare.ts --json out.json          # machine-readable results
```

Flags: `--list`, `-t/--transition <id>` (repeatable, default all), `--measure`, `--compare`
(default), `--capture`, `--render`, `--probes <file>`, `--out <dir>`, `--exe <path>`,
`--json <file>`, `--grid <n>` (alignment points, default 16), `--gif-frames <n>` (default 24),
`--attempts <n>` (native burst attempts, default 3), `--force`, `-v`.

Default exe: `C:\dev\out\native-iconwave1\re-ps3-native.exe`, the newest build on disk (10-28
17:28:56). It is NEWER than `C:\dev\out\native-xmb\re-ps3-native.exe` (17:15:24) and than
`native-merge1` (16:54:28), so the launcher build is stale vs the HEAD sources it was asked about
(`view.cpp` 17:28:35, `main.cpp` 17:20:54) and this pass used the newest one instead of rebuilding.

## 2. Frame sequences and coordinates

Each side of a transition names a directory, a filename regex with the time in a capture group, a
capture crop, and a key time. Times are reported relative to the key (`keyMs`), so console and
native share an origin.

```json
"console": {
  "dir": "C:/dev/out/navfeel/navmove/right1",
  "match": "^f_(\\d+)\\.png$", "timeIndex": 1, "timeUnit": "ms",
  "crop": [0, 180, 1920, 680], "timeRange": [500, 780], "keyMs": 540.4,
  "params": { "label": { "region": [0, 345, 1920, 45] } }
}
```

- Probe geometry is always FULL-FRAME 1920x1080 coordinates. The crop tells the tool what the file
  actually contains; a probe whose region falls outside the crop is reported unavailable (n/a), not
  guessed. That is why the console's 1920x180 band cannot carry item-row metrics while the native
  full frame can.
- `params` per sequence overrides probe parameters for that side only. Necessary because a band
  crop (origin y 220 or 180) and a framebuffer grab (origin y 0) never share a y, and the native
  icons sit 30-45 px higher than the console icons in these captures.
- `prepend` adds an at-rest grab as t=0 so the driver's start value is measured, not extrapolated
  from a burst whose first frame is already 20-160 ms in.
- `pins` would state progress per frame (for `XMB_DEPTH_PIN` series). No transition uses it this
  pass; it exists for the next one.

## 3. Probe catalogue

All probes share one primitive set, the same ones the audits already use: luma with Rec.709
weights, a high-pass ink mask (pixel brighter than its surrounding box mean by k), ink runs merged
into blobs, ink-weighted centroids, luma percentiles in a region.

| kind | measures | main keys |
|---|---|---|
| `bar` | category bar: ink blobs across the icon band; `focusAt` picks the blob nearest the focus slot; pitch from adjacent centroid gaps in [pitchMin,pitchMax] | focusX, focusY, focusW, focusH, focusMass, merged, pitch, count, leftX/rightX, leftMass/rightMass, leftLuma/rightLuma |
| `ink` | ink-weighted centroid and bbox of one region, absolute or high-pass mask | cx, cy, w, h, left, right, top, bottom, mass, p99 |
| `label` | one text line: the densest ink row band in the region, its left edge and its text blocks (gaps under blockGap merge) | left, y, mass, peak, blocks, block0..block3, block*End |
| `rows` | item-row block: horizontal ink bands, each row's y, its label left edge, and the block's left/top/bottom | count, blockLeft, blockTop, blockBottom, y0..y5, left0..left5 |
| `luma` | mean, p99, sigma, min, max of a region | mean, p99, std, min, max |
| `track` | NCC template match of a patch taken from a settled frame, over a search window and a scale list; the incoming column's x/y and its scale | x, y, scale, corr |

Special driver `@motion`: progress from the measured motion window instead of a content metric. Per
frame the tool counts the fraction of sampled pixels that moved more than 10 luma since the previous
frame; a frame is "moving" above 25% of that sequence's own peak rate; the window runs from the
first to the last moving frame and p is the clamped fraction of that window. This is the driver for
the category moves, because the native build pins the entering icon (section 7) so no x is a shared
progress axis there.

## 4. Alignment, scores, verdicts

- Progress p: driver metric normalised between its settled start (median of the first 10% of
  frames) and settled end (median of the last 10%), or the `@motion` window. p is clamped to [0,1]
  for alignment; the unclamped value is kept for overshoot.
- Grid: p = 0, 1/16 .. 1. Both sides are linearly interpolated in p, per metric.
- A grid point outside the p range the two sequences share is marked n/a (this is what keeps a
  native burst that starts at p=0.55 from inventing a comparison at p=0.2).
- `best alignment offset` = median over the grid of (t_native(p) - t_console(p)); `spread` is the
  sigma of those lags. It is the constant time shift that best matches the two, and the spread is
  the residual shape mismatch. Example: page-open's -694 ms is the console's 709 ms page-load delay
  before any motion, not a curve difference.
- `duration` = last moving frame minus first moving frame inside the capture, per side. For the
  native side this is a LOWER BOUND: the burst ends before the animation does (section 8).
- Per-side score (0-100) = 100 * (0.35*coverage + 0.35*monotonic + 0.30*smoothness) *
  (1 - min(1, 4*overshoot)); coverage = fraction of the 17 grid points the side brackets,
  monotonic = fraction of frame steps that do not reverse, smoothness = 1 - min(1, jerk/(1.5*step))
  with jerk the mean |second difference| of p and step the mean |first difference|. Overshoot is the
  largest excursion past p=1.
- Verdict rule: each metric gets PASS/WARN/FAIL on its own band; the TRANSITION verdict is the worst
  of the transition's `headline` metrics only. If no headline metric is measurable, the verdict is
  NO-DATA rather than the worst of the extras. NATIVE-ONLY means there is no console counterpart at
  all.

## 5. Tolerances and where they come from

Stage one is the console's own variance, printed by every compare run as
`console noise (settled-tail sigma)`. It is the sigma of each headline metric over the last 20% of
console frames, i.e. while the transition is settled:

| transition | metric | measured console sigma |
|---|---|---|
| page-open | track.x, track.scale | 0.00, 0.00 (139.7 fps oversamples a 60 fps render, so the tail frames are duplicates) |
| page-close | track.x, track.scale, bar.pitch | 0.00, 0.00, 0.34 px |
| cat-right | track.x, bar.pitch | 2.39 px, 0.40 px |
| cat-left | bar.pitch | 2.90 px |
| options-open / options-close | panelLuma | hidden-object measurement, no time axis |

Stage two is the chosen band, always at least as wide as 2x that sigma and widened where the
instrument itself is quantised (template step, 1/16 p grid, glyph antialias). Full table with a
`source` string per metric is in `work/transition-probes.json` -> `toleranceDefaults`. The headline
bands:

| metric | PASS | WARN | basis |
|---|---:|---:|---|
| track.x | 8 px | 16 px | 2x cat-right console sigma + template quantisation; 8 px is 3% of the 262 px page slide |
| track.scale | 0.05 | 0.12 | 5% of the quad |
| bar.pitch | 4 px | 10 px | 2% of the fixed 200 px grid |
| panelLuma.mean | 3 luma | 8 luma | console settled sigma about 0.6 |
| panelLuma.std | 3 luma | 8 luma | same |

Everything else (row positions, label blocks, ink mass, c) carries its own band in the JSON with the
same two-stage provenance. y-valued metrics use a deliberately loose band (14/34 px) because the
console band crop and the native framebuffer do not share a y origin; a y delta here is capture
placement, not animation.

## 6. Capture recipes

Common arguments (from `captureNative`):

```
re-ps3-native.exe --dev-flash C:/dev/rpcs3/bin/dev_flash --data-dir <dir>/data
  --render-clock 30 --background-clock 2026-10-28T10:00:00 --ui-clock 2026-10-25T12:32:00
  --focus-time 2000 --script "<tokens>"
env: XMB_STEAM_ROOT=<dir>/empty-steam  XMB_INPUT_SCRIPT_ONLY=1
```

`--render-clock 30` pins the background, ribbon and particle time; without it the wave phase drifts
between runs and every luma metric moves. The script clock starts 2500 ms after the window opens and
consumes one token per 300 ms; `wait` is an accepted no-op. Boot state without a data dir is the
Game column (categoryIndex 6, entry steam-store), so navigation is counted from there.

| transition | script tokens | verified end state |
|---|---|---|
| page-open | full, wait, grab-before, LEFT x5, grab-at-settings, burst@ENTER, wait x3, grab-after, quit | category=settings depth=1 |
| page-close | full, wait, LEFT x5, wait, ENTER, wait x2, grab-before, burst@ESC, wait x2, grab-after, quit | page closes (no expect block; frames show the close) |
| cat-right | full, wait, RIGHT, wait, grab-before, burst@RIGHT, wait x2, grab-after, quit | category=psn |
| cat-left | full, wait, RIGHT x2, wait, grab-before, burst@LEFT, wait x2, grab-after, quit | category=network |
| options-open | full, wait, grab-closed, TRIANGLE, wait x3, grab, quit | category=game depth=0, options panel up |
| options-close | full, wait, TRIANGLE, wait x2, TRIANGLE, wait x3, grab, quit | category=game depth=0, panel gone |
| ps-tap | full, wait, grab-before, ps, wait, grab, quit | controller indicator (no console oracle) |
| ps-hold | full, wait, pshold, wait x2, grab, quit | system menu (no console oracle) |

Each burst token presses one key and saves 12 frames 12 ms apart; the first grab costs a frame or
three, so the tool runs every burst recipe `--attempts` times (default 3) and keeps the attempt
whose first frame is earliest. That is a coverage choice about the instrument, not a selection on
the motion; the other attempts stay under `<dir>/attempt-N/` as evidence. One state sidecar per grab
(`<name>.png.json`) is checked against the recipe's `expect` block, so a recipe that lands on the
wrong column fails loudly instead of measuring the wrong screen.

## 7. Transitions captured and verdicts

Durations are first-motion to settle measured inside each capture. Native durations are lower
bounds (the burst ends before the animation does). `max|d|` is the worst headline delta over the
alignment grid. `offset` is native minus console time at matched progress.

| transition | console ms | native ms | max abs delta | offset | verdict |
|---|---:|---:|---:|---:|---|
| page-open | 217 | 121 | 14 px (track.x) | -694 ms | WARN |
| page-close | 217 | 119 | 55 px (track.x) | -62 ms | FAIL |
| cat-right | 198 | 117 | 150 px (track.x) | -47 ms | FAIL |
| cat-left | 249 | 130 | n/a (see below) | -20 ms | NO-DATA |
| options-open | n/a | n/a | 45.5 luma (panelLuma.mean) | n/a | FAIL |
| options-close | n/a | n/a | 25.3 luma (panelLuma.mean) | n/a | FAIL |
| ps-tap | n/a | n/a | n/a | n/a | NATIVE-ONLY |
| ps-hold | n/a | n/a | n/a | n/a | NATIVE-ONLY |

Per transition scores (section 4): page-open console 70 / native 83.8; page-close 70 / 88.7;
cat-right 95.9 / 87; cat-left 62.9 / 88.9. Endpoint transitions score 0 by definition (no time
axis).

### What each verdict means

1. **page-open WARN**. The focused icon's slide matches within 6-14 px at every matched progress
   over a 262 px travel (console 568 -> 306, native 582 -> 312), and the quad scale is within 0.12.
   The -694 ms offset is the console's 709 ms page-load delay before the animation starts (PAGE-
   MOVE.md section 2); the native build starts its 300 ms move immediately, which is the other
   visible difference and is a design question, not a curve error.
2. **page-close FAIL**. Same probes, but the native close is up to 55 px ahead of the console at
   matched progress, i.e. the close curve is not the mirror of the open curve. That is the one
   number to chase in the next pass.
3. **cat-right FAIL, 150 px**. The console's entering category icon slides 751 -> 561 px over
   198 ms. The native build has it at 565 px on the first captured frame (20 ms) and holds it: the
   entering icon is pinned at the focus slot instead of sliding in. This is PAGE-MOVE.md 14.2
   section 7 item 2, now measured per frame. `bar.pitch` PASSES (200 px grid intact on both sides).
4. **cat-left NO-DATA**. The console `left1` burst's template track is unstable (settled sigma
   73.7 px: the patch matches two icons), and the native burst's bar pitch does not resolve on any
   frame of this capture, so no headline metric has a delta. The left direction is measured but
   not yet comparable; the entering-icon finding rests on cat-right only.
5. **options-open / options-close FAIL**. No console burst exists; the console ground truth is a
   settled still (`dialogs/options-open-01.png`, `options-closed-03.png`). The native panel glass is
   45.5 luma brighter (open) and 25.3 luma brighter (close) in the sampled region than the console's.
   The row sets also differ (console: Sort By, Group Content, Start, Delete, Information; native:
   Open, Information), so only glass brightness is in the headline.
6. **ps-tap / ps-hold NATIVE-ONLY**. Captured and recorded, but no console counterpart on disk
   (section 8).

## 8. Missing capture tokens and flags

These are the gaps this pass found. None of them needs a source edit to *document*; each line names
what the next pass must add.

| gap | what it blocks | what it would take |
|---|---|---|
| `burst=` always takes exactly 12 frames at 12 ms, and the first saved frame lands 20-160 ms after the key (grabFramebuffer costs a frame or three and varies per run) | the transition onset and the first 20-160 ms of every native move; native duration is a lower bound only | `burst=<prefix>@KEY@<frames>@<intervalMs>`, or a pre-roll that captures before the key, or a cheaper grab (client-rect readback instead of grabFramebuffer) |
| `burst=x@ps` and `burst=x@pshold` are silently ignored (the `@` argument is looked up in the keys map, where `ps` is not a key) | a time series for the PS tap indicator and the PS hold menu | accept the non-key token names in the burst's `@` argument, e.g. `@ps`, `@pshold`, `@NOTIFY` |
| one key per burst | a two-keys-inside-one-burst transition (console `right2step` has RIGHT at 547.6 and 672.7 ms) | `burst=<prefix>@RIGHT+125:RIGHT` style key list with per-key offsets |
| no console burst of options-open, options-close, PS tap or PS hold exists (only stills, and for PS only crops with no recorded origin) | time-series verdicts for those four | console captures first: `refcap2.ps1` plan with `cburst` around Triangle / PS, at a crop that holds the panel or the indicator |
| `XMB_DEPTH_PIN` (0..2) pins depth but there is no equivalent pin for the category move | a full native p-series of a category move without depending on burst coverage | `XMB_MOVE_PIN=<0..1>` next to the depth pin, or a `--pin-move` flag |
| `--render-clock` pins the background but the console burst's background is live | background luma (the "glass/backdrop" metric) as a verdict | either a console burst taken with a static background, or compare backdrop only against a pinned console still |

## 9. Known limitations / probe debt

- `bar` picks its focus blob by proximity to the focus slot (`focusAt`); when icons merge mid-slide
  the merged blob's centroid is between two icons, so `bar.focusX` and the neighbour keys are
  content-dependent during a move. They are reported but never headline metrics.
- `rows` (item-row block) does not separate text rows from the bright background wave on the native
  frames: it merges to one band where the console band crop gives three or four. Its keys are in the
  tables and the JSON but are not headline metrics until the detector is fixed (next pass: per-row
  text mask restricted to the label column, plus a minimum ink mass per row).
- `label` on the options panel locks onto the native panel rim (a harder, brighter edge than the
  console glass), which is why options verdicts use panel luma only.
- Backdrop luma is phase-dependent (console background live, native pinned), so for the page
  transitions it is measured, printed, and excluded from the verdict.
- The console `left1` template track is unstable (section 7.4); the right1 track is stable
  (sigma 2.39 px) and carries the entering-icon finding.
- Two console captures exist of the page open/close (`burst-png` 642 frames, `burst2-png` 539
  frames). Only the first is wired in; the second is available for a variance study.

## 10. Files written by this pass

| path | what |
|---|---|
| `work/transition-compare.ts` | the tool: decode, probes, alignment, verdicts, capture, render |
| `work/transition-probes.json` | 8 transitions, their probes, per-side geometry, tolerance bands and provenance |
| `work/transition-render.py` | composition + GIF/contact sheet/mp4 via the bundled Python and Pillow |
| `native-xmb/audit/TRANSITION-HARNESS.md` | this file |
| `C:\dev\out\transitions\native\<id>\` | native frames per transition (burst + preamble + after grab + state sidecars + capture.log + attempt-N) |
| `C:\dev\out\transitions\<id>-side-by-side.gif` | console or native left/right, same frame index, labels burned in (page-open, page-close, cat-right, cat-left) |
| `C:\dev\out\transitions\<id>-contact-sheet.png` | every 4th sampled progress point, both rows, labelled |
| `C:\dev\out\transitions\<id>-endpoint.png` | one-frame side-by-side for the endpoint transitions (options, ps) |
| `C:\dev\out\transitions\results.json` | the full machine-readable result: per-frame p, grid deltas, verdicts, scores |
| `C:\dev\out\transitions\.tiles\<id>\` | the composed tiles and the render manifest (intermediate) |

ffmpeg was not on PATH during this pass, so no mp4 was written; the GIF is the deliverable and it
exists for all four time-series transitions (25 frames each, 120 ms per frame).

## 11. Reproduction, end to end

```
cd C:\dev\xmb
bun work/transition-compare.ts --capture --force --attempts 3
bun work/transition-compare.ts --json C:\dev\out\transitions\results.json
bun work/transition-compare.ts --render
```

Console material (pre-existing, not re-captured): `C:\dev\out\rpcs3-xmb-cap\burst-png` (page
open/close, 139.7 fps, keys PgDown 707 ms and PgUp 2706 ms), `C:\dev\out\navfeel\navmove\{right1,
left1}` (83.6 / 71.2 fps, keys 540.4 / 540.1 ms), `C:\dev\out\rpcs3-ref-2026-10-28\dialogs\options-
{open-01,closed-03}.png`.

## 12. Verdicts after the navigation-feel pass (2026-10-28, PAGE-MOVE Phase 16)

These replace the table in section 7 for the four time-series transitions. The native sequences were
re-captured on the Phase-16 build (`C:\dev\out\navfeel\certified\re-ps3-native.exe`) with the extended
grammar, so the onset grab is present in every one. Details and the time-aligned tracks: PAGE-MOVE
section 16.

| transition | console ms | native ms | max abs delta | offset | verdict |
|---|---:|---:|---:|---:|---|
| page-open | 217 | 229 | 61 px (track.x) | -664 ms | FAIL |
| page-close | 217 | 244 | 57 px (track.x) | -20 ms | FAIL |
| cat-right | 198 | 165 | 32.5 px (track.x) | -28.6 ms | FAIL |
| cat-left | 249 | 166 | n/a | -8.9 ms | NO-DATA |

What moved and what the numbers mean:

1. **cat-right**: the stored sequence predated the entering-icon fix (PAGE-MOVE 15.2). A fresh capture
   of HEAD shows the icon sliding 725 -> 565 (the console's 715 -> 561 at the same progress), p = 0
   delta +10 px. The remaining 32.5 px is the `@motion` window (native 165 ms vs console 198 ms), set by
   the onset grab's 6 ms first interval; the same build at a 12 ms burst spans 92 ms and scores 58 px.
   In matched time the two tracks agree within 6 px from +66 ms.
2. **page-close**: the close now holds 49 ms and then runs the open's own 300 ms mode-4 curve. The
   offset improves -55 -> -20 ms and the time-aligned track goes from 101 px ahead to within 13 px at
   +57..+399 ms, but the headline max is one grid point: p = 0 is the page-state glyph, where the
   console's bar template locks at x 360 and the native's at 303 on the same physical icon (ink bbox
   delta 2 px). **Capture stability check**: the same before build scores WARN 6 px with a 24-frame
   burst and FAIL 52 px with a 40-frame burst, so this headline is not capture-stable and the
   time-aligned track is the number to use.
3. **page-open**: 61 px vs the before build's 63 px on the same capture - unchanged. The FAIL is the
   console's ~709 ms page load, which this build does not model (section 7.1 of this file already says
   so); the onset grab now makes it visible at p = 0 instead of hiding it behind a late first frame.
4. **cat-left**: still NO-DATA (the console `left1` template track is unstable and the native
   `bar.pitch` does not resolve on that sequence). The entering Network icon starts at 365 px against
   the console's 356.8 px left-neighbour centroid.

Recordings regenerated: `C:\dev\out\transitions\{page-open,page-close,cat-right,cat-left}-side-by-side.gif`
and `*-contact-sheet.png`. No probe, tolerance or capture recipe was changed by the pass.

