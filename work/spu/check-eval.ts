// Checks a hypothesis for spline.elf's surface evaluation against a local-store snapshot taken at an output list put
// (RPCS3 XMB_SPU_WATCH): the transformed 19x19 lattice at LS 0x9c00 and the 8x8 output tile at the list's LS address.
// Usage: bun check-eval.ts <ls_list_N.bin> <list_N.txt>
import { readFileSync } from "node:fs";
const [lsPath, listPath] = process.argv.slice(2);
const ls = readFileSync(lsPath);
const list = readFileSync(listPath, "utf8").split("\n");
const lsa = parseInt(list[0].match(/lsa (0x[0-9a-f]+)/)![1], 16);
const firstEa = parseInt(list[1].match(/ea (0x[0-9a-f]+)/)![1], 16);
const vertex0 = (firstEa - 0x20980000) / 32, row0 = Math.floor(vertex0 / 128), col0 = vertex0 % 128;
const f4 = (o: number) => [0, 4, 8, 12].map(k => ls.readFloatBE(o + k));
const L = (r: number, c: number) => f4(0x9c00 + (r * 19 + c) * 16);
const bspline = (t: number) => [(1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t * t + 4) / 6, (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6, t ** 3 / 6];
const seg = (i: number) => { const u = i * 16 / 127; const s = Math.min(15, Math.floor(u)); return [s, u - s] as const; };
let worst = 0;
for (let r = 0; r < 8; r++) for (let c = 0; c < 8; c++) {
  const [sv, tv] = seg(row0 + r), [su, tu] = seg(col0 + c), wv = bspline(tv), wu = bspline(tu);
  const p = [0, 0, 0, 0];
  for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) { const q = L(sv + a, su + b); for (let k = 0; k < 4; k++) p[k] += wv[a] * wu[b] * q[k]; }
  const got = f4(lsa + r * 256 + c * 32);
  const err = Math.max(...p.map((x, k) => Math.abs(x - got[k])));
  worst = Math.max(worst, err);
  if (r < 2 && c < 3) console.log(`r${row0 + r} c${col0 + c} predicted ${p.map(x => x.toFixed(5)).join(" ")} | actual ${got.map(x => x.toFixed(5)).join(" ")}`);
}
console.log(`tile at row ${row0} col ${col0}: worst abs error ${worst.toExponential(3)}`);
