import { inflateSync } from 'node:zlib';
import { readFileSync } from 'node:fs';

// Minimal PNG reader (8-bit RGB/RGBA, non-interlaced) for the native --script and
// RPCS3 screenshots. Enough for PSNR/SSIM; the whole point of Phase 7 is pass/fail
// numbers, not a general image library.
function decodePng(path: string): { w: number; h: number; rgba: Uint8Array } {
  const b = readFileSync(path);
  const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  if (sig.some((v, i) => b[i] !== v)) throw new Error(`${path}: not a PNG`);
  let w = 0, h = 0, bitDepth = 0, colorType = 0;
  const idat: Buffer[] = [];
  let pos = 8;
  while (pos < b.length) {
    const len = b.readUInt32BE(pos);
    const type = b.toString('ascii', pos + 4, pos + 8);
    if (type === 'IHDR') {
      w = b.readUInt32BE(pos + 8);
      h = b.readUInt32BE(pos + 12);
      bitDepth = b[pos + 16];
      colorType = b[pos + 17];
      const interlace = b[pos + 20];
      if (bitDepth !== 8 || interlace !== 0) throw new Error(`${path}: only 8-bit non-interlaced PNG is supported`);
    } else if (type === 'IDAT') {
      idat.push(b.subarray(pos + 8, pos + 8 + len));
    } else if (type === 'IEND') {
      break;
    }
    pos += 12 + len;
  }
  const channels = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 0 ? 1 : colorType === 4 ? 2 : 0;
  if (!channels) throw new Error(`${path}: unsupported PNG colour type ${colorType}`);
  const raw = inflateSync(Buffer.concat(idat));
  const stride = w * channels;
  const rgba = new Uint8Array(w * h * 4);
  const paeth = (a: number, b: number, c: number) => {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
  };
  for (let y = 0; y < h; y++) {
    const filter = raw[y * (stride + 1)];
    const row = y * stride;
    const prev = (y - 1) * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? rgba[row + x - channels] : 0;
      const up = y > 0 ? rgba[prev + x] : 0;
      const ul = y > 0 && x >= channels ? rgba[prev + x - channels] : 0;
      let v = raw[y * (stride + 1) + 1 + x];
      if (filter === 1) v = (v + a) & 0xff;
      else if (filter === 2) v = (v + up) & 0xff;
      else if (filter === 3) v = (v + Math.floor((a + up) / 2)) & 0xff;
      else if (filter === 4) v = (v + paeth(a, up, ul)) & 0xff;
      rgba[row + x] = v;
    }
    // Expand to RGBA (and drop the alpha for the comparison, which is grayscale RGB).
    if (channels !== 4) {
      for (let x = w - 1; x >= 0; x--) {
        for (let c = 3; c >= 0; c--) rgba[row + x * 4 + c] = c < channels ? rgba[row + x * channels + c] : 255;
      }
    }
  }
  return { w, h, rgba };
}

function luma(rgba: Uint8Array, i: number) {
  return 0.2126 * rgba[i * 4] + 0.7152 * rgba[i * 4 + 1] + 0.0722 * rgba[i * 4 + 2];
}

function psnr(a: { w: number; h: number; rgba: Uint8Array }, b: typeof a, crop?: [number, number, number, number]) {
  const [cx, cy, cw, ch] = crop ?? [0, 0, a.w, a.h];
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

// 8x8-window SSIM on luma, as the reference video comparisons elsewhere use.
function ssim(a: { w: number; h: number; rgba: Uint8Array }, b: typeof a, crop?: [number, number, number, number]) {
  const [cx, cy, cw, ch] = crop ?? [0, 0, a.w, a.h];
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

const [refPath, nativePath, cropArg] = process.argv.slice(2);
if (!refPath || !nativePath) {
  console.error('usage: bun work/ab-compare.ts <ref.png> <native.png> [crop x,y,w,h]');
  process.exit(2);
}
const ref = decodePng(refPath), native = decodePng(nativePath);
if (ref.w !== native.w || ref.h !== native.h) {
  console.error(`dimension mismatch: ref ${ref.w}x${ref.h} vs native ${native.w}x${native.h}`);
  process.exit(3);
}
const crop = cropArg ? cropArg.split(',').map(Number) as [number, number, number, number] : undefined;
const p = psnr(ref, native, crop), s = ssim(ref, native, crop);
console.log(`${refPath} vs ${nativePath}`);
console.log(`  size ${ref.w}x${ref.h}${crop ? ` crop ${crop.join(',')}` : ''}`);
console.log(`  PSNR ${p.toFixed(2)} dB  SSIM ${s.toFixed(4)}`);
