// Probe raw RCO object entries: header fields, extra bytes as hex/words, and schema field slices.
// Analysis helper only (firmware data stays under work/ and out/).
//   bun work/rco-obj-probe.ts dev_flash/vsh/resource/system_plugin.rco <name-or-offset> ...
import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";

const file = readFileSync(process.argv[2]);
if (file.readUInt32BE(0) !== 0x46525000) throw new Error("not RCO");
const compression = file.readUInt32BE(12) >>> 4;
const tables =
  compression === 0
    ? file
    : Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 32 * 1024 * 1024 })]);
console.log(`compression=${compression} file=${file.length} tables=${tables.length}`);
console.log(
  [0x18, 0x24, 0x30, 0x34, 0x38, 0x40, 0x44, 0x48, 0x80, 0xa4].map((o) => `+${o.toString(16)}=${file.readUInt32BE(o).toString(16)}`).join(" "),
);
const definition = (path: string) => {
  const rows: { name: string; bytes: number }[] = [];
  for (const line of readFileSync(path, "utf8").split(/\r?\n/)) {
    const section = line.match(/^\[([^\]]+)\]/);
    if (section) {
      rows.push({ name: section[1], bytes: 0 });
      continue;
    }
    const field = line.match(/^(\w+)\s*=\s*(\w+)/);
    if (!field || !rows.length) continue;
    rows[rows.length - 1].bytes += ["ref", "event", "image", "text", "model"].includes(field[2]) ? 8 : 4;
  }
  return rows;
};
const objDefs = definition("C:/dev/xmb/out/native-reference/objattribdef-ps3.ini");
const labels = file.readUInt32BE(0x40);
const labelLength = file.readUInt32BE(0x44);
const name = (at: number) => {
  const label = tables.readUInt32BE(at + 4);
  if (label === 0xffffffff) return "";
  const end = tables.indexOf(0, labels + label);
  return tables.toString("utf8", labels + label, end);
};
const dump = (at: number) => {
  const typeId = tables.readUInt16BE(at);
  const rawSize = tables.readUInt32BE(at + 12); const schema = (typeId >>> 8) === 8 ? objDefs[typeId & 255] : undefined; const size = rawSize || (40 + (schema?.bytes ?? 0));
  const children = tables.readUInt32BE(at + 16);
  const extra = tables.subarray(at + 40, at + size);
  console.log(`\n@${at} type=0x${typeId.toString(16)} name=${name(at)} size=${size} children=${children} extra=${extra.length}`);
  const words: string[] = [];
  for (let i = 0; i + 4 <= extra.length; i += 4) words.push(`${i}:${extra.readUInt32BE(i).toString(16)}`);
  console.log("  " + words.slice(0, 64).join(" "));
  const floats: string[] = [];
  for (let i = 0; i + 4 <= extra.length; i += 4) floats.push(`${i}:${extra.readFloatBE(i)}`);
  console.log("  f " + floats.slice(0, 24).join(" "));
};
const args = process.argv.slice(3);
let found = 0;
const walk = (at: number, depth: number) => {
  const typeId = tables.readUInt16BE(at);
  const size = tables.readUInt32BE(at + 12) || 40;
  const n = name(at);
  const want = args.some((a) => a === n || Number(a) === at);
  if (want) {
    found++;
    dump(at);
  }
  const children = tables.readUInt32BE(at + 16);
  let child = at + size;
  for (let i = 0; i < children; ++i) {
    walk(child, depth + 1);
    const next = tables.readUInt32BE(child + 20);
    child = next ? child + next : child;
  }
  void typeId;
};
for (const [offset] of [[0x30], [0x34]]) {
  const root = file.readUInt32BE(offset);
  if (root !== 0xffffffff) walk(root, 0);
}
console.log(`\nmatched ${found}`);
