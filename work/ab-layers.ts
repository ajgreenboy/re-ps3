import { decodePng, psnr, ssim, type Crop, type PngImage } from './ab-compare';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

// Per-layer fidelity harness for the native XMB. Scores a fixed set of named
// regions of a native 1920x1080 frame against the stored RPCS3 reference and
// gates each layer against the thresholds file (baseline minus a small delta
// budget, so a regression fails).
//
//   bun work/ab-layers.ts                     score the stored baseline pair
//   bun work/ab-layers.ts --capture           run the native, then score it
//   bun work/ab-layers.ts --native <png>      score an existing native frame
//   bun work/ab-layers.ts --list              list layer names and crops
//   bun work/ab-layers.ts --json              machine-readable output
//
// Exit codes: 0 all layers pass; 1 a layer gate failed; 2 input/runtime error.
// Capture mode always pins the animation clocks and points XMB_STEAM_ROOT at an
// empty directory, so two runs are pixel-identical; see the thresholds file.

type Rect = [number, number, number, number];
type Baseline = { psnr: number; ssim: number };
type Layer = {
  name: string;
  description?: string;
  crop: Rect;
  baseline: Baseline;
  captureBaseline?: Baseline;
};
type Thresholds = {
  schema: string;
  updated?: string;
  reference: string;
  baselinePair: { native: string; note?: string };
  tolerance: { psnrDb: number; ssim: number; rationale?: string };
  capture: {
    exe: string;
    flash: string;
    tokens: string;
    renderClock: number;
    backgroundClock: string;
    uiClock: string;
    focusTimeMs: number;
    timeoutMs: number;
    note?: string;
  };
  layers: Layer[];
};

const repo = resolve(import.meta.dir, '..');
const usage = `usage: bun work/ab-layers.ts [options]

  (no options)             score the stored baseline pair (reference vs native)
  --capture                run the native executable, then score that frame
  --native <png>           score an existing native frame instead
  --ref <png>              override the reference frame
  --thresholds <json>      thresholds file (default work/ab-layers.thresholds.json)
  --list                   list layer names and crops, then exit
  --json                   print a JSON report instead of a table
  --exe <path>             capture executable (default from the thresholds file)
  --flash <dir>            dev_flash directory for capture
  --steam-root <dir>       Steam library root for capture; an empty directory is
                           created inside the capture directory by default
  --render-clock <sec>     frozen animation time for capture (default 30)
  --bg-clock <ISO>         frozen background/icon-material time for capture
  --ui-clock <ISO>         frozen displayed clock for capture
  --focus-time <ms>        frozen focus glow age for capture
  --tokens <csv>           script tokens before the grab (default full,wait,wait,wait)
  --timeout-ms <n>         capture timeout in milliseconds (default 120000)
  --help                   show this text`;

function fail(message: string): never {
  console.error(`ab-layers: ${message}`);
  process.exit(2);
}

function isRect(value: unknown): value is Rect {
  return Array.isArray(value) && value.length === 4 && value.every(n => Number.isSafeInteger(n)) &&
    value[0] >= 0 && value[1] >= 0 && value[2] > 0 && value[3] > 0;
}

function isScore(value: unknown): value is Baseline {
  return !!value && typeof value === 'object' &&
    typeof (value as Baseline).psnr === 'number' && Number.isFinite((value as Baseline).psnr) &&
    typeof (value as Baseline).ssim === 'number' && Number.isFinite((value as Baseline).ssim) &&
    (value as Baseline).ssim >= -1 && (value as Baseline).ssim <= 1;
}

function parseThresholds(path: string): Thresholds {
  let raw: unknown;
  try {
    raw = JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    fail(`${path}: ${error instanceof Error ? error.message : String(error)}`);
  }
  const t = raw as Thresholds;
  if (!t || typeof t !== 'object') fail(`${path}: not a JSON object`);
  if (t.schema !== 'xmb-per-layer-thresholds/1') fail(`${path}: unsupported schema ${JSON.stringify(t.schema)}`);
  if (typeof t.reference !== 'string' || !t.reference) fail(`${path}: reference must be a non-empty path`);
  if (!t.baselinePair || typeof t.baselinePair.native !== 'string' || !t.baselinePair.native)
    fail(`${path}: baselinePair.native must be a non-empty path`);
  const tol = t.tolerance;
  if (!tol || typeof tol.psnrDb !== 'number' || !Number.isFinite(tol.psnrDb) || tol.psnrDb < 0 || tol.psnrDb > 20)
    fail(`${path}: tolerance.psnrDb must be a number from 0 through 20`);
  if (typeof tol.ssim !== 'number' || !Number.isFinite(tol.ssim) || tol.ssim < 0 || tol.ssim > 1)
    fail(`${path}: tolerance.ssim must be a number from 0 through 1`);
  const c = t.capture;
  if (!c || typeof c.exe !== 'string' || typeof c.flash !== 'string' || typeof c.tokens !== 'string' ||
    typeof c.renderClock !== 'number' || !Number.isFinite(c.renderClock) || c.renderClock < 0 ||
    typeof c.backgroundClock !== 'string' || typeof c.uiClock !== 'string' ||
    typeof c.focusTimeMs !== 'number' || !Number.isSafeInteger(c.focusTimeMs) || c.focusTimeMs < 0 ||
    typeof c.timeoutMs !== 'number' || !Number.isSafeInteger(c.timeoutMs) || c.timeoutMs < 1 || c.timeoutMs > 3600000)
    fail(`${path}: capture block is incomplete or invalid`);
  if (!Array.isArray(t.layers) || !t.layers.length) fail(`${path}: layers must be a non-empty array`);
  const names = new Set<string>();
  for (const [index, layer] of t.layers.entries()) {
    if (!layer || typeof layer !== 'object') fail(`${path}: layers[${index}] is not an object`);
    if (typeof layer.name !== 'string' || !layer.name.trim()) fail(`${path}: layers[${index}].name must be non-empty`);
    if (names.has(layer.name)) fail(`${path}: duplicate layer name ${JSON.stringify(layer.name)}`);
    names.add(layer.name);
    if (!isRect(layer.crop)) fail(`${path}: ${layer.name}: crop must be [x,y,width,height] non-negative integers`);
    if (!isScore(layer.baseline)) fail(`${path}: ${layer.name}: baseline must be {psnr,ssim}`);
    if (layer.captureBaseline !== undefined && !isScore(layer.captureBaseline))
      fail(`${path}: ${layer.name}: captureBaseline must be {psnr,ssim}`);
  }
  return t;
}

function resolveRepo(path: string) {
  return resolve(repo, path);
}

function parseArgs(argv: string[]) {
  const opts = {
    capture: false,
    list: false,
    json: false,
    ref: '' as string,
    native: '' as string,
    thresholds: join(repo, 'work', 'ab-layers.thresholds.json'),
    exe: '' as string,
    flash: '' as string,
    steamRoot: '' as string,
    renderClock: '' as string,
    bgClock: '' as string,
    uiClock: '' as string,
    focusTime: '' as string,
    tokens: '' as string,
    timeoutMs: '' as string,
  };
  const seen = new Set<string>();
  const valued = new Set(['--ref', '--native', '--thresholds', '--exe', '--flash', '--steam-root',
    '--render-clock', '--bg-clock', '--ui-clock', '--focus-time', '--tokens', '--timeout-ms']);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--help' || a === '-h') { console.log(usage); process.exit(0); }
    if (a === '--capture') { opts.capture = true; continue; }
    if (a === '--list') { opts.list = true; continue; }
    if (a === '--json') { opts.json = true; continue; }
    if (!a.startsWith('--')) fail(`unexpected argument ${JSON.stringify(a)};\n${usage}`);
    if (!valued.has(a)) fail(`unknown option ${a};\n${usage}`);
    if (seen.has(a)) fail(`repeated option ${a}`);
    seen.add(a);
    const value = argv[++i];
    if (value === undefined || value.startsWith('--')) fail(`${a} needs a value`);
    if (a === '--ref') opts.ref = value;
    else if (a === '--native') opts.native = value;
    else if (a === '--thresholds') opts.thresholds = resolve(value);
    else if (a === '--exe') opts.exe = value;
    else if (a === '--flash') opts.flash = value;
    else if (a === '--steam-root') opts.steamRoot = value;
    else if (a === '--render-clock') opts.renderClock = value;
    else if (a === '--bg-clock') opts.bgClock = value;
    else if (a === '--ui-clock') opts.uiClock = value;
    else if (a === '--focus-time') opts.focusTime = value;
    else if (a === '--tokens') opts.tokens = value;
    else if (a === '--timeout-ms') opts.timeoutMs = value;
  }
  if (opts.native && opts.capture) fail('--native and --capture are mutually exclusive');
  return opts;
}

function checkCropInImage(layer: Layer, image: PngImage) {
  const [x, y, w, h] = layer.crop;
  if (x + w > image.w || y + h > image.h)
    fail(`${layer.name}: crop ${layer.crop.join(',')} is outside the ${image.w}x${image.h} frame`);
  if (w < 8 || h < 8) fail(`${layer.name}: crop must be at least 8x8 pixels (SSIM requirement)`);
}

function decodeChecked(path: string, label: string): PngImage {
  try {
    return decodePng(path);
  } catch (error) {
    fail(`${label} ${path}: ${error instanceof Error ? error.message : String(error)}`);
  }
}

async function captureFrame(t: Thresholds, opts: ReturnType<typeof parseArgs>) {
  const exe = opts.exe || resolveRepo(t.capture.exe);
  const flash = opts.flash || resolveRepo(t.capture.flash);
  const renderClock = opts.renderClock === '' ? t.capture.renderClock : Number(opts.renderClock);
  if (!Number.isFinite(renderClock) || renderClock < 0) fail('--render-clock must be a non-negative number');
  const timeoutMs = opts.timeoutMs === '' ? t.capture.timeoutMs : Number(opts.timeoutMs);
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 3600000)
    fail('--timeout-ms must be an integer from 1 through 3600000');
  const focusTime = opts.focusTime === '' ? String(t.capture.focusTimeMs) : opts.focusTime;
  if (!/^\d+$/.test(focusTime) || !Number.isSafeInteger(Number(focusTime)))
    fail('--focus-time must be a non-negative integer number of milliseconds');
  const tokens = opts.tokens || t.capture.tokens;
  const steps = tokens.split(',').map(s => s.trim()).filter(Boolean);
  const keys = new Set(['full', 'wait', 'LEFT', 'RIGHT', 'UP', 'DOWN', 'ENTER', 'ESC', 'PGUP', 'PGDN',
    'BACKSPACE', 'PREV', 'NEXT', 'SHUFFLE', 'REPEAT', 'ZOOMIN', 'ZOOMOUT', 'RESETVIEW', 'ROTATELEFT', 'ROTATERIGHT']);
  if (!steps.length || steps.some(s => !keys.has(s) && !/^size=\d+x\d+$/.test(s)))
    fail('--tokens must be comma-separated named keys, wait, full, or size=WxH');

  const captureDir = mkdtempSync(join(tmpdir(), 'xmb-layers-'));
  const grab = join(captureDir, 'native.png');
  const steamRoot = opts.steamRoot || join(captureDir, 'no-steam');
  if (!opts.steamRoot) mkdirSync(steamRoot, { recursive: true });
  const args = ['--dev-flash', flash, '--data-dir', join(captureDir, 'data'),
    '--render-clock', String(renderClock)];
  const bgClock = opts.bgClock || t.capture.backgroundClock;
  const uiClock = opts.uiClock || t.capture.uiClock;
  if (bgClock) args.push('--background-clock', bgClock);
  if (uiClock) args.push('--ui-clock', uiClock);
  args.push('--focus-time', focusTime, '--script', [...steps, `grab=${grab}`, 'quit'].join(','));

  console.log(`capture: ${exe}`);
  console.log(`  args: ${args.join(' ')}`);
  const logPath = join(captureDir, 'native.stderr.log');
  const proc = Bun.spawn([exe, ...args], {
    stdout: 'ignore',
    stderr: 'pipe',
    env: { ...process.env, XMB_STEAM_ROOT: steamRoot },
  });
  const stderrText = new Response(proc.stderr).text();
  let timedOut = false;
  const timer = setTimeout(() => { timedOut = true; proc.kill(); }, timeoutMs);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const stderr = await stderrText;
  writeFileSync(logPath, stderr);
  if (timedOut) fail(`capture timed out after ${timeoutMs} ms; evidence: ${captureDir}`);
  if (code !== 0) {
    const tail = stderr.trim().split(/\r?\n/).slice(-15).join('\n');
    fail(`capture exited ${code}; log: ${logPath}` + (tail ? `\n${tail}` : ''));
  }
  return { grab, captureDir };
}

function readCaptureState(grab: string) {
  try {
    const sidecar = JSON.parse(readFileSync(`${grab}.json`, 'utf8'));
    const { category, depth, selection, clock } = sidecar;
    return { category, depth, selection, clock };
  } catch {
    return undefined;
  }
}

function scoreLayer(layer: Layer, ref: PngImage, native: PngImage, captureMode: boolean) {
  const baseline = captureMode && layer.captureBaseline ? layer.captureBaseline : layer.baseline;
  const p = psnr(ref, native, layer.crop as Crop);
  const s = ssim(ref, native, layer.crop as Crop);
  const minPsnr = baseline.psnr - tolerance.psnrDb;
  const minSsim = baseline.ssim - tolerance.ssim;
  const pass = p >= minPsnr && s >= minSsim;
  return { layer, baseline, p, s, minPsnr, minSsim, pass };
}

function pad(value: string, width: number, right = false) {
  return right ? value.padEnd(width) : value.padStart(width);
}

function fmtDb(value: number) {
  return Number.isFinite(value) ? value.toFixed(2) : 'inf';
}

function fmtSsim(value: number) {
  return value.toFixed(4);
}

function fmtDelta(value: number) {
  if (value === Infinity) return '+inf';
  if (!Number.isFinite(value)) return 'n/a';
  if (Math.abs(value) < 0.005) return '+0.00';
  return value > 0 ? `+${value.toFixed(2)}` : value.toFixed(2);
}

function fmtDeltaSsim(value: number) {
  if (Math.abs(value) < 0.00005) return '+0.0000';
  return value > 0 ? `+${value.toFixed(4)}` : value.toFixed(4);
}

let tolerance = { psnrDb: 0, ssim: 0 };

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const thresholds = parseThresholds(opts.thresholds);
  tolerance = thresholds.tolerance;

  if (opts.list) {
    for (const layer of thresholds.layers)
      console.log(`${layer.name.padEnd(18)} ${layer.crop.join(',').padEnd(18)} baseline ${layer.baseline.psnr.toFixed(2)} dB / ${layer.baseline.ssim.toFixed(4)}`);
    return;
  }

  const referencePath = opts.ref ? resolve(opts.ref) : resolveRepo(thresholds.reference);
  let nativePath: string;
  let captureDir = '';
  let captureState: ReturnType<typeof readCaptureState> = undefined;
  let captureMode = false;
  if (opts.capture) {
    captureMode = true;
    const captured = await captureFrame(thresholds, opts);
    nativePath = captured.grab;
    captureDir = captured.captureDir;
    captureState = readCaptureState(nativePath);
  } else {
    nativePath = opts.native ? resolve(opts.native) : resolveRepo(thresholds.baselinePair.native);
  }

  const ref = decodeChecked(referencePath, 'reference');
  const native = decodeChecked(nativePath, captureMode ? 'captured frame' : 'native frame');
  if (ref.w !== native.w || ref.h !== native.h)
    fail(`dimension mismatch: reference ${ref.w}x${ref.h} vs native ${native.w}x${native.h}`);

  const rows = thresholds.layers.map(layer => {
    checkCropInImage(layer, ref);
    return scoreLayer(layer, ref, native, captureMode);
  });
  const failed = rows.filter(row => !row.pass);
  const ok = failed.length === 0;

  if (opts.json) {
    console.log(JSON.stringify({
      schema: 'xmb-per-layer-report/1',
      mode: captureMode ? 'capture' : 'stored-pair',
      reference: referencePath,
      native: nativePath,
      captureDir: captureDir || undefined,
      captureState,
      width: ref.w,
      height: ref.h,
      tolerance: thresholds.tolerance,
      pass: ok,
      layers: rows.map(row => ({
        name: row.layer.name,
        crop: row.layer.crop,
        psnr: Number.isFinite(row.p) ? +row.p.toFixed(4) : 'Infinity',
        ssim: +row.s.toFixed(4),
        baselinePsnr: row.baseline.psnr,
        baselineSsim: row.baseline.ssim,
        minPsnr: +row.minPsnr.toFixed(4),
        minSsim: +row.minSsim.toFixed(4),
        psnrOk: row.p >= row.minPsnr,
        ssimOk: row.s >= row.minSsim,
        pass: row.pass,
      })),
    }, null, 2));
  } else {
    console.log(`per-layer fidelity: ${opts.capture ? 'canonical capture' : 'stored baseline pair'}`);
    console.log(`  reference  ${referencePath} (${ref.w}x${ref.h})`);
    console.log(`  candidate  ${nativePath}`);
    if (captureDir) console.log(`  capture    ${captureDir}`);
    if (captureState)
      console.log(`  state      category=${captureState.category} depth=${captureState.depth} selection=${captureState.selection} clock="${captureState.clock}"`);
    console.log(`  budget     PSNR >= baseline - ${thresholds.tolerance.psnrDb.toFixed(2)} dB, SSIM >= baseline - ${thresholds.tolerance.ssim.toFixed(4)}`);
    console.log('');
    const nameWidth = Math.max(5, ...thresholds.layers.map(l => l.name.length));
    const cropWidth = Math.max(4, ...thresholds.layers.map(l => l.crop.join(',').length));
    const header = `${pad('layer', nameWidth, true)}  ${pad('crop', cropWidth, true)}  ${pad('PSNR', 7)}  ${pad('base', 7)}  ${pad('delta', 6)}  ${pad('SSIM', 6)}  ${pad('base', 6)}  ${pad('delta', 7)}  gate`;
    console.log(header);
    console.log(`${'-'.repeat(nameWidth)}  ${'-'.repeat(cropWidth)}  ${'-'.repeat(7)}  ${'-'.repeat(7)}  ${'-'.repeat(6)}  ${'-'.repeat(6)}  ${'-'.repeat(6)}  ${'-'.repeat(7)}  ${'-'.repeat(4)}`);
    for (const row of rows) {
      console.log([
        pad(row.layer.name, nameWidth, true),
        pad(row.layer.crop.join(','), cropWidth, true),
        pad(fmtDb(row.p), 7),
        pad(row.baseline.psnr.toFixed(2), 7),
        pad(fmtDelta(row.p - row.baseline.psnr), 6),
        pad(fmtSsim(row.s), 6),
        pad(row.baseline.ssim.toFixed(4), 6),
        pad(fmtDeltaSsim(row.s - row.baseline.ssim), 7),
        row.pass ? 'PASS' : 'FAIL',
      ].join('  '));
    }
    console.log('');
    console.log(`${ok ? 'PASS' : 'FAIL'}: ${rows.length - failed.length}/${rows.length} layers within budget`);
    if (!ok) console.log(`failed layers: ${failed.map(row => row.layer.name).join(', ')}`);
  }
  process.exit(ok ? 0 : 1);
}

try {
  await main();
} catch (error) {
  console.error(`ab-layers: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(2);
}
