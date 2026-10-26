import { decodePng, psnr, ssim, type Crop, type PngImage } from './ab-compare';
import { copyFileSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';

// Per-layer fidelity harness for the native XMB. Scores a fixed set of named
// regions of a native 1920x1080 frame against the stored RPCS3 reference and
// gates each layer against the thresholds file (baseline minus a small delta
// budget, so a regression fails).
//
// Named Steam/XMB-native pages (Game root with Steam rows, Steam Store sections,
// Not Installed folder) have no RPCS3 frame. They are scored against a stored
// deterministic native render of the same state instead: a self-baseline
// regression gate, explicitly not a firmware-fidelity number.
//
//   bun work/ab-layers.ts                     score the stored baseline pair
//   bun work/ab-layers.ts --capture           run the native, then score it
//   bun work/ab-layers.ts --native <png>      score an existing native frame
//   bun work/ab-layers.ts --states            capture and gate every named state
//   bun work/ab-layers.ts --state <name>      capture and gate one named state
//   bun work/ab-layers.ts --all               both gate families in one run
//   bun work/ab-layers.ts --states --save-reference
//                                             (re)write the state references
//   bun work/ab-layers.ts --list              list layers; --list-states lists states
//   bun work/ab-layers.ts --json              machine-readable output
//
// Exit codes: 0 all gates pass; 1 a metric gate failed; 2 input/runtime error or
// a named-state dump assertion failed.
// State captures always pin the animation clocks, disable Steam artwork fetches
// and copy a seed data directory, so two runs are pixel-identical; see the
// thresholds file.

type Rect = [number, number, number, number];
type Baseline = { psnr: number; ssim: number };
type Layer = {
  name: string;
  description?: string;
  crop: Rect;
  baseline: Baseline;
  captureBaseline?: Baseline;
};
type StateRegion = { name: string; crop: Rect };
type StateExpect = {
  category?: string;
  depth?: number;
  entryId?: string;
  entryTitle?: string;
  entryCount?: number;
  catalogueHas?: string[];
  steamRowsBeforeStore?: boolean;
  folderMode?: string;
  folderCountFormula?: string;
  folderDetailFormula?: string;
  folderRowsPrefix?: string;
  folderRowsDetailContains?: string;
  noStoreDump?: boolean;
};
type StateRecipe = {
  name: string;
  description?: string;
  reference: string;
  dataSeed: string;
  seedPaths: string[];
  waits: number;
  settleWaits?: number;
  targetCatalogueId?: string;
  pressEnter?: boolean;
  expect: StateExpect;
  regions: StateRegion[];
};
type Thresholds = {
  schema: string;
  updated?: string;
  reference: string;
  baselinePair: { native: string; note?: string };
  tolerance: { psnrDb: number; ssim: number; rationale?: string };
  stateSelfBaseline?: { psnrDb: number; ssim: number; rationale?: string };
  states?: StateRecipe[];
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
  --states                 capture and gate every named state
  --state <name>           capture and gate one named state
  --all                    stored pair + canonical capture + every named state
  --save-reference         with --states/--state: write the fresh capture as that
                           state's reference (deliberate refresh after an
                           intended rendering change), then report only
  --ref <png>              override the reference frame
  --thresholds <json>      thresholds file (default work/ab-layers.thresholds.json)
  --list                   list layer names and crops, then exit
  --list-states            list named-state recipes and references, then exit
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
  const self = t.stateSelfBaseline;
  if (self !== undefined) {
    if (typeof self.psnrDb !== 'number' || !Number.isFinite(self.psnrDb) || self.psnrDb < 0 || self.psnrDb > 60)
      fail(`${path}: stateSelfBaseline.psnrDb must be a number from 0 through 60`);
    if (typeof self.ssim !== 'number' || !Number.isFinite(self.ssim) || self.ssim < 0 || self.ssim > 1)
      fail(`${path}: stateSelfBaseline.ssim must be a number from 0 through 1`);
  }
  if (t.states !== undefined) {
    if (!self) fail(`${path}: states require stateSelfBaseline`);
    if (!Array.isArray(t.states) || !t.states.length) fail(`${path}: states must be a non-empty array when present`);
    const stateNames = new Set<string>();
    for (const [index, state] of t.states.entries()) {
      if (!state || typeof state !== 'object') fail(`${path}: states[${index}] is not an object`);
      if (typeof state.name !== 'string' || !state.name.trim()) fail(`${path}: states[${index}].name must be non-empty`);
      if (stateNames.has(state.name) || names.has(state.name))
        fail(`${path}: duplicate state name ${JSON.stringify(state.name)}`);
      stateNames.add(state.name);
      if (typeof state.reference !== 'string' || !state.reference) fail(`${path}: ${state.name}: reference must be a path`);
      if (typeof state.dataSeed !== 'string' || !state.dataSeed) fail(`${path}: ${state.name}: dataSeed must be a path`);
      if (!Array.isArray(state.seedPaths) || !state.seedPaths.length ||
        state.seedPaths.some(seed => typeof seed !== 'string' || !seed))
        fail(`${path}: ${state.name}: seedPaths must be a non-empty string array`);
      if (!Number.isSafeInteger(state.waits) || state.waits < 0 || state.waits > 60)
        fail(`${path}: ${state.name}: waits must be an integer from 0 through 60`);
      if (state.settleWaits !== undefined &&
        (!Number.isSafeInteger(state.settleWaits) || state.settleWaits < 0 || state.settleWaits > 60))
        fail(`${path}: ${state.name}: settleWaits must be an integer from 0 through 60`);
      if (state.targetCatalogueId !== undefined &&
        (typeof state.targetCatalogueId !== 'string' || !state.targetCatalogueId))
        fail(`${path}: ${state.name}: targetCatalogueId must be a non-empty string`);
      if (state.pressEnter !== undefined && typeof state.pressEnter !== 'boolean')
        fail(`${path}: ${state.name}: pressEnter must be a boolean`);
      if (!state.expect || typeof state.expect !== 'object' || Array.isArray(state.expect))
        fail(`${path}: ${state.name}: expect must be an object`);
      if (!Array.isArray(state.regions) || !state.regions.length)
        fail(`${path}: ${state.name}: regions must be a non-empty array`);
      for (const [regionIndex, region] of state.regions.entries()) {
        if (!region || typeof region !== 'object' || typeof region.name !== 'string' || !region.name ||
          !isRect(region.crop))
          fail(`${path}: ${state.name}: regions[${regionIndex}] must be {name,crop}`);
      }
    }
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
    listStates: false,
    json: false,
    states: false,
    state: '',
    all: false,
    saveReference: false,
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
    '--render-clock', '--bg-clock', '--ui-clock', '--focus-time', '--tokens', '--timeout-ms', '--state']);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--help' || a === '-h') { console.log(usage); process.exit(0); }
    if (a === '--capture') { opts.capture = true; continue; }
    if (a === '--list') { opts.list = true; continue; }
    if (a === '--list-states') { opts.listStates = true; continue; }
    if (a === '--json') { opts.json = true; continue; }
    if (a === '--states') { opts.states = true; continue; }
    if (a === '--all') { opts.all = true; continue; }
    if (a === '--save-reference') { opts.saveReference = true; continue; }
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
    else if (a === '--state') opts.state = value;
  }
  if (opts.native && opts.capture) fail('--native and --capture are mutually exclusive');
  if (opts.states && opts.state) fail('--states and --state are mutually exclusive');
  if (opts.all && (opts.states || opts.state)) fail('--all already runs every named state');
  if (opts.saveReference && !opts.states && !opts.state) fail('--save-reference requires --states or --state');
  if ((opts.states || opts.state || opts.all) && (opts.capture || opts.native))
    fail('--capture/--native do not combine with the named-state modes');
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

// ---------------------------------------------------------------------------
// Named states: deterministic native captures gated against a stored native
// reference render of the same state (self-baseline, not firmware fidelity).
// ---------------------------------------------------------------------------

type StateProcess = { captureDir: string; grabPath: string; dumpPath: string; logPath: string };

function readJsonFile(path: string): any {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    fail(`${path}: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function seedDataDir(recipe: StateRecipe, captureDir: string) {
  const dataDir = join(captureDir, 'data');
  mkdirSync(dataDir, { recursive: true });
  for (const seed of recipe.seedPaths) {
    const source = resolveRepo(join(recipe.dataSeed, seed));
    if (!existsSync(source)) fail(`${recipe.name}: data seed missing: ${source}`);
    cpSync(source, join(dataDir, seed), { recursive: true });
  }
  return dataDir;
}

async function runStateProcess(t: Thresholds, opts: ReturnType<typeof parseArgs>, recipe: StateRecipe,
  tokens: string[]): Promise<StateProcess> {
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

  const captureDir = mkdtempSync(join(tmpdir(), 'xmb-layers-state-'));
  const dataDir = seedDataDir(recipe, captureDir);
  const grabPath = join(captureDir, 'native.png');
  const dumpPath = join(captureDir, 'entries.json');
  const logPath = join(captureDir, 'native.stderr.log');
  const finalTokens = tokens.map(token => token === '{grab}' ? `grab=${grabPath}` : token);
  const bgClock = opts.bgClock || t.capture.backgroundClock;
  const uiClock = opts.uiClock || t.capture.uiClock;
  const args = ['--dev-flash', flash, '--data-dir', dataDir, '--render-clock', String(renderClock)];
  if (bgClock) args.push('--background-clock', bgClock);
  if (uiClock) args.push('--ui-clock', uiClock);
  args.push('--focus-time', focusTime, '--script', finalTokens.join(','));
  const env = {
    ...process.env,
    XMB_DUMP_ENTRIES: dumpPath,
    XMB_STEAM_ARTWORK: '0',
  } as Record<string, string | undefined>;
  if (opts.steamRoot) env.XMB_STEAM_ROOT = opts.steamRoot;

  if (!quiet) console.log(`  script   ${finalTokens.join(',')}`);
  const proc = Bun.spawn([exe, ...args], { stdout: 'ignore', stderr: 'pipe', env });
  const stderrText = new Response(proc.stderr).text();
  let timedOut = false;
  const timer = setTimeout(() => { timedOut = true; proc.kill(); }, timeoutMs);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const stderr = await stderrText;
  writeFileSync(logPath, stderr);
  if (timedOut) fail(`${recipe.name}: capture timed out after ${timeoutMs} ms; evidence: ${captureDir}`);
  if (code !== 0) {
    const tail = stderr.trim().split(/\r?\n/).slice(-15).join('\n');
    fail(`${recipe.name}: capture exited ${code}; log: ${logPath}` + (tail ? `\n${tail}` : ''));
  }
  return { captureDir, grabPath, dumpPath, logPath };
}

function gameEntries(dump: any): any[] {
  const game = dump && Array.isArray(dump.categories) ? dump.categories.find((c: any) => c && c.id === 'game') : undefined;
  return game && Array.isArray(game.entries) ? game.entries : [];
}

function familySharedCount(recipe: StateRecipe) {
  const file = resolveRepo(join(recipe.dataSeed, 'family-shared.json'));
  try {
    const apps = JSON.parse(readFileSync(file, 'utf8')).apps;
    return Array.isArray(apps) ? apps.length : 0;
  } catch {
    return 0;
  }
}

function checkStateExpect(recipe: StateRecipe, sidecar: any, dump: any, folder: any, store: any) {
  const expect = recipe.expect;
  const problems: string[] = [];
  const actual: Record<string, any> = {
    category: sidecar.category,
    depth: sidecar.depth,
    entryId: sidecar.entryId,
    entryTitle: sidecar.entryTitle,
    entryCount: sidecar.entryCount,
  };
  for (const key of ['category', 'depth', 'entryId', 'entryTitle', 'entryCount'] as const) {
    if (expect[key] !== undefined && actual[key] !== expect[key])
      problems.push(`${key}: expected ${JSON.stringify(expect[key])}, got ${JSON.stringify(actual[key])}`);
  }
  const entries = gameEntries(dump);
  for (const id of expect.catalogueHas ?? [])
    if (!entries.some(entry => entry.id === id))
      problems.push(`catalogue Game column has no entry ${JSON.stringify(id)}`);
  const installed = Number(dump.installedSteam);
  const owned = Number(dump.owned);
  const family = familySharedCount(recipe);
  const expectedFolderCount = Number.isFinite(owned) && Number.isFinite(installed) ? owned - installed + family : NaN;
  if (expect.steamRowsBeforeStore) {
    const storeIndex = entries.findIndex(entry => entry.id === 'steam-store');
    if (storeIndex < 0) problems.push('catalogue Game column has no steam-store row');
    else {
      const before = entries.slice(0, storeIndex).map(entry => entry.id);
      const notSteam = before.filter((id: string) => !id.startsWith('steam:'));
      if (notSteam.length) problems.push(`rows before steam-store that are not inline Steam titles: ${notSteam.join(', ')}`);
      if (Number.isFinite(installed) && before.length !== installed)
        problems.push(`inline Steam rows before steam-store: expected installedSteam=${installed}, got ${before.length}`);
    }
  }
  if (expect.folderDetailFormula === 'owned-installed+familyShared') {
    const row = entries.find(entry => entry.id === 'steam-folder-library');
    if (!row) problems.push('catalogue Game column has no steam-folder-library row');
    else if (String(row.detail) !== String(expectedFolderCount))
      problems.push(`Not Installed row detail: expected ${expectedFolderCount} (owned - installed + familyShared), got ${JSON.stringify(row.detail)}`);
  }
  if (expect.folderMode !== undefined) {
    if (!folder) problems.push('expected the folder-open dump <dump>.folder.json but no folder opened');
    else {
      if (folder.folder !== expect.folderMode)
        problems.push(`folder dump mode: expected ${JSON.stringify(expect.folderMode)}, got ${JSON.stringify(folder.folder)}`);
      if (expect.folderCountFormula === 'owned-installed+familyShared') {
        if (folder.count !== expectedFolderCount)
          problems.push(`folder rows: expected ${expectedFolderCount} (owned - installed + familyShared), got ${folder.count}`);
        if (Array.isArray(folder.rows) && folder.rows.length !== folder.count)
          problems.push(`folder rows array has ${folder.rows.length} entries but count says ${folder.count}`);
      }
      if (Array.isArray(folder.rows)) {
        if (expect.folderRowsPrefix) {
          const bad = folder.rows.filter((row: any) => !String(row.id).startsWith(expect.folderRowsPrefix!));
          if (bad.length) problems.push(`${bad.length} folder rows do not start with ${JSON.stringify(expect.folderRowsPrefix)} (e.g. ${bad[0].id})`);
        }
        if (expect.folderRowsDetailContains) {
          const bad = folder.rows.filter((row: any) => !String(row.detail).includes(expect.folderRowsDetailContains!));
          if (bad.length) problems.push(`${bad.length} folder rows lack ${JSON.stringify(expect.folderRowsDetailContains)} in their detail (e.g. ${bad[0].id})`);
        }
      }
      if (sidecar.entryCount !== folder.count)
        problems.push(`sidecar entryCount ${sidecar.entryCount} != folder rows ${folder.count}`);
    }
  }
  if (expect.noStoreDump && store)
    problems.push('a <dump>.store.json exists: a store request ran, so this state is not network-free and may not be deterministic');
  const summary = [
    `depth ${sidecar.depth}`,
    `category ${sidecar.category}`,
    `selection ${sidecar.selection}`,
    `rows ${sidecar.entryCount}`,
  ];
  if (folder) summary.push(`folder ${folder.count}`);
  return { problems, summary: summary.join(', ') };
}

function findState(thresholds: Thresholds, name: string) {
  const states = thresholds.states ?? [];
  const recipe = states.find(state => state.name === name);
  if (!recipe) fail(`unknown named state ${JSON.stringify(name)}; known: ${states.map(state => state.name).join(', ') || 'none'}`);
  return recipe;
}

async function runStates(thresholds: Thresholds, opts: ReturnType<typeof parseArgs>) {
  const self = thresholds.stateSelfBaseline;
  if (!self) fail(`${opts.thresholds}: named states require stateSelfBaseline`);
  const recipes = opts.state ? [findState(thresholds, opts.state)] : (thresholds.states ?? []);
  if (!recipes.length) fail(`${opts.thresholds}: defines no named states`);
  const jsonMode = opts.json && !opts.all;

  // One Game-column discovery capture gives the row indices used to script the
  // page states. It also proves the rebuild produced the Steam rows at all.
  let catalogueIndices: Map<string, number> | undefined;
  if (recipes.some(recipe => recipe.targetCatalogueId)) {
    const discovery = recipes[0];
    const discoveryTokens = ['full', ...Array(discovery.waits + 6).fill('wait'), 'quit'];
    if (!quiet) console.log('discovery: Game column catalogue from a root capture');
    const run = await runStateProcess(thresholds, opts, discovery, discoveryTokens);
    const dump = readJsonFile(run.dumpPath);
    const entries = gameEntries(dump);
    catalogueIndices = new Map<string, number>();
    entries.forEach((entry, index) => { if (!catalogueIndices!.has(entry.id)) catalogueIndices!.set(entry.id, index); });
    for (const recipe of recipes) {
      if (!recipe.targetCatalogueId) continue;
      if (!catalogueIndices.has(recipe.targetCatalogueId))
        fail(`${recipe.name}: catalogue has no entry ${JSON.stringify(recipe.targetCatalogueId)}; `
          + `the Game column no longer shows the expected Steam rows (rows: ${entries.map(entry => entry.id).join(', ')}). Evidence: ${run.captureDir}`);
    }
    const targets = recipes.filter(recipe => recipe.targetCatalogueId)
      .map(recipe => `${recipe.targetCatalogueId}=${catalogueIndices!.get(recipe.targetCatalogueId)}`).join(', ');
    if (!quiet) {
      console.log(`  Game rows ${entries.length} (installedSteam ${dump.installedSteam}, owned ${dump.owned}); ${targets}`);
      console.log(`  evidence  ${run.captureDir}`);
    }
  }

  const reports: any[] = [];
  let worst = 0;
  let passed = 0;
  if (!jsonMode) {
    console.log('');
    console.log(`named states: deterministic native self-baseline (NOT firmware fidelity)`);
    console.log(`  gate      PSNR >= ${self.psnrDb.toFixed(2)} dB, SSIM >= ${self.ssim.toFixed(4)} against each stored native reference`);
  }
  for (const recipe of recipes) {
    const tokens = ['full', ...Array(recipe.waits).fill('wait')];
    if (recipe.targetCatalogueId) {
      const index = catalogueIndices ? catalogueIndices.get(recipe.targetCatalogueId) : undefined;
      if (index === undefined) fail(`${recipe.name}: no catalogue index for ${recipe.targetCatalogueId}`);
      tokens.push(...Array(index).fill('DOWN'));
      if (recipe.pressEnter) tokens.push('ENTER');
    }
    tokens.push(...Array(recipe.settleWaits ?? 3).fill('wait'), '{grab}', 'quit');
    // The grab token needs the absolute capture path: replace the placeholder.
    if (!jsonMode) console.log(`\n-- ${recipe.name}`);
    if (recipe.description && !jsonMode) console.log(`   ${recipe.description}`);
    const run = await runStateProcess(thresholds, opts, recipe, tokens);
    const grabPath = run.grabPath;
    const sidecarPath = `${grabPath}.json`;
    const sidecar = existsSync(sidecarPath) ? readJsonFile(sidecarPath) : undefined;
    const dump = existsSync(run.dumpPath) ? readJsonFile(run.dumpPath) : undefined;
    const folderPath = `${run.dumpPath}.folder.json`;
    const storePath = `${run.dumpPath}.store.json`;
    const folder = existsSync(folderPath) ? readJsonFile(folderPath) : undefined;
    const store = existsSync(storePath) ? readJsonFile(storePath) : undefined;
    if (!sidecar) fail(`${recipe.name}: no state sidecar ${sidecarPath}; capture evidence: ${run.captureDir}`);
    if (!dump) fail(`${recipe.name}: no catalogue dump ${run.dumpPath}; capture evidence: ${run.captureDir}`);
    const { problems, summary } = checkStateExpect(recipe, sidecar, dump, folder, store);
    const referencePath = resolveRepo(recipe.reference);
    const report: any = {
      name: recipe.name,
      captureDir: run.captureDir,
      grab: grabPath,
      reference: referencePath,
      state: { category: sidecar.category, depth: sidecar.depth, selection: sidecar.selection, entryId: sidecar.entryId, entryTitle: sidecar.entryTitle, entryCount: sidecar.entryCount },
      assertions: { ok: problems.length === 0, problems, summary },
    };
    if (problems.length) {
      console.log(`   ASSERT FAIL: the dump says a different state`);
      for (const problem of problems) console.log(`   - ${problem}`);
      console.log(`   evidence  ${run.captureDir}`);
      console.log(`   state: FAIL`);
      report.ok = false;
      report.error = 'state assertion failed';
      reports.push(report);
      worst = 2;
      continue;
    }
    if (!jsonMode) console.log(`   state     ${summary}`);
    if (opts.saveReference) {
      mkdirSync(dirname(referencePath), { recursive: true });
      const previous = existsSync(referencePath)
        ? (() => {
            const image = decodeChecked(referencePath, 'previous reference');
            const capturedImage = decodeChecked(grabPath, 'captured state');
            if (image.w !== capturedImage.w || image.h !== capturedImage.h) return 'different dimensions';
            return `PSNR ${fmtDb(psnr(image, capturedImage))} SSIM ${fmtSsim(ssim(image, capturedImage))}`;
          })()
        : 'none';
      copyFileSync(grabPath, referencePath);
      // Reference provenance stays ASCII-only: Steam titles may contain trademark
      // characters, so escape anything outside printable ASCII as \uXXXX in JSON.
      const provenance = JSON.stringify({
        state: recipe.name,
        description: recipe.description,
        tolerance: self,
        captured: report.state,
      }, null, 2).replace(/[^\x00-\x7F]/g, char => `\\u${char.charCodeAt(0).toString(16).padStart(4, '0')}`);
      writeFileSync(`${referencePath}.state.json`, `${provenance}\n`);
      console.log(`   reference written  ${referencePath} (previous: ${previous})`);
      console.log(`   state: REFERENCE UPDATED`);
      report.ok = true;
      report.referenceWritten = referencePath;
      reports.push(report);
      passed++;
      continue;
    }
    if (!existsSync(referencePath)) {
      const problem = `reference missing: ${referencePath}; create it deliberately with: bun work/ab-layers.ts --states --save-reference`;
      console.log(`   ASSERT FAIL: ${problem}`);
      console.log(`   evidence  ${run.captureDir}`);
      report.ok = false;
      report.error = problem;
      reports.push(report);
      worst = Math.max(worst, 2);
      continue;
    }
    const reference = decodeChecked(referencePath, 'state reference');
    const capture = decodeChecked(grabPath, 'captured state');
    if (reference.w !== capture.w || reference.h !== capture.h) {
      const problem = `dimension mismatch: reference ${reference.w}x${reference.h} vs capture ${capture.w}x${capture.h}`;
      console.log(`   ASSERT FAIL: ${problem}`);
      report.ok = false;
      report.error = problem;
      reports.push(report);
      worst = Math.max(worst, 2);
      continue;
    }
    let stateOk = true;
    const regions: any[] = [];
    const nameWidth = Math.max(6, ...recipe.regions.map(region => region.name.length));
    const cropWidth = Math.max(4, ...recipe.regions.map(region => region.crop.join(',').length));
    if (!jsonMode) {
      console.log(`   capture   ${grabPath}`);
      console.log(`   ${pad('region', nameWidth, true)}  ${pad('crop', cropWidth, true)}  ${pad('PSNR', 7)}  ${pad('SSIM', 7)}  gate`);
    }
    for (const region of recipe.regions) {
      const [x, y, w, h] = region.crop;
      if (x + w > reference.w || y + h > reference.h) fail(`${recipe.name}: ${region.name}: crop is outside the ${reference.w}x${reference.h} frame`);
      if (w < 8 || h < 8) fail(`${recipe.name}: ${region.name}: crop must be at least 8x8 pixels (SSIM requirement)`);
      const p = psnr(reference, capture, region.crop as Crop);
      const s = ssim(reference, capture, region.crop as Crop);
      const ok = p >= self.psnrDb && s >= self.ssim;
      regions.push({ name: region.name, crop: region.crop, psnr: Number.isFinite(p) ? +p.toFixed(4) : 'Infinity', ssim: +s.toFixed(4), minPsnr: self.psnrDb, minSsim: self.ssim, pass: ok });
      if (!jsonMode)
        console.log(`   ${pad(region.name, nameWidth, true)}  ${pad(region.crop.join(','), cropWidth, true)}  ${pad(fmtDb(p), 6)}  ${pad(fmtSsim(s), 7)}  ${ok ? 'PASS' : 'FAIL'}`);
      if (!ok) stateOk = false;
    }
    report.ok = stateOk;
    report.regions = regions;
    reports.push(report);
    if (!jsonMode) console.log(`   state: ${stateOk ? 'PASS' : 'FAIL'}`);
    if (stateOk) passed++;
    else worst = Math.max(worst, 1);
  }
  if (jsonMode) {
    console.log(JSON.stringify({
      schema: 'xmb-state-report/1',
      mode: 'named-states',
      selfBaseline: self,
      referenceNote: 'References are deterministic native renders of the same state, not firmware captures.',
      pass: passed === recipes.length && worst === 0,
      states: reports,
    }, null, 2));
  } else {
    console.log('');
    if (opts.saveReference) {
      console.log(`references updated: ${passed}/${recipes.length}${worst === 2 ? ' (state/assertion error)' : ''}`);
      console.log('commit the gates only after an intended rendering change; references are native renders, not firmware captures');
    } else {
      console.log(`states: ${passed}/${recipes.length} pass${worst === 2 ? ' (state/assertion error)' : worst === 1 ? ' (metric gate failed)' : ''}`);
      console.log('these are self-baseline regression gates, not firmware-fidelity numbers');
    }
  }
  return { code: worst, report: { schema: 'xmb-state-report/1', states: reports } };
}

async function runLayerGate(thresholds: Thresholds, opts: ReturnType<typeof parseArgs>, captureMode: boolean,
  jsonMode: boolean) {
  const referencePath = opts.ref ? resolve(opts.ref) : resolveRepo(thresholds.reference);
  let nativePath: string;
  let captureDir = '';
  let captureState: ReturnType<typeof readCaptureState> = undefined;
  if (captureMode) {
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

  if (jsonMode) {
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
    console.log(`per-layer fidelity: ${captureMode ? 'canonical capture' : 'stored baseline pair'}`);
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
  return { code: ok ? 0 : 1 };
}

let tolerance = { psnrDb: 0, ssim: 0 };
let quiet = false;

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const thresholds = parseThresholds(opts.thresholds);
  tolerance = thresholds.tolerance;
  quiet = opts.json;

  if (opts.listStates) {
    for (const state of thresholds.states ?? [])
      console.log(`${state.name.padEnd(22)} ${(state.targetCatalogueId ?? `depth ${state.expect.depth ?? 0} root`).padEnd(26)} reference ${state.reference}`);
    return;
  }
  if (opts.list) {
    for (const layer of thresholds.layers)
      console.log(`${layer.name.padEnd(18)} ${layer.crop.join(',').padEnd(18)} baseline ${layer.baseline.psnr.toFixed(2)} dB / ${layer.baseline.ssim.toFixed(4)}`);
    return;
  }
  if (opts.all && opts.json) fail('--json does not combine with --all; run --capture or --states with --json instead');
  if (opts.all) {
    console.log('== per-layer gates: stored baseline pair');
    const stored = await runLayerGate(thresholds, opts, false, false);
    console.log('\n== per-layer gates: canonical capture');
    const capture = await runLayerGate(thresholds, opts, true, false);
    console.log('\n== named states');
    const states = await runStates(thresholds, opts);
    const code = Math.max(stored.code, capture.code, states.code);
    console.log('');
    console.log(`whole set: ${code === 0 ? 'PASS' : code === 1 ? 'FAIL (metric gate)' : 'FAIL (state/runtime error)'}`);
    process.exit(code);
  }
  if (opts.states || opts.state) {
    const states = await runStates(thresholds, opts);
    process.exit(states.code);
  }
  const layers = await runLayerGate(thresholds, opts, opts.capture, opts.json);
  process.exit(layers.code);
}

try {
  await main();
} catch (error) {
  console.error(`ab-layers: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(2);
}
