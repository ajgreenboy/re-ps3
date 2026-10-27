# Info-bar continuation — 2026-10-25

The saved 1920×1080 firmware root capture (`out/native-reference/ab-rpcs3-root.png`) places the bar at approximately (1329,75), size 591×58, with a clock icon centered near (1791,104). Native previously drew a 430×88 bar at (1490,60) and omitted the analog clock.

Native now uses the measured bounds and an analog minute/hour clock. `Shell::clockDateTime()` is shared by the text and clock hands, including the selected time zone and capture override. Cache invalidation follows the displayed minute. The original `tex_infobar` strip is composited at restrained opacity with a subtle outline; opacity/outline and procedural clock are measured approximations, not traced firmware materials or geometry.

The new 1600×900 scripted captures show the corrected proportions and clock placement. Runtime PAF projection, exact clock geometry, typography phase and a matched firmware pixel score remain open. Do not replace this geometry with zero-valued dynamic RCO template fields. Read-only infobar initialization traces remain under `out/native-reference/phase3-infobar-*.c`.

## Clock format: US date and 12-hour time (2026-10-26)

`Shell::clockText()` is the single source for the information-panel clock and every other clock surface,
and it now composes the string from the console's own two options instead of a fixed format:

| key | 0 | 1 (default) | 2 |
|---|---|---|---|
| `clock/dateFormat` | DD/MM | MM/DD | YYYY/MM/DD |
| `clock/hourFormat` | 24-hour | 12-hour | - |

A fresh data directory is therefore a US console: `MM/DD h:mm AM/PM` in the information panel
(`10/25 12:32 PM`, `10/25 7:05 PM`) and `MM/DD/YYYY h:mm:ss AM/PM` on the Date and Time screen. Why that
composition: the board carries the short date plus the time (the reference frame carries `25/10 12:32`,
11 characters, in the same slot), both date fields are zero-padded because the firmware's own option label
is `MM/DD/YYYY` and its reference frame pads its day (`25/10`), and the hour is not padded because that is
the US 12-hour convention. The order comes from the stored preference, so Settings > Date and Time >
Date Format and Time Format still offer DD/MM/YYYY, MM/DD/YYYY, YYYY/MM/DD and 24- or 12-hour; those rows
were already wired to `clockText()`, only their defaults were non-US.

The Date and Time screen's detail line used `QDateTime::currentDateTime()` and a hardcoded English format,
so it ignored both `clock/timeZone` and `--ui-clock`. It now formats `clockDateTime()` with the same
preference-driven full date/time, which makes that screen a pure function of `clock/dateFormat`,
`clock/hourFormat`, `clock/timeZone` and the capture override.

### One space, not two

The date and time were joined with two spaces. The reference's word gap is one space. Bright-glyph columns
of the info-bar crop (x 1400..1772, y 80..135) measure:

| frame | glyph bbox | date-to-time gap |
|---|---|---|
| firmware reference `ab-rpcs3-root.png` | 169 px | 16 px |
| native, two spaces | 166 px | 25 px |
| native, one space | 175 px | 16 px |

The one-space render matches the reference gap exactly; the residual 6 px of width is font advance, not
spacing. Layer score for the same DD/MM + 24-hour setting, two captures that differ only in that
character (offline scoring with `bun work/ab-layers.ts --native`):

| variant | clock text | info bar |
|---|---|---|
| two spaces | 13.66 dB / 0.6198 | 17.26 dB / 0.7098 |
| one space | 13.61 dB / 0.6226 | 17.21 dB / 0.7114 |

So the fix corrects the measured geometry but does not move the clock layer score: -0.05 dB PSNR,
+0.0028 SSIM (SSIM is the one that improves). That crop is dominated by glyph advance and position, not
by the space.

### Deliberate deviation from the stored reference

The stored RPCS3 frame was captured on a DD/MM, 24-hour console, so the US default no longer matches its
string and the two clock layers move by design:

| layer | stored baseline | US default | delta |
|---|---|---|---|
| clock text | 13.51 dB / 0.6131 | 11.39 dB / 0.4105 | -2.12 dB / -0.2026 |
| info bar | 17.12 dB / 0.7046 | 14.78 dB / 0.6117 | -2.34 dB / -0.0929 |

Every other layer is unchanged (frame -0.07 dB, inside the 0.5 dB budget), which confirms the change
touches only the clock string. Setting the preferences back to DD/MM + 24-hour reproduces the reference's
own `25/10 12:32` and scores 13.61 dB / 0.6226 and 17.21 dB / 0.7114, so the difference measures the new
default, not a regression. All four named-state self-baselines were captured with the pinned
`uiClock 2026-10-25T12:32:00` and contain the old string, so they now fail their 55 dB gate
(`steam-game-root`: 34.28 dB / 0.9970 while its item column stays bit-identical); refresh them
deliberately with `bun work/ab-layers.ts --states --save-reference` once the format is accepted.
