// Mean luminance of a region (fractions of the frame) for each frame of a timestamped capture (<dir>/f_<ms>.png),
// e.g. to time when an item's help text appears. Usage: bun region-luma.ts <dir> x0 x1 y0 y1 [from-ms] [to-ms]
import { readdirSync } from "node:fs";
import { decodePng } from "./ab-compare.ts";

const [dir, ...rest] = process.argv.slice(2);
const [x0, x1, y0, y1] = rest.slice(0, 4).map(Number);
const from = Number(rest[4] ?? 0), to = Number(rest[5] ?? Infinity);
for (const f of readdirSync(dir).filter(f => /^f_\d+\.png$/.test(f)).sort()) {
  const t = Number(f.slice(2, -4));
  if (t < from || t > to) continue;
  const { w, h, rgba } = decodePng(`${dir}/${f}`);
  let sum = 0, n = 0;
  for (let y = Math.floor(h * y0); y < Math.floor(h * y1); y++) for (let x = Math.floor(w * x0); x < Math.floor(w * x1); x++) {
    const i = (y * w + x) * 4; sum += 0.299 * rgba[i] + 0.587 * rgba[i + 1] + 0.114 * rgba[i + 2]; n++;
  }
  console.log(`${t} ${(sum / n).toFixed(2)}`);
}
