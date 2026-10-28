// ELF/PRX constant reader: maps a load address (VA) to file offset and prints
// the bytes there as float / int / string. Read-only; used to recover numeric
// constants that the Ghidra decompiles only name (DAT_xxxxxxxx).
//
// Usage:
//   bun work/elf-const.ts --elf <path> --at 0x270a00 --count 16 --type float
//   bun work/elf-const.ts --elf <path> --at 0x270a00 --count 8 --type u32
//   bun work/elf-const.ts --elf <path> --str 0x270a00
//   bun work/elf-const.ts --elf <path> --segs
import { readFileSync } from "node:fs";

const argv = process.argv.slice(2);
const val = (n: string) => {
  const i = argv.indexOf(n);
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : undefined;
};
const flag = (n: string) => argv.includes(n);
const elfPath = val("--elf");
if (!elfPath) {
  console.error("usage: bun work/elf-const.ts --elf <path> (--at <va> --count n --type float|u32|i32 | --str <va> | --segs)");
  process.exit(2);
}
const buf = readFileSync(elfPath);
const is64 = buf[4] === 2;
if (buf[0] !== 0x7f || buf[1] !== 0x45 || buf[2] !== 0x4c || buf[3] !== 0x46) throw new Error("not an ELF");
const phoff = is64 ? Number(buf.readBigUInt64BE(0x20)) : buf.readUInt32BE(0x1c);
const phentsize = is64 ? buf.readUInt16BE(0x36) : buf.readUInt16BE(0x2a);
const phnum = is64 ? buf.readUInt16BE(0x38) : buf.readUInt16BE(0x2c);

type Seg = { type: number; offset: number; vaddr: number; filesz: number; memsz: number; flags: number };
const segs: Seg[] = [];
for (let i = 0; i < phnum; ++i) {
  const p = phoff + i * phentsize;
  const type = buf.readUInt32BE(p);
  const offset = is64 ? Number(buf.readBigUInt64BE(p + 8)) : buf.readUInt32BE(p + 4);
  const vaddr = is64 ? Number(buf.readBigUInt64BE(p + 16)) : buf.readUInt32BE(p + 8);
  const filesz = is64 ? Number(buf.readBigUInt64BE(p + 32)) : buf.readUInt32BE(p + 16);
  const memsz = is64 ? Number(buf.readBigUInt64BE(p + 40)) : buf.readUInt32BE(p + 20);
  const flags = is64 ? buf.readUInt32BE(p + 4) : buf.readUInt32BE(p + 24);
  segs.push({ type, offset, vaddr, filesz, memsz, flags });
}

function toOffset(va: number): { offset: number; seg: Seg } | null {
  for (const s of segs) {
    if (s.type !== 1) continue;
    if (va >= s.vaddr && va < s.vaddr + s.filesz) return { offset: s.offset + (va - s.vaddr), seg: s };
  }
  return null;
}

if (flag("--segs")) {
  console.log(`${elfPath}: ${is64 ? "ELF64" : "ELF32"} segments=${segs.length}`);
  for (const s of segs)
    console.log(`  type=${s.type} vaddr=0x${s.vaddr.toString(16)} off=0x${s.offset.toString(16)} filesz=0x${s.filesz.toString(16)} memsz=0x${s.memsz.toString(16)} flags=${s.flags}`);
  process.exit(0);
}

const atRaw = val("--at") ?? val("--str");
if (!atRaw) {
  console.error("need --at");
  process.exit(2);
}
const at = Number(atRaw);
const hit = toOffset(at);
if (!hit) {
  console.error(`VA 0x${at.toString(16)} is not inside a file-backed PT_LOAD`);
  process.exit(3);
}

if (val("--str")) {
  let end = hit.offset;
  while (end < buf.length && buf[end] !== 0) ++end;
  console.log(`0x${at.toString(16)} (file 0x${hit.offset.toString(16)}): ${JSON.stringify(buf.toString("utf8", hit.offset, end))}`);
  process.exit(0);
}

const count = Number(val("--count") ?? "1");
const type = val("--type") ?? "float";
console.log(`# ${elfPath}  VA 0x${at.toString(16)} -> file 0x${hit.offset.toString(16)}  (${count} x ${type})`);
for (let i = 0; i < count; ++i) {
  const o = hit.offset + i * 4;
  if (o + 4 > buf.length) break;
  const u = buf.readUInt32BE(o);
  const f = buf.readFloatBE(o);
  const label = `0x${(at + i * 4).toString(16)}`;
  if (type === "float") console.log(`${label}\t${f}\t(0x${u.toString(16)})`);
  else if (type === "i32") console.log(`${label}\t${buf.readInt32BE(o)}\t(0x${u.toString(16)})`);
  else console.log(`${label}\t${u}\t(0x${u.toString(16)})`);
}
