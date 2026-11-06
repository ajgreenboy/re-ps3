// Reads the PC watch's bulk dumps of custom_render_plugin's 32 x 16 flow field (sim+0x180, signed-byte XYZ, 0x600
// bytes) and prints, per dump, the guest time of that watch hit, the non-zero cells and the largest |x|,|y|,|z|.
// Usage: bun work/flow-grid.ts <trace.txt> <bulk prefix>   (files <prefix>_<seq>.bin, seq = the watch hit number)
import { readdirSync, readFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';

const [tracePath, prefix] = process.argv.slice(2);
const tbBySeq = new Map<number, number>();
for (const line of readFileSync(tracePath, 'utf8').split('\n')) {
  const seq = Number(line.split(' ', 1)[0]); const tb = /tb=(\d+)/.exec(line);
  if (Number.isFinite(seq) && tb) tbBySeq.set(seq, Number(tb[1]) / 79.8e6);
}
const dir = dirname(prefix), stem = basename(prefix) + '_';
const files = readdirSync(dir).filter(f => f.startsWith(stem) && f.endsWith('.bin'))
  .map(f => ({ f, seq: Number(f.slice(stem.length, -4)) })).sort((a, b) => a.seq - b.seq);
let last = '';
for (const { f, seq } of files) {
  const b = readFileSync(join(dir, f));
  let cells = 0; const max = [0, 0, 0]; const hot: string[] = [];
  for (let c = 0; c < 512; c++) {
    const v = [0, 1, 2].map(k => (b[c * 3 + k] << 24) >> 24);
    if (v.some(x => x !== 0)) { cells++; if (hot.length < 6) hot.push(`(${c % 32},${c >> 5})=${v.join('/')}`); }
    v.forEach((x, k) => { max[k] = Math.max(max[k], Math.abs(x)); });
  }
  const row = `${(tbBySeq.get(seq) ?? NaN).toFixed(2)}s seq ${seq}: ${cells} cells, max ${max.join('/')} ${hot.join(' ')}`;
  const key = `${cells} ${max.join('/')}`;
  if (cells || key !== last) console.log(row);
  last = key;
}
