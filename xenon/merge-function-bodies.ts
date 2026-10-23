// Merge refreshed Ghidra bodies while retaining recovered OPD/direct/switch entries.
// bun xenon/merge-function-bodies.ts <refreshed-directory> <patched-directory> [module-file]
import { readdirSync, readFileSync, writeFileSync, renameSync } from 'node:fs';
import { resolve, join } from 'node:path';
const [freshArg, patchedArg, moduleFile] = process.argv.slice(2);
if (!patchedArg) throw new Error('expected refreshed and patched function directories');
const fresh = resolve(freshArg), patched = resolve(patchedArg);
type Row = { entry: number; ranges: [number, number][]; name: string };
function load(path: string): Map<number, Row> {
  const rows = new Map<number, Row>();
  for (const line of readFileSync(path, 'utf8').replaceAll('\r', '').split('\n')) {
    if (!line || line.startsWith('entry,')) continue;
    const [entry, ranges, ...name] = line.split(',');
    if (!/^[0-9a-f]+$/i.test(entry) || !ranges) throw new Error(`invalid function row: ${path}: ${line}`);
    const body = ranges.split(';').map(r => r.split('-').map(x => parseInt(x, 16)) as [number, number]);
    if (body.some(([s,e]) => !Number.isFinite(s) || !Number.isFinite(e) || s > e)) throw new Error(`invalid body: ${line}`);
    rows.set(parseInt(entry,16), { entry: parseInt(entry,16), ranges: body, name: name.join(',') });
  }
  return rows;
}
const hex = (n: number) => n.toString(16).toUpperCase().padStart(8, '0');
for (const file of readdirSync(fresh).filter(f => f.endsWith('_functions.csv') && (!moduleFile || f === `${moduleFile}_functions.csv`))) {
  const target = join(patched, file);
  const original = readFileSync(target, 'utf8');
  const rows = load(target);
  let added = 0, expanded = 0;
  for (const [entry, incoming] of load(join(fresh, file))) {
    const existing = rows.get(entry);
    if (!existing) { rows.set(entry, incoming); added++; continue; }
    const before = JSON.stringify(existing.ranges);
    const merged: [number, number][] = [];
    for (const [s,e] of [...existing.ranges, ...incoming.ranges].sort((a,b)=>a[0]-b[0])) {
      const tail = merged.at(-1);
      if (tail && s <= tail[1]+1) tail[1] = Math.max(tail[1], e);
      else merged.push([s,e]);
    }
    existing.ranges = merged;
    if (JSON.stringify(merged) !== before) expanded++;
  }
  const output = 'entry,ranges,name\n' + [...rows.values()].sort((a,b)=>a.entry-b.entry)
    .map(r => `${hex(r.entry)},${r.ranges.map(([s,e])=>`${hex(s)}-${hex(e)}`).join(';')},${r.name}`).join('\n') + '\n';
  if (output !== original) {
    const backup = `${target}.before-refresh`;
    // Preserve the latest pre-merge state for local recovery.
    writeFileSync(backup, original);
    writeFileSync(`${target}.tmp`, output);
    renameSync(`${target}.tmp`, target);
  }
  console.log(`${file}: added=${added} expanded/normalized=${expanded} total=${rows.size}`);
}
