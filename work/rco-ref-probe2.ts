// Group 8-byte reference words by type and w1 tag; show what the second word points at in each candidate
// table (label section, text section, ids section, image tree). Analysis helper only.
//   bun work/rco-ref-probe2.ts dev_flash/vsh/resource/explore_plugin_full.rco
import { readFileSync } from "node:fs";
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
    row.bytes += ["ref", "event", "image", "text", "model"].includes(field[2]) ? 8 : 4;
  }
  return rows;
};
const objDefs = definition("C:/dev/xmb/out/native-reference/objattribdef-ps3.ini");
const file = readFileSync(process.argv[2]);
const compression = file.readUInt32BE(12) >>> 4;
const tables =
  compression === 0
    ? file
    : Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 32 * 1024 * 1024 })]);
const h = (o: number) => file.readUInt32BE(o);
const labels = h(0x40);
const labelLength = h(0x44);
const label = (off: number) => {
  if (off >= labelLength) return `<${off.toString(16)} OOR>`;
  const at = labels + off;
  const end = tables.indexOf(0, at);
  return tables.toString("utf8", at, end);
};
const groups = new Map<string, { n: number; w2: number[]; fields: string[] }>();
let textTable: { count: number; rows: { label: number; length: number; offset: number }[] } | undefined;
{
  // Language tree root at 0x18; first language entry carries the text index.
  const root = h(0x18);
  const languages = tables.readUInt32BE(root + 16);
  let at = root + 0x28;
  for (let i = 0; i < languages; ++i) {
    const info = at + 0x28;
    const kind = tables.readUInt16BE(info);
    if (kind === 1) {
      const count = tables.readUInt32BE(info + 4);
      const rows = [] as { label: number; length: number; offset: number }[];
      for (let j = 0; j < count && j < 70000; ++j) {
        const row = info + 8 + j * 12;
        rows.push({ label: tables.readUInt32BE(row), length: tables.readUInt32BE(row + 4), offset: tables.readUInt32BE(row + 8) });
      }
      textTable = { count, rows };
      break;
    }
    const next = tables.readUInt32BE(at + 20);
    if (!next) break;
    at += next;
  }
}
const textData = h(0x38);
const textLabelByIndex = new Map<number, string>();
if (textTable) for (let i = 0; i < textTable.rows.length; ++i) textLabelByIndex.set(i, label(textTable.rows[i].label));
const walk = (at: number) => {
  const typeId = tables.readUInt16BE(at);
  const schema = (typeId >>> 8) === 8 ? objDefs[typeId & 255] : undefined;
  const rawSize = tables.readUInt32BE(at + 12);
  const size = rawSize || 40 + (schema?.bytes ?? 0);
  const name = label(tables.readUInt32BE(at + 4));
  const extra = tables.subarray(at + 40, at + size);
  if (schema) {
    for (const f of schema.fields) {
      if (!["ref", "event", "image", "text", "model"].includes(f.type)) continue;
      const w1 = extra.readUInt32BE(f.offset);
      const w2 = extra.readUInt32BE(f.offset + 4);
      if (w1 === 0xffff0000 && w2 === 0xffffffff) continue;
      const key = `${f.type} w1=${w1.toString(16)}`;
      if (!groups.has(key)) groups.set(key, { n: 0, w2: [], fields: [] });
      const g = groups.get(key)!;
      g.n++;
      if (g.w2.length < 8) g.w2.push(w2);
      if (g.fields.length < 6) g.fields.push(`${name}.${f.name}`);
    }
  }
  const children = tables.readUInt32BE(at + 16);
  let child = at + size;
  for (let i = 0; i < children; ++i) {
    if (child + 40 > tables.length) break;
    walk(child);
    const next = tables.readUInt32BE(child + 20);
    child = next ? child + next : child;
  }
};
for (const off of [0x30, 0x34]) {
  const root = h(off);
  if (root !== 0xffffffff) walk(root);
}
console.log(`labels@${labels.toString(16)}/${labelLength} textData@${textData.toString(16)} textRows=${textTable?.count}`);
console.log(`sample label(0)=${label(0)} label(4)=${label(4)}`);
for (const [k, g] of [...groups].sort((a, b) => b[1].n - a[1].n)) {
  const w2s = g.w2.map((w) => `${w.toString(16)}(text#${textLabelByIndex.get(w) ?? "-"},label:${label(w).slice(0, 24)})`);
  console.log(`${k} n=${g.n} w2=${w2s.join(" ")} e.g. ${g.fields.join(" ")}`);
}
