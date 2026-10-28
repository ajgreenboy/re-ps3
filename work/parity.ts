// work/parity.ts - the P0 parity scorecard tool (native-xmb/PARITY-PHASES.md P0.1-P0.5).
//
// Per-screen parity claims need four things that did not exist as one tool: a catalogue of every
// reachable XMB state, a capture matrix, a reference contract, and a scorecard that says -- honestly,
// per state and per tier -- what is proven. This tool owns the last three and reads the first.
//
//   bun work/parity.ts --catalogue            the state catalogue as a table (XMBML view, route,
//                                             native reachability, regions)
//   bun work/parity.ts --list                 the same inventory, one line per state
//   bun work/parity.ts --matrix               the planned cells (state x theme x background x res)
//   bun work/parity.ts --capture              capture the selected cells and assert Tier A + Tier C
//   bun work/parity.ts --score                score the newest run against the reference set
//                                             (JSON + markdown under out/parity/scorecards/)
//   bun work/parity.ts --regression           score, then diff against the previous scorecard
//   bun work/parity.ts --references-missing   which cells have no RPCS3 reference yet
//
// Typical runs:
//   bun work/parity.ts --capture --score                 canonical cell, every capturable state
//   bun work/parity.ts --capture --score --state root
//   bun work/parity.ts --capture --resolution 720p --background night
//   bun work/parity.ts --capture --score --phase-sweep 0,3,6,9,12,15,18,21
//                                                        also capture the ribbon-phase diagnostic
//   bun work/parity.ts --score --strict                  score with content-difference regions gated
//   bun work/parity.ts --regression                      (after a second capture+score exists)
//
// Two scoring-validity rules this tool now implements:
//   * a region annotated contentDiffers:true in the catalogue is measured and printed but never gates
//     (informative); --strict puts it back in the gate, and the scorecard always prints the annotation
//     count so the informative set cannot grow without showing up;
//   * the ribbon's cloth phase is not comparable under the pinned render clock (it freezes the cloth at
//     the settle's phase). --phase-sweep captures one state on the app's live clock with a scripted
//     pause at each offset and the scorecard reports the shipped-phase score beside the best swept
//     phase. That number is scoring-only and is never a gate: the shipped capture is still the cell
//     that gates.
//
// Conventions reused from work/ab-layers.ts (read-only neighbour; this tool never writes it):
//   * script tokens are the app's own (src/main.cpp): full, size=WxH, wait, arrows, ENTER, ESC,
//     TRIANGLE, ps, pshold, NOTIFY=, VOLSET=, VOLRESTORE, type=, grab=, quit;
//   * clocks are pinned exactly as the harness pins them (--render-clock, --background-clock,
//     --ui-clock, --focus-time), so two captures of the same cell are pixel-identical;
//   * XMB_DUMP_ENTRIES writes the catalogue dump plus the page's own .folder.json / .dl.json /
//     .store.json, and each grab writes <png>.json (the state sidecar);
//   * XMB_LOG/XMB_PS_LOG collect the app's own qInfo lines, which are the Tier C oracle;
//   * a state's data directory is seeded from a recorded seed path, never from the live profile.
//
// Metrics are work/ab-compare.ts's (RGB PSNR, non-overlapping 8x8 luma SSIM), so a Tier B number here
// and a layer number in ab-layers.ts are the same measurement.
//
// Exit codes: 0 every checked gate passed; 1 a Tier A/B/C gate failed or a regression was found;
// 2 an input/runtime error (bad catalogue, missing executable, capture failure).
// Nothing in this tool launches RPCS3: the reference rig belongs to the capture agent (P0.3).

import { decodePng, psnr, ssim, type Crop, type PngImage } from './ab-compare';
import { createHash } from 'node:crypto';
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

type Rect = [number, number, number, number];

// A numeric test, mirroring the sidecar-expectation vocabulary work/ab-layers.ts already uses.
type NumericTest = { gt?: number; ge?: number; lt?: number; le?: number };
type ValueTest = NumericTest | { prefix: string } | { contains: string };
type FieldExpect = string | number | boolean | ValueTest;

// A region whose content the app cannot reproduce from the same reference frame (a console account,
// service or row set the native build replaces with host data). It is still measured and reported,
// but it is informative, never gating - and the reason is required so the annotation is auditable.
type Region = {
  name: string; crop: Rect; required?: boolean; psnrDb?: number; ssim?: number;
  contentDiffers?: boolean;
  contentDiffersReason?: string;
};
type Stage = { catalogueId?: string; rowIndex?: number };
type Route = {
  column?: string;
  waits?: number;
  settleWaits?: number;
  stages?: Stage[];
  preGrab?: string[];
  postGrab?: string[];
};
type DownloadRowExpect = { idPrefix?: string; title: string; state?: string; text?: string; detail?: string };
type Expect = {
  category?: string;
  depth?: number | NumericTest;
  entryId?: string;
  entryTitle?: string;
  entryCount?: number | NumericTest;
  pageKind?: string;
  catalogueHas?: string[];
  catalogueRowIds?: string[];
  folderMode?: string;
  folderTitle?: string;
  folderRowsPrefix?: string;
  folderRowsDetailContains?: string;
  downloads?: { count?: number; error?: string; message?: string; rows?: DownloadRowExpect[] };
  sidecar?: Record<string, FieldExpect>;
  log?: Array<{ pattern: string; count?: number }>;
  noStoreDump?: boolean;
};
type NativeReach = { reachable: 'yes' | 'message-only' | 'backend' | 'no'; how?: string; reason?: string };
type State = {
  id: string;
  title: string;
  class: string;
  pageModel: string;
  xmbml: { document: string; view: string } | null;
  xmbmlNote?: string;
  native: NativeReach;
  // A native surface with no firmware counterpart: it is still captured and gated, but it owes no
  // firmware reference, so it is left out of the reference debt instead of counted as a missing capture.
  noFirmwareReference?: boolean;
  route?: Route;
  dataSeed?: string;
  seedPaths?: string[];
  env?: Record<string, string>;
  waits?: number;
  settleWaits?: number;
  expect?: Expect;
  tierANote?: string;
  trace?: Array<{ pattern: string }>;
  traceLog?: string;
  regions: Region[];
  captures?: boolean;
};
type Catalogue = {
  schema: string;
  generated?: string;
  note?: string;
  flash?: string;
  xmbmlRoot?: string;
  defaults?: { dataSeed?: string; seedPaths?: string[]; waits?: number; settleWaits?: number };
  states: State[];
};
type ThemeAxis = { id: string; preferences: Record<string, string | number>; note?: string };
type BackgroundAxis = { id: string; clock: string; note?: string };
type ResolutionAxis = { id: string; width: number; height: number; token: string; note?: string };
type BudgetClass = {
  psnrDb: number;
  ssim: number;
  wholeFramePsnrDb?: number;
  wholeFrameSsim?: number;
  rationale?: string;
};
type ReferenceDoc = {
  schema: string;
  updated?: string;
  note?: string;
  root: string;
  naming?: { pattern?: string; rules?: string[] };
  provenance?: Record<string, unknown>;
  pairing?: Record<string, string>;
  notReferences?: string[];
  matrix: {
    note?: string;
    themes: ThemeAxis[];
    backgrounds: BackgroundAxis[];
    resolutions: ResolutionAxis[];
    canonical: { theme: string; background: string; resolution: string };
    size?: string;
  };
  rpcs3?: Record<string, unknown>;
  registered?: Array<{ cell: string; reference: string; kind: string; note?: string }>;
  budgets: {
    updated: string;
    provisional?: boolean;
    revisionNote: string;
    datedChangeRule?: string;
    classes: Record<string, BudgetClass>;
    regionOverrides?: string;
  };
};

// The repo root is this script's parent directory: the same resolution ab-layers.ts uses.
const repo = resolve(import.meta.dir, '..');
const cataloguePath = join(repo, 'work', 'parity-catalogue.json');
const referencePath = join(repo, 'work', 'parity-reference.json');
const defaultOutRoot = join(repo, 'out', 'parity');

// Documented defaults, exactly as the P0 dispatch row names them.
const defaultExe = 'C:/dev/out/native-parity/re-ps3-native.exe';
const defaultFlash = 'C:/dev/rpcs3/bin/dev_flash';
// The canonical harness pins (work/ab-layers.thresholds.json capture block).
const defaultRenderClock = 30;
const defaultUiClock = '2026-10-25T12:32:00';
const defaultFocusTimeMs = 2000;
const defaultTimeoutMs = 120000;
// Same regression budget as the per-layer harness: 0.5 dB is a 12% MSE increase, 0.004 SSIM is a
// comparable visible change, and two deterministic captures are pixel-identical.
const regressionBudget = { psnrDb: 0.5, ssim: 0.004 };

const classes = new Set(['icon-heavy', 'text-heavy', 'composite']);

const scriptKeys = new Set(['full', 'wait', 'LEFT', 'RIGHT', 'UP', 'DOWN', 'ENTER', 'ESC', 'PGUP', 'PGDN',
  'BACKSPACE', 'PREV', 'NEXT', 'SHUFFLE', 'REPEAT', 'ZOOMIN', 'ZOOMOUT', 'RESETVIEW', 'ROTATELEFT', 'ROTATERIGHT',
  'REFRESH', 'TRIANGLE', 'F10', 'ps', 'pshold', 'VOLUP', 'VOLDOWN', 'MUTE', 'VOLRESTORE', 'quit']);

const usage = `usage: bun work/parity.ts <command> [options]

commands (exactly one, except --capture may be combined with --score):
  --catalogue               print the state catalogue: id, class, XMBML view, route, reachability, regions
  --list                    one line per state (id, class, reachability, regions, trace oracle)
  --matrix                  print the planned capture matrix (state x theme x background x resolution)
  --capture                 capture the selected cells, then assert Tier A (dumps) and Tier C (logs)
  --score                   score the newest run against the reference set; writes JSON + markdown
                            scorecards under out/parity/scorecards/
  --regression              score, then diff against the previous scorecard (exit 1 on a regression)
  --references-missing      report cells that have no RPCS3 reference yet (no capture, no scoring)

selection:
  --state <id>[,<id>...]    only these catalogue states
  --theme <id>[,...]        theme axis (default: the reference contract's canonical theme)
  --background <id>[,...]   background axis (default: canonical)
  --resolution <id>[,...]   resolution axis (default: canonical)
  --matrix                  with --capture: capture every selected axis combination, not one cell
  --run <dir>               score this run directory instead of the newest one
  --previous <file>         with --regression: diff against this scorecard instead of the newest pair

capture inputs:
  --exe <path>              capture executable (default ${defaultExe})
  --flash <dir>             dev_flash directory (default ${defaultFlash})
  --steam-root <dir>        Steam root for the capture (default: the host's own library)
  --render-clock <sec>      frozen animation time (default ${defaultRenderClock})
  --ui-clock <ISO>          frozen displayed clock (default ${defaultUiClock})
  --focus-time <ms>         frozen focus glow age (default ${defaultFocusTimeMs})
  --timeout-ms <n>          per-capture timeout (default ${defaultTimeoutMs})
  --out <dir>               runs and scorecards root (default out/parity)
  --phase-sweep <sec,...>   also capture one state per offset on the app's live clock, with a
                            scripted pause of that many seconds before the grab, so the ribbon cloth
                            sits at a different phase for scoring (default: off; env PARITY_PHASE_SWEEP)
  --phase-state <id,...>    the state(s) --phase-sweep captures (default root; env PARITY_PHASE_STATE)

scoring:
  --strict                  gate the catalogue's contentDiffers regions again (strict audit mode;
                            the default scores them as informative, printed but not gating)

output:
  --json                    machine-readable output
  --markdown <file>         with --score: also write the markdown scorecard to this path
  --help                    show this text`;

function fail(message: string): never {
  console.error(`parity: ${message}`);
  process.exit(2);
}

function readJson(path: string, label: string): any {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    fail(`${label} ${path}: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function resolveRepo(path: string) {
  return resolve(repo, path);
}

function isRect(value: unknown): value is Rect {
  return Array.isArray(value) && value.length === 4 && value.every(n => Number.isSafeInteger(n)) &&
    value[0] >= 0 && value[1] >= 0 && value[2] > 0 && value[3] > 0;
}

function isNumericTest(value: unknown): value is NumericTest {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const keys = Object.keys(value);
  if (!keys.length || keys.some(key => !['gt', 'ge', 'lt', 'le'].includes(key))) return false;
  return keys.every(key => typeof (value as any)[key] === 'number' && Number.isFinite((value as any)[key]));
}

function isValueTest(value: unknown): value is ValueTest {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const keys = Object.keys(value);
  if (keys.length !== 1) return false;
  const key = keys[0];
  const inner = (value as any)[key];
  if (key === 'prefix' || key === 'contains') return typeof inner === 'string';
  return isNumericTest(value);
}

function isFieldExpect(value: unknown): value is FieldExpect {
  if (typeof value === 'string' || typeof value === 'boolean') return true;
  if (typeof value === 'number' && Number.isFinite(value)) return true;
  return isValueTest(value);
}

function sha256File(path: string) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

// --------------------------------------------------------------------------------------------
// Loading and validating the catalogue and the reference contract. A malformed catalogue is an
// input error (exit 2), never a silently smaller run.
// --------------------------------------------------------------------------------------------

function loadCatalogue(): Catalogue {
  const raw = readJson(cataloguePath, 'catalogue') as Catalogue;
  const where = 'work/parity-catalogue.json';
  if (raw.schema !== 'xmb-parity-catalogue/1') fail(`${where}: unsupported schema ${JSON.stringify(raw.schema)}`);
  if (!Array.isArray(raw.states) || !raw.states.length) fail(`${where}: states must be a non-empty array`);
  const ids = new Set<string>();
  for (const [index, state] of raw.states.entries()) {
    const at = `${where}: states[${index}]`;
    if (!state || typeof state !== 'object') fail(`${at} is not an object`);
    if (typeof state.id !== 'string' || !/^[a-z0-9][a-z0-9-]*$/.test(state.id))
      fail(`${at}.id must be kebab-case ASCII (it becomes part of a reference filename)`);
    if (ids.has(state.id)) fail(`${where}: duplicate state id ${JSON.stringify(state.id)}`);
    ids.add(state.id);
    if (!classes.has(state.class)) fail(`${state.id}: unknown class ${JSON.stringify(state.class)}`);
    if (state.xmbml !== null && (!state.xmbml || typeof state.xmbml.document !== 'string' ||
      typeof state.xmbml.view !== 'string' || !state.xmbml.document || !state.xmbml.view))
      fail(`${state.id}: xmbml must be {document,view} or null`);
    if (!state.native || !['yes', 'message-only', 'backend', 'no'].includes(state.native.reachable))
      fail(`${state.id}: native.reachable must be yes|message-only|backend|no`);
    if (state.native.reachable !== 'yes' && state.captures !== false)
      fail(`${state.id}: a state that is not reachable must set "captures": false`);
    if (state.noFirmwareReference !== undefined && typeof state.noFirmwareReference !== 'boolean')
      fail(`${state.id}: noFirmwareReference must be a boolean`);
    if (!Array.isArray(state.regions) || !state.regions.length) fail(`${state.id}: regions must be a non-empty array`);
    const regionNames = new Set<string>();
    for (const [regionIndex, region] of state.regions.entries()) {
      if (!region || typeof region.name !== 'string' || !region.name)
        fail(`${state.id}: regions[${regionIndex}].name must be non-empty`);
      if (regionNames.has(region.name)) fail(`${state.id}: duplicate region ${JSON.stringify(region.name)}`);
      regionNames.add(region.name);
      if (!isRect(region.crop)) fail(`${state.id}: ${region.name}: crop must be [x,y,width,height] integers`);
      const [x, y, w, h] = region.crop;
      if (w < 8 || h < 8) fail(`${state.id}: ${region.name}: crop must be at least 8x8 (SSIM requirement)`);
      if (x + w > 1920 || y + h > 1080)
        fail(`${state.id}: ${region.name}: crop ${region.crop.join(',')} is outside 1920x1080`);
      for (const key of ['psnrDb', 'ssim'] as const)
        if (region[key] !== undefined && (typeof region[key] !== 'number' || !Number.isFinite(region[key])))
          fail(`${state.id}: ${region.name}.${key} must be a number`);
      // A content-difference annotation is a gate decision, so it is validated like one: a reason is
      // mandatory, the frame (the whole picture) can never be excused, and the flag must be a boolean.
      // The scorecard prints every annotation and its count, so this set cannot grow silently.
      if (region.contentDiffers !== undefined && typeof region.contentDiffers !== 'boolean')
        fail(`${state.id}: ${region.name}.contentDiffers must be a boolean`);
      if (region.contentDiffers === true) {
        if (region.name === 'frame')
          fail(`${state.id}: the frame region cannot be contentDiffers - it is the whole-picture score`);
        if (typeof region.contentDiffersReason !== 'string' || !region.contentDiffersReason.trim())
          fail(`${state.id}: ${region.name}: contentDiffers needs a one-line contentDiffersReason`);
      }
    }
    if (state.route !== undefined) {
      const route = state.route;
      if (!route || typeof route !== 'object') fail(`${state.id}: route must be an object`);
      if (route.column !== undefined && (typeof route.column !== 'string' || !route.column))
        fail(`${state.id}: route.column must be a non-empty category id`);
      for (const key of ['waits', 'settleWaits'] as const)
        if (route[key] !== undefined && (!Number.isSafeInteger(route[key]) || route[key]! < 0 || route[key]! > 60))
          fail(`${state.id}: route.${key} must be an integer from 0 through 60`);
      if (route.stages !== undefined) {
        if (!Array.isArray(route.stages) || !route.stages.length) fail(`${state.id}: route.stages must be a non-empty array`);
        for (const [stageIndex, stage] of route.stages.entries()) {
          const set = (stage.catalogueId === undefined ? 0 : 1) + (stage.rowIndex === undefined ? 0 : 1);
          if (set !== 1) fail(`${state.id}: route.stages[${stageIndex}] must define exactly one of catalogueId, rowIndex`);
          if (stage.catalogueId !== undefined && (typeof stage.catalogueId !== 'string' || !stage.catalogueId))
            fail(`${state.id}: route.stages[${stageIndex}].catalogueId must be non-empty`);
          if (stage.rowIndex !== undefined && (!Number.isSafeInteger(stage.rowIndex) || stage.rowIndex < 0 || stage.rowIndex > 5000))
            fail(`${state.id}: route.stages[${stageIndex}].rowIndex must be an integer from 0 through 5000`);
          if (stageIndex > 0 && stage.catalogueId !== undefined && stage.rowIndex === undefined)
            fail(`${state.id}: route.stages[${stageIndex}] must target a row in the page the previous stage opened (rowIndex)`);
        }
      }
      for (const key of ['preGrab', 'postGrab'] as const) {
        const tokens = route[key];
        if (tokens === undefined) continue;
        if (!Array.isArray(tokens) || tokens.some(token => typeof token !== 'string' || !token))
          fail(`${state.id}: route.${key} must be a string array`);
        for (const token of tokens) {
          const base = token.split('=')[0];
          if (!scriptKeys.has(base) && !/^size=\d+x\d+$/.test(token) &&
            !/^(NOTIFY|VOLSET|VOLKEY|type|idle|burst)=/.test(token))
            fail(`${state.id}: route.${key} token ${JSON.stringify(token)} is not a script token`);
          if (token.startsWith('grab=') || token === 'quit')
            fail(`${state.id}: route.${key} must not set grab or quit (the harness owns both)`);
        }
      }
    }
    if (state.env !== undefined) {
      for (const [key, value] of Object.entries(state.env)) {
        if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) fail(`${state.id}: env key ${JSON.stringify(key)} is invalid`);
        if (typeof value !== 'string') fail(`${state.id}: env.${key} must be a string`);
      }
    }
    if (state.expect !== undefined) {
      const allowed = new Set(['category', 'depth', 'entryId', 'entryTitle', 'entryCount', 'pageKind', 'catalogueHas',
        'catalogueRowIds', 'folderMode', 'folderTitle', 'folderRowsPrefix', 'folderRowsDetailContains', 'downloads',
        'sidecar', 'log', 'noStoreDump']);
      for (const key of Object.keys(state.expect)) {
        if (!allowed.has(key))
          fail(`${state.id}: expect.${key} is not an assertion this tool checks (allowed: ${[...allowed].join(', ')})`);
      }
      for (const key of ['depth', 'entryCount'] as const) {
        const value = (state.expect as any)[key];
        if (value === undefined) continue;
        if (typeof value !== 'number' && !isNumericTest(value))
          fail(`${state.id}: expect.${key} must be an integer or {gt,ge,lt,le}`);
      }
      for (const key of ['category', 'entryId', 'entryTitle', 'pageKind', 'folderMode', 'folderTitle',
        'folderRowsPrefix', 'folderRowsDetailContains'] as const)
        if (state.expect[key] !== undefined && typeof state.expect[key] !== 'string')
          fail(`${state.id}: expect.${key} must be a string`);
      for (const listKey of ['catalogueHas', 'catalogueRowIds'] as const)
        if (state.expect[listKey] !== undefined &&
          (!Array.isArray(state.expect[listKey]) || state.expect[listKey]!.some(id => typeof id !== 'string' || !id)))
          fail(`${state.id}: expect.${listKey} must be a string array`);
      if (state.expect.sidecar !== undefined) {
        for (const [field, value] of Object.entries(state.expect.sidecar)) {
          if (!/^[A-Za-z][A-Za-z0-9]*$/.test(field)) fail(`${state.id}: sidecar field ${JSON.stringify(field)} is invalid`);
          if (!isFieldExpect(value)) fail(`${state.id}: expect.sidecar.${field} must be a literal or a test`);
        }
      }
      if (state.expect.log !== undefined) {
        for (const entry of state.expect.log) {
          if (!entry || typeof entry.pattern !== 'string' || !entry.pattern)
            fail(`${state.id}: expect.log entries must be {pattern}`);
          try { new RegExp(entry.pattern); } catch (error) {
            fail(`${state.id}: expect.log pattern ${JSON.stringify(entry.pattern)}: ${error instanceof Error ? error.message : error}`);
          }
        }
      }
      if (state.expect.downloads !== undefined && state.expect.downloads.rows !== undefined) {
        for (const row of state.expect.downloads.rows)
          if (!row || typeof row.title !== 'string' || !row.title) fail(`${state.id}: expect.downloads.rows entries need a title`);
      }
    }
    if (state.trace !== undefined) {
      if (!Array.isArray(state.trace)) fail(`${state.id}: trace must be an array`);
      for (const entry of state.trace) {
        if (!entry || typeof entry.pattern !== 'string' || !entry.pattern) fail(`${state.id}: trace entries must be {pattern}`);
        try { new RegExp(entry.pattern); } catch (error) {
          fail(`${state.id}: trace pattern ${JSON.stringify(entry.pattern)}: ${error instanceof Error ? error.message : error}`);
        }
      }
    }
    if (state.captures !== false && !state.route && state.id !== 'root')
      fail(`${state.id}: a capturable state needs a route (only the root state has none)`);
  }
  return raw;
}

function loadReference(): ReferenceDoc {
  const raw = readJson(referencePath, 'reference') as ReferenceDoc;
  const where = 'work/parity-reference.json';
  if (raw.schema !== 'xmb-parity-reference/1') fail(`${where}: unsupported schema ${JSON.stringify(raw.schema)}`);
  if (typeof raw.root !== 'string' || !raw.root) fail(`${where}: root must be a non-empty path`);
  const matrix = raw.matrix;
  if (!matrix || !Array.isArray(matrix.themes) || !matrix.themes.length) fail(`${where}: matrix.themes must be non-empty`);
  if (!Array.isArray(matrix.backgrounds) || !matrix.backgrounds.length) fail(`${where}: matrix.backgrounds must be non-empty`);
  if (!Array.isArray(matrix.resolutions) || !matrix.resolutions.length) fail(`${where}: matrix.resolutions must be non-empty`);
  for (const axis of [matrix.themes, matrix.backgrounds, matrix.resolutions]) {
    const ids = new Set<string>();
    for (const entry of axis) {
      if (!entry || typeof entry.id !== 'string' || !/^[a-z0-9][a-z0-9-]*$/.test(entry.id))
        fail(`${where}: axis id ${JSON.stringify((entry as any)?.id)} must be kebab-case ASCII`);
      if (ids.has(entry.id)) fail(`${where}: duplicate axis id ${JSON.stringify(entry.id)}`);
      ids.add(entry.id);
    }
  }
  for (const background of matrix.backgrounds) {
    if (typeof background.clock !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}$/.test(background.clock))
      fail(`${where}: background ${background.id}: clock must be an ISO local date-time (YYYY-MM-DDTHH:MM:SS)`);
  }
  for (const resolution of matrix.resolutions) {
    if (!Number.isSafeInteger(resolution.width) || !Number.isSafeInteger(resolution.height) ||
      resolution.width < 640 || resolution.height < 360 || typeof resolution.token !== 'string' || !resolution.token)
      fail(`${where}: resolution ${resolution.id}: needs width/height and a script token`);
  }
  const canonical = matrix.canonical;
  if (!canonical || !matrix.themes.some(t => t.id === canonical.theme) ||
    !matrix.backgrounds.some(b => b.id === canonical.background) ||
    !matrix.resolutions.some(r => r.id === canonical.resolution))
    fail(`${where}: matrix.canonical must name one id from each axis`);
  const budgets = raw.budgets;
  if (!budgets || typeof budgets.updated !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(budgets.updated))
    fail(`${where}: budgets.updated must be an ISO date (the dated-change rule)`);
  if (typeof budgets.revisionNote !== 'string' || !budgets.revisionNote.trim())
    fail(`${where}: budgets.revisionNote must explain the current policy (the dated-change rule)`);
  for (const className of classes) {
    const budget = budgets.classes?.[className];
    if (!budget) fail(`${where}: budgets.classes.${className} is required for screen class ${className}`);
    if (typeof budget.psnrDb !== 'number' || !Number.isFinite(budget.psnrDb) || budget.psnrDb < 0 || budget.psnrDb > 60)
      fail(`${where}: budgets.classes.${className}.psnrDb must be 0 through 60`);
    if (typeof budget.ssim !== 'number' || !Number.isFinite(budget.ssim) || budget.ssim < 0 || budget.ssim > 1)
      fail(`${where}: budgets.classes.${className}.ssim must be 0 through 1`);
  }
  if (raw.registered !== undefined) {
    for (const entry of raw.registered) {
      if (!entry || typeof entry.cell !== 'string' || typeof entry.reference !== 'string')
        fail(`${where}: registered entries must be {cell,reference,kind}`);
    }
  }
  return raw;
}

// --------------------------------------------------------------------------------------------
// Small formatting helpers (the same shapes the per-layer harness prints).
// --------------------------------------------------------------------------------------------

function pad(value: string, width: number, right = false) {
  return right ? value.padEnd(width) : value.padStart(width);
}
function fmtDb(value: number) {
  return Number.isFinite(value) ? value.toFixed(2) : 'inf';
}
function fmtSsim(value: number) {
  return value.toFixed(4);
}
function stamp() {
  const now = new Date();
  const two = (n: number) => String(n).padStart(2, '0');
  return `${now.getFullYear()}${two(now.getMonth() + 1)}${two(now.getDate())}-${two(now.getHours())}${two(now.getMinutes())}${two(now.getSeconds())}`;
}
function isoNow() {
  return new Date().toISOString().replace(/\.\d+Z$/, 'Z');
}

// XMBML cross-check: the catalogue claims each state is defined by a firmware view. Read that view
// id out of the document, so a typo or a stale document name is caught without a capture.
const viewCache = new Map<string, Set<string>>();
function xmbmlViews(flash: string, xmbmlRoot: string, document: string): Set<string> | undefined {
  const path = join(flash, ...xmbmlRoot.split('/'), document);
  if (viewCache.has(path)) return viewCache.get(path);
  let text: string;
  try {
    text = readFileSync(path, 'utf8');
  } catch {
    viewCache.set(path, new Set());
    return undefined;
  }
  const views = new Set<string>();
  for (const match of text.matchAll(/<View\s+id="([^"]+)"/g)) views.add(match[1]);
  viewCache.set(path, views);
  return views;
}

// --------------------------------------------------------------------------------------------
// Cells: one state x theme x background x resolution combination.
// --------------------------------------------------------------------------------------------

type Cell = { key: string; state: string; theme: ThemeAxis; background: BackgroundAxis; resolution: ResolutionAxis };

function cellKey(stateId: string, theme: string, background: string, resolution: string) {
  return `${stateId}__${theme}__${background}__${resolution}`;
}

function selectedCells(catalogue: Catalogue, refs: ReferenceDoc, opts: ReturnType<typeof parseArgs>): Cell[] {
  const byId = new Map(catalogue.states.map(state => [state.id, state]));
  const states = opts.state.length
    ? opts.state.map(id => {
        const state = byId.get(id);
        if (!state) fail(`unknown state ${JSON.stringify(id)}; known: ${[...byId.keys()].join(', ')}`);
        return state;
      })
    : catalogue.states.filter(state => state.captures !== false);
  const expand = opts.matrix || opts.command === '--matrix';
  const themes = opts.theme.length ? opts.theme
    : expand ? refs.matrix.themes.map(theme => theme.id) : [refs.matrix.canonical.theme];
  const backgrounds = opts.background.length ? opts.background
    : expand ? refs.matrix.backgrounds.map(background => background.id) : [refs.matrix.canonical.background];
  const resolutions = opts.resolution.length ? opts.resolution
    : expand ? refs.matrix.resolutions.map(resolution => resolution.id) : [refs.matrix.canonical.resolution];
  for (const id of themes)
    if (!refs.matrix.themes.some(theme => theme.id === id))
      fail(`unknown theme ${JSON.stringify(id)}; known: ${refs.matrix.themes.map(t => t.id).join(', ')}`);
  for (const id of backgrounds)
    if (!refs.matrix.backgrounds.some(background => background.id === id))
      fail(`unknown background ${JSON.stringify(id)}; known: ${refs.matrix.backgrounds.map(b => b.id).join(', ')}`);
  for (const id of resolutions)
    if (!refs.matrix.resolutions.some(resolution => resolution.id === id))
      fail(`unknown resolution ${JSON.stringify(id)}; known: ${refs.matrix.resolutions.map(r => r.id).join(', ')}`);
  const cells: Cell[] = [];
  for (const state of states) {
    if (state.captures === false) continue;
    for (const theme of themes) for (const background of backgrounds) for (const resolution of resolutions) {
      cells.push({
        key: cellKey(state.id, theme, background, resolution),
        state: state.id,
        theme: refs.matrix.themes.find(t => t.id === theme)!,
        background: refs.matrix.backgrounds.find(b => b.id === background)!,
        resolution: refs.matrix.resolutions.find(r => r.id === resolution)!,
      });
    }
  }
  return cells;
}

// --------------------------------------------------------------------------------------------
// Capture
// --------------------------------------------------------------------------------------------

type RunManifest = {
  schema: string;
  generated: string;
  run: string;
  exe: string;
  exeSha256: string;
  flash: string;
  catalogue: string;
  catalogueSha256: string;
  reference: string;
  referenceSha256: string;
  pins: { renderClock: number; uiClock: string; focusTimeMs: number };
  // Present only when --phase-sweep ran: the offsets and states the run captured on the live clock.
  phaseSweep?: { offsetsSeconds: number[]; states: string[]; note: string };
  discoveries: Array<{ key: string; seed: string; dir: string; dump: string; columns: string[] }>;
  cells: Array<{
    key: string;
    state: string;
    theme: string;
    background: string;
    resolution: string;
    dir: string;
    png: string;
    sidecar: string;
    dump: string;
    folder: string;
    downloads: string;
    log: string;
    psLog: string;
    width: number;
    height: number;
    tokens: string;
    attempts?: number;
    error?: string;
    // A phase-sweep capture: the same cell on the app's live clock with a scripted pause. It is scored
    // against the same reference but is never a gate; only the unwrapped cell (no phase) gates.
    phase?: { offsetSeconds: number; pauseMs: number; label: string };
  }>;
};

type CellFiles = {
  dir: string;
  png: string;
  sidecar: string;
  dump: string;
  folder: string;
  downloads: string;
  log: string;
  psLog: string;
};

function scriptTokenOk(token: string) {
  // grab= and quit are generated by this tool (the catalogue is rejected if it sets them).
  if (token === 'quit' || token.startsWith('grab=')) return true;
  const base = token.split('=')[0];
  // pause= is generated only by the phase sweep, never by a catalogue route (src/main.cpp pauses the
  // script clock for N ms, which is exactly how a live-clock capture is placed at a chosen phase).
  if (/^pause=\d+$/.test(token)) return true;
  return scriptKeys.has(base) || /^size=\d+x\d+$/.test(token) ||
    /^(NOTIFY|VOLSET|VOLKEY|type|idle|burst)=/.test(token);
}

// The catalogue dump lists every column in the shell's own order, so a route that starts in another
// column resolves its LEFT/RIGHT count and its row index from the discovery capture instead of a
// typed offset (the convention work/ab-layers.ts already uses).
type Discovered = { columns: string[]; rows: Map<string, Map<string, number>> };
function discoveredFromDump(dump: any): Discovered {
  const columns: string[] = [];
  const rows = new Map<string, Map<string, number>>();
  for (const category of (Array.isArray(dump?.categories) ? dump.categories : [])) {
    if (!category || typeof category.id !== 'string') continue;
    columns.push(category.id);
    const indices = new Map<string, number>();
    (Array.isArray(category.entries) ? category.entries : []).forEach((entry: any, index: number) => {
      if (entry && typeof entry.id === 'string' && !indices.has(entry.id)) indices.set(entry.id, index);
    });
    rows.set(category.id, indices);
  }
  return { columns, rows };
}

// Mirrors ab-layers' pathTokens: the run starts in the Game column, the first stage may move to
// another column (horizontal clamps at the ends, then UP clamps to the first row), and every stage
// sends DOWN x index then ENTER.
const clampUp = 18;

// Scripted keys land on the app's own per-frame input path, so a token every 300 ms can arrive while the
// previous move's ease is still running; under load that loses a press (measured: 4 of 25 routes failed
// their dump assertion on the first attempt of a full-suite run, and every one recovered with more time).
// Navigation keys therefore get a full token interval each, and the ENTER that opens a page gets one too:
// the route is slower, but a route that can silently skip a row is worse than a slow one.
const navigationKeys = new Set(['LEFT', 'RIGHT', 'UP', 'DOWN']);
function spacedNavigation(tokens: string[]) {
  const spaced: string[] = [];
  for (const [index, token] of tokens.entries()) {
    spaced.push(token);
    const next = tokens[index + 1];
    if (navigationKeys.has(token) && next !== undefined && (navigationKeys.has(next) || next === 'ENTER'))
      spaced.push('wait');
  }
  return spaced;
}
function routeTokens(state: State, discovered: Discovered): { tokens: string[]; route: string } {
  const route = state.route ?? {};
  const tokens: string[] = [];
  const labels: string[] = [];
  let column = 'game';
  const stages = route.stages ?? [];
  if (route.column && route.column !== 'game') {
    const from = discovered.columns.indexOf('game');
    const to = discovered.columns.indexOf(route.column);
    if (from < 0 || to < 0)
      fail(`${state.id}: column ${JSON.stringify(route.column)} is not in the discovery dump (${discovered.columns.join(', ')})`);
    const delta = to - from;
    tokens.push(...Array(Math.abs(delta)).fill(delta > 0 ? 'RIGHT' : 'LEFT'), 'wait', 'wait');
    tokens.push(...Array(clampUp).fill('UP'), 'wait');
    column = route.column;
    labels.push(`column:${route.column}`);
  }
  for (const [stageIndex, stage] of stages.entries()) {
    let index: number;
    if (stage.rowIndex !== undefined) {
      index = stage.rowIndex;
      labels.push(`row:${index}`);
    } else {
      const rows = discovered.rows.get(column);
      const found = rows?.get(stage.catalogueId!);
      if (found === undefined)
        fail(`${state.id}: catalogue has no ${JSON.stringify(stage.catalogueId)} in column ${JSON.stringify(column)}; `
          + `rows: [${rows ? [...rows.keys()].join(', ') : ''}]`);
      index = found;
      labels.push(`catalogue:${stage.catalogueId}[${index}]`);
    }
    tokens.push(...Array(index).fill('DOWN'), 'ENTER');
    if (stageIndex < stages.length - 1) tokens.push('wait', 'wait');
  }
  return { tokens, route: labels.join(' > ') || 'none' };
}

function seedDataDir(state: State, catalogue: Catalogue, cellDir: string): string {
  const dataDir = join(cellDir, 'data');
  mkdirSync(dataDir, { recursive: true });
  const seedRoot = resolveRepo(state.dataSeed ?? catalogue.defaults?.dataSeed ?? '');
  const seedPaths = state.seedPaths ?? catalogue.defaults?.seedPaths ?? [];
  if (!state.dataSeed && !catalogue.defaults?.dataSeed) fail(`${state.id}: no dataSeed in the state or the catalogue defaults`);
  for (const seed of seedPaths) {
    const source = join(seedRoot, seed);
    if (!existsSync(source)) fail(`${state.id}: data seed missing: ${source}`);
    cpSync(source, join(dataDir, seed), { recursive: true });
  }
  return dataDir;
}

// A theme is applied the way the app applies it: the preferences file in the seeded data directory.
// Writing it here (rather than a new CLI flag) keeps the cell's inputs inside the recorded seed.
function applyThemePreferences(dataDir: string, theme: ThemeAxis, flash: string) {
  const path = join(dataDir, 'preferences.json');
  let document: any = { version: 1, values: {} };
  if (existsSync(path)) {
    try {
      document = JSON.parse(readFileSync(path, 'utf8'));
    } catch (error) {
      fail(`seeded preferences.json is not JSON: ${error instanceof Error ? error.message : error}`);
    }
  }
  if (!document || typeof document !== 'object') document = { version: 1, values: {} };
  if (!document.values || typeof document.values !== 'object') document.values = {};
  for (const [key, value] of Object.entries(theme.preferences ?? {}))
    document.values[key] = typeof value === 'string' ? value.replaceAll('${flash}', flash) : value;
  writeFileSync(path, `${JSON.stringify(document, null, 2)}\n`);
}

async function captureCell(catalogue: Catalogue, refs: ReferenceDoc, opts: ReturnType<typeof parseArgs>,
  cell: Cell, state: State, discovered: Discovered, runDir: string,
  suffix = '', phase?: RunManifest['cells'][number]['phase']): Promise<RunManifest['cells'][number]> {
  const cellDir = join(runDir, 'cells', cell.key + suffix);
  mkdirSync(cellDir, { recursive: true });
  const dataDir = seedDataDir(state, catalogue, cellDir);
  applyThemePreferences(dataDir, cell.theme, opts.flash);
  const png = join(cellDir, 'frame.png');
  const files: CellFiles = {
    dir: cellDir,
    png,
    sidecar: `${png}.json`,
    dump: join(cellDir, 'entries.json'),
    folder: join(cellDir, 'entries.json.folder.json'),
    downloads: join(cellDir, 'entries.json.dl.json'),
    log: join(cellDir, 'native.log'),
    psLog: join(cellDir, 'ps.log'),
  };
  for (const path of [files.log, files.psLog]) writeFileSync(path, '');
  const built = routeTokens(state, discovered);
  const waits = state.route?.waits ?? catalogue.defaults?.waits ?? 10;
  const settle = state.route?.settleWaits ?? catalogue.defaults?.settleWaits ?? 3;
  const tokens = [cell.resolution.token, ...Array(waits).fill('wait'), ...built.tokens,
    ...Array(settle).fill('wait'), ...(state.route?.preGrab ?? []),
    // The phase sweep holds the live clock for N ms right before the grab: every painted frame during
    // the pause advances the cloth, so the grab lands at a different instant of the swell.
    ...(phase ? [`pause=${phase.pauseMs}`] : []),
    `grab=${png}`, ...(state.route?.postGrab ?? []), 'quit'];
  for (const token of tokens)
    if (!scriptTokenOk(token)) fail(`${state.id}: generated token ${JSON.stringify(token)} is not a script token`);

  const env: Record<string, string> = {};
  for (const [key, value] of Object.entries(process.env))
    if (typeof value === 'string') env[key] = value;
  env.XMB_DUMP_ENTRIES = files.dump;
  env.XMB_STEAM_ARTWORK = '0';
  env.XMB_LOG = files.log;
  env.XMB_PS_LOG = files.psLog;
  if (opts.steamRoot) env.XMB_STEAM_ROOT = opts.steamRoot;
  for (const [key, value] of Object.entries(state.env ?? {}))
    env[key] = value.replaceAll('${repo}', repo).replaceAll('${flash}', opts.flash);

  const args = ['--dev-flash', opts.flash, '--data-dir', dataDir,
    // A phase capture deliberately runs the app's live animation clock (no --render-clock): the pinned
    // clock never advances the cloth after its first step, so only a live clock can reach a new phase.
    ...(phase ? [] : ['--render-clock', String(opts.renderClock)]),
    '--background-clock', cell.background.clock, '--ui-clock', opts.uiClock, '--focus-time', String(opts.focusTime),
    '--script', tokens.join(',')];
  if (!quiet) {
    console.log(`\n-- ${cell.key}${phase ? `  [${phase.label}, live clock, pause ${phase.pauseMs} ms]` : ''} (${state.class})`);
    console.log(`   route     ${built.route}`);
    console.log(`   script    ${tokens.length} tokens, grab ${png}`);
  }
  const proc = Bun.spawn([opts.exe, ...args], { stdout: 'ignore', stderr: 'pipe', env });
  const stderrText = new Response(proc.stderr).text();
  let timedOut = false;
  const timer = setTimeout(() => { timedOut = true; proc.kill(); }, opts.timeoutMs);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const stderr = await stderrText;
  writeFileSync(join(cellDir, 'stderr.log'), stderr);
  const record: RunManifest['cells'][number] = {
    key: cell.key, state: cell.state, theme: cell.theme.id, background: cell.background.id,
    resolution: cell.resolution.id, dir: cellDir, png, sidecar: files.sidecar, dump: files.dump,
    folder: files.folder, downloads: files.downloads, log: files.log, psLog: files.psLog,
    width: 0, height: 0, tokens: tokens.join(','), ...(phase ? { phase } : {}),
  };
  if (timedOut) {
    record.error = `capture timed out after ${opts.timeoutMs} ms`;
    return record;
  }
  if (code !== 0) {
    record.error = `capture exited ${code}`;
    const tail = stderr.trim().split(/\r?\n/).slice(-6).join('\n');
    if (tail && !quiet) console.log(`   ${record.error}\n${tail}`);
    return record;
  }
  if (existsSync(files.sidecar)) {
    try {
      const sidecar = JSON.parse(readFileSync(files.sidecar, 'utf8'));
      record.width = Number(sidecar.width) || 0;
      record.height = Number(sidecar.height) || 0;
    } catch { /* the assertion pass reports a broken sidecar */ }
  }
  if (!existsSync(png)) record.error = 'no frame written';
  else if (record.width && (record.width !== cell.resolution.width || record.height !== cell.resolution.height))
    record.error = `frame is ${record.width}x${record.height}, the cell is ${cell.resolution.width}x${cell.resolution.height}`;
  return record;
}

// The discovery dump is per data seed: a column's rows depend on the profile the capture runs with
// (a registered Jellyfin service only exists in the seed that carries apps.json), so a route is only
// resolvable against a dump taken from the same seed.
function seedKey(catalogue: Catalogue, state: State) {
  const seed = state.dataSeed ?? catalogue.defaults?.dataSeed ?? '';
  const paths = (state.seedPaths ?? catalogue.defaults?.seedPaths ?? []).join('+');
  return createHash('sha256').update(`${seed}|${paths}`).digest('hex').slice(0, 8);
}

async function discoveryCapture(catalogue: Catalogue, opts: ReturnType<typeof parseArgs>,
  runDir: string, key: string, seedState: State): Promise<{ dir: string; dump: string; discovered: Discovered }> {
  const state = seedState;
  const dir = join(runDir, 'discovery', key);
  mkdirSync(dir, { recursive: true });
  const dataDir = seedDataDir(state, catalogue, dir);
  const dump = join(dir, 'entries.json');
  const png = join(dir, 'discovery.png');
  const env: Record<string, string> = {};
  for (const [key, value] of Object.entries(process.env)) if (typeof value === 'string') env[key] = value;
  env.XMB_DUMP_ENTRIES = dump;
  env.XMB_STEAM_ARTWORK = '0';
  env.XMB_LOG = join(dir, 'native.log');
  env.XMB_PS_LOG = join(dir, 'ps.log');
  writeFileSync(env.XMB_LOG, '');
  writeFileSync(env.XMB_PS_LOG, '');
  if (opts.steamRoot) env.XMB_STEAM_ROOT = opts.steamRoot;
  const tokens = ['full', ...Array(20).fill('wait'), `grab=${png}`, 'quit'];
  if (!quiet) console.log(`discovery: catalogue dump from a Game-column root capture`);
  const args = ['--dev-flash', opts.flash, '--data-dir', dataDir, '--render-clock', String(opts.renderClock),
    '--background-clock', '2026-10-25T10:00:00', '--ui-clock', opts.uiClock, '--focus-time', String(opts.focusTime),
    '--script', tokens.join(',')];
  const proc = Bun.spawn([opts.exe, ...args], { stdout: 'ignore', stderr: 'pipe', env });
  const stderrText = new Response(proc.stderr).text();
  let timedOut = false;
  const timer = setTimeout(() => { timedOut = true; proc.kill(); }, opts.timeoutMs);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const stderr = await stderrText;
  writeFileSync(join(dir, 'stderr.log'), stderr);
  if (timedOut) fail(`discovery capture timed out after ${opts.timeoutMs} ms; evidence: ${dir}`);
  if (code !== 0) fail(`discovery capture exited ${code}; evidence: ${dir}`);
  if (!existsSync(dump)) fail(`discovery capture wrote no catalogue dump; evidence: ${dir}`);
  const discovered = discoveredFromDump(readJson(dump, 'discovery dump'));
  if (!discovered.columns.length) fail(`discovery dump has no categories; evidence: ${dir}`);
  if (!quiet) {
    console.log(`  columns   ${discovered.columns.join(', ')}`);
    console.log(`  evidence  ${dir}`);
  }
  return { dir, dump, discovered };
}

// --------------------------------------------------------------------------------------------
// Tier A (dump assertions) and Tier C (trace assertions)
// --------------------------------------------------------------------------------------------

type CheckResult = { ok: boolean; problems: string[]; summary: string };

function fieldProblem(field: string, expected: FieldExpect, actual: unknown, problems: string[]) {
  if (isValueTest(expected)) {
    if ('prefix' in expected) {
      if (typeof actual !== 'string' || !actual.startsWith(expected.prefix))
        problems.push(`sidecar ${field}: expected a value starting with ${JSON.stringify(expected.prefix)}, got ${JSON.stringify(actual)}`);
      return;
    }
    if ('contains' in expected) {
      if (typeof actual !== 'string' || !actual.includes(expected.contains))
        problems.push(`sidecar ${field}: expected a value containing ${JSON.stringify(expected.contains)}, got ${JSON.stringify(actual)}`);
      return;
    }
    if (typeof actual !== 'number' || !Number.isFinite(actual)) {
      problems.push(`sidecar ${field}: expected a number ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
      return;
    }
    for (const [operator, limit] of Object.entries(expected)) {
      const ok = operator === 'gt' ? actual > limit : operator === 'ge' ? actual >= limit
        : operator === 'lt' ? actual < limit : actual <= limit;
      if (!ok) problems.push(`sidecar ${field}: expected ${operator} ${limit}, got ${actual}`);
    }
    return;
  }
  if (actual !== expected)
    problems.push(`sidecar ${field}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
}

function numericExpect(field: string, expected: number | NumericTest, actual: unknown, problems: string[]) {
  if (typeof expected === 'number') {
    if (actual !== expected) problems.push(`${field}: expected ${expected}, got ${JSON.stringify(actual)}`);
    return;
  }
  if (typeof actual !== 'number' || !Number.isFinite(actual)) {
    problems.push(`${field}: expected a number ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    return;
  }
  for (const [operator, limit] of Object.entries(expected)) {
    const ok = operator === 'gt' ? actual > limit : operator === 'ge' ? actual >= limit
      : operator === 'lt' ? actual < limit : actual <= limit;
    if (!ok) problems.push(`${field}: expected ${operator} ${limit}, got ${actual}`);
  }
}

// Reads the logs a capture wrote (native qInfo lines and the PS watcher's own log) for the trace and
// log assertions. Order matters for a trace: each pattern must match at or after the previous match.
function readCellLogs(files: CellFiles): string[] {
  const lines: string[] = [];
  for (const path of [files.log, files.psLog]) {
    if (!existsSync(path)) continue;
    for (const line of readFileSync(path, 'utf8').split(/\r?\n/)) if (line) lines.push(line);
  }
  return lines;
}

function checkTrace(state: State, files: CellFiles): { status: 'pass' | 'fail' | 'n/a'; problems: string[]; matched: string[] } {
  const trace = state.trace ?? [];
  if (!trace.length) return { status: 'n/a', problems: [], matched: [] };
  const lines = readCellLogs(files);
  const problems: string[] = [];
  const matched: string[] = [];
  let cursor = 0;
  for (const entry of trace) {
    const pattern = new RegExp(entry.pattern);
    let found = -1;
    for (let index = cursor; index < lines.length; index++) {
      if (pattern.test(lines[index])) { found = index; break; }
    }
    if (found < 0) {
      problems.push(`trace ${JSON.stringify(entry.pattern)}: no matching line${cursor ? ' after the previous step' : ''}`);
      matched.push('MISS');
      continue;
    }
    matched.push(lines[found].trim().slice(0, 120));
    cursor = found + 1;
  }
  return { status: problems.length ? 'fail' : 'pass', problems, matched };
}

function checkExpect(state: State, files: CellFiles): CheckResult {
  const problems: string[] = [];
  if (!state.expect) return { ok: true, problems: ['no expect block: nothing asserted (this state has no Tier A oracle)'], summary: 'no assertions' };
  const expect = state.expect;
  const sidecar = existsSync(files.sidecar) ? (() => {
    try { return JSON.parse(readFileSync(files.sidecar, 'utf8')); } catch { problems.push('the state sidecar is not JSON'); return {}; }
  })() : (problems.push(`no state sidecar ${files.sidecar}`), {});
  const dump = existsSync(files.dump) ? (() => {
    try { return JSON.parse(readFileSync(files.dump, 'utf8')); } catch { problems.push('the catalogue dump is not JSON'); return undefined; }
  })() : (problems.push(`no catalogue dump ${files.dump}`), undefined);
  const folder = existsSync(files.folder) ? readJson(files.folder, 'folder dump') : undefined;
  const downloads = existsSync(files.downloads) ? readJson(files.downloads, 'download dump') : undefined;
  const storeExists = existsSync(`${files.dump}.store.json`);

  for (const key of ['category', 'entryId', 'entryTitle', 'pageKind'] as const)
    if (expect[key] !== undefined && sidecar[key] !== expect[key])
      problems.push(`${key}: expected ${JSON.stringify(expect[key])}, got ${JSON.stringify(sidecar[key])}`);
  for (const key of ['depth', 'entryCount'] as const) {
    const value = (expect as any)[key];
    if (value !== undefined) numericExpect(key.replace('entryCount', 'rows'), value, sidecar[key], problems);
  }
  for (const [field, value] of Object.entries(expect.sidecar ?? {}))
    fieldProblem(field, value, sidecar[field], problems);

  // The column a state lives in: the route names it, otherwise the run started in Game. The
  // catalogue assertions read that column out of the discovery dump, not the Game column blindly.
  const column = state.route?.column ?? 'game';
  const entries = (() => {
    const category = dump && Array.isArray(dump.categories)
      ? dump.categories.find((candidate: any) => candidate?.id === column) : undefined;
    return category && Array.isArray(category.entries) ? category.entries : [];
  })();
  for (const id of expect.catalogueHas ?? [])
    if (!entries.some((entry: any) => entry.id === id)) problems.push(`column ${column} has no entry ${JSON.stringify(id)}`);
  if (expect.catalogueRowIds) {
    const got = entries.map((entry: any) => entry.id);
    if (got.length !== expect.catalogueRowIds.length || expect.catalogueRowIds.some((id, index) => id !== got[index]))
      problems.push(`column ${column} rows: expected [${expect.catalogueRowIds.join(', ')}], got [${got.join(', ')}]`);
  }
  if (expect.folderMode !== undefined) {
    if (!folder) problems.push('expected a <dump>.folder.json but no page opened a folder');
    else {
      if (folder.folder !== expect.folderMode)
        problems.push(`folder mode: expected ${JSON.stringify(expect.folderMode)}, got ${JSON.stringify(folder.folder)}`);
      if (expect.folderTitle !== undefined && folder.title !== expect.folderTitle)
        problems.push(`folder title: expected ${JSON.stringify(expect.folderTitle)}, got ${JSON.stringify(folder.title)}`);
      const rows = Array.isArray(folder.rows) ? folder.rows : [];
      if (expect.folderRowsPrefix) {
        const bad = rows.filter((row: any) => !String(row.id).startsWith(expect.folderRowsPrefix!));
        if (bad.length) problems.push(`${bad.length} folder rows do not start with ${JSON.stringify(expect.folderRowsPrefix)} (e.g. ${bad[0].id})`);
      }
      if (expect.folderRowsDetailContains) {
        const bad = rows.filter((row: any) => !String(row.detail).includes(expect.folderRowsDetailContains!));
        if (bad.length) problems.push(`${bad.length} folder rows lack ${JSON.stringify(expect.folderRowsDetailContains)} in their detail`);
      }
      if (sidecar.entryCount !== folder.count)
        problems.push(`sidecar row count ${sidecar.entryCount} != folder rows ${folder.count}`);
    }
  }
  if (expect.downloads) {
    const wanted = expect.downloads;
    if (!downloads) problems.push('expected the download page dump <dump>.dl.json but the page did not open');
    else {
      if (wanted.count !== undefined && downloads.count !== wanted.count)
        problems.push(`download page count: expected ${wanted.count}, got ${downloads.count}`);
      for (const key of ['error', 'message'] as const)
        if (wanted[key] !== undefined && String(downloads[key] ?? '') !== wanted[key])
          problems.push(`download page ${key}: expected ${JSON.stringify(wanted[key])}, got ${JSON.stringify(downloads[key])}`);
      if (wanted.rows) {
        const rows = Array.isArray(downloads.rows) ? downloads.rows : [];
        if (rows.length !== wanted.rows.length) problems.push(`download rows: expected ${wanted.rows.length}, got ${rows.length}`);
        else for (const [index, row] of wanted.rows.entries()) {
          const seen = rows[index];
          if (row.idPrefix !== undefined && !String(seen.id).startsWith(row.idPrefix))
            problems.push(`download row ${index} id: expected prefix ${JSON.stringify(row.idPrefix)}, got ${JSON.stringify(seen.id)}`);
          if (seen.title !== row.title) problems.push(`download row ${index} title: expected ${JSON.stringify(row.title)}, got ${JSON.stringify(seen.title)}`);
          for (const key of ['state', 'text', 'detail'] as const)
            if (row[key] !== undefined && String(seen[key] ?? '') !== row[key])
              problems.push(`download row ${index} ${key}: expected ${JSON.stringify(row[key])}, got ${JSON.stringify(seen[key])}`);
        }
      }
    }
  }
  if (expect.noStoreDump && storeExists)
    problems.push('a <dump>.store.json exists: a live store request ran, so this state is not network-free');
  if (expect.log) {
    const lines = readCellLogs(files);
    for (const entry of expect.log) {
      const pattern = new RegExp(entry.pattern);
      const seen = lines.filter(line => pattern.test(line)).length;
      if (entry.count !== undefined) {
        if (seen !== entry.count) problems.push(`log ${JSON.stringify(entry.pattern)}: expected ${entry.count} matching line(s), got ${seen}`);
      } else if (!seen) problems.push(`log ${JSON.stringify(entry.pattern)}: no matching line`);
    }
  }
  const summary = [`category ${sidecar.category}`, `depth ${sidecar.depth}`, `rows ${sidecar.entryCount}`,
    sidecar.pageKind ? `pageKind ${sidecar.pageKind}` : '', sidecar.entryTitle ? `row ${JSON.stringify(sidecar.entryTitle)}` : '']
    .filter(Boolean).join(', ');
  return { ok: problems.length === 0, problems, summary };
}

// --------------------------------------------------------------------------------------------
// Tier B (reference scoring)
// --------------------------------------------------------------------------------------------

type RegionScore = {
  name: string;
  crop: Rect;
  required: boolean;
  // gating is required && !contentDiffers (or required && contentDiffers under --strict); it is the
  // only input to a cell's Tier B status. informative regions are measured and printed, never gated.
  gating: boolean;
  contentDiffers: boolean;
  contentDiffersReason?: string;
  psnr: number | string;
  ssim: number;
  minPsnr: number;
  minSsim: number;
  pass: boolean;
};

function budgetFor(state: State, refs: ReferenceDoc, region: Region) {
  const budget = refs.budgets.classes[state.class];
  const frame = region.name === 'frame';
  const psnrDb = region.psnrDb ?? (frame ? budget.wholeFramePsnrDb ?? budget.psnrDb : budget.psnrDb);
  const ssimMin = region.ssim ?? (frame ? budget.wholeFrameSsim ?? budget.ssim : budget.ssim);
  return { psnrDb, ssim: ssimMin };
}

function referencePathFor(cell: string, refs: ReferenceDoc) {
  const registered = (refs.registered ?? []).find(entry => entry.cell === cell);
  if (registered) return { path: resolveRepo(registered.reference), kind: registered.kind, registered: true, note: registered.note };
  return { path: join(resolveRepo(refs.root), `${cell}.png`), kind: 'rpcs3', registered: false, note: undefined as string | undefined };
}

function provenancePathFor(cell: string, refs: ReferenceDoc) {
  return join(resolveRepo(refs.root), `${cell}.state.json`);
}

type CellScore = {
  key: string;
  state: string;
  theme: string;
  background: string;
  resolution: string;
  width: number;
  height: number;
  png: string;
  tierA: 'pass' | 'fail' | 'no-oracle';
  tierAProblems: string[];
  tierANote?: string;
  tierC: 'pass' | 'fail' | 'n/a';
  tierCProblems: string[];
  tierCMatched: string[];
  tierB: 'pass' | 'fail' | 'informative' | 'no-reference' | 'dimension-mismatch' | 'provenance-missing' | 'not-captured';
  tierBProblems: string[];
  reference?: string;
  referenceKind?: string;
  referenceNote?: string;
  provenance?: string;
  regions: RegionScore[];
  // The whole-picture score's informative/gating split, so a state's summary can say why it is not a
  // bare FAIL and which regions would be gated under --strict.
  gatingRegions: string[];
  informativeRegions: string[];
  // PSNR over the ribbon's own band (y 360-840), the region where the cloth lives (PIPELINE.md 4).
  // Reported for the phase sweep only; it is not a budgeted region.
  ribbonBandPsnr?: number | string;
  // Set on a --phase-sweep capture; never on the shipped cell.
  phase?: { offsetSeconds: number; label: string };
  claim: string;
  summary?: string;
  error?: string;
};

function scoreCell(state: State, record: RunManifest['cells'][number], refs: ReferenceDoc,
  strict = false): CellScore {
  const files: CellFiles = {
    dir: record.dir, png: record.png, sidecar: record.sidecar, dump: record.dump, folder: record.folder,
    downloads: record.downloads, log: record.log, psLog: record.psLog,
  };
  const assertions = checkExpect(state, files);
  const trace = checkTrace(state, files);
  const score: CellScore = {
    key: record.key, state: record.state, theme: record.theme, background: record.background,
    resolution: record.resolution, width: record.width, height: record.height, png: record.png,
    tierA: record.error ? 'fail' : assertions.ok ? (state.expect ? 'pass' : 'no-oracle') : 'fail',
    tierAProblems: record.error ? [record.error, ...assertions.problems] : assertions.problems,
    tierANote: state.tierANote,
    tierC: record.error ? 'fail' : trace.status,
    tierCProblems: record.error ? [record.error] : trace.problems,
    tierCMatched: trace.matched,
    tierB: 'not-captured',
    tierBProblems: [],
    regions: [],
    gatingRegions: [],
    informativeRegions: [],
    ...(record.phase ? { phase: { offsetSeconds: record.phase.offsetSeconds, label: record.phase.label } } : {}),
    claim: '',
    summary: assertions.summary,
    error: record.error,
  };
  if (record.error) {
    score.tierBProblems.push(record.error);
    score.claim = 'not captured';
    return score;
  }
  const reference = referencePathFor(record.key, refs);
  score.reference = reference.path;
  score.referenceKind = reference.kind;
  score.referenceNote = reference.note;
  const provenance = provenancePathFor(record.key, refs);
  if (existsSync(provenance)) score.provenance = provenance;
  if (!existsSync(reference.path)) {
    score.tierB = 'no-reference';
    score.tierBProblems.push(`no reference for this cell (expected ${reference.path} or a registered entry)`);
  } else if (!score.provenance) {
    score.tierB = 'provenance-missing';
    score.tierBProblems.push(`reference ${reference.path} has no provenance sidecar ${provenance}: the cell is scored but cannot pass`);
  } else {
    const image = (() => { try { return decodePng(record.png); } catch (error) { score.tierBProblems.push(`capture: ${String(error)}`); return undefined; } })();
    const ref = (() => { try { return decodePng(reference.path); } catch (error) { score.tierBProblems.push(`reference: ${String(error)}`); return undefined; } })();
    if (!image || !ref) {
      score.tierB = 'fail';
    } else if (image.w !== ref.w || image.h !== ref.h) {
      score.tierB = 'dimension-mismatch';
      score.tierBProblems.push(`reference ${ref.w}x${ref.h} vs capture ${image.w}x${image.h}: nothing is rescaled`);
    } else {
      // A cell fails only when a *gating* region fails. A region annotated contentDiffers is still
      // measured, printed and counted, but it cannot fail the cell; with --strict it gates again, so
      // the pre-annotation number stays reproducible from the same run.
      let gatingFail = false;
      let informativeFail = false;
      for (const region of state.regions) {
        const [x, y, w, h] = region.crop;
        if (x + w > ref.w || y + h > ref.h) {
          score.tierBProblems.push(`${region.name}: crop ${region.crop.join(',')} is outside the ${ref.w}x${ref.h} reference`);
          if (region.required === true || region.name === 'frame') gatingFail = true;
          continue;
        }
        const budget = budgetFor(state, refs, region);
        const p = psnr(ref, image, region.crop as Crop);
        const s = ssim(ref, image, region.crop as Crop);
        const pass = p >= budget.psnrDb && s >= budget.ssim;
        const required = region.required === true || region.name === 'frame';
        const contentDiffers = region.contentDiffers === true;
        const gating = required && (!contentDiffers || strict);
        score.regions.push({
          name: region.name, crop: region.crop, required, gating,
          contentDiffers, contentDiffersReason: region.contentDiffersReason,
          psnr: Number.isFinite(p) ? +p.toFixed(4) : 'Infinity', ssim: +s.toFixed(4),
          minPsnr: budget.psnrDb, minSsim: budget.ssim, pass,
        });
        if (gating) score.gatingRegions.push(region.name);
        if (contentDiffers) score.informativeRegions.push(region.name);
        if (gating && !pass) gatingFail = true;
        else if (required && contentDiffers && !pass) informativeFail = true;
      }
      // The cloth's own band, for the phase sweep. Not a catalogue region and not budgeted: it exists
      // so a phase number can be read where the cloth actually is (PIPELINE.md 4: y 360-840).
      const bandY = Math.min(360, Math.max(0, ref.h - 8));
      const bandH = Math.min(480, ref.h - bandY);
      if (bandH >= 8) {
        const bandPsnr = psnr(ref, image, [0, bandY, ref.w, bandH] as Crop);
        score.ribbonBandPsnr = Number.isFinite(bandPsnr) ? +bandPsnr.toFixed(4) : 'Infinity';
      }
      score.tierB = score.provenance
        ? (gatingFail ? 'fail' : informativeFail ? 'informative' : 'pass')
        : 'provenance-missing';
    }
  }
  const parts: string[] = [];
  for (const [letter, status] of [['A', score.tierA === 'pass'], ['B', score.tierB === 'pass'], ['C', score.tierC === 'pass']] as const)
    if (status) parts.push(letter);
  score.claim = parts.length ? parts.join('+') : 'none';
  return score;
}

// --------------------------------------------------------------------------------------------
// Scorecard: aggregate, JSON, markdown, regression
// --------------------------------------------------------------------------------------------

// One frame of the phase diagnostic: the same cell scored against the same reference at one instant of
// the app's live animation clock. `gate` is that frame's Tier B status; it is reported, never gated.
type PhaseFrame = {
  label: string;
  offsetSeconds: number;
  tierA: string;
  framePsnr: number | string;
  frameSsim: number;
  ribbonBandPsnr: number | string;
  png: string;
  regions?: RegionScore[];
};

type PhaseComparison = {
  state: string;
  // What the numbers are and are not: printed verbatim in the markdown and JSON.
  note: string;
  shipped: PhaseFrame;
  best: PhaseFrame & { deltaDb: number | null };
  phases: PhaseFrame[];
};

type ScoreCard = {
  schema: string;
  generated: string;
  run: string;
  exe: string;
  exeSha256: string;
  flash: string;
  pins: { renderClock: number; uiClock: string; focusTimeMs: number };
  referenceRoot: string;
  budgets: ReferenceDoc['budgets'];
  cells: CellScore[];
  states: Array<{
    state: string;
    title: string;
    class: string;
    reachable: string;
    tierA: string;
    tierB: string;
    tierC: string;
    claim: string;
    // A one-line verdict that keeps the informative/gating split readable: "A pass, B informative".
    verdict: string;
    contentDiffers: number;
    cells: number;
  }>;
  // The scoring-only cloth-phase diagnostic. Separate from cells/totals on purpose: nothing here is a
  // gate, and the shipped-phase entry is the gated number.
  phaseSweep: PhaseComparison[];
  mode: 'informative' | 'strict';
  totals: {
    states: number;
    capturable: number;
    captured: number;
    tierA: number;
    tierB: number;
    tierBInformative: number;
    tierC: number;
    claims: number;
    noReference: number;
    failed: number;
    informative: number;
    gatingRegions: number;
    contentDiffersRegions: number;
    contentDiffersStates: number;
    noFirmwareReference: number;
    firmwareReferenced: number;
    statesWithReference: number;
    coveredFirmware: number;
  };
  missingReferences: string[];
  unavailable: Array<{ state: string; title: string; reason: string }>;
  nativeOnly: Array<{ state: string; title: string; why: string }>;
  // Every content-difference annotation in the catalogue, with its reason. The scorecard prints the
  // whole set, so it is auditable in one place and its count is visible on every run.
  annotations: Array<{ state: string; region: string; reason: string }>;
  limits: string[];
};

function newestRun(outRoot: string) {
  if (!existsSync(outRoot)) fail(`no runs under ${outRoot}; run --capture first`);
  const dirs = readdirSync(outRoot).filter(name => name.startsWith('run-'))
    .map(name => ({ name, path: join(outRoot, name), mtime: statSync(join(outRoot, name)).mtimeMs }))
    .sort((a, b) => b.mtime - a.mtime);
  if (!dirs.length) fail(`no runs under ${outRoot}; run --capture first`);
  return dirs[0].path;
}

function readManifest(runDir: string): RunManifest {
  const path = join(runDir, 'manifest.json');
  if (!existsSync(path)) fail(`${runDir} has no manifest.json`);
  return readJson(path, 'run manifest') as RunManifest;
}

function frameRegion(cell: CellScore) {
  return cell.regions.find(region => region.name === 'frame');
}
function psnrRank(value: number | string | undefined) {
  if (typeof value === 'number') return value;
  return value === 'Infinity' ? Number.POSITIVE_INFINITY : Number.NEGATIVE_INFINITY;
}
function phaseFrameOf(cell: CellScore): PhaseFrame {
  const frame = frameRegion(cell);
  return {
    label: cell.phase?.label ?? 'shipped (--render-clock pinned)',
    offsetSeconds: cell.phase?.offsetSeconds ?? 0,
    tierA: cell.tierA,
    framePsnr: frame ? frame.psnr : 'n/a',
    frameSsim: frame ? frame.ssim : 0,
    ribbonBandPsnr: cell.ribbonBandPsnr ?? 'n/a',
    png: cell.png,
  };
}

function buildScorecard(catalogue: Catalogue, refs: ReferenceDoc, opts: ReturnType<typeof parseArgs>,
  runDir: string, manifest: RunManifest): ScoreCard {
  const byId = new Map(catalogue.states.map(state => [state.id, state]));
  // Phase cells are scored like any other cell but kept out of `cells`: they are a diagnostic, and a
  // reader must not find a swept number in the gated list.
  const cells: CellScore[] = [];
  const phaseCells: CellScore[] = [];
  for (const record of manifest.cells) {
    const state = byId.get(record.state);
    if (!state) fail(`run cell ${record.key} names a state the catalogue does not define`);
    const scored = scoreCell(state, record, refs, opts.strict);
    if (record.phase) phaseCells.push(scored);
    else cells.push(scored);
  }
  const states: ScoreCard['states'] = [];
  for (const state of catalogue.states) {
    const own = cells.filter(cell => cell.state === state.id);
    if (!own.length) continue;
    // A state's Tier status is the canonical cell's; a state whose other cells pass while the
    // canonical one fails is still not parity, so the worst status of the captured cells wins.
    // informative ranks between n/a and pass: it is the absence of a gating failure, not a pass.
    const order = ['fail', 'provenance-missing', 'dimension-mismatch', 'no-reference', 'no-oracle', 'n/a', 'informative', 'pass'];
    const worst = (key: 'tierA' | 'tierB' | 'tierC') =>
      own.map(cell => cell[key] as string).sort((a, b) => order.indexOf(a) - order.indexOf(b))[0];
    const tierA = worst('tierA'), tierB = worst('tierB'), tierC = worst('tierC');
    const claim = [tierA === 'pass' ? 'A' : '', tierB === 'pass' ? 'B' : '', tierC === 'pass' ? 'C' : ''].filter(Boolean).join('+');
    // The verdict line is what stops "informative" reading as a bare FAIL: it spells out which tiers
    // passed and that B is informative rather than failed.
    const verdictParts: string[] = [
      tierA === 'pass' ? 'A pass' : `A ${tierA}`,
      tierB === 'pass' ? 'B pass' : tierB === 'informative' ? 'B informative'
        : tierB === 'no-reference' ? 'B no reference' : `B ${tierB}`,
      tierC === 'pass' ? 'C pass' : tierC === 'n/a' ? 'C n/a' : `C ${tierC}`,
    ];
    states.push({
      state: state.id, title: state.title, class: state.class, reachable: state.native.reachable,
      tierA, tierB, tierC, claim: claim || 'none', verdict: verdictParts.join(', '),
      contentDiffers: own.reduce((sum, cell) => sum + cell.informativeRegions.length, 0), cells: own.length,
    });
  }
  const capturable = catalogue.states.filter(state => state.captures !== false).length;
  const captured = new Set(cells.filter(cell => !cell.error).map(cell => cell.state)).size;
  const nativeOnly = catalogue.states.filter(state => state.noFirmwareReference === true);
  const nativeOnlyIds = new Set(nativeOnly.map(state => state.id));
  const firmwareReferenced = capturable - nativeOnly.filter(state => state.captures !== false).length;
  // A state counts as referenced when at least one of its cells has a reference at all (Tier B measured),
  // which is the "does this state have a frame anywhere" question a cell count cannot answer. The run only
  // speaks for the states it covered, so the fraction is per run and the catalogue totals are reported
  // beside it - a 10-state run must not read as "0 of 30".
  const coveredFirmware = new Set(cells.filter(cell => !nativeOnlyIds.has(cell.state)).map(cell => cell.state)).size;
  const statesWithReference = new Set(cells.filter(cell => cell.tierB !== 'no-reference' && cell.tierB !== 'not-captured').map(cell => cell.state)).size;
  const totals = {
    states: catalogue.states.length,
    capturable,
    captured,
    tierA: states.filter(state => state.tierA === 'pass').length,
    tierB: states.filter(state => state.tierB === 'pass').length,
    tierBInformative: states.filter(state => state.tierB === 'informative').length,
    tierC: states.filter(state => state.tierC === 'pass').length,
    claims: states.filter(state => state.claim === 'A+B+C').length,
    noReference: cells.filter(cell => cell.tierB === 'no-reference').length,
    failed: cells.filter(cell => cell.tierA === 'fail' || cell.tierB === 'fail' || cell.tierC === 'fail').length,
    informative: cells.filter(cell => cell.tierB === 'informative').length,
    gatingRegions: cells.reduce((sum, cell) => sum + cell.gatingRegions.length, 0),
    // Catalogue-wide, not run-scoped: the annotation set is a property of the contract, and this is
    // the number a reader can watch so it cannot grow silently.
    contentDiffersRegions: catalogue.states.reduce((sum, state) =>
      sum + state.regions.filter(region => region.contentDiffers === true).length, 0),
    contentDiffersStates: catalogue.states.filter(state =>
      state.regions.some(region => region.contentDiffers === true)).length,
    noFirmwareReference: nativeOnly.length,
    firmwareReferenced,
    statesWithReference,
    coveredFirmware,
  };
  // The phase diagnostic: per swept state, the shipped-phase score beside the best live-clock phase.
  const phaseSweep: PhaseComparison[] = [];
  for (const state of catalogue.states) {
    const shipped = cells.find(cell => cell.state === state.id && cell.regions.length > 0);
    const phases = phaseCells.filter(cell => cell.state === state.id && cell.regions.length > 0);
    if (!shipped || !phases.length) continue;
    // A phase whose Tier A failed is not a phase of the same page, so it cannot be the best match.
    const usable = phases.filter(cell => cell.tierA !== 'fail');
    if (!usable.length) continue;
    let bestCell = usable[0];
    for (const candidate of usable.slice(1)) {
      const rank = psnrRank(frameRegion(candidate)?.psnr);
      const bestRank = psnrRank(frameRegion(bestCell)?.psnr);
      if (rank > bestRank ||
        (rank === bestRank && (frameRegion(candidate)?.ssim ?? 0) > (frameRegion(bestCell)?.ssim ?? 0)))
        bestCell = candidate;
    }
    const shippedFrame = {
      ...phaseFrameOf(shipped),
      label: `shipped (render clock ${manifest.pins.renderClock} s)`,
    };
    const bestFrame = phaseFrameOf(bestCell);
    const shippedPsnr = psnrRank(frameRegion(shipped)?.psnr);
    const bestPsnr = psnrRank(frameRegion(bestCell)?.psnr);
    phaseSweep.push({
      state: state.id,
      note: 'Scoring-only, never a gate. Under the shipped --render-clock pin the ribbon cloth is '
        + 'frozen at its settle phase, while the console reference holds an arbitrary instant of the '
        + 'same 22 s swell (PIPELINE.md "Exposure and ribbon phase"); two instants compared as one are '
        + 'not a fidelity measurement. Each phase cell below re-captures the same state on the app\'s '
        + 'live animation clock with a scripted pause before the grab. Best is the phase with the '
        + 'highest whole-frame PSNR; the gate above uses the shipped cell only. Do not quote this '
        + 'number as a gate.',
      shipped: shippedFrame,
      best: {
        ...bestFrame,
        regions: bestCell.regions,
        deltaDb: Number.isFinite(shippedPsnr) && Number.isFinite(bestPsnr)
          ? +(bestPsnr - shippedPsnr).toFixed(3) : null,
      },
      phases: phases.map(cell => phaseFrameOf(cell)),
    });
  }
  // The reference debt excludes the native-only surfaces: a firmware frame for them must not exist.
  const missingReferences = cells.filter(cell => cell.tierB === 'no-reference' && !nativeOnlyIds.has(cell.state)).map(cell => cell.key);
  const unavailable = catalogue.states.filter(state => state.captures === false)
    .map(state => ({ state: state.id, title: state.title, reason: state.native.reason ?? state.native.how ?? '' }));
  const nativeOnlyList = nativeOnly.map(state => ({
    state: state.id, title: state.title,
    why: state.xmbmlNote ?? 'native surface: no firmware view defines this page',
  }));
  return {
    schema: 'xmb-parity-scorecard/1',
    generated: isoNow(),
    run: runDir,
    exe: manifest.exe,
    exeSha256: manifest.exeSha256,
    flash: manifest.flash,
    pins: manifest.pins,
    referenceRoot: resolveRepo(refs.root),
    budgets: refs.budgets,
    cells,
    states,
    phaseSweep,
    mode: opts.strict ? 'strict' : 'informative',
    totals,
    missingReferences,
    unavailable,
    nativeOnly: nativeOnlyList,
    annotations: catalogue.states.flatMap(state => state.regions
      .filter(region => region.contentDiffers === true)
      .map(region => ({ state: state.id, region: region.name, reason: region.contentDiffersReason! }))),
    limits: [
      'A Tier B number is a perceptual distance to an emulator frame, not bit-exactness: the console\'s RSX rasterisation is only partly recoverable (PARITY-PHASES "What this track will still not deliver").',
      'A state is called 1:1 only when Tier A, Tier B and Tier C all pass. A+ B numbers alone are not parity.',
      'Tier A passes on the app\'s own dumps (XMB_DUMP_ENTRIES, the grab sidecar, XMB_LOG). A state whose surface has no dump or sidecar field (the OSK, the controller indicator, the message pages) cannot pass Tier A for that surface, and the scorecard says so instead of treating the picture as proof.',
      'Tier B budgets are provisional targets until a matched RPCS3 reference exists (work/parity-reference.json budgets.revisionNote).',
      'The capture reads the host\'s Steam library and the host\'s Steam settings where a state includes them, so a changed library fails the dump assertions loudly instead of quietly scoring a different state.',
      '720p cells are a 1280x720 windowed framebuffer: the host has no 720p display mode, so the console\'s own 720p projection is P10, not this cell.',
      'States that carry noFirmwareReference are native surfaces with no firmware counterpart: they are captured and gated like any other state, but a firmware reference for them must not exist, so they are excluded from the reference debt (totals.firmwareReferenced).',
      'Regions annotated contentDiffers in work/parity-catalogue.json are measured but not gating: their content is a console account/service/row set the native build cannot reproduce from the same frame, so charging them as fidelity failures measured the content source, not the renderer. They are counted and listed on every scorecard; --strict gates them again, so the informative set cannot grow silently.',
      'The ribbon-phase sweep is scoring-only. The pinned render clock freezes the cloth at its settle phase while the reference holds an arbitrary instant of a 22 s swell, so the best-matching phase is reported beside the shipped-phase score for diagnosis; no gate, total or claim uses it.',
    ],
  };
}

function statusCell(value: string) {
  return value === 'pass' ? 'PASS' : value === 'fail' ? 'FAIL' : value === 'informative' ? 'INFORMATIVE'
    : value === 'n/a' ? 'n/a' : value;
}

function renderMarkdown(card: ScoreCard, refs: ReferenceDoc): string {
  const lines: string[] = [];
  const budgets = refs.budgets;
  lines.push('# Parity scorecard');
  lines.push('');
  lines.push(`Generated ${card.generated} from run \`${card.run}\`.`);
  lines.push('');
  lines.push('| input | value |');
  lines.push('|---|---|');
  lines.push(`| native executable | \`${card.exe}\` |`);
  lines.push(`| executable sha256 | \`${card.exeSha256}\` |`);
  lines.push(`| dev_flash | \`${card.flash}\` |`);
  lines.push(`| render clock / UI clock / focus age | ${card.pins.renderClock} s / ${card.pins.uiClock} / ${card.pins.focusTimeMs} ms |`);
  lines.push(`| reference root | \`${card.referenceRoot}\` |`);
  lines.push(`| budget policy | updated ${budgets.updated}${budgets.provisional ? ' (provisional)' : ''} |`);
  lines.push('');
  lines.push(`**${card.totals.claims}/${card.totals.capturable} capturable states pass all three tiers.** `
    + `Tier A ${card.totals.tierA} pass, Tier B ${card.totals.tierB} pass`
    + `${card.totals.tierBInformative ? ` / ${card.totals.tierBInformative} informative` : ''}, `
    + `Tier C ${card.totals.tierC} pass; ${card.totals.noReference} captured cell(s) have no reference yet; `
    + `${card.totals.failed} gating cell failure(s).`);
  lines.push('');
  lines.push(`**Content-difference annotations (${card.mode === 'strict' ? 'STRICT: gated' : 'informative: not gating'}):** `
    + `${card.totals.contentDiffersRegions} region(s) across ${card.totals.contentDiffersStates} state(s) are annotated `
    + `\`contentDiffers\` in work/parity-catalogue.json; this run scores ${card.totals.informative} cell(s) as B informative. `
    + (card.mode === 'strict'
      ? 'Strict mode gates every annotated region, reproducing the pre-annotation numbers.'
      : 'The strict gate is available with `--strict`; the annotated set is listed in full below.'));
  lines.push('');
  lines.push(`**Reference coverage (this run):** ${card.totals.statesWithReference}/${card.totals.coveredFirmware} firmware-referenced states `
    + `covered by the run pair with a reference; the catalogue has ${card.totals.firmwareReferenced} firmware-referenced states `
    + `and ${card.totals.noFirmwareReference} native-only ones that owe none (${card.totals.states} states total).`);
  lines.push('');
  lines.push('## Tier B budgets (per screen class)');
  lines.push('');
  lines.push('| class | frame PSNR / SSIM | region PSNR / SSIM |');
  lines.push('|---|---:|---:|');
  for (const [name, budget] of Object.entries(budgets.classes)) {
    lines.push(`| ${name} | ${(budget.wholeFramePsnrDb ?? budget.psnrDb).toFixed(2)} dB / ${(budget.wholeFrameSsim ?? budget.ssim).toFixed(4)} `
      + `| ${budget.psnrDb.toFixed(2)} dB / ${budget.ssim.toFixed(4)} |`);
  }
  lines.push('');
  lines.push('## Per state');
  lines.push('');
  lines.push('| state | class | reachable | A | B | C | verdict | claim | content-differs regions |');
  lines.push('|---|---|---|---|---|---|---|---|---:|');
  for (const state of card.states) {
    lines.push(`| ${state.state} | ${state.class} | ${state.reachable} | ${statusCell(state.tierA)} | ${statusCell(state.tierB)} `
      + `| ${statusCell(state.tierC)} | ${state.verdict} | ${state.claim} | ${state.contentDiffers} |`);
  }
  lines.push('');
  const scored = card.cells.filter(cell => cell.regions.length);
  if (scored.length) {
    lines.push('## Tier B region detail');
    lines.push('');
    for (const cell of scored) {
      lines.push(`### ${cell.key}`);
      lines.push('');
      if (cell.reference) lines.push(`Reference \`${cell.reference}\` (${cell.referenceKind ?? 'rpcs3'}).`);
      if (cell.referenceNote) lines.push('', cell.referenceNote);
      lines.push('');
      lines.push('| region | crop | PSNR | SSIM | budget | gate | note |');
      lines.push('|---|---|---:|---:|---:|---|---|');
      for (const region of cell.regions) {
        const label = region.contentDiffers ? ' [content differs]' : '';
        const gate = region.contentDiffers
          ? region.gating
            ? `${region.pass ? 'PASS' : 'FAIL'} (strict: gated)`
            : `${region.pass ? 'PASS' : 'FAIL'} (informative)`
          : region.required
            ? `${region.pass ? 'PASS' : 'FAIL'}`
            : `${region.pass ? 'PASS' : 'FAIL'} (not gating)`;
        lines.push(`| ${region.name}${label} | ${region.crop.join(',')} `
          + `| ${typeof region.psnr === 'number' ? region.psnr.toFixed(2) : region.psnr} | ${region.ssim.toFixed(4)} `
          + `| ${region.minPsnr.toFixed(2)} dB / ${region.minSsim.toFixed(4)} | ${gate} | ${region.contentDiffersReason ?? ''} |`);
      }
      lines.push('');
    }
  }
  // The ribbon-phase diagnostic sits beside the gate and is explicitly labelled as scoring-only.
  if (card.phaseSweep.length) {
    lines.push('## Ribbon-phase sweep (scoring only - not a gate)');
    lines.push('');
    lines.push(card.phaseSweep[0].note);
    lines.push('');
    lines.push('| state | shipped phase | shipped frame | shipped ribbon band | best swept phase | best frame | best ribbon band | delta PSNR |');
    lines.push('|---|---|---:|---:|---|---:|---:|---:|');
    const shown = (value: number | string) => typeof value === 'number' ? value.toFixed(2) : value;
    for (const comparison of card.phaseSweep) {
      lines.push(`| ${comparison.state} | ${comparison.shipped.label} `
        + `| ${shown(comparison.shipped.framePsnr)} dB / ${comparison.shipped.frameSsim.toFixed(4)} `
        + `| ${shown(comparison.shipped.ribbonBandPsnr)} dB `
        + `| ${comparison.best.label} | ${shown(comparison.best.framePsnr)} dB / ${comparison.best.frameSsim.toFixed(4)} `
        + `| ${shown(comparison.best.ribbonBandPsnr)} dB `
        + `| ${comparison.best.deltaDb === null ? 'n/a' : `${comparison.best.deltaDb >= 0 ? '+' : ''}${comparison.best.deltaDb.toFixed(2)}`} dB |`);
    }
    lines.push('');
    for (const comparison of card.phaseSweep) {
      lines.push(`### ${comparison.state} phases`);
      lines.push('');
      lines.push('| phase | frame PSNR / SSIM | ribbon band PSNR | Tier A |');
      lines.push('|---|---:|---:|---|');
      for (const phase of comparison.phases) {
        lines.push(`| ${phase.label} | ${shown(phase.framePsnr)} dB / ${phase.frameSsim.toFixed(4)} `
          + `| ${shown(phase.ribbonBandPsnr)} dB | ${phase.tierA} |`);
      }
      lines.push('');
    }
    lines.push('**Gate rule:** the gate above uses only the shipped cell (the pinned render clock). A swept phase is');
    lines.push('a correlation probe: it may be quoted as "the best phase scores X", never as a parity gate, and the');
    lines.push('shipped-phase score stays the number the gate is computed from.');
    lines.push('');
  }
  // Every annotation in the catalogue is printed, whether or not this run captured the state: the set is
  // a property of the contract, and printing it is what stops it growing silently.
  if (card.annotations.length) {
    lines.push('## Content-difference regions (informative)');
    lines.push('');
    lines.push('These regions are annotated `contentDiffers: true` in work/parity-catalogue.json: the reference');
    lines.push('frame holds console content the native build cannot reproduce from the same frame, so the region is');
    lines.push('measured and reported but never gating. `--strict` gates them again (the pre-annotation numbers).');
    lines.push('');
    lines.push('| state | region | this run | why it differs |');
    lines.push('|---|---|---|---|');
    for (const annotation of card.annotations) {
      const cell = card.cells.find(candidate => candidate.state === annotation.state &&
        candidate.regions.some(region => region.name === annotation.region));
      const region = cell?.regions.find(candidate => candidate.name === annotation.region);
      const measured = region
        ? `${typeof region.psnr === 'number' ? region.psnr.toFixed(2) : region.psnr} dB / ${region.ssim.toFixed(4)} `
          + `(${region.pass ? 'would pass' : 'would fail'} its budget, ${region.gating ? 'gated by --strict' : 'informative'})`
        : 'not scored in this run';
      lines.push(`| ${annotation.state} | ${annotation.region} | ${measured} | ${annotation.reason} |`);
    }
    lines.push('');
  }
  const detail = card.cells.filter(cell => !cell.regions.length);
  if (detail.length) {
    // Grouped by status: 24 identical "no reference yet" rows say less than one line that names them all.
    const groups = new Map<string, { cells: string[]; reason: string }>();
    for (const cell of detail) {
      const key = cell.tierB;
      if (!groups.has(key)) groups.set(key, { cells: [], reason: cell.tierBProblems.join('; ') || cell.tierAProblems.join('; ') });
      groups.get(key)!.cells.push(cell.key);
    }
    lines.push('## Cells without a Tier B score');
    lines.push('');
    lines.push('| status | cells | why |');
    lines.push('|---|---:|---|');
    for (const [status, group] of groups) {
      const shown = group.cells.length > 6
        ? `${group.cells.length} cells (${group.cells.slice(0, 6).join(', ')}, ...)`
        : `${group.cells.length} cell(s): ${group.cells.join(', ')}`;
      lines.push(`| ${status} | ${group.cells.length} | ${shown}. ${status === 'no-reference' ? 'No RPCS3 reference exists for these cells yet; see section "What lacks a reference".' : group.reason} |`);
    }
    lines.push('');
  }
  const problems = card.cells.filter(cell => cell.tierAProblems.length || cell.tierCProblems.length);
  if (problems.length) {
    lines.push('## Tier A / Tier C findings');
    lines.push('');
    for (const cell of problems) {
      lines.push(`- **${cell.key}**`);
      for (const problem of cell.tierAProblems) lines.push(`  - Tier A: ${problem}`);
      for (const problem of cell.tierCProblems) lines.push(`  - Tier C: ${problem}`);
      if (cell.tierANote) lines.push(`  - scope: ${cell.tierANote}`);
    }
    lines.push('');
  }
  if (card.unavailable.length) {
    lines.push('## Catalogue states that are not captured');
    lines.push('');
    lines.push('| state | title | why |');
    lines.push('|---|---|---|');
    for (const state of card.unavailable) lines.push(`| ${state.state} | ${state.title} | ${state.reason} |`);
    lines.push('');
  }
  if (card.nativeOnly.length) {
    lines.push('## Native-only surfaces (no firmware reference by design)');
    lines.push('');
    lines.push('These states are captured and gated like any other, but no firmware surface defines them, so a'
      + ' firmware reference must not exist and they are excluded from the reference debt above.');
    lines.push('');
    lines.push('| state | title | why |');
    lines.push('|---|---|---|');
    for (const state of card.nativeOnly) lines.push(`| ${state.state} | ${state.title} | ${state.why} |`);
    lines.push('');
  }
  lines.push('## Limits of this scorecard');
  lines.push('');
  for (const limit of card.limits) lines.push(`- ${limit}`);
  lines.push('');
  return lines.join('\n');
}

// --------------------------------------------------------------------------------------------
// Regression: same run shape, compared cell by cell and region by region.
// --------------------------------------------------------------------------------------------

type Regression = {
  schema: string;
  generated: string;
  current: string;
  previous: string;
  pass: boolean;
  states: Array<{ state: string; before: string; after: string; pass: boolean }>;
  regions: Array<{ cell: string; region: string; before: number | string; after: number | string; deltaPsnrDb: number | null;
    beforeSsim: number; afterSsim: number; deltaSsim: number; pass: boolean }>;
  findings: string[];
};

function compareScorecards(current: ScoreCard, previous: ScoreCard): Regression {
  const findings: string[] = [];
  const states: Regression['states'] = [];
  const regions: Regression['regions'] = [];
  // Claim is the strict letter set; the Tier B status breaks ties so informative -> fail is a finding
  // even though both carry the same claim letter (A without B).
  const rank = (state: ScoreCard['states'][number]) => {
    const claim = state.claim === 'none' ? 0 : state.claim.split('+').length * 10 + (state.claim.includes('A') ? 1 : 0);
    const tierB = state.tierB === 'pass' ? 3 : state.tierB === 'informative' ? 2 : state.tierB === 'fail' ? 0 : 1;
    return claim * 10 + tierB;
  };
  const previousStates = new Map(previous.states.map(state => [state.state, state]));
  for (const state of current.states) {
    const before = previousStates.get(state.state);
    if (!before) {
      findings.push(`${state.state}: new state (no previous entry); not a regression`);
      continue;
    }
    const lost = rank(state) < rank(before);
    if (lost) findings.push(`${state.state}: verdict fell from "${before.verdict ?? before.claim}" to "${state.verdict}"`);
    states.push({ state: state.state, before: before.claim, after: state.claim, pass: !lost });
  }
  const previousCells = new Map(previous.cells.map(cell => [cell.key, cell]));
  for (const cell of current.cells) {
    const before = previousCells.get(cell.key);
    if (!before) continue;
    if (cell.tierB === 'no-reference' && before.tierB !== 'no-reference')
      findings.push(`${cell.key}: the reference that existed before is missing now`);
    if (cell.reference !== before.reference && cell.tierB !== 'no-reference')
      findings.push(`${cell.key}: reference moved from ${before.reference} to ${cell.reference}`);
    for (const region of cell.regions) {
      const old = before.regions.find(candidate => candidate.name === region.name);
      if (!old) continue;
      // An annotation added between runs changes the gate; say so instead of hiding it behind a
      // better score. This is the regression-mode half of "the informative set cannot grow silently".
      if ((old.contentDiffers === true) !== (region.contentDiffers === true))
        findings.push(`${cell.key} / ${region.name}: contentDiffers changed `
          + `${old.contentDiffers === true ? 'true' : 'false'} -> ${region.contentDiffers === true ? 'true' : 'false'} (gate scope changed)`);
      const oldPsnr = typeof old.psnr === 'number' ? old.psnr : Infinity;
      const newPsnr = typeof region.psnr === 'number' ? region.psnr : Infinity;
      const deltaPsnr = Number.isFinite(oldPsnr) && Number.isFinite(newPsnr) ? newPsnr - oldPsnr : null;
      const deltaSsim = region.ssim - old.ssim;
      const ok = (deltaPsnr === null || deltaPsnr >= -regressionBudget.psnrDb) && deltaSsim >= -regressionBudget.ssim;
      if (!ok) findings.push(`${cell.key} / ${region.name}: PSNR ${fmtDb(oldPsnr)} -> ${fmtDb(newPsnr)} dB, `
        + `SSIM ${fmtSsim(old.ssim)} -> ${fmtSsim(region.ssim)} (budget ${regressionBudget.psnrDb} dB / ${regressionBudget.ssim})`);
      regions.push({
        cell: cell.key, region: region.name, before: old.psnr, after: region.psnr,
        deltaPsnrDb: deltaPsnr === null ? null : +deltaPsnr.toFixed(4),
        beforeSsim: old.ssim, afterSsim: region.ssim, deltaSsim: +deltaSsim.toFixed(4), pass: ok,
      });
    }
  }
  return {
    schema: 'xmb-parity-regression/1',
    generated: isoNow(),
    current: current.run,
    previous: previous.run,
    pass: findings.length === 0,
    states,
    regions,
    findings,
  };
}

// --------------------------------------------------------------------------------------------
// Argument parsing
// --------------------------------------------------------------------------------------------

type Options = {
  command: string;
  json: boolean;
  matrix: boolean;
  state: string[];
  theme: string[];
  background: string[];
  resolution: string[];
  run: string;
  previous: string;
  markdown: string;
  out: string;
  exe: string;
  flash: string;
  steamRoot: string;
  renderClock: number;
  uiClock: string;
  focusTime: number;
  timeoutMs: number;
  strict: boolean;
  phaseSweep: number[];
  phaseState: string[];
};

function listArg(values: string[]) {
  return values.flatMap(value => value.split(',')).map(value => value.trim()).filter(Boolean);
}

function secondsList(values: string[], label: string) {
  return listArg(values).map(value => {
    const number = Number(value);
    if (!Number.isFinite(number) || number < 0 || number > 600)
      fail(`${label} must list seconds from 0 through 600 (the app's script pause is bounded at 600000 ms)`);
    return number;
  });
}

function parseArgs(argv: string[]): Options {
  const opts: Options = {
    command: '',
    json: false, matrix: false,
    state: [], theme: [], background: [], resolution: [],
    run: '', previous: '', markdown: '',
    out: defaultOutRoot,
    exe: defaultExe,
    flash: defaultFlash,
    steamRoot: '',
    renderClock: defaultRenderClock,
    uiClock: defaultUiClock,
    focusTime: defaultFocusTimeMs,
    timeoutMs: defaultTimeoutMs,
    strict: false,
    phaseSweep: secondsList([process.env.PARITY_PHASE_SWEEP ?? ''], 'PARITY_PHASE_SWEEP'),
    phaseState: listArg([process.env.PARITY_PHASE_STATE ?? 'root']),
  };
  const commands = new Set(['--catalogue', '--list', '--matrix', '--capture', '--score', '--regression', '--references-missing']);
  const valued = new Set(['--state', '--theme', '--background', '--resolution', '--run', '--previous', '--markdown',
    '--out', '--exe', '--flash', '--steam-root', '--render-clock', '--ui-clock', '--focus-time', '--timeout-ms',
    '--phase-sweep', '--phase-state']);
  const seen = new Set<string>();
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index];
    if (arg === '--help' || arg === '-h') { console.log(usage); process.exit(0); }
    if (arg === '--json') { opts.json = true; continue; }
    if (arg === '--strict') { opts.strict = true; continue; }
    // --matrix is both a command (print the plan) and a --capture flag (capture every axis pair).
    if (arg === '--matrix') {
      if (opts.command) opts.matrix = true;
      else opts.command = '--matrix';
      continue;
    }
    if (commands.has(arg)) {
      if (opts.command && !(opts.command === '--capture' && arg === '--score'))
        fail(`${arg} does not combine with ${opts.command}; --capture may be combined with --score only`);
      opts.command = opts.command === '--capture' && arg === '--score' ? '--capture --score' : arg;
      continue;
    }
    if (!arg.startsWith('--')) fail(`unexpected argument ${JSON.stringify(arg)};\n${usage}`);
    if (!valued.has(arg)) fail(`unknown option ${arg};\n${usage}`);
    if (seen.has(arg)) fail(`repeated option ${arg}`);
    seen.add(arg);
    const value = argv[++index];
    if (value === undefined || value.startsWith('--')) fail(`${arg} needs a value`);
    if (arg === '--state') opts.state = listArg([value]);
    else if (arg === '--theme') opts.theme = listArg([value]);
    else if (arg === '--background') opts.background = listArg([value]);
    else if (arg === '--resolution') opts.resolution = listArg([value]);
    else if (arg === '--run') opts.run = resolve(value);
    else if (arg === '--previous') opts.previous = resolve(value);
    else if (arg === '--markdown') opts.markdown = resolve(value);
    else if (arg === '--out') opts.out = resolve(value);
    else if (arg === '--exe') opts.exe = value;
    else if (arg === '--flash') opts.flash = value;
    else if (arg === '--steam-root') opts.steamRoot = value;
    else if (arg === '--render-clock') {
      const number = Number(value);
      if (!Number.isFinite(number) || number < 0) fail('--render-clock must be a non-negative number');
      opts.renderClock = number;
    } else if (arg === '--ui-clock') {
      if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}$/.test(value)) fail('--ui-clock must be an ISO local date-time');
      opts.uiClock = value;
    } else if (arg === '--focus-time') {
      const number = Number(value);
      if (!Number.isSafeInteger(number) || number < 0) fail('--focus-time must be a non-negative integer');
      opts.focusTime = number;
    } else if (arg === '--timeout-ms') {
      const number = Number(value);
      if (!Number.isSafeInteger(number) || number < 1 || number > 3600000) fail('--timeout-ms must be 1 through 3600000');
      opts.timeoutMs = number;
    } else if (arg === '--phase-sweep') {
      opts.phaseSweep = secondsList([value], '--phase-sweep');
      if (!opts.phaseSweep.length) fail('--phase-sweep needs at least one offset in seconds');
    } else if (arg === '--phase-state') {
      opts.phaseState = listArg([value]);
      if (!opts.phaseState.length) fail('--phase-state needs at least one state id');
    }
  }
  if (!opts.command) fail(`no command given;\n${usage}`);
  // --matrix is a command of its own (print the plan) and a flag for --capture (capture every axis
  // combination). It also widens --references-missing to the whole matrix, which is the P0.3 inventory.
  if (opts.command !== '--matrix' && opts.command !== '--capture' && opts.command !== '--capture --score' &&
    opts.command !== '--references-missing' && opts.matrix)
    fail('--matrix is only a command, a --capture flag, or a --references-missing flag');
  // Phases exist only as captures; --score reads whatever a run recorded. Saying so is cheaper than
  // silently writing a scorecard with no phase section.
  if (opts.phaseSweep.length && opts.command !== '--capture' && opts.command !== '--capture --score')
    fail('--phase-sweep is a capture option; use it with --capture (a --score run reads the phases it recorded)');
  if (!opts.phaseSweep.length) opts.phaseState = [];
  return opts;
}

let quiet = false;

// --------------------------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------------------------

function reportCatalogue(catalogue: Catalogue, refs: ReferenceDoc, asJson: boolean) {
  const flash = catalogue.flash ?? defaultFlash;
  const xmbmlRoot = catalogue.xmbmlRoot ?? 'vsh/resource/explore/xmb';
  const problems: string[] = [];
  if (existsSync(flash)) {
    for (const state of catalogue.states) {
      if (!state.xmbml) continue;
      const views = xmbmlViews(flash, xmbmlRoot, state.xmbml.document);
      if (!views) problems.push(`${state.id}: XMBML document ${state.xmbml.document} is missing from ${flash}`);
      else if (!views.has(state.xmbml.view))
        problems.push(`${state.id}: ${state.xmbml.document} defines no view ${JSON.stringify(state.xmbml.view)}`);
    }
  }
  if (asJson) {
    console.log(JSON.stringify({
      schema: 'xmb-parity-catalogue-report/1',
      catalogue: cataloguePath,
      flash,
      xmbmlRoot,
      xmbmlChecked: existsSync(flash),
      inventory: catalogue.xmbmlInventory,
      states: catalogue.states.map(state => ({
        id: state.id, title: state.title, class: state.class, pageModel: state.pageModel,
        xmbml: state.xmbml, native: state.native, captures: state.captures !== false,
        noFirmwareReference: state.noFirmwareReference === true,
        route: state.route ?? null, regions: state.regions, expect: state.expect ?? null,
        trace: state.trace ?? [], tierANote: state.tierANote,
      })),
      xmbmlProblems: problems,
    }, null, 2));
  } else {
    console.log(`parity catalogue: ${cataloguePath}`);
    console.log(`  states     ${catalogue.states.length} (${catalogue.states.filter(s => s.captures !== false).length} capturable today, `
      + `${catalogue.states.filter(s => s.noFirmwareReference === true).length} native-only with no firmware reference)`);
    console.log(`  XMBML      ${catalogue.xmbmlInventory?.documents ?? '?'} documents, `
      + `${catalogue.xmbmlInventory?.views ?? '?'} views, ${catalogue.xmbmlInventory?.items ?? '?'} item/query definitions`);
    console.log(`  flash      ${flash}${existsSync(flash) ? '' : ' (absent: XMBML cross-check skipped)'}`);
    const annotations = catalogue.states.flatMap(state => state.regions
      .filter(region => region.contentDiffers === true)
      .map(region => ({ state: state.id, region: region.name, reason: region.contentDiffersReason! })));
    console.log(`  content    ${annotations.length} region(s) annotated contentDiffers (informative, never gating; --strict gates them)`);
    console.log('');
    const idWidth = Math.max(4, ...catalogue.states.map(state => state.id.length));
    console.log(`${pad('state', idWidth, true)}  ${pad('class', 11, true)}  ${pad('XMBML view', 34, true)}  ${pad('reach', 12, true)}  regions`);
    console.log(`${'-'.repeat(idWidth)}  ${'-'.repeat(11)}  ${'-'.repeat(34)}  ${'-'.repeat(12)}  ${'-'.repeat(40)}`);
    for (const state of catalogue.states) {
      const view = state.xmbml ? `${state.xmbml.document}#${state.xmbml.view}` : '(native, no firmware view)';
      const regions = state.regions.map(region => region.name + (region.contentDiffers ? '*' : '')).join(', ');
      console.log(`${pad(state.id, idWidth, true)}  ${pad(state.class, 11, true)}  ${pad(view, 34, true)}  `
        + `${pad(state.native.reachable, 12, true)}  ${regions}`);
    }
    console.log('');
    for (const state of catalogue.states) {
      const route = state.route
        ? [state.route.column ? `column ${state.route.column}` : 'column game',
           ...(state.route.stages ?? []).map(stage => stage.catalogueId ? `open ${stage.catalogueId}` : `row ${stage.rowIndex}`),
           ...(state.route.preGrab ?? []).length ? [`then ${(state.route.preGrab ?? []).join(' ')}`] : []].join(' > ')
        : 'no input';
      console.log(`  ${pad(state.id, idWidth, true)}  ${route}`);
      if (state.noFirmwareReference === true)
        console.log(`  ${' '.repeat(idWidth)}  native-only: no firmware surface defines this page, so no firmware reference is owed`);
      if (state.native.reason) console.log(`  ${' '.repeat(idWidth)}  why: ${state.native.reason}`);
      if (state.tierANote) console.log(`  ${' '.repeat(idWidth)}  scope: ${state.tierANote}`);
    }
    if (annotations.length) {
      console.log('');
      console.log('  content-difference regions (* above): measured, printed, never gating unless --strict');
      for (const annotation of annotations)
        console.log(`    ${pad(annotation.state, 24, true)}  ${pad(annotation.region, 18, true)}  ${annotation.reason}`);
    }
    if (problems.length) {
      console.log('');
      console.log('XMBML cross-check problems:');
      for (const problem of problems) console.log(`  - ${problem}`);
    }
  }
  if (problems.length) process.exitCode = 2;
}

function reportMatrix(catalogue: Catalogue, refs: ReferenceDoc, cells: Cell[], asJson: boolean) {
  if (asJson) {
    console.log(JSON.stringify({
      schema: 'xmb-parity-matrix/1',
      themes: refs.matrix.themes, backgrounds: refs.matrix.backgrounds, resolutions: refs.matrix.resolutions,
      canonical: refs.matrix.canonical,
      cells: cells.map(cell => ({
        key: cell.key, state: cell.state, theme: cell.theme.id, background: cell.background.id,
        backgroundClock: cell.background.clock, resolution: cell.resolution.id, sizeToken: cell.resolution.token,
      })),
      count: cells.length,
    }, null, 2));
    return;
  }
  const stateCount = new Set(cells.map(cell => cell.state)).size;
  console.log(`capture matrix: ${cells.length} cells = ${stateCount} states x `
    + `${new Set(cells.map(cell => cell.theme)).size} themes x `
    + `${new Set(cells.map(cell => cell.background)).size} backgrounds x `
    + `${new Set(cells.map(cell => cell.resolution)).size} resolutions`);
  console.log(`  reference root  ${resolveRepo(refs.root)}`);
  console.log(`  canonical       ${refs.matrix.canonical.theme} / ${refs.matrix.canonical.background} / ${refs.matrix.canonical.resolution}`);
  console.log('');
  for (const cell of cells) {
    console.log(`  ${pad(cell.key, 56, true)}  bg ${cell.background.clock}  ${pad(`${cell.resolution.width}x${cell.resolution.height}`, 9)}  ${cell.resolution.token}`);
  }
}

function reportReferencesMissing(catalogue: Catalogue, refs: ReferenceDoc, cells: Cell[], asJson: boolean) {
  // A state marked noFirmwareReference is a native surface with no firmware counterpart (the Steam folder,
  // the host video folder, the store sections, the sign-in page, Continue Watching). Its cells owe no
  // firmware reference: counting them as debt would keep asking the rig for a frame that must not exist.
  // They stay in the catalogue, in --capture and in the matrix - only the reference debt leaves them out.
  const nativeOnly = new Set(catalogue.states.filter(state => state.noFirmwareReference === true).map(state => state.id));
  const owed = cells.filter(cell => !nativeOnly.has(cell.state));
  const missing: Array<{ key: string; state: string }> = [];
  const present: Array<{ key: string; reference: string; kind: string; provenance: boolean }> = [];
  const nativeCells: Array<{ key: string; state: string }> = [];
  for (const cell of cells) {
    const reference = referencePathFor(cell.key, refs);
    const exists = existsSync(reference.path);
    if (nativeOnly.has(cell.state)) {
      if (!exists) nativeCells.push({ key: cell.key, state: cell.state });
      continue;
    }
    if (!exists) {
      missing.push({ key: cell.key, state: cell.state });
      continue;
    }
    present.push({ key: cell.key, reference: reference.path, kind: reference.kind, provenance: existsSync(provenancePathFor(cell.key, refs)) });
  }
  // States, not cells: the report used to say only how many cells were empty, which cannot answer "does
  // this state have a reference anywhere" (the limitation PARITY-SCORECARD.md 11.3 recorded).
  const referencedStates = new Set(present.map(entry => cells.find(cell => cell.key === entry.key)?.state ?? ''));
  const owedStates = new Set(owed.map(cell => cell.state));
  const statesWithReference = [...owedStates].filter(state => referencedStates.has(state)).length;
  const stateSummary = {
    total: catalogue.states.length,
    firmwareReferenced: owedStates.size,
    withReference: statesWithReference,
    missing: owedStates.size - statesWithReference,
    noFirmwareReference: nativeOnly.size,
  };
  if (asJson) {
    console.log(JSON.stringify({
      schema: 'xmb-parity-references-missing/1',
      referenceRoot: resolveRepo(refs.root),
      cells: cells.length,
      firmwareCells: owed.length,
      present, missing,
      states: stateSummary,
      noFirmwareReference: { states: [...nativeOnly], cells: nativeCells.length },
      note: 'A cell with no reference is reported, never scored against another state. A state with '
        + 'noFirmwareReference owns no firmware surface, so its cells owe no reference and are reported separately.',
    }, null, 2));
  } else {
    console.log(`references: ${present.length}/${owed.length} firmware-reference cells present`
      + `  (${cells.length} selected cells, ${nativeCells.length} on ${nativeOnly.size} native-only state(s) that owe none)`);
    console.log(`  states: ${statesWithReference}/${owedStates.size} firmware-referenced states have a reference somewhere; `
      + `${nativeOnly.size} native-only state(s) owe none (${catalogue.states.length} states in the catalogue)`);
    console.log(`  root ${resolveRepo(refs.root)}`);
    if (present.length) {
      console.log('');
      for (const entry of present)
        console.log(`  present  ${pad(entry.key, 52, true)}  ${entry.kind}${entry.provenance ? '' : ' (provenance missing)'}  ${entry.reference}`);
    }
    if (missing.length) {
      console.log('');
      console.log(`  ${missing.length} cell(s) have no reference yet:`);
      const byState = new Map<string, string[]>();
      for (const entry of missing) {
        if (!byState.has(entry.state)) byState.set(entry.state, []);
        byState.get(entry.state)!.push(entry.key);
      }
      for (const [state, keys] of byState) console.log(`    ${pad(state, 24, true)}  ${keys.length} cell(s)`);
      console.log('');
      console.log('  These are captured and gated on Tier A/C, and reported as "no reference yet" for Tier B.');
      console.log('  The RPCS3 rig produces them: work/parity-reference.json rpcs3.perCellRecipe.');
    }
    if (nativeOnly.size) {
      console.log('');
      console.log('  Native-only surfaces (noFirmwareReference): no firmware frame exists, and none should be captured:');
      for (const state of catalogue.states.filter(candidate => candidate.noFirmwareReference === true))
        console.log(`    ${pad(state.id, 24, true)}  ${state.title}`);
    }
  }
  if (missing.length) process.exitCode = 1;
}

async function runCapture(catalogue: Catalogue, refs: ReferenceDoc, opts: Options, cells: Cell[]) {
  if (!existsSync(opts.exe)) fail(`capture executable not found: ${opts.exe} (build it, or pass --exe)`);
  if (!existsSync(opts.flash)) fail(`dev_flash not found: ${opts.flash} (pass --flash)`);
  mkdirSync(opts.out, { recursive: true });
  const runId = `run-${stamp()}`;
  const runDir = join(opts.out, runId);
  mkdirSync(runDir, { recursive: true });
  const rootState = catalogue.states.find(state => state.id === 'root');
  if (!rootState) fail('the catalogue must define a "root" state: the discovery capture is the root with extra waits');
  const byId = new Map(catalogue.states.map(state => [state.id, state]));
  // The phase sweep is a scoring diagnostic, so it only runs on states the run actually captures and
  // that have a reference to score against. Naming an unknown or uncaptured state is an input error.
  const phaseStates = new Set(opts.phaseSweep.length ? opts.phaseState : []);
  for (const id of phaseStates) {
    if (!byId.has(id)) fail(`--phase-state ${JSON.stringify(id)} is not a catalogue state`);
    if (byId.get(id)!.captures === false) fail(`--phase-state ${JSON.stringify(id)} is not capturable`);
    if (!cells.some(cell => cell.state === id))
      fail(`--phase-state ${JSON.stringify(id)} is not among the captured cells (add it to --state, or drop the filter)`);
  }
  const discoveries: RunManifest['discoveries'] = [];
  const discoveryCache = new Map<string, Discovered>();
  const discoveryFor = async (state: State) => {
    const key = seedKey(catalogue, state);
    const cached = discoveryCache.get(key);
    if (cached) return cached;
    // The discovery run uses the root state's recipe with the seed this state needs.
    const seeded: State = { ...rootState, dataSeed: state.dataSeed, seedPaths: state.seedPaths, env: state.env };
    const run = await discoveryCapture(catalogue, opts, runDir, key, seeded);
    discoveries.push({
      key, seed: seeded.dataSeed ?? catalogue.defaults?.dataSeed ?? '', dir: run.dir, dump: run.dump,
      columns: run.discovered.columns,
    });
    discoveryCache.set(key, run.discovered);
    return run.discovered;
  };
  const manifest: RunManifest = {
    schema: 'xmb-parity-run/1',
    generated: isoNow(),
    run: runDir,
    exe: opts.exe,
    exeSha256: sha256File(opts.exe),
    flash: opts.flash,
    catalogue: cataloguePath,
    catalogueSha256: sha256File(cataloguePath),
    reference: referencePath,
    referenceSha256: sha256File(referencePath),
    pins: { renderClock: opts.renderClock, uiClock: opts.uiClock, focusTimeMs: opts.focusTime },
    ...(opts.phaseSweep.length ? {
      phaseSweep: {
        offsetsSeconds: opts.phaseSweep,
        states: [...phaseStates],
        note: 'Scoring-only diagnostic: each phase cell is the same state captured on the app\'s live '
          + 'animation clock with a scripted pause of the offset before the grab, because the pinned '
          + 'render clock freezes the ribbon cloth at its settle phase. Phase cells are never gates.',
      },
    } : {}),
    discoveries: [],
    cells: [],
  };
  for (const cell of cells) {
    const state = byId.get(cell.state)!;
    const discovered = await discoveryFor(state);
    // A host-level focus change can make a scripted input a no-op (the PS menu refuses a hold while the
    // XMB is not in front), and a key can be swallowed while the app rebuilds its catalogue under a
    // Steam scan, so a failed assertion is retried (two retries, three attempts) before it is reported:
    // the same convention the per-layer harness uses for its named states, with the count in the
    // manifest. The best attempt wins; a run that fails every attempt reports the failure.
    const check = (candidate: RunManifest['cells'][number]) => {
      const files: CellFiles = {
        dir: candidate.dir, png: candidate.png, sidecar: candidate.sidecar, dump: candidate.dump,
        folder: candidate.folder, downloads: candidate.downloads, log: candidate.log, psLog: candidate.psLog,
      };
      return { a: candidate.error ? { ok: false, problems: [candidate.error], summary: '' } : checkExpect(state, files),
        c: candidate.error ? { status: 'fail' as const, problems: [candidate.error], matched: [] } : checkTrace(state, files) };
    };
    const attemptScore = (value: ReturnType<typeof check>) => (value.a.ok ? 2 : 0) + (value.c.status === 'fail' ? 0 : 1);
    const maxAttempts = 3;
    let record: RunManifest['cells'][number] | undefined;
    let result: ReturnType<typeof check> | undefined;
    let attempts = 0;
    for (let attempt = 1; attempt <= maxAttempts; attempt++) {
      const candidate = await captureCell(catalogue, refs, opts, cell, state, discovered, runDir,
        attempt === 1 ? '' : `.attempt${attempt}`);
      const candidateResult = check(candidate);
      attempts = attempt;
      if (!record || !result || attemptScore(candidateResult) > attemptScore(result)) {
        record = candidate;
        result = candidateResult;
      }
      if (candidateResult.a.ok && candidateResult.c.status !== 'fail') break;
      if (!quiet && attempt < maxAttempts)
        console.log(`   retry     attempt ${attempt} failed an assertion; capturing again`);
    }
    manifest.cells.push({ ...record!, attempts });
    // The ribbon-phase diagnostic: the same cell on the live clock, one capture per offset. It is not
    // retried (a phase is only a probe, and a retry would land at a slightly different instant) and it
    // never replaces the shipped cell or feeds the gate.
    if (opts.phaseSweep.length && phaseStates.has(cell.state)) {
      const reference = referencePathFor(cell.key, refs);
      if (!existsSync(reference.path)) {
        if (!quiet) console.log(`   phase     skipped: no reference for ${cell.key}`);
      } else {
        for (const [index, offsetSeconds] of opts.phaseSweep.entries()) {
          const phase = { offsetSeconds, pauseMs: Math.round(offsetSeconds * 1000), label: `live+${offsetSeconds}s` };
          const candidate = await captureCell(catalogue, refs, opts, cell, state, discovered, runDir,
            `.phase${index}`, phase);
          const phaseCheck = check(candidate);
          manifest.cells.push({ ...candidate, attempts: 1 });
          if (!quiet) {
            console.log(`   phase     ${phase.label}  ${candidate.error ? candidate.error
              : phaseCheck.a.ok ? 'Tier A PASS' : 'Tier A FAIL'}  ${candidate.width}x${candidate.height}`);
            for (const problem of phaseCheck.a.problems) console.log(`   - ${problem}`);
          }
        }
      }
    }
    if (quiet) continue;
    const checked = result!.a;
    const trace = result!.c as { status: 'pass' | 'fail' | 'n/a'; problems: string[]; matched: string[] };
    console.log(`   frame     ${record!.width}x${record!.height}${record!.error ? ` (${record!.error})` : ''}`
      + `${attempts > 1 ? `  attempt ${attempts}` : ''}`);
    console.log(`   Tier A    ${checked.ok ? 'PASS' : 'FAIL'}  ${checked.summary}`);
    for (const problem of checked.problems) console.log(`   - ${problem}`);
    console.log(`   Tier C    ${trace.status === 'pass' ? 'PASS' : trace.status === 'n/a' ? 'n/a (no input trace)' : 'FAIL'}`);
    for (const line of trace.matched) if (line !== 'MISS') console.log(`   > ${line}`);
    for (const problem of trace.problems) console.log(`   - ${problem}`);
  }
  writeFileSync(join(runDir, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  // A stable pointer to the newest run, so --score works without re-reading directory timestamps.
  writeFileSync(join(opts.out, 'latest-run.txt'), `${runDir}\n`);
  if (!quiet) {
    console.log('');
    console.log(`run: ${runDir}`);
    console.log(`manifest: ${join(runDir, 'manifest.json')}`);
    console.log(`captured ${manifest.cells.filter(cell => !cell.error).length}/${manifest.cells.length} cell(s)`);
  }
  return runDir;
}

function loadBudgetsAndCheck(catalogue: Catalogue, refs: ReferenceDoc) {
  if (refs.budgets.provisional && !quiet)
    console.log(`note: Tier B budgets are provisional (updated ${refs.budgets.updated}); no matched reference exists yet`);
  if (!quiet) console.log(`budgets: ${Object.keys(refs.budgets.classes).join(', ')} (work/parity-reference.json budgets.updated ${refs.budgets.updated})`);
}

function writeScorecards(card: ScoreCard, refs: ReferenceDoc, opts: Options) {
  const dir = join(opts.out, 'scorecards');
  mkdirSync(dir, { recursive: true });
  const name = `scorecard-${stamp()}`;
  const jsonPath = join(dir, `${name}.json`);
  const mdPath = join(dir, `${name}.md`);
  const markdown = renderMarkdown(card, refs);
  writeFileSync(jsonPath, `${JSON.stringify(card, null, 2)}\n`);
  writeFileSync(mdPath, markdown);
  const extra = opts.markdown ? resolve(opts.markdown) : '';
  if (extra) {
    mkdirSync(resolve(extra, '..'), { recursive: true });
    writeFileSync(extra, markdown);
  }
  return { jsonPath, mdPath };
}

function scorecardsIn(outRoot: string) {
  const dir = join(outRoot, 'scorecards');
  if (!existsSync(dir)) return [] as string[];
  return readdirSync(dir).filter(name => /^scorecard-\d{8}-\d{6}\.json$/.test(name))
    .sort().map(name => join(dir, name));
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  quiet = opts.json;
  const catalogue = loadCatalogue();
  const refs = loadReference();
  const cells = selectedCells(catalogue, refs, opts);

  if (opts.command === '--catalogue') { reportCatalogue(catalogue, refs, opts.json); return; }
  if (opts.command === '--list') {
    for (const state of catalogue.states) {
      const cells = state.captures === false ? 'not capturable' : 'capturable';
      const trace = state.trace?.length ? `${state.trace.length} trace step(s)` : 'no trace';
      const reference = state.noFirmwareReference === true ? 'no fw reference (native-only)' : 'fw reference owed';
      console.log(`${pad(state.id, 24, true)} ${pad(state.class, 11, true)} ${pad(state.native.reachable, 12, true)} `
        + `${pad(cells, 14, true)} ${pad(reference, 29, true)} ${pad(String(state.regions.length), 2, true)} region(s)  ${trace}`);
    }
    return;
  }
  if (opts.command === '--matrix') { reportMatrix(catalogue, refs, cells, opts.json); return; }
  if (opts.command === '--references-missing') { reportReferencesMissing(catalogue, refs, cells, opts.json); return; }

  if (opts.command === '--capture' || opts.command === '--capture --score') {
    loadBudgetsAndCheck(catalogue, refs);
    const runDir = await runCapture(catalogue, refs, opts, cells);
    if (opts.command === '--capture') return;
    const manifest = readManifest(runDir);
    const card = buildScorecard(catalogue, refs, opts, runDir, manifest);
    const paths = writeScorecards(card, refs, opts);
    reportScorecard(card, paths, opts);
    return;
  }
  if (opts.command === '--score') {
    const runDir = opts.run || newestRun(opts.out);
    const manifest = readManifest(runDir);
    const card = buildScorecard(catalogue, refs, opts, runDir, manifest);
    const paths = writeScorecards(card, refs, opts);
    reportScorecard(card, paths, opts);
    return;
  }
  if (opts.command === '--regression') {
    const runDir = opts.run || newestRun(opts.out);
    const manifest = readManifest(runDir);
    const card = buildScorecard(catalogue, refs, opts, runDir, manifest);
    const paths = writeScorecards(card, refs, opts);
    const previousPath = opts.previous || (() => {
      const all = scorecardsIn(opts.out).filter(path => path !== paths.jsonPath);
      return all.length ? all[all.length - 1] : '';
    })();
    if (!previousPath) {
      console.log(`regression: no previous scorecard under ${join(opts.out, 'scorecards')}`);
      console.log('  the first scorecard is written; run --regression again after a second --score');
      reportScorecard(card, paths, opts);
      return;
    }
    const previous = readJson(previousPath, 'previous scorecard') as ScoreCard;
    const regression = compareScorecards(card, previous);
    const regressionPath = paths.jsonPath.replace(/scorecard-/, 'regression-');
    writeFileSync(regressionPath, `${JSON.stringify(regression, null, 2)}\n`);
    if (!opts.json) {
      console.log(`regression: ${regression.pass ? 'PASS' : 'FAIL'} (${regression.findings.length} finding(s))`);
      console.log(`  previous  ${regression.previous}`);
      console.log(`  current   ${regression.current}`);
      console.log(`  budget    PSNR >= previous - ${regressionBudget.psnrDb} dB, SSIM >= previous - ${regressionBudget.ssim}`);
      for (const finding of regression.findings) console.log(`  - ${finding}`);
      console.log(`  report    ${regressionPath}`);
      console.log(`  scorecard ${paths.jsonPath}`);
    } else {
      console.log(JSON.stringify(regression, null, 2));
    }
    if (!regression.pass) process.exitCode = 1;
    return;
  }
  fail(`unknown command ${opts.command}`);
}

function reportScorecard(card: ScoreCard, paths: { jsonPath: string; mdPath: string }, opts: Options) {
  if (opts.json) {
    console.log(JSON.stringify(card, null, 2));
    return;
  }
  console.log('');
  console.log(`parity scorecard: ${card.totals.claims}/${card.totals.capturable} capturable states pass A+B+C`);
  console.log(`  Tier A ${card.totals.tierA} pass, Tier B ${card.totals.tierB} pass`
    + `${card.totals.tierBInformative ? ` / ${card.totals.tierBInformative} informative` : ''}, Tier C ${card.totals.tierC} pass`);
  console.log(`  ${card.totals.noReference} captured cell(s) with no reference yet; ${card.totals.failed} gating cell failure(s)`);
  console.log(`  content-difference annotations ${card.mode === 'strict' ? '(STRICT: gated)' : '(informative)'}: `
    + `${card.totals.contentDiffersRegions} region(s) across ${card.totals.contentDiffersStates} state(s); `
    + `${card.totals.informative} cell(s) B informative`);
  if (card.phaseSweep.length)
    console.log(`  ribbon-phase sweep: ${card.phaseSweep.map(entry => `${entry.state} ${entry.shipped.label} `
      + `${typeof entry.shipped.framePsnr === 'number' ? entry.shipped.framePsnr.toFixed(2) : entry.shipped.framePsnr} dB -> `
      + `${entry.best.label} ${typeof entry.best.framePsnr === 'number' ? entry.best.framePsnr.toFixed(2) : entry.best.framePsnr} dB`)
      .join('; ')} (scoring only, not a gate)`);
  console.log(`  reference coverage ${card.totals.statesWithReference}/${card.totals.coveredFirmware} states covered by this run `
    + `(+${card.totals.noFirmwareReference} native-only that owe none; catalogue ${card.totals.firmwareReferenced} + `
    + `${card.totals.noFirmwareReference} native-only of ${card.totals.states})`);
  console.log('');
  const width = Math.max(5, ...card.states.map(state => state.state.length));
  console.log(`${pad('state', width, true)}  ${pad('class', 11, true)}  ${pad('A', 6)}  ${pad('B', 14)}  ${pad('C', 4)}  claim`);
  console.log(`${'-'.repeat(width)}  ${'-'.repeat(11)}  ${'-'.repeat(6)}  ${'-'.repeat(14)}  ${'-'.repeat(4)}  ${'-'.repeat(8)}`);
  for (const state of card.states) {
    console.log(`${pad(state.state, width, true)}  ${pad(state.class, 11, true)}  ${pad(stateA(state.tierA), 6)}  `
      + `${pad(state.tierB, 14)}  ${pad(stateC(state.tierC), 4)}  ${state.claim}`);
  }
  const informativeStates = card.states.filter(state => state.tierB === 'informative');
  if (informativeStates.length)
    console.log(`\n  B informative (no gating region failed; only content-difference regions did): `
      + `${informativeStates.map(state => `${state.state} (${state.contentDiffers})`).join(', ')}`);
  const scored = card.cells.filter(cell => cell.regions.length);
  for (const cell of scored) {
    console.log('');
    console.log(`-- ${cell.key}  (${cell.referenceKind ?? 'rpcs3'} ${cell.reference})`);
    const nameWidth = Math.max(6, ...cell.regions.map(region => region.name.length));
    console.log(`   ${pad('region', nameWidth, true)}  ${pad('PSNR', 7)}  ${pad('SSIM', 7)}  ${pad('budget', 16)}  gate`);
    for (const region of cell.regions)
      console.log(`   ${pad(region.name, nameWidth, true)}  ${pad(typeof region.psnr === 'number' ? region.psnr.toFixed(2) : region.psnr, 7)}  `
        + `${pad(region.ssim.toFixed(4), 7)}  ${pad(`${region.minPsnr.toFixed(2)}/${region.minSsim.toFixed(3)}`, 16)}  `
        + `${region.pass ? 'PASS' : 'FAIL'}${region.contentDiffers
          ? ` (content differs${region.gating ? ', strict: gated' : ', informative'})`
          : region.required ? '' : ' (not gating)'}`);
  }
  const findings = card.cells.filter(cell => cell.tierAProblems.length || cell.tierCProblems.length);
  if (findings.length) {
    console.log('');
    console.log('Tier A / Tier C findings:');
    for (const cell of findings) {
      console.log(`  ${cell.key}`);
      for (const problem of cell.tierAProblems) console.log(`    A: ${problem}`);
      for (const problem of cell.tierCProblems) console.log(`    C: ${problem}`);
    }
  }
  console.log('');
  console.log(`json      ${paths.jsonPath}`);
  console.log(`markdown  ${paths.mdPath}`);
  if (card.totals.failed) process.exitCode = 1;
}

function stateA(value: string) {
  return value === 'pass' ? 'PASS' : value === 'no-oracle' ? 'none' : 'FAIL';
}
function stateC(value: string) {
  return value === 'pass' ? 'PASS' : value === 'n/a' ? 'n/a' : 'FAIL';
}

try {
  await main();
} catch (error) {
  console.error(`parity: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(2);
}
