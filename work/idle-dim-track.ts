// Tracks the sysconf idle dim on a cburst of a Settings page (crop 500,420,1400,340): per frame, the text ink of
// the second row's label and value and of its icon bullet, each as the summed luma excess over a per-column
// background interpolated from bands just above and below the box. Printed relative to the mean of a reference
// window before the dim. Usage: bun work/idle-dim-track.ts <burst dir> [refFromMs refToMs]
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import { decodePng } from './ab-compare';

const dir = process.argv[2];
const refFrom = Number(process.argv[3] ?? 1800), refTo = Number(process.argv[4] ?? 3000);
type Box = [number, number, number, number]; // x0, y0, x1, y1 (crop pixels)
const boxes: Record<string, Box> = { label: [120, 242, 390, 284], value: [795, 250, 835, 276], icon: [50, 248, 80, 278] };
const luma = (d: Uint8Array, i: number) => 0.2126 * d[i] + 0.7152 * d[i + 1] + 0.0722 * d[i + 2];
function ink(img: { w: number; rgba: Uint8Array }, [x0, y0, x1, y1]: Box) {
  let sum = 0;
  for (let x = x0; x < x1; x++) {
    let above = 0, below = 0;
    for (let y = y0 - 6; y < y0 - 2; y++) above += luma(img.rgba, (y * img.w + x) * 4) / 4;
    for (let y = y1 + 2; y < y1 + 6; y++) below += luma(img.rgba, (y * img.w + x) * 4) / 4;
    for (let y = y0; y < y1; y++) {
      const bg = above + (below - above) * (y - y0) / (y1 - y0);
      sum += Math.max(0, luma(img.rgba, (y * img.w + x) * 4) - bg);
    }
  }
  return sum;
}
const frames = readdirSync(dir).filter(f => /^f_\d+\.png$/.test(f)).map(f => ({ f, ms: Number(f.slice(2, -4)) })).sort((a, b) => a.ms - b.ms);
const rows = frames.map(({ f, ms }) => { const img = decodePng(join(dir, f)); return { ms, ...Object.fromEntries(Object.entries(boxes).map(([k, b]) => [k, ink(img, b)])) } as Record<string, number>; });
const ref: Record<string, number> = {};
for (const k of Object.keys(boxes)) { const r = rows.filter(r => r.ms >= refFrom && r.ms <= refTo); ref[k] = r.reduce((s, r) => s + r[k], 0) / r.length; }
console.log('ms ' + Object.keys(boxes).join(' '));
for (const r of rows) console.log(r.ms + ' ' + Object.keys(boxes).map(k => (r[k] / ref[k]).toFixed(3)).join(' '));
