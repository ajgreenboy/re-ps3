// Tracks how far the XMB's bright UI content (labels, icons) has scrolled in a timestamped capture of a key test
// (frames <dir>/f_<ms>.png, as recorded from RPCS3): per frame, the profile of pixels above a threshold across a
// region is cross-correlated with the previous frame's, and the sub-pixel shifts are summed. Prints "ms position".
// Usage: bun motion-track.ts <dir> [vertical|horizontal] [x0 x1 y0 y1 as fractions] [threshold] [max shift per frame, px]
import { readdirSync } from "node:fs";
import { decodePng } from "./ab-compare.ts";

const [dir, axis = "vertical", ...rest] = process.argv.slice(2);
const [x0, x1, y0, y1] = rest.length >= 4 ? rest.slice(0, 4).map(Number) : axis === "vertical" ? [0.33, 0.62, 0.22, 0.95] : [0.02, 0.98, 0.25, 0.42];
const threshold = rest.length >= 5 ? Number(rest[4]) : 150;
const maxShift = rest.length >= 6 ? Number(rest[5]) : 40;

function profile(path: string): Float64Array {
  const { w, h, rgba } = decodePng(path);
  const xa = Math.floor(w * x0), xb = Math.floor(w * x1), ya = Math.floor(h * y0), yb = Math.floor(h * y1);
  const vertical = axis === "vertical";
  const p = new Float64Array(vertical ? yb - ya : xb - xa);
  for (let y = ya; y < yb; y++) for (let x = xa; x < xb; x++) {
    const i = (y * w + x) * 4, l = 0.299 * rgba[i] + 0.587 * rgba[i + 1] + 0.114 * rgba[i + 2];
    if (l > threshold) p[vertical ? y - ya : x - xa] += l - threshold;
  }
  return p;
}
const corr = (a: Float64Array, b: Float64Array, s: number) => { let c = 0; for (let i = maxShift; i < a.length - maxShift; i++) c += a[i] * b[i - s]; return c; };

const files = readdirSync(dir).filter(f => /^f_\d+\.png$/.test(f)).sort();
let prev: Float64Array | undefined, pos = 0;
for (const f of files) {
  const p = profile(`${dir}/${f}`), t = Number(f.slice(2, -4));
  if (prev) {
    let best = -Infinity, bs = 0;
    for (let s = -maxShift; s <= maxShift; s++) { const c = corr(prev, p, s); if (c > best) { best = c; bs = s; } }
    const a = corr(prev, p, bs - 1), b = best, c = corr(prev, p, bs + 1), den = a - 2 * b + c;
    pos += bs + (Math.abs(den) > 1e-9 ? 0.5 * (a - c) / den : 0);
  }
  console.log(`${t} ${pos.toFixed(2)}`);
  prev = p;
}
