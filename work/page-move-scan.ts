// Pointer / word scanner for the local decrypted firmware modules (read-only).
//
// Purpose: find every place a 32-bit value occurs in a module, as a load address
// (VA), so that OPD descriptors, vtable slots and constant-table references can be
// located without a disassembler. Used by audit/PAGE-MOVE.md phase 14.1.
//
// Usage:
//   bun work/page-move-scan.ts <elf> word <hexvalue> [<hexvalue> ...]
//       print the VA of every big-endian u32 equal to any of the values
//   bun work/page-move-scan.ts <elf> opd <hexcode-va>
//       find 8-byte OPD descriptors whose first word is the given code VA and print
//       the descriptor VA plus its second word (TOC)
//   bun work/page-move-scan.ts <elf> ptr <hexva> [...]
//       find the VA of every 4-byte word equal to a pointer to the given addresses
//   bun work/page-move-scan.ts <elf> dump <hexva> <words> [float|u32|str]
//       dump words from a VA with float / u32 / ascii columns
//
// VA mapping uses the PT_LOAD program headers (no assumption about file layout).
import { readFileSync } from "node:fs";

const argv = process.argv.slice(2);
const path = argv[0];
if (!path) {
  console.error("usage: bun work/page-move-scan.ts <elf> word|opd|ptr|dump ...");
  process.exit(2);
}
const buf = readFileSync(path);
const is64 = buf[4] === 2;
const phoff = is64 ? Number(buf.readBigUInt64BE(0x20)) : buf.readUInt32BE(0x1c);
const phentsize = is64 ? buf.readUInt16BE(0x36) : buf.readUInt16BE(0x2a);
const phnum = is64 ? buf.readUInt16BE(0x38) : buf.readUInt16BE(0x2c);

type Seg = { type: number; offset: number; vaddr: number; filesz: number; memsz: number };
const segs: Seg[] = [];
for (let i = 0; i < phnum; ++i) {
  const p = phoff + i * phentsize;
  const type = buf.readUInt32BE(p);
  const offset = is64 ? Number(buf.readBigUInt64BE(p + 8)) : buf.readUInt32BE(p + 4);
  const vaddr = is64 ? Number(buf.readBigUInt64BE(p + 16)) : buf.readUInt32BE(p + 8);
  const filesz = is64 ? Number(buf.readBigUInt64BE(p + 32)) : buf.readUInt32BE(p + 16);
  const memsz = is64 ? Number(buf.readBigUInt64BE(p + 40)) : buf.readUInt32BE(p + 20);
  segs.push({ type, offset, vaddr, filesz, memsz });
}
const loads = segs.filter((s) => s.type === 1);

function va2off(va: number): number {
  for (const s of loads) if (va >= s.vaddr && va < s.vaddr + s.filesz) return s.offset + (va - s.vaddr);
  return -1;
}
function off2va(off: number): number {
  for (const s of loads) if (off >= s.offset && off < s.offset + s.filesz) return s.vaddr + (off - s.offset);
  return -1;
}
const hx = (n: number) => "0x" + n.toString(16);

const mode = argv[1];
if (mode === "word" || mode === "ptr") {
  const values = argv.slice(2).map((v) => Number(v));
  const hits: string[] = [];
  for (const s of loads) {
    for (let off = s.offset; off + 4 <= s.offset + s.filesz; off += 4) {
      const w = buf.readUInt32BE(off);
      if (values.includes(w)) hits.push(`${hx(off2va(off))}\t${hx(w)}`);
    }
  }
  console.log(hits.length ? hits.join("\n") : "(no hits)");
} else if (mode === "opd") {
  const code = Number(argv[2]);
  const hits: string[] = [];
  for (const s of loads) {
    for (let off = s.offset; off + 8 <= s.offset + s.filesz; off += 8) {
      if (buf.readUInt32BE(off) === code) hits.push(`opd ${hx(off2va(off))} -> fn ${hx(code)} toc ${hx(buf.readUInt32BE(off + 4))}`);
    }
  }
  console.log(hits.length ? hits.join("\n") : "(no opd)");
} else if (mode === "dump") {
  const va = Number(argv[2]);
  const words = Number(argv[3] ?? 8);
  const off0 = va2off(va);
  if (off0 < 0) {
    console.error(`${hx(va)} not in a file-backed PT_LOAD`);
    process.exit(3);
  }
  for (let i = 0; i < words; ++i) {
    const off = off0 + i * 4;
    const u = buf.readUInt32BE(off);
    const f = buf.readFloatBE(off);
    let chars = "";
    for (let b = 0; b < 4; ++b) {
      const c = buf[off + b];
      chars += c >= 32 && c < 127 ? String.fromCharCode(c) : ".";
    }
    console.log(`${hx(va + i * 4)}\t${hx(u).padStart(10)}\t${f.toFixed(6).padStart(14)}\t${chars}`);
  }
} else {
  console.error(`unknown mode ${mode}`);
  process.exit(2);
}
