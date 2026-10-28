// P2 geometry groundwork helper (read-only): RCO raw attribute dump + capture region measurement.
// Nothing here writes into the repo; it only prints numbers for audit/GEOMETRY-ROOT.md.
//
// Usage:
//   bun work/p2-root-geometry.ts rco <rcoPath> <nameRegex>       raw object attrs (offset/size/extra floats)
//   bun work/p2-root-geometry.ts band <png> <x,y,w,h>             per-row and per-column mean luma of a region
//   bun work/p2-root-geometry.ts blobs <png> <x,y,w,h> <thresh>   bright blobs above the region background
//   bun work/p2-root-geometry.ts vc <captureDir>                   unique screen geometry from the .vc vertex constants
import { readFileSync, readdirSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { decodePng, type PngImage } from './ab-compare';

const argv = process.argv.slice(2);
const mode = argv[0];

// ---------------------------------------------------------------- RCO raw dump
type Field = { name: string; type: string; offset: number };
function definition(file: string) {
  const source = readFileSync(file, 'utf8');
  const rows: { name: string; fields: Field[]; bytes: number }[] = [];
  for (const line of source.split(/\r?\n/)) {
    const section = line.match(/^\[([^\]]+)\]/);
    if (section) { rows.push({ name: section[1], fields: [], bytes: 0 }); continue; }
    const field = line.match(/^(\w+)\s*=\s*(\w+)/);
    if (!field || !rows.length) continue;
    const row = rows.at(-1)!;
    row.fields.push({ name: field[1], type: field[2], offset: row.bytes });
    row.bytes += ['ref', 'event', 'image', 'text', 'model'].includes(field[2]) ? 8 : 4;
  }
  return rows;
}

function rcoDump(path: string, nameRe: RegExp) {
  const objDefs = definition('out/native-reference/objattribdef-ps3.ini');
  const file = readFileSync(path);
  if (file.readUInt32BE(0) !== 0x46525000) throw new Error('Unsupported RCO');
  const compression = file.readUInt32BE(12) >>> 4;
  const tables = compression === 0 ? file
    : compression === 1
      ? Buffer.concat([file.subarray(0, 0xa4), inflateSync(file.subarray(0xb0, 0xb0 + file.readUInt32BE(0xa4)), { maxOutputLength: 32 * 1024 * 1024 })])
      : (() => { throw new Error('Unsupported compression'); })();
  const labels = file.readUInt32BE(0x40);
  const u32 = (p: number) => tables.readUInt32BE(p);
  const nameAt = (label: number) => label === 0xffffffff ? '' : tables.toString('utf8', labels + label, tables.indexOf(0, labels + label));
  let count = 0;
  const walk = (at: number, depth: number, parent: string): number => {
    if (++count > 100000 || depth > 128) throw new Error('tree limit');
    const typeId = tables.readUInt16BE(at), label = u32(at + 4), rawSize = u32(at + 12), children = u32(at + 16);
    const schema = (typeId >>> 8) === 8 ? objDefs[typeId & 255] : undefined;
    const size = rawSize || 40 + (schema?.bytes ?? 0);
    const name = nameAt(label);
    if (nameRe.test(name)) {
      const extra = tables.subarray(at + 40, at + size);
      console.log(`${name} [${schema?.name ?? '?'} 0x${typeId.toString(16)} @${at} size=${size} kids=${children} parent=${parent || '-'} extra=${extra.length}B]`);
      if (schema) {
        const parts: string[] = [];
        for (const f of schema.fields) {
          if (f.type === 'float') parts.push(`${f.name}=${extra.readFloatBE(f.offset)}@+${f.offset}`);
          else if (f.type === 'int') parts.push(`${f.name}=${extra.readInt32BE(f.offset)}@+${f.offset}`);
          else parts.push(`${f.name}=0x${extra.subarray(f.offset, f.offset + 8).toString('hex')}@+${f.offset}`);
        }
        console.log('    ' + parts.join(' '));
      }
      console.log('    raw ' + extra.toString('hex'));
    }
    let child = at + size;
    for (let i = 0; i < children; ++i) {
      const end = walk(child, depth + 1, name || parent);
      const next = u32(child + 20);
      child = next ? child + next : end;
    }
    return child;
  };
  for (const offset of [0x30, 0x34]) {
    const at = file.readUInt32BE(offset);
    if (at !== 0xffffffff) walk(at, 0, '');
  }
}

// ------------------------------------------------------------- capture helpers
function luma(img: PngImage, x: number, y: number) {
  const i = (y * img.w + x) * 4;
  return 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
}
function region(path: string, crop: number[]) {
  const img = decodePng(path);
  const [x, y, w, h] = crop;
  if (x < 0 || y < 0 || x + w > img.w || y + h > img.h) throw new Error(`crop outside ${img.w}x${img.h}`);
  return { img, x, y, w, h };
}
function band(path: string, crop: number[]) {
  const { img, x, y, w, h } = region(path, crop);
  console.log(`image ${img.w}x${img.h} region ${x},${y},${w},${h}`);
  console.log('row\tmeanLuma\tmaxLuma');
  for (let yy = y; yy < y + h; yy++) {
    let sum = 0, max = 0;
    for (let xx = x; xx < x + w; xx++) { const l = luma(img, xx, yy); sum += l; if (l > max) max = l; }
    console.log(`${yy}\t${(sum / w).toFixed(2)}\t${max.toFixed(1)}`);
  }
  console.log('col\tmeanLuma');
  for (let xx = x; xx < x + w; xx++) {
    let sum = 0;
    for (let yy = y; yy < y + h; yy++) sum += luma(img, xx, yy);
    console.log(`${xx}\t${(sum / h).toFixed(2)}`);
  }
}
function blobs(path: string, crop: number[], threshold: number) {
  const { img, x, y, w, h } = region(path, crop);
  // background = median luma of the region border rows/cols
  const border: number[] = [];
  for (let xx = x; xx < x + w; xx++) { border.push(luma(img, xx, y), luma(img, xx, y + h - 1)); }
  for (let yy = y; yy < y + h; yy++) { border.push(luma(img, x, yy), luma(img, x + w - 1, yy)); }
  border.sort((a, b) => a - b);
  const background = border[border.length >> 1];
  const limit = background + threshold;
  const seen = new Uint8Array(w * h);
  const blobsOut: { area: number; minX: number; maxX: number; minY: number; maxY: number; sumX: number; sumY: number; sumL: number }[] = [];
  for (let sy = 0; sy < h; sy++) for (let sx = 0; sx < w; sx++) {
    const p = sy * w + sx;
    if (seen[p]) continue;
    const l0 = luma(img, x + sx, y + sy);
    if (l0 <= limit) { seen[p] = 1; continue; }
    const stack = [[sx, sy]];
    seen[p] = 1;
    const b = { area: 0, minX: 1e9, maxX: -1, minY: 1e9, maxY: -1, sumX: 0, sumY: 0, sumL: 0 };
    while (stack.length) {
      const [cx, cy] = stack.pop()!;
      const l = luma(img, x + cx, y + cy);
      b.area++; b.sumX += cx * l; b.sumY += cy * l; b.sumL += l;
      if (cx < b.minX) b.minX = cx; if (cx > b.maxX) b.maxX = cx;
      if (cy < b.minY) b.minY = cy; if (cy > b.maxY) b.maxY = cy;
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = cx + dx, ny = cy + dy;
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const q = ny * w + nx;
        if (seen[q]) continue;
        seen[q] = 1;
        if (luma(img, x + nx, y + ny) > limit) stack.push([nx, ny]);
      }
    }
    if (b.area >= 40) blobsOut.push(b);
  }
  console.log(`image ${img.w}x${img.h} region ${x},${y},${w},${h} background ${background.toFixed(1)} limit ${limit.toFixed(1)}`);
  console.log('area\tbboxAbs\tcentreAbs');
  for (const b of blobsOut.sort((a, b) => a.minX - b.minX))
    console.log(`${b.area}\t[${x + b.minX}..${x + b.maxX}]x[${y + b.minY}..${y + b.maxY}]\t(${(x + b.sumX / b.sumL).toFixed(1)}, ${(y + b.sumY / b.sumL).toFixed(1)})`);
}

// -------------------------------------------------- .vc vertex-constant geometry
// The icon vertex program (quad.vpo) receives the object's world position in the w
// components of slots 256/257 and the camera distance in 259.w:
//   [256] = (2.8125*size, 0, 0, 2.8125*worldX)
//   [257] = (0, 5*size, 0, 5*worldY)
//   [259] = (0, 0, -1, distance)
// and the projected centre in slot 467 = (ndcX, ndcY). Screen mapping at 1080p:
//   screen x = 960 + 2700*worldX/d, screen y = 540 - 2700*worldY/d,
//   projected size = size*2700/d.
// Slot 467 is the cross-check: 960*(1+ndcX), 540*(1-ndcY).
function vcTable(dir: string) {
  const files = readdirSync(dir).filter(f => f.endsWith('.vc'));
  const rows = new Map<string, { count: number; sx: number; sy: number; d: number; size: number; px: number; ndc: string; file: string }>();
  for (const f of files) {
    const b = readFileSync(`${dir}/${f}`);
    if (b.length < 512 * 16) continue;
    const slot = (i: number) => [0, 1, 2, 3].map(k => b.readFloatLE(i * 16 + k * 4));
    const s256 = slot(256), s257 = slot(257), s259 = slot(259), s467 = slot(467);
    if (!Number.isFinite(s256[0]) || s256[0] <= 0) continue;
    const size = s256[0] / 2.8125;
    const worldX = s256[3] / 2.8125;
    const worldY = s257[3] / 5;
    const d = s259[3];
    if (!(d > 0)) continue;
    const sx = 960 + 2700 * worldX / d;
    const sy = 540 - 2700 * worldY / d;
    const px = size * 2700 / d;
    const ndc = `${(960 * (1 + s467[0])).toFixed(2)},${(540 * (1 - s467[1])).toFixed(2)}`;
    const key = `${sx.toFixed(2)}\t${sy.toFixed(2)}\t${d.toFixed(2)}\t${px.toFixed(3)}`;
    const cur = rows.get(key);
    if (cur) cur.count++; else rows.set(key, { count: 1, sx, sy, d, size, px, ndc, file: f });
  }
  console.log(`dir ${dir}: ${files.length} .vc files, ${rows.size} distinct projected draws`);
  console.log('count\tscreenX\tscreenY\tdistance\tsizeWorld\tprojPx\tslot467xy\tfile');
  for (const r of [...rows.values()].sort((a, b) => a.sy - b.sy || a.sx - b.sx))
    console.log(`${r.count}\t${r.sx.toFixed(2)}\t${r.sy.toFixed(2)}\t${r.d.toFixed(2)}\t${r.size.toFixed(3)}\t${r.px.toFixed(2)}\t${r.ndc}\t${r.file}`);
}

if (mode === 'rco') rcoDump(argv[1], new RegExp(argv[2] ?? '.', 'i'));
else if (mode === 'band') band(argv[1], argv[2].split(',').map(Number));
else if (mode === 'blobs') blobs(argv[1], argv[2].split(',').map(Number), Number(argv[3] ?? 12));
else if (mode === 'vc') vcTable(argv[1]);
else {
  console.log(readFileSync(new URL(import.meta.url)).toString('utf8').split('\n').slice(0, 10).join('\n'));
}
