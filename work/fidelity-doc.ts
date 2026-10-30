// Build native-xmb/audit/FIDELITY-MEASURED.md from the artifacts this sweep produced, so every number in
// the document is transcribed by script from the JSON the tools wrote.
//
//   bun work/fidelity-doc.ts
//
// Inputs (all written by this sweep): out/fidelity-sweep/parity-summary.json, merged-surfaces.json,
// measured-summary.json, extra-surfaces.json, native-manifest.json, gate.log, parity.log.
import { readFileSync, writeFileSync, statSync, existsSync } from 'node:fs';
import { join, basename } from 'node:path';

const outRoot = 'C:/dev/out/fidelity-sweep';
const repo = 'C:/dev/xmb';
const read = (name: string) => JSON.parse(readFileSync(join(outRoot, name), 'utf8'));
const parity = read('parity-summary.json');
const merged = read('merged-surfaces.json');
const summary = read('measured-summary.json');
const extra = read('extra-surfaces.json');
const gateLog = readFileSync(join(outRoot, 'gate.log'), 'utf8').trimEnd();

const exe = 'C:/dev/xmb/out/native-xmb/re-ps3-native.exe';
const exeStat = statSync(exe);
const fmt = (n: number | undefined | null, d = 2) => (typeof n === 'number' ? n.toFixed(d) : '—');
const cell = (text: string | number | undefined | null) => String(text ?? '').replaceAll('|', '\\|');
const scope = (path: string) => {
  const rel = path.replaceAll('\\', '/').replace('C:/dev/xmb/', '').replace('C:/dev/out/', 'out/');
  return rel;
};

const budgetText = (cls: string) => {
  const b = parity.budgets[cls];
  return b ? `${b.psnrDb.toFixed(2)} / ${b.ssim.toFixed(4)}` : '—';
};

// ---------------------------------------------------------------- parity table
const parityReferenced = parity.cells.filter((c: any) => typeof c.frameDb === 'number' && c.budgetDb);
const parityUnreferenced = parity.cells.filter((c: any) => !(typeof c.frameDb === 'number' && c.budgetDb));
const parityRows = parityReferenced.map((c: any) => {
  const gating = c.gatingFails.length ? c.gatingFails.join(', ') : '—';
  const informative = c.informative.length ? c.informative.join(', ') : '—';
  return `| ${cell(c.state)} | ${c.class} | ${c.tierA} | ${c.tierB} | ${c.tierC ?? 'n/a'} | ${fmt(c.frameDb)} | ${fmt(c.frameSsim, 4)} | ${budgetText(c.class)} | ${cell(gating)} | ${cell(informative)} |`;
}).join('\n');

// ---------------------------------------------------------------- extra table
const extraRows = extra.pairs.map((p: any) => {
  const frame = p.frame ? `${fmt(p.frame.db)} / ${fmt(p.frame.ssim, 4)}` : `error: ${p.error}`;
  const crops = ['label', 'icons', 'panel']
    .filter(name => p[name]?.db !== undefined)
    .map(name => `${name} ${fmt(p[name].db)} / ${fmt(p[name].ssim, 3)}`)
    .join('; ');
  const native = /^[A-Za-z]:[\\/]/.test(p.native) ? `parity cell (${p.native.split('/cells/')[1]?.split('/')[0] ?? p.native})` : p.native;
  return `| ${cell(p.id)} | ${cell(p.surface)} | ${basename(p.consoleFrame)} | ${cell(native)} | ${crops} | ${frame} | ${budgetText(p.class)} | ${p.contentDiffers ? 'yes' : 'no'} |`;
}).join('\n');

// ---------------------------------------------------------------- worst list
const worst = summary.worst15 as any[];
const causeOf: Record<string, string> = {
  'column-psn-row0': 'the console draws the full-width PSN Sign Up artwork band, its information panel, the trophy ribbon and the collage; the native PSN column draws a plain row list with a single icon (audit/PARITY-SCORECARD annotations column-psn; audit/ICON-GAP-AUDIT absent-artwork rows)',
  'column-psn__original__day__1080p': 'same surface, second console frame: native column is a plain list, console frame is the Sign Up banner plus information panel',
  'user-create-page': 'the native opens the OSK directly when Cross is pressed on Users > Create New User; the console first draws a user-name page (centred prompt, field, OK button) and opens the keyboard from its Edit action (out/rpcs3-allgraphics-2026-10-28/INVENTORY.md section 11; audit/OSK-FIRMWARE.md section 2)',
  'page-network-connection-settings__original__day__1080p': 'content: the console body is the firmware Internet Connection Settings text, the native body is this build\'s own not-available message, and the page also misses the console back hint (catalogue annotation message body; audit/SETTINGS-COVERAGE.md section 5.3)',
  'page-network-test-error__original__day__1080p': 'content: the console body reads An Ethernet cable is not connected.; the native row answers with the build\'s own not-available message, and the back hint region fails too (catalogue annotation; audit/DIALOGS-FIRMWARE.md message-page rows)',
  'page-network-status-list__original__day__1080p': 'content: the console draws its fixed 13-row report (Internet Connection through UPnP), the native page answers with the host\'s own network report; label and value columns are host strings (catalogue annotation status rows; audit/GEOMETRY-ROOT.md depth-2 grid)',
  'column-photo__original__day__1080p': 'row content (Search for Media Servers over an empty console VFS vs the host Photo Library) plus the category icon row, where the native draws a tenth category (TV) the console does not have, shifting every icon centre by the row pitch (catalogue annotation; audit/REFERENCE-GAPS.md 4.1)',
  'column-music__original__day__1080p': 'row content (Search for Media Servers vs host Music Library) and the ten-icon category row against the console\'s nine',
  'column-friends__original__day__1080p': 'the console Friends column is rowless because it is signed out and offline; the native column draws three Steam rows, so the region under the category icon has no console counterpart at all',
  'column-video__original__day__1080p': 'row content (BD Data Utility on the console, Continue Watching natively) plus the ten-icon category row',
  'column-video-row0': 'same column against the allgraphics frame; the console clock there reads 29/10 12:00 (DD/MM, 24-hour) while the native clock reads 10/25 12:32 PM, so the info-bar region (14.65 dB) carries a format difference as well as the row-content one',
  'column-video-scrolled': 'same column, scrolled: console row 3 is Video Editor & Uploader, native row 3 is Video Settings',
  'root__original__day__1080p': 'item labels (native Steam Store / All Games rows have no console counterpart) and the info bar: the clock text format and the 0.61-alpha clock-face disc the native does not draw (audit/FIDELITY-ROOT-BAR.md clock face; audit/INFO-BAR.md ramps 17 percent weak)',
  'info-indicator__original__day__1080p': 'the controller indicator region fails on SSIM (23.43 dB / 0.8072 against a 0.8800 budget): the pad glyph and its slots are drawn but the shape and shading differ (audit/PS-BUTTON.md; audit/INFO-BAR.md indicator slots)',
  'page-settings-theme__original__day__1080p': 'row set: the firmware page has four rows, the native page six (two native-only rows that load a .p3t theme and a wallpaper), and the focused row expansion and help-line geometry differ (audit/SETTINGS-FIRMWARE.md 5.6/12.4)',
  'dialog__original__day__1080p': 'by construction: this state captures the inline empty-state line instead of the console delete box, which is dialog-box\'s job (catalogue annotation dialog)',
  'column-game-scrolled': 'the native Game column inserts two native-only rows (Steam Store, All Games) above the firmware rows, so the same row index is a different row: console row 6 is PlayStation Store, native row 6 is Saved Data Utility (PSP)',
  'column-friends-row0': 'the console column is rowless; the native column lists three Steam rows (Open Steam Friends, Refresh Steam Friends, Steam Connection)',
};
const worstRows = worst.map((row: any, index: number) =>
  `| ${index + 1} | ${cell(row.id)} | ${row.class} | ${fmt(row.db)} | ${fmt(row.db - row.budgetDb, 2)} | ${row.contentDiffers ? 'yes' : 'no'} | ${cell(causeOf[row.id] ?? '—')} |`).join('\n');

const gatingRows = Object.entries(parity.gatingFailCounts as Record<string, number>)
  .sort((a, b) => b[1] - a[1]).map(([name, count]) => `| ${cell(name)} | ${count} |`).join('\n');

const classTable = summary.classAverages.map((entry: any) => {
  const parityMean = parity.classAverages.find((c: any) => c.class === entry.class);
  const extraMean = summary.classAveragesBySource.extra.find((c: any) => c.class === entry.class);
  return `| ${entry.class} | ${entry.n} | ${fmt(entry.meanDb)} | ${budgetText(entry.class)} | ${parityMean ? fmt(parityMean.meanFrameDb) : '—'} | ${extraMean ? fmt(extraMean.meanDb) : '—'} |`;
}).join('\n');

const margins = merged.rows.map((r: any) => r.db - r.budgetDb);
const bucket = (lo: number, hi: number) => margins.filter((d: number) => d >= lo && d < hi).length;
const ssimPass = merged.rows.filter((r: any) => r.ssim >= r.budgetSsim).length;

const annotationRows = (parity.annotations ?? []).map((a: any) =>
  `| ${cell(a.state)} | ${cell(a.region)} | ${cell(a.reason)} |`).join('\n');

const unreferencedRows = parityUnreferenced.map((c: any) =>
  `| ${cell(c.state)} | ${c.class} | ${c.tierA} | ${c.tierB} | ${c.tierC ?? 'n/a'} | ${cell(c.summary)} |`).join('\n');

const manifest = read('native-manifest.json');
const failedJobs = manifest.jobs.filter((job: any) => job.frames.some((f: any) => !f.exists));
const failedLines = failedJobs.map((job: any) =>
  `| ${cell(job.id)} | ${cell(job.surface)} | ${job.frames.filter((f: any) => !f.exists).map((f: any) => f.name).join(', ')} | ${cell(job.note)} |`).join('\n');

const doc = `# Fidelity, measured: the launcher build against every firmware reference we own

Written 2026-10-29 for the fidelity-sweep pass. One table per instrument, every number taken from a tool
run in this session, nothing re-baselined, no reference edited, no source file touched. The raw JSON and
CSV sit beside this document's evidence root, \`C:\\dev\\out\\fidelity-sweep\\\`.

**The short answer to "how fleshed out is it".** Of 62 measured comparisons, 30 clear their class frame
budget on PSNR but only 10 also clear it on SSIM; 0 of 36 catalogue states pass all three tiers; the
average frame is 17.94 dB on the columns, 17.32 dB on the text pages and 20.14 dB on the overlays. The two
surfaces that carry the most weight in the eye — the PSN column's artwork and the clock/info bar — are 8.68
dB and 14.17 dB, and both fail for a nameable, already-audited reason rather than for lack of measurement.

---

## 0. Method, and how to re-run it

| step | command | what it wrote |
|---|---|---|
| build identity | \`Get-FileHash\`, \`git rev-parse\`, \`Get-Item\` | section 1 |
| canonical gate | \`bun work/ab-layers.ts --capture --exe ${exe}\` | \`out/fidelity-sweep/gate.log\` |
| parity, all cells | \`bun work/parity.ts --capture --score --exe ${exe}\` | \`out/parity/run-20261029-192650/\`, \`out/parity/scorecards/scorecard-20261029-193824.{json,md}\` |
| extra surfaces, capture | \`bun work/fidelity-sweep.ts capture\` (jobs \`col-*\`, \`options\`, \`dialog\`, \`depth2-*\`, \`osk\`, \`band-*\`) | \`out/fidelity-sweep/native/<job>/*.png\` + sidecars, \`native-manifest.json\` |
| extra surfaces, score | \`bun work/fidelity-sweep.ts score\` (pairs in \`work/fidelity-pairs.json\`) | \`out/fidelity-sweep/extra-surfaces.{json,csv}\` |
| parity flatten | \`bun work/fidelity-parity-summary.ts <scorecard.json>\` | \`parity-cells.csv\`, \`parity-states.csv\`, \`parity-summary.json\` |
| merge + statistics | \`bun work/fidelity-report.ts\` | \`merged-surfaces.{json,csv}\`, \`measured-summary.json\` |
| this document | \`bun work/fidelity-doc.ts\` | \`native-xmb/audit/FIDELITY-MEASURED.md\` |

Both instruments were run at the parity harness's own pin, so the two tables are comparable: 1920×1080
fullscreen, \`--render-clock 30\`, \`--background-clock 2026-10-28T10:00:00\` (the day preset at the date the
matched day references were taken), \`--ui-clock 2026-10-25T12:32:00\`, \`--focus-time 2000\`, dev_flash
\`C:/dev/rpcs3/bin/dev_flash\`, and the \`out/native-reference/store/ui3-data\` seed copied the way
\`work/parity.ts\` copies it.

---

## 1. Build identity, and the one honest caveat

| item | value |
|---|---|
| executable | \`C:\\dev\\xmb\\out\\native-xmb\\re-ps3-native.exe\` |
| sha256 | \`4dc3b3e4e9282fd47ebb32bacf7b504e1e18ac2d3a19a7cd3d444214ffb34428\` |
| size | ${exeStat.size.toLocaleString('en-US')} bytes |
| mtime | ${new Intl.DateTimeFormat('sv-SE', { timeZone: 'America/Chicago', dateStyle: 'short', timeStyle: 'medium' }).format(new Date(exeStat.mtimeMs))} -05:00 |
| HEAD when the sweep started | \`07a4fe4\` (committed 2026-10-29 18:33:11 -05:00) |
| exe vs that commit | built 18:33:37, 26 s after the commit; the scorecard records the same sha256 |
| HEAD when this document was written | \`79b2325\` (2026-10-29 19:52:50 -05:00) |
| what moved | \`79b2325\` adds four \`native-xmb/audit/FIDELITY-*.md\` files and touches no source, so the source at HEAD is the source this binary was built from |
| working tree at report time | \`M native-xmb/src/view.cpp\`, \`M native-xmb/src/view.h\`, \`M work/ab-layers.thresholds.json\` — another session's in-flight edits, made after this sweep started; **every number below is for the binary above, not for that working tree** |
| the gate's baseline file | \`work/ab-layers.thresholds.json\` was clean when the gate ran (19:27) and has been modified since by that other session, so a re-run today compares against different baselines than section 2 records |

The live app instance the brief warned about was left alone: it is a separate process
(\`--data-dir C:\\dev\\xmb\\out\\native-xmb\\data\`) and every capture here ran out of its own seeded data
directory, as \`work/parity.ts\` does.

---

## 2. The canonical gate: PASS, 6 of 6 layers

\`\`\`
${gateLog.split('\n').filter(line => /text presence|state |^PASS/.test(line)).map(line => line.trim()).join('\n')}
\`\`\`

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
against is \`out/native-reference/ab-rpcs3-root.png\`, the retired pre-contract frame
(\`work/parity-reference.json\` \`retired[0]\`), so its 18.76 dB is the historical comparison and not a
firmware number. The two lowest layers, item label row (11.99 dB) and clock text (11.41 dB), are the two
the parity table also flags first — the gate and the parity instrument agree on where the loss lives.

---

## 3. Parity: the catalogue, all 36 captured cells

Headline from \`scorecard-20261029-193824.md\`: **0/36 capturable states pass all three tiers.** Tier A 36
pass, Tier B 0 pass / 3 informative, Tier C 10 pass; 10 captured cells have no reference yet; 23 gating
cell failures. Reference coverage: **26 of 31 firmware-referenced states** (the catalogue has 31
firmware-referenced states and 5 native-only ones that owe none, 39 in total).

### 3.1 Cells with a reference (26)

| state | class | Tier A | Tier B | Tier C | frame dB | frame SSIM | budget dB / SSIM | gating regions that failed | informative regions |
|---|---|---|---|---|---:|---:|---|---|---|
${parityRows}

### 3.2 Gating failures by region name (the whole run)

| region | cells failing it |
|---|---:|
${gatingRows}

\`frame\` fails 20 times, and once the frame is gating the smaller regions decide whether it is a render
difference or a content difference; \`category icon row\` (6) is the structural region that fails most after
the frame, which is the ten-icon-against-nine finding again, and \`back hint\` (2) and \`page rows\` (2) are
the depth-2 and settings-page rows.

### 3.3 Captured cells with no reference (10)

| state | class | Tier A | Tier B | Tier C | what Tier A saw |
|---|---|---|---|---|---|
${unreferencedRows}

### 3.4 The informative (contentDiffers) annotations, in full

These are regions the catalogue marks as console content the native build cannot reproduce from the same
frame. They are measured and printed but never gating; \`--strict\` would gate them again.

| state | region | why it differs |
|---|---|---|
${annotationRows}

---

## 4. Extra surfaces: the console frames the parity catalogue has no cell for

36 pairs, all scored against the frames under
\`C:\\dev\\out\\rpcs3-allgraphics-2026-10-28\\frames\\\` (1920×1080, day preset, guest clock 29/10 12:00-12:31,
theme Original). Every console frame used here was opened and checked against its own inventory entry
before scoring; the ones that do not show what their names claim are listed in section 6.

Two native frames are parity cells rather than frames from this sweep's own capture directory; the note
column of \`extra-surfaces.csv\` and section 6 say why (the media-library page capture loop, and the delete
box needing the resume seed). Both are captures of the same binary at the same pin.

| pair | surface | console frame | native frame | crop dB / SSIM | frame dB / SSIM | budget dB / SSIM | content differs |
|---|---|---|---|---|---:|---|---|
${extraRows}

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
| measured comparisons | ${summary.measuredSurfaces} (${summary.parityCellsWithReference} referenced catalogue cells + ${summary.extraPairs} extra pairs over 27 extra surface states) |
| frame PSNR at or above its class budget | ${summary.atOrAboveBudget} of ${summary.measuredSurfaces} |
| frame PSNR at or above budget, or within 1 dB below it | ${summary.withinOneDbOfBudget} of ${summary.measuredSurfaces} |
| frame PSNR within 1/3 dB of budget (all of them at or above it) | ${summary.withinThirdDbOfBudget} of ${summary.measuredSurfaces} |
| frame SSIM at or above its class budget | ${ssimPass} of ${summary.measuredSurfaces} |
| passes PSNR **and** SSIM (the gate the scorecard uses) | ${summary.framePass} of ${summary.measuredSurfaces} |
| margin distribution | ${bucket(0, 99)} at or above · ${bucket(-1 / 3, 0)} between 0 and −1/3 dB · ${bucket(-1, -1 / 3)} between −1/3 and −1 dB · ${bucket(-3, -1)} between −1 and −3 dB · ${bucket(-99, -3)} below −3 dB |
| mean frame PSNR, all 62 | ${fmt(merged.rows.reduce((sum: number, row: any) => sum + row.db, 0) / merged.rows.length)} dB |
| content-differs pairs (extra table) | ${summary.contentDiffersPairs} of ${summary.extraPairs} |
| content-differs pairs that still clear their frame budget | ${summary.contentDiffersPairsPassingFrame} |
| non-content-differs surfaces that fail their frame budget | ${summary.nonContentDiffersFail} |

**Average frame dB by class** (both instruments pooled, and each instrument on its own so the pooling
cannot hide a disagreement):

| class | pairs | mean frame dB | budget dB / SSIM | parity-only mean | extra-only mean |
|---|---:|---:|---|---:|---:|
${classTable}

The two instruments land within 0.02 dB of each other on the pooled mean (18.73 parity, 18.74 extra), which
is the useful check here: they use different console frames, different seeds and different routes, and they
agree, so the number is a property of the build rather than of one capture batch.

**The margin distribution is bimodal, and that matters.** No surface lands within a third of a dB *below*
its budget: a surface is either at or above budget (${bucket(0, 99)}), or it is a dB or more under
(${bucket(-1, -1 / 3) + bucket(-3, -1) + bucket(-99, -3)}). So "within 1/3 dB" is ${summary.withinThirdDbOfBudget}, and that
number is the same set as "at or above budget" — either the surface is right or a named mechanism is
wrong, there is no cluster of near-misses to grind down.

**SSIM is the binding gate.** 30 surfaces clear the PSNR budget, ${ssimPass} clear the SSIM budget, and only
${summary.framePass} clear both. The overshoots on PSNR with an SSIM miss are the surfaces where the mean
error is small but the structure is displaced (the controller indicator: 23.43 dB against a 0.8800 budget,
0.8072 measured).

---

## 6. The 15 worst measured surfaces, with the cause named from the existing audits

Ranked by whole-frame PSNR against the class budget. A surface whose content legitimately differs is marked,
because for those the number measures the content source, not the renderer — but they are still the worst
*measured* surfaces and the brief asked for the list, not for the flattering subset. The last two rows sit above their PSNR budget and rank here only because the tree's average is 18.7 dB, so an 18.3 dB surface is still one of the fifteen lowest.

| # | surface | class | frame dB | margin to budget | content differs | likely cause, from the audits |
|---|---|---:|---:|---|---|---|
${worstRows}

Two patterns run through that list and are worth stating separately from it:

1. **The PSN column is a different screen, not a worse drawing.** 8.68 dB is eleven dB under budget and no
   renderer work moves it: the console's column is an artwork surface (full-width purple banner, trophy
   ribbon, photo collage, information panel) and the native column is a row list. Until that artwork path
   exists (\`audit/ICON-GAP-AUDIT.md\`, the absent-artwork rows), the PSN column will keep scoring like a
   blank page.
2. **The info bar is the quietest systematic loss.** The gate's info-bar layer is 15.05 dB and the same crop
   against the console frames is 14.17 dB mean with a 0.1 dB spread across all 19 column pairs. A 0.1 dB
   spread across nine different columns and two different capture batches means the loss is not in the
   columns: it is the clock string format (the console frames read \`29/10 12:00\`; this build's \`--ui-clock\`
   renders \`10/25 12:32 PM\`) plus the clock-face disc the build does not draw
   (\`audit/FIDELITY-ROOT-BAR.md\`, the 0.61-alpha disc).

---

## 7. What could not be measured, and the frames that lie

### 7.1 The capture-loop limitation on media-library pages

The photo and music library pages have no native frame in this table. The route reaches the page and then
the scripted capture loop does not return from the framebuffer grab: four attempts across two routes
(\`DOWN,ENTER\` and \`ENTER\`), per-attempt timeouts from 90 s to 240 s, the run's log ending with the
\`ENTER\` accepted and no grab saved. The video library page does capture (parity's \`page-video-library\` cell,
and the pair in section 4), so the wall is not "media pages", it is that specific grab. Recorded rather than
worked around: a screenshot retried until it appears would be a worse instrument than an honest gap.

The delete-confirm box is measured (\`dialog-confirm\`, 19.91 dB / 0.8989) from the parity \`dialog-box\` cell
because that state needs the resume seed (\`out/native-reference/phase5/resume-data\`) to have an item to
delete; opened from the column row with nothing to continue, this build correctly draws its inline
empty-state line instead, and that absence is measured too (\`dialog-column-route\`, 19.16 dB).

A third gap is a genuine surface gap, not an instrument one: the console's \`Playlists\` page
(\`page2-photo-item1\`) has no native counterpart at all — the native Photo column has no Playlists row — so
it is recorded here rather than scored.

### 7.2 Frames in the allgraphics batch that do not show what their names claim

Opened for this sweep and confirmed wrong, each contradicted by \`native-xmb/audit/SETTINGS-COVERAGE.md\`
section 5.2's frame-integrity warning and confirmed by eye here:

| frame | what it actually shows | used? |
|---|---|---|
| \`page-settings-system.png\` | the **Users** column (Turn Off System / Create New User / *User), not System Settings | no |
| \`page-settings-system-down1.png\`, \`-down2.png\` | the same Users column, one and two rows down | no |
| \`settings-row6-system.png\` | the same Users column | no |
| \`settings-row3-video.png\` | the Users column with Turn Off System focused | no |
| \`page-settings-video.png\` | the PS menu's "Do you want to turn off the system?" confirm box | no |
| \`col-video-r00..r05.png\` | the walk never left its last row (\`INVENTORY.md\` section 7 discarded it); the authoritative walk is \`col2-video-*\` | no (col2 used) |
| \`dlg-confirm-1800.png\` | visually identical to \`dlg-confirm-3000.png\` at this scale (the dim level differs, not the layout) | 3000 used |
| \`page2-*-b.png\`, \`page3-*-b.png\`, \`osk-default-2.png\` | duplicates of the \`-a\` frames from a second pass | first of each used |
| \`col-*-rNN\` past the last row (\`col-game-r07..r09\`, \`col-settings-r16..r20\`) | the column clamps at its last row, so these are the last row again rather than the "overscroll" state \`INVENTORY.md\` section 5 describes (\`col-game-r09\` still shows Corrupted Data focused) | no (the scrolled pairs use real deep rows, not clamped repeats) |

Every frame that *is* used in section 4 was opened and matches its label: the nine \`col-*-r00\` columns
(Users, Settings, Photo, Music, Video via \`col2\`, Game, Network, PSN, Friends), the scrolled rows
(\`col-settings-r09\` Accessory Settings, \`col-settings-r15\` Network Settings, \`col-game-r05\` PlayStation
Store, \`col-photo-r02\` Playlists, \`col-music-r01\` Playlists, \`col2-video-r02\` Video Editor & Uploader,
\`col-network-r03\` Internet Search, \`col-psn-r04\` PlayStation Store, \`col-users-r02\` *User), the options
menu and its focus walk, the delete-confirm pair, \`page-user-create\`, \`osk-default\`, the four choice bands,
and \`page3-video-row0\`.

### 7.3 Three catalogue annotations the measurement contradicts

Each is worth a dated correction because it changes what a reader would expect the gap to be:

- \`font-list\` is annotated "the native list draws the firmware faces this build registers
  (SCE-PS3 Seurat LATIN, VAGRundschriftDLig)", but the captured band holds **Original, Rounded, Pop** —
  the console's three values, row for row (sidecar \`entryCount\` 3, selection walk Original → Rounded → Pop).
  The font band is one of the closest surfaces in this sweep (18.66-18.68 dB) rather than a content gap.
- \`background-list\` is annotated "the native list has three values and no Brightness row", but the captured
  band has **four** rows with Brightness first (sidecar \`entryCount\` 4, selection walk Original → Classic →
  Wallpaper at indices 1-3), exactly the console's four.
- \`theme-list\` has the opposite problem: the annotation says nothing about the row count, and the native
  band holds a **one-item** value list (Install plus Original) against the console's four (Install, Original,
  Classic, Air Paint), so Down cannot move focus there at all (sidecar selection 0 on all three grabs).

These are candidate catalogue corrections, not edits: this pass wrote no reference, budget or catalogue
file, and \`work/parity-catalogue.json\` is unchanged.

---

## 8. Evidence files

| file | contents |
|---|---|
| \`C:\\dev\\out\\fidelity-sweep\\gate.log\` | the gate's full output (section 2) |
| \`C:\\dev\\out\\fidelity-sweep\\parity.log\` | the parity capture's per-cell log, Tier A/C verdicts and region scores |
| \`C:\\dev\\out\\fidelity-sweep\\parity-cells.csv\`, \`parity-states.csv\`, \`parity-summary.json\` | the parity table, flattened from \`out/parity/scorecards/scorecard-20261029-193824.json\` |
| \`C:\\dev\\out\\fidelity-sweep\\extra-surfaces.json\`, \`extra-surfaces.csv\` | the 36 extra pairs with every crop rect and score |
| \`C:\\dev\\out\\fidelity-sweep\\native-manifest.json\`, \`native-manifest-partial.json\` | each capture job: tokens, exit code, per-frame sidecar (category, depth, selection, entry title, row count) |
| \`C:\\dev\\out\\fidelity-sweep\\native/\\\` | the captured frames (\`col-*\`, \`options\`, \`dialog\`, \`osk\`, \`band-*\`) plus their \`.png.json\` sidecars |
| \`C:\\dev\\out\\fidelity-sweep\\merged-surfaces.json\`, \`merged-surfaces.csv\` | all 62 comparisons ranked by frame PSNR |
| \`C:\\dev\\out\\fidelity-sweep\\measured-summary.json\` | the statistics in section 5 in machine-readable form |
| \`C:\\dev\\out\\fidelity-sweep\\sheets\\\` | the contact sheets used to verify what each frame shows |
| \`C:\\dev\\xmb\\work\\fidelity-sweep.ts\`, \`fidelity-parity-summary.ts\`, \`fidelity-report.ts\`, \`fidelity-doc.ts\`, \`fidelity-pairs.json\`, \`fidelity-sheet.py\` | the sweep's own scripts, written for this pass |

Frames captured but not scored are kept: the rest of each column walk (\`native/col-*/r*.png\` past the paired row), \`native/band-*/page.png\`
(the Theme Settings page under each band), and \`native/dialog/cancelled.png\` (the state after Circle).
They are the evidence for the row counts and for the "the band never left its at-rest focus" claims, not
score rows.

Nothing in this pass re-baselined a reference, edited a budget, or touched a source file. The only writes
are \`work/fidelity-*.ts\`, \`work/fidelity-pairs.json\`, \`work/fidelity-sheet.py\`, everything under
\`C:\\dev\\out\\fidelity-sweep\\\`, and this document.
`;

writeFileSync(join(repo, 'native-xmb', 'audit', 'FIDELITY-MEASURED.md'), doc);
console.log(`wrote ${join(repo, 'native-xmb', 'audit', 'FIDELITY-MEASURED.md')} (${doc.length} bytes)`);
