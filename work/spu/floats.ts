// Prints a big-endian binary as rows of four floats (and hex): bun floats.ts <file> [rows] [offsetRows]
import { readFileSync } from "node:fs";
const [path, rows = "24", skip = "0"] = process.argv.slice(2);
const b = readFileSync(path);
for (let r = +skip; r < Math.min(+skip + +rows, b.length / 16); r++) {
  const w = [0, 4, 8, 12].map(k => b.readUInt32BE(r * 16 + k)), f = [0, 4, 8, 12].map(k => b.readFloatBE(r * 16 + k));
  console.log(`${(r * 16).toString(16).padStart(5, "0")}: ${w.map(x => x.toString(16).padStart(8, "0")).join(" ")}  ${f.map(x => Math.abs(x) < 1e-30 || Math.abs(x) > 1e9 ? "    -     " : x.toPrecision(5).padStart(10)).join(" ")}`);
}
