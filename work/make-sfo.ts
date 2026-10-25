// Write a minimal PARAM.SFO (PSF 1.1) for local tests of installed-content listing.
// Usage: bun work/make-sfo.ts <out PARAM.SFO> KEY=VALUE ...   (all values written as UTF-8 strings)
import { writeFileSync } from "node:fs";

const [out, ...pairs] = process.argv.slice(2);
if (!out || pairs.length === 0) { console.error("usage: bun work/make-sfo.ts <file> KEY=VALUE ..."); process.exit(1); }
const entries = pairs.map(p => { const i = p.indexOf("="); return { key: p.slice(0, i), value: Buffer.from(p.slice(i + 1) + "\0", "utf8") }; })
  .sort((a, b) => a.key.localeCompare(b.key));
const keyTable = Buffer.concat(entries.map(e => Buffer.from(e.key + "\0", "latin1")));
const padded = (n: number) => (n + 3) & ~3;
const keyStart = 20 + entries.length * 16, dataStart = padded(keyStart + keyTable.length);
const header = Buffer.alloc(dataStart);
header.write("\0PSF", 0, "latin1"); header.writeUInt32LE(0x101, 4); header.writeUInt32LE(keyStart, 8); header.writeUInt32LE(dataStart, 12); header.writeUInt32LE(entries.length, 16);
let keyOffset = 0, dataOffset = 0; const data: Buffer[] = [];
entries.forEach((e, i) => {
  const at = 20 + i * 16, max = padded(e.value.length);
  header.writeUInt16LE(keyOffset, at); header.writeUInt16LE(0x0204, at + 2); header.writeUInt32LE(e.value.length, at + 4); header.writeUInt32LE(max, at + 8); header.writeUInt32LE(dataOffset, at + 12);
  keyOffset += e.key.length + 1; data.push(Buffer.concat([e.value, Buffer.alloc(max - e.value.length)])); dataOffset += max;
});
keyTable.copy(header, keyStart);
writeFileSync(out, Buffer.concat([header, ...data]));
console.log(`wrote ${out} (${entries.length} entries)`);
