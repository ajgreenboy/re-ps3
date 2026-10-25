// Reads big-endian words/floats at virtual addresses of a local PRX/ELF (64-bit, as the firmware plugins are), for
// constants and permute masks referenced by disassembly. Usage: bun prx-read.ts <prx> <hexaddr>[:count] ...
import { readFileSync } from "node:fs";
const [path, ...queries] = process.argv.slice(2);
const elf = readFileSync(path);
const phoff = Number(elf.readBigUInt64BE(0x20)), phentsize = elf.readUInt16BE(0x36), phnum = elf.readUInt16BE(0x38);
const segments = [...Array(phnum)].map((_, i) => { const o = phoff + i * phentsize;
  return { type: elf.readUInt32BE(o), offset: Number(elf.readBigUInt64BE(o + 8)), vaddr: Number(elf.readBigUInt64BE(o + 16)), filesz: Number(elf.readBigUInt64BE(o + 32)) }; });
const at = (va: number) => { const s = segments.find(s => s.type === 1 && va >= s.vaddr && va < s.vaddr + s.filesz); if (!s) throw new Error(`0x${va.toString(16)} not in a loaded segment`); return s.offset + va - s.vaddr; };
for (const q of queries) { const [a, n] = q.split(":"); const va = parseInt(a, 16), count = +(n ?? 4);
  const words = [...Array(count)].map((_, i) => elf.readUInt32BE(at(va + i * 4)));
  console.log(`${va.toString(16)}: ${words.map(w => w.toString(16).padStart(8, "0")).join(" ")}  = ${words.map(w => { const b = Buffer.alloc(4); b.writeUInt32BE(w); return +b.readFloatBE(0).toPrecision(7); }).join(" ")}`); }
