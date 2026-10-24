// Read big-endian words from a decrypted PS3 PRX (ELF64) at Ghidra virtual addresses (image base 0).
// Usage: bun work/prx-read.ts <file.prx> <addr>[:count] ...   prints each word as hex, float and int.
import { readFileSync } from "node:fs";

const [file, ...specs] = process.argv.slice(2);
if (!file || specs.length === 0) {
  console.error("usage: bun work/prx-read.ts <file.prx> <addr>[:count] ...");
  process.exit(1);
}
const data = readFileSync(file);
const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
const phoff = Number(view.getBigUint64(0x20));
const phentsize = view.getUint16(0x36);
const phnum = view.getUint16(0x38);
const segments: { vaddr: number; offset: number; filesz: number }[] = [];
for (let i = 0; i < phnum; i++) {
  const p = phoff + i * phentsize;
  if (view.getUint32(p) !== 1) continue; // PT_LOAD
  segments.push({ offset: Number(view.getBigUint64(p + 8)), vaddr: Number(view.getBigUint64(p + 16)), filesz: Number(view.getBigUint64(p + 32)) });
}
const toOffset = (vaddr: number) => {
  for (const s of segments) if (vaddr >= s.vaddr && vaddr < s.vaddr + s.filesz) return s.offset + vaddr - s.vaddr;
  return -1;
};
for (const spec of specs) {
  const [a, c] = spec.split(":");
  const addr = Number(a);
  const count = c ? Number(c) : 1;
  for (let i = 0; i < count; i++) {
    const at = addr + i * 4;
    const off = toOffset(at);
    if (off < 0) { console.log(`${at.toString(16).padStart(8, "0")}  (not in a loaded segment)`); continue; }
    console.log(`${at.toString(16).padStart(8, "0")}  ${view.getUint32(off).toString(16).padStart(8, "0")}  ${view.getFloat32(off)}  ${view.getInt32(off)}`);
  }
}
