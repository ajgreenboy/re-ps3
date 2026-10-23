// Adds functions reachable only through function descriptors (OPDs) to the Ghidra function lists.
// Ghidra misses code that nothing branches to directly (module_start, exports, callbacks). On PS3 an OPD
// is a pair {u32 code, u32 toc}; any aligned pair in a module whose toc is that module's TOC and whose code
// lies in the module's executable segments marks a function entry. New entries span to the next known
// function start (xmbld translates overlaps twice, so over-long spans are harmless).
//
// With an extra file of hex addresses (one per line), those become function entries too: XenonRecomp
// marks `bl`/`b` targets that have no function as "// ERROR <addr>" in its output, and each is one.
//
// usage: bun gen-opd-functions.ts <boot493 dir> <functions dir (patched in place)> [<extra entries file>]
import { readdirSync } from "node:fs";
import { join } from "node:path";

const [imageDir, functionsDir, extraFile] = process.argv.slice(2);
if (!functionsDir) throw new Error("usage: bun gen-opd-functions.ts <boot493 dir> <functions dir> [<extra entries>]");
const extra = extraFile
  ? (await Bun.file(extraFile).text()).split(/\s+/).filter(Boolean).map((x) => parseInt(x, 16))
  : [];

const img = new Uint8Array(await Bun.file(join(imageDir, "image.bin")).arrayBuffer());
const dv = new DataView(img.buffer);
type Seg = { addr: number; size: number; off: number; exec: boolean };
const segs: Seg[] = [];
for (let i = 0, pos = 12, n = dv.getUint32(8); i < n; i++) {
  const addr = dv.getUint32(pos), size = dv.getUint32(pos + 4), flags = dv.getUint32(pos + 8);
  segs.push({ addr, size, off: pos + 12, exec: (flags & 1) !== 0 });
  pos += 12 + size;
}

type Module = { file: string; base: number; toc: number; end: number };
const modules: Module[] = [];
for (const line of (await Bun.file(join(imageDir, "modules.csv")).text()).replaceAll("\r", "").split("\n").slice(1)) {
  const [file, , base, toc] = line.split(",");
  if (file) modules.push({ file, base: Number(base), toc: Number(toc), end: 0 });
}
modules.sort((a, b) => a.base - b.base);
// vsh.elf has no TOC in modules.csv: take it from its entry OPD
const elf = new DataView(await Bun.file(join(imageDir, "image.elf")).arrayBuffer());
const read32 = (a: number) => {
  const s = segs.find((s) => a >= s.addr && a + 4 <= s.addr + s.size);
  return s ? dv.getUint32(s.off + (a - s.addr)) : undefined;
};
modules[0].toc = read32(elf.getUint32(0x18) + 4) ?? 0;
for (let i = 0; i < modules.length; i++) modules[i].end = i + 1 < modules.length ? modules[i + 1].base : 0x18d0000;

let added = 0;
for (const m of modules) {
  const csvPath = join(functionsDir, `${m.file}_functions.csv`);
  const lines = (await Bun.file(csvPath).text()).replaceAll("\r", "").split("\n");
  const starts = new Set<number>();
  const allStarts: number[] = [];
  const ranges: [number, number][] = []; // existing function bodies, [start, end)
  for (const line of lines.slice(1)) {
    const [entry, rs] = line.split(",");
    if (!entry) continue;
    const a = m.base + parseInt(entry, 16);
    starts.add(a);
    allStarts.push(a);
    for (const r of (rs ?? "").split(";")) {
      const [s, e] = r.split("-").map((x) => parseInt(x, 16));
      if (!isNaN(s) && !isNaN(e)) ranges.push([m.base + s, m.base + e + 1]);
    }
  }
  // A code pointer inside a body Ghidra already mapped is a data coincidence, not a vtable method
  const insideKnown = (a: number) => ranges.some(([s, e]) => a > s && a < e);
  const execSegs = segs.filter((s) => s.exec && s.addr >= m.base && s.addr < m.end);
  const inCode = (a: number) => execSegs.some((s) => a >= s.addr && a < s.addr + s.size);
  const found = new Set<number>();
  for (const a of extra) if (a >= m.base && a < m.end && inCode(a) && !starts.has(a)) found.add(a);
  if (extra.length) {
    // in extra-entries mode, only add those
  } else for (const s of segs.filter((s) => s.addr >= m.base && s.addr < m.end)) {
    for (let a = s.addr; a + 8 <= s.addr + s.size; a += 4) {
      if (dv.getUint32(s.off + (a - s.addr) + 4) !== m.toc) continue;
      const code = dv.getUint32(s.off + (a - s.addr));
      if (code & 3 || !inCode(code) || starts.has(code) || insideKnown(code)) continue;
      found.add(code);
    }
  }
  if (!found.size) continue;
  const sorted = [...new Set([...allStarts, ...found])].sort((a, b) => a - b);
  const hex = (x: number) => (x >>> 0).toString(16).toUpperCase().padStart(8, "0");
  for (const code of found) {
    const next = sorted.find((a) => a > code) ?? execSegs.find((s) => code >= s.addr && code < s.addr + s.size)!.addr +
      execSegs.find((s) => code >= s.addr && code < s.addr + s.size)!.size;
    const seg = execSegs.find((s) => code >= s.addr && code < s.addr + s.size)!;
    const end = Math.min(next, seg.addr + seg.size);
    lines.push(`${hex(code - m.base)},${hex(code - m.base)}-${hex(end - 1 - m.base)},opd_${hex(code)}`);
  }
  // keep the trailing-newline convention of the input
  const body = lines.filter((l, i) => l !== "" || i === 0).join("\n") + "\n";
  await Bun.write(csvPath, body);
  added += found.size;
  console.log(`${m.file}: ${found.size} OPD-only functions`);
}
console.log(`added ${added} functions`);
