// Read the parity scorecard this sweep produced and flatten it into the tables the measured document
// needs: per cell (state x theme x background x resolution) with its frame score, its budget, its gating
// failures by region name, and its informative (contentDiffers) regions.
//
//   bun work/fidelity-parity-summary.ts <scorecard.json>
//
// Writes C:/dev/out/fidelity-sweep/parity-cells.csv, parity-states.csv, parity-summary.json.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, basename } from 'node:path';

const outRoot = 'C:/dev/out/fidelity-sweep';
const card = JSON.parse(readFileSync(process.argv[2], 'utf8'));
mkdirSync(outRoot, { recursive: true });

const fmt = (n: number | undefined | null, digits = 3) => (typeof n === 'number' ? n.toFixed(digits) : '');

const cells = (card.cells ?? []).map((cell: any) => {
  const frame = (cell.regions ?? []).find((r: any) => r.name === 'frame');
  const gatingFails = (cell.regions ?? []).filter((r: any) => r.gating && r.pass === false).map((r: any) => r.name);
  const informative = (cell.regions ?? []).filter((r: any) => r.contentDiffers).map((r: any) => r.name);
  const nonGatingFails = (cell.regions ?? []).filter((r: any) => !r.gating && r.pass === false).map((r: any) => r.name);
  return {
    key: cell.key, state: cell.state, theme: cell.theme, background: cell.background, resolution: cell.resolution,
    class: card.states?.find((s: any) => s.state === cell.state)?.class ?? '',
    width: cell.width, height: cell.height,
    tierA: cell.tierA, tierB: cell.tierB, tierC: cell.tierC, claim: cell.claim,
    frameDb: frame?.psnr, frameSsim: frame?.ssim, budgetDb: frame?.minPsnr, budgetSsim: frame?.minSsim,
    framePass: frame?.pass, ribbonBandPsnr: cell.ribbonBandPsnr,
    gatingFails, nonGatingFails, informative,
    regions: (cell.regions ?? []).map((r: any) => ({
      name: r.name, rect: r.crop, psnr: r.psnr, ssim: r.ssim, minPsnr: r.minPsnr, minSsim: r.minSsim,
      gating: r.gating, contentDiffers: r.contentDiffers, pass: r.pass,
      contentDiffersReason: r.contentDiffersReason,
    })),
    summary: cell.summary, reference: cell.reference, error: cell.error,
  };
});

const header = ['state', 'theme', 'background', 'resolution', 'class', 'tier_a', 'tier_b', 'tier_c', 'claim',
  'frame_db', 'frame_ssim', 'budget_db', 'budget_ssim', 'frame_pass', 'ribbon_band_db',
  'gating_fails', 'informative_regions', 'non_gating_fails', 'summary'];
const lines = [header.join(',')];
for (const cell of cells) {
  lines.push([cell.state, cell.theme, cell.background, cell.resolution, cell.class, cell.tierA, cell.tierB,
    cell.tierC, cell.claim, fmt(cell.frameDb), fmt(cell.frameSsim, 4), fmt(cell.budgetDb), fmt(cell.budgetSsim, 4),
    String(cell.framePass), fmt(cell.ribbonBandPsnr), cell.gatingFails.join('|'), cell.informative.join('|'),
    cell.nonGatingFails.join('|'), cell.summary].map(v => `"${String(v ?? '').replaceAll('"', '""')}"`).join(','));
}
writeFileSync(join(outRoot, 'parity-cells.csv'), `${lines.join('\n')}\n`);

const states = card.states ?? [];
const stateHeader = ['state', 'title', 'class', 'reachable', 'tier_a', 'tier_b', 'tier_c', 'claim', 'verdict',
  'content_differs_regions', 'cells'];
const stateLines = [stateHeader.join(',')];
for (const state of states) {
  stateLines.push([state.state, state.title, state.class, state.reachable, state.tierA, state.tierB, state.tierC,
    state.claim, state.verdict, state.contentDiffers, state.cells]
    .map(v => `"${String(v ?? '').replaceAll('"', '""')}"`).join(','));
}
writeFileSync(join(outRoot, 'parity-states.csv'), `${stateLines.join('\n')}\n`);

const byClass = new Map<string, { n: number; db: number; ssim: number }>();
for (const cell of cells) {
  if (typeof cell.frameDb !== 'number') continue;
  const entry = byClass.get(cell.class) ?? { n: 0, db: 0, ssim: 0 };
  entry.n += 1; entry.db += cell.frameDb; entry.ssim += cell.frameSsim;
  byClass.set(cell.class, entry);
}
const classAverages = [...byClass.entries()].map(([name, entry]) => ({
  class: name, cells: entry.n, meanFrameDb: entry.db / entry.n, meanFrameSsim: entry.ssim / entry.n,
}));

const gatingFailCounts = new Map<string, number>();
for (const cell of cells) for (const name of cell.gatingFails)
  gatingFailCounts.set(name, (gatingFailCounts.get(name) ?? 0) + 1);

const nearBudget = cells.filter((cell: any) => typeof cell.frameDb === 'number'
  && cell.frameDb >= (cell.budgetDb - 1) && cell.frameDb < cell.budgetDb);
const withinBudget = cells.filter((cell: any) => typeof cell.frameDb === 'number' && cell.frameDb >= cell.budgetDb);

writeFileSync(join(outRoot, 'parity-summary.json'), `${JSON.stringify({
  schema: 'xmb-fidelity-parity-summary/1',
  scorecard: process.argv[2], scorecardName: basename(process.argv[2]),
  generated: card.generated, run: card.run, exe: card.exe, exeSha256: card.exeSha256, pins: card.pins,
  budgets: card.budgets?.classes,
  totals: card.totals,
  classAverages,
  gatingFailCounts: Object.fromEntries([...gatingFailCounts.entries()].sort((a, b) => b[1] - a[1])),
  frameWithinBudget: withinBudget.length,
  frameWithinOneDb: withinBudget.length + nearBudget.length,
  cells, states,
  annotations: card.annotations,
  missingReferences: card.missingReferences,
  unavailable: card.unavailable,
  nativeOnly: card.nativeOnly,
}, null, 2)}\n`);

console.log(`cells ${cells.length} states ${states.length}`);
console.log(`class averages ${classAverages.map(c => `${c.class} ${c.meanFrameDb.toFixed(2)} (${c.cells})`).join(', ')}`);
console.log(`gating failures ${JSON.stringify(Object.fromEntries(gatingFailCounts))}`);
console.log(`frame >= budget ${withinBudget.length}, within 1 dB below ${nearBudget.length}`);
