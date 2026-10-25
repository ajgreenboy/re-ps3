// For the row-0 vertices of a captured output tile, finds the surface parameter u that reproduces each vertex
// (v = 0 there), to recover spline.elf's column -> u mapping. Usage: bun fit-u.ts <ls_list_N.bin> <list_N.txt>
import { readFileSync } from "node:fs";
const [lsPath, listPath] = process.argv.slice(2);
const ls = readFileSync(lsPath), list = readFileSync(listPath, "utf8").split("\n");
const lsa = parseInt(list[0].match(/lsa (0x[0-9a-f]+)/)![1], 16);
const vertex0 = (parseInt(list[1].match(/ea (0x[0-9a-f]+)/)![1], 16) - 0x20980000) / 32, row0 = Math.floor(vertex0 / 128), col0 = vertex0 % 128;
const f4 = (o: number) => [0, 4, 8, 12].map(k => ls.readFloatBE(o + k));
const L = (r: number, c: number) => f4(0x9c00 + (r * 19 + c) * 16);
const B = (t: number) => [(1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t * t + 4) / 6, (-3 * t ** 3 + 3 * t * t + 3 * t + 1) / 6, t ** 3 / 6];
const at = (u: number) => { const s = Math.min(15, Math.floor(u)), t = u - s, w = B(t), wv = B(0), p = [0, 0, 0, 0];
  for (let a = 0; a < 4; a++) for (let b = 0; b < 4; b++) { const q = L(a, s + b); for (let k = 0; k < 4; k++) p[k] += wv[a] * w[b] * q[k]; } return p; };
if (row0 !== 0) throw new Error("tile must be in row 0");
for (let c = 0; c < 8; c++) {
  const got = f4(lsa + c * 32); let best = 0, bestErr = Infinity;
  for (let u = 0; u <= 16; u += 1e-4) { const p = at(u), e = Math.hypot(p[0] - got[0], p[1] - got[1], p[2] - got[2]); if (e < bestErr) { bestErr = e; best = u; } }
  console.log(`col ${col0 + c}: u ${best.toFixed(4)}  (col*16/127 = ${((col0 + c) * 16 / 127).toFixed(4)}, col/8 = ${((col0 + c) / 8).toFixed(4)})  err ${bestErr.toExponential(2)}`);
}
