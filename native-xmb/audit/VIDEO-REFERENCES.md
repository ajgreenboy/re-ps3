# Public video references: measured values

Measured October 24, 2026 by reading decoded frames in a browser (canvas readback of the YouTube video element).
No video or frame was downloaded or stored. Values come from pixel statistics on 1920×1080 frames and are
**reference measurements of RPCS3-rendered 4.x XMB footage**, not Sony documentation. RPCS3 runs the real
VSH, so presentation should be firmware-accurate, but colour depends on each recording's month/time and on
video compression. Use these to fit and check the native renderer; recheck against the user's own recordings.

## Sources

| Id | Video | Content | Format |
|---|---|---|---|
| W | [PS3 XMB Wave in 4K60 — Full Day Cycle, No Icons](https://www.youtube.com/watch?v=NDxCEV2fj-g) (JustBeLikeBrett) | RPCS3 with icons hidden; console clock stepped through 24 h in 1.5 h steps, ~17 min per step | 1920×1080 decoded, 60 fps |
| N | [RPCS3 Original PS3 XMB Interface](https://www.youtube.com/watch?v=nDL5YJt2aaA) (Avalice Productions) | Navigation, Original theme, console date 12/9, ~12:05 | 1920×1080, 60 fps, 100 s |

Chapter starts in W (video time → console clock): 0:00 00:00, 17:03 01:30, 34:07 03:00, 51:11 04:30,
1:08:15 06:00, 1:25:19 07:30, 1:42:23 09:00, 1:59:27 10:30, 2:16:31 12:00, 2:33:35 13:30, 2:50:39 15:00,
3:07:43 16:30, 3:24:47 18:00, 3:41:50 19:30, 3:58:54 21:00, 4:15:58 22:30. The recording month is not stated;
the hue suggests the same month throughout, so the series isolates the **time-of-day** brightness curve.

## Background colour by time of day (W)

Average RGB of 12 horizontal bands, top (band 1) to bottom (band 12), sampled 300 s into each chapter.
Bands include the ribbon where it crosses them (bands 6–9 usually).

| Clock | Bands 1–12 (hex RGB) |
|---|---|
| 00:00 | 000007 000007 000008 000009 00000c 00000f 06061c 171732 151736 020527 07092e 0c0d32 |
| 01:30 | 000000 000001 000001 000002 000004 05040e 151428 1d1d32 040519 00011b 020422 080827 |
| 03:00 | 000000 000000 000000 050408 0f0d19 171426 110f23 0c0c22 06031e 0d0728 191034 251b42 |
| 04:30 | 232840 262a42 282b46 2d2f4c 40405f 4a4768 433e61 373156 342b51 392e56 3f315c 44345f |
| 06:00 | 4d5476 4e5478 4d5178 4d4f77 585781 6e6a94 4d4772 403864 413763 463965 4b3b69 4d3c6a |
| 07:30 | 5e658b 5c6289 565b83 51537d 585883 66628e 514b77 423a67 443966 493c69 4e3e6c 503e6d |
| 09:00 | 636b92 5f668e 585d87 51537f 4f4e7b 524f7c 4e4876 574e7d 504574 4a3c6b 4f3f6f 513f6f |
| 10:30 | 656d96 606791 595e8a 515381 4c4c7a 4a4776 4b4575 4e4575 5b4f80 524475 503f71 523f70 |
| 12:00 | 656d96 616792 595e8a 525583 4f4e7d 4c4978 494372 4d4474 554a7a 564879 544375 564375 |
| 13:30 | 666e96 616892 5a5e8b 525482 4b4a79 464372 484272 564d7d 6b5f91 514374 503f71 523f71 |
| 15:00 | 687099 646b95 5d628e 555786 5b5b8a 696696 60598a 483f70 493d6e 4e3f71 534275 554275 |
| 16:30 | 656e95 636a94 5e638f 575a88 5a5a89 7672a2 635d8e 4a4172 4a3e70 504174 554377 584377 |
| 18:00 | 525a7f 545b82 545881 51537e 60608c 6b6895 66608f 4a4170 483c6d 4d3e70 534174 564274 |
| 19:30 | 161832 181935 1a1b39 242445 3c3b5e 3f3c62 2d2951 27224b 2b244f 312855 382d5c 3e3263 |
| 21:00 | 000003 000004 000006 000009 060515 191631 181732 1e1d3d 07052c 0c0a34 171341 231e4d |
| 22:30 | 000007 000007 000008 000009 050412 0b0a1e 12122a 1c1d37 060724 010425 06082c 0a0c30 |

Findings:

- **Two regimes, short transitions.** Day (06:00–18:00) is flat: top ≈ `#656d96` (10:30–16:30), mid ≈ `#4c4978`.
  Night (21:00–03:00) has a near-black upper half. 04:30 and 19:30 are intermediate (≈ 35–45% of day
  brightness). 06:00 and 18:00 are already ≈ 75–85% of midday. Model brightness as a function of clock time
  with ramps around roughly 03:00–06:00 and 18:00–21:00. Midday is not brighter than 10:30 or 15:00.
- **The vertical gradient flips between day and night.** By day the top is brightest and darkens downward.
  By night the top is black and the colour sits in the lower half. A single multiplier on one gradient will
  not reproduce both.
- The hue (blue-lavender) stays constant across the day in this recording; the hue comes from the month.

## Ribbon motion (W, 12:00 segment, 40 s at 1 s steps)

Crest height (fraction of screen height from top, smoothed over 21 px, searched within 25–90%) at 8 evenly
spaced columns, left → right:

```
 0s 0.500 0.456 0.435 0.319 0.272 0.264 0.294 0.341
 7s 0.627 0.581 0.566 0.506 0.420 0.346 0.331 0.312
13s 0.389 0.438 0.489 0.524 0.569 0.531 0.507 0.471
21s 0.486 0.418 0.408 0.403 0.403 0.409 0.422 0.412
29s 0.586 0.579 0.581 0.554 0.503 0.456 0.406 0.365
38s 0.479 0.472 0.510 0.561 0.604 0.639 0.645 0.640
```

- A swell enters at the left and moves right. The peak reaches column 1 at about 7 s, column 4 at about
  12–13 s and column 8 at about 15–16 s, so it crosses the screen in **about 8 s**.
- The next swell follows about **22 s** later (column 1 at about 29 s, column 8 at about 38 s): a cycle of
  about **22–23 s**.
- The crest stays between **≈0.26 and ≈0.65** of screen height and rests near 0.40–0.45 between swells.
- The motion is slow and continuous with no visible frame-to-frame jitter at 1 s sampling. Finer
  timing (sub-second shimmer and the ribbon's internal lines) still needs 60 fps sampling.

## Layout, Original theme, December midday (N, frame at ~5 s)

Fractions of the 1920×1080 frame, read from the screenshot at reduced scale (±0.5% of frame):

| Element | Position |
|---|---|
| Category icon row centre | y ≈ 0.27 |
| Selected category icon centre | x ≈ 0.29 |
| Category spacing | ≈ 0.107 of width |
| Category label under selected icon | y ≈ 0.33 |
| Clock (`12/9 12:05`) | right-aligned near x ≈ 0.88, y ≈ 0.10 |
| Selected item row | y ≈ 0.45 (larger icon, glowing label) |
| First unselected item below | y ≈ 0.62 (≈ 0.17 below selected) |
| Further item rows | ≈ 0.076 of height apart |
| Background (December, midday) | teal-blue, brighter on the left; ribbon in the lower left |

## Navigation timing (N)

**Method.** Frames were downscaled to 192×108, and the mean absolute RGB difference between consecutive
frames was measured in two regions: the category row band (y 14–42%) and the item list band (42–93%).
1. A 10 Hz scan over the full 100 s found motion events.
2. Short isolated events were then re-measured at every frame (60 fps).
3. The ratio of top-band to middle-band change classifies each move: ≥1 is a category (horizontal) move,
   ≤0.75 an item (vertical) move, anything between is mixed.
4. "Settle" is the number of frames until the per-frame change falls below 10% of its peak.

Events that collapse within one frame were discarded. They are page swaps, fades or duplicated encoder
frames, not eased motion. Input timing is not visible in the video, so onset means the first changed frame.

| Event (video s) | Kind | Settle to 10% | Decay time constant |
|---|---|---|---|
| 0.55 | category | 17 frames, 283 ms | 182 ms |
| 24.83 | category | 11 frames, 183 ms | 108 ms |
| 1.28 | mixed | 13 frames, 216 ms | 94 ms |
| 2.12 | mixed | 13 frames, 216 ms | 87 ms |
| 12.02 | item | 16 frames, 266 ms | 169 ms |
| 13.40 | item | 10 frames, 166 ms | 65 ms |
| 14.18 | item | 12 frames, 200 ms | 80 ms |
| 14.97 | item | 11 frames, 183 ms | 111 ms |
| 52.92 | item | 9 frames, 150 ms | 89 ms |

Findings:

- **Every move is an ease-out.** The largest change is in the first frame, then it falls off roughly
  geometrically. There is no ease-in and no constant-speed phase.
- **Duration.** A single move visibly completes in about **150–280 ms** (median about 200 ms). Treated as
  an exponential approach, the time constant is about **65–180 ms** (median about 90 ms). Category moves
  sit at the slow end, where the whole row and the column below it both move.
- **Repeated input.** Back-to-back inputs restart the curve from the current position instead of queueing.
  Profiles show a fresh peak arriving before the previous decay ends, as in the events at 12.0 s and 13.4 s.
- **Caveat.** Pixel-difference energy is a proxy for speed. It also depends on content contrast, so treat
  these numbers as bounds for fitting curves, not as exact curve parameters. Confirm against the user's own
  60 fps recording (`out/native-reference/timing-*`) before finalising.

## Native renderer against these references (October 24)

Applied in `native-xmb/src/view.cpp`:

- **Day/night curve.** Keyframed clock brightness (`View::dayLight`). By day the gradient is brightest at the top;
  by night the upper half goes black and colour stays low.
- **Ribbon swell.** A Gaussian swell crosses in about 8 s and repeats every 22.5 s, on a crest resting near 0.42.
- **Ease-out rates.** Separate time constants: categories 120 ms, items 85 ms, entry/depth 110 ms.

Check with `re-ps3-native.exe --dev-flash <dir> --measure-reference <file>`. It renders the background offscreen at
1920×1080, writes the same band and crest statistics, and saves `video-reference-{midday,night,dusk}.png`.

| Measure | Reference | Native | Status |
|---|---|---|---|
| Top-band brightness by clock time | keyframes above | same keyframes (by construction) | matches |
| Night bottom bands, relative to the midday top | ≈0.11–0.27 (mean ≈0.17) | ≈0.11–0.20 | matches |
| Dawn 04:30 / dusk 19:30, top → bottom | 0.38→0.52 / 0.24→0.45 | 0.38→0.48 / 0.24→0.40 | close |
| Swell crossing time | ≈8 s | ≈7 s | close |
| Swell repeat | ≈22–23 s | ≈23 s | matches |
| Swell crest / resting crest | 0.60–0.645 / 0.40–0.45 | 0.59–0.62 / 0.37–0.44 | close |
| Settle to 10%: category / item / entry | 183–283 / 150–266 / ≈270–330 ms | 276 / 195 / 253 ms | within range |

Still different, and not fixed by constant fitting:

- **Ribbon shape.** The reference is a wide, translucent ribbon made of several soft layers. The native ribbon is one
  thin bright line plus particles. This needs the original lines material (`qgl/lines`) ported, not retuning.
- **Ribbon brightness.** By day the native ribbon raises its bands to ≈1.0–1.3× the top band; the reference raises
  them only slightly (≈0.7× the top band). It follows from the ribbon shape above.
- **Theme hues.** The measured video uses a lavender month palette; the comparison ran with the user's persisted
  Green theme. Only brightness ratios are compared, not hue.
