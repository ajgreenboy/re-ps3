// Dust motion from a burst of frames (refcap3 `burst`/`cburst` output: f_<ms>.png, optional rect.txt). Specks are
// small bright blobs (luma above a 9x9 box-mean background by `--thresh`, 1..`--max-area` px, so icons and glyph
// strokes drop out); each speck is paired with its mutual nearest neighbour in the frame closest to `--gap` ms later
// (within `--radius` px). Per window of `--window` ms it prints the pair count, the median speed (px/s) and the
// mean vx / vy (px/s, +y down) inside the strip x in [--x0, --x1) (screen px) and outside it.
// Usage: bun work/dust-flow.ts <burst dir> [--gap 100] [--window 250] [--x0 480] [--x1 680] [--thresh 22]
//        [--max-area 12] [--radius 10] [--from ms] [--to ms] [--clip x,y,w,h]  (screen px; drops specks outside,
//        so a full-frame burst and a crop burst cover the same region)
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { decodePng } from './ab-compare';

const args = process.argv.slice(2);
const dir = args[0];
const opt = (name: string, fallback: number) => { const i = args.indexOf('--' + name); return i >= 0 ? Number(args[i + 1]) : fallback; };
const gap = opt('gap', 100), win = opt('window', 250), x0 = opt('x0', 480), x1 = opt('x1', 680);
const thresh = opt('thresh', 22), maxArea = opt('max-area', 12), radius = opt('radius', 10);
const from = opt('from', -Infinity), to = opt('to', Infinity);
const clipArg = args.indexOf('--clip') >= 0 ? args[args.indexOf('--clip') + 1].split(',').map(Number) : null;
let offX = 0, offY = 0;
if (existsSync(join(dir, 'rect.txt'))) {
  const m = /x=(\d+) y=(\d+)/.exec(readFileSync(join(dir, 'rect.txt'), 'utf8')); if (m) { offX = +m[1]; offY = +m[2]; }
}
const frames = readdirSync(dir).filter(f => /^f_\d+\.png$/.test(f)).map(f => ({ f, ms: Number(f.slice(2, -4)) }))
  .filter(f => f.ms >= from - gap && f.ms <= to + gap).sort((a, b) => a.ms - b.ms);

type Speck = { x: number; y: number };
const cache = new Map<string, Speck[]>();
function specks(file: string): Speck[] {
  const hit = cache.get(file); if (hit) return hit;
  const img = decodePng(join(dir, file)); const { w, h, rgba } = img;
  const L = new Float32Array(w * h);
  for (let i = 0; i < w * h; i++) L[i] = 0.2126 * rgba[i * 4] + 0.7152 * rgba[i * 4 + 1] + 0.0722 * rgba[i * 4 + 2];
  // 9x9 box mean via an integral image.
  const I = new Float64Array((w + 1) * (h + 1));
  for (let y = 0; y < h; y++) { let row = 0; for (let x = 0; x < w; x++) { row += L[y * w + x]; I[(y + 1) * (w + 1) + x + 1] = I[y * (w + 1) + x + 1] + row; } }
  const mean = (x: number, y: number) => {
    const xa = Math.max(0, x - 4), xb = Math.min(w, x + 5), ya = Math.max(0, y - 4), yb = Math.min(h, y + 5);
    return (I[yb * (w + 1) + xb] - I[ya * (w + 1) + xb] - I[yb * (w + 1) + xa] + I[ya * (w + 1) + xa]) / ((xb - xa) * (yb - ya));
  };
  const on = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (L[y * w + x] - mean(x, y) > thresh) on[y * w + x] = 1;
  const seen = new Uint8Array(w * h); const out: Speck[] = [];
  for (let i = 0; i < w * h; i++) {
    if (!on[i] || seen[i]) continue;
    const stack = [i]; seen[i] = 1; let n = 0, sx = 0, sy = 0, sw = 0;
    while (stack.length) {
      const k = stack.pop()!; const kx = k % w, ky = (k / w) | 0; const wt = L[k];
      n++; sx += kx * wt; sy += ky * wt; sw += wt;
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = kx + dx, ny = ky + dy; if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const q = ny * w + nx; if (on[q] && !seen[q]) { seen[q] = 1; stack.push(q); }
      }
    }
    const px = sx / sw + offX, py = sy / sw + offY;
    const inClip = !clipArg || (px >= clipArg[0] && py >= clipArg[1] && px < clipArg[0] + clipArg[2] && py < clipArg[1] + clipArg[3]);
    if (n <= maxArea && inClip) out.push({ x: px, y: py });
  }
  cache.set(file, out); return out;
}
function nearest(p: Speck, list: Speck[]) {
  let best = -1, d2 = radius * radius;
  for (let i = 0; i < list.length; i++) { const dx = list[i].x - p.x, dy = list[i].y - p.y, d = dx * dx + dy * dy; if (d < d2) { d2 = d; best = i; } }
  return best;
}
type Pair = { t: number; x: number; vx: number; vy: number };
const pairs: Pair[] = [];
for (let i = 0; i < frames.length; i++) {
  const a = frames[i]; if (a.ms < from || a.ms > to) continue;
  let j = i + 1; while (j < frames.length && frames[j].ms - a.ms < gap) j++;
  if (j >= frames.length) break;
  const b = frames[j], dt = (b.ms - a.ms) / 1000;
  const A = specks(a.f), B = specks(b.f);
  for (const p of A) {
    const k = nearest(p, B); if (k < 0) continue;
    if (nearest(B[k], A) !== A.indexOf(p)) continue; // mutual
    pairs.push({ t: a.ms, x: p.x, vx: (B[k].x - p.x) / dt, vy: (B[k].y - p.y) / dt });
  }
  cache.delete(frames[Math.max(0, i - 2)].f);
}
const median = (v: number[]) => { if (!v.length) return NaN; const s = [...v].sort((p, q) => p - q); return s[s.length >> 1]; };
const stat = (ps: Pair[]) => ps.length ? `${String(ps.length).padStart(4)} p50 ${median(ps.map(p => Math.hypot(p.vx, p.vy))).toFixed(1).padStart(5)} vx ${(ps.reduce((s, p) => s + p.vx, 0) / ps.length).toFixed(1).padStart(6)} vy ${(ps.reduce((s, p) => s + p.vy, 0) / ps.length).toFixed(1).padStart(6)}` : '   0';
console.log(`window(ms)      strip x ${x0}..${x1}                     | outside`);
const t0 = Math.min(...pairs.map(p => p.t)), t1 = Math.max(...pairs.map(p => p.t));
for (let t = Math.floor(t0 / win) * win; t <= t1; t += win) {
  const inWin = pairs.filter(p => p.t >= t && p.t < t + win);
  console.log(`${String(t).padStart(5)}..${String(t + win).padStart(5)}  ${stat(inWin.filter(p => p.x >= x0 && p.x < x1))} | ${stat(inWin.filter(p => p.x < x0 || p.x >= x1))}`);
}
