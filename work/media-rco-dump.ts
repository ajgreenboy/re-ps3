// Object-tree / image-table / text-table dumper for the media-player RCOs.
// Reads the local dev_flash RCO containers directly (read-only), plus the two
// attribute-definition INIs that out/native-reference/rco-behavior used.
//
// Nothing here writes firmware bytes anywhere: output is text on stdout or a
// text file under work/.
//
// Usage:
//   bun work/media-rco-dump.ts --rco videoplayer_plugin [--tree] [--images] [--texts] [--fields]
//   bun work/media-rco-dump.ts --rco videoplayer_plugin --obj videoplayer_cp_play_button --raw
//   bun work/media-rco-dump.ts --rco videoplayer_plugin --grep volume
// Options:
//   --flash <dir>   dev_flash root (default C:/dev/rpcs3/bin/dev_flash)
//   --rco <name>    container base name (no .rco)
//   --tree          print the object tree (default)
//   --images        print the image table with decoded dimensions
//   --texts         print the English text table (label -> string)
//   --fields        print every decoded attribute (default: geometry + colour + text/font/image)
//   --obj <name>    only print this object (repeatable, comma separated)
//   --grep <re>     only print objects whose name matches
//   --raw           with --obj, dump the raw extra bytes and every ref candidate
import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";

const argv = process.argv.slice(2);
const val = (n: string, d?: string) => {
  const i = argv.indexOf(n);
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : d;
};
const flag = (n: string) => argv.includes(n);

const FLASH = val("--flash", "C:/dev/rpcs3/bin/dev_flash")!;
const RCO = val("--rco");
if (!RCO) {
  console.error("usage: bun work/media-rco-dump.ts --rco <name> [--tree|--images|--texts] [--obj name] [--grep re] [--raw]");
  process.exit(2);
}

type FieldDef = { name: string; type: string; offset: number };
type Schema = { name: string; fields: FieldDef[]; bytes: number };

function loadIni(path: string): Schema[] {
  const rows: Schema[] = [];
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
}
const objDefs = loadIni("C:/dev/xmb/out/native-reference/objattribdef-ps3.ini");
const animDefs = loadIni("C:/dev/xmb/out/native-reference/animattribdef-ps3.ini");

const file = readFileSync(`${FLASH}/vsh/resource/${RCO}.rco`);
if (file.readUInt32BE(0) !== 0x46525000) throw new Error("not an RCO");
const compression = file.readUInt32BE(12) >>> 4;
const tables =
  compression === 0
    ? file
    : compression === 1
      ? Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 32 * 1024 * 1024 })])
      : (() => {
          throw new Error("unsupported compression");
        })();

const labels = file.readUInt32BE(0x40);
const labelLength = file.readUInt32BE(0x44);
const imageData = file.readUInt32BE(0x80);
const textRoot = file.readUInt32BE(0x18);
const textData = file.readUInt32BE(0x38);
const textLength = file.readUInt32BE(0x3c);

function labelAt(offset: number): string | null {
  if (offset === 0xffffffff || offset < 0 || offset >= labelLength) return null;
  const end = tables.indexOf(0, labels + offset);
  if (end < 0) return null;
  return tables.toString("utf8", labels + offset, end);
}

// ---------------------------------------------------------------- text table
function readEnglish(): Map<number, { label: string; text: string }> {
  const out = new Map<number, { label: string; text: string }>();
  if (textRoot === 0xffffffff || !textLength || textData === 0xffffffff) return out;
  let english: Buffer;
  if (compression === 1) {
    english = Buffer.alloc(0);
    let chunk = textData;
    for (let i = 0; i < 64; ++i) {
      if (chunk + 16 > textData + textLength) break;
      const lang = file.readUInt16BE(chunk);
      const next = file.readUInt32BE(chunk + 4);
      const packed = file.readUInt32BE(chunk + 8);
      if (lang === 1) {
        english = inflateSync(file.subarray(chunk + 16, chunk + 16 + packed), { maxOutputLength: 32 * 1024 * 1024 });
        break;
      }
      if (!next) break;
      chunk += next;
    }
  } else english = file.subarray(textData, textData + textLength);
  if (!english.length) return out;
  const languages = tables.readUInt32BE(textRoot + 16);
  let at = textRoot + 0x28;
  for (let l = 0; l < languages; ++l) {
    const info = at + 0x28;
    if (tables.readUInt16BE(info) === 1) {
      const format = tables.readUInt16BE(info + 2);
      const count = tables.readUInt32BE(info + 4);
      for (let i = 0; i < count; ++i) {
        const index = info + 8 + i * 12;
        const labelOffset = tables.readUInt32BE(index);
        const length = tables.readUInt32BE(index + 4);
        const offset = tables.readUInt32BE(index + 8);
        if (labelOffset >= labelLength || offset + length > english.length) continue;
        const label = labelAt(labelOffset) ?? "";
        let text = "";
        if (format === 0) text = english.toString("utf8", offset, offset + length);
        else if (format === 1) {
          const parts: string[] = [];
          for (let p = offset; p + 1 < offset + length; p += 2) parts.push(String.fromCharCode(english.readUInt16BE(p)));
          text = parts.join("");
        }
        text = text.replace(/\0+$/, "");
        out.set(labelOffset, { label, text });
      }
      break;
    }
    const next = tables.readUInt32BE(at + 20);
    if (!next) break;
    at += next;
  }
  return out;
}
const texts = readEnglish();

// --------------------------------------------------------------- image table
type ImageEntry = {
  offset: number;
  labelOffset: number;
  name: string;
  format: number;
  compression: number;
  packed: number;
  dataOffset: number;
  expanded: number;
  width: number;
  height: number;
  pixelFormat: string;
  note: string;
};
function gimSize(bytes: Buffer) {
  // .GIM1.00 + blocks; find the first image block (type 4) and read its info.
  if (!bytes.subarray(0, 8).toString("latin1").startsWith(".GIM1.00")) return null;
  let at = 0x10;
  for (let blocks = 0; blocks < 128; ++blocks) {
    if (at + 16 > bytes.length) return null;
    const type = bytes.readUInt16BE(at);
    const size = bytes.readUInt32BE(at + 4);
    const data = bytes.readUInt32BE(at + 12);
    if (size < 16 || data < 16 || data >= size || at + size > bytes.length) return null;
    if (type === 2 || type === 3) {
      at += data;
      continue;
    }
    if (type !== 4) {
      at += size;
      continue;
    }
    const info = at + data;
    if (info + 0x30 > bytes.length) return null;
    const fmt = bytes.readUInt16BE(info + 4);
    const w = bytes.readUInt16BE(info + 8);
    const h = bytes.readUInt16BE(info + 10);
    const bpp = bytes.readUInt16BE(info + 12);
    return { width: w, height: h, format: "gim:" + fmt.toString(16) + " bpp " + bpp };
  }
  return null;
}
function ddsSize(bytes: Buffer) {
  if (bytes.length < 128 || !bytes.subarray(0, 4).toString("latin1").startsWith("DDS ")) return null;
  const h = bytes.readUInt32LE(12);
  const w = bytes.readUInt32LE(16);
  const fourcc = bytes.readUInt32LE(84).toString(16);
  return { width: w, height: h, format: "dds fourcc 0x" + fourcc };
}
function pngSize(bytes: Buffer) {
  if (bytes.length < 24 || bytes.readUInt32BE(0) !== 0x89504e47) return null;
  return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20), format: "png" };
}
function imageTable(): ImageEntry[] {
  const root = file.readUInt32BE(0x24);
  const out: ImageEntry[] = [];
  if (root === 0xffffffff) return out;
  const count = tables.readUInt32BE(root + 16);
  let at = root + 0x28;
  for (let i = 0; i < count; ++i) {
    if (tables.readUInt16BE(at) !== 0x0401) break;
    const labelOffset = tables.readUInt32BE(at + 4);
    const next = tables.readUInt32BE(at + 20);
    const extra = at + 0x28;
    const format = tables.readUInt16BE(extra);
    const comp = tables.readUInt16BE(extra + 2) & 255;
    const packed = tables.readUInt32BE(extra + 4);
    const offset = tables.readUInt32BE(extra + 8);
    const expanded = tables.readUInt32BE(extra + 16);
    const entry: ImageEntry = {
      offset: at,
      labelOffset,
      name: labelAt(labelOffset) ?? "",
      format,
      compression: comp,
      packed,
      dataOffset: offset,
      expanded,
      width: 0,
      height: 0,
      pixelFormat: "",
      note: "",
    };
    try {
      const start = imageData + offset;
      let bytes: Buffer;
      if (comp === 1) bytes = inflateSync(file.subarray(start, start + packed), { maxOutputLength: 32 * 1024 * 1024 });
      else bytes = file.subarray(start, start + packed);
      const size = gimSize(bytes) ?? ddsSize(bytes) ?? pngSize(bytes);
      if (size) {
        entry.width = size.width;
        entry.height = size.height;
        entry.pixelFormat = size.format;
      } else entry.note = "unrecognised header " + bytes.subarray(0, 8).toString("hex");
    } catch (e) {
      entry.note = String(e);
    }
    out.push(entry);
    at += next;
  }
  return out;
}
const images = imageTable();
const imageByEntry = new Map(images.map((i) => [i.offset, i]));
const imageByLabel = new Map(images.map((i) => [i.labelOffset, i]));

// ------------------------------------------------------------- object tree
type Obj = {
  offset: number;
  typeId: number;
  name: string;
  parent: string;
  children: number;
  size: number;
  schema: Schema | undefined;
  extra: Buffer;
  fields: { name: string; type: string; raw: string; value: number | string | null; resolved?: string }[];
  depth: number;
};
const objects: Obj[] = [];
function walkObjects() {
  const root = file.readUInt32BE(0x30);
  if (root === 0xffffffff) return;
  let guard = 0;
  const walk = (at: number, depth: number, parent: string): number => {
    if (++guard > 100000 || depth > 64) throw new Error("tree limit");
    const typeId = tables.readUInt16BE(at);
    const label = tables.readUInt32BE(at + 4);
    const rawSize = tables.readUInt32BE(at + 12);
    const children = tables.readUInt32BE(at + 16);
    const schema = typeId >>> 8 === 8 ? objDefs[typeId & 255] : animDefs[typeId & 255];
    const size = rawSize || (schema ? 40 + schema.bytes : 40);
    if (size < 40 || at + size > tables.length) throw new Error("bad entry at " + at);
    const name = labelAt(label) ?? "";
    const extra = tables.subarray(at + 40, at + size);
    const obj: Obj = { offset: at, typeId, name, parent, children, size, schema, extra, fields: [], depth };
    if (schema) {
      for (const f of schema.fields) {
        const bytes = ["ref", "event", "image", "text", "model", "object"].includes(f.type) ? 8 : 4;
        if (f.offset + bytes > extra.length) continue;
        let raw = extra.subarray(f.offset, f.offset + bytes).toString("hex");
        let value: number | string | null = null;
        let resolved: string | undefined;
        if (f.type === "float") value = Number(extra.readFloatBE(f.offset).toFixed(4));
        else if (f.type === "int") value = extra.readInt32BE(f.offset);
        else if (bytes === 8) {
          const tag = extra.readUInt32BE(f.offset);
          const arg = extra.readUInt32BE(f.offset + 4);
          // 8-byte fields are (u32 kind tag, u32 table-relative offset).
          // 0x0402xxxx is the image kind; its offset points at an image entry
          // inside the tables stream (not at a label, and not at the pixel data).
          if ((tag >>> 16) === 0x0402) {
            const img = imageByEntry.get(arg) ?? imageByLabel.get(arg);
            if (img) {
              resolved = `image:${img.name || "(anon)"} ${img.width}x${img.height}`;
              value = img.name;
            } else resolved = `image:<unresolved 0x${arg.toString(16)}>`;
          } else if (f.type === "text") {
            const t = texts.get(arg) ?? texts.get(tag);
            if (t) {
              resolved = `text:${t.label}="${t.text}"`;
              value = t.text;
            } else resolved = `text:<unresolved 0x${arg.toString(16)}>`;
          } else {
            const l = labelAt(arg);
            if (l) {
              resolved = `${f.type}:${l}`;
              value = l;
            }
          }
        }
        obj.fields.push({ name: f.name, type: f.type, raw, value, resolved });
      }
    }
    objects.push(obj);
    const kids: Obj[] = [];
    let child = at + size;
    for (let i = 0; i < children; ++i) {
      const end = walk(child, depth + 1, name);
      const next = tables.readUInt32BE(child + 20);
      child = next ? child + next : end;
    }
    return Math.max(at + size, child);
  };
  walk(root, 0, "");
}
walkObjects();

// ------------------------------------------------------------------- output
const showImages = flag("--images");
const showTexts = flag("--texts");
const showTree = flag("--tree") || (!showImages && !showTexts);
const fullFields = flag("--fields");
const only = val("--obj")?.split(",").map((s) => s.trim());
const grep = val("--grep") ? new RegExp(val("--grep")!, "i") : null;
const raw = flag("--raw");

console.log(`# ${RCO}.rco  ${file.length} bytes  compression=${compression}  objects=${objects.length}  images=${images.length}  texts=${texts.size}`);

if (showImages) {
  console.log("\n## image table");
  for (const i of [...images].sort((a, b) => a.name.localeCompare(b.name)))
    console.log(
      `${i.name || "(anon)"}\t${i.width}x${i.height}\t${i.pixelFormat || "-"}\tentry@0x${i.offset.toString(16)}\tlabel@0x${i.labelOffset.toString(16)}\trco+0x${(imageData + i.dataOffset).toString(16)}\tpacked=${i.packed}\texpanded=${i.expanded}${i.note ? "\t" + i.note : ""}`,
    );
}

if (showTexts) {
  console.log("\n## text table (English)");
  for (const [off, t] of [...texts.entries()].sort((a, b) => a[1].label.localeCompare(b[1].label)))
    console.log(`${t.label}\t${JSON.stringify(t.text)}\tlabel@0x${off.toString(16)}`);
}

if (showTree) {
  console.log("\n## object tree");
  const interesting = (o: Obj) => {
    if (only) return only.includes(o.name);
    if (grep) return grep.test(o.name);
    return true;
  };
  for (const o of objects) {
    if (!interesting(o)) continue;
    const pad = "  ".repeat(o.depth);
    const fields: string[] = [];
    for (const f of o.fields) {
      if (f.resolved) fields.push(`${f.name}=${f.resolved}`);
      else if (fullFields) {
        if (f.type === "float" || f.type === "int") fields.push(`${f.name}=${f.value}`);
        else fields.push(`${f.name}=0x${f.raw}`);
      } else if (
        ["posX", "posY", "posZ", "width", "height", "redScale", "greenScale", "blueScale", "alphaScale", "size", "topRed", "topGreen", "topBlue", "alpha", "shadowAlpha"].includes(f.name) &&
        f.value !== 0
      )
        fields.push(`${f.name}=${f.value}`);
    }
    console.log(
      `${pad}${o.name || "(anon)"}  [${o.schema?.name ?? "?"} ${"0x" + o.typeId.toString(16)} @0x${o.offset.toString(16)} size=${o.size} children=${o.children}${o.parent ? " parent=" + o.parent : ""}]${fields.length ? " " + fields.join(" ") : ""}`,
    );
    if (raw && only?.includes(o.name)) {
      console.log(`${pad}  raw extra (${o.extra.length}): ${o.extra.toString("hex")}`);
      for (const f of o.fields) console.log(`${pad}  field ${f.name} ${f.type} = 0x${f.raw} -> ${f.resolved ?? f.value}`);
    }
  }
}
