// Compares captured ribbon normals (vertex + 16) with derivatives of the clip-space B-spline surface, to find
// spline.elf's normal convention. Usage: bun check-normal.ts <ls_list_N.bin> <list_N.txt>
import { readFileSync } from "node:fs";
const [lsPath, listPath] = process.argv.slice(2);
const ls = readFileSync(lsPath), list = readFileSync(listPath, "utf8").split("\n");
const lsa = parseInt(list[0].match(/lsa (0x[0-9a-f]+)/)![1], 16);
const vertex0 = (parseInt(list[1].match(/ea (0x[0-9a-f]+)/)![1], 16) - 0x20980000) / 32, row0 = Math.floor(vertex0 / 128), col0 = vertex0 % 128;
const f4 = (o: number) => [0, 4, 8, 12].map(k => ls.readFloatBE(o + k));
const L = (r: number, c: number) => f4(0x9c00 + (r * 19 + c) * 16);
const B = (t: number) => [(1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t * t + 4) / 6, (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6, t ** 3 / 6];
const D = (t: number) => [-((1 - t) ** 2) / 2, (3 * t * t - 4 * t) / 2, (-3 * t * t + 2 * t + 1) / 2, t * t / 2];
const param = (i: number) => [Math.floor(i / 8), (i % 8) * 16 / 127] as const;
const surface = (r: number, c: number, wu: (t: number) => number[], wv: (t: number) => number[]) => {
  const [sv, tv] = param(r), [su, tu] = param(c), a_ = wv(tv), b_ = wu(tu), p = [0, 0, 0, 0];
  for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) { const q = L(sv + a, su + b); for (let k = 0; k < 4; k++) p[k] += a_[a] * b_[b] * q[k]; }
  return p;
};
const cross = (a: number[], b: number[]) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
for (const [r, c] of [[0, 0], [1, 1], [3, 5]]) {
  const p = surface(row0 + r, col0 + c, B, B), pu = surface(row0 + r, col0 + c, D, B), pv = surface(row0 + r, col0 + c, B, D);
  const n = f4(lsa + r * 256 + c * 32 + 16), x = cross(pu, pv), x2 = cross(pv, pu);
  console.log(`r${row0 + r} c${col0 + c} normal ${n.map(v => v.toFixed(5)).join(" ")}`);
  console.log(`   pu ${pu.map(v => v.toFixed(5)).join(" ")} pv ${pv.map(v => v.toFixed(5)).join(" ")}`);
  console.log(`   pu x pv ${x.map(v => v.toFixed(5)).join(" ")}  ratio ${x.map((v, k) => (n[k] / v).toFixed(4)).join(" ")}`);
  console.log(`   pv x pu ratio ${x2.map((v, k) => (n[k] / v).toFixed(4)).join(" ")}`);
}
