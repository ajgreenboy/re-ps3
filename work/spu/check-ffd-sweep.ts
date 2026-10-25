// Matches the native ffd_shader1 deformation lattice (XMB_RIBBON_FFD_SWEEP output: _Time then 539 x xyz) against the
// 8704-byte FFD lattices the PPU hands spline.elf (RPCS3 XMB_SPU_DUMP log), finding the best _Time for each.
// Usage: bun check-ffd-sweep.ts <sweep file> <dump dir> [count]
import { readFileSync } from "node:fs";
const [sweepPath, dir, countArg] = process.argv.slice(2);
const sweep = readFileSync(sweepPath, "utf8").trim().split("\n").map(l => l.split(" ").map(Number));
const ids = readFileSync(`${dir}/dma.txt`, "utf8").split("\n").filter(l => l.includes("size 8704")).map(l => l.split(" ")[0].padStart(5, "0"));
const count = +(countArg ?? 12), step = Math.max(1, Math.floor(ids.length / count));
for (let n = 1; n < ids.length; n += step) {
  const b = readFileSync(`${dir}/dma_${ids[n]}_get.bin`);
  const cap = [...Array(539 * 3)].map((_, k) => b.readFloatBE(Math.floor(k / 3) * 16 + (k % 3) * 4));
  if (cap.every((v, k) => Math.abs(v - [0, -0.469999, 0][k % 3]) < 1e-5)) { console.log(`aux ${ids[n]}: constant (field not yet rendered)`); continue; }
  let best = { err: Infinity, time: 0 };
  for (let r = 0; r + 1 < sweep.length; r++) for (let f = 0; f <= 1; f += 0.02) { let err = 0;
    for (let k = 0; k < cap.length; k++) err = Math.max(err, Math.abs(sweep[r][k + 1] * (1 - f) + sweep[r + 1][k + 1] * f - cap[k]));
    if (err < best.err) best = { err, time: sweep[r][0] * (1 - f) + sweep[r + 1][0] * f }; }
  console.log(`aux ${ids[n]}: best _Time ${best.time.toFixed(2)}, worst abs error ${best.err.toExponential(2)}`);
}
