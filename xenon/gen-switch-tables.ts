// Finds PS3 (GCC, PIC) jump tables in the xmbld image and writes them as XenonRecomp [[switch]] entries.
// Pattern before each `bctr`:
//   cmplwi crN,rI,max ; bgt default          (bound: max + 1 cases)
//   lis rT,hi ; [shift] rS,rI,2 ; addi rT,rT,lo ; lwzx rE,rT,rS ; [extsw rE,rE] ; add rC,rE,rT ; mtctr rC ; bctr
// Entries are signed 32-bit offsets from the table base T = (hi << 16) + lo.
//
// usage: bun gen-switch-tables.ts <boot493 dir> <functions dir> <out switch toml> [<patched functions dir>]
// With a patched functions dir, functions whose jump tables reach past their Ghidra end get an extra
// range covering the case blocks (Ghidra doesn't follow PS3 jump tables), and the CSVs are written there.
import { mkdirSync, readdirSync } from "node:fs";
import { join } from "node:path";

const [imageDir, functionsDir, outPath, patchDir] = process.argv.slice(2);
if (!outPath) throw new Error("usage: bun gen-switch-tables.ts <boot493 dir> <functions dir> <out.toml> [<patched functions dir>]");

// ---- image ----
const img = new Uint8Array(await Bun.file(join(imageDir, "image.bin")).arrayBuffer());
const dv = new DataView(img.buffer);
type Seg = { addr: number; size: number; off: number };
const segs: Seg[] = [];
for (let i = 0, pos = 12, n = dv.getUint32(8); i < n; i++) {
  const addr = dv.getUint32(pos), size = dv.getUint32(pos + 4);
  segs.push({ addr, size, off: pos + 12 });
  pos += 12 + size;
}
const segOf = (a: number) => segs.find((s) => a >= s.addr && a + 4 <= s.addr + s.size);
const read32 = (a: number): number | undefined => {
  const s = segOf(a);
  return s ? dv.getUint32(s.off + (a - s.addr)) : undefined;
};

// ---- function ranges (Ghidra CSVs are module-relative; vsh is absolute) ----
const bases = new Map<string, number>();
const tocs: { base: number; toc: number }[] = []; // module base -> TOC, sorted by base
for (const line of (await Bun.file(join(imageDir, "modules.csv")).text()).replaceAll("\r", "").split("\n").slice(1)) {
  const [file, , base, toc] = line.split(",");
  if (!file) continue;
  bases.set(file, Number(base));
  tocs.push({ base: Number(base), toc: Number(toc) });
}
// vsh.elf (base 0) has no TOC in modules.csv; take it from its entry OPD (image.elf e_entry)
{
  const elf = new DataView((await Bun.file(join(imageDir, "image.elf")).arrayBuffer()));
  const entryOpd = elf.getUint32(0x18);
  const vsh = tocs.find((t) => t.base === 0);
  if (vsh) vsh.toc = read32(entryOpd + 4) ?? 0;
}
tocs.sort((a, b) => a.base - b.base);
const tocOf = (a: number) => tocs.findLast((t) => t.base <= a)?.toc ?? 0;
type Fn = { start: number; end: number; file: string; row: number; base: number };
const fns: Fn[] = [];
const csvRows = new Map<string, string[]>(); // file -> lines (header first)
for (const f of readdirSync(functionsDir).filter((f) => f.endsWith("_functions.csv"))) {
  const module = f.replace(/_functions\.csv$/, "");
  const lines = (await Bun.file(join(functionsDir, f)).text()).replaceAll("\r", "").split("\n");
  csvRows.set(f, lines);
  const base = bases.get(module);
  if (base === undefined) continue;
  lines.forEach((line, row) => {
    if (row === 0) return;
    const [, ranges] = line.split(",");
    if (!ranges) return;
    let lo = Infinity, hi = 0;
    for (const r of ranges.split(";")) {
      const [a, b] = r.split("-").map((x) => parseInt(x, 16));
      lo = Math.min(lo, a);
      hi = Math.max(hi, b);
    }
    fns.push({ start: base + lo, end: base + hi + 1, file: f, row, base });
  });
}
fns.sort((a, b) => a.start - b.start);
const fnOf = (a: number) => {
  let lo = 0, hi = fns.length - 1, best: Fn | undefined;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (fns[mid].start <= a) { best = fns[mid]; lo = mid + 1; } else hi = mid - 1;
  }
  return best && a < best.end ? best : undefined;
};

// ---- decoding helpers ----
const op = (i: number) => i >>> 26;
const rD = (i: number) => (i >>> 21) & 31;
const rA = (i: number) => (i >>> 16) & 31;
const rB = (i: number) => (i >>> 11) & 31;
const xo = (i: number) => (i >>> 1) & 0x3ff;
const simm = (i: number) => ((i & 0xffff) << 16) >> 16;
const isMtctr = (i: number) => (i & 0xfc1fffff) === 0x7c0903a6;
const isAdd = (i: number) => op(i) === 31 && ((i >>> 1) & 0x1ff) === 266;
const isLwzx = (i: number) => op(i) === 31 && xo(i) === 23;
const isExtsw = (i: number) => op(i) === 31 && xo(i) === 986;
const isAddi = (i: number) => op(i) === 14;
const isLis = (i: number) => op(i) === 15 && rA(i) === 0;
const isLwzToc = (i: number) => op(i) === 32 && rA(i) === 2; // lwz rT,d(r2)
// shift-left-by-2 forms that feed lwzx: rlwinm rS,rI,2,0,29 / rldic rS,rI,2,x / rldicr rS,rI,2,61
const shiftSource = (i: number): number | undefined => {
  if (op(i) === 21 && ((i >>> 11) & 31) === 2) return rD(i); // rlwinm: rS=rA field, source = rS(bits 21-25)
  if (op(i) === 30 && ((i >>> 11) & 31) === 2 && (((i >>> 1) & 1) === 0)) return rD(i); // rldic/rldicr (sh low bits = 2)
  return undefined;
};
const shiftDest = (i: number) => rA(i);
// cmplwi crN,rX,UI: op 10, L=0
const isCmplwi = (i: number) => op(i) === 10;

// End of a case block that lies outside its function: the end of the Ghidra fragment holding it,
// or else just past the next blr / unconditional b.
function blockEnd(label: number, owner: Fn): number {
  const frag = fnOf(label);
  if (frag && frag !== owner) return frag.end;
  for (let a = label; a < label + 0x4000; a += 4) {
    const i = read32(a);
    if (i === undefined) return a;
    if (i === 0x4e800020 || (op(i) === 18 && (i & 3) === 0)) return a + 4;
  }
  return label + 4;
}
const extensions = new Map<Fn, number>(); // function -> new end

let found = 0, matched = 0;
const unmatched: string[] = [];
const out: string[] = [];
for (const s of segs) {
  for (let a = s.addr; a + 4 <= s.addr + s.size; a += 4) {
    if (read32(a) !== 0x4e800420) continue; // bctr
    const fn = fnOf(a);
    if (!fn) continue; // data, not code
    found++;
    const w = (k: number) => read32(a - 4 * k) ?? 0;
    // walk back: mtctr rC ; add rC,rE,rT ; [extsw rE] ; lwzx rE,rT,rS
    let k = 1;
    if (!isMtctr(w(k))) { unmatched.push(a.toString(16)); continue; }
    const rC = rD(w(k)); k++;
    if (!isAdd(w(k)) || rD(w(k)) !== rC) { unmatched.push(a.toString(16)); continue; }
    const addI = w(k); k++;
    if (isExtsw(w(k))) k++;
    if (!isLwzx(w(k))) { unmatched.push(a.toString(16)); continue; }
    const lw = w(k); k++;
    const rE = rD(lw);
    const rT = rA(addI) === rE ? rB(addI) : rA(addI);
    if (rA(lw) !== rT && rB(lw) !== rT) { unmatched.push(a.toString(16)); continue; }
    const rS = rA(lw) === rT ? rB(lw) : rA(lw);
    // further back: addi rT,rT,lo ; shift rS,rI,2 ; lis rT,hi (any order), then cmplwi on rI
    // table base: lis rT,hi + addi rT,rT,lo, or lwz rT,d(r2) from the module's TOC
    let lo: number | undefined, hi: number | undefined, tocTable: number | undefined;
    let rI: number | undefined, bound: number | undefined;
    for (let j = k; j < k + 48; j++) {
      const i = w(j);
      // A blr or unconditional b above means this block is reached by a branch; stop there.
      if (i === 0x4e800020 || (op(i) === 18 && (i & 3) === 0)) break;
      if (lo === undefined && hi === undefined && tocTable === undefined && isLwzToc(i) && rD(i) === rT) tocTable = read32((tocOf(a) + simm(i)) >>> 0);
      else if (lo === undefined && isAddi(i) && rD(i) === rT && rA(i) === rT) lo = simm(i);
      else if (hi === undefined && isLis(i) && rD(i) === rT) hi = simm(i);
      else if (rI === undefined && shiftSource(i) !== undefined && shiftDest(i) === rS) rI = shiftSource(i);
      else if (rI !== undefined && bound === undefined && isCmplwi(i) && rA(i) === rI) bound = i & 0xffff;
      const haveBase = tocTable !== undefined || (lo !== undefined && hi !== undefined);
      if (haveBase && rI !== undefined && bound !== undefined) break;
    }
    const haveBase = tocTable !== undefined || (lo !== undefined && hi !== undefined);
    if (!haveBase || rI === undefined) { unmatched.push(a.toString(16)); continue; }
    const table = tocTable !== undefined ? tocTable : ((hi! << 16) + lo!) >>> 0;
    const labels: number[] = [];
    if (bound !== undefined) {
      // The bound says how many entries there are; cases may lie past Ghidra's end of the function.
      for (let n = 0; n <= bound; n++) {
        const e = read32(table + n * 4);
        if (e === undefined) break;
        labels.push((table + (e | 0)) >>> 0);
      }
      if (labels.length !== bound + 1 || labels.some((l) => l & 3 || l < fn.start || !segOf(l))) { unmatched.push(a.toString(16) + "(entries)"); continue; }
      const past = labels.filter((l) => l >= fn.end);
      if (past.length) {
        // Extend the function through the last case block
        let end = fn.end;
        for (const l of past) end = Math.max(end, blockEnd(l, fn));
        extensions.set(fn, Math.max(extensions.get(fn) ?? 0, end));
        if (!patchDir) { unmatched.push(a.toString(16) + "(past end)"); continue; }
      }
    } else {
      // No bound in sight: read entries until one is implausible or the table runs into its own cases
      let lowest = Infinity;
      for (let n = 0; n < 1024 && table + n * 4 < lowest; n++) {
        const e = read32(table + n * 4);
        if (e === undefined) break;
        const target = (table + (e | 0)) >>> 0;
        if (target < fn.start || target >= fn.end + 0x10000 || target & 3 || !segOf(target)) break;
        labels.push(target);
        if (target > table) lowest = Math.min(lowest, target);
      }
      if (!labels.length) { unmatched.push(a.toString(16) + "(entries)"); continue; }
      const past = labels.filter((l) => l >= fn.end);
      if (past.length) {
        let end = fn.end;
        for (const l of past) end = Math.max(end, blockEnd(l, fn));
        extensions.set(fn, Math.max(extensions.get(fn) ?? 0, end));
        if (!patchDir) { unmatched.push(a.toString(16) + "(past end)"); continue; }
      }
    }
    matched++;
    out.push(`[[switch]]\nbase = 0x${a.toString(16).toUpperCase()}\nr = ${rI}\nlabels = [${labels.map((l) => "0x" + l.toString(16).toUpperCase()).join(", ")}]\n`);
  }
}
await Bun.write(outPath, `# Generated by gen-switch-tables.ts from ${imageDir}. Do not edit.\n\n` + out.join("\n"));
console.log(`bctr in functions: ${found}, jump tables: ${matched}, unmatched: ${unmatched.length}, functions to extend: ${extensions.size} -> ${outPath}`);
if (patchDir) {
  try { mkdirSync(patchDir, { recursive: true }); } catch {} // Bun throws EEXIST here on Windows
  for (const [fn, end] of extensions) {
    const lines = csvRows.get(fn.file)!;
    const hex = (x: number) => (x >>> 0).toString(16).toUpperCase().padStart(8, "0");
    const [entry, ranges, ...rest] = lines[fn.row].split(",");
    lines[fn.row] = [entry, `${ranges};${hex(fn.end - fn.base)}-${hex(end - 1 - fn.base)}`, ...rest].join(",");
  }
  for (const [file, lines] of csvRows) await Bun.write(join(patchDir, file), lines.join("\n"));
  console.log(`patched function lists -> ${patchDir}`);
}
if (unmatched.length) console.log("unmatched (first 30):", unmatched.join(" "));
