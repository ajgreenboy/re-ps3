// Enumerate every reference word tag used by object/animation fields across all RCOs, with a resolved
// example for each tag. Analysis helper only.
//   bun work/rco-tag-scan.ts dev_flash
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { inflateSync } from "node:zlib";

const definition = (path: string) => {
  const rows: { name: string; fields: { name: string; type: string; offset: number }[]; bytes: number }[] = [];
  for (const line of readFileSync(path, "utf8").split(/\r?\n/)) {
    const section = line.match(/^\[([^\]]+)\]/);
    if (section) {
      rows.push({ name: section[1], fields: [], bytes: 0 });
      continue;
    }
    const field = line.match(/^(\w+)\s*=\s*(\w+)/);
    if (!field || !rows.length) continue;
    const row = rows[rows.length - 1];
    row.fields.push({ name: field[1], type: field[2], offset: row.bytes });
    row.bytes += ["ref", "event", "image", "text", "model", "object"].includes(field[2]) ? 8 : 4;
  }
  return rows;
};
const objDefs = definition("C:/dev/xmb/out/native-reference/objattribdef-ps3.ini");
const animDefs = definition("C:/dev/xmb/out/native-reference/animattribdef-ps3.ini");
const root = process.argv[2];
const files: string[] = [];
const walkDir = (dir: string) => {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory()) walkDir(join(dir, entry.name));
    else if (entry.name.endsWith(".rco")) files.push(join(dir, entry.name));
  }
};
walkDir(join(root, "vsh/resource"));

const tags = new Map<string, { n: number; example: string; names: Set<string> }>();
for (const path of files) {
  const file = readFileSync(path);
  const c = file.readUInt32BE(12) >>> 4;
  if (c > 1) continue;
  const tables = c === 0 ? file : Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)))]);
  const labels = file.readUInt32BE(0x40);
  const labelLength = file.readUInt32BE(0x44);
  const name = (off: number) => {
    if (off === 0xffffffff || off >= labelLength) return "";
    const at = labels + off;
    const end = tables.indexOf(0, at);
    return tables.toString("utf8", at, end);
  };
  const visit = (at: number, defs: typeof objDefs) => {
    const typeId = tables.readUInt16BE(at);
    const schema = defs[typeId & 0xff];
    const rawSize = tables.readUInt32BE(at + 12);
    const size = rawSize || 40 + (schema ? schema.bytes : 0);
    const extra = tables.subarray(at + 40, at + size);
    const owner = name(tables.readUInt32BE(at + 4)) || "(anon)";
    if (schema) {
      for (const f of schema.fields) {
        if (!["ref", "event", "image", "text", "model", "object"].includes(f.type)) continue;
        if (f.offset + 8 > extra.length) continue;
        const w1 = extra.readUInt32BE(f.offset);
        const w2 = extra.readUInt32BE(f.offset + 4);
        if (w1 === 0xffff0000 && w2 === 0xffffffff) continue;
        const key = `0x${(w1 >>> 16).toString(16)} ${f.type}`;
        if (!tags.has(key)) tags.set(key, { n: 0, example: "", names: new Set() });
        const t = tags.get(key)!;
        t.n++;
        if (!t.example) t.example = `${path.split(/[\\/]/).pop()}:${owner}.${f.name}=0x${w2.toString(16)}`;
        t.names.add(path.split(/[\\/]/).pop()!);
      }
    }
    const children = tables.readUInt32BE(at + 16);
    let child = at + size;
    for (let i = 0; i < children; ++i) {
      visit(child, defs);
      const next = tables.readUInt32BE(child + 20);
      child = next ? child + next : child;
    }
  };
  for (const [offset, defs] of [
    [0x30, objDefs],
    [0x34, animDefs],
  ] as const) {
    const at = file.readUInt32BE(offset);
    if (at !== 0xffffffff) visit(at, defs);
  }
}
for (const [key, t] of [...tags].sort((a, b) => b[1].n - a[1].n))
  console.log(`${key} n=${t.n} resources=${t.names.size} e.g. ${t.example}`);
