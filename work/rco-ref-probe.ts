// Scan a container's objects and summarise the 8-byte reference words (image/text/ref/event/model)
// plus what those words point at. Analysis helper only.
//   bun work/rco-ref-probe.ts dev_flash/vsh/resource/explore_plugin_full.rco [limit]
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
const limit = Number(process.argv[3] ?? 12);
const compression = file.readUInt32BE(12) >>> 4;
const tables =
  compression === 0
    ? file
    : Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 32 * 1024 * 1024 })]);
const labels = file.readUInt32BE(0x40);
const labelLength = file.readUInt32BE(0x44);
const imageRoot = file.readUInt32BE(0x24);
console.log(`compression=${compression} file=${file.length} tables=${tables.length} imageRoot=${imageRoot.toString(16)} labels=${labels.toString(16)}/${labelLength}`);
const label = (off: number) => {
  if (off === 0xffffffff || off >= labelLength) return `<label ${off.toString(16)} out of range>`;
  const at = labels + off;
  const end = tables.indexOf(0, at);
  return tables.toString("utf8", at, end);
};
const u32 = (p: number) => (p + 4 <= tables.length ? tables.readUInt32BE(p) : 0xdeadbeef);
// Image table walk (same layout assets.cpp uses: 0x0401 entries, label at +4, next at +20).
const imageByOffset = new Map<number, string>();
const imageByLabel = new Map<number, string>();
{
  const count = u32(imageRoot + 16);
  let at = imageRoot + 0x28;
  console.log(`image entries=${count}`);
  for (let i = 0; i < count && i < 4000; ++i) {
    if (at + 0x38 > tables.length) break;
    const kind = tables.readUInt16BE(at);
    const lb = u32(at + 4);
    if (kind === 0x0401) {
      const n = label(lb);
      imageByOffset.set(at, n);
      imageByLabel.set(lb, n);
    }
    const next = u32(at + 20);
    if (!next) break;
    at += next;
  }
}
const count = { image: 0, text: 0, ref: 0, event: 0, model: 0, other: 0 };
const samples: string[] = [];
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
      const w1 = f.offset + 4 <= extra.length ? extra.readUInt32BE(f.offset) : 0xdeadbeef;
      const w2 = f.offset + 8 <= extra.length ? extra.readUInt32BE(f.offset + 4) : 0xdeadbeef;
      count[f.type as keyof typeof count]++;
      if (samples.length < limit || (w1 !== 0xffff0000 && samples.length < 400)) {
        const isNull = w1 === 0xffff0000 && w2 === 0xffffffff;
        let note = "";
        if (!isNull) {
          note = ` label@${w2.toString(16)}=${label(w2)} imageByOffset=${imageByOffset.get(w2) ?? "-"} imageByLabel=${imageByLabel.get(w2) ?? "-"}`;
          if (f.type === "text") {
            // The text table itself is reached through the language entry; assets.cpp readEnglish counts entries.
          }
        }
        if (!isNull && samples.length < 400)
          samples.push(`${name}.${f.name} [${f.type}] w1=${w1.toString(16)} w2=${w2.toString(16)}${isNull ? " NULL" : note}`);
      }
    }
  }
  const children = tables.readUInt32BE(at + 16);
  let child = at + size;
  for (let i = 0; i < children; ++i) {
    if (child + 40 > tables.length) break;
    walk(child);
    const next = u32(child + 20);
    child = next ? child + next : child;
  }
};
for (const off of [0x30, 0x34]) {
  const root = file.readUInt32BE(off);
  if (root !== 0xffffffff) walk(root);
}
console.log(count);
for (const s of samples.slice(0, limit)) console.log(s);
