// Phase 5 performance evidence for the native XMB: swap-interval pacing from the
// app's own frame trace, a process-start-to-first-frame startup budget with the
// loader milestones the app already logs, and a scripted churn soak that samples
// the running process's memory.
//
//   bun work/perf-harden.ts pacing --trace <file> [--log <file>] [--warmup-ms N] [--json out]
//   bun work/perf-harden.ts startup [--runs N] [--exe path] [--flash dir] [--seed dir] [--json out]
//   bun work/perf-harden.ts soak [--minutes N] [--sample-ms N] [--exe path] [--flash dir]
//                                [--seed dir] [--json out]
//
// Exit codes: 0 the mode completed; 2 input/runtime error.
//
// The three modes are deliberately separate processes. `pacing` is offline on a
// captured trace, `startup` runs the app briefly, and `soak` spends the whole
// requested time in one session; running the soak while measuring the other two
// would put the measurement's own load into the numbers.
//
// Every number these modes report comes from the application, not from a mock:
// `frame swap:`/`frame paint:` lines come from XMB_FRAME_TRACE, the loader times
// come from the Qt message timestamps (%{time process}) of the lines the loaders
// already write, and the soak memory samples come from the running process.

import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';

const repo = resolve(import.meta.dir, '..');
const usage = `usage: bun work/perf-harden.ts <mode> [options]

  pacing                  swap-interval percentiles from an XMB_FRAME_TRACE file
    --trace <file>          the trace (required unless --run is given)
    --run <dir>             record a new paced session into <dir> first (live clocks,
                            built-in page/media churn), then score it
    --attempts <n>          session attempts before giving up (default 3)
    --log <file>            the run's log, for the "native display refresh" line
    --warmup-ms <n>         ignore samples before this age of the trace (default 2000)
    --json <file>           write a JSON report
    --no-activate           do not bring the window to the foreground

  startup                 process start -> first frame, plus the loader milestones
    --runs <n>              number of launches (default 7)
    --exe <path>            capture executable (default: the thresholds file's build)
    --flash <dir>           dev_flash directory
    --seed <dir>            data directory seed (default out/native-reference/store/ui3-data)
    --json <file>           write a JSON report
    --no-activate           do not bring the window to the foreground

  soak                    scripted page/media churn with memory sampling
    --minutes <n>           session length in minutes (default 20)
    --sample-ms <n>         memory sample interval (default 5000)
    --exe <path>            capture executable (default: the thresholds file's build)
    --flash <dir>           dev_flash directory
    --seed <dir>            data directory seed (default out/native-reference/store/video-data)
    --json <file>           write a JSON report
    --no-activate           do not keep the window in the foreground
    --keep                  print the evidence directory instead of leaving it to the log
  --help                  show this text`;

function fail(message: string): never {
  console.error(`perf-harden: ${message}`);
  process.exit(2);
}

type Options = Record<string, string | true>;
function parseOptions(argv: string[], values: Set<string>, flags: Set<string>): Options {
  const options: Options = {};
  for (let index = 0; index < argv.length; index++) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') { console.log(usage); process.exit(0); }
    if (!argument.startsWith('--')) fail(`unexpected argument ${JSON.stringify(argument)}\n${usage}`);
    const name = argument.slice(2);
    if (flags.has(name)) { options[name] = true; continue; }
    if (!values.has(name)) fail(`unknown option ${argument}\n${usage}`);
    const value = argv[++index];
    if (value === undefined || value.startsWith('--')) fail(`${argument} needs a value`);
    options[name] = value;
  }
  return options;
}

function repoPath(path: string) {
  return resolve(repo, path);
}

function thresholdsExe() {
  try {
    const thresholds = JSON.parse(readFileSync(join(repo, 'work', 'ab-layers.thresholds.json'), 'utf8'));
    if (typeof thresholds?.capture?.exe === 'string' && thresholds.capture.exe) return repoPath(thresholds.capture.exe);
    if (typeof thresholds?.capture?.exe === 'string') return thresholds.capture.exe;
  } catch { /* fall through to the default below */ }
  return 'C:/dev/out/native-phase3/re-ps3-native.exe';
}
function thresholdsFlash() {
  try {
    const thresholds = JSON.parse(readFileSync(join(repo, 'work', 'ab-layers.thresholds.json'), 'utf8'));
    if (typeof thresholds?.capture?.flash === 'string') return repoPath(thresholds.capture.flash);
  } catch { /* fall through */ }
  return 'C:/dev/rpcs3/bin/dev_flash';
}

function quantile(sorted: number[], p: number): number {
  if (!sorted.length) return NaN;
  return sorted[Math.max(0, Math.ceil(p * sorted.length) - 1)];
}
type Summary = { count: number; mean: number; p50: number; p95: number; p99: number; max: number; min: number };
function summarize(values: number[]): Summary {
  if (!values.length) return { count: 0, mean: NaN, p50: NaN, p95: NaN, p99: NaN, max: NaN, min: NaN };
  const sorted = [...values].sort((left, right) => left - right);
  return {
    count: values.length,
    mean: values.reduce((sum, value) => sum + value, 0) / values.length,
    p50: quantile(sorted, 0.5), p95: quantile(sorted, 0.95), p99: quantile(sorted, 0.99),
    max: sorted[sorted.length - 1], min: sorted[0],
  };
}
function round(value: number, digits = 3) {
  return Number.isFinite(value) ? +value.toFixed(digits) : value;
}

// ---------------------------------------------------------------------------
// pacing
// ---------------------------------------------------------------------------
// The paced session: live animation clocks (pinning them would remove exactly the
// background/ribbon/particle work the measurement is about), a settled Game root,
// a Video Library page move, a local media start/stop and the way back, then quit.
const pacingScript = ['full', 'wait', 'wait', 'wait',
  'LEFT', 'LEFT', 'wait', 'DOWN', 'ENTER', 'wait', 'wait', 'wait', 'wait',
  'DOWN', 'DOWN', 'wait', 'wait', 'ENTER', 'wait', 'wait', 'wait', 'wait', 'wait',
  'ESC', 'wait', 'wait', 'RIGHT', 'RIGHT', 'wait', 'wait', 'wait', 'quit'];

async function runPacingSession(directory: string, attempts: number, activate: boolean) {
  const exe = thresholdsExe();
  const flash = thresholdsFlash();
  const seed = repoPath('out/native-reference/store/ui3-data');
  if (!existsSync(exe)) fail(`executable not found: ${exe}`);
  mkdirSync(directory, { recursive: true });
  for (let attempt = 1; attempt <= attempts; attempt++) {
    const { dataDir } = seedDataDir(seed);
    const tracePath = join(directory, 'frames.txt');
    const logPath = join(directory, 'native.messages.log');
    const stderrPath = join(directory, 'native.stderr.log');
    const started = Date.now();
    const proc = Bun.spawn([exe, '--dev-flash', flash, '--data-dir', dataDir, '--no-intro',
      '--ui-clock', '2026-10-25T12:32:00', '--script', pacingScript.join(',')], {
      stdout: 'ignore', stderr: 'ignore',
      env: {
        ...process.env as Record<string, string>,
        XMB_FRAME_TRACE: tracePath,
        XMB_LOG: logPath,
        XMB_STEAM_ARTWORK: '0',
      },
    });
    await Bun.sleep(1200);
    await activateWindow(proc.pid, activate);
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; proc.kill(); }, 60000);
    let code: number;
    try { code = await proc.exited; } finally { clearTimeout(timer); }
    const seconds = (Date.now() - started) / 1000;
    if (timedOut || code !== 0) {
      console.log(`  attempt ${attempt}: ${timedOut ? 'never finished (the window did not present)' : `exited ${code}`} after ${seconds.toFixed(1)} s; retrying`);
      continue;
    }
    writeFileSync(stderrPath, '');
    if (!existsSync(tracePath)) fail(`the paced run wrote no frame trace; evidence: ${directory}`);
    console.log(`  session    finished in ${seconds.toFixed(1)} s, trace ${tracePath}`);
    return tracePath;
  }
  fail(`the paced session did not complete in ${attempts} attempt(s); another full-screen window on this machine stalls the script timer, so rerun when it is gone`);
}

async function pacing(options: Options) {
  const activate = options['no-activate'] !== true;
  let tracePath = String(options.trace ?? '');
  if (!tracePath && options.run) {
    const directory = resolve(String(options.run));
    const attempts = Number(options.attempts ?? 3);
    if (!Number.isSafeInteger(attempts) || attempts < 1 || attempts > 10) fail('--attempts must be an integer from 1 through 10');
    console.log(`pacing session: ${pacingScript.length} script tokens, live clocks, evidence ${directory}`);
    tracePath = await runPacingSession(directory, attempts, activate);
    if (!options.log) options.log = join(directory, 'native.messages.log');
  }
  if (!tracePath) fail('pacing needs --trace <file> (an existing trace) or --run <dir> (record a new one)');
  const text = readFileSync(resolve(tracePath), 'utf8');
  const warmupNs = Math.ceil(Number(options['warmup-ms'] ?? 2000) * 1e6);
  const swaps: number[] = [];
  const paint: Array<{ at: number; cpu: number; moving: boolean }> = [];
  for (const line of text.split(/\r?\n/)) {
    const swap = line.match(/^frame swap:\s*(\d+)\s*$/);
    if (swap) { swaps.push(Number(swap[1])); continue; }
    const painted = line.match(/^frame paint:\s*(\d+)\s+(\d+)\s+([01])\s+([01])\s*$/);
    if (painted) paint.push({ at: Number(painted[1]), cpu: Number(painted[2]), moving: painted[4] === '1' });
  }
  if (swaps.length < 10) fail(`${tracePath}: only ${swaps.length} frame swap samples; nothing to summarise`);
  const kept = swaps.filter(at => at >= warmupNs);
  if (kept.length < 10) fail(`${tracePath}: fewer than 10 swap samples after --warmup-ms ${options['warmup-ms'] ?? 2000}`);
  const intervals: number[] = [];
  for (let index = 1; index < kept.length; index++) intervals.push((kept[index] - kept[index - 1]) / 1e6);
  const stats = summarize(intervals);
  const keptPaint = paint.filter(sample => sample.at >= warmupNs);
  const paintCpu = summarize(keptPaint.map(sample => sample.cpu / 1e6));
  const movingPaint = summarize(keptPaint.filter(sample => sample.moving).map(sample => sample.cpu / 1e6));
  // A missed vsync shows up as an interval of about two display periods, so the
  // counts below are "ran late" (over 1.5 periods) and "dropped a frame" (over 2.5).
  let displayHz = Number(options['display-hz'] ?? 0);
  let clockHzSource = 'argument';
  if (!displayHz && options.log) {
    const logText = readFileSync(resolve(String(options.log)), 'utf8');
    const match = logText.match(/native display refresh:\s*([0-9.]+)\s*Hz/);
    if (match) { displayHz = Number(match[1]); clockHzSource = 'log'; }
  }
  const period = displayHz > 0 ? 1000 / displayHz : NaN;
  const report: any = {
    mode: 'pacing',
    trace: resolve(tracePath),
    warmupMs: Number(options['warmup-ms'] ?? 2000),
    scope: 'swap callbacks (QOpenGLWidget::frameSwapped) with swapInterval=1; CPU dispatch time, not physical panel presentation',
    displayHz: Number.isFinite(displayHz) && displayHz > 0 ? displayHz : null,
    displayHzSource: Number.isFinite(displayHz) && displayHz > 0 ? clockHzSource : null,
    displayPeriodMs: round(period),
    samples: { swaps: kept.length, paints: keptPaint.length },
    swapIntervalsMs: {
      mean: round(stats.mean), p50: round(stats.p50), p95: round(stats.p95), p99: round(stats.p99),
      min: round(stats.min), max: round(stats.max),
    },
    lateSwaps: Number.isFinite(period) ? intervals.filter(value => value > period * 1.5).length : null,
    droppedFrames: Number.isFinite(period) ? intervals.filter(value => value > period * 2.5).length : null,
    paintCpuMs: { p50: round(paintCpu.p50), p95: round(paintCpu.p95), p99: round(paintCpu.p99), max: round(paintCpu.max), count: paintCpu.count },
    movingPaintCpuMs: { p50: round(movingPaint.p50), p95: round(movingPaint.p95), max: round(movingPaint.max), count: movingPaint.count },
    targets: Number.isFinite(period) ? {
      hz120PeriodMs: round(1000 / 120),
      hz60PeriodMs: round(1000 / 60),
      hz120NeedsP95UnderMs: round(1000 / 120),
      hz120NeedsP99UnderMs: round(1000 / 120),
    } : null,
  };
  console.log(`pacing: ${tracePath}`);
  console.log(`  scope      swap callbacks with swapInterval=1; not physical panel presentation`);
  console.log(`  display    ${report.displayHz ? `${report.displayHz} Hz (${report.displayPeriodMs} ms per frame, ${clockHzSource})` : 'unknown (pass --display-hz or --log)'}`);
  console.log(`  samples    ${report.samples.swaps} swaps, ${report.samples.paints} paints after ${report.warmupMs} ms warmup`);
  console.log(`  intervals  p50 ${report.swapIntervalsMs.p50} ms, p95 ${report.swapIntervalsMs.p95} ms, p99 ${report.swapIntervalsMs.p99} ms, `
    + `max ${report.swapIntervalsMs.max} ms, mean ${report.swapIntervalsMs.mean} ms`);
  if (report.displayHz) console.log(`  vsync      ${report.lateSwaps} swap(s) over 1.5 periods, ${report.droppedFrames} over 2.5 periods`);
  console.log(`  paint CPU  p50 ${report.paintCpuMs.p50} ms, p95 ${report.paintCpuMs.p95} ms, p99 ${report.paintCpuMs.p99} ms (moving p95 ${report.movingPaintCpuMs.p95} ms)`);
  if (options.json) {
    writeFileSync(resolve(String(options.json)), `${JSON.stringify(report, null, 2)}\n`);
    console.log(`  report     ${resolve(String(options.json))}`);
  }
  return report;
}

// ---------------------------------------------------------------------------
// startup and soak share the launch/seed plumbing
// ---------------------------------------------------------------------------
function seedDataDir(seed: string) {
  const captureDir = mkdtempSync(join(tmpdir(), 'xmb-perf-'));
  const dataDir = join(captureDir, 'data');
  mkdirSync(dataDir, { recursive: true });
  for (const entry of readdirSync(seed, { withFileTypes: true })) {
    if (/\.(txt|log)$/.test(entry.name) || entry.name === 'dump.json') continue;
    cpSync(join(seed, entry.name), join(dataDir, entry.name), { recursive: true });
  }
  return { captureDir, dataDir };
}

type Milestone = { name: string; seconds: number };
function parseMilestones(text: string) {
  const milestones: Record<string, number> = {};
  const values: Milestone[] = [];
  for (const line of text.split(/\r?\n/)) {
    const match = line.match(/^\s*([0-9]+(?:\.[0-9]+)?)\|([a-z]+)\|(.*)$/);
    if (!match) continue;
    const at = Number(match[1]);
    const message = match[3];
    if (!('volumeStart' in milestones) && message.startsWith('volume: start')) milestones.volumeStart = at;
    if (!('textAtlas' in milestones) && message.startsWith('text atlas')) milestones.textAtlas = at;
    if (!('catalog' in milestones) && message.startsWith('catalog: settings projection resolved')) milestones.catalog = at;
    if (!('xmbml' in milestones) && message.startsWith('xmbml: network column resolved')) milestones.xmbml = at;
    if (!('renderer' in milestones) && message.startsWith('native renderer')) milestones.renderer = at;
    if (!('display' in milestones) && message.startsWith('native display refresh')) milestones.display = at;
    if (!('content' in milestones) && message.startsWith('content: Steam installed')) milestones.content = at;
    if (!('owned' in milestones) && message.startsWith('steam owned')) milestones.owned = at;
  }
  for (const [name, seconds] of Object.entries(milestones)) values.push({ name, seconds });
  return { milestones, values };
}

function firstSwapMs(tracePath: string) {
  if (!existsSync(tracePath)) return NaN;
  for (const line of readFileSync(tracePath, 'utf8').split(/\r?\n/)) {
    const match = line.match(/^frame swap:\s*(\d+)\s*$/);
    if (match) return Number(match[1]) / 1e6;
  }
  return NaN;
}

// While another full-screen window covers the measured one there is no vsync
// pacing: the frameSwapped -> update() loop saturates the message queue and the
// script's 300 ms timer starves (measured: ~220 fps and no script token consumed
// for five minutes). Bringing the window to the foreground is what makes it
// present at the display rate, so both modes ask for it; --no-activate turns the
// call off, and a run that never finishes is reported as a failure instead of a
// quiet slow number.
const activationScript = (pid: number) =>
  `Add-Type -AssemblyName Microsoft.VisualBasic; [Microsoft.VisualBasic.Interaction]::AppActivate(${pid})`;
async function activateWindow(pid: number, enabled: boolean) {
  if (!enabled || process.platform !== 'win32') return;
  try {
    const proc = Bun.spawn(['powershell', '-NoProfile', '-Command', activationScript(pid)],
      { stdout: 'ignore', stderr: 'ignore' });
    await proc.exited;
  } catch { /* activation is best effort; the run's own completion is the check */ }
}

// ---------------------------------------------------------------------------
// startup
// ---------------------------------------------------------------------------
async function startup(options: Options) {
  const runs = Number(options.runs ?? 7);
  if (!Number.isSafeInteger(runs) || runs < 1 || runs > 50) fail('--runs must be an integer from 1 through 50');
  const exe = options.exe ? resolve(String(options.exe)) : thresholdsExe();
  const flash = options.flash ? resolve(String(options.flash)) : thresholdsFlash();
  const seed = options.seed ? resolve(String(options.seed)) : repoPath('out/native-reference/store/ui3-data');
  if (!existsSync(exe)) fail(`executable not found: ${exe}`);
  if (!existsSync(seed)) fail(`seed data directory not found: ${seed}`);
  type Run = { firstFrameMs: number; milestones: Record<string, number>; displayHz: number | null; seconds: number; stalled: boolean };
  const activate = options['no-activate'] !== true;
  const results: Run[] = [];
  let stalled = 0;
  for (let index = 1; index <= runs; index++) {
    const { captureDir, dataDir } = seedDataDir(seed);
    const tracePath = join(captureDir, 'frames.txt');
    const started = Date.now();
    const proc = Bun.spawn([exe, '--dev-flash', flash, '--data-dir', dataDir, '--no-intro',
      '--script', 'full,wait,quit'], {
      stdout: 'ignore', stderr: 'pipe',
      env: {
        ...process.env as Record<string, string>,
        XMB_FRAME_TRACE: tracePath,
        QT_MESSAGE_PATTERN: '%{time process}|%{type}|%{message}',
      },
    });
    const stderrText = new Response(proc.stderr).text();
    await Bun.sleep(1200);
    await activateWindow(proc.pid, activate);
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; proc.kill(); }, 30000);
    let code: number;
    try { code = await proc.exited; } finally { clearTimeout(timer); }
    const seconds = (Date.now() - started) / 1000;
    const stderr = await stderrText;
    writeFileSync(join(captureDir, 'native.stderr.log'), stderr);
    if (timedOut) {
      // The window never presented, so its script timer could not run and the run
      // says nothing about startup. Report it and leave it out of the statistics.
      stalled++;
      console.log(`run ${index}: STALLED (no window presentation, killed after 30 s); evidence: ${captureDir}`);
      continue;
    }
    if (code !== 0) fail(`startup run ${index}: the app exited ${code}; evidence: ${captureDir}`);
    const { milestones } = parseMilestones(stderr);
    const swapMs = firstSwapMs(tracePath);
    const displayMatch = stderr.match(/native display refresh:\s*([0-9.]+)\s*Hz/);
    const firstFrameMs = Number.isFinite(swapMs) && milestones.volumeStart !== undefined
      ? milestones.volumeStart * 1000 + swapMs : NaN;
    results.push({ firstFrameMs, milestones, displayHz: displayMatch ? Number(displayMatch[1]) : null, seconds, stalled: false });
    console.log(`run ${index}: first frame ${round(firstFrameMs, 0)} ms (catalog ${round((milestones.catalog ?? NaN) * 1000, 0)} ms, `
      + `xmbml ${round((milestones.xmbml ?? NaN) * 1000, 0)} ms, display ${round((milestones.display ?? NaN) * 1000, 0)} ms, `
      + `steam ${round((milestones.content ?? NaN) * 1000, 0)} ms, owned ${round((milestones.owned ?? NaN) * 1000, 0)} ms) `
      + `in ${seconds.toFixed(1)} s${displayMatch ? ` at ${displayMatch[1]} Hz` : ''}`);
    // keep the evidence of every run next to the report
    if (options.json) {
      const keep = join(dirname(resolve(String(options.json))), `startup-run-${index}`);
      mkdirSync(keep, { recursive: true });
      cpSync(captureDir, keep, { recursive: true });
    }
  }
  if (!results.length) fail('every startup run stalled; nothing to report');
  const frameValues = results.map(run => run.firstFrameMs).filter(value => Number.isFinite(value));
  const frameStats = summarize(frameValues);
  const names: Array<[string, string]> = [
    ['catalog', 'catalog (XMBML settings projection)'],
    ['xmbml', 'xmbml (Network column)'],
    ['textAtlas', 'text atlas (GL init)'],
    ['renderer', 'native renderer'],
    ['display', 'native display refresh (window shown)'],
    ['content', 'Steam content scan'],
    ['owned', 'Steam owned library'],
  ];
  const breakdown: any[] = [];
  console.log('');
  console.log('  milestone timings, milliseconds since process start');
  for (const [key, label] of names) {
    const values = results.map(run => run.milestones[key] * 1000).filter(value => Number.isFinite(value));
    const stats = summarize(values);
    breakdown.push({ milestone: key, label, count: stats.count, median: round(stats.p50, 0), min: round(stats.min, 0), max: round(stats.max, 0) });
    console.log(`    ${label.padEnd(42)} median ${String(round(stats.p50, 0)).padStart(6)}  min ${String(round(stats.min, 0)).padStart(6)}  max ${String(round(stats.max, 0)).padStart(6)}  (n=${stats.count})`);
  }
  const report: any = {
    mode: 'startup',
    exe, flash, seed,
    runs,
    stalledRuns: stalled,
    scope: 'process start to the first frame swap (Qt %{time process} of the View construction line plus the first frame swap), then the loader milestones the app already logs',
    firstFrameMs: { median: round(frameStats.p50, 0), min: round(frameStats.min, 0), max: round(frameStats.max, 0), mean: round(frameStats.mean, 0), runs: frameValues.map(value => round(value, 0)) },
    displayHz: results.map(run => run.displayHz).filter(value => value !== null),
    milestones: breakdown,
    perRun: results,
  };
  console.log('');
  console.log(`  first frame: median ${report.firstFrameMs.median} ms, min ${report.firstFrameMs.min} ms, max ${report.firstFrameMs.max} ms over ${frameValues.length} run(s)`
    + (stalled ? `, ${stalled} stalled and excluded` : ''));
  if (options.json) {
    writeFileSync(resolve(String(options.json)), `${JSON.stringify(report, null, 2)}\n`);
    console.log(`  report     ${resolve(String(options.json))}`);
  }
  return report;
}

// ---------------------------------------------------------------------------
// soak
// ---------------------------------------------------------------------------
// The churn cycle repeats one route and writes a notification each pass, because a
// notification is the one event this build logs unconditionally: two Circles return
// to the root from any state, the column sweep crosses every column opening the
// focused row (a page or a dialog) and closing it again, and the video-library and
// Steam-folder steps open a real page when the selection lands where intended. The
// recorded activity is what the app logged (notifications, motion, media starts), so
// a cycle that did less than designed shows up in the numbers instead of being
// assumed.
const churnCycle = ['ESC', 'ESC', 'wait',
  'NOTIFY=Soak churn', 'wait', 'wait',
  'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'wait',
  'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'wait', 'ENTER', 'wait', 'wait', 'ESC', 'wait',
  'LEFT', 'LEFT', 'LEFT', 'LEFT', 'LEFT', 'wait',
  'UP', 'UP', 'UP', 'UP', 'UP', 'wait', 'DOWN', 'wait', 'ENTER', 'wait', 'wait', 'wait', 'ESC', 'wait',
  'RIGHT', 'RIGHT', 'wait',
  'UP', 'UP', 'UP', 'UP', 'UP', 'wait', 'DOWN', 'wait', 'ENTER', 'wait', 'wait', 'wait', 'ESC', 'wait'];

// The script timer's interval is 300 ms, but the app delivers a token only when its
// event loop is idle; measured here at 0.37-0.55 s per token (and slower under load),
// so a session's token count is sized for ~0.8 s/token to make --minutes mean minutes
// of wall clock. The runner also reports the wall time it actually spent.
const observedTokenSeconds = 0.8;

function soakTokens(minutes: number) {
  const budget = Math.max(20, Math.round(minutes * 60 / observedTokenSeconds));
  const tokens = ['full', 'wait', 'wait', 'wait', 'wait'];
  while (tokens.length + churnCycle.length <= budget) tokens.push(...churnCycle);
  while (tokens.length < budget) tokens.push('wait');
  tokens.push('quit');
  return tokens;
}

async function soak(options: Options) {
  const minutes = Number(options.minutes ?? 20);
  if (!Number.isFinite(minutes) || minutes <= 0 || minutes > 180) fail('--minutes must be a number from 0 through 180');
  const sampleMs = Number(options['sample-ms'] ?? 5000);
  if (!Number.isSafeInteger(sampleMs) || sampleMs < 500) fail('--sample-ms must be an integer of at least 500');
  const exe = options.exe ? resolve(String(options.exe)) : thresholdsExe();
  const flash = options.flash ? resolve(String(options.flash)) : thresholdsFlash();
  const seed = options.seed ? resolve(String(options.seed)) : repoPath('out/native-reference/store/video-data');
  if (!existsSync(exe)) fail(`executable not found: ${exe}`);
  if (!existsSync(seed)) fail(`seed data directory not found: ${seed}`);
  const { captureDir, dataDir } = seedDataDir(seed);
  const tracePath = join(captureDir, 'frames.txt');
  const csvPath = join(captureDir, 'memory.csv');
  const tokens = options['no-churn'] === true ? [] : soakTokens(minutes);
  const characters = tokens.join(',').length;
  // Windows caps a CreateProcess command line at 32,767 characters; fail before the
  // spawn rather than truncating the session silently.
  if (characters > 30000) fail(`the script is ${characters} characters, too close to the Windows command-line limit; use fewer minutes`);
  console.log(`soak: ${minutes} minute(s) of wall clock, `
    + (tokens.length ? `${tokens.length} script tokens (${characters} characters)` : 'no input (continuous rendering only)')
    + `, sampling every ${sampleMs} ms`);
  console.log(`  exe   ${exe}`);
  console.log(`  data  ${dataDir}`);
  const args = [exe, '--dev-flash', flash, '--data-dir', dataDir, '--no-intro', '--ui-clock', '2026-10-25T12:32:00'];
  if (tokens.length) args.push('--script', tokens.join(','));
  const proc = Bun.spawn(args, {
    stdout: 'ignore', stderr: 'pipe',
    env: {
      ...process.env as Record<string, string>,
      XMB_FRAME_TRACE: tracePath,
      // Qt's own timestamp on every line is what lets the churn be measured per
      // minute and a stalled churn be reported instead of averaged in.
      QT_MESSAGE_PATTERN: '%{time process}|%{type}|%{message}',
      XMB_MEDIA_TRACE: '1',
      XMB_STEAM_ARTWORK_LOG: '1',
      XMB_NOTIFY_LOG: '1',
      XMB_MOTION_LOG: '1',
      XMB_STEAM_ARTWORK: '0',
    },
  });
  const stderrText = new Response(proc.stderr).text();
  await Bun.sleep(1500);
  const activate = options['no-activate'] !== true;
  await activateWindow(proc.pid, activate);
  // One PowerShell sampler for the whole session: it lives exactly as long as the
  // app process, so sampling adds no spawn per sample.
  const samplerScript =
    `$targetPid=${proc.pid}; while($true){ `
    + `$p=Get-Process -Id $targetPid -ErrorAction SilentlyContinue; if(-not $p){break}; `
    + `Add-Content -Path '${csvPath.replaceAll("'", "''")}' -Value ((Get-Date -Format o)+','+$p.WorkingSet64+','+$p.PrivateMemorySize64+','+$p.Handles+','+$p.CPU); `
    + `Start-Sleep -Milliseconds ${sampleMs} }`;
  const sampler = Bun.spawn(['powershell', '-NoProfile', '-Command', samplerScript], { stdout: 'ignore', stderr: 'ignore' });
  const started = Date.now();
  const budgetSeconds = options['no-churn'] === true ? minutes * 60 + 30 : minutes * 60 * 1.6 + 120;
  const timer = setTimeout(() => { console.error(`perf-harden: soak exceeded its ${minutes} minute budget; closing the window`); Bun.spawn(['taskkill', '/PID', String(proc.pid)], { stdout: 'ignore', stderr: 'ignore' }); setTimeout(() => { try { proc.kill(); } catch { /* already gone */ } }, 10000); }, budgetSeconds * 1000);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const seconds = (Date.now() - started) / 1000;
  try { sampler.kill(); } catch { /* already gone */ }
  const stderr = await stderrText;
  writeFileSync(join(captureDir, 'native.stderr.log'), stderr);
  // Churn heartbeat: every logged page build and media start carries the process
  // time of the line, so a session whose script timer stalled part-way is visible
  // as a heartbeat that stops, instead of quietly averaging into the memory trend.
  const churnPattern = /media: directory result|continue watching: page opened|steam downloads: page opened|media queue: play|media: resumed at|shell: options opened|notify: enqueue|steam folder \w+|motion: \d+ ms position/;
  const churn: Array<{ at: number; message: string }> = [];
  let lastFrameReport = 0;
  for (const line of stderr.split(/\r?\n/)) {
    const match = line.match(/^\s*([0-9]+(?:\.[0-9]+)?)\|([a-z]+)\|(.*)$/);
    if (!match) continue;
    const at = Number(match[1]);
    if (match[3].startsWith('native frames:')) lastFrameReport = at;
    if (churnPattern.test(match[3])) churn.push({ at, message: match[3] });
  }
  const samples: Array<{ at: number; workingSet: number; privateBytes: number; handles: number }> = [];
  if (existsSync(csvPath)) {
    for (const line of readFileSync(csvPath, 'utf8').split(/\r?\n/)) {
      const parts = line.trim().split(',');
      if (parts.length !== 5) continue;
      const at = Date.parse(parts[0]);
      if (!Number.isFinite(at)) continue;
      samples.push({ at, workingSet: Number(parts[1]), privateBytes: Number(parts[2]), handles: Number(parts[3]) });
    }
  }
  if (!samples.length) fail(`soak produced no memory samples; evidence: ${captureDir}`);
  const first = samples[0];
  const last = samples[samples.length - 1];
  const spanMinutes = (last.at - first.at) / 60000;
  const working = samples.map(sample => sample.workingSet);
  const workingStats = summarize(working);
  const peak = Math.max(...working);
  // Growth is measured across the churn window only: the first samples are startup
  // (asset decode), which is not a leak signal, and a stalled tail is reported.
  const churnStartAt = first.at + 15000;
  const churnSamples = samples.filter(sample => sample.at >= churnStartAt);
  const churnFirst = churnSamples.length ? churnSamples[0] : first;
  const churnLast = churnSamples.length ? churnSamples[churnSamples.length - 1] : last;
  const churnGrowthMb = (churnLast.workingSet - churnFirst.workingSet) / (1024 * 1024);
  const churnMinutes = (churnLast.at - churnFirst.at) / 60000;
  const growthMb = (last.workingSet - first.workingSet) / (1024 * 1024);
  const growthPerHourMb = spanMinutes > 0 ? growthMb / spanMinutes * 60 : NaN;
  const churnPerHourMb = churnMinutes > 0 ? churnGrowthMb / churnMinutes * 60 : NaN;
  const gaps: number[] = [];
  for (let index = 1; index < churn.length; index++) gaps.push(churn[index].at - churn[index - 1].at);
  const maxChurnGap = gaps.length ? Math.max(...gaps) : NaN;
  const lastChurnAt = churn.length ? churn[churn.length - 1].at : NaN;
  const frameSwaps = existsSync(tracePath) ? readFileSync(tracePath, 'utf8').split(/\r?\n/).filter(line => line.startsWith('frame swap:')).length : 0;
  const pageOpens = churn.filter(entry => /media: directory result|page opened/.test(entry.message)).length;
  const mediaStarts = churn.filter(entry => /media queue: play|media: resumed at/.test(entry.message)).length;
  const notifications = churn.filter(entry => entry.message.startsWith('notify: enqueue')).length;
  const completedNaturally = code === 0;
  const finished = completedNaturally;
  const report: any = {
    mode: 'soak',
    exe, seed, minutes, sampleMs,
    processExit: code,
    completed: finished,
    wallSeconds: round(seconds, 1),
    driver: { kind: '--script tokens', tokenCount: tokens.length, tokenCharacters: characters },
    scope: 'one process, scripted page/media churn; memory read from the running process working set, private bytes and handle count',
    memory: {
      samples: samples.length,
      spanMinutes: round(spanMinutes, 2),
      firstMb: round(first.workingSet / (1024 * 1024), 1),
      lastMb: round(last.workingSet / (1024 * 1024), 1),
      medianMb: round(workingStats.p50 / (1024 * 1024), 1),
      peakMb: round(peak / (1024 * 1024), 1),
      growthMb: round(growthMb, 1),
      growthPerHourMb: round(growthPerHourMb, 1),
      churnWindowMb: round(churnGrowthMb, 1),
      churnWindowPerHourMb: round(churnPerHourMb, 1),
      churnWindowMinutes: round(churnMinutes, 2),
      firstHandles: first.handles,
      lastHandles: last.handles,
      peakHandles: Math.max(...samples.map(sample => sample.handles)),
    },
    activity: {
      pageOpens, mediaStarts, notifications, frameSwaps,
      churnEvents: churn.length,
      lastChurnAtSeconds: round(lastChurnAt, 1),
      maxChurnGapSeconds: round(maxChurnGap, 1),
      lastFrameReportSeconds: round(lastFrameReport, 1),
      churnEventsPerMinute: round(churn.length / Math.max(1, spanMinutes), 1),
    },
    trace: tracePath,
    stderr: join(captureDir, 'native.stderr.log'),
    evidence: captureDir,
    samples: samples.map(sample => ({ at: new Date(sample.at).toISOString(), workingSetMb: round(sample.workingSet / (1024 * 1024), 1), handles: sample.handles })),
  };
  console.log(`  exit       ${code} after ${round(seconds, 0)} s (${finished ? 'completed' : 'did NOT complete'})`);
  console.log(`  memory     first ${report.memory.firstMb} MB, last ${report.memory.lastMb} MB, median ${report.memory.medianMb} MB, peak ${report.memory.peakMb} MB`);
  console.log(`  growth     ${report.memory.growthMb} MB over ${report.memory.spanMinutes} min (${report.memory.growthPerHourMb} MB/hour)`);
  console.log(`  churn      ${report.memory.churnWindowMb} MB over ${report.memory.churnWindowMinutes} min after warmup (${report.memory.churnWindowPerHourMb} MB/hour), `
    + `handles ${report.memory.firstHandles} -> ${report.memory.lastHandles} (peak ${report.memory.peakHandles})`);
  console.log(`  activity   ${pageOpens} page open(s), ${mediaStarts} media start(s), ${notifications} notification(s), `
    + `${report.activity.churnEvents} churn event(s) (last at ${report.activity.lastChurnAtSeconds} s, max gap ${report.activity.maxChurnGapSeconds} s)`);
  if (options.json) {
    writeFileSync(resolve(String(options.json)), `${JSON.stringify(report, null, 2)}\n`);
    console.log(`  report     ${resolve(String(options.json))}`);
  }
  if (options.keep) console.log(`  evidence   ${captureDir}`);
  return report;
}

// ---------------------------------------------------------------------------
try {
  const mode = process.argv[2];
  if (!mode || mode === '--help' || mode === '-h') { console.log(usage); process.exit(mode ? 0 : 2); }
  const rest = process.argv.slice(3);
  if (mode === 'pacing') {
    pacing(parseOptions(rest, new Set(['trace', 'log', 'warmup-ms', 'json', 'display-hz', 'run', 'attempts']), new Set(['no-activate'])));
  } else if (mode === 'startup') {
    await startup(parseOptions(rest, new Set(['runs', 'exe', 'flash', 'seed', 'json']), new Set(['no-activate'])));
  } else if (mode === 'soak') {
    await soak(parseOptions(rest, new Set(['minutes', 'sample-ms', 'exe', 'flash', 'seed', 'json']), new Set(['keep', 'no-activate', 'no-churn'])));
  } else {
    fail(`unknown mode ${JSON.stringify(mode)}\n${usage}`);
  }
} catch (error) {
  console.error(`perf-harden: ${error instanceof Error ? error.message : String(error)}`);
  process.exit(2);
}
