// Joins records.tsv with the r_NNNNN.params.txt files from --decode-fp into one row per (vblank, actor texture):
// the time-varying sce01 inputs. Usage: bun coldboot-series-table.ts <split dir> [texture offset filter]
import { readFileSync, existsSync } from "node:fs";
const dir = process.argv[2], filter = process.argv[3];
const lines = readFileSync(`${dir}/records.tsv`, "utf8").trim().split("\n").slice(1);
let first = -1;
for (const line of lines) {
  const f = line.split("\t"), i = f[0].padStart(5, "0");
  if (filter && !f[4].startsWith(filter)) continue;
  const pf = `${dir}/r_${i}.params.txt`; if (!existsSync(pf)) continue;
  const p: Record<string, string> = {};
  for (const l of readFileSync(pf, "utf8").trim().split("\n")) { const [k, v] = l.split(" = "); p[k] = v; }
  const vblank = +f[1]; if (first < 0) first = vblank;
  const model = f[8].split(",");
  console.log([vblank - first, f[3], f[4].split(":")[0], `m3=${p["_Material[3]"]}`, `L=${[0, 1, 2].map(k => p[`_SpotLight[${k}]`].split(" ")[0]).join(",")}`,
    `att=${[0, 1, 2].map(k => p[`_SpotLight[${k}]`].split(" ")[1]).join(",")}`, `col=${[0, 1, 2].map(k => p[`_LightingMisc[${k}]`].split(" ")[2]).join(",")}`,
    `amb=${[0, 1, 2].map(k => p[`_LightingMisc[${k}]`].split(" ")[0]).join(",")}`, `spec=${[0, 1, 2].map(k => p[`_SpotLight[${k}]`].split(" ")[3]).join(",")}`, `pos=${model[3]},${model[7]},${model[11]}`, `scl=${model[0]},${model[5]}`].join("  "));
}
