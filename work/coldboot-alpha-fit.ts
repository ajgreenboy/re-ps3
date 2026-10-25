// Fits back_colours0's _Alpha during the cold boot (XMB_FP_SERIES of the 8608-byte program, decoded with
// --decode-fp lines:lib/moyou/back_colours0.fpo) against candidate curves of the time since the first draw.
// Usage: bun coldboot-alpha-fit.ts <split dir>
import { readFileSync } from "node:fs";
const dir = process.argv[2];
const recs = readFileSync(`${dir}/records.tsv`, "utf8").trim().split("\n").slice(1).map(l => {
  const f = l.split("\t"); const p: Record<string, number> = {};
  for (const x of readFileSync(`${dir}/r_${f[0].padStart(5, "0")}.params.txt`, "utf8").trim().split("\n")) { const [k, v] = x.split(" = "); p[k] = +v.split(" ")[0]; }
  return { t: +f[2] / 1e6, a: p._Alpha, nb: p._NightBrightness };
});
const t0 = recs[0].t;
const s = recs.filter(r => r.t - t0 < 5.5).map(r => ({ t: r.t - t0, a: r.a }));
const curves: Record<string, (x: number) => number> = {
  smoothstep: x => x * x * (3 - 2 * x),
  smootherstep: x => x * x * x * (x * (6 * x - 15) + 10),
  sine: x => (1 - Math.cos(Math.PI * x)) / 2,
  quadInOut: x => x < .5 ? 2 * x * x : 1 - 2 * (1 - x) ** 2,
};
for (const [name, f] of Object.entries(curves)) {
  let best = [9, 0, 0];
  for (let T = 5; T <= 10; T += .05) for (let d = -.3; d <= .3; d += .005) {
    const e = Math.sqrt(s.reduce((e, r) => e + (f(Math.min(1, Math.max(0, (r.t + d) / T))) - r.a) ** 2, 0) / s.length);
    if (e < best[0]) best = [e, T, d];
  }
  console.log(name.padEnd(13), "rms", best[0].toFixed(5), "T", best[1].toFixed(2), "offset", best[2].toFixed(3));
}
// with T fixed at 7.5 s (DAT_00091e20)
for (const [name, f] of Object.entries(curves)) {
  let best = [9, 0];
  for (let d = -.3; d <= .3; d += .002) { const e = Math.sqrt(s.reduce((e, r) => e + (f(Math.min(1, Math.max(0, (r.t + d) / 7.5))) - r.a) ** 2, 0) / s.length); if (e < best[0]) best = [e, d]; }
  console.log(name.padEnd(13), "T=7.5 rms", best[0].toFixed(5), "offset", best[1].toFixed(3));
}
