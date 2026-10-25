import { decodePng, psnr, ssim } from './ab-compare';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

// One-shot A/B: run the native to a named state and score it against a stored
// reference. The heavy lifting (native render + PNG decode + PSNR/SSIM) is split
// between re-ps3-native --script and ab-compare.ts; this just glues them together.
//
//   bun work/ab-shot.ts <ref.png> <tokens> [crop x,y,w,h] [--flash dir] [--exe path]
//                      [--bg-clock ISO] [--render-clock sec]
//
// <tokens> are --script tokens before the grab, e.g. "full,wait,wait,wait".

const args = process.argv.slice(2);
const pos: string[] = [];
let flash = 'C:\\dev\\rpcs3\\bin\\dev_flash';
let exe = 'C:\\dev\\xmb\\out\\native-xmb\\re-ps3-native.exe';
let bgClock = '', renderClock = '30', crop: string | undefined;
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--flash') flash = args[++i];
  else if (a === '--exe') exe = args[++i];
  else if (a === '--bg-clock') bgClock = args[++i];
  else if (a === '--render-clock') renderClock = args[++i];
  else if (a === '--crop') crop = args[++i];
  else pos.push(a);
}
const [ref, tokens] = pos;
if (!ref || !tokens) {
  console.error('usage: bun work/ab-shot.ts <ref.png> <tokens> [--crop x,y,w,h] [--flash dir] [--exe path] [--bg-clock ISO] [--render-clock sec]');
  process.exit(2);
}
const grab = join(tmpdir(), `ab-shot-${Date.now()}.png`);
const spawn = ['--dev-flash', flash, '--data-dir', join(tmpdir(), `ab-shot-data-${Date.now()}`), '--render-clock', renderClock];
if (bgClock) spawn.push('--background-clock', bgClock);
spawn.push('--script', `${tokens},grab=${grab},quit`);
const proc = Bun.spawn([exe, ...spawn], { stdout: 'ignore', stderr: 'inherit' });
const code = await proc.exited;
if (code !== 0) { console.error(`native exited with code ${code}`); process.exit(code); }

const refImg = decodePng(ref), nativeImg = decodePng(grab);
if (refImg.w !== nativeImg.w || refImg.h !== nativeImg.h) {
  console.error(`dimension mismatch: ref ${refImg.w}x${refImg.h} vs native ${nativeImg.w}x${nativeImg.h}`);
  process.exit(3);
}
const c = crop ? crop.split(',').map(Number) as [number, number, number, number] : undefined;
console.log(`${ref} vs ${grab}`);
console.log(`  size ${refImg.w}x${refImg.h}${c ? ` crop ${c.join(',')}` : ''}`);
console.log(`  PSNR ${psnr(refImg, nativeImg, c).toFixed(2)} dB  SSIM ${ssim(refImg, nativeImg, c).toFixed(4)}`);
