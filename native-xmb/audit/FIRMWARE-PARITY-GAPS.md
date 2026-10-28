# FIRMWARE-PARITY GAPS - PS3 4.93 XMB vs native-xmb

Assessment only. No source file was modified for this document. Written 2026-10-26.
Scope: what the 4.93 firmware does in rendering, layout, motion, menus, text, behaviour and
audio, and what this C++/Qt recreation still lacks.

> **Status 2026-10-27/28 - read section 1.4 for the current score; section 1 below is the 2026-10-26
> assessment and is kept as history.** The firmware recoveries and the consumption passes landed after
> this register was written: the runtime RCO object model (`6096dbf`), the dialogs recovery (`e4e3c23`)
> and its implementation (`e171263`, verified in `6cf54a4`), the item-menu and information panel
> recovery (`969c9ac`), the OSK layout module and its firmware recovery (`68134a6`), the settings pages
> analysis and the root/page geometry (`4bbff6e`), the media panels analysis (`79f377d`), the scene
> runtime-read adoption (`9545047`), the Settings row-set application (`937babe`, `4c4b25e`), the GIM
> decode completion (`e90b196`), the P0 scorecard (`09ae471`) and the 2026-10-28 waves: the
> settings/item bands (`3f3a445`), the media panels and volume meter (`320a69e`), the item-menu groups
> (`b760278`, `8f02123`), the info-bar plate (`6ead87c`), the OSK draw (`78ccde9`), the texture and
> theme decoder fixes (`1781dd3`), the first text-bearing Tier B scorecard (`e818e25`), and the third
> wave now rated in 1.4: the layout/text corrections (`2ecc49c`, `80f4ae4`), the Theme lists
> (`7eafa75`, `8ef9ecd`, `abfc80b`), the two-button box (`9723cb4`), the PS menu and tap plate
> (`af30d9e`), the composite references (`cb94045`), the scoring-validity layer (`471e781`) and the
> canonical pin correction (`70a7711`). Section 2.6 lists every surface that moved from free-hand to
> firmware-derived, with the commit that moved it.
> Where a row's "current native state" changed, the row says "(updated 2026-10-27)" or "(updated
> 2026-10-28)" - a row without that marker still describes the 2026-10-26 code.
>
> Two numbers a reader needs before quoting anything in this file: the scorecard is
> [PARITY-SCORECARD.md](PARITY-SCORECARD.md) (`49acac6`, section 18: Tier A 35 of 35 capturable states,
> Tier B 0 pass / 2 informative / 23 gating, Tier C 9, 25 of 30 firmware states referenced, **0 states
> 1:1**), and the completion ledger is [PHASES-2026-10-28.md](../PHASES-2026-10-28.md) section 1 (16
> done / 49 partly / 22 not started / 1 obsolete). A percentage in sections 1.1-1.4 is a judgement with
> stated components; the tier counts are the measurement.

> **Status 2026-10-27 - this register predates the 2026-10-26/27 feature run.** Since it was written,
> the notification composite (B4, audit/NOTIFICATIONS.md), the item-level Triangle options menu
> (audit/OPTIONS-MENU.md), the Download Management page (audit/STEAM-DOWNLOADS.md section 10), sidecar
> subtitles with the player panel (audit/SUBTITLES.md), Continue Watching (audit/MEDIA-CONTROLS.md), the
> volume HUD (audit/HTPC-IDEAS.md section 6) and the in-XMB Jellyfin sign-in (audit/JELLYFIN-NATIVE.md)
> landed. Read the rows and the ranked list below as the 2026-10-26 plan, and per-feature status in
> audit/FEATURE-CATALOG.md. The one number this banner corrects: the canonical clock-text and info-bar
> capture baselines are now **11.39 / 0.4105** and **14.78 / 0.6117** (American clock default;
> `work/ab-layers.thresholds.json` revisionNote item 3), superseding the 13.51 / 0.6131 and
> 17.12 / 0.7046 measured under the earlier DD/MM format.

## Evidence classes used below

- **M** = measured or traced: read from a firmware binary/RCO/MNU, or captured from a real
  firmware run (RPCS3) and recorded with an address, constant or frame in the audits.
- **F** = fit or assumption: a curve, constant or geometry inferred from reference frames or
  from an earlier port, not traced to a firmware call.
- **R** = recorded run: the project's own audit trail claims a local run. Not re-executed for
  this document.

The register below does not carry a column of these tags; each row cites the audit or source line
that records the evidence, and any value that is a fit rather than a trace is named as such in the row
and gathered again in "What is measured and what is only fitted" (section 2.5).

Firmware modules, decrypted plugins, RCOs, RSX captures and reference frames stay local and are
not committed. In this checkout `out/native-reference/` (the stored RPCS3 reference frame that
`work/ab-layers.ts` scores against) is absent; the layer numbers quoted below are the recorded
baselines in `work/ab-layers.thresholds.json`. A fresh `--capture` run needs that reference
regenerated from RPCS3 or supplied with `--ref`.

## 1. What "firmware parity" means here, and the score at the time of the assessment

Parity here does not mean "it looks like the XMB". It means every visible and behavioural element
is produced from the firmware's own data - the RSX programs and their MNU parameters, the RCO object
trees and XMBML documents, the paf widget semantics, the SPU simulations and the measured timings -
and is verified against a matched firmware state, with fits clearly separated from traces. The
recreation scored, honestly, **about 47%** of that target at the time of the assessment (range 40-50
depending on how strictly "verified against a matched capture" is required). Weighted by surface
(weights rendering 35%, layout and motion 20%, menus and screens 20%, behaviours 15%, audio 10%):
rendering 55% (background, HDR/glare, ribbon and cold boot are firmware ports; icon material and the
whole glyph pipeline are reconstructions), layout and motion 70% (nearly all positions and both ease
modes are measured, but several are fits), menus and screens 35% (the columns and pages are hand-built
approximations of XMBML, and the Triangle options system is absent), behaviours 30% (notifications,
background music, list actions and the in-game overlay do not exist), audio 40% (four original cues
and five theme roles exist; there is no background music and no measured mixing). The score is capped
well below 100% by things no PC can reproduce, listed in section 4.

This paragraph is the **2026-10-26** assessment, kept verbatim. Several of its parentheticals are no
longer true (the Triangle options panel, notifications, the volume HUD, Download Management and
Continue Watching now exist; the glass icon material is the full-brightness path) and the score moved.
Section 1.1 re-scores it and names what moved.

### 1.1 Re-score, 2026-10-27 (after the firmware-recovery week)

Same weights, same target, same rule: a surface counts only where the firmware's own data drives it
**and** a matched comparison exists or is specified. This is a judgement with stated components, not a
measurement - the measurement is the scorecard's tier counts, and it says none of these percentages.

| Surface (weight) | 2026-10-26 | now | What moved, and what did not |
|---|---:|---:|---|
| Rendering (35%) | 55% | **58%** | The firmware glass program became the normal path wherever an icon draws at full `_Ambiant.w`, with the opaque material demoted to dimmed states (`23980a5`); the scene's MNU constants are now read rather than fitted (spot direction, near/far focus, ICONS keyframes, LINE1 schedule; `6096dbf`, `9545047`); elision and caret now use the renderer's own advances with a separate firmware-shape ellipsis (`23980a5`). Not moved: glyph metrics still come from Qt shaping (agreement, not authority), the refraction input is still a background cut (P4.2 not started), glow is untraced, and no icon-interior/edge layer exists to score the new material. |
| Layout and motion (20%) | 70% | **70%** | The analysis improved and the code did not. `GEOMETRY-ROOT` (`4bbff6e`) resolved the six structural findings with exact numbers (label 113, parent 109.46, incoming 210 at 1.1022x, depth-keyed 262, the receding-bar model) and the page-move trace (`3c0c87b`) destroyed the 300 ms / mode 4 citation - but `view.cpp` still ships the 186/189 pitch, the `kColumnX-40` anchor and 300 ms / mode 4. Nothing in this weight is applied to `src/` yet. |
| Menus and screens (20%) | 35% | **44%** | Message surfaces are now split by kind and drawn from measured geometry, RCO colours and firmware strings, with the invented dim and hint gone (`e171263`, `6cf54a4`); the item options panel and the notification composite exist; **the Settings row sets are now firmware-driven** - the column, the three segment pages and the one-level page rows come from `category_sysconf.xml` (`937babe`) and the column is installed in the shell (`4c4b25e`). Not moved: the settings list/choice page classes, `tex_sett` row icons and confirm pages; the options panel is still a right-hand strip rather than the firmware's plate-backed floating box, the information panel has no indicator row, and the OSK draw is still free-hand. (All three of those landed in the 2026-10-28 second wave - section 1.3.) |
| Behaviours (15%) | 30% | **38%** | Notifications, the volume HUD, Download Management, Continue Watching and the in-XMB Jellyfin sign-in landed; Auto-Off now darkens the display instead of quitting, kiosk mode is verified frameless, and pad battery/transport are real (`067d6f0`); Steam account switching, `+app_stop` verification, avatars and the trophy verdict landed in the data layer (`b339422`). Not moved: background music, list actions (delete/copy/move/display-all), a visible profile switch, and the in-game overlay. |
| Audio (10%) | 40% | **42%** | The firmware's single cue gain is measured at **unity** (`27f1569`: `explore_plugin.prx` VA 0x272000, `sysconf_plugin.prx` VA 0x14ab70), the Trophy cue is wired and `snd_trophy` fires on the trophy notification, and the commit-a-value/host-write/theme-clear cues are wired (`4c4b25e`) with per-decision tracing. Not moved: `snd_category_decide` still has no action, the error cue is substituted by a labelled guess, and there is still no measured mixing, volume law or cue-to-photon latency (A3, A4). |
| **Weighted total** | **47%** (40-50) | **53%** (45-55) | The band is stated because the components are judgements; a single number invites quoting it as a score. |

Footnote on the arithmetic: the 2026-10-26 component figures weight to about 49%, not the 47% section 1
states - both sit inside the 40-50 band it declares, and the difference is the kind of rounding this
table is trying to stop; the new figures weight to 53.0%. If a reader wants one number, the answer is
that neither is a measurement.

What the re-score is worth: one full-surface behaviour class (dialogs) moved from "wrong surface" to
"measured and implemented", one material path (icons) moved from reconstruction to firmware program,
one data path (scene) moved from fits to reads, and one page class (settings) moved from unknown to
partly applied: the row sets are the firmware's own, while the page classes are still old. Everything
else either stayed or moved only in analysis. The honest summary is that the **application** side has
started to close on the **specification** side: section 2.6 lists nine surfaces, one is applied end to
end (the message surfaces), three are partly applied (the settings row sets, the OSK layout, the
runtime object model), and the rest are documented targets.

### 1.2 Rating changes, 2026-10-28

Only the rows this wave touched are listed; the information bar is included because its evidence moved
while its rating did not. Dialogs are not repeated here: `M5` already records the
`e171263`/`6cf54a4` application.

| Row | Was | Now | Evidence |
|---|---|---|---|
| `M7` Settings pages | specified, **not applied** | **partly applied**: the column, the three segment pages and the one-level page rows are the firmware's; the page classes, `tex_sett` icons and confirm pages are not | `937babe` (`SETTINGS-FIRMWARE` 10), `4c4b25e` (shell install) |
| `A1`/`A4` audio | cue set mapped, cue gain unexplained | **gain measured at unity**, Trophy and the commit/host/theme cues wired, per-decision trace; mixing, volume law and latency still open | `27f1569` (`INTERACTION-AUDIO` 10), `4c4b25e` |
| `M6` OSK | spec, layout module, 38/52 textures, free-hand draw | **layout applied and decode closed**: 52/52 textures, caret period measured at ~936 ms; the draw is still free-hand and still carries the 500 ms fit (superseded by 1.3: the draw landed in `6ead87c`, verified `78ccde9`) | `68134a6`, `e90b196` (`OSK-FIRMWARE` 14), `110f89d` (`OSK-FIRMWARE` 13.1) |
| Texture decode (P1.5) | coverage unknown | **3,174/3,174 images decode** across 89 containers, zero failures; this is a decoder result, not a visual one | `4c4b25e` (`TEXTURE-COVERAGE` 1), `e90b196` |
| `M4`/`L5` information bar | plate not drawn, rect measured | **unchanged at this pass**: `110f89d` measures the information page and plate rect, but `tex_indi_bg` is still not the drawn asset and the indicator row does not exist (superseded by 1.3: both landed in `6ead87c`) | `110f89d` (`CAPTURE-BATCH` 4.5), `TEXTURE-COVERAGE` 4 item 1 |

The component moves that follow are Menus 40 -> 44 and Audio 40 -> 42, which is the weighted total of
53% in section 1.1. Rendering stays 58%: complete decode is not a rendering win while the five
draw-side substitutions in `TEXTURE-COVERAGE` 4 are unapplied. Layout and motion stays 70%: the
page-move analysis (`3c0c87b`) contradicted the native 300 ms/mode 4 but nothing in `src/` moved.

### 1.3 Rating changes, 2026-10-28 second wave (through `e818e25`)

Section 1.2 is the first 2026-10-28 pass; this is the second, and it supersedes 1.2's totals. Same
weights, same rule: a surface counts only where the firmware's own data drives it **and** a matched
comparison exists or is specified.

**The overall score is a judgement, not a measurement, and the measurement beside it says zero.** At
`e818e25` the scorecard reports **Tier A 35 of 35 capturable states** (structural equality on the app's
own dumps), **Tier B 0 of 35 with 22 cells measured** (whole frames 17.6-19.0 dB / 0.83-0.87 SSIM
against 18-20 dB class budgets; the failures concentrate in row/list geometry and band text rows),
**Tier C 9**, and 22 of the 30 firmware-referenced states covered by a reference. **No state is 1:1.**
The percentages below answer "how much of the target is driven by the firmware's own data"; the tier
counts answer "how much of it matches". They are different questions, and only the second is measured.

| Surface (weight) | 2026-10-27 | now | What moved, and what did not |
|---|---:|---:|---|
| Rendering (35%) | 58% | **61%** | The theme picture path now uses the shared GIM decoder, proven pixel-identical on the real `01.p3t` (65/65) and fixed on a P3TF mask fixture (1/8 -> 8/8), and `bestImage` refuses a same-name artwork swap unless it is the same picture (`1781dd3`). Three of the five draw-side substitutions closed: the `tex_indi_bg` plate (`6ead87c`), the OSK LABEL_*/KEYTOP masks (`78ccde9`) and the `tex_music` swap (`1781dd3`). Not moved: the refraction input, glyph metric authority, glow, RSX quantisation, no icon-interior/edge layer, and the Settings row icons still draw `qgl` ids. |
| Layout and motion (20%) | 70% | **71%** | The settings/item band geometry is measured and drawn: two origins (1260-1261 and 1324-1325), 40 px and 38 px pitches, fixed baselines 560/598/636, a 237 px group gap (`3f5b0bd`, `3f3a445`, `8f02123`). Not moved: the root column still ships the 189 px pitch with the `kColumnX-40` anchor, 250 incoming and 99/110 labels, and the depth move still runs 300 ms in mode 4 - which is why the Tier B row/list failures have no fix yet. |
| Menus and screens (20%) | 44% | **55%** | The item menu is the firmware's band in its two groups with the RCO's own labels and no Close row (`828518e`, `b760278`); the info bar is the firmware plate plus the online slot (`6ead87c`); the OSK is drawn from its RCO and module (`6ead87c`, `78ccde9`); every settings choice list enumerates the page's own items and is drawn as the measured band (`416d13c`, `3f3a445`, `5094833`); the media panels and the `PhLevelMeter` volume exist (`320a69e`). Not moved: the settings list page's expanding focused row, `tex_sett` icons, the confirm/apply/result family, the nested Sort By planes, and the root/page geometry. |
| Behaviours (15%) | 38% | **40%** | The music control panel's interactions exist (Left/Right walk, Cross runs, Triangle toggle, 150 ms fade, audio chrome fading under it), the volume display behaves as the firmware's meter including mute-as-unlit (`320a69e`), Sort By sorts by birthTime/lastModified with the firmware's date pairs (`828518e`), and the firmware zone token is a storable value that migrates (`5094833`). Not moved: Delete/Copy/Move/Display All, BGM, a visible profile switch, the in-game overlay. |
| Audio (10%) | 42% | **43%** | The music panel's module-action cue fires through `Shell::playCue` - a real call site, mapped because the native cue table calls that sound Accept rather than Confirm (`8f02123`). Not moved: `snd_category_decide`, the error cue, and any loopback measurement. |
| **Weighted total** | **53%** (45-55) | **57%** (50-64) | The band is stated because the components are judgements; a single number invites quoting it as a score. The arithmetic: 61x.35 + 71x.20 + 55x.20 + 40x.15 + 43x.10 = 56.9. |

What the re-score is worth, stated plainly: four composite surfaces moved from "wrong shape" to "the
shape the frames show" (item menu, info bar, OSK, media panels), one data path moved from a private
reader to the shared decoder, and one settings page class moved from "specified" to "drawn as measured".
The thing that did **not** move is the thing the scorecard measures: rows and lists. A reader who wants
one honest sentence should take the tier counts, not the percentage.


### 1.4 Rating changes, 2026-10-28 third wave (through `49acac6`)

Section 1.3 is the second 2026-10-28 pass; this is the third, and it supersedes 1.3's totals. Same
weights, same rule: a surface counts only where the firmware's own data drives it **and** a matched
comparison exists or is specified. Only surfaces that changed tonight are re-rated. **Media panels are
not one of them**: `320a69e` is an ancestor of the last sync (`88b54ba`), so the media-panel rating in
1.3 stands unchanged - a re-rating here would be a change with no commit behind it.

**The overall score is a judgement, not a measurement, and the measurement beside it still says zero
states are 1:1.** At `49acac6`, scorecard 18 reports **Tier A 35 of 35 capturable states**, **Tier B 0
pass / 2 informative / 23 gating** (the 25 measured cells; 10 captured cells still have no reference),
**Tier C 9**, and **25 of the 30** firmware-referenced states covered. The percentages answer "how much
of the target is driven by the firmware's own data"; the tier counts answer "how much of it matches".
They are different questions, and only the second is measured.

| Surface (weight) | 2026-10-28 second wave | now | What moved, and what did not |
|---|---:|---:|---|
| Rendering (35%) | 61% | **62%** | The canonical background pin moved to the reference's own date and every gated layer improved (`70a7711`: frame 18.76/0.8360, top background 21.06/0.9434, icon row 21.62/0.9062); the same pass proves the console's own microcode reproduces this build's scene value, so the residual is a named constant past the draw rather than a translation defect. Text metrics were corrected from the matched pair (`80f4ae4`: em 27->32, ink .72->.455+.545, matched-string crop 14.85->22.81). Not moved: the refraction input, glyph-metric authority, glow, RSX quantisation, no icon-interior/edge layer, and the Settings row icons still draw `qgl` ids. |
| Layout and motion (20%) | 71% | **74%** | The level-0 label pen (113 / 678), category title (-5), parent unfocused icon (455.54), faint-neighbour alpha (.067), depth-1 pen (60), the Theme band family (x0 1324 / zero 1744 / inset 17) and the measured 0.40 band dim are applied (`2ecc49c`, `abfc80b`, `80f4ae4`), and 17 of 22 Tier B states improved. Not moved: the receding-bar model and the incoming 210 / 1.10223 scale are still not in `src/`, so Phase 13's core constants are unchanged. |
| Menus and screens (20%) | 55% | **60%** | The PS system menu is measured and fixed against its first console frame (`af30d9e`: entries 30.89/0.9857, frame 28.82/0.9918, legend 23.93); the two-button confirm box is measured and drawn from a console frame (`9723cb4`: region 18.12/0.8420, frame 19.77); the four Theme lists are the console's contents (`7eafa75`, `8ef9ecd`: 63 lists / 442 items, Color 13 rows with 12 month chips, Background 4 with the Brightness row, Font 3, refusals enforced). Not moved: the settings list page's expanding row, `tex_sett` icons, the confirm/apply/result family, the nested Sort By planes, and the root/page geometry. |
| Behaviours (15%) | 40% | **41%** | The PS-tap plate now appears with the measured 155/66/2001/66 ms envelope, because the `ps` token finally reaches the tap handler (`af30d9e`). Not moved: the dialog box's drawn focus is still presentational (`Shell::activate` has no choice index), Delete/Copy/Move/Display All, BGM, a visible profile switch, the in-game overlay. |
| Audio (10%) | 43% | **43%** | Nothing in this wave touched a cue, a level or a mix. |
| **Weighted total** | **57%** (50-64) | **59%** (52-66) | 62x.35 + 74x.20 + 60x.20 + 41x.15 + 43x.10 = 58.95. The band is stated because the components are judgements; a single number invites quoting it as a score. |

The Tier A/B evidence behind each re-rated surface, printed beside the judgement so the two cannot be
confused:

- **PS menu and control indicator.** Tier A: `ps-menu` and `info-indicator` pass. Tier B: `ps-menu`
  frame **28.82/0.9918 PASS**, `ps menu entries` **30.89/0.9857 PASS** against the hand-set 26.0/0.95
  budget, `ps menu legend` **23.93/0.9423**; `info-indicator`'s indicator region **22.37/0.9277 PASS**
  (budget 18.0/0.88), while its whole frame remains 18.02/0.8257 FAIL because the background behind the
  plate is this build's day wave, not the console's (`af30d9e`, scorecard 18).
- **Dialog box.** Tier A: `dialog` passes, but **its route captures the inline line, not the box**, so
  its Tier B cell stays 16.42/0.7576 FAIL. The box itself is measured by a direct crop against the
  dialog reference: frame **19.77/0.8937**, `dialog` region **18.12/0.8420** - over the 18.0 PSNR budget,
  short of 0.88 SSIM. The drawn `Yes`/`No` focus is presentational (10.5 in DIALOGS-FIRMWARE).
- **Theme lists.** Tier A: the three list states were the 07:37 run's only Tier A misses (row counts and
  the panel log line); `32dd3f0` aligns the catalogue and `f4f5920` fixes the last Background assertion,
  and scorecard 18 reports Tier A 35/35. Tier B, last printed per state before the draw landed:
  `color-list` 19.23/0.8639 frame and 19.77/0.8766 band column, `background-list` 18.82/0.8617 and
  19.93/0.8797, `font-list` 18.38/0.8544; none passes. The draw's own crops against the console's list
  frames are Color band 19.67/0.7430, chip column 17.71/0.7637 (annotated `contentDiffers` at
  19.93/0.8404 because the console draws unlabelled chips and this build labels six values), Background
  band 19.34/0.7463 and Font band 20.39/0.7756.
- **Text metrics.** Tier A passes everywhere. Tier B: the only row both frames spell the same
  ("System Name") goes **14.85/0.4813 -> 22.81/0.9457**; `page-settings-system` page rows 16.26->16.60
  and `page-settings-theme` 15.27->15.53; the canonical gate is 6/6 and `root`'s item-label row is
  11.08/0.4003 - inside its gate, but its label is a different string, so it is annotated
  `contentDiffers`, not a pass. The multiply shadow is inert.
- **Background pin.** Tier A is unaffected (a capture pin is not an assertion). The canonical family is
  6/6 at 18.76/0.8360 frame, 21.06/0.9434 top background, 21.62/0.9062 icon row, 11.99/0.3895 item
  label, 11.41/0.4528 clock and 15.05/0.7667 info bar. The **parity** cells keep the corpus pin, so no
  Tier B number moved because of the correction; `root`'s last printed frame is 17.97/0.8225. **And that
  pin is still the old date**: `work/parity-reference.json` line 62 fixes the `day` cell at
  `2026-10-25T10:00:00` while the reference frame is a 10/28 capture, so the parity corpus still carries
  the same `_MonthTime` phase confound (26.129 against the app's 23.63 at the 10-25 pin, worth about
  3.7/255) that the canonical gate just removed. No Tier B cell has been re-captured at the corrected
  date, which means today's Tier B frame and band numbers are **depressed by a known amount**, not
  ceilings. Re-pinning the corpus is a corpus change (it invalidates the stored cells) and is not done
  here.

What the wave is worth, stated plainly: three composite surfaces moved from "never measured" (or from a
6.6 dB region deficit) to passing their own region budgets - PS menu entries and frame, the control
indicator, and the Theme lists' Tier A assertions; the two-button box moved from an invented native fit
to a measured frame; the one text row whose string matches the console moved 8 dB; and the background's
canonical numbers moved for a measured reason rather than a fit. What did **not** move is what Phase 13
still owns: the receding bar, the incoming column, the 189 px pitch and the `kColumnX-40` anchor. The
background residue (+0.0594 linear scene, past the draw) is named and its closing artifact is specified,
but it is not closed. A reader who wants one honest sentence should take the tier counts, not the
percentage: **0 states are 1:1.**


## 2. Gap register

Size: S = bounded slice, days, evidence already local. M = needs one new capture or trace plus
code, one to two weeks. L = missing subsystem or unresolved firmware mechanism, weeks or more.

### 2.1 Rendering

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| R1 | Background gradient, monthly palettes, day curve | `lines.qrc` back_colours0, `BACKGROUND.mnu`, custom_render_plugin clock map | Ported: `src/view.cpp:319-336`, `:382-408` (backgroundTime); boot alpha `:613-615` | `_DayTime` curve is a cosine fit that reproduces the captured 1168.77 at 10:00 but is not traced (comment `src/view.cpp:386-395`). Native background still reads brighter than RPCS3 at the same hour (`audit/PIPELINE.md:342`). Closes with: RPCS3 frame at the pinned 10:00 clock plus an `XMB_FP_DUMP` of back_colours0; gate "top background" (20.25 dB / 0.9363) | M |
| R2 | HDR tone map and glare | `lines.qrc` glutils ToneMap, GlareSource*, Gaussian, AccGlare, ApplyGlare, `HDR.mnu` | Ported: `src/hdr.cpp:90-177`, presets day/night/coldboot1 | Boot composite is not reproduced: RPCS3 runs LinesController and its pre-exposure path with no ToneMap pass, so its boot frame is near black until ~2.2 s and ends darker (display 67 vs 83 native in the same region). Closes with: a boot capture of LinesController's pre-exposure LUTs plus a region-luma comparison (`audit/PIPELINE.md:230-233`) | M |
| R3 | Ribbon geometry, cloth, FFD, presets | custom_render_plugin cloth (PPU), `ffd_shader1`, `spline.elf` (SPU), `LINE1.mnu` | Ported end to end: `src/ribbon.cpp`, drawn at `src/view.cpp:336`; stages verified | Boot preset blend is not ported: the capture fits TIMESTEP 3.69 between LINE1's 4 and coldboot1's 2, and qgl_base's animation player is untraced (`audit/PIPELINE.md:118-120, 150-153`). gameboot/welcome/music presets are not scheduled because those flows do not exist | M |
| R4 | Particles pass 1: simulation, quads, material | `particles.elf` (SPU), `PARTICLES.mnu`, `particles_quads.vpo/.fpo` | Firmware-shaped pool in `src/particles.cpp` (1536 cap, 48-byte stride, fixed 0.0088883 s step, Park-Miller seed), one indexed draw. **Updated 2026-10-27** (`6096dbf` read, `9545047` adopted): the spot direction and the depth-of-field band come from `PARTICLES.mnu` (SPOT POS 4.16/2.63/-7.6 normalised; NEAR/FAR FOCUS 6.12435/12.83 base, 12.15 at the pinned clock), `PARTICLES_SPE`/`PARTICLES_UI` are read and documented as not applicable, and `proc_iridescent.tga` is bound (`154f22d`) | Still fitted or absent: emission volume (no emitter asset exists), Brownian per-step kick, `_Color`/`_Gamma` authoring, the fresnel/life-bounds/darkness alpha chain, and qgl material mode 3 (the pass is still an additive blend). The vpo's per-vertex use of the spot is not decoded - a fixed direction is what the port has evidence for (`src/particles.cpp` shader comment). Closes with: `XMB_FP_SERIES` of the particle draw (ucode 1968) decoded by `--decode-fp`, plus a vertex-array capture for stride/emitter | L |
| R5 | Particles pass 2 (`particles_second`) | `particles_second.vpo/.fpo`, `_Glare` packing, drawn into the GlareSource chain | **Updated 2026-10-27**: the second program exists as its own pass (`154f22d`): `src/particles.cpp` pass 2 builds the corner extent from `_Glare.y` (slot 459) and draws the real iridescent texture into the glare source through `Hdr::finish` | The alpha chain the second fpo packs into `o[10].w` is still composited additively, the qgl blend mode is undecoded, and the life bounds that decide which particles reach the glare pass are not modelled. Closes with: `XMB_FP_SERIES` of particles_second plus a glare-source dump | M |
| R6 | Particles spotlight / darkness / depth of field | `_LightPack[4]`, `_Darkness`, `_Focus`, `_FocusCurves`, `_NearControl`, `_LifeBounds*`, `_FrontFacingQuaternion` | **Updated 2026-10-27**: the spot direction and the near/far focus band are read from `PARTICLES.mnu` (`9545047`), so the key light and the DoF span are no longer fits; the three size bands remain | `_LightPack[4]` per-light packing, `_Darkness`, the `_FocusCurves` shaping, `_NearControl`, the near-facing quaternion and the life-bounds box. Closes with: captured vertex constants (vpo slots 459-467) matched against the preset values in `audit/PARTICLES.md` | L |
| R7 | Category icon material | `icons.qrc` quad.vpo, `icons_no_ss_no_shad.fpo`, `mask.fpo`, `ICONS.mnu`, `texenv.dds`, `amb.bmp`, `icontex` normals | **Updated 2026-10-27** (`23980a5`): the firmware glass program (`quad.vpo` + `icons_no_ss_no_shad.fpo`, refracting the finished frame through CookieCutter) is the normal path wherever an icon draws at full `_Ambiant.w`; the opaque stand-in is kept only for dimmed states. Routing measurements: category band 20.90/0.8941 all-opaque, 20.04/0.8963 all-glass, 20.89/0.8966 mixed (`audit/ITEM-ICONS.md`) | Supplying the refraction input (P4.2 not started), the `_LuminanceParams` response, and an "icon interior" / "icon edge" layer to score either path. Original's tint still mixes the month palette at a fitted 0.45 strength (`src/view.cpp` `setFixedTint`); the four ICONS.mnu preset overrides are now blended on the firmware clock keyframes (`9545047`). Closes with: an RPCS3 open-column/page-open capture at a matched day clock plus 3x icon crops, scored as a new layer | L |
| R8 | Item icons: glass vs opaque | Same program; captured per-draw `_Eye` / `_Ambiant.w` (`audit/ITEM-ICONS.md:47-56`) | **Updated 2026-10-27** (`23980a5`): item icons at full brightness take the glass path through the same routing as category icons; a dimmed item icon keeps the opaque branch and skips the CookieCutter cut (`src/icons.cpp`) | The refracted, env-lit look of a page column at `_Ambiant.w` 0.698. Evidence that decides it: run the glass path against a matched RPCS3 page capture and compare icon crops | M |
| R9 | Second item-icon size class (1.1022x) | Captured draws: 85.554 and, in a transition, 203.700 (`audit/ITEM-ICONS.md:72-75`, 91) | **Answered 2026-10-27** (`4bbff6e`, `audit/GEOMETRY-ROOT.md` 5.3): 1.1022x is the **incoming page column's entry scale**, not a level scale - the incoming anchor is 210 (not 250) with the icons at 1.10223. Not wired: `src/view.cpp` still draws incoming columns at 250 and scale 1 | Wire the entry scale (Phase 13.3) and prove it in a page-open burst that matches the measured neighbour positions within 1 px at `dRecede` 2483.53. No new capture is needed; the constants are measured | S |
| R10 | Item-icon sizes at level > 0 and the above-focus offset | Not captured | Level ignored (`src/view.cpp:277-279`); the 95 px deeper-level above-focus offset is inferred (`src/view.cpp:891`) | One folder-page capture with rows above the selection; decode `_Eye.w` and `_Eye.y` per draw | S |
| R11 | Non-icontex item icons and game artwork | paf planes for RCO images; ICON0 as file artwork with its own RCO geometry | Qt painting with measured sizes 60/104, 40/56, 18/39 (`src/view.cpp:904`), fitted to the video | RCO-object-driven position/size, the firmware "PS" frame for game icons, and ICON0 plane geometry. Closes with: RCO object attributes for those images plus a matched capture | M |
| R12 | Text: hinting and metrics | `libfontFT.prx` `cellFontFT_65E27CB3 @ 0x6aa4` loads outlines with flags 10; paf 26.6 advances and kerning | Flags 10 matched; shaping and glyph positions still come from `QTextLayout`/`QGlyphRun`. **Updated 2026-10-27** (`23980a5`): elision and the caret no longer call `QFontMetrics` - `TextRenderer::elideToWidth` and the caret use the renderer's own unhinted advances, which agree with PAF within 0.11 px at every label size | One metric authority. Agreement is not authority: advances are still Qt's shaping, so "the renderer's advance" is a proxy for the firmware's 26.6 sums. Closes with: FreeType 26.6 advance sums for matched faces and a matched string-extent capture; gate "clock text" (11.39 / 0.4500) and "item label row" (11.95 / 0.3925) | L |
| R13 | Text: coverage gamma and overlap | Coverage copied with no curve; compare-and-keep-larger copy rule (`FUN_004056d8`, `FUN_004057a0`) | **Updated 2026-10-28**: the coverage-union mask exists (`37776ae`: `glBlendEquation(GL_MAX)`, bit-identical to the keep-larger helper on a 169 px probe, 57 px would differ under alpha-over); the shadow material is now the firmware's own (4/-5 at 0.25, alpha 1, `a445f4a`), replacing the invented 0/0 at 0.2; no gamma curve is applied | The display-stage gamma is still UNKNOWN in firmware (`audit/TEXT-RASTER.md:135-158`), so any curve is a fit; the shadow's multiplicative exponent (0.533 at .25) is a labelled fit; the per-string composition order in the paf path is unproven. Closes with: one overlapping-glyph crop A/B plus a captured pair of glyphs at the same phase | L |
| R14 | Text: glow / halo | No `Text` glow field in the RCO schema; no glow texture for explore; mechanism unknown (`audit/TEXT-RASTER.md:280-326`) | Gaussian halo sigma 2.2, radius 6 (`src/text_renderer.cpp:17`, `:115`), 3.4 s breathing from 0.35 (`src/view.cpp:956`), both from MaXMB console observations | Firmware mechanism and values. Closes with: a console capture resting on one row for 8 s or more, plus the paf property-animation call that drives it | L |
| R15 | Text: subpixel phase | 26.6 phases masked with 0x3f and rounded with a +0x3f bias; `FT_Set_Transform` operands UNKNOWN (`audit/TEXT-RASTER.md:102-133`) | **Updated 2026-10-27**: sub-pixel phase tiles render the four phases (`37776ae`), superseding the "origins snap to whole pixels" statement | Whether the outline itself is translated by the phase or only the blit origin rounds: re-decompile the transform stores, not a capture | S |
| R16 | Text: elision | Plugin-managed separate `label2_text_ellipsis` Text object positioned from the metric path (`explore_plugin_all.c` around 91929-92360) | **Updated 2026-10-27** (`23980a5`): `TextRenderer::elideToWidth` returns the prefix and the ellipsis pen position from the same unhinted advances, and `View` draws the ellipsis as a second text run at that position with a firmware-shape glyph (PAF vs shaped within 0.11 px at every label size). The `QFontMetrics` path remains only as the fallback when the renderer is not ready | The advances are still Qt's shaping, so placement is agreement rather than authority; the firmware-side placement formula is only recovered for the label sizes measured. Closes with: long-label crops against a matched console frame plus the 26.6 advance trace | M |
| R17 | Text: OSK cursor | No cursor object exists in any OSK RCO; caret is code-drawn by the OSK plugin. **Updated 2026-10-28**: the RPCS3 capture measures a 12 x 27 px I-beam, 2 px shaft, 12 px serif, with a **~936 ms period** (~53% lit), superseding the 500 ms fit (`audit/OSK-FIRMWARE.md` 13.1) | **Updated 2026-10-28 (second wave)**: the I-beam shape is drawn - green, shaped from the renderer's own advance, phase pinned so captures are byte-identical (`6ead87c`, verified `78ccde9`). Still open: `osk_layout.h` carries `blinkMilliseconds = 500` and `blinkTraced = false`, so the measured 936 ms period is not wired | Wire the 936 ms period and share the caret metric with the glyphs. Closes with: the OSK plugin trace of its blink timer plus a frame sequence at a known field text | S |
| R18 | Text: per-style weight and slant | Weight/slant table at VA `0x68e0a8`-`0x68e0c4`, weight applied as `FT_Outline_Embolden` clamped to [-0.07, 0.04] | Not applied per style; Qt bold/italic only (`src/text_renderer.cpp:179-185`) | The style table and the slant channel (which would travel through the unproven transform matrix). Closes with: the same transform disassembly; weights alone are implementable now | M |
| R19 | UI composition, dim and glare on the UI layer | paf UI draw order relative to ToneMap/ApplyGlare not traced | UI layer is a 2x MSAA FBO composited over the finished HDR frame; category icons are drawn after HDR finish; screensaver dim is a uniform over the whole frame. **Updated 2026-10-27** (`23980a5`): UI-layer glare was measured at several strengths and deliberately not adopted - it fails at every strength, so the layer gate is unchanged | Whether firmware text/icons are inside the tone-mapped scene, and whether the firmware dims with a brightness curve rather than a black overlay. Closes with: an RSX capture of the UI draw order versus ApplyGlare, and the screensaver fade capture (ISC-45.3) | M |

### 2.2 Layout and motion

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| L1 | RCO/plugin-traced positions vs measured constants | `xmb_plugin_normal.rco` / `xmb_ingame.rco` `xmenu` (posX -395, posY 40), `XMList`, `PhXmBar` defaults, `objattribdef-ps3.ini` | `kColumnX = 565` (from `xmenu.posX`), pitch 200, quads 117.13/167.33, rows 290/280, page shift 262. **Updated 2026-10-27** (`4bbff6e`, `audit/GEOMETRY-ROOT.md`): `xmenu.posX -395` is proved to be the **only** geometry-bearing RCO value (screen x = 960 - 395); `posY 40` reaches no draw; every XMList child carries zero geometry; the item rows are per-draw paf output, so no RCO can supply them. The level-0 label column is 113, not 99, and the parent unfocused label is 109.46; the 262 shift is keyed on **depth**, not on "page open" | Not applied to `src/`: the 186/189 pitch, the `290/280` row scene transform and the `kColumnX-40` anchor still ship, and label drawing still uses 99/110/250. The receding-bar model (`x = movingFocus + (200n + 10 sign(n)) * 2700/dRecede`, `dRecede` 2700 -> 2483.53) replaces the 186/189 constant (Phase 13.1-13.2). Closes with: a page-open burst matching the captured neighbour positions within 1 px, plus the settled depth-1 frame drawing exactly one category icon | L |
| L2 | Page move duration and mode | paf `PhXmBar` in vsh.elf; `0x68f020` (a target value, **not** a duration - corrected 2026-10-27) | `pageMilliseconds = 300`, mode 4 (`src/view.h`, `src/view.cpp`) - unchanged since the 2026-10-26 assessment | **Contradicted 2026-10-27** (`3c0c87b`, `audit/PAGE-MOVE.md`): `0x68f020` is a target value, not a duration (the duration is the caller's `f1`); the mode literal is 5 in every bar animator and no mode-4 call exists in paf; the plugin's own bar driver feeds **200 ms from state+0x520** (only writers 200.0 and 0.0). One hop (vtable base vs r4/r5 shuffle) stays unclosed, and the 18-frame capture now conflicts with a 200 ms animation (best reading: a composite of the 12-frame item move and the ~29-frame recede). The explore_plugin 300 ms constants cannot be cited (they are dialog durations). Native constants held pending Phase 14.1 | M |
| L3 | Category cross-fade | explore_plugin content transitions; reference frames 14.4 s / 95 s | 200 ms mode 5 fade (`fadeEase_`, `src/view.cpp:591`) with the old list offset 150 px (`:975-976`) | Both the fade duration and the 150 px slide are measured from video, not traced. Closes with: an RPCS3 category-change burst analysed by `work/motion-track.ts`, or the transition call in explore_plugin | M |
| L4 | Held-key repeat and help text | paf `PhXmBar` key handler; measured on the real XMB | 300 ms then 66.7 ms, counted from the deadline (`src/view.cpp:1334-1339`); help text drawn on `column.selection` (`:964`); keyboard auto-repeat dropped (`:1250-1255`) | No configurable repeat (Accessory Settings has no Key Repeat Interval/Speed rows, `src/settings_menu.cpp:114-118`) and no acceleration model, which matches 4.93 but leaves the settings backend absent. Closes with: a settings page plus a repeat-interval capture | S |
| L5 | Information-panel clock | `PhText` named "clock" in `xmb_plugin_normal.rco` (NOT a paf `PhClock` - corrected 2026-10-27, INFO-BAR.md); xmb_plugin `FUN_0000cb44` sets it through vshcommon export `0x33FB58` (vsh.elf), whose per-minute tick is still outside the local decompile. **Updated 2026-10-27** (`4bbff6e`, GEOMETRY-ROOT 2.1/3.7): the plane and clock geometry are in the RCO - `indi_base` 560x82 at `posY -74`, `indi_adjust` -26, `clock` `posY -12` and text size 10 | Minute-resolution cache invalidation and a Qt analog clock at (1791,104) | The RCO plane is **not** the drawn box: the console's info bar is drawn by the plugin/paf and the capture rect differs, so the rect stays measured, not traced (GEOMETRY-ROOT 5.6). Still open: the periodic writer that calls the paf setter, the bar's paf projection/typography. Closes with: the setter caller in vshmain/vshcommon, or an infobar capture series; gate "info bar" (14.78 / 0.6117) and "clock text" (11.39 / 0.4105) | M |
| L6 | Safe area, overscan and non-1080p projections | Display Area Settings / video output modes | Layout is `min(pixels/1920, pixels/1080)` scaled and centred (`src/view.cpp:658-659`); icon projection re-centres a 1920-wide layout (`src/icons.cpp:156-157`) | Any display-area setting, 720p/4:3 projections, and independent text scaling. Only 1080p was ever captured (`audit/ITEM-ICONS.md:188-190`). Closes with: RPCS3 captures at 720p and with a changed display area | M |
| L7 | Depth, gap and parent-column constants | Captured `_Eye` / distance pairs (2700 and 2857.2). **Updated 2026-10-27** (`4bbff6e`, GEOMETRY-ROOT 3.5, 5.4-5.5): the parent column behind a page is a world transform at 2857.2 with label x 109.46, and the incoming column's 262-depth shift is now explained | 2700 + 157.2 x depth, 15 px gap closure, 110/262 shifts - all still shipping and still arithmetic from captures | 157.2 is arithmetic on two captured distances; the 110 shift is superseded by 109.46 and the incoming scale by 1.10223 (Phase 13.2-13.3). Closes with: a per-draw eye dump of a page-open sequence; the capture already exists locally in fpdump7 and should be re-read rather than re-shot | S |
| L8 | Wallpaper and Classic composition | Theme wallpaper mode; `BACKGROUND.mnu` classic path | Wallpaper is a host image drawn full-window with a 35/255 black wash (`src/view.cpp:781-787`); Classic hides particles (`:314`) | Firmware wallpaper compositing (scaling, dim, whether the wave stays) and what Classic actually changes. Closes with: console captures of both modes at a fixed theme | M |

### 2.3 Menus and screens

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| M1 | Category columns and items | 28 local `category_*.xml` XMBML files plus explore_plugin resource tables and selector/visibility rules | Ten columns and their items are hardcoded (`src/shell.cpp:569-605`); only Settings titles/details/icon names are projected from `category_sysconf.xml` (`:608-622`) | Every other column's items, ordering, selector visibility and resource text are hand-written. Closes with: one column generated from its XMBML view plus its RCO strings, compared row-by-row against a firmware capture | L |
| M2 | Submenu pages | explore_plugin page templates and XMBML views | Pages are built in code: settings pages, folder pages, Jellyfin and Steam folders (`src/shell.cpp`, `src/settings_menu.cpp:49-144`); depth capped at 32 (`src/settings_menu.cpp:50`) | A generic XMBML page renderer (columns, keys, icons, values) and the firmware's page-entry timing. Closes with: a non-settings firmware page (e.g. Trophy Collection or Game Data) reproduced from its XML and compared | L |
| M3 | Options panel (Triangle) | `explore_plugin` `expage_menu` - BG0/BG1 plates plus MLists, rows installed at run time as strings, no scroll object (`audit/ITEM-MENU-INFO-FIRMWARE.md` A, `969c9ac`) | **Updated 2026-10-28 (second wave)**: the panel is the firmware's **full-height right-hand band over the dimmed page** - the shape `110f89d` measured, correcting the earlier "floating plate" reading - at the measured x1320 origin (zero at 1742), 38 px pitch and fixed baselines 560/598/636, built in the firmware's two groups with `dividerBefore` as data and a 237 px group gap, every label resolved from `explore_plugin_full`'s text table with its firmware condition, no Close row, no scroll, and `snd_cancel` on close (`828518e`, `b760278`, `3f3a445`, `8f02123`) | The nested Sort By level is still one band against the firmware's separate 128x272 planes (`b760278`); the open is the native 200 ms against the measured 245-255 ms (`3f5b0bd`); the per-category row set is asserted by the harness, not yet gated by a firmware crop. The plates' own dimensions remain partly measured. Closes with a firmware crop of the band and a per-category row comparison | M |
| M4 | Information bar and panel | `xmb_plugin_normal.rco` `page_xmb_indicator`: `tex_indi_bg` 9-slice plane 560x82 at `posY -74`, a drawn 32 px analog clock, five 32x32 icon slots at -410/-376/-342/-274/-244, `page_busy` (`audit/ITEM-MENU-INFO-FIRMWARE.md` B, `969c9ac`; geometry in `GEOMETRY-ROOT.md` 2.1, rect in `110f89d`) | **Updated 2026-10-28 (third wave)**: the bar is the firmware plate - `xmb_plugin_normal:tex_indi_bg` drawn as its own 8 px nine-slice at the measured visible rect 1329,75 591x58, with the tile's rounded corner, interior black at alpha 13/255, top/bottom/left ramps and no right edge, verified 7 of 8 border pixels within 1 luma of `alpha*RGB + (1-alpha)*bg`, replacing the invented rounded rect and the 1x125 `tex_infobar` gradient; the control-indicator row draws `tex_indi_online` and only from real Steam presence (`6ead87c`). Since `af30d9e` the PS-tap control-indicator **plate** is the measured 134x46 at (1191,81), flat RGB (53,49,41), with the pad glyph/`1`/`-` contents and the 155/66/2001/66 ms envelope; the token had never reached the tap handler because the pad gate freezes the watcher | The other four slots (message, chat, avatar, plus) have no real state in this build and stay undrawn - an empty slot is honest, a made-up count is not. Still open: the busy icon from `page_busy`, the periodic paf setter that drives the clock (L5), and the bar's paf projection/typography. The indicator region passes at 22.37/0.9277; the whole frame fails on the background. Closes with a minute-boundary clock capture. Gate: info bar 15.05 / 0.7667 | M |
| M5 | Messages and dialogs | **No dialog RCO exists**: `msgdialog_plugin.rco` holds two objects and no `mbox_*`; the box, flat 12x12 plate, two bars, hint row and fades (100/200/1000 units) are assembled by `vsh.elf` (`audit/DIALOGS-FIRMWARE.md`, `e4e3c23`) | **Implemented 2026-10-27** (`e171263`, verified `6cf54a4`): five `MessageKind` surfaces (inline line, titled page, box, error-code page, information page) in `src/dialogs.cpp`, plate/bars/hint row from RCO colours and values, PAF fades 200/1000/100/150, no full-screen dim, both flat rules, two-button boxes and the confirm cue from the box's own path. Measured inline empty state 628..985 x 494..516 against firmware 630..982 x 494..514; titled page rule 1 at y 198 alpha .227 against .218, rule 2 at y 872, hint at 1155. **Updated 2026-10-28 (third wave)**: the two-button box now has its own firmware frame and is measured and drawn from it (`9723cb4`) - plate 319x176 at (482,452) with a white 43/255 wash (not `tex_msgdialog_bg`'s grey), no bar rows, no heading, the item's own 56x56 icon centred, the body left at x1074 on a 34 px pitch, `Yes`/`No` as text 120 px apart with `No` haloed, and the footer rule at y879 with X/O hints | No **numeric** 800xxxxx error code is producible on this console (`3f5b0bd`), so the error-code page is measured-but-partial while the measured network-test error page is a catalogue state (`2d9cd30`). The box's **behavioural** residual is stated: `Shell::activate` still accepts a Box unconditionally, so the drawn `Yes`/`No` focus is presentational, and its open/close fades remain TRACED only (no 200+ fps burst). The catalogue's `dialog` state still captures the inline line, so its Tier B cell (16.42/0.7576) does not measure the box; the box's direct crop is 18.12/0.8420 (over the PSNR budget, short of 0.88 SSIM). A pixel gate still waits for Phase 9: the firmware frames are 1262x800. | M |
| M6 | OSK | `oskfullkeypanel_plugin.rco` plus the module: panel 493,290 934x500, 6x10 grid at 72x54 cells (55 key objects), 7x7 palette with 32 symbols, 52 named textures, measured caret and 0.70 dim (`audit/OSK-FIRMWARE.md`, `68134a6`) | **Updated 2026-10-28 (second wave)**: **the draw landed** (`6ead87c`, verified `78ccde9`): RCO nine-slice panel, 10x6 grid at the measured face stops, LABEL_* badge pills, SBTN/LBTN focus rings, prediction column with tinted KEYTOP masks, 7x5 palette with scroll, green I-beam caret, black at the measured 0.70 dim, starting at the measured default focus `g`, span- and palette-aware, with the caret phase pinned so captures are byte-identical. `src/osk_layout.*` reads the RCO and module (33/33 self-checks, committed-table fallback) and 52/52 textures decode (`e90b196`) | The remaining differences are listed in OSK-FIRMWARE 15: faces from geometry, caption x, an empty prediction column, the palette frame x, an opaque panel, no open/close fade, and the blink still at 500 ms with `blinkTraced = false` against the measured ~936 ms. The four layer gates are recorded as region scores, not wired as a gate. Closes with the 936 ms period wired and the four layers gated | M |
| M7 | Settings pages | `sysconf_plugin.rco` page schemas and strings. Specified 2026-10-27 (`4bbff6e`, `audit/SETTINGS-FIRMWARE.md`): 16 retail rows from `seg_fixed_items`, a list page class with label x 625 / value x 1301, choice lists with their own measured bands, 29 numbered diffs | **Updated 2026-10-28 (third wave)**: the column, the three segment pages and every one-level page's row set are the firmware's (`937babe`, `4c4b25e`); every choice surface enumerates the page's own MItem children through the RCO English table in document order - 63 lists / **442 items**, 45 of them sysconf's 373 (`416d13c`, corrected by `7eafa75`) - and is drawn as the measured **full-height right-hand band** (x1260 / x1324 per page family, 40 px pitch, scrolled rows under the focus row, the committed value focused, no dot and no check) with a **0.40** dim on the page content (`3f3a445`, `abfc80b`, `2ecc49c`). The four Theme lists are the console's contents: Color 13 rows (a text `Original` row plus twelve unlabelled month chips on a 12-row window, 6 writable, 7 drawn-but-disabled with a logged reason), Background four rows including the disabled `Brightness` submenu row, Font Original/Rounded/Pop with all three applied (`7eafa75`, `8ef9ecd`); a disabled item's sentinel is rejected by `Preferences`, and the measured commit path is January -> colour 5, February -> nothing (`955cfc1`). The firmware's 110 GMT-offset zone tokens are storable, migrate, and round-trip (`5094833`) | The list page class with its expanding focused row, `tex_sett` row icons at 18/28 px, the confirm/apply/result family and per-page cue evidence (11.2/11.4/11.5/11.6). Open on the Theme lists: the measured Color down-chevron is not authored, the live focus preview (the console recolours before commit) is not applied, the `Brightness` second level does not exist, and the other seven month palettes are drawn disabled rather than mapped. Closes with a dump assertion per page plus a frame probe on the two page classes. | L |
| M8 | PS system menu | Measured RPCS3 capture of a PS hold (`audit/PS-BUTTON.md`) | **Updated 2026-10-28 (third wave)**: measured against its first console frame and fixed (`af30d9e`) - entries left-aligned at x831 on a 40 px pitch with the selected row one size larger (24 px focused, 23 px unfocused) and white, the focus bloom a sigma-5 blur of the label mask, the controller/charge panel at its measured ink rect x132-502 y803-865 with 17x17 slots and their active red, plus the menu's own clock, header rule and footer legend (rule y880, X/Enter 726/762, O/Back 1155/1192 at y897-916). The PS-tap plate is 134x46 at (1191,81) with the measured 155/66/2001/66 ms envelope, and the reason it had never appeared was that the `ps` token never reached the tap handler (the pad gate freezes the watcher) | Real controller slot/charge data, and Quit Game covers only titles this app launched (Steam handoff is watched through the registry). Score: frame 28.82/0.9918 PASS, entries 30.89/0.9857 PASS (budget 26.0/0.95), legend 23.93/0.9423; the indicator region passes at 22.37/0.9277 while its frame fails on the background. | S |
| M9 | Cold-boot intro | coldboot.raf, coldboot.jsx, raf.qrc vert0/sce01, anim_coldboot2 events, coldboot_stereo.ac3 | Ported (`src/coldboot.cpp`, `src/view.cpp:606-623`) with script timeline, sound and background boot presets | Epilepsy notice after ShowGUI, the SD actor branch (HD only, `src/coldboot.cpp:277`), the RPCS3 pre-exposure brightness, and the gameboot sequence | M |
| M10 | Photo / music / video overlays | Specified 2026-10-27 (`79f377d`, `audit/MEDIA-PANELS-FIRMWARE.md`): `videoplayer_plugin`'s 25-button 13x3 matrix over a time/chapter page with Progress widgets; the music panel exists twice (`audioplayer_plugin` and `explore_plugin_full`'s XMB panel); the photo viewer is a 20-button panel plus a slideshow panel and screen planes; the volume display is a paf `PhLevelMeter` with 20x40 textures and a 150 ms fade | **Updated 2026-10-28 (second wave)**: implemented with fits (`320a69e`). The volume display is the real `PhLevelMeter` - ten 20x40 `tex_default_lvmeter` segments over a nine-sliced 48x48 backing, 150 ms fade at delay 0, mute drawn as an unlit meter, **no caption, percentage, plate or border**; the old HUD survives only as a logged fallback. Video chrome draws `tex_progress_bar`/`in`, the jump marker, 16x16 digit cells at the bar's ends and the six `tex_cp_help_*` icons with the container's own strings instead of an ASCII hint sentence. The previously missing music control panel exists as the XMB form: `tex_cp_panel` nine-slice at 1.3, status planes only where a native state exists, 200x6 progress, seven 90x60 buttons on the shared focus, Triangle toggle with a 150 ms fade, the audio chrome fading under it | Photo and audio chrome are unchanged by design. Still open: 15 recorded fits (face stops, caption positions, button rects where no object gives them), the per-row faces and thumbnails, EXIF, the track/subtitle selectors, the photo viewer's screen planes, and one cross-fence cue gap. Closes with panel crops and the object-level draw | L |

### 2.4 Behaviours

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| B1 | Triangle options-menu semantics | `explore_plugin` per-item options; manual "Using the control panel" pages; object tree in `ITEM-MENU-INFO-FIRMWARE.md` A | **Updated 2026-10-28 (second wave)**: the menu model owns input while open and now carries the firmware's semantics - two groups with `dividerBefore` as data and a 237 px gap, focus on the first action row, every label from the RCO text table with its firmware condition, Sort By with the Creation/Update date pairs, no Close row, Circle/Escape close with `snd_cancel`, eject labelled Remove/Remove Disc with no row built (`828518e`, `b760278`, `8f02123`) | The commands the firmware really offers beyond Open/Play/Start/Resume/Information/Sort By - Delete, Copy, Remove Disc - still have no action (P7.3); the nested Sort By level is one band; the panel opens in the native 200 ms against the measured 245-255 ms. Closes with a per-category row-order assertion plus one real destructive action on a fixture | M |
| B2 | Sort, group, display-all, delete, copy | explore_plugin list actions; firmware XMBML action names; manual pages for copy/delete | No action exists (`src/shell.h:32` Action enum; no sort/group/delete/copy code) | Every list action, its confirmation, progress and refresh. Closes with: one action end-to-end (a safe one, e.g. display mode or copy to a scratch folder) against a firmware capture | L |
| B3 | Background music | `music_1`/`welcome_1` background presets, audioplayer, `PARTICLES_SPE` music variant | None. Back stops playback and clears the queue (`src/shell.cpp`, `Shell::back`); no music category icon animation; the music_1 background preset is never scheduled | Any music-while-browsing model, its mini/now-playing surface, the music category icon change and the music background preset. Closes with: a scripted run that keeps audio playing while navigating, plus the music_1 preset values from `PARTICLES.mnu`/`LINE1.mnu` | L |
| B4 | XMB notifications | `system_plugin` `page_notification` composite - plaque `tex_notification` plus the 100x100 type glyphs - with its 100/200 mode-5 slide and four animation units | **Updated 2026-10-27**: the composite exists (`Shell::notify`, `View::drawNotification`): 4-entry FIFO, 4 s lifetime, 200 ms mode-5 slide, two real producers (tracked-title return, library rescan) | Notification sound, the firmware overlap policy with the HUD and the PS menu, per-type suppression, trophy/friend triggers and the icon shadow plane. Closes with a scripted `notify` state scored as its own crop (NATIVE-07 in the catalogue) | M |
| B5 | Auto-off and idle | Power Save semantics; screensaver LUT | **Updated 2026-10-27** (`067d6f0`): Auto-Off now **darkens the display instead of quitting** - `SC_MONITORPOWER` broadcast with a `GUID_CONSOLE_DISPLAY_STATE` read-back, idempotent and reversible, the XMB stays alive (measured: 60 s setting, 60,992 ms idle, display state 0 at 6 s, process alive at 71 s, frame still rendered). Display-awake hold while playing/launching and the screensaver dim over the composed frame still exist | The 30-minute criterion was measured at a 1-minute setting; the firmware brightness curve, fade timing and wake behaviour remain unmeasured, so ISC-45.3 stays open | S |
| B6 | Theme (.p3t) coverage | theme RCO/p3t structure: infotable, icontable, bgimagetable, setable, RAF scene, font/colour selections | Still background, icontable and five sound roles (`src/p3t.h:35-38`, `src/shell.cpp:1175-1188`); dynamic RAF scenes explicitly not drawn (`src/p3t.h:13`). **Updated 2026-10-28 (second wave)**: the picture path is the shared GIM decoder, proven unchanged on the real `01.p3t` (65/65 pictures pixel-identical) and fixed on a P3TF fixture carrying real format-0x5 masks (1/8 -> 8/8 slots), the private weaker reader is gone (`1781dd3`) | Dynamic theme scenes, the `font` and `color` selections, theme install/list/delete and preview. Closes with: one dynamic theme's RAF played (coldboot.cpp already plays a RAF) and a font/colour theme applied | L |
| B7 | Host settings backends | PS3 settings that map to a PC (display, audio, input) | Real: Windows master volume/mute (`src/host.cpp:152-181`), screen/adapter reports (`:44-56, 90-149`), display-awake hold; display/fullscreen preference | Input configuration (repeat interval/speed, keyboard type, controller assignment), audio device selection, output resolution/refresh, RGB range, deep colour. Closes with: one real backend (e.g. repeat interval) plus a wired settings row and a capture | M |
| B8 | Game and media launch plus return | PS3 launch/quit lifecycle; `xmb_ingame` for the in-game XMB | Tracked `QProcess` with exit report, rescan, focus restore, idle suppression (`src/shell.cpp:134-169`); Steam handoff watched through Steam's registry (`:176-207`); Quit Game in the PS menu | A configured backend preference (emulator path/args, Steam path), in-game XMB/overlay, exclusive-display handoff, and a recorded launch-return capture. Closes with: a backend settings row plus a scripted launch/return state | M |
| B9 | Users and profiles | PS3 user login, per-user settings, delete/rename/avatar | Create/select only, with global preferences and library paths (`src/profiles.h`, `src/shell.cpp`); active user shown only under Trophy/saved-data rows | Rename, delete, avatar, logout, per-user preferences, startup user selection. Closes with: per-profile storage plus a startup selection state | L |
| B10 | PSN and online surfaces | Sony services (sign-in, Store, trophies, What's New, messages) | Honest placeholders: `Action::Unavailable` rows and firmware empty-state strings (`src/shell.cpp:595-604`, `src/settings_menu.cpp:71-78`) | Nothing should be fabricated; this stays a documented unavailability (section 4) | - |

### 2.5 Audio

| ID | Gap | Firmware source | Current native state | Missing piece and the evidence that closes it | S |
|---|---|---|---|---|---|
| A1 | Interaction cue set and mapping | `system_plugin.rco` sound resources (VAGp v6, 48 kHz stereo ADPCM) | **Updated 2026-10-28**: nine system cues are decoded, cached and mapped onto seven native slots (`src/interaction_audio.cpp`; `27f1569` adds the Trophy slot); the player has a bounded `playAt`/`Decision` API and one trace line per decision; the commit-a-value path plays SetSetting silently then Accept after its write, and host-write/theme-clear/Trophy cues fire (`4c4b25e`); the dialog confirm is traced to `snd_decide` and fires from the box (`e171263`); the music panel's module-action cue fires through `Shell::playCue` because the native cue table calls that sound Accept rather than Confirm (`8f02123`) | `snd_category_decide` still has no native action; the original error cue decodes as silence and the rejected-write path substitutes `snd_system_ng` labelled a guess; the action-to-cue matrix is inferred, not measured; no loopback recording verifies audibility, phase or level. Closes with: a loopback recording per action plus a log-to-cue matrix (Phase 14.5) | S |
| A2 | Theme sound roles | p3t `setable`; MaXMB five-role mapping | Five roles (`se_cursor`, `se_decide`, `se_cancel`, `se_optionmenu`, `se_system_ok`) map to six cue slots, Accept falling back to Confirm (`src/interaction_audio.cpp:300-309`, `:271-273`) | Whether the firmware's role set is larger, per-role volume, and mixing with the system cues. Closes with: a theme with all sounds decoded and a role-by-role A/B on a console capture | S |
| A3 | Background music (BGM) | XMB background music (music_1 background preset; audio player) | None | A music source, crossfade with cues, ducking, and persistence while navigating. Closes with: any BGM implementation plus a log/loopback assertion; ab-layers cannot score audio | L |
| A4 | Mixing, volume law and A/V phase | PS3 audio output config, key-tone volume, firmware mix bus | **Updated 2026-10-28** (`27f1569`): the firmware's single cue gain is measured at **1.0** (`explore_plugin.prx` loads float 1.0 at VA 0x272000 into the play wrapper's f1; `sysconf_plugin.prx` loads the same at VA 0x14ab70), so `kFirmwareCueGain = 1.0` is applied once per sink and the user volume stays linear; per-decision trace lines record play/retrigger-skip/refused with reasons | Still no measured mixing (one sink, previous cue stopped), no volume curve, and no cue-to-photon latency; the firmware's overlap/voice policy is unproven. Closes with: a loopback recording at three volumes plus a frame-timestamped cue capture (Phase 14.5) | M |

### 2.6 From free-hand to firmware-derived: the 2026-10-26/28 recoveries

Each row is a surface that used to be drawn by hand from screenshots. "Specified" means the firmware
data exists and is recorded in an audit; "applied" means `src/` reads or draws it. The distinction
matters more than any percentage above: **five of the nine are applied to the draw** (the message
surfaces, the information bar, the media panels, the volume display and the OSK), and **four are partly
applied** (the item menu - band yes, nested planes no; the settings surfaces - band, Theme lists and row
sets yes, list page class no; the root/page geometry - label pen, parent icon, faint alpha and depth-1
pen yes, the receding bar and incoming scale no; and the runtime object model - scene yes, view no).
**No row is a bare documented target any more**, which is the wave's real change: the frames the
scorecard loses on now have at least part of their measured geometry in `src/`.

| Surface | Firmware specification | What is now known instead of free-hand | Applied to `src/`? |
|---|---|---|---|
| Message surfaces (inline line, titled page, box, error page, information page) | [DIALOGS-FIRMWARE.md](DIALOGS-FIRMWARE.md), `e4e3c23`; verification `6cf54a4`; box frame `9723cb4` (section 10) | No dialog RCO exists (`msgdialog_plugin.rco` holds two objects, no `mbox_*`); the box, plate, two bars, hint row and fades (100/200/1000 units) are assembled by `vsh.elf`; measured inline and titled-page layouts; the full button/error/info string map; and since `9723cb4` the two-button box's real frame: plate 319x176 at (482,452) with a white 43/255 wash, no bars, no heading, the item's own 56x56 icon, body x1074 on a 34 px pitch, `Yes`/`No` 120 px apart with `No` haloed, footer rule y879 | **Yes** (`e171263`): all five `MessageKind` surfaces in `src/dialogs.cpp`, plate/bars/hint from RCO values, flat rules, `page_btnnavi` glyphs with `msg_*` labels, two-button boxes, the confirm cue from the box's own path, and the invented 53% dim and "OK" hint removed. The box is now drawn from its measured frame (`9723cb4`). Remaining: the drawn `Yes`/`No` focus is presentational (`Shell::activate` has no choice index), the catalogue's `dialog` route still captures the inline line, and no numeric 800xxxxx error code is producible on this console (`3f5b0bd`) - the measured network-test error page is a catalogue state instead (`2d9cd30`). |
| Item options menu | [ITEM-MENU-INFO-FIRMWARE.md](ITEM-MENU-INFO-FIRMWARE.md) A, `969c9ac`; band form corrected in `110f89d` | `explore_plugin`'s `expage_menu`: BG0/BG1 plates plus MLists, rows installed at run time as strings, no scroll object, nested 128x272 levels, labels from the RCO text table (no "Close" string exists; the eject label is "Remove Disc"); the measured form is a **full-height right band over the dimmed page**, not a floating plate | **Yes, partly** (`828518e`, `b760278`, `3f3a445`, `8f02123`): two groups with `dividerBefore` as data and a 237 px gap, the measured x1320 origin and 38 px pitch, the RCO's labels with their firmware conditions, no Close row, no scroll, `snd_cancel` on close. The nested Sort By level is still one band, and the open is the native 200 ms against 245-255 ms. |
| Information panel + indicators | ITEM-MENU-INFO-FIRMWARE B, `969c9ac`; [GEOMETRY-ROOT.md](GEOMETRY-ROOT.md) 2.1, 3.7 | `xmb_plugin_normal` `page_xmb_indicator`: `indi_base` 560x82 at `posY -74`, `tex_indi_bg` 9-slice drawn 591x58 at 1329,75, a **drawn** 32 px analog clock, five 32x32 slots at -244/-274/-342/-376/-410, and no trophy/download/What's New objects | **Yes, for the bar** (`6ead87c`): the plate is drawn from its own tile with the measured interior and ramps, and `tex_indi_online` is bound from real Steam presence. The other four slots have no real state and stay undrawn; the busy icon and the clock setter are open. The information *page* (Triangle > Information) is the message-surface family and is applied (`e171263`). |
| Media panels (video, music, photo) | [MEDIA-PANELS-FIRMWARE.md](MEDIA-PANELS-FIRMWARE.md), `79f377d` | `videoplayer_plugin`'s 25-button 13x3 matrix over a time/chapter page with Progress widgets; the music panel exists twice (`audioplayer_plugin` and `explore_plugin_full`'s XMB panel) and the native had neither; the photo viewer is a 20-button panel plus a slideshow panel and screen planes | **Yes, with fits** (`320a69e`): the music panel is the XMB form (seven 90x60 buttons, 200x6 progress, Triangle toggle, 150 ms fade, audio chrome fading under it); video chrome is `tex_progress_bar`/`in` plus the jump marker, digit cells and the six `tex_cp_help_*` icons with the container's strings. Photo and audio chrome are unchanged by design; 15 fits are recorded. |
| Volume display | MEDIA-PANELS-FIRMWARE 5; [HTPC-IDEAS.md](HTPC-IDEAS.md) 6 | A paf `PhLevelMeter` with 20x40 `system_plugin` `tex_default_lvmeter_*` textures and a 150 ms fade - **no caption, percentage, plate or border** | **Yes** (`320a69e`): ten 20x40 lit/off/focus segments over a nine-sliced 48x48 backing, 150 ms fade at delay 0, mute drawn as an unlit meter, no caption or percentage. The invented plate/caption HUD is a logged fallback only. PHASES section 5's "do not build" entry is now enforced in code. |
| On-screen keyboard | [OSK-FIRMWARE.md](OSK-FIRMWARE.md), `68134a6`; runtime loader `src/osk_layout.*` | `oskfullkeypanel_plugin.rco` plus the module: panel 493,290 934x500, 6x10 grid at 72x54 cells, 55 key objects (40 character), 7x7 palette with 32 symbols, 52 named textures, a measured caret (12 x 27, 2 px shaft, ~936 ms period from `110f89d`) and dim 0.70 | **Yes** (`6ead87c`, verified `78ccde9`): the draw reads the resolved layout - nine-slice panel, 10x6 grid, LABEL_* badges, SBTN/LBTN rings, prediction column with tinted KEYTOP masks, 7x5 palette, 0.70 dim, green I-beam - and `osk_layout` carries 33/33 self-checks against the RPCS3 captures. Remaining: faces from geometry, caption x, palette frame x, opaque panel, no fade, and the blink still at 500 ms in code against the measured ~936 ms (OSK-FIRMWARE 15). |
| Settings pages | [SETTINGS-FIRMWARE.md](SETTINGS-FIRMWARE.md), `4bbff6e`; application `937babe`, `4c4b25e`, `416d13c`, `3f3a445`, `5094833`, `7eafa75`, `abfc80b` | 16 retail rows from `seg_fixed_items`; two page classes - a list page with label x 625 / value x 1301 and an expanding focused row, and a **full-height right-hand choice band** over the dimmed page (the 2026-10-26 "opens its own page" reading is corrected by `110f89d`); the four Theme lists' real contents (Color 13 = Original + 12 month chips, Background 4 with `Brightness`, Font 3); 29 numbered diffs | **Partly**: the column, the three segment pages, every one-level page's row set, every choice list (63 lists / **442 items**) and the four Theme lists are the firmware's, and the band is drawn at the measured per-page origin (1260 / 1324), 40 px pitch and **0.40** dim with the committed value focused and no dot or check. Six Theme values are writable (Original plus the five month tints) and seven are drawn disabled with a logged reason; a disabled sentinel commits nothing. The list page's expanding focused row, `tex_sett` icons and the confirm/apply/result family are still open (11.2/11.4/11.5), as are the Color down-chevron, the live focus preview and the `Brightness` second level. |
| Root/page geometry | [GEOMETRY-ROOT.md](GEOMETRY-ROOT.md), `4bbff6e` | `xmenu.posX -395` is the **only** geometry-bearing RCO object (screen x 565); page/list/menu planes carry zero geometry, so every row is per-draw paf output; level-0 label 113 (not 99), parent unfocused 109.46, incoming column 210 at 1.1022x, 262 keyed on depth; the receding-bar model with `dRecede` 2700 -> 2483.53 | **Partly** (updated 2026-10-28, third wave): the level-0 label pen (113 / 678), the category title (-5), the parent unfocused icon (455.54) and the faint-neighbour alpha (.067) are applied (`2ecc49c`), and the depth-1 pen is 60 (`80f4ae4`). Still shipping the old values: the 186/189 pitch, the `kColumnX-40` anchor, 250 incoming and the 1.10223 scale. Phase 13.1/13.3/13.4. |
| Runtime object model and materials | [RCO-OBJECT-MODEL.md](RCO-OBJECT-MODEL.md), `6096dbf` | 32 object types, 12 animation types, 728+43 schema fields, 122 containers / 8,070 objects / 165 animations parsed with zero failures; a `Materials` reader for the MNU and override files; four hardcoded-vs-RCO contradictions recorded | **Partly**: the reader and `Materials` ship and the scene now consumes them (`9545047`); the **view** still derives transforms from captures instead of reading the reader (P1.1 partly). |

Two rules this table makes concrete. A "specified" surface is not a shipped surface, and the ledger
counts it as `partly` at best. And a specification is not a trace either: the caret blink, the ~100-unit
menu open and several measured rectangles come from captures, and each audit labels them as such.


### What is measured and what is only fitted

Updated 2026-10-28. The register rows cite the audit that records each value; fits are called out in
the row text, and are collected here because the difference between parity and resemblance is exactly
this list:

- Fitted or assumed: the page-move 300 ms / mode 4 pair (L2 - actively contradicted by `3c0c87b`, which
  reads the plugin's own driver at 200 ms / mode 5); the category cross-fade 150 px offset (L3); the
  189 px faint-category pitch and the 290/280 icon-row scene transform (L1); the 95 px deeper-level
  above-focus offset (R10); time-of-day `_DayTime` curve (R1) and the `_MonthTime` time-of-day term
  (`8ef9ecd` keeps it because the day-quantised constant renders the wrong palette at the canonical
  pin); particle emission volume, Brownian kick and the depth-fade alpha chain (R4); the opaque icon
  material's lighting, bevel and mix (R7); Original's 0.45 month-tint strength (R7); halo sigma/radius
  and breathing (R14); the Qt analog clock and its minute tick (L5 - the plate rect itself is now
  measured); the media panels' 15 fits (M10 - face stops, caption positions and button rects where no
  object gives them); the wallpaper dim wash (L8); the OSK draw's remaining differences (M6 - faces
  from geometry, caption x, palette frame x); the five non-Original colour names (M7 - measured swatches,
  this build's names); the two-button box's open/close fades and hint fade (M5 - TRACED only, no burst);
  and the 500 ms caret period still carried by `osk_layout.h`, which the 936 ms measurement supersedes
  but has not replaced.
- Traced or measured: background/ribbon/HDR programs and presets; icon quad sizes, pitches, row
  positions, brightnesses, camera distances; focus column x (`xmenu.posX`); the depth-keyed 262 shift;
  both paf ease modes and the 200 ms durations; held-repeat 300/66.7 ms; help-text timing; the nine
  system cues, their unity gain and the five theme sound roles; the PS menu layout (`af30d9e`: entry
  pitch 40, x831, panel rects, legend and rules); cold-boot timeline and sound length; the particle spot
  direction and near/far focus band (`9545047`); the dialog geometry, colours, strings and four fades
  (`e4e3c23`, `6cf54a4`) and the two-button box's plate/body/answers/footer (`9723cb4`); the OSK panel,
  grid, palette, texture list, 52/52 decode and the ~936 ms caret (`68134a6`, `e90b196`, `110f89d`);
  the settings column and page row sets (`937babe`); the settings page facts and root/page geometry
  constants (`4bbff6e`); all 3,174 named images decoding across 89 containers (`4c4b25e`); the two
  choice-band origins and pitches, the 0.40 band dim and the 237 px item-menu group gap (`3f5b0bd`,
  `3f3a445`, `abfc80b`, `8f02123`); the 63 settings choice lists / 442 items from the RCO (`416d13c`,
  `7eafa75`) and the four Theme list contents (Color 13, Background 4, Font 3); the info bar plate rect,
  13/255 interior and ramp profile (`6ead87c`); the PS-tap plate rect, fill and envelope (`af30d9e`);
  the media panels' object rects and the meter's ten 20x40 segments (`320a69e`); the theme picture
  path's 65/65 identity (`1781dd3`); and the applied layout/text constants - label pen 113, title -5,
  parent icon 109.46, faint alpha .067, depth-1 pen 60, item-label em 32 and ink .455+.545
  (`2ecc49c`, `80f4ae4`).
- Not applied, per row of section 2.6: the item menu's nested 128x272 planes, the settings list page
  class / `tex_sett` icons / confirm pages, the root geometry's receding-bar and incoming-column
  transform, and the runtime object model's view half. A firmware value that the native does not read
  yet is not "measured parity"; it is a documented target.

## 3. Ranked next ten: smallest honest slice, and the evidence it produces

> **Superseded 2026-10-27 as an order.** The forward plan is [PHASES-2026-10-28.md](../PHASES-2026-10-28.md)
> sections 2-3. Four of these ten are done or overtaken: item 1 (`GL_MAX` coverage union, `37776ae`),
> 4 (options panel exists), 5 (notifications exist), 6 (the page-move disassembly landed and contradicted
> mode 4), 7 (`GEOMETRY-ROOT`), 8 (the Network column is generated from XMBML), 9 (the Settings spec
> exists). Item 2 is still owed (no icon-interior/edge layer exists), and item 10 is now Phase 14.1.
> The numbers below are the recorded canonical-capture baselines (2026-10-26 build); the 2026-10-27 scene
> commit (`9545047`) claims 6/6 with all layers equal or better, which this pass did not re-run.

Ordered by fidelity gained per unit of work, using only what the existing scripted-capture harness
(`--script` tokens + `work/ab-layers.ts`) can score. Current canonical-capture numbers, for
reference (`work/ab-layers.thresholds.json` captureBaseline at the 2026-10-28 background pin,
`revisionNote20261028b`): frame 18.76 dB / 0.8360 SSIM, top background 21.06 / 0.9434, category icon
row 21.62 / 0.9062, item label row 11.99 / 0.3895, clock text 11.41 / 0.4528, info bar 15.05 / 0.7667.
The previous 10-25-pin set (18.29/0.8342, 20.25/0.9363, 20.93/0.8990, 11.95/0.3925, 11.39/0.4500,
14.98/0.7639) is what `b0defd8` measured after the glyph correction; section 1.4 and HANDOFF 4.2 record
the pin move and its per-layer gains.


1. **Text coverage compositing (R13), rendering.** Smallest slice: draw one string into an offscreen
   coverage mask with `glBlendEquation(GL_MAX)` and composite once, behind an env flag, changing no
   layout. Evidence: `bun work/ab-layers.ts --capture` produces a direct delta on "item label row"
   and "clock text"; `bun work/ab-compare.ts <ref> <native> 400,425,800,100` isolates one label.
   Expected information: whether max-coverage alone moves the label layer, independent of metrics.
2. **Item-icon reference crops (R8, R9, R10), rendering.** Smallest slice: add two layers ("icon
   interior", "icon edge") and two named states (open Game column; a folder page with rows above the
   selection) to `work/ab-layers.thresholds.json`, then capture. Evidence: fresh numbers for the
   surfaces that currently have no gate; the same states give the level>0 and 1.1022x evidence the
   firmware captures already contain (`fpdump7`) once re-decoded with `--decode-fp`.
3. **Particles second pass and iridescent texture (R5, R4), rendering.** Smallest slice: bind
   `proc_iridescent.tga` as the fragment input and draw the real `particles_second` program into the
   glare source through the existing `Hdr::finish` hook (`src/view.cpp:373-377`). Evidence:
   deterministic `--render-clock 30` captures, gating "top background" plus a new "particle band"
   crop; `XMB_BG_SCENE` shows the pre-tone-map scene so the two passes can be judged separately.
4. **Triangle options panel for one surface (B1, M3), menus.** Smallest slice: one item-level panel
   opened by Triangle on the Game column, with two real actions (Refresh, Information), reusing the
   existing panel geometry. Evidence: a new named state `game-options` captured by script
   (`... ,grab=`) with the panel crop scored as its own layer; the log records the open/close.
5. **XMB notification composite (B4), behaviours.** Smallest slice: draw one static notification from
   the real `system_plugin` assets at a scripted moment, with no queue or sound yet. Evidence: a
   `notify` named state plus a top-right crop layer; the existing "info bar" crop shows whether the
   notification placement collides with the clock.
6. **Page-move trace, then re-time (L2), motion.** Smallest slice: no native change first - disassemble
   the depth transition call site to settle mode 4 versus mode 5. Evidence: pixel scoring cannot prove a
   duration, so the proof is a `--script "burst=<prefix>@ENTER"` burst (12 frames at 12 ms) against an
   RPCS3 burst, plus `XMB_MOTION_LOG=1` for the native 18-frame settle; once re-timed, a new
   `page-open` named state gates the settled position through `ab-layers`.
7. **Layout trace for the open-page state (L1, L7), layout.** Smallest slice: re-read the existing
   captured eye/vertex dumps for a page-open sequence and derive the 186 pitch and the row-y transform
   instead of re-shooting. Evidence: replaces two fitted constants in `src/view.cpp:826-827` and
   gates through "category icon row" (21.05 / 0.8950) after a deliberate baseline refresh.
8. **One XMBML-driven column (M1, M2), menus.** Smallest slice: generate the Network column's rows
   from `category_network.xml` plus its resource strings, keeping the current icons. Evidence: the
   named-state `expect` block (row ids/titles) already used for the Steam states, plus a frame capture
   compared with an RPCS3 Network column.
9. **Settings schema for one group (M7, B7), menus/behaviours.** Smallest slice: rebuild Video
   Settings with the firmware's BD/DVD row set (Unavailable where honest) and wire one real backend
   (e.g. key-repeat interval under Accessory Settings). Evidence: a `settings-video` named state with
   row assertions; the text renderer already draws the labels so the capture also exercises R12/R16.
10. **Info-panel clock writer and bar projection (L5), layout/motion.** Smallest slice: find the paf
    setter caller (PAGE-MOVE's suggested vshmain/vshcommon and `cellRtc` search), then replace the
    measured rectangle with the traced projection. Evidence: "info bar" (17.12 / 0.7046) and "clock
    text" (13.51 / 0.6131) both gate the change; the minute-boundary state can be pinned with
    `--ui-clock`, so the capture is deterministic.

Not in the top ten because the evidence loop is longer or the surface is smaller: BGM (A3), dynamic
theme RAF (B6), in-game overlay (B8), OSK languages (M6), 720p/overscan (L6), elision/caret metric
authority (R16, R17) - the last is cheap to code but needs the PAF metric trace first, and should ride
along with item 1.

## 4. Cannot be closed

These are not laziness or missing reference data; a PC reproduction cannot honestly reproduce them.

1. **Console power state.** "Turn Off the System" on the PS3 removes power; here it closes the
   application and says so (`audit/PS-BUTTON.md`). Suspending or shutting down the PC is a
   different, destructive operation. Since 2026-10-27 the **Auto-Off** setting is honest about this in
   the other direction: it darkens the display instead of quitting, with a broadcast plus a
   read-back, so "idle" no longer means "the app exits behind you" (`067d6f0`).
2. **Controller hardware ownership.** Turning a pad off, Bluetooth pairing, vibration and motion
   calibration are owned by Windows/HID drivers; "Turn Off the Controller" can only explain.
   The pad HID path is also untested without hardware (`HANDOFF-PHASES.md:42`).
3. **PSN and everything behind it.** Sign-in, Store purchase/download, trophy sync, What's New,
   messages, friends presence via Sony, Remote Play, cloud saves. The firmware-dependent services are
   disconnected; the ISA forbids fabricating success (`ISA.md:27`).
4. **RSX/GCM bit-exactness.** Exact hardware microcode behaviour - fp16 rounding of H registers,
   fixed-point clamps, RSX alpha-test and blend quirks - can be approximated but not proven identical
   on a desktop GL driver. Parity here is visual, within a measured tolerance, not bit-exact reference
   output.
5. **Cell/SPU timing effects.** Frame-rate-coupled simulation (RPCS3's own boot stalls shift the
   cold-boot timeline by 20-40 ms) and the fixed 60 Hz scheduling of the cloth and particles cannot be
   reproduced as a wall-clock identity on a variable-rate host; only the trajectory can.
6. **Disc and protected media.** BD/DVD playback with AACS, BD-J menus, 1080p24 and 50 Hz output
   modes, deep colour, RGB range and cross-colour reduction are either licensed hardware paths or
   display-pipeline properties the application does not own.
7. **Destructive system utilities.** Format Utility, Restore PS3 System, Backup/Restore, Install Other
   OS and Data Transfer map to operations that would damage the host; they must stay explained
   refusals.
8. **In-game XMB over a real PS3 title.** `xmb_ingame.rco`/`xmb_ingame.prx` run inside the firmware
   over a running game; a native recreation can offer Quit Game and a window-level overlay, not the
   firmware's in-game XMB.
9. **Console identity and account.** Console ID, PSN account binding, activation, and per-console
   saved-data/trophy encryption have no honest PC equivalent.
10. **The firmware's own update path.** System Update against Sony's servers; the native app can only
    update itself.

## 5. Stale documentation found (source contradicts it today)

Each item was checked against the current source, not against another doc.

> Status note added 2026-10-26: this section records the state at the time of the assessment, and the
> corrections were applied to the affected docs with dated notes. Commits landed after the register made a
> few of its own statements stale, so read them with these corrections: item 8's "item-level Triangle
> menus do not [exist]" is superseded by the options API in `src/shell.h` and its panel in
> `src/view.cpp:859-882,998-1004`; R15's "glyph origins snap to whole output pixels" is superseded by the
> per-phase tiles (fit, `audit/TEXT-RASTER.md` 2.4); A1's "four cues" is superseded by nine loaded system
> cues with six mapped slots (`src/interaction_audio.cpp:32-43,321-345`); item 8's "VIS-39 notifications
> ... remain genuinely absent" is superseded by the in-flight notification queue and pop-up
> (`Shell::notify`, `View::drawNotification`); R7's source line numbers moved. Section 1's parentheticals
> ("the Triangle options system is absent", "notifications ... do not exist") were true at the
> assessment; the options API and the notification queue now exist, while the section's scores are left
> as the assessment recorded them.
> Line numbers in this section are as read when it was written; the affected docs now cite current lines.
> The numbered items below are the findings as recorded, including their present-tense wording ("still says",
> "still reads"); read that tense as "said at the time of this pass". The quoted stale text may no longer be
> present because the docs were corrected on 2026-10-26.

1. **Particles row, `audit/PIPELINE.md:18`.** Still says the layer is "Replaced: linkev/PlayStation-3-XMB's
   sparkle layer ... additive point sprites ... not a firmware port". Contradicted: `src/particles.cpp`
   is a firmware-shaped pool (48-byte records, 1536 cap, PARTICLES.mnu constants, fixed 0.0088883 s
   step, VBO + index draw), drawn into the HDR scene and again into the glare source
   (`src/view.cpp:372-377`). The row was accurate before 2026-10-26.
2. **`audit/PARTICLES.md:395-421` and `:579-586`.** "Today's `src/particles.cpp` is 2000 additive point
   sprites with no simulation" and the 18-row gap table describing no lifetime, no forces, one pass.
   Superseded by the appended "Applied"/"Tuned" sections at `:587-623` and by the source; the body text
   still reads as current.
3. **Item icons, `audit/PIPELINE.md:339-340`.** "Item icons in the open column still go through Qt."
   Already flagged as contradicted in `audit/ITEM-ICONS.md:106`; the code queues icontex items to
   `IconRenderer` (`src/view.cpp:928-931`), only RCO/artwork/theme images stay Qt.
4. **Labels "still Qt", `audit/PIPELINE.md:22` and `HANDOFF-PHASES.md:37`.** Text now renders through
   the GL glyph atlas with FreeType coverage (`src/text_renderer.cpp`, called at `src/view.cpp:768`);
   only shaping, elision and caret metrics remain Qt. "Qt drawing" is stale; "Qt shaping plus measured
   metrics" is accurate.
5. **The Jellyfin sentence, `HANDOFF-PHASES.md:57`.** "Jellyfin is currently a URL app, not a native
   player." Contradicted by `src/jellyfin_client.cpp`, `src/jellyfin_stream.cpp`, `src/jellyfin_shell.cpp`,
   the `Action::Jellyfin*` routes in `src/shell.cpp`, ISA criteria ISC-41.1 and ISC-46 through 46.5
   marked done (`ISA.md:97, 112-117`), and the same file's own status table at `:23` and `:45`.
6. **Page-move timing.** The claim is honestly hedged in `HANDOFF-PHASES.md:39` ("fitted from RPCS3,
   not traced") and in `audit/PAGE-MOVE.md`, but two places still overstate or misattribute it.
   (a) `HANDOFF-PHASES.md:199-201` files it under "Done 2026-10-25" while the same sentence calls it a
   fit - a "Done" label on an untraced constant. (b) `audit/PIPELINE.md:302-303` says "explore_plugin
   holds several 300 ms float constants" in the context of the page move; `audit/PAGE-MOVE.md:62-81`
   proves those constants are file-delete dialog, `user.info` dialog and list-label durations, and
   `:99-101` shows the only 300 ms bar call passing mode 5 in the decompile. The authoritative
   statement is: fitted 300 ms / mode 4, not traced, and the explore_plugin constants cannot be cited.
7. **Native timing block, `audit/VISUAL-BEHAVIOR.md:139-146`.** Claims `1 - exp(-18 dt)` smoothing,
   repeat "300 ms then every 100 ms", keyboard auto-repeat following host events, and a 16 ms Qt repaint
   timer. Contradicted: `View::PafEase` mode 5 at 200 ms (`src/view.cpp:590-593`), repeat 300 ms then
   67 ms (`:1336`), arrow auto-repeats dropped and normalized (`:1250-1255`), repaint on swap callbacks
   (`audit/ANIMATION-SMOOTHNESS.md:3-9`).
8. **"Not implemented" rows in `audit/VISUAL-BEHAVIOR.md:105-113`.** VIS-35 context menu: a settings-choice
   options panel now exists (`src/view.cpp:843-881`), though item-level Triangle menus do not.
   VIS-36 player control panel: fade/hide controls now exist (`src/media_controls.cpp`,
   `src/view.cpp:800`). VIS-37 dialogs: a styled dialog with rules and button hints exists
   (`src/view.cpp:1023-1035`). VIS-38 information panel: clock and PS-tap HUD exist (`:981-996`,
   `:1068-1076`). VIS-43 startup/shutdown/login: cold boot is ported (`src/coldboot.cpp`) and the PS
   menu covers system off. VIS-39 notifications and VIS-42 artwork previews remain genuinely absent.
9. **Launch/return lifecycle, `audit/FEATURE-COVERAGE.md:481-501`.** "Return lifecycle: entirely absent
   ... no `QProcess` object is constructed anywhere in `src/`" and "idle auto-close keeps running while
   a game is up". Contradicted by `src/shell.cpp:134-207` (tracked `QProcess`, exit report, rescan,
   Steam registry watch, idle suppression). The document's own trailing "Update" at `:649-655` says so,
   but the body text still reads as current.
10. **Media metadata, `audit/FEATURE-COVERAGE.md:363-367`.** "Metadata: absent ... no artist, album ...
    is read anywhere in `src/`." Now title/artist/album are read from the player at `LoadedMedia`
    (`src/shell.cpp:761-765`) for the now-playing line. Library rows still have no tags, so the
    criterion verdict is unchanged but the claim is stale.
11. **`audit/FEATURE-CATALOG.md` statuses.** Already noted as lagging in `FEATURE-COVERAGE.md:549-558`;
    still wrong today, e.g. XMB-109 (Screensaver) is listed "Not implemented" while five options and a
    native dim exist (`src/settings_menu.cpp:108`, `src/view.cpp:673-677`), and the catalogue has no
    entries at all for the native Steam, Jellyfin, import-registry or PS-menu surfaces.
12. **Line-number drift in `audit/TEXT-RASTER.md`.** Elision is cited at `view.cpp:888` (now `:951`),
    the cursor at `view.cpp:1215-1217` (now `:1466-1468`), and "three Qt font sites" no longer covers
    the renderer (`src/text_renderer.cpp:179-185`). The analysis stands; the citations do not.
13. **`ISA.md` criteria state.** ISC-20 still unchecked (`ISA.md:73`) although a tracked launch/return
    path exists, and ISC-6/10/11/12/30 remain open while their sub-behaviours are partly implemented -
    the criteria are broader than the code, but the unchecked list no longer maps one-to-one to
    "absent". Not a false claim, but it will mislead a reader who uses the checklist as a gap list.

Note on sources checked and found current, not stale: `audit/ITEM-ICONS.md` (including its measured
brightness-on-alpha experiment), `audit/CATEGORY-ICON-FIX.md`, `audit/PS-BUTTON.md`,
`audit/MEDIA-CONTROLS.md`, `audit/INFO-BAR.md`, `audit/FONT-MATCHING.md`, `audit/PAGE-MOVE.md`,
`audit/LAYOUT-TRACE.md` and the ribbon/cold-boot sections of `audit/PIPELINE.md`.

### 5.1 Stale claims corrected by the 2026-10-27/28 documentation pass

The items above are the 2026-10-26 findings. This pass corrected a further set, each against the code
or a commit, not against another document:

- **HANDOFF-2026-10-27.md** said its tree (`0c8faee`, 177 commits, 33/66 ISA) was current and that
  PHASES-2026-10-27 was the plan of record. Corrected: it is now labelled a dated snapshot and points at
  `HANDOFF-2026-10-28.md` (HEAD `9545047`, 200 commits) and PHASES-2026-10-28 (15/44/28/1 ledger).
  `HANDOFF.md` (the October 23/24 landing page) carries the same banner.
- **`ISA.md` 33/66** claimed two icon-material ticks whose probe predates the glass-material change.
  Corrected: 31/66, ISC-42/42.1 re-opened with the re-run recipe, and every tick annotated where its
  scope is narrower than its ledger row.
- **Section 1's score (47%)** is now section 1.1: re-scored to about 52% (45-55 band) with the
  components and the reason for each move; section 1.2 carries the first 2026-10-28 moves to 53%,
  section 1.3 the second wave to 57% (50-64 band), and section 1.4 the third wave to 59% (52-66 band).
  Sections 1.3 and 1.4 also state plainly that the score is a judgement and put the scorecard's tier
  counts beside it (at `49acac6`: Tier A 35/35, Tier B 0 pass / 2 informative / 23 gating, Tier C 9,
  zero states 1:1).
- **The 300 ms / mode 4 page move**, cited here as "fitted but plausible", is **contradicted**: the only
  bar animator mode is 5, `0x68f020` is a target value, and the plugin's driver feeds 200 ms
  (`3c0c87b`, L2 above). The native constants are unchanged, which is the point: this is a stale native
  implementation, not a stale document.
- **`audit/FEATURE-COVERAGE.md`**: the ISC-17 verdict line still said subtitles were "explicitly required
  ... and absent"; ISC-14 still said there is no screensaver and counted 22 preference keys;
  ISC-20's closing "Update" still called Steam launches detached. All three corrected in place with
  date stamps.
- **`audit/FEATURE-CATALOG.md`**: XMB-275 (Download Management) and XMB-284 (Trophy Collection) still said
  "Not implemented as a complete original workflow" while NATIVE-06 ships and SERVICES-BREADTH 4.5 is
  the trophy verdict; XMB-005 and XMB-289 described no Users/Friends surface although local profiles and
  Steam-backed friends exist. Corrected with the ledger wording.
- **`audit/FUNCTIONS.md`, `audit/MEDIA-CONTINUATION.md`, `audit/PHASE-CONTINUATION.md`** read as current
  state. Corrected: each now opens with a historical banner and a pointer to this register and to
  PHASES-2026-10-28; MEDIA-CONTINUATION's zoom-percentage and PHASE-CONTINUATION's idle-quit claims are
  explicitly marked superseded.
- **`audit/README.md`** listed the audit set without the seven 2026-10-27 documents. Corrected: index
  lines added for DIALOGS-FIRMWARE, ITEM-MENU-INFO-FIRMWARE, MEDIA-PANELS-FIRMWARE, OSK-FIRMWARE,
  SETTINGS-FIRMWARE, GEOMETRY-ROOT, RCO-OBJECT-MODEL, PARITY-SCORECARD and SESSION-POWER, plus
  PHASES-2026-10-28 and WORKSPACES.

Still stale and **not** fixed here, because they are outside this pass's write fence:

- **`native-xmb/README.md`** still says "Current handoff (2026-10-27): HANDOFF-2026-10-27.md - the tree
  at `0c8faee`". It must point at HANDOFF-2026-10-28 and PHASES-2026-10-28.
- **`audit/PIPELINE.md`, `audit/VISUAL-BEHAVIOR.md`, `audit/TEXT-RASTER.md`** carry drifted line numbers
  and a few statuses this pass did not re-read; the analysis stands, the citations need an owning pass.
- **`INSTALL.md`** was not re-read against the new flags (`--kiosk`, `--display`, `--start-windowed`,
  `pause=`) that `067d6f0` added.

## 6. Limits of this assessment

- Source reading plus the recorded measurements in the audits. No build, no window, no RPCS3 run and
  no capture was performed here; every runtime claim is cited to the audit that records the run and is
  therefore of the recorded-run class above. The 2026-10-27/28 re-scores (sections 1.1-1.4) are the same
  class: they re-read the audits, the commits and the source, and ran nothing.

- The layer scores quoted are the recorded baselines. They gate regressions and are not firmware
  fidelity thresholds; the "item label row" gate at 11.99 dB / 0.3895 in particular is a
  content-difference gate, not a pass.
- Firmware-derived files stay local; only addresses, constants, values and conclusions appear here.
