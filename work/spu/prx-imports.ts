// Lists a PRX's imported functions: library, NID, stub-table address, and the name from a library,nid,name CSV.
// Module info sits at segment 0's vaddr + p_paddr - p_offset (as RPCS3's PRX loader reads it).
// Usage: bun prx-imports.ts <prx> [names.csv] [stub-address-hex ...]
import { readFileSync, existsSync } from "node:fs";
const [path, namesPath, ...filters] = process.argv.slice(2);
const elf = readFileSync(path);
const phoff = Number(elf.readBigUInt64BE(0x20)), phentsize = elf.readUInt16BE(0x36), phnum = elf.readUInt16BE(0x38);
const segs = [...Array(phnum)].map((_, i) => { const o = phoff + i * phentsize;
  return { type: elf.readUInt32BE(o), offset: Number(elf.readBigUInt64BE(o + 8)), vaddr: Number(elf.readBigUInt64BE(o + 16)), paddr: Number(elf.readBigUInt64BE(o + 24)), filesz: Number(elf.readBigUInt64BE(o + 32)) }; });
const at = (va: number) => { const s = segs.find(s => s.type === 1 && va >= s.vaddr && va < s.vaddr + s.filesz); if (!s) throw new Error(`0x${va.toString(16)} unmapped`); return s.offset + va - s.vaddr; };
const cstr = (va: number) => { if (!va) return "?"; let o = at(va), s = ""; while (elf[o]) s += String.fromCharCode(elf[o++]); return s; };
const names = new Map<string, string>();
if (namesPath && existsSync(namesPath)) for (const line of readFileSync(namesPath, "utf8").split("\n")) { const [lib, nid, name] = line.split(","); if (nid && name) names.set(nid.trim().toLowerCase().replace(/^0x/, ""), `${lib}.${name.trim()}`); }
const s0 = segs[0], info = at(s0.vaddr + s0.paddr - s0.offset);
const start = elf.readUInt32BE(info + 0x2c), end = elf.readUInt32BE(info + 0x30);
const wanted = new Set(filters.map(f => parseInt(f, 16)));
for (let p = start; p < end;) {
  const o = at(p), size = elf[o], count = elf.readUInt16BE(o + 6);
  const lib = cstr(elf.readUInt32BE(o + 0x10)), nids = elf.readUInt32BE(o + 0x14), stubs = elf.readUInt32BE(o + 0x18);
  for (let i = 0; i < count; i++) { const nid = elf.readUInt32BE(at(nids + i * 4)).toString(16).padStart(8, "0"), stub = stubs + i * 4;
    if (!wanted.size || wanted.has(stub)) console.log(`${lib} ${nid} stub 0x${stub.toString(16)} ${names.get(nid) ?? ""}`); }
  p += size || 0x2c;
}
