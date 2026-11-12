# Boot time (ZF4)

Goal: shorten process start to the first presented frame in `--kiosk` (`boot: first frame after N ms`), without changing a
pixel. Measured on the dev PC (Ryzen 5 5600X, display 0 fullscreen, scratch `--data-dir`, `XMB_SINGLE_INSTANCE=0`), with
`work/boot-time.ts`, which runs the variants interleaved so background load hits each alike.

## Result

| Variant | Runs | Median | Range |
|---|---|---|---|
| Before (commit before ZF4) | 7 | 1694 ms | 1532-1898 ms |
| After (all changes) | 7 | 982 ms | 937-1247 ms |
| After with `XMB_BOOT_PREFETCH=0` (only the deferral and the memoised icon path) | 7 | 1396 ms | 1309-1548 ms |

The first "after" run (1247 ms) is the cold one: the ribbon settle cache did not exist yet. Later runs read it. Gain: about 710 ms
(42 %) at the median. The 2026-11-09 figure of about 1.9 s was taken on a busier machine; the same binary measures 1.6-1.7 s here.
A busy machine moves every number (another process at 100 % CPU turned 1.0 s into 9 s), so only interleaved comparisons count.

## Where the time went (XMB_BOOT_PROFILE=1)

`XMB_BOOT_PROFILE=1` logs `boot-profile: <span> N ms` lines (`src/boot_profile.h`): sequential marks on the main thread plus
`prefetch ...` spans on workers. Off by default.

Before, one typical run (1.7 s), main thread, in order:

| Span | ms |
|---|---|
| QApplication ready | 50 |
| `Assets` constructor (firmware font registration) | 28 |
| `Shell` constructor: audio wiring (first host snapshot, ~140 ms cold, the token check) | 100-140 |
| `Shell` constructor: weather row icon (`WeatherClient::icon`: path clipper) | 270-315 |
| `Shell` constructor: Tailscale icon from `tailscale-ipn.exe` + status process start | 55 + 38 |
| `Shell` constructor: everything else (media columns, RCO strings, Spotify, Home) | ~60 |
| `View` constructor: category icon decode 38, `QLabel` + first font database use 100-200, OSK layout 18 | ~210 |
| Window create + WGL context (driver load, 4x MSAA format) up to `initializeGL` | 480-520 |
| `initializeGL`: ribbon init 80 (waits for the cloth warm-up), the rest ~40 | ~125 |
| First `paintGL` (UI FBOs + `drawScene`: glyphs, textures) | ~290 |
| Show to swap | ~70 |

The ribbon's 41,550-step cloth warm-up (730-800 ms) already ran on its own thread, started in the `View` constructor. It was only
hidden because the earlier work took longer; it became the critical path once other work shrank.

## Changes

1. **Ribbon settle cache and early start** (`ribbon.cpp`, `boot_prefetch.cpp`). The warm-up is a pure function of the compiled day
   preset. It now starts at process start, before the `Assets`/`Shell` build, and its result (the `Cloth` struct, raw bytes) is
   stored in `<data>/cache/ribbon-settle.bin`. The key covers the preset fields, the step count, the grid size and the
   executable's size and time (any rebuild recomputes once). A short, foreign or damaged file (size and FNV checksum) is ignored.
   A hit is bit-identical to computing. `XMB_RIBBON_SETTLE_SYNC=1` still forces the old synchronous path.
2. **First host snapshot on a worker** (`host.cpp`). `startEarlySnapshot()` reads the machine state at process start; the first
   `host::snapshot()` on the GUI thread joins it (it used to be taken synchronously by the audio wiring). An `invalidateSnapshot()`
   in between discards it.
3. **Weather icon prepared on a worker** (`weather_client.cpp`). `WeatherClient::icon` is memoised under a lock, a worker draws
   the row's icon while the shell is built, and the clipper results (`keepOut`, `drawBehind`) are remembered per thread so the
   shadow and main pass stop repeating them (140 ms per call). Same paths, same pixels.
4. **Service icons on a worker** (`boot_prefetch.cpp`). `serviceIcons::tailscale()` and `spotify()` are function-local statics; a worker
   initialises them and the shell's first call waits for that instead of repeating it. In the A/B this was within noise
   (about -20 ms at best); it is kept because it is free.
5. **OpenGL driver warm-up** (`boot_prefetch.cpp`). A throwaway context on a worker loads the driver and picks the 4x MSAA pixel
   format before the window needs it (about 100 ms off the median in the ablation). The window's context must not be created while
   the warm-up context is being created: a first version let them overlap and the driver crashed in `glGetIntegerv`
   (2 of 4 `ab-layers` runs). `joinOpenGlWarmup()` runs before the main window object is created.
6. **Tailscale status process after the first frame** (`tailscale_shell.cpp`, `boot::afterFirstFrame`). Starting `tailscale status`
   cost ~38 ms of the GUI thread. The answer was always asynchronous and nothing of it is drawn by the first frame. Queued jobs
   run on the first buffer swap, or after 5 s if none arrives.

A font-database warm-up (`QFontDatabase::families()` on a worker) was tried and removed: it moved the cost from the `QLabel` to the
`Assets` constructor and gained nothing.

`XMB_BOOT_PREFETCH=0` turns jobs 1, 2, 3, 4, 5 off (for an A/B or a support case); `XMB_BOOT_PREFETCH=ribbon+snapshot+gl+weather+icons`
picks a subset (comma or plus separated). Prefetch runs for an interactive start and for `--script` runs (so the gate covers it);
report modes start none of it. Safe mode keeps its network rules: the deferred Tailscale refresh
still checks `safeMode()`.

## Determinism

No job changes an input to a frame: the cache holds the exact bytes of a computation with fixed inputs; the snapshot is the same
read, taken earlier; the memoised icon and the remembered clipper path are the same values; the Tailscale refresh is invisible at the
first frame. `work/ab-layers.ts` against the pre-change binary:

| State | Before binary | After binary |
|---|---|---|
| music-player-page | PASS, inf / 1.0000 | PASS, inf / 1.0000 |
| photo-gallery-piles | PASS, inf / 1.0000 | PASS, inf / 1.0000 |
| steam-download-page | PASS, 98.43 / 1.0000 | PASS, 98.43 / 1.0000 |
| continue-watching | PASS | PASS (6 of 6 after the crash fix) |
| auto-off-guide | FAIL 24.47 / 0.9655 (reference drift in this tree) | FAIL 24.47 / 0.9655, identical |
| xmb-volume-hud | FAIL 20.21 / 0.9054 (reference drift in this tree) | FAIL 20.21 / 0.9054, identical |

The two FAILs are identical to four digits on both binaries, so they come from the reference or other tree changes, not from ZF4.

## What is left

Remaining main-thread cost at about 980 ms: `Assets` font registration plus first font database use (~160 + 80 ms, serial because the
driver warm-up holds the loader lock during part of it), window and WGL context creation (~95 ms), the first `drawScene`
(~240 ms: glyph rasterisation and texture decode on first use, per-thread Qt font caches so a worker cannot warm them),
`View` category image decode (~35 ms) and the audio output wiring (~60 ms). Ideas: decode the first-frame images on workers
(`Assets::image` is not thread-safe today), a glyph-atlas cache for the first screen, and creating the `QAudioOutput` after the
first frame. A real sign-in boot (Windows logon, shell start, GPU already busy) is still unmeasured here.
