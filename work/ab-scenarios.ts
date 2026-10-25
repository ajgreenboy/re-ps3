import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, readFileSync, realpathSync, statSync, writeFileSync } from 'node:fs';
import { isAbsolute, join, relative, resolve, sep } from 'node:path';
import { decodePng, psnr, ssim } from './ab-compare';

type Region = { name: string; crop?: [number, number, number, number]; minPsnr?: number; minSsim?: number };
type Scenario = { name: string; reference: string; tokens: string[]; backgroundClock?: string; renderClock?: string; uiClock?: string; focusTime?: number; expect?: { category?: string; depth?: number; selection?: number }; regions: Region[] };
type Manifest = { version: 1; defaults?: { backgroundClock?: string; renderClock?: string; uiClock?: string; focusTime?: number }; scenarios: Scenario[] };
type Args = { manifest: string; exe: string; flash: string; references: string; output: string; timeoutMs: number };

const usage = 'usage: bun work/ab-scenarios.ts <manifest.json> --exe <path> --flash <dir> --references <dir> --output <dir> [--timeout-ms N]';
const keyTokens = new Set(['LEFT', 'RIGHT', 'UP', 'DOWN', 'ENTER', 'ESC', 'PGUP', 'PGDN', 'BACKSPACE', 'PREV', 'NEXT', 'SHUFFLE', 'REPEAT', 'ZOOMIN', 'ZOOMOUT', 'RESETVIEW', 'ROTATELEFT', 'ROTATERIGHT']);
const safeName = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;
const fail = (message: string): never => { throw new Error(message); };
function parseArgs(argv: string[]): Args {
  if (argv.includes('--help') || argv.includes('-h')) { console.log(usage); process.exit(0); }
  let manifest = '';
  const values: Record<string, string> = {};
  const allowed = new Set(['--exe', '--flash', '--references', '--output', '--timeout-ms']);
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith('--')) { if (manifest) fail(`unexpected positional argument: ${arg}`); manifest = arg; continue; }
    if (!allowed.has(arg) || values[arg] !== undefined || !argv[i + 1] || argv[i + 1].startsWith('--')) fail(`invalid or repeated argument: ${arg}`);
    values[arg] = argv[++i];
  }
  if (!manifest || ['--exe', '--flash', '--references', '--output'].some(k => !values[k])) fail(usage);
  const timeoutMs = values['--timeout-ms'] === undefined ? 30_000 : Number(values['--timeout-ms']);
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 3_600_000) fail('--timeout-ms must be an integer from 1 through 3600000');
  const result: Args = { manifest: resolve(manifest), exe: values['--exe'], flash: values['--flash'], references: values['--references'], output: values['--output'], timeoutMs };
  for (const [label, path] of Object.entries({ exe: result.exe, flash: result.flash, references: result.references, output: result.output }))
    if (!isAbsolute(path)) fail(`--${label} must be an absolute path`);
  return result;
}
function object(value: unknown, label: string): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) fail(`${label} must be an object`);
  return value as Record<string, unknown>;
}
function optionalString(value: unknown, label: string): string | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== 'string' || !value.trim()) fail(`${label} must be a non-empty string`);
  return value;
}
function threshold(value: unknown, label: string, min: number, max = Number.POSITIVE_INFINITY): number | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max) fail(`${label} must be a number from ${min} through ${max}`);
  return value;
}
function focusTime(value: unknown, label: string): number | undefined {
  const parsed = threshold(value, label, 0);
  if (parsed !== undefined && !Number.isSafeInteger(parsed)) fail(`${label} must be a non-negative safe integer`);
  return parsed;
}
function nonNegativeInteger(value: unknown, label: string): number | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) fail(`${label} must be a non-negative safe integer`);
  return value;
}
function captureState(path: string): Record<string, unknown> {
  let parsed: unknown;
  try { parsed = JSON.parse(readFileSync(path, 'utf8')); } catch (error) { fail(`cannot read capture state ${path}: ${String(error)}`); }
  const state = object(parsed, 'capture state');
  if (typeof state.category !== 'string' || !state.category.trim()) fail('capture state category must be a non-empty string');
  for (const field of ['categoryIndex', 'depth', 'selection', 'width', 'height']) {
    const value = state[field];
    if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) fail(`capture state ${field} must be a non-negative safe integer`);
  }
  if (state.width === 0 || state.height === 0) fail('capture state dimensions must be positive');
  if (!(typeof state.clock === 'string' || (typeof state.clock === 'number' && Number.isFinite(state.clock)))) fail('capture state clock must be a string or finite number');
  return state;
}
function readManifest(path: string): Manifest {
  let parsed: unknown;
  try { parsed = JSON.parse(readFileSync(path, 'utf8')); } catch (error) { fail(`cannot read manifest: ${String(error)}`); }
  const root = object(parsed, 'manifest');
  if (root.version !== 1 || !Array.isArray(root.scenarios) || root.scenarios.length === 0) fail('manifest requires version: 1 and a non-empty scenarios array');
  const defaultsRaw = root.defaults === undefined ? {} : object(root.defaults, 'defaults');
  const defaults = { backgroundClock: optionalString(defaultsRaw.backgroundClock, 'defaults.backgroundClock'), renderClock: optionalString(defaultsRaw.renderClock, 'defaults.renderClock'), uiClock: optionalString(defaultsRaw.uiClock, 'defaults.uiClock'), focusTime: focusTime(defaultsRaw.focusTime, 'defaults.focusTime') };
  const names = new Set<string>();
  const scenarios = root.scenarios.map((raw, i): Scenario => {
    const entry = object(raw, `scenarios[${i}]`);
    if (typeof entry.name !== 'string' || !safeName.test(entry.name) || names.has(entry.name.toLowerCase())) fail(`scenarios[${i}].name must be safe and unique`);
    names.add(entry.name.toLowerCase());
    if (typeof entry.reference !== 'string' || !entry.reference || isAbsolute(entry.reference)) fail(`scenarios[${i}].reference must be a relative path`);
    if (!Array.isArray(entry.tokens) || entry.tokens.length === 0 || !entry.tokens.every(t => typeof t === 'string')) fail(`scenarios[${i}].tokens must be a non-empty string array`);
    for (const token of entry.tokens as string[]) {
      const valid = token === 'wait' || token === 'full' || keyTokens.has(token) || /^size=[0-9]{1,4}x[0-9]{1,4}$/.test(token);
      if (!valid || /[,\r\n]/.test(token) || /grab|quit|burst|type=/i.test(token)) fail(`scenario ${entry.name} has disallowed script token: ${token}`);
      if (token.startsWith('size=')) {
        const [w, h] = token.slice(5).split('x').map(Number);
        if (w < 800 || h < 450 || w > 7680 || h > 4320) fail(`scenario ${entry.name} has unsupported size token: ${token}`);
      }
    }
    let expected: Scenario['expect'];
    if (entry.expect !== undefined) {
      const rawExpect = object(entry.expect, `scenario ${entry.name}.expect`);
      const category = optionalString(rawExpect.category, `${entry.name}.expect.category`);
      const depth = nonNegativeInteger(rawExpect.depth, `${entry.name}.expect.depth`);
      const selection = nonNegativeInteger(rawExpect.selection, `${entry.name}.expect.selection`);
      if (category === undefined && depth === undefined && selection === undefined) fail(`scenario ${entry.name}.expect must specify at least one expected field`);
      expected = { ...(category === undefined ? {} : { category }), ...(depth === undefined ? {} : { depth }), ...(selection === undefined ? {} : { selection }) };
    }
    if (!Array.isArray(entry.regions) || entry.regions.length === 0) fail(`scenario ${entry.name}.regions must be a non-empty array`);
    const regionNames = new Set<string>();
    const regions = entry.regions.map((r, j): Region => {
      const region = object(r, `scenarios[${i}].regions[${j}]`);
      if (typeof region.name !== 'string' || !safeName.test(region.name) || regionNames.has(region.name.toLowerCase())) fail(`scenario ${entry.name} region names must be safe and unique`);
      regionNames.add(region.name.toLowerCase());
      let crop: Region['crop'];
      if (region.crop !== undefined) {
        if (!Array.isArray(region.crop) || region.crop.length !== 4 || !region.crop.every(Number.isSafeInteger)) fail(`region ${region.name}.crop must be [x,y,w,h] integers`);
        crop = region.crop as Region['crop'];
        if (crop![0] < 0 || crop![1] < 0 || crop![2] <= 0 || crop![3] <= 0) fail(`region ${region.name}.crop must have non-negative origin and positive size`);
      }
      return { name: region.name, ...(crop ? { crop } : {}), ...(threshold(region.minPsnr, 'minPsnr', 0) === undefined ? {} : { minPsnr: region.minPsnr as number }), ...(threshold(region.minSsim, 'minSsim', -1, 1) === undefined ? {} : { minSsim: region.minSsim as number }) };
    });
    return { name: entry.name, reference: entry.reference, tokens: entry.tokens as string[], regions,
      backgroundClock: optionalString(entry.backgroundClock, `${entry.name}.backgroundClock`) ?? defaults.backgroundClock,
      renderClock: optionalString(entry.renderClock, `${entry.name}.renderClock`) ?? defaults.renderClock,
      uiClock: optionalString(entry.uiClock, `${entry.name}.uiClock`) ?? defaults.uiClock,
      focusTime: focusTime(entry.focusTime, `${entry.name}.focusTime`) ?? defaults.focusTime,
      ...(expected ? { expect: expected } : {}) };
  });
  return { version: 1, defaults, scenarios };
}
const sha256 = (path: string) => createHash('sha256').update(readFileSync(path)).digest('hex');
function inside(root: string, target: string): boolean { const rel = relative(root, target); return rel === '' || (!rel.startsWith(`..${sep}`) && rel !== '..' && !isAbsolute(rel)); }

const report: { version: 1; startedAt: string; finishedAt?: string; runDirectory?: string; manifestSha256?: string; exeSha256?: string; scenarios: Record<string, unknown>[] } = { version: 1, startedAt: new Date().toISOString(), scenarios: [] };
let exitCode = 0;
try {
  const args = parseArgs(process.argv.slice(2));
  const manifest = readManifest(args.manifest);
  mkdirSync(args.output, { recursive: true });
  const outputRoot = resolve(args.output);
  const runDirectory = mkdtempSync(join(outputRoot, 'run-'));
  report.runDirectory = runDirectory;
  report.manifestSha256 = sha256(args.manifest);
  report.exeSha256 = sha256(args.exe);
  const referencesRoot = realpathSync(args.references);
  if (!statSync(referencesRoot).isDirectory()) fail('--references must be a directory');
  if (!statSync(args.exe).isFile()) fail('--exe must name an existing file');
  if (!statSync(args.flash).isDirectory()) fail('--flash must be an existing directory');
  for (const scenario of manifest.scenarios) {
    const scenarioReport: Record<string, unknown> = { name: scenario.name, reference: scenario.reference, regions: [], status: 'pending' };
    report.scenarios.push(scenarioReport);
    try {
      const refCandidate = resolve(referencesRoot, scenario.reference);
      if (!inside(referencesRoot, refCandidate)) fail(`reference escapes --references: ${scenario.reference}`);
      let referencePath: string;
      try { referencePath = realpathSync(refCandidate); } catch { scenarioReport.status = 'missing-reference'; scenarioReport.error = 'reference file is missing'; exitCode = 2; continue; }
      if (!inside(referencesRoot, referencePath) || !statSync(referencePath).isFile()) fail(`reference is outside --references or not a file: ${scenario.reference}`);
      const nativePath = join(runDirectory, `${scenario.name}.png`);
      const scenarioDir = join(runDirectory, scenario.name);
      mkdirSync(scenarioDir, { recursive: true });
      const stdoutPath = join(scenarioDir, 'stdout.log'), stderrPath = join(scenarioDir, 'stderr.log'), qtLogPath = join(scenarioDir, 'qt.log');
      scenarioReport.logs = { stdout: stdoutPath, stderr: stderrPath, qt: qtLogPath };
      const argsNative = ['--dev-flash', args.flash, '--data-dir', join(scenarioDir, 'data'), '--render-clock', scenario.renderClock ?? '30'];
      if (scenario.backgroundClock) argsNative.push('--background-clock', scenario.backgroundClock);
      if (scenario.uiClock) argsNative.push('--ui-clock', scenario.uiClock);
      if (scenario.focusTime !== undefined) argsNative.push('--focus-time', String(scenario.focusTime));
      const effectiveTokens = [...scenario.tokens, `grab=${nativePath}`, 'quit'];
      argsNative.push('--script', effectiveTokens.join(','));
      scenarioReport.effective = { tokens: effectiveTokens, renderClock: scenario.renderClock ?? '30', backgroundClock: scenario.backgroundClock ?? null, uiClock: scenario.uiClock ?? null, focusTime: scenario.focusTime ?? null };
      const childEnv: Record<string, string> = {};
      for (const [key, value] of Object.entries(process.env)) if (value !== undefined) childEnv[key] = value;
      childEnv.XMB_LOG = qtLogPath;
      const child = Bun.spawn([args.exe, ...argsNative], { stdout: Bun.file(stdoutPath), stderr: Bun.file(stderrPath), env: childEnv });
      let timedOut = false;
      const timeout = setTimeout(() => { timedOut = true; child.kill(); }, args.timeoutMs);
      let code: number;
      try { code = await child.exited; } finally { clearTimeout(timeout); }
      scenarioReport.native = nativePath;
      if (timedOut) { scenarioReport.status = 'timeout'; scenarioReport.error = `process exceeded ${args.timeoutMs} ms`; exitCode = 2; continue; }
      if (code !== 0) { scenarioReport.status = 'nonzero-exit'; scenarioReport.error = `native process exited ${code}`; exitCode = 2; continue; }
      if (scenario.expect) {
        const statePath = `${nativePath}.json`;
        scenarioReport.expectedState = scenario.expect;
        scenarioReport.statePath = statePath;
        const actualState = captureState(statePath);
        scenarioReport.actualState = actualState;
        const mismatches = Object.entries(scenario.expect).filter(([key, expected]) => actualState[key] !== expected);
        if (mismatches.length) {
          scenarioReport.status = 'state-mismatch';
          scenarioReport.error = mismatches.map(([key, expected]) => `${key}: expected ${String(expected)}, got ${String(actualState[key])}`).join('; ');
          exitCode = 2;
          continue;
        }
      }
      if (!Bun.file(nativePath).size) { scenarioReport.status = 'capture-missing'; scenarioReport.error = 'native capture was not written'; exitCode = 2; continue; }
      const refImage = decodePng(referencePath), nativeImage = decodePng(nativePath);
      scenarioReport.referenceSha256 = sha256(referencePath);
      scenarioReport.nativeSha256 = sha256(nativePath);
      scenarioReport.dimensions = { width: nativeImage.w, height: nativeImage.h };
      if (refImage.w !== nativeImage.w || refImage.h !== nativeImage.h) { scenarioReport.status = 'dimension-mismatch'; scenarioReport.error = `reference ${refImage.w}x${refImage.h}, native ${nativeImage.w}x${nativeImage.h}`; exitCode = 2; continue; }
      let thresholdFailure = false;
      const regionResults = scenario.regions.map(region => {
        const crop = region.crop ?? [0, 0, refImage.w, refImage.h];
        if (crop[0] + crop[2] > refImage.w || crop[1] + crop[3] > refImage.h) throw new Error(`crop for ${region.name} exceeds ${refImage.w}x${refImage.h}`);
        const p = psnr(refImage, nativeImage, region.crop), s = ssim(refImage, nativeImage, region.crop);
        const psnrPass = region.minPsnr === undefined ? undefined : p >= region.minPsnr;
        const ssimPass = region.minSsim === undefined ? undefined : s >= region.minSsim;
        if (psnrPass === false || ssimPass === false) thresholdFailure = true;
        return { name: region.name, crop: region.crop ?? null, psnr: Number.isFinite(p) ? p : 'Infinity', ssim: s,
          minPsnr: region.minPsnr ?? null, minSsim: region.minSsim ?? null,
          status: psnrPass === undefined && ssimPass === undefined ? 'unscored' : (psnrPass !== false && ssimPass !== false ? 'pass' : 'fail') };
      });
      scenarioReport.regions = regionResults;
      scenarioReport.status = thresholdFailure ? 'threshold-fail' : 'captured';
      if (thresholdFailure && exitCode === 0) exitCode = 1;
    } catch (error) {
      scenarioReport.status = 'error'; scenarioReport.error = String(error); exitCode = 2;
    }
  }
  report.finishedAt = new Date().toISOString();
  const reportText = `${JSON.stringify(report, null, 2)}\n`;
  const reportPath = join(runDirectory, 'ab-scenarios-report.json');
  writeFileSync(reportPath, reportText);
  writeFileSync(join(outputRoot, 'ab-scenarios-report.json'), reportText);
  for (const scenario of report.scenarios) console.log(`${scenario.name}: ${scenario.status}`);
  console.log(`report: ${reportPath}`);
} catch (error) {
  console.error(`${String(error)}\n${usage}`);
  process.exit(2);
}
process.exit(exitCode);
