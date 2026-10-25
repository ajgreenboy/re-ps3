import { inflateSync } from 'node:zlib';
import { readFileSync } from 'node:fs';

export type PngImage = { w: number; h: number; rgba: Uint8Array };
export type Crop = [number, number, number, number];
const crcTable = Uint32Array.from({ length: 256 }, (_, n) => {
  for (let k = 0; k < 8; k++) n = (n >>> 1) ^ ((n & 1) ? 0xedb88320 : 0);
  return n >>> 0;
});
function crc32(bytes: Uint8Array) {
  let crc = 0xffffffff;
  for (const n of bytes) crc = (crc >>> 8) ^ crcTable[(crc ^ n) & 255];
  return (crc ^ 0xffffffff) >>> 0;
}

// 8-bit non-interlaced grayscale, gray-alpha, RGB and RGBA screenshots. Decode
// packed scanlines completely before expanding pixels: expansion must not alter
// the previous packed row used by PNG's Up/Average/Paeth filters.
export function decodePng(path: string): PngImage {
  const b = readFileSync(path);
  const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  if (sig.some((v, i) => b[i] !== v)) throw new Error(`${path}: not a PNG`);
  let w = 0, h = 0, colorType = -1, ended = false;
  const idat: Buffer[] = [];
  let pos = 8;
  while (pos < b.length) {
    if (pos + 12 > b.length) throw new Error(`${path}: truncated PNG chunk`);
    const len = b.readUInt32BE(pos);
    if (pos + 12 + len > b.length) throw new Error(`${path}: truncated PNG chunk data`);
    const type = b.toString('ascii', pos + 4, pos + 8);
    if (crc32(b.subarray(pos + 4, pos + 8 + len)) !== b.readUInt32BE(pos + 8 + len))
      throw new Error(`${path}: invalid ${type} checksum`);
    if (type === 'IHDR') {
      if (pos !== 8 || len !== 13 || w) throw new Error(`${path}: invalid PNG header`);
      w = b.readUInt32BE(pos + 8);
      h = b.readUInt32BE(pos + 12);
      colorType = b[pos + 17];
      if (!w || !h || w * h > 64 * 1024 * 1024) throw new Error(`${path}: invalid or oversized PNG dimensions`);
      if (b[pos + 16] !== 8 || b[pos + 18] !== 0 || b[pos + 19] !== 0 || b[pos + 20] !== 0)
        throw new Error(`${path}: only standard 8-bit non-interlaced PNG is supported`);
    } else if (type === 'IDAT') {
      if (!w) throw new Error(`${path}: PNG image data precedes its header`);
      idat.push(b.subarray(pos + 8, pos + 8 + len));
    } else if (type === 'IEND') {
      if (len !== 0) throw new Error(`${path}: invalid PNG end chunk`);
      ended = true;
      break;
    }
    pos += 12 + len;
  }
  const channels = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 0 ? 1 : colorType === 4 ? 2 : 0;
  if (!ended || !w || !idat.length) throw new Error(`${path}: incomplete PNG`);
  if (!channels) throw new Error(`${path}: unsupported PNG colour type ${colorType}`);
  const stride = w * channels;
  const expected = (stride + 1) * h;
  const raw = inflateSync(Buffer.concat(idat), { maxOutputLength: expected });
  if (raw.length !== expected) throw new Error(`${path}: incorrect PNG scanline length`);
  const packed = new Uint8Array(w * h * channels);
  const paeth = (a: number, b: number, c: number) => {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
  };
  for (let y = 0; y < h; y++) {
    const filter = raw[y * (stride + 1)];
    if (filter > 4) throw new Error(`${path}: unsupported PNG row filter ${filter}`);
    const row = y * stride;
    const prev = (y - 1) * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? packed[row + x - channels] : 0;
      const up = y > 0 ? packed[prev + x] : 0;
      const ul = y > 0 && x >= channels ? packed[prev + x - channels] : 0;
      let v = raw[y * (stride + 1) + 1 + x];
      if (filter === 1) v = (v + a) & 0xff;
      else if (filter === 2) v = (v + up) & 0xff;
      else if (filter === 3) v = (v + Math.floor((a + up) / 2)) & 0xff;
      else if (filter === 4) v = (v + paeth(a, up, ul)) & 0xff;
      packed[row + x] = v;
    }
  }
  const rgba = new Uint8Array(w * h * 4);
  for (let pixel = 0; pixel < w * h; pixel++) {
    const source = pixel * channels, dest = pixel * 4;
    rgba[dest] = packed[source];
    rgba[dest + 1] = channels < 3 ? packed[source] : packed[source + 1];
    rgba[dest + 2] = channels < 3 ? packed[source] : packed[source + 2];
    rgba[dest + 3] = channels === 2 ? packed[source + 1] : channels === 4 ? packed[source + 3] : 255;
  }
  return { w, h, rgba };
}

export function parseCrop(value: string): Crop {
  const parts = value.split(',').map(Number);
  if (parts.length !== 4 || parts.some(n => !Number.isSafeInteger(n)) || parts[0] < 0 || parts[1] < 0 || parts[2] < 1 || parts[3] < 1)
    throw new Error('crop must be four integers x,y,width,height with non-negative origin and positive size');
  return parts as Crop;
}
function checkedCrop(a: PngImage, b: PngImage, crop?: Crop): Crop {
  if (a.w !== b.w || a.h !== b.h) throw new Error(`dimension mismatch: ${a.w}x${a.h} vs ${b.w}x${b.h}`);
  if (!a.w || !a.h || a.rgba.length !== a.w * a.h * 4 || b.rgba.length !== b.w * b.h * 4) throw new Error('invalid image buffer');
  const c = crop ?? [0, 0, a.w, a.h];
  if (c.length !== 4 || c.some(n => !Number.isSafeInteger(n)) || c[0] < 0 || c[1] < 0 || c[2] < 1 || c[3] < 1 || c[0] + c[2] > a.w || c[1] + c[3] > a.h)
    throw new Error('crop is invalid or outside the image');
  return c as Crop;
}

function luma(rgba: Uint8Array, i: number) {
  return 0.2126 * rgba[i * 4] + 0.7152 * rgba[i * 4 + 1] + 0.0722 * rgba[i * 4 + 2];
}

export function psnr(a: { w: number; h: number; rgba: Uint8Array }, b: typeof a, crop?: [number, number, number, number]) {
  const [cx, cy, cw, ch] = checkedCrop(a, b, crop);
  let se = 0, n = 0;
  for (let y = cy; y < cy + ch; y++) for (let x = cx; x < cx + cw; x++) {
    const i = y * a.w + x;
    const dr = a.rgba[i * 4] - b.rgba[i * 4];
    const dg = a.rgba[i * 4 + 1] - b.rgba[i * 4 + 1];
    const db = a.rgba[i * 4 + 2] - b.rgba[i * 4 + 2];
    se += dr * dr + dg * dg + db * db; n += 3;
  }
  const mse = se / n;
  return mse === 0 ? Infinity : 10 * Math.log10(255 * 255 / mse);
}

// Non-overlapping 8x8-window SSIM on luma; incomplete edge tiles are excluded.
export function ssim(a: { w: number; h: number; rgba: Uint8Array }, b: typeof a, crop?: [number, number, number, number]) {
  const [cx, cy, cw, ch] = checkedCrop(a, b, crop);
  if (cw < 8 || ch < 8) throw new Error('SSIM requires a crop at least 8x8 pixels');
  const C1 = (0.01 * 255) ** 2, C2 = (0.03 * 255) ** 2;
  let sum = 0, n = 0;
  const W = 8;
  for (let y0 = cy; y0 + W <= cy + ch; y0 += W) for (let x0 = cx; x0 + W <= cx + cw; x0 += W) {
    let mx = 0, my = 0, vx = 0, vy = 0, vxy = 0;
    for (let y = y0; y < y0 + W; y++) for (let x = x0; x < x0 + W; x++) {
      const i = y * a.w + x;
      const la = luma(a.rgba, i), lb = luma(b.rgba, i);
      mx += la; my += lb; vx += la * la; vy += lb * lb; vxy += la * lb;
    }
    const N = W * W;
    mx /= N; my /= N; vx = vx / N - mx * mx; vy = vy / N - my * my; vxy = vxy / N - mx * my;
    sum += ((2 * mx * my + C1) * (2 * vxy + C2)) / ((mx * mx + my * my + C1) * (vx + vy + C2));
    n++;
  }
  return n ? sum / n : 0;
}

if (import.meta.main) {
  try {
  const [refPath, nativePath, cropArg, cropValue] = process.argv.slice(2);
  if (!refPath || !nativePath) {
    console.error('usage: bun work/ab-compare.ts <ref.png> <native.png> [crop x,y,w,h]');
    process.exit(2);
  }
  const ref = decodePng(refPath), native = decodePng(nativePath);
  if (ref.w !== native.w || ref.h !== native.h) {
    console.error(`dimension mismatch: ref ${ref.w}x${ref.h} vs native ${native.w}x${native.h}`);
    process.exit(3);
  }
  const crop = cropArg ? parseCrop(cropArg === 'crop' ? cropValue ?? '' : cropArg) : undefined;
  const p = psnr(ref, native, crop), s = ssim(ref, native, crop);
  console.log(`${refPath} vs ${nativePath}`);
  console.log(`  size ${ref.w}x${ref.h}${crop ? ` crop ${crop.join(',')}` : ''}`);
  console.log(`  PSNR ${p.toFixed(2)} dB  SSIM ${s.toFixed(4)}`);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(2);
  }
}
