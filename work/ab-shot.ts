import { decodePng, parseCrop, psnr, ssim } from './ab-compare';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';

// One-shot A/B: run the native to a named state and score it against a stored
// reference. The heavy lifting (native render + PNG decode + PSNR/SSIM) is split
// between re-ps3-native --script and ab-compare.ts; this just glues them together.
//
//   bun work/ab-shot.ts <ref.png> <tokens> [crop x,y,w,h] [--flash dir] [--exe path]
//                      [--bg-clock ISO] [--render-clock sec]
//
// <tokens> are --script tokens before the grab, e.g. "full,wait,wait,wait".

async function main() {
const args = process.argv.slice(2);
const repo = resolve(import.meta.dir, '..');
const pos: string[] = [];
let flash = join(dirname(repo), 'rpcs3', 'bin', 'dev_flash');
let exe = join(repo, 'out', 'native-xmb', 're-ps3-native.exe');
let bgClock = '', renderClock = '30', uiClock = '', focusTime = '', timeoutMs = 30000, crop: string | undefined;
const seen = new Set<string>();
const usage = 'bun work/ab-shot.ts <ref.png> <tokens> [x,y,w,h] [--crop x,y,w,h] [--flash dir] [--exe path] [--bg-clock ISO] [--render-clock seconds] [--ui-clock ISO] [--focus-time milliseconds] [--timeout-ms milliseconds]';
if (args.includes('--help')) { console.log(usage); return; }
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a.startsWith('--')) {
    if (seen.has(a) || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`invalid or repeated option: ${a}`);
    seen.add(a);
  }
  if (a === '--flash') flash = args[++i];
  else if (a === '--exe') exe = args[++i];
  else if (a === '--bg-clock') bgClock = args[++i];
  else if (a === '--render-clock') renderClock = args[++i];
  else if (a === '--ui-clock') uiClock = args[++i];
  else if (a === '--focus-time') focusTime = args[++i];
  else if (a === '--timeout-ms') timeoutMs = Number(args[++i]);
  else if (a === '--crop') crop = args[++i];
  else if (a.startsWith('--')) throw new Error(`unknown option: ${a}`);
  else pos.push(a);
}
const [ref, tokens, legacyCrop] = pos;
if (!ref || !tokens || pos.length > 3) throw new Error(usage);
crop ??= legacyCrop;
const c = crop ? parseCrop(crop) : undefined;
if (!Number.isFinite(Number(renderClock)) || Number(renderClock) < 0) throw new Error('render-clock must be non-negative finite seconds');
if (focusTime && (!/^\d+$/.test(focusTime) || !Number.isSafeInteger(Number(focusTime)))) throw new Error('focus-time must be non-negative integer milliseconds');
if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 3600000) throw new Error('timeout-ms must be an integer from 1 through 3600000');
const steps = tokens.split(',').map(t => t.trim());
const keys = new Set(['full', 'wait', 'LEFT', 'RIGHT', 'UP', 'DOWN', 'ENTER', 'ESC', 'PGUP', 'PGDN', 'BACKSPACE', 'PREV', 'NEXT', 'SHUFFLE', 'REPEAT', 'ZOOMIN', 'ZOOMOUT', 'RESETVIEW', 'ROTATELEFT', 'ROTATERIGHT']);
if (steps.some(t => !keys.has(t) && !/^size=\d+x\d+$/.test(t))) throw new Error('capture tokens must be named keys, wait, full, or size=WxH');
const refImg = decodePng(ref);
psnr(refImg, refImg, c); ssim(refImg, refImg, c);
const capture = mkdtempSync(join(tmpdir(), 'xmb-ab-shot-'));
const grab = join(capture, 'native.png');
const spawn = ['--dev-flash', resolve(flash), '--data-dir', join(capture, 'data'), '--render-clock', renderClock];
if (bgClock) spawn.push('--background-clock', bgClock);
if (uiClock) spawn.push('--ui-clock', uiClock);
if (focusTime) spawn.push('--focus-time', focusTime);
spawn.push('--script', [...steps, `grab=${grab}`, 'quit'].join(','));
const proc = Bun.spawn([resolve(exe), ...spawn], { stdout: 'ignore', stderr: 'inherit', env: { ...process.env, XMB_LOG: join(capture, 'native.log') } });
let timedOut = false;
const timer = setTimeout(() => { timedOut = true; proc.kill(); }, timeoutMs);
let code: number;
try { code = await proc.exited; } finally { clearTimeout(timer); }
if (timedOut) throw new Error(`native capture timed out after ${timeoutMs} ms; evidence: ${capture}`);
if (code !== 0) throw new Error(`native exited ${code}; evidence: ${capture}`);

const nativeImg = decodePng(grab);
if (refImg.w !== nativeImg.w || refImg.h !== nativeImg.h) {
  console.error(`dimension mismatch: ref ${refImg.w}x${refImg.h} vs native ${nativeImg.w}x${nativeImg.h}`);
  process.exit(3);
}
console.log(`${ref} vs ${grab}`);
console.log(`  size ${refImg.w}x${refImg.h}${c ? ` crop ${c.join(',')}` : ''}`);
console.log(`  PSNR ${psnr(refImg, nativeImg, c).toFixed(2)} dB  SSIM ${ssim(refImg, nativeImg, c).toFixed(4)}`);
}
try { await main(); } catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(2);
}
