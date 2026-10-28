// Read-only float scan of a decrypted PRX/ELF (PS3, big-endian): where do the native layout
// constants exist in the firmware modules? Prints the vaddr of every exact float32/float64 match.
// Usage: bun work/p2-plugin-float-scan.ts <elf> <value> [value...]
import { readFileSync } from 'node:fs';

const [, , path, ...raw] = process.argv;
if (!path || !raw.length) throw new Error('Usage: bun work/p2-plugin-float-scan.ts <elf> <value> [value...]');
const targets = raw.map(Number);
const b = readFileSync(path);
if (b.readUInt32BE(0) !== 0x7f454c46) throw new Error('not an ELF');
const is64 = b[4] === 2;
const phoff = is64 ? Number(b.readBigUInt64BE(0x20)) : b.readUInt32BE(0x1c);
const phentsize = is64 ? b.readUInt16BE(0x36) : b.readUInt16BE(0x2a);
const phnum = is64 ? b.readUInt16BE(0x38) : b.readUInt16BE(0x2c);
const segs: { off: number; vaddr: number; filesz: number; flags: number }[] = [];
for (let i = 0; i < phnum; i++) {
  const o = phoff + i * phentsize;
  if (is64) segs.push({ flags: b.readUInt32BE(o + 4), off: Number(b.readBigUInt64BE(o + 8)), vaddr: Number(b.readBigUInt64BE(o + 16)), filesz: Number(b.readBigUInt64BE(o + 32)) });
  else segs.push({ flags: b.readUInt32BE(o + 4), off: b.readUInt32BE(o + 4), vaddr: b.readUInt32BE(o + 8), filesz: b.readUInt32BE(o + 16) });
}
const vaOf = (off: number) => {
  for (const s of segs) if (off >= s.off && off < s.off + s.filesz) return s.vaddr + (off - s.off);
  return -1;
};
const found = new Map<number, string[]>();
for (const t of targets) found.set(t, []);
const f32 = Math.fround;
for (let off = 0; off + 8 <= b.length; off += 4) {
  const f = b.readFloatBE(off);
  if (!Number.isFinite(f) || f === 0) continue;
  for (const t of targets) if (f === f32(t)) found.get(t)!.push('f32@0x' + vaOf(off).toString(16));
}
for (let off = 0; off + 8 <= b.length; off += 4) {
  const d = b.readDoubleBE(off);
  if (!Number.isFinite(d) || d === 0) continue;
  for (const t of targets) if (d === t) found.get(t)!.push('f64@0x' + vaOf(off).toString(16));
}
console.log(`${path}: ${b.length} bytes, ${segs.length} segments`);
for (const [t, hits] of found) console.log(`${t}\t${hits.length ? hits.slice(0, 12).join(' ') : 'NOT FOUND'}`);
