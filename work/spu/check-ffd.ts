// Tests a hypothesis for spline.elf's per-point stage (func_3c68 FFD + func_3bf0 matrix) against a local-store
// snapshot: params at LS 0xd590/0xd5a0/0xd5b0, matrix at 0xb300, 11x7x7 FFD lattice at 0xb380, transformed lattice at
// 0x9c00. The untransformed lattice is taken from the run's logged 5776-byte gets (dma_*_get.bin); the best one wins.
// Tries the lattice index orders and both matrix orientations. Usage: bun check-ffd.ts <dir> <ls_list_N.bin>
import { readFileSync, readdirSync, statSync } from "node:fs";
const [dir, lsName] = process.argv.slice(2);
const ls = readFileSync(`${dir}/${lsName}`);
const f4 = (b: Buffer, o: number) => [0, 4, 8, 12].map(k => b.readFloatBE(o + k));
const scale = f4(ls, 0xd590), extent = f4(ls, 0xd5a0), origin = f4(ls, 0xd5b0);
const M = [0, 1, 2, 3].map(r => f4(ls, 0xb300 + r * 16));
const aux = (i: number) => f4(ls, 0xb380 + i * 16);
const target = (i: number) => f4(ls, 0x9c00 + i * 16);
const B = (t: number) => [(1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t * t + 4) / 6, (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6, t ** 3 / 6];
const orders: Record<string, (x: number, y: number, z: number) => number> = {
  "x+11y+77z": (x, y, z) => x + 11 * y + 77 * z, "49x+7y+z": (x, y, z) => 49 * x + 7 * y + z, "7x+77z?": (x, y, z) => 7 * x + y + 77 * z,
};
const transform = (p: number[], order: (x: number, y: number, z: number) => number, rowMajor: boolean) => {
  const n = [0, 1, 2].map(k => (p[k] - origin[k]) / scale[k]), q = n.map(x => Math.min(0.999, Math.max(0, x)));
  const cell = [q[0] * 8, q[1] * 4, q[2] * 4], ix = cell.map(Math.floor), w = cell.map((c, k) => B(c - ix[k]));
  const sum = [0, 0, 0];
  for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) for (let c = 0; c < 4; c++) {
    const v = aux(order(ix[0] + a, ix[1] + b, ix[2] + c)), wt = w[0][a] * w[1][b] * w[2][c];
    for (let k = 0; k < 3; k++) sum[k] += wt * v[k];
  }
  // The firmware keeps the unclamped normalized point for the sum; only the cell lookup is clamped.
  const d = [0, 1, 2].map(k => (n[k] + sum[k]) * extent[k]).concat(1);
  return [0, 1, 2, 3].map(r => [0, 1, 2, 3].reduce((s, c) => s + (rowMajor ? M[r][c] : M[c][r]) * d[c], 0));
};
const gets = process.argv[4] ? [process.argv[4]] : readdirSync(dir).filter(f => /^dma_[0-9]+_get[.]bin$/.test(f) && statSync(`${dir}/${f}`).size === 5776);
let best = { err: Infinity, label: "" };
for (const g of gets) {
  const lat = readFileSync(`${dir}/${g}`);
  for (const [name, order] of Object.entries(orders)) for (const rowMajor of [true, false]) {
    let worst = 0;
    for (let i = 0; i < 361; i++) { const p = transform(f4(lat, i * 16), order, rowMajor), t = target(i); worst = Math.max(worst, ...p.map((x, k) => Math.abs(x - t[k]))); }
    if (worst < best.err) best = { err: worst, label: `${g} order ${name} ${rowMajor ? "row" : "column"}-major` };
  }
}
console.log(`params scale ${scale.map(x => x.toFixed(4))} extent ${extent.map(x => x.toFixed(4))} origin ${origin.map(x => x.toFixed(4))}`);
console.log(`best: ${best.label}, worst abs error ${best.err.toExponential(3)} (over ${gets.length} logged lattices)`);
