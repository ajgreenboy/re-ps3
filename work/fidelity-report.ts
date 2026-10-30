// Merge the two measured tables (parity cells and the extra surfaces) into one ranked list, and print the
// summary statistics the fidelity document quotes.
//
//   bun work/fidelity-report.ts
//
// Reads C:/dev/out/fidelity-sweep/parity-summary.json and extra-surfaces.json, writes merged-surfaces.json,
// merged-surfaces.csv and measured-summary.json.
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const outRoot = 'C:/dev/out/fidelity-sweep';
const parity = JSON.parse(readFileSync(join(outRoot, 'parity-summary.json'), 'utf8'));
const extras = JSON.parse(readFileSync(join(outRoot, 'extra-surfaces.json'), 'utf8'));

const budgets: Record<string, { psnrDb: number; ssim: number }> = parity.budgets;
const rows: any[] = [];

for (const cell of parity.cells) {
  if (typeof cell.frameDb !== 'number' || !cell.budgetDb) continue;
  rows.push({
    source: 'parity', id: cell.key, surface: `parity cell ${cell.state} (${cell.theme}/${cell.background}/${cell.resolution})`,
    class: cell.class, db: cell.frameDb, ssim: cell.frameSsim, budgetDb: cell.budgetDb, budgetSsim: cell.budgetSsim,
    pass: cell.frameDb >= cell.budgetDb && cell.frameSsim >= cell.budgetSsim,
    tierA: cell.tierA, tierB: cell.tierB, tierC: cell.tierC,
    gatingFails: cell.gatingFails.join(' | '), informative: cell.informative.join(' | '), contentDiffers: false,
  });
}
for (const pair of extras.pairs) {
  if (typeof pair.frame?.db !== 'number') continue;
  const budget = budgets[pair.class] ?? { psnrDb: 18, ssim: 0.85 };
  rows.push({
    source: 'extra', id: pair.id, surface: pair.surface, class: pair.class, db: pair.frame.db, ssim: pair.frame.ssim,
    budgetDb: budget.psnrDb, budgetSsim: budget.ssim,
    pass: pair.frame.db >= budget.psnrDb && pair.frame.ssim >= budget.ssim,
    crops: Object.fromEntries(Object.entries(pair).filter(([key, value]: any) => value && typeof value === 'object' && 'rect' in value)
      .map(([key, value]: any) => [key, { db: value.db, ssim: value.ssim }])),
    contentDiffers: pair.contentDiffers, note: pair.note,
  });
}

rows.sort((a, b) => a.db - b.db);
writeFileSync(join(outRoot, 'merged-surfaces.json'), `${JSON.stringify({ schema: 'xmb-fidelity-merged/1', generatedAt: new Date().toISOString(), rows }, null, 2)}\n`);
const header = ['rank', 'source', 'id', 'surface', 'class', 'frame_db', 'frame_ssim', 'budget_db', 'budget_ssim', 'pass', 'db_margin', 'content_differs', 'gating_fails', 'note'];
const lines = [header.join(',')];
rows.forEach((row, index) => {
  lines.push([index + 1, row.source, row.id, row.surface, row.class, row.db.toFixed(3), row.ssim.toFixed(4),
    row.budgetDb.toFixed(2), row.budgetSsim.toFixed(4), String(row.pass), (row.db - row.budgetDb).toFixed(3),
    String(row.contentDiffers ?? ''), row.gatingFails ?? '', row.note ?? '']
    .map(value => `"${String(value).replaceAll('"', '""')}"`).join(','));
});
writeFileSync(join(outRoot, 'merged-surfaces.csv'), `${lines.join('\n')}\n`);

const mean = (values: number[]) => values.reduce((a, b) => a + b, 0) / values.length;
const byClass = new Map<string, number[]>();
for (const row of rows) byClass.set(row.class, [...(byClass.get(row.class) ?? []), row.db]);
const classAverages = [...byClass.entries()].map(([name, values]) => ({ class: name, n: values.length, meanDb: mean(values) }));

const within = (margin: number) => rows.filter(row => row.db >= row.budgetDb - margin);
const passing = rows.filter(row => row.pass);
const summary = {
  schema: 'xmb-fidelity-measured-summary/1',
  measuredSurfaces: rows.length,
  parityCellsWithReference: rows.filter(row => row.source === 'parity').length,
  extraPairs: rows.filter(row => row.source === 'extra').length,
  framePass: passing.length,
  frameFail: rows.length - passing.length,
  withinOneDbOfBudget: within(1).length,
  withinThirdDbOfBudget: within(1 / 3).length,
  atOrAboveBudget: within(0).length,
  classAverages,
  classAveragesBySource: {
    parity: parity.classAverages,
    extra: classAverages.filter(entry => rows.some(row => row.source === 'extra' && row.class === entry.class)),
  },
  gatingFailCounts: parity.gatingFailCounts,
  worst15: rows.slice(0, 15).map(row => ({ id: row.id, surface: row.surface, class: row.class, db: Number(row.db.toFixed(2)), ssim: Number(row.ssim.toFixed(4)), budgetDb: row.budgetDb, contentDiffers: row.contentDiffers ?? false })),
  contentDiffersPairs: rows.filter(row => row.contentDiffers).length,
  contentDiffersPairsPassingFrame: rows.filter(row => row.contentDiffers && row.pass).length,
  nonContentDiffersFail: rows.filter(row => !row.contentDiffers && !row.pass).length,
};
writeFileSync(join(outRoot, 'measured-summary.json'), `${JSON.stringify(summary, null, 2)}\n`);
console.log(JSON.stringify(summary, null, 1));
