// Runs the whole spline.elf model (FFD -> matrix -> bicubic surface + normal) on a ribbon draw's captured inputs
// (draw_NNNN.lat/.mat/.aux from RPCS3 XMB_VTX_DUMP) and compares it with each 128x128 block of the draw's vertex
// buffer (draw_NNNN.a0, stride 32: clip position then normal). The FFD params are the values seen in LS snapshots.
// Usage: bun check-pipeline.ts <dir> <draw number> [<draw number>...]
import { readFileSync } from "node:fs";
const [dir, ...draws] = process.argv.slice(2);
const f4 = (b: Buffer, o: number) => [0, 4, 8, 12].map(k => b.readFloatBE(o + k));
const scale = [5.67726, 1.00077, 1], origin = [0, -0.47, 0];
const B = (t: number) => [(1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t * t + 4) / 6, (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6, t ** 3 / 6];
const D = (t: number) => [-((1 - t) ** 2) / 2, (3 * t * t - 4 * t) / 2, (-3 * t * t + 2 * t + 1) / 2, t * t / 2];
const cross = (a: number[], b: number[]) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

export function evaluate(lat: Buffer, mat: Buffer, aux: Buffer, extent = [3.2, 1.2747, 2.8894]) {
  const M = [0, 1, 2, 3].map(r => f4(mat, r * 16));
  const L: number[][] = [];
  for (let i = 0; i < 361; i++) {
    const p = f4(lat, i * 16), n = [0, 1, 2].map(k => (p[k] - origin[k]) / scale[k]);
    const cell = n.map((x, k) => Math.min(0.999, Math.max(0, x)) * [8, 4, 4][k]), ix = cell.map(Math.floor), w = cell.map((c, k) => B(c - ix[k]));
    const sum = [0, 0, 0];
    for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) for (let c = 0; c < 4; c++) {
      const v = f4(aux, ((ix[0] + a) + 11 * (ix[1] + b) + 77 * (ix[2] + c)) * 16), wt = w[0][a] * w[1][b] * w[2][c];
      for (let k = 0; k < 3; k++) sum[k] += wt * v[k];
    }
    const d = [0, 1, 2].map(k => (n[k] + sum[k]) * extent[k]).concat(1);
    L.push([0, 1, 2, 3].map(c => d.reduce((s, v, r) => s + v * M[r][c], 0)));
  }
  const param = (i: number) => [Math.floor(i / 8), (i % 8) * 16 / 127] as const;
  const surface = (r: number, c: number, wu: (t: number) => number[], wv: (t: number) => number[]) => {
    const [sv, tv] = param(r), [su, tu] = param(c), a_ = wv(tv), b_ = wu(tu), p = [0, 0, 0, 0];
    for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) { const q = L[(sv + a) * 19 + su + b]; for (let k = 0; k < 4; k++) p[k] += a_[a] * b_[b] * q[k]; }
    return p;
  };
  const out: number[][] = [];
  for (let r = 0; r < 128; r++) for (let c = 0; c < 128; c++)
    out.push([...surface(r, c, B, B), ...cross(surface(r, c, B, D), surface(r, c, D, B)), 0]);
  return out;
}

if (import.meta.main) for (const n of draws) {
  const name = `${dir}/draw_${n.padStart(4, "0")}`;
  const model = evaluate(readFileSync(`${name}.lat`), readFileSync(`${name}.mat`), readFileSync(`${name}.aux`));
  const a0 = readFileSync(`${name}.a0`), blocks = a0.length / (16384 * 32);
  const report: string[] = [];
  for (let blk = 0; blk < blocks; blk++) {
    let pos = 0, nrm = 0;
    for (let i = 0; i < 16384; i += 37) {
      const o = (blk * 16384 + i) * 32, got = [...f4(a0, o), ...f4(a0, o + 16)];
      for (let k = 0; k < 4; k++) pos = Math.max(pos, Math.abs(got[k] - model[i][k]));
      for (let k = 4; k < 8; k++) nrm = Math.max(nrm, Math.abs(got[k] - model[i][k]));
    }
    report.push(`block ${blk}: pos ${pos.toExponential(2)} normal ${nrm.toExponential(2)}`);
  }
  console.log(`draw ${n}: ${report.join(" | ")}`);
}
