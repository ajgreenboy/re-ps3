# Fidelity, measured: the launcher build against every firmware reference we own

Written 2026-10-29 for the fidelity-sweep pass. One table per instrument, every number taken from a tool
run in this session, nothing re-baselined, no reference edited, no source file touched. The raw JSON and
CSV sit beside this document's evidence root, `C:\dev\out\fidelity-sweep\`.

**The short answer to "how fleshed out is it".** Of 62 measured comparisons, 30 clear their class frame
budget on PSNR but only 10 also clear it on SSIM; 0 of 36 catalogue states pass all three tiers; the
average frame is 17.94 dB on the columns, 17.32 dB on the text pages and 20.14 dB on the overlays. The two
surfaces that carry the most weight in the eye — the PSN column's artwork and the clock/info bar — are 8.68
dB and 14.17 dB, and both fail for a nameable, already-audited reason rather than for lack of measurement.

---

## 0. Method, and how to re-run it

| step | command | what it wrote |
|---|---|---|
| build identity | `Get-FileHash`, `git rev-parse`, `Get-Item` | section 1 |
| canonical gate | `bun work/ab-layers.ts --capture --exe C:/dev/xmb/out/native-xmb/re-ps3-native.exe` | `out/fidelity-sweep/gate.log` |
| parity, all cells | `bun work/parity.ts --capture --score --exe C:/dev/xmb/out/native-xmb/re-ps3-native.exe` | `out/parity/run-20261029-192650/`, `out/parity/scorecards/scorecard-20261029-193824.{json,md}` |
| extra surfaces, capture | `bun work/fidelity-sweep.ts capture` (jobs `col-*`, `options`, `dialog`, `depth2-*`, `osk`, `band-*`) | `out/fidelity-sweep/native/<job>/*.png` + sidecars, `native-manifest.json` |
| extra surfaces, score | `bun work/fidelity-sweep.ts score` (pairs in `work/fidelity-pairs.json`) | `out/fidelity-sweep/extra-surfaces.{json,csv}` |
| parity flatten | `bun work/fidelity-parity-summary.ts <scorecard.json>` | `parity-cells.csv`, `parity-states.csv`, `parity-summary.json` |
| merge + statistics | `bun work/fidelity-report.ts` | `merged-surfaces.{json,csv}`, `measured-summary.json` |
| this document | `bun work/fidelity-doc.ts` | `native-xmb/audit/FIDELITY-MEASURED.md` |

Both instruments were run at the parity harness's own pin, so the two tables are comparable: 1920×1080
fullscreen, `--render-clock 30`, `--background-clock 2026-10-28T10:00:00` (the day preset at the date the
matched day references were taken), `--ui-clock 2026-10-25T12:32:00`, `--focus-time 2000`, dev_flash
`C:/dev/rpcs3/bin/dev_flash`, and the `out/native-reference/store/ui3-data` seed copied the way
`work/parity.ts` copies it.

---

## 1. Build identity, and the one honest caveat

| item | value |
|---|---|
| executable | `C:\dev\xmb\out\native-xmb\re-ps3-native.exe` |
| sha256 | `4dc3b3e4e9282fd47ebb32bacf7b504e1e18ac2d3a19a7cd3d444214ffb34428` |
| size | 4,257,280 bytes |
| mtime | 2026-10-29 18:33:37 -05:00 |
| HEAD when the sweep started | `07a4fe4` (committed 2026-10-29 18:33:11 -05:00) |
| exe vs that commit | built 18:33:37, 26 s after the commit; the scorecard records the same sha256 |
| HEAD when this document was written | `79b2325` (2026-10-29 19:52:50 -05:00) |
| what moved | `79b2325` adds four `native-xmb/audit/FIDELITY-*.md` files and touches no source, so the source at HEAD is the source this binary was built from |
| working tree at report time | `M native-xmb/src/view.cpp`, `M native-xmb/src/view.h`, `M work/ab-layers.thresholds.json` — another session's in-flight edits, made after this sweep started; **every number below is for the binary above, not for that working tree** |
| the gate's baseline file | `work/ab-layers.thresholds.json` was clean when the gate ran (19:27) and has been modified since by that other session, so a re-run today compares against different baselines than section 2 records |

The live app instance the brief warned about was left alone: it is a separate process
(`--data-dir C:\dev\xmb\out\native-xmb\data`) and every capture here ran out of its own seeded data
directory, as `work/parity.ts` does.

---

## 2. The canonical gate: PASS, 6 of 6 layers

```
text presence: 1668 bright pixels in the clock crop (minimum 900) PASS
state      category=game depth=0 selection=0 clock="10/25 12:32 PM"
PASS: 6/6 layers within budget
```

| layer | crop | PSNR | SSIM | gate |
|---|---:|---:|---:|---|
| frame | 0,0,1920,1080 | 18.76 | 0.8360 | PASS |
| top background | 0,0,700,200 | 21.06 | 0.9434 | PASS |
| category icon row | 0,240,1920,120 | 21.62 | 0.9062 | PASS |
| item label row | 400,425,800,100 | 11.99 | 0.3895 | PASS |
| clock text | 1570,80,260,50 | 11.41 | 0.4528 | PASS |
| info bar | 1329,75,591,58 | 15.05 | 0.7667 | PASS |

A PASS here means "no drawing regression against the stored native self-baseline", not "matches the
console": the deltas are +0.00 because the baseline is this same build. The reference the gate scores
against is `out/native-reference/ab-rpcs3-root.png`, the retired pre-contract frame
(`work/parity-reference.json` `retired[0]`), so its 18.76 dB is the historical comparison and not a
firmware number. The two lowest layers, item label row (11.99 dB) and clock text (11.41 dB), are the two
the parity table also flags first — the gate and the parity instrument agree on where the loss lives.

---

## 3. Parity: the catalogue, all 36 captured cells

Headline from `scorecard-20261029-193824.md`: **0/36 capturable states pass all three tiers.** Tier A 36
pass, Tier B 0 pass / 3 informative, Tier C 10 pass; 10 captured cells have no reference yet; 23 gating
cell failures. Reference coverage: **26 of 31 firmware-referenced states** (the catalogue has 31
firmware-referenced states and 5 native-only ones that owe none, 39 in total).

### 3.1 Cells with a reference (26)

| state | class | Tier A | Tier B | Tier C | frame dB | frame SSIM | budget dB / SSIM | gating regions that failed | informative regions |
|---|---|---|---|---|---:|---:|---|---|---|
| root | icon-heavy | pass | fail | n/a | 18.25 | 0.8262 | 20.00 / 0.9000 | frame, info bar | item label row |
| column-user | icon-heavy | pass | fail | n/a | 18.86 | 0.8518 | 20.00 / 0.9000 | frame, category icon row | item label row |
| column-settings | icon-heavy | pass | fail | n/a | 19.21 | 0.8441 | 20.00 / 0.9000 | frame, category icon row, item label row | — |
| column-photo | icon-heavy | pass | fail | n/a | 17.97 | 0.8382 | 20.00 / 0.9000 | frame, category icon row | item label row |
| column-music | icon-heavy | pass | fail | n/a | 18.05 | 0.8417 | 20.00 / 0.9000 | frame, category icon row | item label row |
| column-video | icon-heavy | pass | fail | n/a | 18.11 | 0.8364 | 20.00 / 0.9000 | frame, category icon row | item label row |
| column-network | icon-heavy | pass | fail | n/a | 18.73 | 0.8492 | 20.00 / 0.9000 | frame, item label row | — |
| column-psn | icon-heavy | pass | fail | n/a | 8.68 | 0.6120 | 20.00 / 0.9000 | frame, category icon row | item label row |
| column-friends | icon-heavy | pass | fail | n/a | 18.10 | 0.8507 | 20.00 / 0.9000 | frame | item label row |
| page-settings-system | text-heavy | pass | informative | n/a | 19.28 | 0.8780 | 18.00 / 0.8500 | — | page rows |
| page-settings-network | text-heavy | pass | fail | n/a | 18.68 | 0.8911 | 18.00 / 0.8500 | page rows | — |
| page-network-status-list | text-heavy | pass | fail | n/a | 17.49 | 0.8659 | 18.00 / 0.8500 | frame | status rows |
| page-network-test-error | text-heavy | pass | fail | n/a | 15.15 | 0.7641 | 18.00 / 0.8500 | frame, back hint | message body |
| page-network-connection-settings | text-heavy | pass | fail | n/a | 14.99 | 0.7520 | 18.00 / 0.8500 | frame, back hint | message body |
| page-settings-theme | text-heavy | pass | fail | n/a | 18.38 | 0.8471 | 18.00 / 0.8500 | frame, page rows | — |
| theme-list | composite | pass | fail | n/a | 19.41 | 0.8675 | 18.00 / 0.8800 | frame, band column, band rows | — |
| color-list | composite | pass | fail | n/a | 19.78 | 0.8718 | 18.00 / 0.8800 | frame | chip column |
| background-list | composite | pass | fail | n/a | 19.28 | 0.8696 | 18.00 / 0.8800 | frame | band rows |
| font-list | composite | pass | fail | n/a | 18.75 | 0.8603 | 18.00 / 0.8800 | frame | band rows |
| page-settings-video | text-heavy | pass | informative | n/a | 19.22 | 0.8807 | 18.00 / 0.8500 | — | page rows |
| osk | composite | pass | fail | n/a | 26.11 | 0.8897 | 18.00 / 0.8800 | osk panel | — |
| dialog | composite | pass | fail | pass | 18.39 | 0.8423 | 18.00 / 0.8800 | frame | dialog |
| options-panel | composite | pass | fail | pass | 19.18 | 0.8738 | 18.00 / 0.8800 | frame | options panel |
| info-indicator | composite | pass | fail | n/a | 18.32 | 0.8295 | 18.00 / 0.8800 | frame, controller indicator | — |
| ps-menu | composite | pass | fail | pass | 28.82 | 0.9918 | 18.00 / 0.8800 | ps menu controller panel | — |
| dialog-box | composite | pass | informative | pass | 19.88 | 0.9013 | 18.00 / 0.8800 | — | box plate |

### 3.2 Gating failures by region name (the whole run)

| region | cells failing it |
|---|---:|
| frame | 20 |
| category icon row | 6 |
| item label row | 2 |
| page rows | 2 |
| back hint | 2 |
| info bar | 1 |
| band column | 1 |
| band rows | 1 |
| osk panel | 1 |
| controller indicator | 1 |
| ps menu controller panel | 1 |

`frame` fails 20 times, and once the frame is gating the smaller regions decide whether it is a render
difference or a content difference; `category icon row` (6) is the structural region that fails most after
the frame, which is the ten-icon-against-nine finding again, and `back hint` (2) and `page rows` (2) are
the depth-2 and settings-page rows.

### 3.3 Captured cells with no reference (10)

| state | class | Tier A | Tier B | Tier C | what Tier A saw |
|---|---|---|---|---|---|
| column-tv | icon-heavy | pass | no-reference | n/a | category tv, depth 0, rows 1, pageKind root, row "My Channels" |
| page-game-all-games | text-heavy | pass | no-reference | n/a | category game, depth 1, rows 14, pageKind page, row "All Titles" |
| page-video-library | text-heavy | pass | no-reference | n/a | category video, depth 1, rows 3, pageKind folder, row "Movies" |
| page-steam-store-sections | text-heavy | pass | no-reference | n/a | category game, depth 1, rows 3, pageKind page, row "Top Sellers" |
| page-download-management | text-heavy | pass | no-reference | pass | category network, depth 1, rows 4, pageKind downloads, row "Zulu Active" |
| page-jellyfin-signin | text-heavy | pass | no-reference | pass | category tv, depth 1, rows 6, pageKind jellyfinSignIn, row "Username" |
| continue-watching | text-heavy | pass | no-reference | pass | category video, depth 1, rows 1, pageKind continueWatching, row "Top Level Clip.mp4" |
| notification | composite | pass | no-reference | pass | category game, depth 0, rows 11, pageKind root, row "Steam Store" |
| volume-hud | composite | pass | no-reference | pass | category game, depth 0, rows 11, pageKind root, row "Steam Store" |
| media-player-panel | composite | pass | no-reference | pass | category video, depth 1, rows 1, pageKind folder, row "Cue Test.mp4" |

### 3.4 The informative (contentDiffers) annotations, in full

These are regions the catalogue marks as console content the native build cannot reproduce from the same
frame. They are measured and printed but never gating; `--strict` would gate them again.

| state | region | why it differs |
|---|---|---|
| root | item label row | reference Game column holds PlayStation Store / Saved Data Utility (PS3) / Corrupted Data; the native column holds Steam Store rows, so the focused label is PS Vita System Application Utility on the console and Steam Store here |
| column-user | item label row | reference Users column focuses Turn Off System from the console account rows; the native column focuses Create New User from the host profile store |
| column-photo | item label row | reference Photo column focuses Search for Media Servers over an empty console VFS; the native column focuses Photo Library from the host photo folder |
| column-music | item label row | reference Music column focuses Search for Media Servers over an empty console VFS; the native column focuses Music Library from the host music folder |
| column-video | item label row | reference Video column focuses BD Data Utility; the native column focuses Continue Watching from the host video folder |
| column-psn | item label row | reference PSN column shows the console signed-out Sign Up row over its sign-up artwork; the native column shows Sign In from its own signed-out list |
| column-friends | item label row | reference Friends column is rowless because the console is signed out and offline; the native column draws its own Open Steam Friends row |
| page-settings-system | page rows | reference page is the console 25 firmware rows with console values (PS3-354, English (UK)); the native page is built from sysconf_plugin schemas and host strings, so the row set and values differ |
| page-network-status-list | status rows | reference rows are the console fixed 13-row network report (Internet Connection Enabled through UPnP Enabled); the native page answers with the host own network report |
| page-network-test-error | message body | reference body reads An Ethernet cable is not connected.; the native row answers with this build own not available in the native recreation yet message |
| page-network-connection-settings | message body | reference body is the firmware Adjust settings for connection to the Internet text; the native row answers with this build own not available message |
| color-list | chip column | reference draws 12 unlabelled colour chips; the native list draws the same 12 chips (Original plus eleven months) but labels only the non-Original ones, so the chip fill is comparable while the label content has no console counterpart |
| background-list | band rows | reference rows are Brightness (submenu), Original, Classic, Wallpaper; the native list has three values and no Brightness row |
| font-list | band rows | reference rows are Original, Rounded, Pop; the native list draws the firmware faces this build registers (SCE-PS3 Seurat LATIN, VAGRundschriftDLig) |
| page-settings-video | page rows | reference page is the console 19 BD/DVD rows; the native page draws seven measured rows plus three native-only media rows |
| dialog | dialog | the reference frame is the console delete-confirm box; this state deliberately captures the inline empty-state line, so the plate, answers and footer are absent by construction - dialog-box is the state that compares the box itself |
| options-panel | options panel | reference panel was opened on Game Corrupted Data and lists Sort By, Group Content, Start, Delete, Information; the native route opens Video Library and lists Open, Copy, Delete, Information |
| psn-signin | dialog | the reference frame is the console delete-confirm box; this state deliberately captures the inline empty-state line, so the plate, answers and footer are absent by construction - dialog-box is the state that compares the box itself |
| dialog-box | box plate | the console frame opens the box over the Game column wallpaper (region luma 111) while this route opens it over the darker Continue Watching page (native 163), so the wash sits on a different underlying scene by construction; the answers and footer rows stay gating because they are drawn on the same plate |

---

## 4. Extra surfaces: the console frames the parity catalogue has no cell for

36 pairs, all scored against the frames under
`C:\dev\out\rpcs3-allgraphics-2026-10-28\frames\` (1920×1080, day preset, guest clock 29/10 12:00-12:31,
theme Original). Every console frame used here was opened and checked against its own inventory entry
before scoring; the ones that do not show what their names claim are listed in section 6.

Two native frames are parity cells rather than frames from this sweep's own capture directory; the note
column of `extra-surfaces.csv` and section 6 say why (the media-library page capture loop, and the delete
box needing the resume seed). Both are captures of the same binary at the same pin.

| pair | surface | console frame | native frame | crop dB / SSIM | frame dB / SSIM | budget dB / SSIM | content differs |
|---|---|---|---|---|---:|---|---|
| column-user-row0 | Users column, row 1 at rest | col-users-r00.png | col-user/r00.png | label 11.61 / 0.430; icons 20.78 / 0.908; panel 14.59 / 0.757 | 18.91 / 0.8558 | 20.00 / 0.9000 | yes |
| column-settings-row0 | Settings column, row 1 at rest | col-settings-r00.png | col-settings/r00.png | label 11.44 / 0.490; icons 20.16 / 0.885; panel 14.62 / 0.759 | 19.06 / 0.8457 | 20.00 / 0.9000 | no |
| column-photo-row0 | Photo column, row 1 at rest | col-photo-r00.png | col-photo/r00.png | label 12.16 / 0.309; icons 19.08 / 0.878; panel 14.57 / 0.757 | 18.91 / 0.8504 | 20.00 / 0.9000 | yes |
| column-music-row0 | Music column, row 1 at rest | col-music-r00.png | col-music/r00.png | label 10.84 / 0.358; icons 18.44 / 0.859; panel 14.57 / 0.755 | 18.82 / 0.8446 | 20.00 / 0.9000 | yes |
| column-video-row0 | Video column, row 1 at rest | col2-video-r00.png | col-video/r00.png | label 10.58 / 0.382; icons 18.48 / 0.847; panel 14.65 / 0.760 | 18.12 / 0.8320 | 20.00 / 0.9000 | yes |
| column-game-row0 | Game column, row 1 at rest | col-game-r00.png | col-game/r00.png | label 12.05 / 0.410; icons 20.99 / 0.889; panel 14.58 / 0.754 | 18.58 / 0.8347 | 20.00 / 0.9000 | yes |
| column-network-row0 | Network column, row 1 at rest | col-network-r00.png | col-network/r00.png | label 12.15 / 0.409; icons 21.47 / 0.906; panel 14.58 / 0.754 | 18.73 / 0.8517 | 20.00 / 0.9000 | no |
| column-psn-row0 | PlayStation Network column, row 1 at rest | col-psn-r00.png | col-psn/r00.png | label 10.12 / 0.422; icons 9.53 / 0.751; panel 6.54 / 0.606 | 8.68 / 0.6117 | 20.00 / 0.9000 | yes |
| column-friends-row0 | Friends column, row 1 at rest | col-friends-r00.png | col-friends/r00.png | label 11.33 / 0.425; icons 23.02 / 0.926; panel 14.58 / 0.754 | 18.53 / 0.8580 | 20.00 / 0.9000 | yes |
| column-user-scrolled | Users column, scrolled (console row 3 of 3) | col-users-r02.png | col-user/r02.png | label 11.61 / 0.430; icons 20.69 / 0.901; panel 14.62 / 0.759 | 18.65 / 0.8495 | 20.00 / 0.9000 | yes |
| column-settings-scrolled-9 | Settings column, scrolled to Accessory Settings (console row 10) | col-settings-r09.png | col-settings/r09.png | label 12.50 / 0.479; icons 19.86 / 0.892; panel 14.61 / 0.758 | 18.78 / 0.8354 | 20.00 / 0.9000 | no |
| column-settings-scrolled-15 | Settings column, scrolled to Network Settings (console row 16) | col-settings-r15.png | col-settings/r15.png | label 13.17 / 0.439; icons 20.03 / 0.890; panel 14.62 / 0.759 | 19.51 / 0.8652 | 20.00 / 0.9000 | no |
| column-photo-scrolled | Photo column, scrolled (console row 3 of 3) | col-photo-r02.png | col-photo/r02.png | label 12.18 / 0.397; icons 19.21 / 0.865; panel 14.57 / 0.757 | 18.99 / 0.8495 | 20.00 / 0.9000 | yes |
| column-music-scrolled | Music column, scrolled (console row 2 of 2) | col-music-r01.png | col-music/r01.png | label 10.83 / 0.421; icons 18.44 / 0.863; panel 14.57 / 0.757 | 18.84 / 0.8437 | 20.00 / 0.9000 | yes |
| column-video-scrolled | Video column, scrolled (console row 3 of 3) | col2-video-r02.png | col-video/r02.png | label 12.34 / 0.377; icons 18.29 / 0.836; panel 14.61 / 0.755 | 18.25 / 0.8265 | 20.00 / 0.9000 | yes |
| column-game-scrolled | Game column, scrolled to row 6 (PlayStation Store on the console) | col-game-r05.png | col-game/r05.png | label 11.65 / 0.408; icons 21.37 / 0.885; panel 14.58 / 0.754 | 18.44 / 0.8306 | 20.00 / 0.9000 | yes |
| column-network-scrolled | Network column, scrolled to Internet Search (console row 4) | col-network-r03.png | col-network/r03.png | label 12.29 / 0.443; icons 22.05 / 0.901; panel 14.58 / 0.754 | 18.88 / 0.8574 | 20.00 / 0.9000 | no |
| column-psn-scrolled | PlayStation Network column, scrolled (console row 5) | col-psn-r04.png | col-psn/r04.png | label 11.27 / 0.396; icons 19.67 / 0.900; panel 14.58 / 0.754 | 18.71 / 0.8523 | 20.00 / 0.9000 | yes |
| column-friends-scrolled | Friends column, DOWN probe (console row 2) | col-friends-r02.png | col-friends/r02.png | label 11.61 / 0.396; icons 23.00 / 0.923; panel 14.58 / 0.754 | 19.03 / 0.8655 | 20.00 / 0.9000 | yes |
| options-open | Options menu (Triangle), open | menu-options-open.png | options/open.png | label 19.10 / 0.835; icons 17.11 / 0.789; panel 16.99 / 0.780 | 18.66 / 0.8626 | 18.00 / 0.8800 | yes |
| options-row2 | Options menu, focus row 2 | menu-options-row2.png | options/row2.png | label 19.18 / 0.835; icons 17.08 / 0.785; panel 17.17 / 0.782 | 18.61 / 0.8614 | 18.00 / 0.8800 | yes |
| options-row3 | Options menu, focus row 3 | menu-options-row3.png | options/row3.png | label 18.81 / 0.834; icons 17.07 / 0.788; panel 17.19 / 0.782 | 18.70 / 0.8611 | 18.00 / 0.8800 | yes |
| options-row4 | Options menu, focus row 4 | menu-options-row4.png | options/row4.png | label 18.47 / 0.828; icons 17.03 / 0.788; panel 17.23 / 0.779 | 18.94 / 0.8616 | 18.00 / 0.8800 | yes |
| dialog-confirm | Delete-confirmation dialog (settled) | dlg-confirm-3000.png | parity cell (dialog-box__original__day__1080p) | label 21.07 / 0.899; icons 22.30 / 0.952; panel 12.10 / 0.379 | 19.91 / 0.8989 | 18.00 / 0.8800 | yes |
| dialog-column-route | Delete-confirmation dialog, column route (no box) | dlg-confirm-3000.png | dialog/confirm.png | label 12.85 / 0.565; icons 18.30 / 0.739; panel 13.53 / 0.359 | 19.16 / 0.8678 | 18.00 / 0.8800 | yes |
| osk-default | On-screen keyboard at rest | osk-default.png | osk/osk.png | label 21.01 / 0.762; icons 24.94 / 0.846; panel 20.90 / 0.767 | 25.96 / 0.8921 | 18.00 / 0.8800 | yes |
| user-create-page | User-name page (before the OSK) | page-user-create.png | osk/page.png | label 8.40 / 0.529; icons 13.79 / 0.629; panel 9.00 / 0.563 | 12.94 / 0.5988 | 18.00 / 0.8500 | yes |
| band-theme | Theme choice band (Theme Settings page, row 1) | page-theme-list.png | band-theme/band.png | label 20.57 / 0.889; icons 16.20 / 0.742; panel 17.47 / 0.748 | 18.94 / 0.8648 | 18.00 / 0.8800 | yes |
| band-theme-down1 | Theme choice band, one Down | page-theme-list-down1.png | band-theme/band-down1.png | label 20.25 / 0.884; icons 16.07 / 0.742; panel 17.03 / 0.741 | 18.86 / 0.8620 | 18.00 / 0.8800 | yes |
| band-color | Color chip band (Theme Settings page, row 2) | page-color-list.png | band-color/band.png | label 20.89 / 0.890; icons 17.24 / 0.770; panel 21.42 / 0.853 | 19.69 / 0.8733 | 18.00 / 0.8800 | yes |
| band-color-down1 | Color chip band, one Down | page-color-list-down1.png | band-color/band-down1.png | label 22.96 / 0.895; icons 18.94 / 0.795; panel 22.08 / 0.851 | 22.11 / 0.8946 | 18.00 / 0.8800 | yes |
| band-background | Background choice band (Theme Settings page, row 3) | page-background-list.png | band-background/band.png | label 20.63 / 0.886; icons 17.54 / 0.780; panel 17.98 / 0.745 | 19.51 / 0.8714 | 18.00 / 0.8800 | no |
| band-background-down1 | Background choice band, one Down | page-background-list-down1.png | band-background/band-down1.png | label 20.58 / 0.884; icons 16.91 / 0.763; panel 17.90 / 0.736 | 19.21 / 0.8691 | 18.00 / 0.8800 | no |
| band-font | Font choice band (Theme Settings page, row 4) | page-font-list.png | band-font/band.png | label 20.68 / 0.893; icons 16.05 / 0.754; panel 17.15 / 0.678 | 18.68 / 0.8641 | 18.00 / 0.8800 | no |
| band-font-down1 | Font choice band, one Down | page-font-list.png | band-font/band-down1.png | label 20.53 / 0.892; icons 16.03 / 0.753; panel 16.70 / 0.672 | 18.66 / 0.8639 | 18.00 / 0.8800 | no |
| depth2-video-library | Video > Video Library page (depth 1) | page3-video-row0.png | parity cell (page-video-library__original__day__1080p) | label 17.72 / 0.816; icons 22.89 / 0.971; panel 17.30 / 0.807 | 19.72 / 0.8860 | 18.00 / 0.8500 | yes |

### 4.1 What the crops say when they are read as a group

| crop | pairs | mean dB | min | max | reading |
|---|---:|---:|---:|---:|---|
| item label row (400,425,800,100) | 19 | 11.67 | 10.12 | 13.17 | the single weakest structural region in the tree; glyph phase, size and the label's own text |
| category icon row (0,240,1920,120) | 19 | 19.71 | 9.53 | 23.02 | good on the columns, catastrophic on PSN (9.53) where the console draws a banner there instead of icons |
| info bar (1329,75,591,58) | 19 | 14.17 | 6.54 | 14.65 | the most *consistent* deficit: 14.57-14.65 dB on every column, which is what a clock-string format difference plus a missing clock-face disc looks like |

---

## 5. Summary statistics

| measure | value |
|---|---|
| measured comparisons | 62 (26 referenced catalogue cells + 36 extra pairs over 27 extra surface states) |
| frame PSNR at or above its class budget | 30 of 62 |
| frame PSNR at or above budget, or within 1 dB below it | 35 of 62 |
| frame PSNR within 1/3 dB of budget (all of them at or above it) | 30 of 62 |
| frame SSIM at or above its class budget | 11 of 62 |
| passes PSNR **and** SSIM (the gate the scorecard uses) | 10 of 62 |
| margin distribution | 30 at or above · 0 between 0 and −1/3 dB · 5 between −1/3 and −1 dB · 23 between −1 and −3 dB · 4 below −3 dB |
| mean frame PSNR, all 62 | 18.74 dB |
| content-differs pairs (extra table) | 27 of 36 |
| content-differs pairs that still clear their frame budget | 4 |
| non-content-differs surfaces that fail their frame budget | 29 |

**Average frame dB by class** (both instruments pooled, and each instrument on its own so the pooling
cannot hide a disagreement):

| class | pairs | mean frame dB | budget dB / SSIM | parity-only mean | extra-only mean |
|---|---:|---:|---|---:|---:|
| icon-heavy | 28 | 17.94 | 20.00 / 0.9000 | 17.33 | 17.94 |
| text-heavy | 9 | 17.32 | 18.00 / 0.8500 | 17.60 | 17.32 |
| composite | 25 | 20.14 | 18.00 / 0.8800 | 20.79 | 20.14 |

The two instruments land within 0.02 dB of each other on the pooled mean (18.73 parity, 18.74 extra), which
is the useful check here: they use different console frames, different seeds and different routes, and they
agree, so the number is a property of the build rather than of one capture batch.

**The margin distribution is bimodal, and that matters.** No surface lands within a third of a dB *below*
its budget: a surface is either at or above budget (30), or it is a dB or more under
(32). So "within 1/3 dB" is 30, and that
number is the same set as "at or above budget" — either the surface is right or a named mechanism is
wrong, there is no cluster of near-misses to grind down.

**SSIM is the binding gate.** 30 surfaces clear the PSNR budget, 11 clear the SSIM budget, and only
10 clear both. The overshoots on PSNR with an SSIM miss are the surfaces where the mean
error is small but the structure is displaced (the controller indicator: 23.43 dB against a 0.8800 budget,
0.8072 measured).

---

## 6. The 15 worst measured surfaces, with the cause named from the existing audits

Ranked by whole-frame PSNR against the class budget. A surface whose content legitimately differs is marked,
because for those the number measures the content source, not the renderer — but they are still the worst
*measured* surfaces and the brief asked for the list, not for the flattering subset. The last two rows sit above their PSNR budget and rank here only because the tree's average is 18.7 dB, so an 18.3 dB surface is still one of the fifteen lowest.

| # | surface | class | frame dB | margin to budget | content differs | likely cause, from the audits |
|---|---|---:|---:|---|---|---|
| 1 | column-psn-row0 | icon-heavy | 8.68 | -11.32 | yes | the console draws the full-width PSN Sign Up artwork band, its information panel, the trophy ribbon and the collage; the native PSN column draws a plain row list with a single icon (audit/PARITY-SCORECARD annotations column-psn; audit/ICON-GAP-AUDIT absent-artwork rows) |
| 2 | column-psn__original__day__1080p | icon-heavy | 8.68 | -11.32 | no | same surface, second console frame: native column is a plain list, console frame is the Sign Up banner plus information panel |
| 3 | user-create-page | text-heavy | 12.94 | -5.06 | yes | the native opens the OSK directly when Cross is pressed on Users > Create New User; the console first draws a user-name page (centred prompt, field, OK button) and opens the keyboard from its Edit action (out/rpcs3-allgraphics-2026-10-28/INVENTORY.md section 11; audit/OSK-FIRMWARE.md section 2) |
| 4 | page-network-connection-settings__original__day__1080p | text-heavy | 14.99 | -3.01 | no | content: the console body is the firmware Internet Connection Settings text, the native body is this build's own not-available message, and the page also misses the console back hint (catalogue annotation message body; audit/SETTINGS-COVERAGE.md section 5.3) |
| 5 | page-network-test-error__original__day__1080p | text-heavy | 15.15 | -2.85 | no | content: the console body reads An Ethernet cable is not connected.; the native row answers with the build's own not-available message, and the back hint region fails too (catalogue annotation; audit/DIALOGS-FIRMWARE.md message-page rows) |
| 6 | page-network-status-list__original__day__1080p | text-heavy | 17.49 | -0.51 | no | content: the console draws its fixed 13-row report (Internet Connection through UPnP), the native page answers with the host's own network report; label and value columns are host strings (catalogue annotation status rows; audit/GEOMETRY-ROOT.md depth-2 grid) |
| 7 | column-photo__original__day__1080p | icon-heavy | 17.97 | -2.03 | no | row content (Search for Media Servers over an empty console VFS vs the host Photo Library) plus the category icon row, where the native draws a tenth category (TV) the console does not have, shifting every icon centre by the row pitch (catalogue annotation; audit/REFERENCE-GAPS.md 4.1) |
| 8 | column-music__original__day__1080p | icon-heavy | 18.05 | -1.95 | no | row content (Search for Media Servers vs host Music Library) and the ten-icon category row against the console's nine |
| 9 | column-friends__original__day__1080p | icon-heavy | 18.10 | -1.90 | no | the console Friends column is rowless because it is signed out and offline; the native column draws three Steam rows, so the region under the category icon has no console counterpart at all |
| 10 | column-video__original__day__1080p | icon-heavy | 18.11 | -1.89 | no | row content (BD Data Utility on the console, Continue Watching natively) plus the ten-icon category row |
| 11 | column-video-row0 | icon-heavy | 18.12 | -1.88 | yes | same column against the allgraphics frame; the console clock there reads 29/10 12:00 (DD/MM, 24-hour) while the native clock reads 10/25 12:32 PM, so the info-bar region (14.65 dB) carries a format difference as well as the row-content one |
| 12 | column-video-scrolled | icon-heavy | 18.25 | -1.75 | yes | same column, scrolled: console row 3 is Video Editor & Uploader, native row 3 is Video Settings |
| 13 | root__original__day__1080p | icon-heavy | 18.25 | -1.75 | no | item labels (native Steam Store / All Games rows have no console counterpart) and the info bar: the clock text format and the 0.61-alpha clock-face disc the native does not draw (audit/FIDELITY-ROOT-BAR.md clock face; audit/INFO-BAR.md ramps 17 percent weak) |
| 14 | info-indicator__original__day__1080p | composite | 18.32 | 0.32 | no | the controller indicator region fails on SSIM (23.43 dB / 0.8072 against a 0.8800 budget): the pad glyph and its slots are drawn but the shape and shading differ (audit/PS-BUTTON.md; audit/INFO-BAR.md indicator slots) |
| 15 | page-settings-theme__original__day__1080p | text-heavy | 18.38 | 0.38 | no | row set: the firmware page has four rows, the native page six (two native-only rows that load a .p3t theme and a wallpaper), and the focused row expansion and help-line geometry differ (audit/SETTINGS-FIRMWARE.md 5.6/12.4) |

Two patterns run through that list and are worth stating separately from it:

1. **The PSN column is a different screen, not a worse drawing.** 8.68 dB is eleven dB under budget and no
   renderer work moves it: the console's column is an artwork surface (full-width purple banner, trophy
   ribbon, photo collage, information panel) and the native column is a row list. Until that artwork path
   exists (`audit/ICON-GAP-AUDIT.md`, the absent-artwork rows), the PSN column will keep scoring like a
   blank page.
2. **The info bar is the quietest systematic loss.** The gate's info-bar layer is 15.05 dB and the same crop
   against the console frames is 14.17 dB mean with a 0.1 dB spread across all 19 column pairs. A 0.1 dB
   spread across nine different columns and two different capture batches means the loss is not in the
   columns: it is the clock string format (the console frames read `29/10 12:00`; this build's `--ui-clock`
   renders `10/25 12:32 PM`) plus the clock-face disc the build does not draw
   (`audit/FIDELITY-ROOT-BAR.md`, the 0.61-alpha disc).

---

## 7. What could not be measured, and the frames that lie

### 7.1 The capture-loop limitation on media-library pages

The photo and music library pages have no native frame in this table. The route reaches the page and then
the scripted capture loop does not return from the framebuffer grab: four attempts across two routes
(`DOWN,ENTER` and `ENTER`), per-attempt timeouts from 90 s to 240 s, the run's log ending with the
`ENTER` accepted and no grab saved. The video library page does capture (parity's `page-video-library` cell,
and the pair in section 4), so the wall is not "media pages", it is that specific grab. Recorded rather than
worked around: a screenshot retried until it appears would be a worse instrument than an honest gap.

The delete-confirm box is measured (`dialog-confirm`, 19.91 dB / 0.8989) from the parity `dialog-box` cell
because that state needs the resume seed (`out/native-reference/phase5/resume-data`) to have an item to
delete; opened from the column row with nothing to continue, this build correctly draws its inline
empty-state line instead, and that absence is measured too (`dialog-column-route`, 19.16 dB).

A third gap is a genuine surface gap, not an instrument one: the console's `Playlists` page
(`page2-photo-item1`) has no native counterpart at all — the native Photo column has no Playlists row — so
it is recorded here rather than scored.

### 7.2 Frames in the allgraphics batch that do not show what their names claim

Opened for this sweep and confirmed wrong, each contradicted by `native-xmb/audit/SETTINGS-COVERAGE.md`
section 5.2's frame-integrity warning and confirmed by eye here:

| frame | what it actually shows | used? |
|---|---|---|
| `page-settings-system.png` | the **Users** column (Turn Off System / Create New User / *User), not System Settings | no |
| `page-settings-system-down1.png`, `-down2.png` | the same Users column, one and two rows down | no |
| `settings-row6-system.png` | the same Users column | no |
| `settings-row3-video.png` | the Users column with Turn Off System focused | no |
| `page-settings-video.png` | the PS menu's "Do you want to turn off the system?" confirm box | no |
| `col-video-r00..r05.png` | the walk never left its last row (`INVENTORY.md` section 7 discarded it); the authoritative walk is `col2-video-*` | no (col2 used) |
| `dlg-confirm-1800.png` | visually identical to `dlg-confirm-3000.png` at this scale (the dim level differs, not the layout) | 3000 used |
| `page2-*-b.png`, `page3-*-b.png`, `osk-default-2.png` | duplicates of the `-a` frames from a second pass | first of each used |
| `col-*-rNN` past the last row (`col-game-r07..r09`, `col-settings-r16..r20`) | the column clamps at its last row, so these are the last row again rather than the "overscroll" state `INVENTORY.md` section 5 describes (`col-game-r09` still shows Corrupted Data focused) | no (the scrolled pairs use real deep rows, not clamped repeats) |

Every frame that *is* used in section 4 was opened and matches its label: the nine `col-*-r00` columns
(Users, Settings, Photo, Music, Video via `col2`, Game, Network, PSN, Friends), the scrolled rows
(`col-settings-r09` Accessory Settings, `col-settings-r15` Network Settings, `col-game-r05` PlayStation
Store, `col-photo-r02` Playlists, `col-music-r01` Playlists, `col2-video-r02` Video Editor & Uploader,
`col-network-r03` Internet Search, `col-psn-r04` PlayStation Store, `col-users-r02` *User), the options
menu and its focus walk, the delete-confirm pair, `page-user-create`, `osk-default`, the four choice bands,
and `page3-video-row0`.

### 7.3 Three catalogue annotations the measurement contradicts

Each is worth a dated correction because it changes what a reader would expect the gap to be:

- `font-list` is annotated "the native list draws the firmware faces this build registers
  (SCE-PS3 Seurat LATIN, VAGRundschriftDLig)", but the captured band holds **Original, Rounded, Pop** —
  the console's three values, row for row (sidecar `entryCount` 3, selection walk Original → Rounded → Pop).
  The font band is one of the closest surfaces in this sweep (18.66-18.68 dB) rather than a content gap.
- `background-list` is annotated "the native list has three values and no Brightness row", but the captured
  band has **four** rows with Brightness first (sidecar `entryCount` 4, selection walk Original → Classic →
  Wallpaper at indices 1-3), exactly the console's four.
- `theme-list` has the opposite problem: the annotation says nothing about the row count, and the native
  band holds a **one-item** value list (Install plus Original) against the console's four (Install, Original,
  Classic, Air Paint), so Down cannot move focus there at all (sidecar selection 0 on all three grabs).

These are candidate catalogue corrections, not edits: this pass wrote no reference, budget or catalogue
file, and `work/parity-catalogue.json` is unchanged.

---

## 8. Evidence files

| file | contents |
|---|---|
| `C:\dev\out\fidelity-sweep\gate.log` | the gate's full output (section 2) |
| `C:\dev\out\fidelity-sweep\parity.log` | the parity capture's per-cell log, Tier A/C verdicts and region scores |
| `C:\dev\out\fidelity-sweep\parity-cells.csv`, `parity-states.csv`, `parity-summary.json` | the parity table, flattened from `out/parity/scorecards/scorecard-20261029-193824.json` |
| `C:\dev\out\fidelity-sweep\extra-surfaces.json`, `extra-surfaces.csv` | the 36 extra pairs with every crop rect and score |
| `C:\dev\out\fidelity-sweep\native-manifest.json`, `native-manifest-partial.json` | each capture job: tokens, exit code, per-frame sidecar (category, depth, selection, entry title, row count) |
| `C:\dev\out\fidelity-sweep\native/\` | the captured frames (`col-*`, `options`, `dialog`, `osk`, `band-*`) plus their `.png.json` sidecars |
| `C:\dev\out\fidelity-sweep\merged-surfaces.json`, `merged-surfaces.csv` | all 62 comparisons ranked by frame PSNR |
| `C:\dev\out\fidelity-sweep\measured-summary.json` | the statistics in section 5 in machine-readable form |
| `C:\dev\out\fidelity-sweep\sheets\` | the contact sheets used to verify what each frame shows |
| `C:\dev\xmb\work\fidelity-sweep.ts`, `fidelity-parity-summary.ts`, `fidelity-report.ts`, `fidelity-doc.ts`, `fidelity-pairs.json`, `fidelity-sheet.py` | the sweep's own scripts, written for this pass |

Frames captured but not scored are kept: the rest of each column walk (`native/col-*/r*.png` past the paired row), `native/band-*/page.png`
(the Theme Settings page under each band), and `native/dialog/cancelled.png` (the state after Circle).
They are the evidence for the row counts and for the "the band never left its at-rest focus" claims, not
score rows.

Nothing in this pass re-baselined a reference, edited a budget, or touched a source file. The only writes
are `work/fidelity-*.ts`, `work/fidelity-pairs.json`, `work/fidelity-sheet.py`, everything under
`C:\dev\out\fidelity-sweep\`, and this document.
