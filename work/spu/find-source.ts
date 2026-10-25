// Searches a memory dump for the bytes of a captured ribbon vertex buffer, to find where the data was produced.
// Usage: bun find-source.ts <draw_NNNN.a0> <memory dump> <dump base address hex>
import { readFileSync } from "node:fs";
const [vbPath, dumpPath, baseHex] = process.argv.slice(2);
const vb = readFileSync(vbPath), dump = readFileSync(dumpPath), base = parseInt(baseHex, 16);
const swap32 = (b: Buffer) => { const c = Buffer.from(b); for (let i = 0; i + 4 <= c.length; i += 4) c.writeUInt32LE(b.readUInt32BE(i), i); return c; };
// a few vertices spread over the grid: position (16 bytes) and normal (next 16 bytes)
for (const v of [0, 1, 127, 128, 8256, 16383]) {
  for (const [what, off] of [["pos", 0], ["nrm", 16]] as const) {
    const key = vb.subarray(v * 32 + off, v * 32 + off + 12); // xyz only: w may differ
    const hits: string[] = [];
    for (const [order, k] of [["be", key], ["le", swap32(key)]] as const) {
      let at = dump.indexOf(k);
      while (at >= 0 && hits.length < 4) { hits.push(`${order}@0x${(base + at).toString(16)}`); at = dump.indexOf(k, at + 1); }
    }
    console.log(`v${v} ${what} [${[0, 4, 8].map(i => vb.readFloatBE(v * 32 + off + i).toFixed(4)).join(" ")}]: ${hits.join(" ") || "not found"}`);
  }
}
