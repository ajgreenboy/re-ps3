# Info-bar continuation — 2026-10-25

The saved 1920×1080 firmware root capture (`out/native-reference/ab-rpcs3-root.png`) places the bar at approximately (1329,75), size 591×58, with a clock icon centered near (1791,104). Native previously drew a 430×88 bar at (1490,60) and omitted the analog clock.

Native now uses the measured bounds and an analog minute/hour clock. `Shell::clockDateTime()` is shared by the text and clock hands, including the selected time zone and capture override. Cache invalidation follows the displayed minute. The original `tex_infobar` strip is composited at restrained opacity with a subtle outline; opacity/outline and procedural clock are measured approximations, not traced firmware materials or geometry.

The new 1600×900 scripted captures show the corrected proportions and clock placement. Runtime PAF projection, exact clock geometry, typography phase and a matched firmware pixel score remain open. Do not replace this geometry with zero-valued dynamic RCO template fields. Read-only infobar initialization traces remain under `out/native-reference/phase3-infobar-*.c`.
