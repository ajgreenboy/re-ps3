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
// One scoring-validity rule, mirroring work/parity.ts's region annotations: a layer
// annotated contentDiffers in the thresholds file has a stored reference whose content
// the build cannot reproduce from the same frame, so it is measured and printed on every
// run but does not gate. --strict gates it again, and the annotation and its mandatory
// one-line reason are printed on every run, so the informative set cannot grow silently.
// The annotation applies to a live candidate (--capture or --native); the stored baseline
// pair is two frozen artifacts and stays a pure tooling gate.
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
  // The stored reference's content cannot be reproduced from the same frame (a console
  // row set, account or service the build replaces with its own data). The layer is
  // still scored and printed, but it is informative rather than gating on a live
  // capture/--native candidate; --strict gates it again. The reason is mandatory so the
  // annotation is auditable.
  contentDiffers?: boolean;
  contentDiffersReason?: string;
};
type StateRegion = { name: string; crop: Rect };
// A navigation stage. `categoryId` (first stage only) moves the cursor to another
// column first; then the row is chosen by a catalogue id, a folder row resolved
// from the parent page's dump, an explicit zero-based row index from the top of
// the page, or a literal. `rowIndex` is the only form that needs no dump: it is
// used for pages this build writes no dump for, and the sidecar assertion is the
// oracle for the row it landed on.
type PathStage = { categoryId?: string; catalogueId?: string; folderRowTitle?: string; folderRowId?: string; rowIndex?: number };
type FolderRowExpect = { idPrefix: string; title: string; detail?: string; detailFormula?: string };
// Extra sidecar assertions: an exact scalar, or one range/prefix/contains test.
type SidecarTest = { gt?: number; ge?: number; lt?: number; le?: number; prefix?: string; contains?: string };
type SidecarFieldExpect = string | number | boolean | SidecarTest;
// A regex the capture's own log (XMB_LOG) must match, with an optional exact count.
type LogExpect = { pattern: string; count?: number };
// The Download Management page's own dump (<dump>.dl.json).
type DownloadsExpect = {
  count?: number;
  error?: string;
  message?: string;
  rows?: Array<{ idPrefix?: string; title: string; state?: string; text?: string; detail?: string }>;
};
type StateExpect = {
  category?: string;
  depth?: number;
  entryId?: string;
  entryTitle?: string;
  entryCount?: number;
  catalogueHas?: string[];
  catalogueRowIds?: string[];
  catalogueDetails?: Array<{ id: string; formula: string }>;
  folderMode?: string;
  folderTitle?: string;
  folderCountFormula?: string;
  folderRowsPrefix?: string;
  folderRowsDetailContains?: string;
  folderRows?: FolderRowExpect[];
  noStoreDump?: boolean;
  pageKind?: string;
  sidecar?: Record<string, SidecarFieldExpect>;
  log?: LogExpect[];
  downloads?: DownloadsExpect;
};
type StateRecipe = {
  name: string;
  description?: string;
  reference: string;
  dataSeed: string;
  seedPaths: string[];
  waits: number;
  settleWaits?: number;
  path?: PathStage[];
  // Extra environment for the capture. Values may contain ${repo}, which is
  // replaced with the checkout root so a fixture path stays relative here.
  env?: Record<string, string>;
  // Script tokens inserted after the navigation (and the settle waits) and
  // before the grab, e.g. TRIANGLE or NOTIFY=/VOLSET=; and tokens inserted
  // after the grab and before quit, e.g. VOLRESTORE.
  preGrab?: string[];
  postGrab?: string[];
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
  --strict                 gate content-differs layers again (strict audit mode; the
                           default scores them as informative on --capture/--native, and
                           the stored baseline pair always gates)
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

// Every --script token the native build accepts (src/main.cpp). A recipe may also
// use the payload forms (NOTIFY=, VOLSET=, VOLKEY=, type=, idle=, burst=).
const scriptKeys = new Set(['full', 'wait', 'LEFT', 'RIGHT', 'UP', 'DOWN', 'ENTER', 'ESC', 'PGUP', 'PGDN',
  'BACKSPACE', 'PREV', 'NEXT', 'SHUFFLE', 'REPEAT', 'ZOOMIN', 'ZOOMOUT', 'RESETVIEW', 'ROTATELEFT', 'ROTATERIGHT',
  'REFRESH', 'TRIANGLE', 'ps', 'pshold', 'VOLUP', 'VOLDOWN', 'MUTE', 'VOLRESTORE']);
function isScriptToken(token: string) {
  return scriptKeys.has(token) || /^size=\d+x\d+$/.test(token) || /^(NOTIFY|VOLSET|VOLKEY|type|idle|burst)=/.test(token);
}
function isSidecarTest(value: unknown): value is SidecarTest {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const keys = Object.keys(value);
  if (keys.length !== 1) return false;
  const key = keys[0];
  const inner = (value as Record<string, unknown>)[key];
  if (key === 'prefix' || key === 'contains') return typeof inner === 'string';
  if (['gt', 'ge', 'lt', 'le'].includes(key)) return typeof inner === 'number' && Number.isFinite(inner);
  return false;
}
function isSidecarFieldExpect(value: unknown) {
  return typeof value === 'string' || typeof value === 'boolean' ||
    (typeof value === 'number' && Number.isFinite(value)) || isSidecarTest(value);
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
    if (layer.contentDiffers !== undefined && typeof layer.contentDiffers !== 'boolean')
      fail(`${path}: ${layer.name}: contentDiffers must be a boolean`);
    if (layer.contentDiffers === true &&
      (typeof layer.contentDiffersReason !== 'string' || !layer.contentDiffersReason.trim()))
      fail(`${path}: ${layer.name}: contentDiffers needs a one-line contentDiffersReason`);
    if (layer.contentDiffers !== true && layer.contentDiffersReason !== undefined)
      fail(`${path}: ${layer.name}: contentDiffersReason is only valid with contentDiffers: true`);
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
      if (state.path !== undefined) {
        if (!Array.isArray(state.path) || !state.path.length)
          fail(`${path}: ${state.name}: path must be a non-empty array when present`);
        for (const [stageIndex, stage] of state.path.entries()) {
          if (!stage || typeof stage !== 'object')
            fail(`${path}: ${state.name}: path[${stageIndex}] is not an object`);
          const keys = [stage.catalogueId, stage.folderRowTitle, stage.folderRowId,
            stage.rowIndex === undefined ? undefined : String(stage.rowIndex)].filter(value => value !== undefined).length;
          if (keys !== 1)
            fail(`${path}: ${state.name}: path[${stageIndex}] must define exactly one of catalogueId, folderRowTitle, folderRowId, rowIndex`);
          for (const key of ['catalogueId', 'folderRowTitle', 'folderRowId'] as const)
            if (stage[key] !== undefined && (typeof stage[key] !== 'string' || !stage[key]))
              fail(`${path}: ${state.name}: path[${stageIndex}].${key} must be a non-empty string`);
          if (stage.rowIndex !== undefined && (!Number.isSafeInteger(stage.rowIndex) || stage.rowIndex < 0 || stage.rowIndex > 5000))
            fail(`${path}: ${state.name}: path[${stageIndex}].rowIndex must be an integer from 0 through 5000`);
          if (stage.categoryId !== undefined && (typeof stage.categoryId !== 'string' || !stage.categoryId))
            fail(`${path}: ${state.name}: path[${stageIndex}].categoryId must be a non-empty string`);
          if (stageIndex > 0 && (stage.catalogueId !== undefined || stage.categoryId !== undefined))
            fail(`${path}: ${state.name}: path[${stageIndex}] must target a row inside the page opened by the previous stage`);
        }
        if (state.path[0].catalogueId === undefined && state.path[0].rowIndex === undefined)
          fail(`${path}: ${state.name}: path[0] must select a catalogue entry (catalogueId) or a rowIndex`);
      }
      if (state.env !== undefined) {
        if (!state.env || typeof state.env !== 'object' || Array.isArray(state.env))
          fail(`${path}: ${state.name}: env must be an object`);
        for (const [key, value] of Object.entries(state.env)) {
          if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key))
            fail(`${path}: ${state.name}: env key ${JSON.stringify(key)} is not a valid environment name`);
          if (typeof value !== 'string')
            fail(`${path}: ${state.name}: env.${key} must be a string`);
        }
      }
      for (const listKey of ['preGrab', 'postGrab'] as const) {
        const tokens = state[listKey];
        if (tokens === undefined) continue;
        if (!Array.isArray(tokens) || tokens.some(token => typeof token !== 'string' || !token))
          fail(`${path}: ${state.name}: ${listKey} must be a string array`);
        for (const token of tokens)
          if (!isScriptToken(token))
            fail(`${path}: ${state.name}: ${listKey} token ${JSON.stringify(token)} is not a script token `
              + '(grab and quit are the harness\'s own, so they are rejected)');
      }
      if (!state.expect || typeof state.expect !== 'object' || Array.isArray(state.expect))
        fail(`${path}: ${state.name}: expect must be an object`);
      const expect = state.expect;
      if (expect.catalogueRowIds !== undefined &&
        (!Array.isArray(expect.catalogueRowIds) || expect.catalogueRowIds.some(id => typeof id !== 'string' || !id)))
        fail(`${path}: ${state.name}: expect.catalogueRowIds must be a string array`);
      if (expect.catalogueDetails !== undefined) {
        if (!Array.isArray(expect.catalogueDetails))
          fail(`${path}: ${state.name}: expect.catalogueDetails must be an array`);
        for (const detail of expect.catalogueDetails)
          if (!detail || typeof detail.id !== 'string' || !detail.id || typeof detail.formula !== 'string' || !detail.formula)
            fail(`${path}: ${state.name}: expect.catalogueDetails entries must be {id,formula}`);
      }
      if (expect.folderRows !== undefined) {
        if (!Array.isArray(expect.folderRows) || !expect.folderRows.length)
          fail(`${path}: ${state.name}: expect.folderRows must be a non-empty array`);
        for (const row of expect.folderRows) {
          if (!row || typeof row.idPrefix !== 'string' || !row.idPrefix || typeof row.title !== 'string')
            fail(`${path}: ${state.name}: expect.folderRows entries must be {idPrefix,title,detail?}`);
          if (row.detail !== undefined && typeof row.detail !== 'string')
            fail(`${path}: ${state.name}: expect.folderRows.detail must be a string`);
          if (row.detailFormula !== undefined && typeof row.detailFormula !== 'string')
            fail(`${path}: ${state.name}: expect.folderRows.detailFormula must be a string`);
          if (row.detail !== undefined && row.detailFormula !== undefined)
            fail(`${path}: ${state.name}: expect.folderRows entry cannot set both detail and detailFormula`);
        }
      }
      if (expect.pageKind !== undefined && (typeof expect.pageKind !== 'string' || !expect.pageKind))
        fail(`${path}: ${state.name}: expect.pageKind must be a non-empty string`);
      if (expect.sidecar !== undefined) {
        if (!expect.sidecar || typeof expect.sidecar !== 'object' || Array.isArray(expect.sidecar))
          fail(`${path}: ${state.name}: expect.sidecar must be an object`);
        for (const [field, value] of Object.entries(expect.sidecar)) {
          if (!/^[A-Za-z][A-Za-z0-9]*$/.test(field))
            fail(`${path}: ${state.name}: expect.sidecar field ${JSON.stringify(field)} is not a sidecar key`);
          if (!isSidecarFieldExpect(value))
            fail(`${path}: ${state.name}: expect.sidecar.${field} must be a string, number, boolean, `
              + 'or one of {gt,ge,lt,le,prefix,contains}');
        }
      }
      if (expect.log !== undefined) {
        if (!Array.isArray(expect.log) || !expect.log.length)
          fail(`${path}: ${state.name}: expect.log must be a non-empty array`);
        for (const entry of expect.log) {
          if (!entry || typeof entry !== 'object' || typeof entry.pattern !== 'string' || !entry.pattern)
            fail(`${path}: ${state.name}: expect.log entries must be {pattern,count?}`);
          try { new RegExp(entry.pattern); } catch (error) {
            fail(`${path}: ${state.name}: expect.log pattern ${JSON.stringify(entry.pattern)} is not a valid regex: `
              + (error instanceof Error ? error.message : String(error)));
          }
          if (entry.count !== undefined && (!Number.isSafeInteger(entry.count) || entry.count < 1 || entry.count > 10000))
            fail(`${path}: ${state.name}: expect.log.count must be a positive integer`);
        }
      }
      if (expect.downloads !== undefined) {
        const downloads = expect.downloads;
        if (!downloads || typeof downloads !== 'object' || Array.isArray(downloads))
          fail(`${path}: ${state.name}: expect.downloads must be an object`);
        for (const key of ['count'] as const)
          if (downloads[key] !== undefined && (!Number.isSafeInteger(downloads[key]) || downloads[key] < 0))
            fail(`${path}: ${state.name}: expect.downloads.${key} must be a non-negative integer`);
        for (const key of ['error', 'message'] as const)
          if (downloads[key] !== undefined && typeof downloads[key] !== 'string')
            fail(`${path}: ${state.name}: expect.downloads.${key} must be a string`);
        if (downloads.rows !== undefined) {
          if (!Array.isArray(downloads.rows) || !downloads.rows.length)
            fail(`${path}: ${state.name}: expect.downloads.rows must be a non-empty array`);
          for (const row of downloads.rows) {
            if (!row || typeof row !== 'object' || typeof row.title !== 'string' || !row.title)
              fail(`${path}: ${state.name}: expect.downloads.rows entries must be {title,...}`);
            for (const key of ['idPrefix', 'state', 'text', 'detail'] as const)
              if (row[key] !== undefined && typeof row[key] !== 'string')
                fail(`${path}: ${state.name}: expect.downloads.rows.${key} must be a string`);
          }
        }
      }
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
    strict: false,
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
    if (a === '--strict') { opts.strict = true; continue; }
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
  if (opts.saveReference && opts.json) fail('--json does not combine with --save-reference');
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

// `informative` is decided by the caller: it is true only for a layer annotated
// contentDiffers that is being scored against a live candidate and outside --strict. An
// informative layer is still measured and its numbers printed; it simply cannot fail the
// run, which is why `pass` stays the gate outcome rather than the raw budget test.
function scoreLayer(layer: Layer, ref: PngImage, native: PngImage, captureMode: boolean, informative: boolean) {
  const baseline = captureMode && layer.captureBaseline ? layer.captureBaseline : layer.baseline;
  const p = psnr(ref, native, layer.crop as Crop);
  const s = ssim(ref, native, layer.crop as Crop);
  const minPsnr = baseline.psnr - tolerance.psnrDb;
  const minSsim = baseline.ssim - tolerance.ssim;
  const withinBudget = p >= minPsnr && s >= minSsim;
  return { layer, baseline, p, s, minPsnr, minSsim, withinBudget, informative, pass: informative || withinBudget };
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
  const messagePath = join(captureDir, 'native.messages.log');
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
    // The Windows build has no console, so the app's own qInfo/qWarning lines go to
    // XMB_LOG. Recipe log assertions read that file; the grep is unambiguous because
    // every capture gets its own directory.
    XMB_LOG: messagePath,
  } as Record<string, string | undefined>;
  for (const [key, value] of Object.entries(recipe.env ?? {})) env[key] = value.replaceAll('${repo}', repo);
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

// The catalogue dump lists every column in the shell's own order, so a recipe that
// starts in another column can resolve its row (and the number of LEFT/RIGHT moves)
// from the same discovery capture instead of hardcoded offsets.
type Catalogue = { categories: string[]; rows: Map<string, Map<string, number>> };
function catalogueFromDump(dump: any): Catalogue {
  const categories: string[] = [];
  const rows = new Map<string, Map<string, number>>();
  for (const category of (Array.isArray(dump?.categories) ? dump.categories : [])) {
    if (!category || typeof category.id !== 'string') continue;
    categories.push(category.id);
    const indices = new Map<string, number>();
    (Array.isArray(category.entries) ? category.entries : []).forEach((entry: any, index: number) => {
      if (entry && typeof entry.id === 'string' && !indices.has(entry.id)) indices.set(entry.id, index);
    });
    rows.set(category.id, indices);
  }
  return { categories, rows };
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

function expectedDetailFormula(recipe: StateRecipe, formula: string, dump: any) {
  const owned = Number(dump.owned);
  const installed = Number(dump.installedSteam);
  const family = familySharedCount(recipe);
  if (formula === 'ownedGames') return Number.isFinite(owned) ? String(owned) : undefined;
  if (formula === 'notInstalled')
    return Number.isFinite(owned) && Number.isFinite(installed) ? String(owned - installed + family) : undefined;
  if (formula === 'familyShared') return String(family);
  return undefined;
}

function sidecarTest(field: string, value: SidecarFieldExpect, actual: unknown, problems: string[]) {
  const got = actual as any;
  if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') {
    if (got !== value) problems.push(`sidecar ${field}: expected ${JSON.stringify(value)}, got ${JSON.stringify(got)}`);
    return;
  }
  const [operator, expected] = Object.entries(value)[0];
  if (operator === 'prefix') {
    if (typeof got !== 'string' || !got.startsWith(expected as string))
      problems.push(`sidecar ${field}: expected a value starting with ${JSON.stringify(expected)}, got ${JSON.stringify(got)}`);
    return;
  }
  if (operator === 'contains') {
    if (typeof got !== 'string' || !got.includes(expected as string))
      problems.push(`sidecar ${field}: expected a value containing ${JSON.stringify(expected)}, got ${JSON.stringify(got)}`);
    return;
  }
  if (typeof got !== 'number' || !Number.isFinite(got)) {
    problems.push(`sidecar ${field}: expected a number ${operator} ${expected}, got ${JSON.stringify(got)}`);
    return;
  }
  const limit = expected as number;
  const ok = operator === 'gt' ? got > limit : operator === 'ge' ? got >= limit
    : operator === 'lt' ? got < limit : got <= limit;
  if (!ok) problems.push(`sidecar ${field}: expected ${operator} ${limit}, got ${got}`);
}

function countPattern(text: string, pattern: RegExp) {
  let count = 0;
  for (const line of text.split(/\r?\n/)) if (pattern.test(line)) count++;
  return count;
}

function checkStateExpect(recipe: StateRecipe, sidecar: any, dump: any, folder: any, store: any, dl: any,
  messages: string) {
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
  // The surfaces that are not a folder or a catalogue row (a dialog, the sign-in
  // page, the options panel, a notification, a subtitle cue) are asserted through
  // the grab sidecar's own fields, which the app writes at the grab instant.
  if (expect.pageKind !== undefined && sidecar.pageKind !== expect.pageKind)
    problems.push(`pageKind: expected ${JSON.stringify(expect.pageKind)}, got ${JSON.stringify(sidecar.pageKind)}`);
  for (const [field, value] of Object.entries(expect.sidecar ?? {}))
    sidecarTest(field, value, sidecar[field], problems);
  for (const entry of expect.log ?? []) {
    const pattern = new RegExp(entry.pattern);
    const seen = countPattern(messages, pattern);
    if (entry.count !== undefined) {
      if (seen !== entry.count)
        problems.push(`log ${JSON.stringify(entry.pattern)}: expected ${entry.count} matching line(s), got ${seen}`);
    } else if (!seen)
      problems.push(`log ${JSON.stringify(entry.pattern)}: no matching line`);
  }
  const entries = gameEntries(dump);
  for (const id of expect.catalogueHas ?? [])
    if (!entries.some(entry => entry.id === id))
      problems.push(`catalogue Game column has no entry ${JSON.stringify(id)}`);
  if (expect.catalogueRowIds) {
    const got = entries.map(entry => entry.id);
    const expected = expect.catalogueRowIds;
    if (got.length !== expected.length || expected.some((id, index) => id !== got[index]))
      problems.push(`catalogue Game column rows: expected [${expected.join(', ')}], got [${got.join(', ')}]`);
  }
  for (const expectation of expect.catalogueDetails ?? []) {
    const row = entries.find(entry => entry.id === expectation.id);
    if (!row) {
      problems.push(`catalogue Game column has no entry ${JSON.stringify(expectation.id)}`);
      continue;
    }
    const expected = expectedDetailFormula(recipe, expectation.formula, dump);
    if (expected === undefined) problems.push(`catalogue ${expectation.id}: unknown detail formula ${JSON.stringify(expectation.formula)}`);
    else if (String(row.detail) !== expected)
      problems.push(`catalogue ${expectation.id} detail: expected ${expected}, got ${JSON.stringify(row.detail)}`);
  }
  const installed = Number(dump.installedSteam);
  const owned = Number(dump.owned);
  const family = familySharedCount(recipe);
  const expectedFolderCount = Number.isFinite(owned) && Number.isFinite(installed) ? owned - installed + family : NaN;
  if (expect.folderMode !== undefined) {
    if (!folder) problems.push('expected the folder-open dump <dump>.folder.json but no folder opened');
    else {
      if (folder.folder !== expect.folderMode)
        problems.push(`folder dump mode: expected ${JSON.stringify(expect.folderMode)}, got ${JSON.stringify(folder.folder)}`);
      if (expect.folderTitle !== undefined && folder.title !== expect.folderTitle)
        problems.push(`folder dump title: expected ${JSON.stringify(expect.folderTitle)}, got ${JSON.stringify(folder.title)}`);
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
        if (expect.folderRows) {
          if (folder.rows.length !== expect.folderRows.length)
            problems.push(`folder row list: expected ${expect.folderRows.length} rows, got ${folder.rows.length}`);
          else for (const [index, expected] of expect.folderRows.entries()) {
            const row = folder.rows[index];
            const where = `folder row ${index}`;
            if (!String(row.id).startsWith(expected.idPrefix))
              problems.push(`${where} id: expected prefix ${JSON.stringify(expected.idPrefix)}, got ${JSON.stringify(row.id)}`);
            if (row.title !== expected.title)
              problems.push(`${where} title: expected ${JSON.stringify(expected.title)}, got ${JSON.stringify(row.title)}`);
            let expectedDetail = expected.detail;
            if (expected.detailFormula !== undefined)
              expectedDetail = expectedDetailFormula(recipe, expected.detailFormula, dump);
            if (expectedDetail !== undefined && String(row.detail) !== expectedDetail)
              problems.push(`${where} (${expected.title}) detail: expected ${JSON.stringify(expectedDetail)}, got ${JSON.stringify(row.detail)}`);
          }
        }
      }
      if (sidecar.entryCount !== folder.count)
        problems.push(`sidecar entryCount ${sidecar.entryCount} != folder rows ${folder.count}`);
    }
  }
  if (expect.noStoreDump && store)
    problems.push('a <dump>.store.json exists: a store request ran, so this state is not network-free and may not be deterministic');
  // The Download Management page writes its own <dump>.dl.json, including its empty
  // and error cases, so the page is checkable without reading pixels.
  if (expect.downloads) {
    const wanted = expect.downloads;
    if (!dl) problems.push('expected the download page dump <dump>.dl.json but the page did not open');
    else {
      if (wanted.count !== undefined && dl.count !== wanted.count)
        problems.push(`download page count: expected ${wanted.count}, got ${dl.count}`);
      if (wanted.error !== undefined && String(dl.error ?? '') !== wanted.error)
        problems.push(`download page error: expected ${JSON.stringify(wanted.error)}, got ${JSON.stringify(dl.error)}`);
      if (wanted.message !== undefined && String(dl.message ?? '') !== wanted.message)
        problems.push(`download page message: expected ${JSON.stringify(wanted.message)}, got ${JSON.stringify(dl.message)}`);
      if (wanted.rows) {
        const rows = Array.isArray(dl.rows) ? dl.rows : [];
        if (rows.length !== wanted.rows.length)
          problems.push(`download page rows: expected ${wanted.rows.length}, got ${rows.length}`
            + (rows.length ? ` [${rows.map((row: any) => row.title).join(', ')}]` : ''));
        else for (const [index, expected] of wanted.rows.entries()) {
          const row = rows[index];
          const where = `download row ${index}`;
          if (expected.idPrefix !== undefined && !String(row.id).startsWith(expected.idPrefix))
            problems.push(`${where} id: expected prefix ${JSON.stringify(expected.idPrefix)}, got ${JSON.stringify(row.id)}`);
          if (row.title !== expected.title)
            problems.push(`${where} title: expected ${JSON.stringify(expected.title)}, got ${JSON.stringify(row.title)}`);
          for (const key of ['state', 'text', 'detail'] as const)
            if (expected[key] !== undefined && String(row[key] ?? '') !== expected[key])
              problems.push(`${where} ${key}: expected ${JSON.stringify(expected[key])}, got ${JSON.stringify(row[key])}`);
        }
      }
      if (sidecar.entryCount !== dl.count && wanted.rows)
        problems.push(`sidecar entryCount ${sidecar.entryCount} != download page rows ${dl.count}`);
    }
  }
  const summary = [
    `depth ${sidecar.depth}`,
    `category ${sidecar.category}`,
    `selection ${sidecar.selection}`,
    `rows ${sidecar.entryCount}`,
  ];
  if (sidecar.pageKind) summary.push(`pageKind ${sidecar.pageKind}`);
  if (folder) summary.push(`folder ${folder.count}`);
  return { problems, summary: summary.join(', ') };
}

function findState(thresholds: Thresholds, name: string) {
  const states = thresholds.states ?? [];
  const recipe = states.find(state => state.name === name);
  if (!recipe) fail(`unknown named state ${JSON.stringify(name)}; known: ${states.map(state => state.name).join(', ') || 'none'}`);
  return recipe;
}

// How many UP taps are spent clamping a freshly entered column to its first row.
// Every column is shorter than this (the tallest is Settings with 16 rows) and
// vertical() clamps rather than wraps, so the count only has to exceed the tallest
// column; the state assertion catches a column that outgrows it.
const clampUp = 18;

// One resolved navigation step: which column (when it changes) and which row index
// on that page. `categoryId` is only set for a stage that starts in another column.
type ResolvedStage = { categoryId?: string; index: number; label: string };

// Builds the key tokens that walk a resolved path from the Game column root. The
// FIRST stage without a categoryId reproduces the original Game-root recipes
// exactly (DOWN x index, ENTER), so their stored references stay valid.
function pathTokens(recipe: StateRecipe, stages: ResolvedStage[], catalogue: Catalogue | undefined) {
  const tokens: string[] = [];
  let current = 'game';
  for (const [stageIndex, stage] of stages.entries()) {
    if (stage.categoryId && stage.categoryId !== current) {
      if (current !== 'game')
        fail(`${recipe.name}: path moves from ${JSON.stringify(current)} to ${JSON.stringify(stage.categoryId)}; `
          + 'the run starts in the Game column, so only a first stage may name another column');
      const from = catalogue?.categories.indexOf('game') ?? -1;
      const to = catalogue?.categories.indexOf(stage.categoryId);
      if (from < 0 || to === undefined || to < 0)
        fail(`${recipe.name}: column ${JSON.stringify(stage.categoryId)} is not in the catalogue dump`);
      const delta = to - from;
      // The shell starts in the Game column (the canonical capture asserts category=game),
      // so the move is the column-index difference; horizontal() clamps at the ends.
      tokens.push(...Array(Math.abs(delta)).fill(delta > 0 ? 'RIGHT' : 'LEFT'), 'wait', 'wait');
      tokens.push(...Array(clampUp).fill('UP'), 'wait');
      current = stage.categoryId;
    }
    tokens.push(...Array(stage.index).fill('DOWN'), 'ENTER');
    if (stageIndex < stages.length - 1) tokens.push('wait', 'wait');
  }
  return tokens;
}

// Walks a recipe's navigation path. A catalogue stage is resolved from the
// discovery dump of its column; each later stage is either an explicit zero-based
// rowIndex (no dump exists for that page) or a row inside the page opened by the
// previous stage, resolved by opening that page and reading its <dump>.folder.json.
async function resolveStatePath(thresholds: Thresholds, opts: ReturnType<typeof parseArgs>, recipe: StateRecipe,
  catalogue: Catalogue | undefined): Promise<ResolvedStage[]> {
  const path = recipe.path ?? [];
  const stages: ResolvedStage[] = [];
  if (!path.length) return stages;
  const first = path[0];
  if (first.rowIndex !== undefined) {
    stages.push({ categoryId: first.categoryId, index: first.rowIndex, label: `row ${first.rowIndex}` });
  } else {
    const category = first.categoryId ?? 'game';
    const rows = catalogue?.rows.get(category);
    const rootIndex = rows?.get(first.catalogueId!);
    if (rootIndex === undefined)
      fail(`${recipe.name}: catalogue has no entry ${JSON.stringify(first.catalogueId)} in column ${JSON.stringify(category)}; `
        + `rows: [${rows ? [...rows.keys()].join(', ') : ''}]`);
    stages.push({ categoryId: first.categoryId, index: rootIndex, label: `catalogue:${first.catalogueId}` });
  }
  for (let stageIndex = 1; stageIndex < path.length; stageIndex++) {
    const stage = path[stageIndex];
    if (stage.rowIndex !== undefined) {
      stages.push({ index: stage.rowIndex, label: `row ${stage.rowIndex}` });
      continue;
    }
    // A probe waits longer than a plain capture: it must navigate to a row that only
    // exists once the asynchronous Steam/app content scan has rebuilt the column, and
    // the scan lands at 1.5-2 s on a quiet machine and later under load.
    const tokens = ['full', ...Array(recipe.waits).fill('wait'), ...pathTokens(recipe, stages, catalogue),
      'wait', 'quit'];
    // One probe can land before the page it opens has finished building - a script
    // token is delivered when the app's event loop is idle, which on a busy machine
    // can be well after the 300 ms interval - so a probe with no folder dump is
    // retried once before the recipe is reported as unresolvable.
    let page: any;
    let run: StateProcess | undefined;
    for (let attempt = 1; attempt <= 2; attempt++) {
      run = await runStateProcess(thresholds, opts, recipe, tokens);
      const folderPath = `${run.dumpPath}.folder.json`;
      if (existsSync(folderPath)) { page = readJsonFile(folderPath); break; }
      if (attempt === 1 && !quiet) console.log(`   probe ${stageIndex} produced no folder dump; retrying once`);
    }
    if (!page) fail(`${recipe.name}: could not resolve path stage ${stageIndex}: no folder dump; evidence: ${run?.captureDir}`);
    const rows = Array.isArray(page.rows) ? page.rows : [];
    const index = stage.folderRowId
      ? rows.findIndex((row: any) => row.id === stage.folderRowId)
      : rows.findIndex((row: any) => row.title === stage.folderRowTitle);
    if (index < 0)
      fail(`${recipe.name}: page ${JSON.stringify(page.title)} (mode ${JSON.stringify(page.folder)}) has no row `
        + `${JSON.stringify(stage.folderRowTitle ?? stage.folderRowId)}; rows: [${rows.map((row: any) => row.title).join(', ')}]; evidence: ${run.captureDir}`);
    stages.push({ index, label: `row:${stage.folderRowTitle ?? stage.folderRowId}` });
  }
  return stages;
}

async function runStates(thresholds: Thresholds, opts: ReturnType<typeof parseArgs>) {
  const self = thresholds.stateSelfBaseline;
  if (!self) fail(`${opts.thresholds}: named states require stateSelfBaseline`);
  const recipes = opts.state ? [findState(thresholds, opts.state)] : (thresholds.states ?? []);
  if (!recipes.length) fail(`${opts.thresholds}: defines no named states`);
  const jsonMode = opts.json && !opts.all;

  // One discovery capture per distinct data seed + environment: the catalogue dump
  // depends on both (apps.json adds the custom rows, XMB_STEAM_ROOT decides the Game
  // column), so a recipe is resolved against a dump captured with its own inputs.
  const pathRecipes = recipes.filter(recipe => (recipe.path?.length ?? 0) > 0);
  const catalogues = new Map<string, Catalogue>();
  const signature = (recipe: StateRecipe) => `${recipe.dataSeed}|${JSON.stringify(recipe.env ?? {})}`;
  const catalogueFor = async (recipe: StateRecipe) => {
    const key = signature(recipe);
    const cached = catalogues.get(key);
    if (cached) return cached;
    const discoveryTokens = ['full', ...Array(recipe.waits + 12).fill('wait'), 'quit'];
    if (!quiet) console.log(`discovery: catalogue dump from ${recipe.dataSeed}`);
    const run = await runStateProcess(thresholds, opts, recipe, discoveryTokens);
    const dump = readJsonFile(run.dumpPath);
    const catalogue = catalogueFromDump(dump);
    catalogues.set(key, catalogue);
    if (!quiet) {
      console.log(`  Game rows ${gameEntries(dump).length} (installedSteam ${dump.installedSteam}, owned ${dump.owned})`);
      console.log(`  columns   ${catalogue.categories.join(', ')}`);
      console.log(`  evidence  ${run.captureDir}`);
    }
    return catalogue;
  };
  const pathStages = new Map<string, ResolvedStage[]>();
  for (const recipe of pathRecipes) {
    const catalogue = await catalogueFor(recipe);
    const stages = await resolveStatePath(thresholds, opts, recipe, catalogue);
    pathStages.set(recipe.name, stages);
    if (!quiet && stages.length)
      console.log(`  route     ${recipe.name}: ${stages.map(stage => stage.label + `[${stage.index}]`).join(' > ')}`);
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
    const stages = pathStages.get(recipe.name) ?? [];
    const recipeCatalogue = catalogues.get(signature(recipe));
    const tokens = ['full', ...Array(recipe.waits).fill('wait'), ...pathTokens(recipe, stages, recipeCatalogue),
      ...Array(recipe.settleWaits ?? 3).fill('wait'),
      ...(recipe.preGrab ?? []), '{grab}', ...(recipe.postGrab ?? []), 'quit'];
    // The grab token needs the absolute capture path: replace the placeholder.
    if (!jsonMode) console.log(`\n-- ${recipe.name}`);
    if (recipe.description && !jsonMode) console.log(`   ${recipe.description}`);
    // A stray host key event can move the selection while a capture is running, so an
    // assertion mismatch retries the capture once before it is reported as a failure.
    let run: StateProcess | undefined;
    let sidecar: any;
    let dump: any;
    let folder: any;
    let store: any;
    let dl: any;
    let messages = '';
    let problems: string[] = [];
    let summary = '';
    for (let attempt = 1; attempt <= 2; attempt++) {
      run = await runStateProcess(thresholds, opts, recipe, tokens);
      const sidecarPath = `${run.grabPath}.json`;
      sidecar = existsSync(sidecarPath) ? readJsonFile(sidecarPath) : undefined;
      dump = existsSync(run.dumpPath) ? readJsonFile(run.dumpPath) : undefined;
      const folderPath = `${run.dumpPath}.folder.json`;
      const storePath = `${run.dumpPath}.store.json`;
      const dlPath = `${run.dumpPath}.dl.json`;
      folder = existsSync(folderPath) ? readJsonFile(folderPath) : undefined;
      store = existsSync(storePath) ? readJsonFile(storePath) : undefined;
      dl = existsSync(dlPath) ? readJsonFile(dlPath) : undefined;
      messages = existsSync(join(run.captureDir, 'native.messages.log'))
        ? readFileSync(join(run.captureDir, 'native.messages.log'), 'utf8') : '';
      if (!sidecar) fail(`${recipe.name}: no state sidecar ${sidecarPath}; capture evidence: ${run.captureDir}`);
      if (!dump) fail(`${recipe.name}: no catalogue dump ${run.dumpPath}; capture evidence: ${run.captureDir}`);
      const checked = checkStateExpect(recipe, sidecar, dump, folder, store, dl, messages);
      problems = checked.problems;
      summary = checked.summary;
      if (!problems.length || attempt === 2) break;
      if (!quiet)
        console.log(`   state mismatch on attempt ${attempt}; retrying the capture once: ${problems.join('; ')}`);
    }
    const grabPath = run!.grabPath;
    const referencePath = resolveRepo(recipe.reference);
    const report: any = {
      name: recipe.name,
      captureDir: run.captureDir,
      grab: grabPath,
      reference: referencePath,
      state: { category: sidecar.category, depth: sidecar.depth, selection: sidecar.selection, entryId: sidecar.entryId, entryTitle: sidecar.entryTitle, entryCount: sidecar.entryCount, pageKind: sidecar.pageKind },
      assertions: { ok: problems.length === 0, problems, summary },
    };
    if (problems.length) {
      if (!jsonMode) {
        console.log(`   ASSERT FAIL: the dump says a different state`);
        for (const problem of problems) console.log(`   - ${problem}`);
        console.log(`   evidence  ${run.captureDir}`);
        console.log(`   state: FAIL`);
      }
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
      if (!jsonMode) {
        console.log(`   ASSERT FAIL: ${problem}`);
        console.log(`   evidence  ${run.captureDir}`);
      }
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
      if (!jsonMode) console.log(`   ASSERT FAIL: ${problem}`);
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

  // A textless build can score BETTER than a text-bearing one: a blank clock region is closer to
  // the reference than rendered glyphs in the wrong font. That happened on 2026-10-28 and silently
  // moved the baselines, so a capture must prove it drew text before its numbers count. The clock
  // crop is the cheapest oracle (the app always renders the clock there).
  if (captureMode) {
    const crop = [1570, 80, 260, 50] as const;
    let bright = 0;
    for (let y = crop[1]; y < crop[1] + crop[3]; y++) {
      for (let x = crop[0]; x < crop[0] + crop[2]; x++) {
        const i = (y * native.w + x) * 4;
        const luma = 0.2126 * native.rgba[i] + 0.7152 * native.rgba[i + 1] + 0.0722 * native.rgba[i + 2];
        if (luma > 140) bright++;
      }
    }
    const minimum = 900; // a text-bearing build measures ~1,677; a textless one ~302
    if (bright < minimum) {
      fail(`captured frame renders no clock text (${bright} bright pixels in the clock crop, minimum ${minimum}) - ` +
        'the text renderer is broken, so this gate would score a blank clock as an improvement');
    }
    console.log(`  text presence: ${bright} bright pixels in the clock crop (minimum ${minimum}) PASS`);
  }

  // A content-differs annotation only relaxes scoring against a LIVE candidate: the stored
  // baseline pair is two frozen artifacts, kept byte-for-byte as the tooling gate its note
  // describes, so it cannot become informative.
  const liveCandidate = captureMode || !!opts.native;
  const rows = thresholds.layers.map(layer => {
    checkCropInImage(layer, ref);
    const informative = liveCandidate && !opts.strict && layer.contentDiffers === true;
    return scoreLayer(layer, ref, native, captureMode, informative);
  });
  const annotated = rows.filter(row => row.layer.contentDiffers === true);
  const informativeRows = rows.filter(row => row.informative);
  const failed = rows.filter(row => !row.pass);
  const ok = failed.length === 0;

  if (jsonMode) {
    console.log(JSON.stringify({
      schema: 'xmb-per-layer-report/1',
      mode: captureMode ? 'capture' : 'stored-pair',
      strict: opts.strict,
      reference: referencePath,
      native: nativePath,
      captureDir: captureDir || undefined,
      captureState,
      width: ref.w,
      height: ref.h,
      tolerance: thresholds.tolerance,
      pass: ok,
      annotations: annotated.map(row => ({
        layer: row.layer.name,
        reason: row.layer.contentDiffersReason,
        informative: row.informative,
      })),
      informative: informativeRows.map(row => row.layer.name),
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
        withinBudget: row.withinBudget,
        contentDiffers: row.layer.contentDiffers === true,
        contentDiffersReason: row.layer.contentDiffersReason,
        informative: row.informative,
        gating: !row.informative,
        status: row.informative ? 'informative' : row.withinBudget ? 'pass' : 'fail',
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
    if (annotated.length) {
      const scope = !liveCandidate ? 'stored baseline pair: still gated'
        : opts.strict ? 'STRICT: gated again'
        : 'informative: not gating, --strict gates it';
      console.log(`  content    ${annotated.length} layer(s) annotated contentDiffers (${scope})`);
    }
    console.log('');
    const nameWidth = Math.max(5, ...thresholds.layers.map(l => l.name.length));
    const cropWidth = Math.max(4, ...thresholds.layers.map(l => l.crop.join(',').length));
    const header = `${pad('layer', nameWidth, true)}  ${pad('crop', cropWidth, true)}  ${pad('PSNR', 7)}  ${pad('base', 7)}  ${pad('delta', 6)}  ${pad('SSIM', 6)}  ${pad('base', 6)}  ${pad('delta', 7)}  gate`;
    console.log(header);
    console.log(`${'-'.repeat(nameWidth)}  ${'-'.repeat(cropWidth)}  ${'-'.repeat(7)}  ${'-'.repeat(7)}  ${'-'.repeat(6)}  ${'-'.repeat(6)}  ${'-'.repeat(6)}  ${'-'.repeat(7)}  ${'-'.repeat(4)}`);
    for (const row of rows) {
      // The gate column says why a result is (or is not) gating; the numbers themselves are
      // never altered by the annotation.
      const gate = row.informative
        ? (row.withinBudget ? 'PASS (informative)' : 'FAIL (informative)')
        : row.layer.contentDiffers === true && liveCandidate && opts.strict
          ? (row.withinBudget ? 'PASS (strict: gated)' : 'FAIL (strict: gated)')
          : (row.withinBudget ? 'PASS' : 'FAIL');
      console.log([
        pad(row.layer.name, nameWidth, true),
        pad(row.layer.crop.join(','), cropWidth, true),
        pad(fmtDb(row.p), 7),
        pad(row.baseline.psnr.toFixed(2), 7),
        pad(fmtDelta(row.p - row.baseline.psnr), 6),
        pad(fmtSsim(row.s), 6),
        pad(row.baseline.ssim.toFixed(4), 6),
        pad(fmtDeltaSsim(row.s - row.baseline.ssim), 7),
        gate,
      ].join('  '));
    }
    console.log('');
    if (informativeRows.length) {
      const passCount = rows.filter(row => !row.informative && row.withinBudget).length;
      const informativeNote = `${informativeRows.length} informative (content differs)`;
      console.log(`${ok ? 'PASS' : 'FAIL'}: ${rows.length}/${rows.length} layers - ${passCount} pass`
        + `${failed.length ? `, ${failed.length} fail` : ''}, ${informativeNote}`);
      for (const row of informativeRows.filter(candidate => !candidate.withinBudget))
        console.log(`  informative ${row.layer.name} is outside its budget `
          + `(${fmtDb(row.p)} dB / ${fmtSsim(row.s)} vs floor ${fmtDb(row.minPsnr)} dB / ${fmtSsim(row.minSsim)}): `
          + 'scored and printed, not gating');
      for (const row of annotated)
        console.log(`  annotation ${row.layer.name}: ${row.layer.contentDiffersReason}`);
    } else {
      console.log(`${ok ? 'PASS' : 'FAIL'}: ${rows.length - failed.length}/${rows.length} layers within budget`);
      if (!ok) console.log(`failed layers: ${failed.map(row => row.layer.name).join(', ')}`);
    }
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
    for (const state of thresholds.states ?? []) {
      const route = (state.path ?? []).map(stage => {
        const column = stage.categoryId ? `${stage.categoryId}/` : '';
        if (stage.catalogueId) return `${column}catalogue:${stage.catalogueId}`;
        if (stage.rowIndex !== undefined) return `${column}row:${stage.rowIndex}`;
        return `${column}row:${stage.folderRowTitle ?? stage.folderRowId}`;
      }).join(' > ');
      console.log(`${state.name.padEnd(24)} ${(route || 'root, no navigation').padEnd(58)} reference ${state.reference}`);
    }
    return;
  }
  if (opts.list) {
    for (const layer of thresholds.layers)
      console.log(`${layer.name.padEnd(18)} ${layer.crop.join(',').padEnd(18)} baseline ${layer.baseline.psnr.toFixed(2)} dB / ${layer.baseline.ssim.toFixed(4)}`
        + (layer.contentDiffers === true ? '  [content differs]' : ''));
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
