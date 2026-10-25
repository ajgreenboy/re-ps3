// Replays ribbon.cpp's LINE1 boot schedule (coldboot1 -> coldboot2 filter, then the eased move to the clock preset)
// at one step per spline task run (1/60 s, as the cloth clock shows) and compares FFD SCALE2 with the task data the
// PPU hands spline.elf (RPCS3 XMB_SPU_DUMP log). The capture ran in the yoake hold (05:00-07:00).
// Usage: bun check-presets.ts <dump dir>
import { readFileSync } from "node:fs";
const dir = process.argv[2];
const ids = readFileSync(`${dir}/dma.txt`, "utf8").split("\n").filter(l => l.includes("ea 0x200dda00 size 256")).map(l => l.split(" ")[0].padStart(5, "0")).slice(1);
const cap = ids.map(id => { const b = readFileSync(`${dir}/dma_${id}_get.bin`); return [64, 68, 72].map(o => b.readFloatBE(o)); });
const coldboot1 = [2.82755, 0.999999, 2.85341], coldboot2 = [3.2, 1.27579, 2.88782], yoake = [3.2, 1.27473, 2.88939];
const mix = (a: number[], b: number[], t: number) => a.map((x, i) => x + (b[i] - x) * t);
let cur = coldboot1, snap: number[] | undefined, worst = 0, at = 0;
for (let k = 0; k < Math.min(cap.length, 600); k++) {
  const t = k / 60;
  if (k > 0) {
    if (t < 229 / 60) cur = mix(cur, coldboot2, 0.01);
    else { snap ??= cur; const n = (t - 229 / 60) * 60;
      let p = 1.4e-5 * n * n; if (n > 112) { const s = Math.min(1, (n - 112) / 56), s2 = s * s, s3 = s2 * s; p = (2*s3 - 3*s2 + 1) * 1.4e-5 * 112 * 112 + (s3 - 2*s2 + s) * 56 * 2 * 1.4e-5 * 112 + (-2*s3 + 3*s2); }
      cur = mix(snap, yoake, Math.min(1, p)); }
  }
  const e = Math.max(...cur.map((x, i) => Math.abs(x - cap[k][i]))); if (e > worst) { worst = e; at = k; }
  if (k % 40 === 0) console.log(`step ${k}: model ${cur.map(x => x.toFixed(4))} capture ${cap[k].map(x => x.toFixed(4))}`);
}
console.log(`worst abs error ${worst.toExponential(2)} at step ${at} (SCALE2 x range 2.83..3.2)`);
