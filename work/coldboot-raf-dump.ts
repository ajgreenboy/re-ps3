// Dumps a PS3 vsh .raf scene (e.g. dev_flash/vsh/resource/coldboot.raf).
// Container: "_RAF", u32 BE inflated size, zlib stream. Inflated "RAFO": u32 version, then 6 (offset,size) sections:
// 0 tree, 1 object names (u32 element offset + NUL string, packed), 2 keys (NUL strings), 3 ints?, 4 floats, 5 file data.
// Tree element: name key, attr count, parent, prev, next, first child, last child (u32 each, -1 = none), then attrs
// of 16 bytes: key, type, a, b.
// Usage: bun coldboot-raf-dump.ts <coldboot.raf> [out dir for embedded files]
import { inflateSync } from "node:zlib";
import { mkdirSync, writeFileSync } from "node:fs";
const raw = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer());
const outDir = process.argv[3];
const u = new Uint8Array(inflateSync(raw.subarray(8)));
const dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
const be = (o: number) => dv.getUint32(o);
const sec = [...Array(6)].map((_, i) => ({ off: be(8 + i * 8), size: be(12 + i * 8) }));
const cstr = (o: number) => { let e = o; while (u[e]) e++; return new TextDecoder().decode(u.subarray(o, e)); };
const key = (k: number) => cstr(sec[2].off + k);
const nameAt = (k: number) => ({ elem: be(sec[1].off + k), name: cstr(sec[1].off + k + 4) });
const flt = (i: number) => dv.getFloat32(sec[4].off + i * 4);
const files: { name: string; off: number; size: number }[] = [];
function attr(t: number, a: number, b: number): string {
  switch (t) {
    case 1: return `int ${a | 0}`;
    case 2: return `float ${dv.getFloat32(0, false) && new DataView(new Uint32Array([a]).buffer).getFloat32(0, true)}`;
    case 3: return `str "${key(a)}"`;
    case 5: return `vec[${[...Array(b)].map((_, i) => +flt(a + i).toFixed(6)).join(", ")}]`;
    case 6: return `data @${a.toString(16)} +${b.toString(16)}`;
    case 7: case 8: { const n = nameAt(a); return `${t === 7 ? "obj" : "ref"} ${n.name}(@${n.elem.toString(16)})`; }
    default: return `type${t} ${a.toString(16)} ${b.toString(16)}`;
  }
}
function walk(e: number, depth: number) {
  while (e !== 0xffffffff) {
    const o = sec[0].off + e, n = be(o + 4);
    const attrs: string[] = [];
    let src: { a: number; b: number } | undefined, label = "";
    for (let i = 0; i < n; i++) {
      const p = o + 28 + i * 16, k = key(be(p)), t = be(p + 4), a = be(p + 8), b = be(p + 12);
      attrs.push(`${k}=${attr(t, a, b)}`);
      if (t === 6) src = { a, b };
      if (k === "src" && t === 3) label = key(a);
    }
    console.log(`${"  ".repeat(depth)}@${e.toString(16)} <${key(be(o))}> ${attrs.join("  ")}`);
    if (src) files.push({ name: label || `file_${e.toString(16)}`, off: src.a, size: src.b });
    const child = be(o + 20);
    if (child !== 0xffffffff) walk(child, depth + 1);
    e = be(o + 16);
  }
}
console.log("sections", sec.map(s => `${s.off.toString(16)}+${s.size.toString(16)}`).join(" "));
walk(0, 0);
if (outDir) {
  mkdirSync(outDir, { recursive: true });
  writeFileSync(`${outDir}/raf.bin`, u);
  for (const f of files) writeFileSync(`${outDir}/${f.name.replace(/[\/:]/g, "_")}@${f.off.toString(16)}`, u.subarray(sec[5].off + f.off, sec[5].off + f.off + f.size));
}
