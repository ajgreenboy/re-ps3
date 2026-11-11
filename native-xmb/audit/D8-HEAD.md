# D8 options structure: HEAD recheck (2026-11-11)

Source baseline: `023f7e7`. **Neither recorded D8 delta reproduces. No rendering or row-builder change is
needed.** The verdict's original measurements describe `07a4fe4`; later corrections already appear in
`FIDELITY-SURFACES.md` sections 14.2, 15.4 and 16. This recheck preserves the ribbon/background and all
at-ceiling surfaces. Existing uncommitted ribbon work is not part of this change.

## Row sets and context

- `src/shell.cpp:2676-2682,2802-2852`: Game title actions (`Launch`, `LaunchSteam`, `LaunchApp`) already
  build Sort By, Group Content when the page is groupable, Start, Delete and Information. The first action
  has `dividerBefore`. Steam titles additionally carry real Steam hand-off commands; these are intentional
  native extensions, not missing firmware rows.
- Fresh scripted route: Game -> All Games -> All Titles -> Triangle. The dump has depth 2, entry
  `steam:2807960`, **nine rows in two groups**: `Sort By|Group Content`, then
  `Start|Open in Steam Library|Store Page|Screenshots|Verify Game Files|Delete|Information`.
  Evidence: `out/d8/game-options.png` and
  `C:\Users\Albert\AppData\Local\Temp\xmb-layers-state-KMzygf\entries.json.options.json`.
- `src/shell.cpp:2700-2710`: grouping requires a titled multi-row page; provider/form/download/Continue
  Watching pages are excluded. `src/shell.cpp:2854-2865` adds Sort By to a sortable filesystem page,
  not a universal Group Content row. The existing `video-options-panel` recipe asserts exactly
  `Sort By|Open|Copy|Delete|Information` on the Movies folder in Video Library
  (`work/ab-layers.thresholds.json:254-274`). User, playlist and Spotify rows have their own branches.
- Existing actions are not fabricated: Group Content uses first-letter folders with reversible All
  (`optionGroupable`, `groupLetter`, `buildGroupContentMenu`); date sorting is disabled with a reason if no row
  has file dates (`src/shell.cpp:3156-3169`). Game deletion without a backend is disabled with a reason
  (`src/shell.cpp:2838-2849`); Steam deletion hands off to Steam.

Firmware check: `category_game.xml`, `category_video.xml`, `category_music.xml`, `category_photo.xml`
under `C:\dev\rpcs3\bin\dev_flash\vsh\resource\explore\xmb` describe category queries and item/plugin
bindings, not a universal five-row Triangle menu. None contains `msg_sort_by`, `msg_group_content`,
`msg_sort` or `msg_folder_class`. Inspecting the English text table of `explore_plugin_full.rco` with
`bun work/rco-text-inspect.ts` confirms the actual keys **`msg_sort` = Sort By** and
**`msg_folder_class` = Group Content**, plus `msg_game_start`, `msg_del`, `msg_info`.
`tex_quit` is the in-game Quit Game row, not an icon to add to these utility rows
(`src/shell.cpp:2491-2515`). The menu's actual chevrons/badge are already drawn by the view; the plate is
`system_plugin/tex_optionmenu_bg`. No firmware assets are copied into this commit.

The measured console reference is `C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\menu-options-open.png`
(Corrupted Data), not a Video Library folder reference. It does **not** establish a rule that every
media/provider/folder row must carry Group Content. No additional rows are forced into unmeasured contexts.

## Fresh geometry measurement

Decoded PNGs with `work/ab-compare.ts` (`decodePng`); Rec.709 luma, ink >140 with at least four pixels per
scanline in x=1330..1549. The Game-title capture's extra Steam commands change subsequent labels, so only
the two utility rows and first primary row are compared here.

| quantity | console reference | fresh HEAD Game-title capture |
|---|---|---|
| Sort By ink y | 301..318, descender 321..322 | 302..324 |
| Group Content ink y | 341..359 | 342..360 |
| Start ink y | 539..560 | 539..559 |
| Group Content -> Start ink-top distance | 198 px (historical audit rounds to 199) | 197 px |
| band left edge | 1320 | 1320 |
| y100 luma at x1319 -> 1320 -> 1321 | 84.72 -> 89.86 -> 100.58 | 87.58 -> 98.30 -> 98.65 |

The strongest y100 edge jump over x1300..1404 is x1321 in the console (edge filtering also lights x1320),
and x1320 in HEAD. There is no +66 px offset. `src/view.cpp:2039-2043` sets **both** `x0=1320` and
`plateX=1320`, with label inset 14. Moving this another -66 px would introduce a defect.

`src/view.cpp:2040,2282-2300` already sets `groupGap=238`, pitch 40.5 and first primary centre 549.5.
With two utility rows the centres are 311.5, 352 and 549.5: the last utility-to-primary distance is
197.5 px, matching the measured roughly 199 px inter-group separation. **238 is the group-origin offset,
not the last-row-to-next-row gap**; replacing it with 199 would incorrectly compress the menu.

## Validation

- Requested `native-xmb/build.ps1 -OutputDirectory C:/dev/xmb/out/native-xmb`: PASS; printed
  `Built C:\dev\xmb\out\native-xmb\re-ps3-native.exe`.
- Targeted `--state video-options-panel`: PASS, frame and panel both PSNR infinity / SSIM 1.0000;
  fresh capture `C:\Users\Albert\AppData\Local\Temp\xmb-layers-state-KKfxlj\native.png` is identical to
  the existing reference. Its y100 edge is also x1320. Targeted `--state continue-watching`: PASS.
- Full `bun work/ab-layers.ts --all --exe C:\dev\xmb\out\native-xmb\re-ps3-native.exe`:
  stored pair **6/6**, canonical capture **6/6** (5 gated passes, 1 informative), named states **21/22**.
  The only failure is the pre-existing environmental `steam-download-page` (frame 33.84 dB / 0.9962,
  rows 32.22 dB / 0.9925); left untouched as requested. Overall command exits 1 for that failure.
  `video-options-panel` passes with infinity / 1.0000 in both regions. The first full invocation hit
  the shell's 120-second timeout during discovery; these counts are from the completed timeout-free run.
- No named-state references refreshed: no intended rendering changes, and the extra Game-title capture
  is an ignored local diagnostic under `out/d8`, not a replacement parity reference.

Deferred: broadening Group Content to unmeasured media/provider contexts (no reference proves their row
sets); nested-menu plate fidelity and other non-D8 gaps; the known live-Steam-download environmental
failure. No background, ribbon, or at-ceiling element was changed.
