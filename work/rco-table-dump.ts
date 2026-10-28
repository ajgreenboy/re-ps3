// RCO table dumper: image names (with GIM dimensions) and text keys (English) of one local RCO.
// Reads only local dev_flash files; writes nothing. Firmware data stays local.
//
// Usage:
//   bun work/rco-table-dump.ts <rco-file> --images        image table: name, format, dimensions
//   bun work/rco-table-dump.ts <rco-file> --text          English text table: key = value
//   bun work/rco-table-dump.ts <rco-file> --labels        every label name in the label table
//   bun work/rco-table-dump.ts <rco-file> --header        header table offsets/lengths
import { inflateSync } from "node:zlib";
import { readFileSync } from "node:fs";

const path = process.argv[2];
const flags = new Set(process.argv.slice(3));
if (!path) throw new Error("usage: rco-table-dump.ts <rco-file> [--images|--text|--labels|--header]");
const file = readFileSync(path);
if (file.readUInt32BE(0) !== 0x46525000) throw new Error("not a PS3 RCO");
const compression = file.readUInt32BE(12) >>> 4;
const tables =
  compression === 0
    ? file
    : compression === 1
      ? Buffer.concat([
          file.subarray(0, 0xa4),
          inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 64 * 1024 * 1024 }),
        ])
      : (() => {
          throw new Error("unsupported compression " + compression);
        })();

const u32 = (b: Buffer, o: number) => b.readUInt32BE(o);
const labels = u32(file, 0x40);
const labelLength = u32(file, 0x44);
const nameAt = (label: number) => {
  if (label === 0xffffffff) return "";
  const end = tables.indexOf(0, labels + label);
  return tables.toString("utf8", labels + label, end);
};

function inflateAt(start: number, packed: number, unpacked: number) {
  return inflateSync(file.subarray(start, start + packed), { maxOutputLength: Math.max(unpacked, 1) + 1024 });
}

// GIM picture block -> width/height. Same layout rules the native decoder uses.
function gimSize(data: Buffer): { w: number; h: number; format: number } | null {
  if (data.length < 0x40 || !data.subarray(0, 8).toString().startsWith(".GIM1.00")) return null;
  let at = 0x10;
  for (let blocks = 0; blocks < 32; ++blocks) {
    if (at + 16 > data.length) return null;
    const type = data.readUInt16BE(at);
    const size = u32(data, at + 4);
    const dataOff = u32(data, at + 12);
    if (size < 16 || dataOff < 16 || dataOff >= size || at + size > data.length) return null;
    if (type === 4) {
      const info = at + dataOff;
      if (info + 0x30 > data.length) return null;
      return { format: data.readUInt16BE(info + 4), w: data.readUInt16BE(info + 8), h: data.readUInt16BE(info + 10) };
    }
    at += dataOff;
  }
  return null;
}

if (flags.has("--header")) {
  const slots = [
    ["objects", 0x30],
    ["animations", 0x34],
    ["text", 0x18],
    ["textData", 0x38],
    ["textLength", 0x3c],
    ["labels", 0x40],
    ["labelLength", 0x44],
    ["imageRoot", 0x24],
    ["imageData", 0x80],
    ["soundRoot", 0x1c],
  ];
  for (const [name, off] of slots) console.log(name.padEnd(14), "0x" + u32(file, off).toString(16), u32(file, off));
}

if (flags.has("--images")) {
  const root = u32(file, 0x24);
  const imageDataAt = u32(file, 0x80);
  if (root === 0xffffffff) console.log("(no image table)");
  else {
    const count = u32(tables, root + 16);
    let at = root + 0x28;
    for (let i = 0; i < count; ++i) {
      const label = u32(tables, at + 4);
      const next = u32(tables, at + 20);
      const extra = at + 0x28;
      const format = tables.readUInt16BE(extra);
      const comp = tables.readUInt16BE(extra + 2) & 255;
      const packed = u32(tables, extra + 4);
      const offset = u32(tables, extra + 8);
      const unpacked = u32(tables, extra + 16);
      let size: { w: number; h: number; format: number } | null = null;
      let error = "";
      try {
        const start = imageDataAt + offset;
        const bytes = comp === 1 ? inflateAt(start, packed, unpacked) : file.subarray(start, start + packed);
        size = gimSize(bytes);
      } catch (e) {
        error = String(e);
      }
      console.log(
        `${nameAt(label)}\tentry=${at}\tgim=${size ? size.w + "x" + size.h : "?"}\tformat=${format}\tcomp=${comp}\tpacked=${packed}${error ? "\t" + error : ""}`,
      );
      at += next || 56;
    }
  }
}

if (flags.has("--labels")) {
  for (let at = labels; at < labels + labelLength; ) {
    const end = tables.indexOf(0, at);
    if (end < 0) break;
    console.log(tables.toString("utf8", at, end));
    at = end + 1;
  }
}

if (flags.has("--text")) {
  const root = u32(file, 0x18);
  const textData = u32(file, 0x38);
  const textLength = u32(file, 0x3c);
  if (root === 0xffffffff) console.log("(no text table)");
  else {
    let english: Buffer = Buffer.alloc(0);
    if (compression) {
      let chunk = textData;
      for (let i = 0; i < 64; ++i) {
        const lang = file.readUInt16BE(chunk);
        const next = u32(file, chunk + 4);
        const packed = u32(file, chunk + 8);
        if (lang === 1) {
          english = inflateSync(file.subarray(chunk + 16, chunk + 16 + packed), { maxOutputLength: u32(file, chunk + 12) + 1024 });
          break;
        }
        if (!next) break;
        chunk += next;
      }
    } else english = file.subarray(textData, textData + textLength);
    // Mirror src/assets.cpp readEnglish(): language entries are a sibling list starting at
    // root + 0x28, each with its own info block at +0x28, and the string encoding is per entry.
    const languages = u32(tables, root + 16);
    let at = root + 0x28;
    for (let language = 0; language < languages; ++language) {
      const info = at + 0x28;
      if (tables.readUInt16BE(info) === 1) {
        const format = tables.readUInt16BE(info + 2);
        const count = u32(tables, info + 4);
        for (let i = 0; i < count; ++i) {
          const index = info + 8 + i * 12;
          const label = u32(tables, index);
          const length = u32(tables, index + 4);
          const offset = u32(tables, index + 8);
          const bytes = english.subarray(offset, offset + length);
          let value = "";
          if (format === 0) value = bytes.toString("utf8");
          else if (format === 1) value = Buffer.from(bytes).swap16().toString("utf16le");
          else if (format === 2) {
            let s = "";
            for (let b = 0; b + 4 <= bytes.length; b += 4) s += String.fromCodePoint(bytes.readUInt32BE(b));
            value = s;
          } else value = "<unsupported format " + format + ">";
          while (value.endsWith("\u0000")) value = value.slice(0, -1);
          console.log(`${nameAt(label)} = ${value.replace(/\n/g, "\\n")}`);
        }
      }
      const next = u32(tables, at + 20);
      if (!next) break;
      at += next;
    }
  }
}
