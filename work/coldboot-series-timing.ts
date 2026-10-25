// Script time of each coldboot.raf event from an XMB_FP_SERIES capture of sce01 (records.tsv + params from
// --decode-fp): t0 is fitted from the logo's linear alpha (0 -> 1 over first_wait), then the light, blur, footer and
// fade tracks are inverted to the time each one started, for comparison with coldboot.jsx (1.5 / 2.55 / 3.3 / 4.3 s).
// Usage: bun coldboot-series-timing.ts <split dir> <logo texture offset> <blur offset> <footer offset>
import { readFileSync } from "node:fs";
const [dir, logoTex, blurTex, footerTex] = process.argv.slice(2);
const recs = readFileSync(`${dir}/records.tsv`, "utf8").trim().split("\n").slice(1).map(l => {
  const f = l.split("\t"); const p: Record<string, number[]> = {};
  for (const x of readFileSync(`${dir}/r_${f[0].padStart(5, "0")}.params.txt`, "utf8").trim().split("\n")) { const [k, v] = x.split(" = "); p[k] = v.split(" ").map(Number); }
  return { t: +f[2] / 1e6, tex: f[4].split(":")[0], a: p["_Material[3]"][0], lx: p["_SpotLight[0]"][0], col: p["_LightingMisc[0]"][2] };
});
const logo = recs.filter(r => r.tex === logoTex), blur = recs.filter(r => r.tex === blurTex), footer = recs.filter(r => r.tex === footerTex);
// t0: least squares on t_host - 1.5 a for 0 < a < 1 during the fade-in
const fade = logo.filter(r => r.a > 0 && r.a < 1 && r.t < logo[0].t + 2);
const t0s = fade.map(r => r.t - 1.5 * r.a); const t0 = t0s.reduce((a, b) => a + b) / t0s.length;
const spread = Math.max(...t0s) - Math.min(...t0s);
console.log(`t0 from ${fade.length} fade-in samples, spread ${(spread * 1000).toFixed(2)} ms; first draw at ${((logo[0].t - t0) * 1000).toFixed(1)} ms`);
// light: x from -1.15 to 1.85 linear over 1.3 s
const lt = logo.filter(r => r.lx > -1.15 && r.lx < 1.85 && r.t - t0 < 4).map(r => r.t - t0 - 1.3 * (r.lx + 1.15) / 3);
console.log(`light move start ${(lt.reduce((a, b) => a + b) / lt.length).toFixed(4)} s (n ${lt.length}, spread ${((Math.max(...lt) - Math.min(...lt)) * 1000).toFixed(1)} ms)`);
const firstAbove = (rs: typeof recs, from: number, pred: (r: typeof recs[0]) => boolean) => rs.find(r => r.t - t0 > from && pred(r));
const show = (name: string, r?: typeof recs[0]) => console.log(`${name}: ${r ? ((r.t - t0)).toFixed(4) + " s, value " + r.a.toFixed(4) : "none"}`);
show("blur first > 0", firstAbove(blur, 0, r => r.a > 0));
show("blur first < 1 after full", firstAbove(blur, 3.1, r => r.a < 1));
show("footer first > 0", firstAbove(footer, 0, r => r.a > 0));
show("logo first < 1 (fade out)", firstAbove(logo, 3, r => r.a < 1));
show("logo last drawn", logo[logo.length - 1]);
// footer fade-in is linear over 0.5 s: invert
const ft = footer.filter(r => r.a > 0 && r.a < 1 && r.t - t0 < 4).map(r => r.t - t0 - 0.5 * r.a);
console.log(`footer fade-in start ${(ft.reduce((a, b) => a + b) / ft.length).toFixed(4)} s (n ${ft.length})`);
for (const r of logo.filter(r => r.t - t0 > 4.25 && r.t - t0 < 5.6 && Math.round((r.t - t0) * 60) % 6 === 0)) console.log(`  fade t ${(r.t - t0).toFixed(4)} logo a ${r.a.toFixed(5)} light x ${r.lx.toFixed(4)}`);
