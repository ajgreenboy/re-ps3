// Finds big-endian u32 occurrences of the virtual address of a file offset in a PPU ELF (pointer tables).
// Usage: bun coldboot-elf-find-ptr.ts <elf> <file offset hex>...
const u = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer()); const dv = new DataView(u.buffer);
const phoff = Number(dv.getBigUint64(0x20)), phnum = dv.getUint16(0x38), phentsize = dv.getUint16(0x36);
const segs = [...Array(phnum)].map((_, i) => { const o = phoff + i * phentsize; return { type: dv.getUint32(o), off: Number(dv.getBigUint64(o + 8)), vaddr: Number(dv.getBigUint64(o + 16)), filesz: Number(dv.getBigUint64(o + 32)) }; });
const toV = (off: number) => { const s = segs.find(s => s.type === 1 && off >= s.off && off < s.off + s.filesz); return s ? off - s.off + s.vaddr : -1; };
const toOff = (v: number) => { const s = segs.find(s => s.type === 1 && v >= s.vaddr && v < s.vaddr + s.filesz); return s ? v - s.vaddr + s.off : -1; };
for (const a of process.argv.slice(3)) {
  const v = toV(parseInt(a, 16)); const hits: string[] = [];
  for (let i = 0; i + 4 <= u.length; i += 4) if (dv.getUint32(i) === v) hits.push(`file 0x${i.toString(16)} = vaddr 0x${toV(i).toString(16)}`);
  console.log(`off 0x${a} -> vaddr 0x${v.toString(16)}:`, hits.join("; ") || "none");
}
export {};
