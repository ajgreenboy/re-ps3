# Fidelity verdict - the exhaustive comparison, consolidated (2026-10-29)

Six independent audits, each measured against console frames and the firmware containers, merged into one
verdict. Every number below comes from one of them; the six documents are the evidence, this is the summary.

| audit | file | scope | result |
|---|---|---|---|
| Surface map | `FIDELITY-SURFACES-MAP.md` | every page the firmware has | 125 surfaces: **23 reachable / 40 partial / 62 absent** |
| Root frame and category bar | `FIDELITY-ROOT-BAR.md` | depth-0 frame, 49 elements | **26 match / 19 differ / 3 absent** |
| Rows, pages, options, dialogs | `FIDELITY-SURFACES.md` | 63 elements / 8 surfaces | **18 match / 15 partial / 21 gap / 8 unverified** |
| Media, players, OSK | `FIDELITY-MEDIA.md` | 74 elements | **25 match / 27 differ / 19 absent / 3 no-reference** |
| Behaviour and state machines | `FIDELITY-BEHAVIOUR.md` | 189 behaviours | **99 match / 26 differ / 21 absent / 43 unprovable** |
| Measured sweep | `FIDELITY-MEASURED.md` | 62 graded comparisons | mean **18.74 dB**; **10 of 62 clear their budgets** |

Build measured: `out/native-xmb/re-ps3-native.exe`, sha256 `4dc3b3e4...ffb34428`, built from HEAD `07a4fe4`
(29 MB of frame captures under `C:\dev\out\rpcs3-allgraphics-2026-10-28` and the parity corpus are the console
side; the sweep and parity instruments agree to 0.02 dB, so the measurement is not instrument noise).

## 1. Element totals (four element audits, 375 elements)

| verdict | count | share |
|---|---:|---:|
| match | 168 | 44.8% |
| differs (wrong art / geometry / timing) | 87 | 23.2% |
| absent | 64 | 17.1% |
| unverified or no console reference | 56 | 14.9% |

Settings rows are counted separately: **143 rows, 68 real / 75 placeholder / 0 absent** (`SETTINGS-COVERAGE.md`).

The honest headline: **the recreation is roughly half-exact, a quarter visibly wrong, and a sixth missing** at the
element level - but **surface breadth is much weaker than element fidelity**, because whole page classes that the
firmware has (browser, store, trophies, messaging, save data, media players, editor, settings sub-flows) have no
native page at all.

## 2. Surface breadth (the "fully fleshed" answer)

125 expected firmware surfaces, deduplicated by page class from 28 XMBML documents (207 views) and 122 RCO
containers (1,174 page objects):

- reachable: **23 (18.4%)**
- partial: **40 (32.0%)**
- absent: **62 (49.6%)**
- gradeable (native page *and* console frame): **53**
- never captured on the console: **72 (57.6%)**, 26 of which are unreachable on this rig by construction

Per column (surfaces / reachable / partial / absent): Settings 33/6/13/14, system-level 32/16/11/5,
Friends 10/0/5/5, Game 9/0/1/8, PSN 9/0/1/8, Video 7/0/1/5, Music 6/0/1/4, Photo 6/0/1/4, Network 7/0/2/5,
Users 5/0/3/2, TV 3/0/1/2, root row 1/1/0/0.

## 3. Measured fidelity (62 comparisons)

- mean frame PSNR **18.74 dB** (parity 18.73, extra surfaces 18.74)
- at or above class PSNR budget: **30 of 62**; at or above budget - 1 dB: **35**
- clear SSIM: **11**; clear both PSNR and SSIM: **10** - **SSIM is the binding gate**
- class means: icon-heavy 17.33-17.94, text-heavy 17.32-17.60, composite 20.14-20.79
- gating failures by region: frame 20, category icon row 6, item label row 2, page rows 2, back hint 2,
  info bar 1, band column 1, band rows 1, OSK panel 1, controller indicator 1, PS-menu controller panel 1

Worst measured surfaces (dB, margin to budget): PSN column **8.68** (-11.32), user-create page **12.94**
(-5.06), network connection-settings **14.99**, network test-error **15.15**, network status list **17.49**,
Photo column 17.97, Music 18.05, Friends 18.10, Video 18.11, root 18.25.

## 4. The ranked gap list (merged, deduplicated)

**A. Blocks the look (measured, pure render):**
1. Background field **+10..21 luma high** on every frame - the single largest shared lever (needs the console's
   64x32 pre-tone-map surface to close).
2. **Ribbon crest inverted** on the right half (-291 px at x1700) and **+30..105 luma** too bright.
3. **Row glyphs hollow**: `itemGlyph()`'s glass-mask alpha rewrite draws the firmware's 0.765 folder body at
   0.184, and **5 of the 7 Game rows bind the wrong texture** versus `category_game.xml`.
4. **Item focus plate BG1** (319x175 at 405,419) not drawn; **clock face dark disc** missing; focused item icon
   expands 1.73x instead of 2.56x.
5. **Ten icon columns vs the console's nine** - the extra TV column shifts every later icon 200 px.

**B. Whole surfaces missing (breadth):**
6. Internet Browser (largest plugin in the firmware), PlayStation Store, Trophy Collection, friends
   messaging/chat, save-data management, the real media players, Video Editor and Uploader, the settings
   sub-flows (netconf/deviceconf/software update/print/backup), copy/move/progress dialogs, notification
   producers, in-game XMB/impose, What's New, Photo Gallery, playlists, sign-up/sign-in, disc/BD playback.

**C. Player panels (thinnest area):**
7. Video player's **25-button control matrix** and 14 status planes absent; photo viewer's **20-button panel**
   and hint/slideshow strips absent; audio player's progress page not rebuilt; volume HUD can vanish on a still
   frame (fade-in missing from the UI-layer signature) and its focus segment is never drawn.

**D. Structure and behaviour:**
8. **Options menu has 2 rows vs the console's 5** (no Sort By/Group Content group, no 199 px gap) and its band
   sits +66 px right.
9. **Settings writes commit and pop instantly** - no confirm chain or result page.
10. Media columns carry a **different row set** (0 of the console's 8 media rows), Theme page has 6 rows vs 4,
    message pages lose the y=879 rule, the idle hint plate never draws, the `*User` sub-label never fills.
11. PS-menu power semantics, Auto-Off re-arm via input, OSK extras (L1/R1 caret, L2/R2 pages, Select mode,
    open/close fade), gameboot/XMB return, multi-select copy/delete, Steam account-switch surface.

## 5. At ceiling (measured exact - do not re-open)

Category grid (pitch 200.00, focus 565, quad 117.13/167.328, material .698/1.0), category label ink (0 x,
+1 px cap), focused icon silhouette, item row block (505/80/+175/-305), label typography and alpha, info-bar
plate rect and interior wash, **PS-tap plate** (1-2 px, 66.40 vs 67.84), clock text edge and ring stroke,
indicator slots (5 authored, none lit), no vignette/border; depth-1 page grid 450/607/678/749, depth-2 rules
exactly y199/y879 and back-hint ink exact; OSK panel 493,290 934x500, 10x6 grid 66x50, 468 ms blink, both
function rows, prediction frame, caps page; music plate/progress/7 buttons at 90 px; volume plate 10 segments
at 20 px; settings row set 143/143 with column and Theme pages row-for-row.

## 6. Unprovable locally (named artifacts, not guesses)

43 behaviours (`FIDELITY-BEHAVIOUR.md` 14), 3 media elements (`FIDELITY-MEDIA.md`), plus the surfaces the rig
cannot reach by construction (PSN services, disc/BD, in-game XMB, notifications, media players, volume HUD,
copy/busy dialogs). Settling artifacts are listed beside each row - chiefly RPCS3 input bursts, a 200+ fps
page-open burst, console audio captures, `vsh.elf` PhLevelMeter layout, decrypted `videoplayer_plugin.prx`, and
the console's 64x32 pre-tone-map surface.

## 7. What "done" would take

The plan of record's remaining phases map onto this list: Phase 13 carries A, Phase 12/11 carry C and D, Phase 9
carries breadth measurement (the 62-comparison instrument is now the gate), Phase 15 carries D's behaviours, and
Phase 16 can only close when A and the B-surface set are done. In order of measured yield per unit of work:
**A1 background residue** (lifts every state), **A3/A4 row art + BG1 + clock disc + icon growth** (pure render),
**A5 the TV column decision**, **D8 options structure**, **C7 player panels**, then the B surfaces in the order
the surface map ranked them.
