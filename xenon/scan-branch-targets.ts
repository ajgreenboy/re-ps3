// Lists direct branch targets that no function starts at (one hex address per line), straight from the image,
// so gen-opd-functions.ts can add them without a XenonRecomp round trip. A target is reported when a `bl`
// inside a function lands anywhere but a function start, or a `b`/conditional branch leaves its function's
// translated .pdata spans for an address that is not a function start. The spans match xmbld,
// including gaps between Ghidra body ranges. Only targets in executable segments are kept.
// usage: bun scan-branch-targets.ts <boot493 dir> <functions dir> <out file>
import { readdirSync } from "node:fs";
import { join } from "node:path";

const [imageDir, functionsDir, outPath] = process.argv.slice(2);
if (!outPath) throw new Error("usage: bun scan-branch-targets.ts <boot493 dir> <functions dir> <out file>");

const img = new Uint8Array(await Bun.file(join(imageDir, "image.bin")).arrayBuffer());
const dv = new DataView(img.buffer);
type Seg = { addr: number; size: number; off: number; exec: boolean };
const segs: Seg[] = [];
for (let i = 0, pos = 12, n = dv.getUint32(8); i < n; i++) {
  const addr = dv.getUint32(pos), size = dv.getUint32(pos + 4), flags = dv.getUint32(pos + 8);
  segs.push({ addr, size, off: pos + 12, exec: (flags & 1) !== 0 });
  pos += 12 + size;
}
const execSeg = (a: number) => segs.find((s) => s.exec && a >= s.addr && a < s.addr + s.size);
const read32 = (a: number) => {
  const s = execSeg(a);
  return s ? dv.getUint32(s.off + (a - s.addr)) : undefined;
};

const bases = new Map<string, number>();
for (const line of (await Bun.file(join(imageDir, "modules.csv")).text()).replaceAll("\r", "").split("\n").slice(1)) {
  const [file, , base] = line.split(",");
  if (file) bases.set(file, Number(base));
}

const starts = new Set<number>();
const funcs: { ranges: [number, number][] }[] = [];
for (const f of readdirSync(functionsDir).filter((f) => f.endsWith("_functions.csv"))) {
  const base = bases.get(f.replace(/_functions\.csv$/, ""));
  if (base === undefined) continue;
  for (const line of (await Bun.file(join(functionsDir, f)).text()).replaceAll("\r", "").split("\n").slice(1)) {
    const [entry, rs] = line.split(",");
    if (!entry) continue;
    starts.add(base + parseInt(entry, 16));
    const ranges: [number, number][] = [];
    const entryAddress = base + parseInt(entry, 16);
    let maxEnd = entryAddress;
    for (const r of (rs ?? "").split(";")) {
      const [s, e] = r.split("-").map((x) => parseInt(x, 16));
      if (!isNaN(s) && !isNaN(e)) {
        const start = base + s, end = base + e + 1;
        maxEnd = Math.max(maxEnd, end);
        // Match xmbld.load_pdata: ranges below the entry get their own span.
        if (start < entryAddress) ranges.push([start, end]);
      }
    }
    // xmbld's .pdata spans entry..last-range-end, including holes in Ghidra's
    // disjoint body. XenonRecomp emits instructions in those holes too. Scan
    // that same span or its branches can escape without a translated entry.
    if (maxEnd > entryAddress) ranges.push([entryAddress, maxEnd]);
    funcs.push({ ranges });
  }
}

const found = new Set<number>();
for (const fn of funcs) {
  const inside = (a: number) => fn.ranges.some(([s, e]) => a >= s && a < e);
  for (const [s, e] of fn.ranges)
    for (let a = s & ~3; a + 4 <= e; a += 4) {
      const w = read32(a);
      if (w === undefined) continue;
      const op = w >>> 26;
      let target: number | undefined, call = false;
      if (op === 18) {
        // b / bl
        let d = w & 0x03fffffc;
        if (d & 0x02000000) d -= 0x04000000;
        target = (w & 2 ? 0 : a) + d;
        call = (w & 1) === 1;
      } else if (op === 16) {
        // bc / bcl
        let d = w & 0xfffc;
        if (d & 0x8000) d -= 0x10000;
        target = (w & 2 ? 0 : a) + d;
        call = (w & 1) === 1;
      }
      if (target === undefined) continue;
      target >>>= 0;
      if (starts.has(target) || !execSeg(target) || read32(target) === 0) continue;
      if (!call && inside(target)) continue;
      found.add(target);
    }
}
await Bun.write(outPath, [...found].map((a) => a.toString(16).toUpperCase()).join("\n") + (found.size ? "\n" : ""));
console.log(`${found.size} unresolved branch targets -> ${outPath}`);
