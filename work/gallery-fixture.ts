// Deterministic Photo Gallery test library for the gate's two gallery states (photo-gallery-piles, photo-gallery-grid).
//
//   bun work/gallery-fixture.ts                  write C:/dev/out/gal/photos if it does not exist yet
//   bun work/gallery-fixture.ts --check          verify the library against its manifest (no writes)
//   bun work/gallery-fixture.ts --dir <path>     use another folder (tests)
//   bun work/gallery-fixture.ts --force          regenerate a library this script wrote (needs its manifest)
//
// It exists because the original library (C:/dev/out/gal/photos, 2026-11-08) was lost with its generator. The gate
// states pin these facts about it, and the generator reproduces them by construction (checked by `--check` and by
// the app's own `--gallery-layout` report, see audit/VERIFICATION-HARNESS.md):
//   374 photos in 17 folders (the root plus 16), 49 calendar days = 49 piles on 2 rows, the newest pile holds 2 photos,
//   365 photos dated by EXIF and 9 by file time, dates spanning 2021-2023 with a New Year's Eve midnight crossing.
//
// The pictures are smooth colour fields (no real photos, nothing downloaded). Everything is derived from fixed tables
// with +, -, *, / and sqrt only, so two runs - on any machine - write identical bytes; the manifest records a sha256.
// It never touches a folder it did not write: an existing, non-empty folder without its manifest is refused.
import { existsSync, mkdirSync, readdirSync, rmSync, statSync, utimesSync, writeFileSync, readFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { createHash } from 'node:crypto';
import { deflateSync } from 'node:zlib';

export const DEFAULT_LIBRARY = 'C:/dev/out/gal/photos';
export const SPEC = { photos: 374, directories: 17, piles: 49, exifPhotos: 365, fileTimePhotos: 9, newestPile: 2 };

// ---- the calendar: one pile per day, oldest first -----------------------------------------------------------------
const DAYS = [
  // 2021 (17): ends on New Year's Eve
  '2021-02-14', '2021-03-06', '2021-03-27', '2021-04-18', '2021-05-09', '2021-05-30', '2021-06-19', '2021-07-04', '2021-07-23',
  '2021-08-14', '2021-09-05', '2021-09-26', '2021-10-17', '2021-11-07', '2021-11-27', '2021-12-12', '2021-12-31',
  // 2022 (18): starts on New Year's Day
  '2022-01-01', '2022-01-22', '2022-02-13', '2022-03-05', '2022-03-26', '2022-04-16', '2022-05-08', '2022-05-29', '2022-06-18',
  '2022-07-09', '2022-07-30', '2022-08-20', '2022-09-10', '2022-10-01', '2022-10-22', '2022-11-12', '2022-12-03', '2022-12-24',
  // 2023 (14)
  '2023-01-14', '2023-02-04', '2023-02-25', '2023-03-18', '2023-04-08', '2023-04-29', '2023-05-20', '2023-06-10', '2023-07-01',
  '2023-07-22', '2023-08-12', '2023-09-02', '2023-09-23', '2023-10-14',
];
// Photos per day, same order. The last (newest) day holds 2 photos; the largest pile holds 44.
const COUNTS = [
  6, 3, 9, 4, 12, 5, 8, 2, 15, 7, 4, 20, 6, 3, 10, 5, 9,
  4, 7, 3, 11, 5, 2, 14, 6, 9, 3, 8, 5, 1, 12, 4, 7, 3, 10,
  5, 8, 3, 6, 44, 4, 9, 2, 7, 5, 3, 11, 23, 2,
];
// Sixteen sub-folders (two levels deep at most); index 0 below is the library root itself.
const FOLDERS = ['', 'family', 'family/2021', 'family/2022', 'trips', 'trips/coast', 'trips/hills', 'trips/lake', 'city', 'city/night',
  'garden', 'pets', 'friends', 'friends/parties', 'work', 'misc', 'misc/old'];
const SIZES: [number, number][] = [[800, 600], [600, 800], [640, 480], [960, 544]];

// ---- small helpers ------------------------------------------------------------------------------------------------
const clamp = (v: number, lo: number, hi: number) => v < lo ? lo : v > hi ? hi : v;
const pad = (n: number, w = 2) => String(n).padStart(w, '0');

// Colour field of one photo at block position (u, v in 0..1): a two-colour gradient, a soft sun and a darker ground.
function hsv(h: number, s: number, v: number): [number, number, number] {   // h in 0..6
  const hh = h - Math.floor(h / 6) * 6, i = Math.floor(hh), f = hh - i;
  const p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  return [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i] as [number, number, number];
}
function fieldColour(g: number, u: number, v: number, aspect: number): [number, number, number] {
  const hue = (g * 2.2917) % 6;                                    // 137.5 degrees of hue per photo, in sixths
  const a = hsv(hue, 0.55 + (g % 4) * 0.08, 0.92), b = hsv(hue + 0.7 + (g % 3) * 0.2, 0.8, 0.36);
  const dir: [number, number][] = [[1, 0], [0, 1], [0.7, 0.7], [-0.7, 0.7], [0.5, 0.86], [-1, 0], [0, -1], [0.86, -0.5]];
  const [gx, gy] = dir[g % dir.length];
  const t = clamp(0.5 + (u - 0.5) * gx + (v - 0.5) * gy, 0, 1);
  let rgb = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const cx = 0.2 + ((g * 37) % 61) / 100, cy = 0.18 + ((g * 17) % 23) / 100;
  const dx = (u - cx) * aspect, dy = v - cy, d = Math.sqrt(dx * dx + dy * dy);
  const glow = Math.max(0, 1 - d / 0.24), sun = glow * glow * 0.55;
  rgb = rgb.map(c => c + (1 - c) * sun);
  const ground = clamp((v - 0.7) / 0.06, 0, 1) * 0.45;
  return rgb.map(c => clamp(c * (1 - ground), 0, 1)) as [number, number, number];
}

// ---- JPEG: baseline 4:4:4, every 8x8 block carries only its DC coefficient -------------------------------------------
// Quantisation 8 makes the quantised DC equal (value - 128), so a block is one flat colour. Huffman tables are custom
// and tiny (DC categories 0-11 as 4-bit codes, AC = end-of-block only), which every libjpeg accepts.
class Bits {
  out: number[] = []; acc = 0; n = 0;
  put(value: number, count: number) {
    for (let i = count - 1; i >= 0; i--) {
      this.acc = (this.acc << 1) | ((value >> i) & 1);
      if (++this.n === 8) { this.out.push(this.acc); if (this.acc === 0xff) this.out.push(0); this.acc = 0; this.n = 0; }
    }
  }
  finish() { while (this.n !== 0) this.put(1, 1); return this.out; }
}
function seg(marker: number, body: number[]): number[] { return [0xff, marker, (body.length + 2) >> 8, (body.length + 2) & 255, ...body]; }
function jpeg(width: number, height: number, g: number, exif: number[] | null): Uint8Array {
  const bw = Math.ceil(width / 8), bh = Math.ceil(height / 8), aspect = width / height;
  const dht = [
    0x00, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...Array.from({ length: 12 }, (_, i) => i),   // DC 0: twelve 4-bit codes
    0x10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00,                                           // AC 0: "0" = end of block
  ];
  const head: number[] = [0xff, 0xd8, ...(exif ? seg(0xe1, exif) : []),
    ...seg(0xdb, [0, ...new Array(64).fill(8)]),
    ...seg(0xc0, [8, height >> 8, height & 255, width >> 8, width & 255, 3, 1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0]),
    ...seg(0xc4, dht),
    ...seg(0xda, [3, 1, 0x00, 2, 0x00, 3, 0x00, 0, 63, 0])];
  const bits = new Bits(), pred = [0, 0, 0];
  for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
    const [r, gr, b] = fieldColour(g, (bx + 0.5) / bw, (by + 0.5) / bh, aspect).map(c => c * 255);
    const y = 0.299 * r + 0.587 * gr + 0.114 * b;
    const cb = 128 - 0.168736 * r - 0.331264 * gr + 0.5 * b, cr = 128 + 0.5 * r - 0.418688 * gr - 0.081312 * b;
    [y, cb, cr].forEach((value, c) => {
      const q = clamp(Math.round(value - 128), -128, 127), diff = q - pred[c];
      pred[c] = q;
      const mag = Math.abs(diff);
      let s = 0; while ((1 << s) <= mag) s++;
      bits.put(s, 4);
      if (s > 0) bits.put(diff >= 0 ? diff : diff + (1 << s) - 1, s);
      bits.put(0, 1);                                                  // AC end of block
    });
  }
  return Uint8Array.from([...head, ...bits.finish(), 0xff, 0xd9]);
}

// ---- EXIF APP1 ------------------------------------------------------------------------------------------------------
type ExifPlan = { bigEndian: boolean; original?: string; digitized?: string; ifd0Date?: string; orientation?: number };
function exifSegment(p: ExifPlan): number[] {
  const be = p.bigEndian;
  const u16 = (v: number) => be ? [v >> 8, v & 255] : [v & 255, v >> 8];
  const u32 = (v: number) => be ? [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255] : [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
  type Entry = { tag: number; type: number; count: number; value: number[] | string };   // string = ASCII payload stored out of line
  const ifd0: Entry[] = [], sub: Entry[] = [];
  if (p.orientation) ifd0.push({ tag: 0x0112, type: 3, count: 1, value: [...u16(p.orientation), 0, 0] });
  if (p.ifd0Date) ifd0.push({ tag: 0x0132, type: 2, count: 20, value: p.ifd0Date });
  if (p.original) sub.push({ tag: 0x9003, type: 2, count: 20, value: p.original });
  if (p.digitized) sub.push({ tag: 0x9004, type: 2, count: 20, value: p.digitized });
  if (sub.length) ifd0.push({ tag: 0x8769, type: 4, count: 1, value: [] });          // pointer patched below
  ifd0.sort((a, b) => a.tag - b.tag);
  const ifdSize = (n: number) => 2 + n * 12 + 4;
  const ifd0At = 8, subAt = ifd0At + ifdSize(ifd0.length), dataAt = sub.length ? subAt + ifdSize(sub.length) : subAt;
  const data: number[] = [];
  const emit = (entries: Entry[], at: number): number[] => {
    const bytes = [...u16(entries.length)];
    for (const e of entries) {
      let value = e.value;
      if (typeof value === 'string') { value = u32(dataAt + data.length); data.push(...Buffer.from(e.value as string + '\0', 'latin1')); }
      else if (e.tag === 0x8769) value = u32(subAt);
      bytes.push(...u16(e.tag), ...u16(e.type), ...u32(e.count), ...value);
    }
    bytes.push(0, 0, 0, 0);
    return bytes;
  };
  const a = emit(ifd0, ifd0At), b = sub.length ? emit(sub, subAt) : [];
  return [...Buffer.from('Exif\0\0', 'latin1'), ...(be ? [0x4d, 0x4d, 0, 42] : [0x49, 0x49, 42, 0]), ...u32(8), ...a, ...b, ...data];
}

// ---- PNG (the no-EXIF files that are not JPEGs) -----------------------------------------------------------------------
const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc32 = (buf: Uint8Array) => { let c = 0xffffffff; for (const x of buf) c = crcTable[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function png(width: number, height: number, g: number): Uint8Array {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  const bw = Math.ceil(width / 8), bh = Math.ceil(height / 8);
  for (let y = 0; y < height; y++) {
    const row = y * (width * 3 + 1);
    for (let x = 0; x < width; x++) {
      const [r, gr, b] = fieldColour(g, (Math.floor(x / 8) + 0.5) / bw, (Math.floor(y / 8) + 0.5) / bh, width / height);
      raw[row + 1 + x * 3] = Math.round(r * 255); raw[row + 2 + x * 3] = Math.round(gr * 255); raw[row + 3 + x * 3] = Math.round(b * 255);
    }
  }
  const chunk = (id: string, body: Uint8Array) => {
    const out = Buffer.alloc(12 + body.length);
    out.writeUInt32BE(body.length, 0); out.write(id, 4, 'latin1'); Buffer.from(body).copy(out, 8);
    out.writeUInt32BE(crc32(out.subarray(4, 8 + body.length)), 8 + body.length);
    return out;
  };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', new Uint8Array())]);
}

// ---- the plan: which photo is where, when, and how it is dated ----------------------------------------------------------
export type Photo = { g: number; day: number; stamp: string; relPath: string; kind: 'jpeg-exif' | 'jpeg-plain' | 'png'; width: number; height: number; plan?: ExifPlan };
export function plan(): Photo[] {
  if (COUNTS.reduce((a, b) => a + b, 0) !== SPEC.photos || COUNTS.length !== SPEC.piles || DAYS.length !== SPEC.piles) throw new Error('calendar tables do not add up');
  const photos: Photo[] = [];
  const noExif = new Set([17, 58, 99, 140, 181, 222, 263, 304, 345]);      // chronological indices dated by file time
  let g = 0, fallbackSeen = 0;
  for (let day = 0; day < DAYS.length; day++) {
    const n = COUNTS[day], step = Math.floor(14 * 3600 / n);
    for (let j = 0; j < n; j++, g++) {
      let seconds = 8 * 3600 + j * step + ((g * 7) % 30);
      if (day === 16 && j === n - 1) seconds = 23 * 3600 + 59 * 60 + 58;   // 2021-12-31 23:59:58 ...
      if (day === 17 && j === 0) seconds = 3;                               // ... and 2022-01-01 00:00:03
      const stamp = `${DAYS[day].replace(/-/g, ':')} ${pad(Math.floor(seconds / 3600))}:${pad(Math.floor(seconds / 60) % 60)}:${pad(seconds % 60)}`;
      const folder = FOLDERS[g < FOLDERS.length ? g : Number((BigInt(g) * 2654435761n) % BigInt(FOLDERS.length))];
      const [width, height] = SIZES[g % SIZES.length];
      const base: Omit<Photo, 'relPath' | 'kind' | 'plan'> = { g, day, stamp, width, height };
      if (noExif.has(g)) {
        const png_ = fallbackSeen++ % 2 === 1;                              // 5 plain JPEGs, 4 PNGs
        photos.push({ ...base, kind: png_ ? 'png' : 'jpeg-plain', relPath: join(folder, `IMG_${pad(g, 4)}.${png_ ? 'png' : 'jpg'}`).replace(/\\/g, '/'), width: png_ ? 640 : width, height: png_ ? 480 : height });
        continue;
      }
      const decoy = `${pad(2024)}:${stamp.slice(5)}`;                         // a later date: it must lose against the original
      const ifd0Only = g % 17 === 0, digitizedOnly = !ifd0Only && g % 13 === 0;
      const ep: ExifPlan = { bigEndian: g % 11 === 0 };
      if (ifd0Only) ep.ifd0Date = stamp;
      else {
        if (digitizedOnly) ep.digitized = stamp; else { ep.original = stamp; ep.digitized = stamp; }
        if (g % 5 === 0) ep.ifd0Date = decoy;
      }
      if (g % 7 === 0) ep.orientation = 6;
      photos.push({ ...base, kind: 'jpeg-exif', relPath: join(folder, `IMG_${pad(g, 4)}.jpg`).replace(/\\/g, '/'), plan: ep });
    }
  }
  return photos;
}

function sha(dir: string): string {
  const h = createHash('sha256');
  const walk = (d: string) => {
    for (const name of readdirSync(d).sort()) {
      const p = join(d, name);
      if (statSync(p).isDirectory()) walk(p); else { h.update(relative(dir, p).replace(/\\/g, '/') + '\0'); h.update(readFileSync(p)); }
    }
  };
  walk(dir);
  return h.digest('hex');
}
const manifestPath = (dir: string) => join(dirname(dir), 'library.json');
const PHOTO_EXT = /\.(png|jpe?g|bmp|webp|gif|tiff?)$/i;
function census(dir: string) {
  let files = 0; const dirs = new Set<string>();
  const walk = (d: string) => { dirs.add(d); for (const name of readdirSync(d)) { const p = join(d, name); if (statSync(p).isDirectory()) walk(p); else if (PHOTO_EXT.test(name)) files++; } };
  walk(dir);
  return { files, directories: dirs.size };
}

export function checkGalleryLibrary(dir = DEFAULT_LIBRARY): string[] {
  if (!existsSync(dir)) return [`missing: ${dir}`];
  if (!existsSync(manifestPath(dir))) return [`${dir} exists but ${manifestPath(dir)} does not: not a library this script wrote`];
  const problems: string[] = [];
  const c = census(dir), m = JSON.parse(readFileSync(manifestPath(dir), 'utf8'));
  if (c.files !== SPEC.photos) problems.push(`${c.files} photo files, expected ${SPEC.photos}`);
  if (c.directories !== SPEC.directories) problems.push(`${c.directories} folders, expected ${SPEC.directories}`);
  if (m.sha256 !== sha(dir)) problems.push('content differs from the manifest (a file was changed, added or removed)');
  return problems;
}

export function generateGalleryLibrary(dir = DEFAULT_LIBRARY, force = false): string {
  if (existsSync(dir) && readdirSync(dir).length > 0) {
    if (!force || !existsSync(manifestPath(dir))) throw new Error(`${dir} is not empty and ${force ? 'has no manifest from this script' : 'force was not given'}; nothing was changed`);
    rmSync(dir, { recursive: true });
  }
  mkdirSync(dir, { recursive: true });
  const photos = plan();
  for (const p of photos) {
    const path = join(dir, p.relPath);
    mkdirSync(dirname(path), { recursive: true });
    const bytes = p.kind === 'png' ? png(p.width, p.height, p.g) : jpeg(p.width, p.height, p.g, p.plan ? exifSegment(p.plan) : null);
    writeFileSync(path, bytes);
    // File time: a no-EXIF file is dated by its local mtime (noon of its day); the others get their capture time.
    const [y, mo, d] = DAYS[p.day].split('-').map(Number);
    const [hh, mi, ss] = p.kind === 'jpeg-exif' ? p.stamp.slice(11).split(':').map(Number) : [12, p.g % 60, 0];
    const when = new Date(y, mo - 1, d, hh, mi, ss);
    utimesSync(path, when, when);
  }
  const digest = sha(dir);
  writeFileSync(manifestPath(dir), JSON.stringify({ generator: 'work/gallery-fixture.ts', ...SPEC, sha256: digest }, null, 2) + '\n');
  return digest;
}

if (import.meta.main) {
  const arg = (name: string) => { const i = process.argv.indexOf(name); return i >= 0 ? process.argv[i + 1] : undefined; };
  const dir = arg('--dir') ?? DEFAULT_LIBRARY;
  if (process.argv.includes('--check')) {
    const problems = checkGalleryLibrary(dir);
    for (const p of problems) console.error(`gallery library: ${p}`);
    if (!problems.length) console.log(`gallery library ready: ${dir}`);
    process.exit(problems.length ? 1 : 0);
  }
  const force = process.argv.includes('--force');
  if (!force && existsSync(manifestPath(dir)) && checkGalleryLibrary(dir).length === 0) { console.log(`gallery library already present: ${dir}`); process.exit(0); }
  const digest = generateGalleryLibrary(dir, force);
  console.log(`gallery library written: ${dir}  (${SPEC.photos} photos, sha256 ${digest.slice(0, 16)})`);
}
