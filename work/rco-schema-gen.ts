// Emit the RCO attribute-definition table as C++ initialisers for native-xmb/src/rco_objects.cpp.
// Source: Rcomage's objattribdef-ps3.ini and animattribdef-ps3.ini (the format's own attribute schema,
// MIT). Field order and widths define each object type's property layout; the schema is a format
// definition, not firmware data. Every value stays in the RCO bytes.
//   bun work/rco-schema-gen.ts
// Writes work/rco-schema-objects.inc and work/rco-schema-animations.inc.
import { readFileSync, writeFileSync } from "node:fs";

const SIZES: Record<string, number> = { ref: 8, event: 8, image: 8, text: 8, model: 8, object: 8 };
const CODE: Record<string, string> = { float: "f", int: "i", unk: "u", ref: "r", event: "e", image: "g", text: "t", model: "m", object: "o" };

type Row = { name: string; fields: string[]; bytes: number };

function read(path: string): Row[] {
  const rows: Row[] = [];
  for (const line of readFileSync(path, "utf8").split(/\r?\n/)) {
    const section = line.match(/^\[([^\]]+)\]/);
    if (section) {
      rows.push({ name: section[1], fields: [], bytes: 0 });
      continue;
    }
    const field = line.match(/^(\w+)\s*=\s*(\w+)/);
    if (!field || !rows.length) continue;
    const code = CODE[field[2]];
    if (!code) throw new Error(`unknown type ${field[2]}`);
    rows[rows.length - 1].fields.push(`${field[1]}:${code}`);
    rows[rows.length - 1].bytes += SIZES[field[2]] ?? 4;
  }
  return rows;
}

// One initialiser per entry: the field string is split into adjacent literals, each on its own line after
// the type name, so the table stays compilable and diffable.
function block(list: Row[], base: number): string {
  const lines: string[] = [];
  list.forEach((row, i) => {
    const id = `0x${(base * 256 + i).toString(16)}`;
    const text = row.fields.join(",");
    const chunks: string[] = [];
    for (let at = 0; at < text.length; at += 96) chunks.push(text.slice(at, at + 96));
    lines.push(`    {${id}, "${row.name}",`);
    if (!chunks.length) {
      lines.push(`        "", ${row.bytes}},`);
      return;
    }
    for (let c = 0; c < chunks.length; ++c) lines.push(`        "${chunks[c]}"${c + 1 === chunks.length ? `, ${row.bytes}},` : ""}`);
  });
  return lines.join("\n");
}

const objects = read("C:/dev/xmb/out/native-reference/objattribdef-ps3.ini");
const animations = read("C:/dev/xmb/out/native-reference/animattribdef-ps3.ini");
writeFileSync("C:/dev/xmb/work/rco-schema-objects.inc", block(objects, 8) + "\n");
writeFileSync("C:/dev/xmb/work/rco-schema-animations.inc", block(animations, 9) + "\n");
console.log(
  `objects ${objects.length} (${objects.reduce((n, r) => n + r.fields.length, 0)} fields), ` +
    `animations ${animations.length} (${animations.reduce((n, r) => n + r.fields.length, 0)} fields)`,
);
