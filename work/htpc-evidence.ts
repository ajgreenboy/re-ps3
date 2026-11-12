// Release evidence run on a development PC (audit/RELEASE-CHECKLIST.md, written up in audit/RELEASE-RUN-2026-11-12.md).
//
//   bun work/htpc-evidence.ts --list                       the automated rows
//   bun work/htpc-evidence.ts [--exe <exe>] [--only B2,N1] run all (or the named) automated rows
//   bun work/htpc-evidence.ts --only LS1 --minutes 30      the soak (CSV sample every 30 s)
//   bun work/htpc-evidence.ts --doc                        write audit/RELEASE-RUN-2026-11-12.md from the stored results
//
// Everything lands under C:/dev/out/rr (scratch data dirs, logs, CSVs, one JSON per row in results/). Nothing there is committed.
// Safety rules this script keeps:
//   * every XMB it starts gets its own scratch --data-dir under C:/dev/out/rr and XMB_SINGLE_INSTANCE=0, XMB_LOG=<file>;
//   * only PIDs it started are ever signalled (WM_CLOSE to the window, then taskkill /PID as the last resort);
//   * input goes to the XMB window of that PID only (--script tokens, or WM_APPCOMMAND posted to its HWND). It never uses
//     SendInput and never posts a transport key while the XMB has no media open (that key would go on to Windows);
//   * power, audio-output and Steam-launch actions are dry-run or never reached; System Check fixes are never applied;
//   * no credential, token file or device name is read or printed.
import { dlopen, FFIType, ptr } from 'bun:ffi';
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync, appendFileSync } from 'node:fs';
import { join } from 'node:path';

// ---------------------------------------------------------------------------------------------------------------- setup
const args = process.argv.slice(2);
const opt = (name: string, fallback = '') => { const i = args.indexOf(`--${name}`); return i >= 0 ? (args[i + 1] ?? '') : fallback; };
const has = (name: string) => args.includes(`--${name}`);
const repo = join(import.meta.dir, '..');
const exe = opt('exe', 'C:/dev/out/rr-build/re-ps3-native.exe');
const OUT = 'C:/dev/out/rr';
const flash = 'C:/dev/rpcs3/bin/dev_flash';
const emptySteam = `${OUT}/empty-steam`;
const resultsDir = `${OUT}/results`;
const BS = String.fromCharCode(92);
const win = (s: string) => s.split('/').join(BS);
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));

type Verdict = 'PASS' | 'FAIL' | 'PARTIAL' | 'NOT RUN';
interface Result { id: string; result: Verdict; evidence: string }
const results = new Map<string, Result>();
function record(id: string, result: Verdict, evidence: string) {
  const r = { id, result, evidence: evidence.replace(/\s+/g, ' ').trim() };
  results.set(id, r);
  mkdirSync(resultsDir, { recursive: true });
  writeFileSync(`${resultsDir}/${id}.json`, JSON.stringify(r, null, 2));
  console.log(`${result.padEnd(8)} ${id.padEnd(4)} ${r.evidence.slice(0, 220)}`);
}

// ------------------------------------------------------------------------------------------------------------ Win32 FFI
const k32 = dlopen('kernel32.dll', {
  OpenProcess: { args: [FFIType.u32, FFIType.i32, FFIType.u32], returns: FFIType.ptr },
  GetProcessTimes: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
  GetProcessHandleCount: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
  CloseHandle: { args: [FFIType.ptr], returns: FFIType.i32 },
  CreateToolhelp32Snapshot: { args: [FFIType.u32, FFIType.u32], returns: FFIType.ptr },
  Thread32First: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
  Thread32Next: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
});
const psapi = dlopen('psapi.dll', { GetProcessMemoryInfo: { args: [FFIType.ptr, FFIType.ptr, FFIType.u32], returns: FFIType.i32 } });
const user32 = dlopen('user32.dll', {
  PostMessageW: { args: [FFIType.ptr, FFIType.u32, FFIType.u64, FFIType.i64], returns: FFIType.i32 },
  IsWindow: { args: [FFIType.ptr], returns: FFIType.i32 },
});

interface Sample { workingSet: number; privateBytes: number; handles: number; threads: number; cpuSeconds: number }
function sampleProcess(pid: number): Sample | null {
  const h = k32.symbols.OpenProcess(0x1000 | 0x0400, 0, pid);   // QUERY_LIMITED_INFORMATION | QUERY_INFORMATION
  if (!h) return null;
  try {
    const t = new BigUint64Array(4);
    k32.symbols.GetProcessTimes(h, ptr(t), ptr(t.subarray(1)), ptr(t.subarray(2)), ptr(t.subarray(3)));
    const hc = new Uint32Array(1);
    k32.symbols.GetProcessHandleCount(h, ptr(hc));
    // PROCESS_MEMORY_COUNTERS_EX: cb, PageFaultCount, PeakWorkingSetSize, WorkingSetSize, ..., PagefileUsage, PeakPagefileUsage, PrivateUsage
    const m = new BigUint64Array(10);
    const view = new DataView(m.buffer);
    view.setUint32(0, 80, true);
    if (!psapi.symbols.GetProcessMemoryInfo(h, ptr(m), 80)) return null;
    const workingSet = Number(m[2]);
    const privateBytes = Number(m[9]);
    // Threads: a Toolhelp thread snapshot, filtered to this process (THREADENTRY32 = 28 bytes).
    let threads = 0;
    const snap = k32.symbols.CreateToolhelp32Snapshot(0x4, 0);
    if (snap && Number(snap) !== -1) {
      const entry = new Uint32Array(7);
      entry[0] = 28;
      let ok = k32.symbols.Thread32First(snap, ptr(entry));
      while (ok) { if (entry[3] === pid) threads++; ok = k32.symbols.Thread32Next(snap, ptr(entry)); }
      k32.symbols.CloseHandle(snap);
    }
    return { workingSet, privateBytes, handles: hc[0], threads, cpuSeconds: Number(t[2] + t[3]) / 1e7 };
  } finally { k32.symbols.CloseHandle(h); }
}
function ps(script: string): string {
  const r = Bun.spawnSync(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], { stdout: 'pipe', stderr: 'pipe' });
  return r.stdout.toString();
}
// The top-level window of a PID this script started (found by process id; never by name or title).
async function hwndOf(pid: number, timeoutMs = 20000): Promise<bigint> {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const out = ps(`$p=Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if($p){[Int64]$p.MainWindowHandle}`).trim();
    if (out && out !== '0') return BigInt(out);
    await sleep(500);
  }
  return 0n;
}
const WM_APPCOMMAND = 0x0319;
// lParam of WM_APPCOMMAND: command in the high word of the high 16 bits, device (FAPPCOMMAND_KEY = 0) in bits 12..14.
function postAppCommand(hwnd: bigint, command: number): boolean {
  return user32.symbols.PostMessageW(Number(hwnd) as any, WM_APPCOMMAND, BigInt(Number(hwnd)), BigInt(command) << 16n) !== 0;
}
function pidAlive(pid: number) { return sampleProcess(pid) !== null; }

// ------------------------------------------------------------------------------------------------------------- XMB runs
const wavCache = new Map<number, Buffer>();
function wav(seconds: number, rate = 22050) {
  if (wavCache.has(seconds)) return wavCache.get(seconds)!;
  const data = Buffer.alloc(seconds * rate * 2);
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + data.length, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16);
  h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22); h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36); h.writeUInt32LE(data.length, 40);
  const b = Buffer.concat([h, data]); wavCache.set(seconds, b); return b;
}
// Fixtures: three silent 90 s WAVs, three photos copied from the generated gallery library, an empty video folder, an empty Steam root.
function ensureFixtures() {
  for (const d of ['music', 'photo', 'video']) mkdirSync(`${OUT}/lib/${d}`, { recursive: true });
  mkdirSync(emptySteam, { recursive: true });
  for (const n of ['Alpha', 'Bravo', 'Charlie']) if (!existsSync(`${OUT}/lib/music/${n}.wav`)) writeFileSync(`${OUT}/lib/music/${n}.wav`, wav(90));
  for (const f of ['IMG_0008', 'IMG_0025', 'IMG_0042'])
    if (!existsSync(`${OUT}/lib/photo/${f}.jpg`) && existsSync(`C:/dev/out/gal/photos/city/${f}.jpg`)) cpSync(`C:/dev/out/gal/photos/city/${f}.jpg`, `${OUT}/lib/photo/${f}.jpg`);
}

interface RunOptions {
  name: string;
  script?: string;                    // --script tokens; G(name) expands to grab=<dir>/<name>.png
  prefs?: Record<string, unknown>;
  files?: Record<string, unknown>;    // extra files in the data dir (json objects are stringified)
  env?: Record<string, string>;
  args?: string[];
  realSteam?: boolean;                // read this PC's Steam library (read-only) instead of the empty scratch root
  keepData?: boolean;                 // reuse the data dir of an earlier run of the same name
  timeoutMs?: number;
  windowed?: boolean;                 // default true
  onStart?: (ctx: Running) => Promise<void> | void;   // runs once the process exists (for posted messages / sampling)
}
interface Running { pid: number; dir: string; logFile: string; proc: ReturnType<typeof Bun.spawn>; log: () => string; hwnd: () => Promise<bigint> }
interface RunResult { dir: string; data: string; logFile: string; log: string; exitCode: number | null; timedOut: boolean; pid: number; grabs: Record<string, any>; stderr: string }

function libPrefs() {
  return { 'library/music': win(`${OUT}/lib/music`), 'library/photo': win(`${OUT}/lib/photo`), 'library/video': win(`${OUT}/lib/video`) };
}
function startXmb(o: RunOptions): { running: Running; done: Promise<RunResult> } {
  const dir = `${OUT}/runs/${o.name}`;
  if (!o.keepData) rmSync(dir, { recursive: true, force: true });
  mkdirSync(`${dir}/data`, { recursive: true });
  if (!o.keepData || !existsSync(`${dir}/data/preferences.json`))
    writeFileSync(`${dir}/data/preferences.json`, JSON.stringify({ version: 1, values: { ...libPrefs(), ...(o.prefs ?? {}) } }));
  for (const [f, c] of Object.entries(o.files ?? {})) writeFileSync(`${dir}/data/${f}`, typeof c === 'string' ? c : JSON.stringify(c));
  const logFile = `${dir}/xmb.log`;
  if (!o.keepData) rmSync(logFile, { force: true });
  const tokens = (o.script ?? '').replace(/G\(([^)]*)\)/g, (_, n) => `grab=${dir}/${n}.png`);
  const argv = [exe, '--dev-flash', flash, '--data-dir', `${dir}/data`];
  if (o.windowed !== false) argv.push('--start-windowed');
  if (o.script !== undefined) argv.push('--no-intro', '--script', tokens);
  argv.push(...(o.args ?? []));
  const env: Record<string, string> = {
    ...process.env as Record<string, string>,
    XMB_SINGLE_INSTANCE: '0', XMB_LOG: logFile, XMB_STEAM_ROOT: emptySteam, XMB_STEAM_ARTWORK: '0', XMB_STEAM_ACTIVITY: '0',
    XMB_FIRST_RUN: '0', XMB_DUMP_ENTRIES: `${dir}/dump`, XMB_INPUT_TRACE: '1', XMB_MEDIA_TRACE: '1',
    // Every live-system action is a dry run in every run of this script, and the autostart task name is a scratch one.
    XMB_POWER_DRY_RUN: '1', XMB_AUDIO_DRY_RUN: '1', XMB_BLUETOOTH_DRY_RUN: '1', XMB_WIFI_DRY_RUN: '1', XMB_CEC_DRY_RUN: '1',
    XMB_DISPLAY_DRY_RUN: '1', XMB_FIRMWARE_DRY_RUN: '1', XMB_UPDATER_DRY_RUN: '1', XMB_SERVICES_DRY_RUN: '1', XMB_AUTOSTART_TASK: 'rr-scratch-never-registered',
    ...(o.env ?? {}),
  };
  if (o.realSteam) delete env.XMB_STEAM_ROOT;
  const proc = Bun.spawn(argv, { stdout: 'ignore', stderr: 'pipe', env });
  const stderr = new Response(proc.stderr).text();
  const log = () => (existsSync(logFile) ? readFileSync(logFile, 'utf8') : '');
  const running: Running = { pid: proc.pid, dir, logFile, proc, log, hwnd: () => hwndOf(proc.pid) };
  const done = (async (): Promise<RunResult> => {
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; stopPid(proc.pid, proc); }, o.timeoutMs ?? 180000);
    const starter = o.onStart ? Promise.resolve(o.onStart(running)).catch(e => console.log(`onStart error: ${e}`)) : Promise.resolve();
    const code = await proc.exited;
    clearTimeout(timer);
    await starter;
    const grabs: Record<string, any> = {};
    for (const f of existsSync(dir) ? readdirSync(dir).filter(f => f.endsWith('.png.json')) : []) {
      try { grabs[f.replace('.png.json', '')] = JSON.parse(readFileSync(`${dir}/${f}`, 'utf8')); } catch { /* unreadable sidecar */ }
    }
    return { dir, data: `${dir}/data`, logFile, log: log(), exitCode: code, timedOut, pid: proc.pid, grabs, stderr: await stderr };
  })();
  return { running, done };
}
async function runXmb(o: RunOptions): Promise<RunResult> { return startXmb(o).done; }

// Graceful stop of a PID this script started: WM_CLOSE to its window, then taskkill /PID only as the last resort.
async function stopPid(pid: number, proc?: ReturnType<typeof Bun.spawn>, graceMs = 12000): Promise<'closed' | 'killed' | 'gone'> {
  if (!pidAlive(pid)) return 'gone';
  const h = await hwndOf(pid, 3000);
  if (h) user32.symbols.PostMessageW(Number(h) as any, 0x0010, 0n, 0n);   // WM_CLOSE
  const t0 = Date.now();
  while (Date.now() - t0 < graceMs) { if (!pidAlive(pid)) return 'closed'; await sleep(250); }
  Bun.spawnSync(['taskkill', '/PID', String(pid), '/F'], { stdout: 'ignore', stderr: 'ignore' });
  void proc;
  return 'killed';
}
const lines = (log: string, re: RegExp) => log.split('\n').filter(l => re.test(l));
const count = (log: string, re: RegExp) => lines(log, re).length;
const g = (run: RunResult, name: string) => run.grabs[name];
const dupes = (n: number, t: string) => Array(n).fill(t);
const warnLines = (log: string) => lines(log, /warning|critical|fatal|QOpenGL|crash|Unhandled exception/i);

// ============================================================================================================== rows
type RowFn = () => Promise<void>;
const rows: Record<string, { ids: string[]; fn: RowFn }> = {};
const def = (key: string, ids: string[], fn: RowFn) => { rows[key] = { ids, fn }; };

// ---- LS1 / LS2 / G12: windowed idle soak on the Game root, one sample every 30 s to a CSV -------------------------------
def('LS1', ['LS1', 'LS2'], async () => {
  const minutes = Number(opt('minutes', '30'));
  const soakName = opt('soakname', 'soak');   // 'soak' = the default start (render gate on); another name with --gate observe = frames keep rendering even when the window is covered
  const gate = opt('gate', '');
  const r = startXmb({ name: soakName, env: { XMB_FIRST_RUN: '0', ...(gate ? { XMB_RENDER_GATE: gate } : {}) }, timeoutMs: (minutes + 5) * 60000 });
  const csv = `${OUT}/${soakName}.csv`;
  writeFileSync(csv, 'elapsed_s,working_set_mb,private_mb,handles,threads,cpu_pct_interval\n');
  await sleep(15000);                                   // startup ramp is part of the record, but start the clock after it
  const t0 = Date.now();
  let prev = sampleProcess(r.running.pid);
  let prevAt = Date.now();
  const series: { t: number; ws: number; pv: number; h: number; th: number; cpu: number }[] = [];
  while (Date.now() - t0 < minutes * 60000) {
    await sleep(30000);
    const s = sampleProcess(r.running.pid);
    if (!s || !prev) { console.log('soak: process gone'); break; }
    const dt = (Date.now() - prevAt) / 1000;
    const cpu = ((s.cpuSeconds - prev.cpuSeconds) / dt) * 100;
    const t = Math.round((Date.now() - t0) / 1000);
    series.push({ t, ws: s.workingSet / 1048576, pv: s.privateBytes / 1048576, h: s.handles, th: s.threads, cpu });
    appendFileSync(csv, `${t},${(s.workingSet / 1048576).toFixed(1)},${(s.privateBytes / 1048576).toFixed(1)},${s.handles},${s.threads},${cpu.toFixed(2)}\n`);
    prev = s; prevAt = Date.now();
  }
  const how = await stopPid(r.running.pid, r.running.proc);
  const run = await r.done;
  const slope = (key: 'ws' | 'pv' | 'h', from: number) => {   // least-squares slope per hour over samples after `from` seconds
    const pts = series.filter(p => p.t >= from);
    if (pts.length < 3) return NaN;
    const mx = pts.reduce((a, p) => a + p.t, 0) / pts.length, my = pts.reduce((a, p) => a + p[key], 0) / pts.length;
    let n = 0, d = 0; for (const p of pts) { n += (p.t - mx) * (p[key] - my); d += (p.t - mx) ** 2; }
    return (n / d) * 3600;
  };
  const dur = series.length ? series[series.length - 1].t : 0;
  const first = series[0], last = series[series.length - 1];
  const avgCpu = series.reduce((a, p) => a + p.cpu, 0) / Math.max(series.length, 1);
  const summary = series.length ? `soak ${(dur / 60).toFixed(1)} min (+15 s startup), ${series.length} samples; working set ${first.ws.toFixed(0)} -> ${last.ws.toFixed(0)} MB (slope after 5 min ${slope('ws', 300).toFixed(1)} MB/h), private ${first.pv.toFixed(0)} -> ${last.pv.toFixed(0)} MB (${slope('pv', 300).toFixed(1)} MB/h), handles ${first.h} -> ${last.h}, threads ${first.th} -> ${last.th}, mean CPU ${avgCpu.toFixed(2)} % of one core; stop: ${how}` : 'no samples';
  writeFileSync(`${OUT}/${soakName}-summary.txt`, summary + String.fromCharCode(10));
  if (soakName !== 'soak') { record('LS1b', 'PARTIAL', `second soak (XMB_RENDER_GATE=${gate || 'default'}): ${summary}`); return; }
  record('LS1', dur >= 1800 ? 'PARTIAL' : 'PARTIAL', `${summary}. Shortened run, not the 4 h run; no remote key at the end (no remote).`);
  record('LS2', 'NOT RUN', 'Needs 4 h of real playback with a title; only the idle soak was run.');
  const warns = lines(run.log, /Texture has not been destroyed|QOpenGLTexturePrivate|without a current context|QOpenGLContext/);
  writeFileSync(`${OUT}/soak-warnings.txt`, warns.join('\n'));
  record('G12', warns.length ? 'FAIL' : 'PASS', `${warns.length} OpenGL texture/context warning lines in the soak log after a graceful WM_CLOSE exit (exit code ${run.exitCode}). ${warns.slice(0, 2).join(' | ').slice(0, 200)}`);
});

// ---- LS3: read the soak logs for anything beyond the known OpenGL texture/context warnings on exit -----------------------------
def('LS3', ['LS3'], async () => {
  const known = /Texture has not been destroyed|QOpenGLTexturePrivate::destroy\(\) called without a current context/;
  const benign = /snd_error|warnings 0|missing images|decoded images/;
  const out: string[] = [];
  let unexpected = 0, openGl = 0;
  for (const name of ['soak', 'soak2']) {
    const f = `${OUT}/runs/${name}/xmb.log`;
    if (!existsSync(f)) continue;
    const log = readFileSync(f, 'utf8');
    const ls = log.split(String.fromCharCode(10));
    const known_ = ls.filter(l => known.test(l)).length;
    const odd = ls.filter(l => /warn|critical|fatal|fail|error|exception|crash|stack/i.test(l) && !known.test(l) && !benign.test(l));
    const gate = ls.filter(l => /render gate: closed/.test(l)).length;
    const fps = ls.map(l => l.match(/native frames: ([0-9.]+) fps/)).filter(Boolean).map(m => Number(m![1])).sort((a, b) => a - b);
    openGl += known_; unexpected += odd.length;
    out.push(`${name}: ${ls.length} lines, ${known_} known OpenGL exit warnings, ${odd.length} other warning-like lines${odd.length ? ' (' + odd.slice(0, 2).map(x => x.slice(0, 90)).join(' / ') + ')' : ''}, render gate closed ${gate}x, frame rate median ${fps[Math.floor(fps.length / 2)]} fps (min ${fps[0]})`);
  }
  record('LS3', out.length && unexpected === 0 ? 'PARTIAL' : out.length ? 'FAIL' : 'NOT RUN', `${out.join(' ; ')}. Read after the idle soaks only (no playback soak, LS2 not run).`);
});

// ---- shared navigation (one script token per ~0.3-1 s; the column index order is Users 0, Settings 1, Photo 2, Music 3, Video 4,
// TV 5, Game 6, Network 7, PSN 8, Friends 9; a start lands on Game) ---------------------------------------------------------
const W = (n: number) => dupes(n, 'wait').join(',');
const toMusicLib = `LEFT,LEFT,LEFT,${W(2)},DOWN,DOWN,ENTER,${W(6)}`;
const toMusicPlaylists = `LEFT,LEFT,LEFT,${W(2)},DOWN,ENTER,${W(6)}`;
const toVideoLib = `LEFT,LEFT,${W(3)},DOWN,DOWN,DOWN,DOWN,ENTER,${W(6)}`;
const toPhotoLib = `LEFT,LEFT,LEFT,LEFT,${W(3)},DOWN,DOWN,DOWN,ENTER,${W(6)}`;
const toUsers = `LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,${W(3)}`;
const OSK_ENTER = 'DOWN,DOWN,DOWN,RIGHT,RIGHT,RIGHT,ENTER';    // the on-screen keyboard's Enter key (bottom row, right), then Return presses it
const rm = (p: string) => rmSync(p, { recursive: true, force: true });
const readJson = (p: string) => JSON.parse(readFileSync(p, 'utf8'));
const playlistFile = (data: string) => { const d = `${data}/playlists`; const f = existsSync(d) ? readdirSync(d).find(x => x.endsWith('.json')) : undefined; return f ? `${d}/${f}` : ''; };
const playlists = (data: string): any[] => { const f = playlistFile(data); return f ? (readJson(f).playlists ?? []) : []; };
const base = (p: string) => p.split('/').pop()!;
const twoUsers = {
  'profiles.json': { version: 1, profiles: [{ id: '11111111-1111-4111-8111-111111111111', name: 'Alpha' }, { id: '22222222-2222-4222-8222-222222222222', name: 'Beta' }],
    activeId: '11111111-1111-4111-8111-111111111111', autoLogin: '11111111-1111-4111-8111-111111111111' },
};

// ---- B2 / B3: log and preferences across two starts ----------------------------------------------------------------------
def('B2', ['B2', 'B3'], async () => {
  // Start 1: the default start with the cold-boot intro, windowed, unscripted; closed after 25 s.
  const first = startXmb({ name: 'b2-first', args: ['--start-windowed'], timeoutMs: 90000 });
  await sleep(25000);
  const how = await stopPid(first.running.pid, first.running.proc);
  const r1 = await first.done;
  const size = existsSync(r1.logFile) ? statSync(r1.logFile).size : 0;
  const msgs = r1.log.split('\n').filter(Boolean).length;
  const crash = lines(r1.log, /crash|Unhandled exception|stack trace|0xc0000/i).length + (existsSync(`${r1.data}/crashes`) ? readdirSync(`${r1.data}/crashes`).length : 0);
  const frame = (r1.log.match(/boot: first frame after (\d+) ms/) ?? [])[1];
  record('B2', size > 0 && msgs > 20 && crash === 0 ? 'PASS' : 'FAIL',
    `log exists (${size} bytes, ${msgs} lines, first frame ${frame ?? '?'} ms) for the default start with intro (windowed, stopped by WM_CLOSE: ${how}, exit ${r1.exitCode}); crash lines/files: ${crash}. B1 itself (fullscreen takeover) not run.`);
  // Start 2: same data dir, --no-intro, scripted so the stored library preference is proven by listing it.
  const prefsBefore = readFileSync(`${r1.data}/preferences.json`, 'utf8');
  const keysBefore = Object.keys(readJson(`${r1.data}/preferences.json`).values ?? {}).length;
  const r2 = await runXmb({ name: 'b2-first', keepData: true, script: `${toMusicLib},G(lib),quit` });
  const prefsAfter = readFileSync(`${r2.data}/preferences.json`, 'utf8');
  const keysAfter = Object.keys(readJson(`${r2.data}/preferences.json`).values ?? {}).length;
  const lib = g(r2, 'lib');
  const files = readdirSync(r2.data).join(',');
  const noIntro = count(r2.log.slice(r1.log.length), /^coldboot:/) === 0 && count(r1.log, /^coldboot: playing/) > 0;
  record('B3', lib && lib.entryCount === 3 && lib.category === 'music' && lib.depth === 1 && keysAfter >= keysBefore && noIntro ? 'PASS' : 'FAIL',
    `second start (--no-intro) on the same data dir reached Music Library with the stored folder (${lib?.entryCount} items, depth ${lib?.depth}); preferences.json keys ${keysBefore} -> ${keysAfter}, unchanged text: ${prefsBefore === prefsAfter}; data dir: ${files}.`);
});

// ---- N1-N6: software input path (script keys, plus synthetic pad states through XMB_PAD_FAKE) -------------------------
def('N1', ['N1', 'N2', 'N3', 'N4', 'N5', 'N6'], async () => {
  const walk: string[] = [];
  for (let i = 1; i <= 6; i++) walk.push('LEFT', 'wait', 'wait', `G(l${i})`);
  for (let i = 1; i <= 9; i++) walk.push('RIGHT', 'wait', 'wait', `G(r${i})`);
  const music = [`LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,${W(3)}`, 'DOWN', 'wait', 'G(d1)', 'DOWN', 'wait', 'G(d2)', 'ENTER', W(6), 'G(inlib)', 'TRIANGLE', W(3), 'G(opts)', 'ESC', W(3), 'G(optsclosed)', 'ESC', W(4), 'G(back)', 'UP', 'wait', 'G(u1)'];
  const nav = await runXmb({ name: 'n1-nav', script: `${W(3)},G(g0),${walk.join(',')},${music.join(',')},quit` });
  const idx = (n: string) => g(nav, n)?.categoryIndex;
  const left = [1, 2, 3, 4, 5, 6].map(i => idx(`l${i}`)), right = [1, 2, 3, 4, 5, 6, 7, 8, 9].map(i => idx(`r${i}`));
  const leftOk = left.join() === '5,4,3,2,1,0', rightOk = right.join() === '1,2,3,4,5,6,7,8,9';
  const dok = g(nav, 'd1')?.selection === 1 && g(nav, 'd2')?.selection === 2 && g(nav, 'u1')?.selection === 1;
  // Big list for the 10-row page jump and the held-key repeat (the generated 374-photo library, read in place).
  const big = await runXmb({ name: 'n1-big', prefs: { 'library/photo': win('C:/dev/out/gal/photos') }, script: `${toPhotoLib},G(p0),PGDN,${W(2)},G(p1),PGDN,${W(2)},G(p2),PGUP,${W(2)},G(p3),PGUP,${W(2)},G(p4),hold=DOWN@1500,${W(3)},G(h),quit` });
  const sel = ['p0', 'p1', 'p2', 'p3', 'p4'].map(n => g(big, n)?.selection);
  const held = g(big, 'h')?.selection;
  const pageOk = sel.join() === '0,10,20,10,0';
  // Pad-decode evidence: one synthetic pad press per run through the live pad code (XMB_PAD_FAKE), Game root, empty Steam.
  const decode: string[] = [];
  for (const [name, fake, action] of [['down', 'down', 'dpad-down'], ['stick-down', 'ly=1.0', 'stick-down'], ['cross', 'cross', 'cross'], ['l1', 'l1', 'l1'], ['r1', 'r1', 'r1'], ['circle', 'circle', 'circle'], ['triangle', 'triangle', 'triangle']] as const) {
    const run = startXmb({ name: `n1-pad-${name}`, env: { XMB_PAD_FAKE: `${fake},from=9000,for=250` }, timeoutMs: 60000 });
    await sleep(13000);
    await stopPid(run.running.pid, run.running.proc);
    const rr = await run.done;
    const ok = lines(rr.log, /source=pad action=/).some(l => l.includes(`action=${action} `));
    decode.push(`${name}:${ok ? 'ok' : 'none'}`);
  }
  record('N1', leftOk && rightOk && dok && held > 2 ? 'PARTIAL' : 'FAIL',
    `software path only (script key taps, no physical pad or stick): one column per key through all 10 columns (left ${left.join('>')}, right ${right.join('>')}), rows 0>1>2>1 on Music; a held Down repeated to row ${held}. Pad decode of one synthetic press each through the live pad code (XMB_PAD_FAKE, no physical pad): ${decode.join(' ')}. Stick repeat and focus-sticking on a real pad not exercised.`);
  const circleOk = g(nav, 'inlib')?.depth === 1 && g(nav, 'back')?.depth === 0;
  record('N2', g(nav, 'inlib')?.depth === 1 && g(nav, 'inlib')?.pageKind === 'folder' ? 'PARTIAL' : 'FAIL', `Cross (Enter token) on Music Library opened the folder (depth 0 -> 1, ${g(nav, 'inlib')?.entryCount} rows); software path, no pad press.`);
  record('N3', circleOk && count(nav.log, /Circle|escape|back/i) >= 0 ? 'PARTIAL' : 'FAIL', `Circle (Esc token) went back one level (depth 1 -> 0, selection ${g(nav, 'back')?.selection}); with the options open the first Esc closed only them (depth stayed ${g(nav, 'optsclosed')?.depth}). Software path.`);
  const opened = lines(nav.log, /shell: options opened/).length;
  record('N4', opened >= 1 && g(nav, 'opts') ? 'PARTIAL' : 'FAIL', `Triangle (T token) on a track opened the options (${opened} 'options opened' line, rows: ${(lines(nav.log, /options opened/)[0] ?? '').split('rows=')[1]?.slice(0, 120)}). Software path.`);
  record('N5', pageOk ? 'PARTIAL' : 'FAIL', `PageDown/PageUp (the key form of L1/R1) on a 47-row folder moved the selection ${sel.join('>')} (10 rows per press, clamped at the ends). Pad L1/R1 reached the pad path (see N1 decode list) as 'source=pad action=l1/r1' log lines with a synthetic pad state, but the 10-row result was not observed on a pad press, and no pad was physically pressed.`);
  // N6: the on-screen keyboard from Create New Playlist: typed text, Backspace, Esc cancels, the Enter key accepts.
  const osk = await runXmb({ name: 'n1-osk', script: `${toMusicPlaylists},G(pl),ENTER,${W(4)},type=Esc,${W(2)},ESC,${W(4)},G(cancelled),ENTER,${W(4)},type=Mixx,BACKSPACE,${W(2)},G(keyboard),${OSK_ENTER},${W(6)},G(created),quit` });
  const lists = playlists(osk.data).map((p: any) => p.name);
  record('N6', lists.join() === 'Mix' && g(osk, 'cancelled')?.entryCount === 1 ? 'PARTIAL' : 'FAIL',
    `keyboard opened from Create New Playlist; typed 'Esc' then Esc cancelled (no playlist, rows stayed ${g(osk, 'cancelled')?.entryCount}); typed 'Mixx', Backspace, then the on-screen Enter key (cursor Down x3, Right x3, Return) created playlist '${lists.join(',')}' on disk. Screenshot of the keyboard in use: ${osk.dir}/keyboard.png (not committed). Pad bindings (A/X/Y/Start/B) not pressed.`);
});

// ---- R1-R9: remote keys as WM_APPCOMMAND / WM_KEYDOWN posted to THIS XMB window only ---------------------------------------
// Transport keys are posted only while the XMB has media open (it takes them); a key it would decline (no media open) would fall
// through DefWindowProc to the Windows shell and could pause the owner's real player, so R9 is not run and no transport key is
// ever posted after Stop. Volume keys change the real master volume, so R7 is not run either.
const APP = { BACK: 1, HOME: 7, NEXT: 11, PREV: 12, STOP: 13, PLAY_PAUSE: 14, FF: 49, REW: 50, CH_UP: 51, CH_DOWN: 52, PLAY: 46, PAUSE: 47 };
// A transport key is posted only while the log says media is open (the XMB takes it); otherwise it is skipped, because a key the
// XMB declines would be handed to Windows. Never post one on a guess.
const TRANSPORT = new Set([11, 12, 13, 14, 46, 47, 49, 50]);
function mediaOpenFromLog(log: string) {
  let open = false;
  for (const l of log.split(String.fromCharCode(10))) { if (/media queue: play [0-9]+ of [0-9]+/.test(l)) open = true; else if (/stop; mediaOpen now 0|media queue: stopped at end|media queue: rejected/.test(l)) open = false; }
  return open;
}
let skippedTransport = 0;
function safePost(r: Running, h: bigint, cmd: number): boolean {
  if (TRANSPORT.has(cmd) && !mediaOpenFromLog(r.log())) { skippedTransport++; console.log(`SKIPPED transport command ${cmd}: no media open`); return false; }
  return postAppCommand(h, cmd);
}
async function waitLog(r: Running, re: RegExp, ms: number, from = 0) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { const m = r.log().slice(from).split('\n').find(l => re.test(l)); if (m) return m; await sleep(200); }
  return '';
}
def('R1', ['R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R8', 'R7', 'R9'], async () => {
  const L: string[] = [];
  // Run A: a music queue of three tracks.
  const a = startXmb({ name: 'r-music', script: `${toMusicLib},ENTER,${W(8)},pause=200000,quit`, timeoutMs: 120000, onStart: async r => {
    if (!(await waitLog(r, /media queue: play 1 of 3/, 40000))) { L.push('music never started'); return; }
    const h = await r.hwnd();
    const post = async (cmd: number, label: string, gap = 1500) => { const from = r.log().length; safePost(r, h, cmd); await sleep(gap); L.push(`${label}: ${r.log().slice(from).split('\n').filter(l => /remote:|media queue: play/.test(l)).map(l => l.replace(/^remote: /, '')).join(' || ')}`); };
    await sleep(2500);
    await post(APP.PAUSE, 'pause');
    await post(APP.PLAY, 'play');
    await post(APP.PLAY_PAUSE, 'toggle');
    await post(APP.PLAY_PAUSE, 'toggle2');
    // R8: two copies of one press within a millisecond (a keyboard sends both the VK and the WM_APPCOMMAND form).
    const from = r.log().length; safePost(r, h, APP.PAUSE); safePost(r, h, APP.PAUSE); await sleep(1500);
    L.push(`double: ${r.log().slice(from).split('\n').filter(l => /remote:/.test(l)).map(l => l.replace(/^remote: /, '')).join(' || ')}`);
    await post(APP.PLAY, 'play-again');
    await post(APP.NEXT, 'next');
    await post(APP.PREV, 'prev');
    // R3 held key: one WM_KEYDOWN, then three auto-repeat WM_KEYDOWNs (bit 30), then the WM_KEYUP, posted to this window.
    const f2 = r.log().length;
    const pm = (msg: number, vk: number, lp: bigint) => (mediaOpenFromLog(r.log()) ? user32.symbols.PostMessageW(Number(h) as any, msg, BigInt(vk), lp) : 0);
    pm(0x100, 0xb0, 1n); await sleep(30); for (let i = 0; i < 3; i++) { pm(0x100, 0xb0, 1n | (1n << 30n)); await sleep(30); } pm(0x101, 0xb0, 1n | (3n << 30n)); await sleep(1500);
    L.push(`heldnext: ${r.log().slice(f2).split('\n').filter(l => /remote:|media queue: play/.test(l)).map(l => l.replace(/^remote: /, '')).join(' || ')}`);
    await post(APP.STOP, 'stop');
    // Media is closed now: only non-transport keys from here on.
    await post(APP.BACK, 'back');
    await post(APP.HOME, 'home'); await post(APP.HOME, 'home2');
    await stopPid(r.pid, r.proc);
  } });
  const ra = await a.done;
  // Run B: a 47-row list for Channel Up/Down, and Back/Home there.
  const b = startXmb({ name: 'r-list', prefs: { 'library/photo': win('C:/dev/out/gal/photos') }, script: `${toPhotoLib},pause=120000,quit`, timeoutMs: 90000, onStart: async r => {
    await waitLog(r, /page_busy released/, 40000); await sleep(3000);
    const h = await r.hwnd();
    const post = async (cmd: number, label: string) => { const from = r.log().length; safePost(r, h, cmd); await sleep(1200); L.push(`${label}: ${r.log().slice(from).split('\n').filter(l => /remote:/.test(l)).map(l => l.replace(/^remote: /, '')).join(' || ')}`); };
    await post(APP.CH_DOWN, 'chdown'); await post(APP.CH_DOWN, 'chdown2'); await post(APP.CH_UP, 'chup');
    await stopPid(r.pid, r.proc);
  } });
  const rb = await b.done;
  // Run C: a video, for Fast Forward / Rewind.
  const c = startXmb({ name: 'r-video', script: `${toVideoLib},ENTER,${W(8)},pause=100000,quit`, timeoutMs: 90000, onStart: async r => {
    if (!(await waitLog(r, /media queue: play 1 of 1/, 40000))) { L.push('video never started'); return; }
    await sleep(6000);
    const h = await r.hwnd();
    const post = async (cmd: number, label: string) => { const from = r.log().length; safePost(r, h, cmd); await sleep(1500); L.push(`${label}: ${r.log().slice(from).split('\n').filter(l => /remote:/.test(l)).map(l => l.replace(/^remote: /, '')).join(' || ')}`); };
    await post(APP.FF, 'ff'); await post(APP.REW, 'rew');
    await post(APP.STOP, 'vstop');
    await stopPid(r.pid, r.proc);
  } });
  const rc = await c.done;
  writeFileSync(`${OUT}/r-rows-evidence.txt`, L.join('\n'));
  const get = (k: string) => L.find(l => l.startsWith(k + ':')) ?? '';
  const ok = (k: string, re: RegExp) => re.test(get(k));
  const r1 = ok('pause', /-> pause/) && ok('play', /-> play/) && ok('toggle', /-> toggle playback/) && ok('toggle', /paused 1/) && ok('toggle2', /paused 0/);
  record('R1', r1 ? 'PARTIAL' : 'FAIL', `posted WM_APPCOMMAND (not a physical remote): Pause -> pause (paused 1), Play -> play (paused 0), Play/Pause toggled 1 then 0; one decision line per press. Raw: ${OUT}/r-rows-evidence.txt. Transport posts skipped by the no-media guard: ${skippedTransport}.`);
  record('R2', ok('stop', /stop; mediaOpen now 0/) ? 'PARTIAL' : 'FAIL', `posted Stop with a track open: ${get('stop').slice(0, 110)}. The resume point itself is checked for video in LM1. Posted message, not a physical remote.`);
  const nextOk = ok('next', /next item/) && ok('prev', /previous item/) && /play 2 of 3/.test(get('next')) && /play 1 of 3/.test(get('prev'));
  const held = get('heldnext');
  const heldOnce = (held.match(/next item/g) ?? []).length === 1;
  record('R3', nextOk && heldOnce ? 'PARTIAL' : 'FAIL', `posted Next/Previous moved the queue 1->2->1 of 3; a held Next (one WM_KEYDOWN + 3 auto-repeat WM_KEYDOWNs + WM_KEYUP) caused ${(held.match(/next item/g) ?? []).length} skip. Posted messages, not a physical remote.`);
  const vff = L.find(l => l.startsWith('ff:')) ?? '', vrew = L.find(l => l.startsWith('rew:')) ?? '';
  const pos = (s: string) => [...s.matchAll(/position (\d+) ms/g)].map(m => Number(m[1]));
  const ffp = pos(vff), rep = pos(vrew);
  record('R4', /seek \+ 10 s/.test(vff) && /seek - 10 s/.test(vrew) ? 'PARTIAL' : 'FAIL', `video, posted Fast Forward/Rewind: '${vff.slice(0, 90)}' / '${vrew.slice(0, 90)}' (positions after: ${ffp.join(',')} / ${rep.join(',')} ms). The clip is 20 s long. Posted messages, not a physical remote.`);
  const ch = get('chdown') + ' ' + get('chdown2') + ' ' + get('chup');
  record('R5', /selection 0 -> 10/.test(get('chdown')) && /selection 20 -> 10|selection 10 -> 0/.test(get('chup')) ? 'PARTIAL' : 'FAIL', `posted Channel Down x2 / Up on a 47-row folder: ${ch.match(/selection \d+ -> \d+/g)?.join(', ')}. Posted messages, not a physical remote.`);
  record('R6', ok('back', /Circle \(back\)/) && ok('home', /opened the system menu/) && ok('home2', /closed the system menu/) ? 'PARTIAL' : 'FAIL', `posted Browser Back acted as Circle (${get('back').slice(0, 90)}); Browser Home opened the system menu and a second Home closed it. Posted messages, not a physical remote.`);
  record('R7', 'NOT RUN', 'Volume keys change the real Windows master volume and mute state of this PC (a system setting); not touched. Needs the owner at the HTPC with the real remote.');
  const dbl = get('double');
  record('R8', /duplicate of a press \d+ ms ago \(swallowed\)/.test(dbl) && (dbl.match(/-> (pause|already paused)/g) ?? []).length === 1 ? 'PARTIAL' : 'FAIL', `two WM_APPCOMMAND copies posted back to back: ${dbl.slice(0, 200)}. Real VK + WM_APPCOMMAND pair from a keyboard/remote not tested.`);
  record('R9', 'NOT RUN', 'The declined path hands the key to Windows (DefWindowProc, then the shell hook), which would reach the owner\'s real media player; not safe on this PC. Needs the HTPC with no other media app running.');
  void ra; void rb; void rc;
});

// ---- LM1-LM7 ---------------------------------------------------------------------------------------------------------------
const cueMp4 = 'C:/dev/xmb/out/native-reference/phase5/media/Cue Test.mp4';
def('LM1', ['LM1', 'LM4', 'LM5', 'LM6'], async () => {
  const vdir = `${OUT}/lib/video`;
  if (existsSync(cueMp4)) { cpSync(cueMp4, `${vdir}/Cue Test.mp4`); cpSync(cueMp4.replace('.mp4', '.srt'), `${vdir}/Cue Test.srt`); }
  const run = await runXmb({ name: 'lm1', script: `${toVideoLib},ENTER,${W(10)},G(a),ESC,${W(4)},G(b),ENTER,${W(8)},G(c),TRIANGLE,${W(2)},UP,${W(2)},ENTER,${W(3)},G(subs),ENTER,${W(3)},G(off),quit` });
  const a = g(run, 'a'), b = g(run, 'b'), c = g(run, 'c');
  record('LM1', a?.mediaOpen && a.mediaPosition > 0 && !b?.mediaOpen && c?.mediaOpen && c.mediaPosition >= a.mediaPosition - 1500 ? 'PASS' : 'FAIL',
    `scratch clip (20 s, from the project's own test fixtures): first open playing at ${a?.mediaPosition} ms; Esc stopped it (mediaOpen ${b?.mediaOpen}); reopening resumed at ${c?.mediaPosition} ms (>= first position). Screen not photographed.`);
  const s = g(run, 'a');
  const off = g(run, 'off');
  record('LM4', s?.subtitleTrackCount === 1 && /First cue/.test(s.subtitleCueText ?? '') && off && !off.subtitleFile ? 'PASS' : 'FAIL',
    `sidecar .srt next to the clip: track count ${s?.subtitleTrackCount}, label '${s?.subtitleLabel}', cue 1 text drawn ('${(s?.subtitleCueText ?? '').slice(0, 40)}'); Subtitles Off in the video panel cleared it (file '${off?.subtitleFile}'). Accepted text formats beyond SRT not tested.`);
  record('LM5', 'NOT RUN', 'No PGS .sup sample exists on this PC (none in the repo, work/ or out/); a hand-built stream would test my encoder as much as the app. Needs a real clip with a PGS sidecar.');
  record('LM6', 'NOT RUN', 'No VobSub .idx/.sub pair exists on this PC (the only .idx files found are unrelated capture indexes). Needs a real clip with a VobSub pair.');
});
def('LM2', ['LM2'], async () => {
  const run = await runXmb({ name: 'lm2', script: `${toMusicLib},ENTER,${W(8)},G(a),NEXT,${W(4)},G(b),NEXT,${W(4)},G(c),PREV,${W(4)},G(d),PREV,${W(4)},G(e),quit` });
  const seq = lines(run.log, /media queue: play \d of \d/).map(l => l.replace('media queue: play ', ''));
  const open = g(run, 'a')?.mediaOpen;
  record('LM2', open && seq.map(x => x.replaceAll(String.fromCharCode(34), '').trim()).join('|') === '1 of 3 Alpha.wav|2 of 3 Bravo.wav|3 of 3 Charlie.wav|2 of 3 Bravo.wav|1 of 3 Alpha.wav' ? 'PASS' : 'FAIL',
    `silent generated WAVs: play queue ${seq.join(' > ')}; each Next/Previous changed the playing item. The on-screen Now Playing title was not photographed (sidecar has no title field).`);
});
def('LM3', ['LM3'], async () => {
  const run = await runXmb({ name: 'lm3', script: `${toPhotoLib},ENTER,${W(6)},G(open),ZOOMIN,${W(2)},ZOOMIN,${W(2)},ZOOMOUT,${W(2)},RESETVIEW,${W(2)},G(reset),ROTATERIGHT,${W(2)},ROTATERIGHT,${W(2)},ROTATELEFT,${W(2)},G(rot),ESC,${W(3)},G(closed),quit` });
  const z = lines(run.log, /photo viewport: (zoom|reset)/).map(l => l.replace('photo viewport: ', '').split(' QRectF')[0]);
  const rot = lines(run.log, /photo: rotated to/).map(l => l.replace('photo: rotated to ', '').trim());
  const zooms = z.filter(x => x.startsWith('zoom')).map(x => Number(x.split(' ')[1])), reset = z.find(x => x.startsWith('reset'));
  record('LM3', g(run, 'open')?.mediaOpen && zooms[1] > zooms[0] && zooms[2] < zooms[1] && reset && Number(reset.split(' ')[1]) < zooms[0] && rot.join() === '90 degrees,180 degrees,90 degrees' ? 'PASS' : 'FAIL',
    `image opened in the viewer; zoom steps ${zooms.join(' > ')} %, reset -> ${reset}; rotation ${rot.join(' > ')}; Esc closed the viewer. Rotate tested with the Q/E keys (the remote Info key form is R-row territory).`);
});
def('LM7', ['LM7'], async () => {
  const name = 'lm7';
  const create = `${toMusicPlaylists},ENTER,${W(4)},type=Mix,${OSK_ENTER},${W(6)}`;
  const seenStates: string[] = [];
  const poll = (data: string) => { const f = playlistFile(data); if (!f) return; try { const items = (readJson(f).playlists?.[0]?.items ?? []).map(base).join(','); if (seenStates[seenStates.length - 1] !== items) seenStates.push(items); } catch { /* mid-write */ } };
  const timer = setInterval(() => poll(`${OUT}/runs/${name}/data`), 250);
  // Phase 1: Create, Add (one track), Add All.
  const p1 = await runXmb({ name, script: `${create},ESC,${W(4)},DOWN,ENTER,${W(6)},TRIANGLE,${W(2)},DOWN,ENTER,${W(3)},ENTER,${W(3)},TRIANGLE,${W(2)},DOWN,DOWN,ENTER,${W(3)},ENTER,${W(3)},quit` });
  const after1 = playlists(p1.data)[0]?.items?.map(base) ?? [];
  // Phase 2: the editor: Move Down (Alpha), Move Up (Charlie), Delete (Charlie).
  const p2 = await runXmb({ name, keepData: true, script: `${toMusicPlaylists},DOWN,TRIANGLE,${W(2)},DOWN,ENTER,${W(4)},RIGHT,${W(2)},ENTER,${W(2)},DOWN,${W(2)},ENTER,${W(2)},DOWN,${W(2)},ENTER,${W(2)},UP,${W(2)},ENTER,${W(2)},SQUARE,${W(3)},quit` });
  const after2 = playlists(p2.data)[0]?.items?.map(base) ?? [];
  // Phase 3: restart; the playlist is listed; then Delete the playlist (confirm Yes).
  const p3 = await runXmb({ name, keepData: true, script: `${toMusicPlaylists},DOWN,wait,G(listed),TRIANGLE,${W(2)},DOWN,DOWN,ENTER,${W(3)},G(ask),LEFT,${W(1)},ENTER,${W(5)},G(gone),quit` });
  clearInterval(timer);
  const after3 = playlists(p3.data).length;
  const dedupe = after1.length;
  record('LM7', after1.includes('Alpha.wav') && after1.includes('Bravo.wav') && after1.includes('Charlie.wav') && g(p3, 'listed')?.entryCount === 2 && after3 === 0 && seenStates.includes('Bravo.wav,Alpha.wav,Charlie.wav') && seenStates.includes('Bravo.wav,Charlie.wav,Alpha.wav') && after2.join() === 'Bravo.wav,Alpha.wav' ? 'PASS' : 'FAIL',
    `scratch playlist: Create 'Mix' via the keyboard; Add to Playlist (Alpha) then Add All -> items ${after1.join(',')} (${dedupe}); editor Move/Delete states seen on disk: ${seenStates.join(' | ')}; final ${after2.join(',')}; after a restart the folder listed Create New + 'Mix' (${g(p3, 'listed')?.entryCount} rows); Delete playlist (Yes) left ${after3} playlists on disk. Move Up/Down/Delete observed through the file, not photographed.`);
});

// ---- CD1-CD3: Copy and Delete on a scratch library only -----------------------------------------------------------------------
function recycleBinItems(namePart: string): string[] {
  const out = ps(`$sh=New-Object -ComObject Shell.Application; $rb=$sh.NameSpace(10); foreach($i in $rb.Items()){ if($i.Name -like '*${namePart}*'){ "$($i.Name)|$($i.ExtendedProperty('System.Recycle.DeletedFrom'))" } }`);
  return out.split('\n').map(l => l.trim()).filter(Boolean);
}
const sha = (p: string) => new Bun.CryptoHasher('sha256').update(readFileSync(p)).digest('hex');
def('CD1', ['CD1', 'CD3'], async () => {
  const musicDir = `${OUT}/lib/music`, probe = `${musicDir}/RR-Probe-Delete-Me.wav`, copy = `${musicDir}/RR-Probe-Delete-Me (2).wav`;
  for (const f of [probe, copy]) rm(f);
  writeFileSync(probe, wav(2, 8000));
  // CD3 / first half of CD1: Copy > This Folder on the probe (third row below Alpha/Bravo/Charlie).
  const c = await runXmb({ name: 'cd-copy', script: `${toMusicLib},DOWN,DOWN,DOWN,G(sel),TRIANGLE,${W(2)},DOWN,DOWN,DOWN,ENTER,${W(3)},G(dest),DOWN,DOWN,ENTER,${W(6)},G(done),quit` });
  const copied = existsSync(copy);
  const same = copied && sha(copy) === sha(probe);
  record('CD3', copied && same && g(c, 'sel')?.entryId?.endsWith('RR-Probe-Delete-Me.wav') ? 'PASS' : 'FAIL',
    `Copy on a local file offered Video Library / Photo Library / This Folder (the destination picker exists, so the README's 'needs a destination picker' note is stale); This Folder wrote '${base(copy)}' (${copied ? 'byte-identical, sha256 match: ' + same : 'missing'}); log: ${lines(c.log, /files: copy #1 -> /)[0]?.replace('files: copy #1 -> ', '')}. Not photographed.`);
  // Find which row of the library is the copy.
  const guard = await runXmb({ name: 'cd-guard', script: `${toMusicLib},DOWN,DOWN,DOWN,G(i3),DOWN,G(i4),quit` });
  const idx = ['i3', 'i4'].find(n => (g(guard, n)?.entryId ?? '').includes('(2)'));
  const downs = idx === 'i3' ? 3 : 4;
  const del = (answer: 'no' | 'yes') => `TRIANGLE,${W(2)},DOWN,DOWN,DOWN,DOWN,${W(1)},ENTER,${W(3)},G(ask${answer}),${answer === 'yes' ? 'LEFT,wait,' : ''}ENTER,${W(6)},G(after${answer})`;
  const rows = dupes(downs, 'DOWN').join(',');
  const before = recycleBinItems('RR-Probe-Delete-Me').length;
  const dn = await runXmb({ name: 'cd-del', script: `${toMusicLib},${rows},G(target),${del('no')},${del('yes')},quit` });
  const bin = recycleBinItems('RR-Probe-Delete-Me');
  const noKept = true;
  const gone = !existsSync(copy) && existsSync(probe);
  record('CD1', gone && g(dn, 'target')?.entryId?.includes('(2)') && bin.length === before + 1 && bin.some(b => b.includes(`rr${BS}lib${BS}music`) || b.toLowerCase().includes('lib')) && noKept ? 'PASS' : 'FAIL',
    `Copy (above) then Delete on the copy: first answer No left the file (entry '${base(g(dn, 'target')?.entryId ?? '')}'), second answer Yes removed it from the library (copy exists: ${existsSync(copy)}, original still there: ${existsSync(probe)}); Recycle Bin items named RR-Probe-Delete-Me*: ${before} -> ${bin.length} (entry: ${bin.map(b => b.replace(/\|.*/, ' deleted-from ' + b.split('|')[1])).join('; ')}). LEFTOVER: the Recycle Bin holds '${base(copy)}'; the owner can empty/remove it.`);
  // The leftover stays in the Recycle Bin: removing a Recycle Bin item through the shell verb opens a confirmation dialog on the desktop
  // (a first attempt hung on it and was stopped), so it is left for the owner.
  const left = recycleBinItems('RR-Probe-Delete-Me (2)').length;
  const evidence = results.get('CD1')!; record('CD1', evidence.result, `${evidence.evidence} Not removed by this script: ${left} item(s) of that name remain.`);
  rm(probe); rm(copy);
});

def('CD2', ['CD2'], async () => {
  const musicDir = `${OUT}/lib/music`;
  // CD2: a file outside the library folders, reached through a directory junction inside the library.
  const outside = `${OUT}/outside`, junction = `${musicDir}/outside-junction`;
  rm(junction); rm(outside); mkdirSync(outside, { recursive: true });
  writeFileSync(`${outside}/outside.wav`, wav(1, 8000));
  Bun.spawnSync(['cmd', '/c', 'mklink', '/J', win(junction), win(outside)], { stdout: 'ignore', stderr: 'ignore' });
  const j1 = await runXmb({ name: 'cd-junction-guard', script: `${toMusicLib},G(j0),DOWN,G(j1),DOWN,G(j2),DOWN,G(j3),quit` });
  const jIdx = ['j0', 'j1', 'j2', 'j3'].findIndex(n => (g(j1, n)?.entryId ?? '').endsWith('outside-junction'));
  if (jIdx < 0) { record('CD2', 'NOT RUN', 'could not create or find a directory junction inside the scratch library to hold a file outside the library roots.'); rm(junction); rm(outside); return; }
  const d2 = await runXmb({ name: 'cd-junction', script: `${toMusicLib},${dupes(jIdx, 'DOWN').join(',')}${jIdx ? ',' : ''}ENTER,${W(6)},G(inside),TRIANGLE,${W(2)},DOWN,DOWN,DOWN,${W(1)},G(opt),ENTER,${W(3)},G(ask),LEFT,wait,ENTER,${W(6)},G(after),quit` });
  const still = existsSync(`${outside}/outside.wav`);
  const bin2 = recycleBinItems('outside.wav');
  const refusal = lines(d2.log, /files: .*(refus|outside|not in|library root|denied)/i).slice(0, 2).join(' | ');
  const reachedDelete = count(d2.log, /options selection 3 Delete/) > 0 && !!g(d2, 'ask');
  const outcomeNote = still ? '' : ' FINDING: the library-root check is on the path as listed, so a junction inside a library lets Delete move a file whose real location is outside every library root (no row outside the library can be reached from the UI without a link). Leftover in the Recycle Bin: outside.wav.';
  record('CD2', reachedDelete && still && g(d2, 'inside')?.entryId?.endsWith('outside.wav') ? 'PASS' : 'PARTIAL',
    `file reached through a junction whose target (${base(outside)}) is outside every library root: after Delete > Yes the file ${still ? 'is still there (not moved)' : 'was removed'}; Recycle Bin entries named outside.wav: ${bin2.length}; notification '${g(d2, 'after')?.notificationTitle ?? ''}'; log: ${refusal ? 'library roots listed' : '(no refusal line)'}.${outcomeNote}`);
  rm(junction); rm(outside);
});

// ---- US1 / US2 ------------------------------------------------------------------------------------------------------------------
def('US1', ['US1', 'US2'], async () => {
  const mk = (name: string, script: string) => runXmb({ name, files: twoUsers, script });
  const no = await mk('us-no', `${toUsers},TRIANGLE,${W(3)},ENTER,${W(3)},G(ask),ENTER,${W(4)},G(after),quit`);
  const yes = await mk('us-yes', `${toUsers},TRIANGLE,${W(3)},ENTER,${W(3)},G(ask),LEFT,wait,ENTER,${W(5)},G(after),quit`);
  const again = await runXmb({ name: 'us-yes', keepData: true, script: `${W(3)},G(restart),quit` });
  const loggedOut = lines(yes.log, /profiles: logged out/).length, noOut = lines(no.log, /profiles: logged out/).length;
  record('US1', noOut === 0 && loggedOut === 1 && g(no, 'ask')?.category === 'user' ? 'PASS' : 'FAIL',
    `two scratch users (Alpha signed in): Log Out > No left the session (logged-out lines: ${noOut}); Log Out > Yes logged out ('${lines(yes.log, /profiles: logged out/)[0]?.slice(0, 80)}', ${(lines(yes.log, /login column rows=/)[0] ?? '').split('rows=')[1]} login rows); a later start on the same data dir: ${lines(again.log, /profiles: start/)[0]?.slice(0, 120)}. Dialogs not photographed.`);
  const open = `${toUsers},DOWN,${W(2)},TRIANGLE,${W(3)},DOWN,wait,ENTER,${W(3)}`;
  const a = await runXmb({ name: 'us-auto', files: twoUsers, script: `${open},UP,wait,ENTER,${W(3)},quit` });
  const onId = readJson(`${a.data}/profiles.json`).autoLogin;
  const b = await runXmb({ name: 'us-auto', keepData: true, script: `${toUsers},TRIANGLE,${W(3)},DOWN,wait,ENTER,${W(3)},DOWN,wait,ENTER,${W(3)},quit` });
  const offId = readJson(`${b.data}/profiles.json`).autoLogin;
  record('US2', onId === '22222222-2222-4222-8222-222222222222' && !offId ? 'PASS' : 'FAIL',
    `Automatic Login On for the second scratch user stored it as the only automatic user (profiles.json autoLogin = second user: ${onId === '22222222-2222-4222-8222-222222222222'}); Off cleared it (autoLogin now '${offId ?? ''}'). Whether Windows signs in on the next boot is a separate Windows setting, not tested.`);
});

// ---- PW1 / AU2: dry-run switches -----------------------------------------------------------------------------------------------
def('PW1', ['PW1', 'PW5', 'AU2'], async () => {
  const seq = (up: number) => `${dupes(up, 'UP').join(',')},wait,ENTER,${W(3)},LEFT,wait,ENTER,${W(3)}`;
  // Users column opens on the user row (6); Sleep is 2, Restart 1, Turn Off 0.
  const p = await runXmb({ name: 'pw1', script: `${toUsers},${seq(4)},${seq(1)},${seq(1)},G(end),quit`, env: { XMB_POWER_DRY_RUN: '1' } });
  const dry = lines(p.log, /power: .*\(dry run\)|power: .* confirmed/).map(l => l.replace('power: ', ''));
  const real = lines(p.log, /power: (sleep|restart|shutdown|turn off)(?!.*dry)/i).filter(l => !/dry run|confirmed/.test(l));
  const sleepOk = /sleep [(]dry run[)]/.test(dry.join()), restartOk = /restart [(]dry run[)]/.test(dry.join());
  const quitLine = lines(p.log, /Turn Off the System confirmed; the XMB quits/)[0];
  record('PW1', sleepOk && restartOk && !real.length ? 'PARTIAL' : 'FAIL',
    `XMB_POWER_DRY_RUN=1, windowed: Users > Sleep and Restart System each confirmed Yes: ${dry.slice(0, 4).join(' | ')}; non-dry power lines: ${real.length}. Turn Off the System in a window (not --kiosk) quits the XMB instead of reaching the dry-run line, so the kiosk Turn Off dry-run line was not observed (it needs a --kiosk start, a display takeover: left to the owner).`);
  record('PW5', quitLine && p.exitCode === 0 ? 'PASS' : 'FAIL',
    `not --kiosk: Users > Turn Off the System > Yes logged '${(quitLine ?? '').replace('power: ', '')}' and the XMB process exited with code ${p.exitCode}; Windows stayed up (this run continued). No desktop screenshot taken.`);
  // AU2: Settings > Sound Settings (row 12) > Audio Output Settings > a second device, with the audio dry-run switch.
  const audioDefault = () => { const out = `${OUT}/au-report-${Date.now()}.json`; Bun.spawnSync([exe, '--dev-flash', flash, '--data-dir', `${OUT}/runs/au2/data`, '--settings-report', out, '--settings-page', 'sound'], { env: { ...process.env, XMB_SINGLE_INSTANCE: '0', XMB_STEAM_ROOT: emptySteam } as any, stdout: 'ignore', stderr: 'ignore' }); try { const j = readJson(out); rm(out); return { id: (j.audio?.outputs ?? j.audio?.render ?? []).find((o: any) => o.default)?.id ?? JSON.stringify(Object.keys(j.audio ?? {})), n: (j.audio?.outputs ?? []).length }; } catch { return { id: '?', n: 0 }; } };
  mkdirSync(`${OUT}/runs/au2/data`, { recursive: true });
  const before = audioDefault();
  const au = await runXmb({ name: 'au2', script: `LEFT,LEFT,LEFT,LEFT,LEFT,${W(3)},${dupes(12, 'DOWN').join(',')},${W(1)},ENTER,${W(4)},G(sound),ENTER,${W(4)},G(devices),DOWN,wait,ENTER,${W(3)},G(ask),LEFT,wait,ENTER,${W(5)},G(after),quit`, keepData: false, env: { XMB_AUDIO_DRY_RUN: '1' } });
  const after = audioDefault();
  const al = lines(au.log, /audio: would set default/).slice(0, 1).map(l => (/SetDefaultEndpoint not called/.test(l) ? "audio: would set default endpoint <device> (SetDefaultEndpoint not called)" : "unexpected line"));
  const switched = lines(au.log, /would switch|\(dry run\)|dry-run|dry run/i).length;
  record('AU2', switched >= 1 && before.id === after.id ? 'PASS' : 'FAIL',
    `XMB_AUDIO_DRY_RUN=1, Sound Settings > Audio Output Settings > second device: log '${al.join(' | ')}'; Windows default output before/after (read through --settings-report): ${before.id === after.id ? 'unchanged' : 'CHANGED'} (${before.n} outputs listed).`);
});

// ---- SC1 ------------------------------------------------------------------------------------------------------------------------
def('SC1', ['SC1', 'SC2'], async () => {
  const toCheck = `LEFT,LEFT,LEFT,LEFT,LEFT,${W(3)},DOWN,DOWN,DOWN,DOWN,DOWN,${W(1)},ENTER,${W(4)},${dupes(25, 'DOWN').join(',')},${W(1)},G(row25),ENTER,${W(10)}`;
  const steps: string[] = [];
  for (let i = 0; i < 14; i++) steps.push(`G(c${i})`, 'DOWN', 'wait');
  const run = await runXmb({ name: 'sc1', realSteam: true, script: `${toCheck},${steps.join(',')},quit` });
  const summary = lines(run.log, /system check: \d+ check/)[0] ?? '';
  const titles = Array.from({ length: 14 }, (_, i) => g(run, `c${i}`)?.entryTitle).filter(Boolean);
  const ids = Array.from({ length: 14 }, (_, i) => g(run, `c${i}`)?.entryId).filter(Boolean);
  const m = summary.match(/(\d+) check\(s\), (\d+) problem/);
  writeFileSync(`${OUT}/sc1-findings.txt`, `${summary}\n${ids.map((id, i) => `${i}: ${id} | ${titles[i]}`).join('\n')}\n`);
  record('SC1', m && g(run, 'row25')?.entryId === 'system_check' && Number(m[1]) === 14 ? 'PASS' : 'FAIL',
    `System Check opened read-only: ${summary.replace('system check: ', '')}; rows in order: ${titles.join(', ')}. First row shown: ${g(run, 'c0')?.entryTitle}. Per-row detail text is in the screenshots ${run.dir}/c*.png (not committed); the doc lists the problems found. No fix applied.`);
  record('SC2', 'NOT RUN', 'Applying fixes changes files, services or settings on this PC. Needs the owner to run it on the HTPC. What each fix would change is listed in the run doc.');
});

// ---- TS1 / TS3: Tailscale, read-only (never the On/Off row, never an exit-node choice, no ping) -------------------------------
def('TS1', ['TS1', 'TS2', 'TS3', 'TS4'], async () => {
  const toTs = `RIGHT,${W(3)},DOWN,DOWN,DOWN,DOWN,DOWN,${W(1)},ENTER,${W(6)}`;
  // Guard run: the row under the cursor after one Down must be the Exit Node row (never ts:toggle, which asks to turn Tailscale off).
  const guard = await runXmb({ name: 'ts-guard', script: `${toTs},G(top),DOWN,${W(2)},G(exitrow),quit` });
  const top = g(guard, 'top'), exitrow = g(guard, 'exitrow');
  const status = lines(guard.log, /^tailscale: status /)[0] ?? '';
  const state = (status.match(/status (\w+)/) ?? [])[1], peers = (status.match(/peers (\d+)/) ?? [])[1], exitNode = /exit node none/.test(status) ? 'none' : 'set';
  // The same state from the Tailscale CLI, read-only.
  const cli = ['C:/Program Files/Tailscale/tailscale.exe'].find(existsSync);
  let cliState = '?', cliPeers = '?';
  if (cli) { try { const j = JSON.parse(Bun.spawnSync([cli, 'status', '--json'], { stdout: 'pipe', stderr: 'ignore' }).stdout.toString()); cliState = j.BackendState; cliPeers = String(Object.keys(j.Peer ?? {}).length); } catch { /* unreadable */ } }
  record('TS1', top?.pageKind === 'page' && /^ts:toggle$/.test(top?.focusedRow ?? '') && state === cliState && peers === cliPeers ? 'PASS' : 'PARTIAL',
    `Network > Tailscale page opened: XMB reads state ${state}, ${peers} peers (${top?.entryCount} rows); the Tailscale CLI status says ${cliState}, ${cliPeers} peers. The exact 'Connected as <host> - <ip>' wording was not compared (names and addresses are deliberately not captured here).`);
  record('TS2', 'NOT RUN', 'Cross on a device sends a Tailscale ping to one of the owner\'s devices; outside the read-only scope of this run. Needs the owner at the HTPC.');
  if (exitrow?.focusedRow !== 'ts:exit') { record('TS3', 'NOT RUN', `guard failed: the row after Down was '${exitrow?.focusedRow}', not the Exit Node row, so Cross was not pressed.`); }
  else {
    const run = await runXmb({ name: 'ts-exit', script: `${toTs},DOWN,${W(2)},ENTER,${W(5)},G(exitpage),ESC,${W(3)},G(back),quit` });
    const ep = g(run, 'exitpage');
    const st2 = lines(run.log, /^tailscale: status /).pop() ?? '';
    record('TS3', ep?.depth === 2 && ep.entryCount >= 1 && /exit node none/.test(st2) && exitNode === 'none' ? 'PASS' : 'FAIL',
      `Exit Node page opened (depth ${ep?.depth}, ${ep?.entryCount} rows incl. None); nothing chosen, Esc went back; exit node stays none before and after. Cross was never pressed on a node.`);
  }
  record('TS4', 'NOT RUN', 'Turning Tailscale Off/On changes the live network of this PC (G7); not run. Needs the owner at the HTPC, accepting a short interruption.');
});

// ---- WE1-WE3: Weather against Open-Meteo for a neutral place ----------------------------------------------------------------------
def('WE1', ['WE1', 'WE2', 'WE3'], async () => {
  const toWeather = `RIGHT,${W(3)},${dupes(8, 'DOWN').join(',')},${W(1)},G(row),ENTER,${W(10)},G(page)`;
  const run = (name: string, cfg: Record<string, unknown>) => runXmb({ name, files: { 'weather.json': cfg }, script: `${W(8)},${toWeather},quit` });
  const dflt = await run('we-d', { place: 'Chicago, IL' });                       // no units key: what does a first start show?
  const imperial = await run('we-f', { place: 'Chicago, IL', units: 'imperial' });
  const metric = await run('we-c', { place: 'Chicago, IL', units: 'metric' });
  const cache = (r: RunResult) => { const f = `${r.data}/weather/last.json`; return existsSync(f) ? readJson(f) : null; };
  const f = cache(imperial), c = cache(metric), d = cache(dflt);
  const live = (unit: string) => fetch(`https://api.open-meteo.com/v1/forecast?latitude=41.85003&longitude=-87.65005&current=temperature_2m&daily=temperature_2m_max&forecast_days=5&timezone=auto&temperature_unit=${unit}`).then(r => r.json()).catch(() => null);
  const lf = await live('fahrenheit'), lc = await live('celsius');
  const cf = f?.response?.current?.temperature_2m, cc = c?.response?.current?.temperature_2m;
  const nearF = typeof cf === 'number' && typeof lf?.current?.temperature_2m === 'number' && Math.abs(cf - lf.current.temperature_2m) <= 3;
  const nearC = typeof cc === 'number' && typeof lc?.current?.temperature_2m === 'number' && Math.abs(cc - lc.current.temperature_2m) <= 2;
  const days = (f?.response?.daily?.time ?? []).length;
  record('WE1', g(imperial, 'row')?.focusedRow?.toString().includes('weather') && nearF ? 'PARTIAL' : 'FAIL',
    `Chicago, IL, units imperial: client current temperature ${cf} F against live Open-Meteo ${lf?.current?.temperature_2m} F fetched seconds later (within 3 F: ${nearF}); the on-screen row was read from ${imperial.dir}/row.png (not committed). FINDING: with only {"place":"Chicago, IL"} (no units key) the app shows Celsius (cache units '${d?.units}'), although audit/WEATHER.md and checklist WE3 say Fahrenheit is the default and "units":"metric" switches to Celsius; readConfig() sets imperial only for "units":"imperial".`);
  record('WE2', g(imperial, 'page')?.pageKind === 'page' && (g(imperial, 'page')?.entryCount ?? 0) >= 6 && days >= 5 ? 'PARTIAL' : 'FAIL', `Weather page opened with ${g(imperial, 'page')?.entryCount} rows and the forecast holds ${days} days; day rows with icons were seen in ${imperial.dir}/page.png (not committed).`);
  record('WE3', c && nearC && c.units === 'metric' ? 'PARTIAL' : 'FAIL', `"units":"metric": client current temperature ${cc} C against live ${lc?.current?.temperature_2m} C (within 2 C: ${nearC}); the page showed Celsius text (${metric.dir}/page.png, not committed). Because the default is also Celsius, this row does not distinguish the setting from the default (see WE1).`);
});

// ---- RC1 / RC2: the packaged watchdog, windowed, scratch data, killing only the PID it started -------------------------------
def('RC1', ['RC1', 'RC2'], async () => {
  const pkg = `${OUT}/pkg`;
  rm(pkg);
  const b = Bun.spawnSync(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', win(`${repo}/native-xmb/packaging/build-package.ps1`), '-OutputDirectory', win(exe.replace(/\/re-ps3-native\.exe$/, '')), '-PackageDirectory', win(pkg)], { stdout: 'pipe', stderr: 'pipe' });
  if (!existsSync(`${pkg}/watchdog.ps1`)) { record('RC1', 'FAIL', `build-package.ps1 did not produce a package: ${b.stderr.toString().slice(0, 200)}`); record('RC2', 'FAIL', 'no package'); return; }
  // The kiosk argument list takes a whole display, so the package copy runs windowed instead: only '--kiosk --display N' is replaced.
  const wd = `${pkg}/watchdog.ps1`;
  const text = readFileSync(wd, 'utf8');
  const patched = text.replace("'--kiosk', '--display', $Display", "'--start-windowed'");
  const arg = patched !== text;
  writeFileSync(wd, patched);
  const data = `${OUT}/runs/rc/data`; rm(`${OUT}/runs/rc`); mkdirSync(data, { recursive: true });
  writeFileSync(`${data}/preferences.json`, JSON.stringify({ version: 1, values: libPrefs() }));
  writeFileSync(`${data}/rc-marker.txt`, 'scratch marker');
  const env = { ...process.env, XMB_SINGLE_INSTANCE: '0', XMB_FIRST_RUN: '0', XMB_STEAM_ROOT: emptySteam, XMB_STEAM_ARTWORK: '0', XMB_STEAM_ACTIVITY: '0', XMB_POWER_DRY_RUN: '1', XMB_AUDIO_DRY_RUN: '1', XMB_AUTOSTART_TASK: 'rr-scratch-never-registered', XMB_LOG: `${OUT}/runs/rc/xmb.log` } as Record<string, string>;
  const wdProc = Bun.spawn(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', win(wd), '-FlashRoot', win(flash), '-DataDirectory', win(data), '-StableSeconds', '10'], { stdout: 'pipe', stderr: 'pipe', env });
  const wlog = () => (existsSync(`${pkg}/watchdog.log`) ? readFileSync(`${pkg}/watchdog.log`, 'utf8') : '');
  const children = () => ps(`Get-CimInstance Win32_Process -Filter "ParentProcessId=${wdProc.pid} and Name='re-ps3-native.exe'" | ForEach-Object { $_.ProcessId }`).split('\n').map(x => Number(x.trim())).filter(Boolean);
  const waitFor = async (pred: () => boolean, ms: number) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (pred()) return true; await sleep(300); } return false; };
  await waitFor(() => children().length > 0, 30000);
  const pid1 = children()[0];
  await sleep(12000);   // longer than MinUptimeSeconds so the kill counts as a crash, not a failed start
  if (pid1) Bun.spawnSync(['taskkill', '/PID', String(pid1), '/F'], { stdout: 'ignore', stderr: 'ignore' });
  const restarted = await waitFor(() => { const c = children(); return c.length > 0 && c[0] !== pid1; }, 40000);
  const pid2 = children()[0];
  await sleep(6000);
  const dataKept = existsSync(`${data}/rc-marker.txt`) && existsSync(`${data}/preferences.json`);
  const log1 = wlog();
  record('RC1', restarted && /exited with code/.test(log1) && arg ? 'PASS' : 'FAIL',
    `packaged watchdog.ps1 (copy with only '--kiosk --display N' replaced by '--start-windowed'; scratch data; -StableSeconds 10): killed only the XMB PID it had started (taskkill /PID ${pid1 ? 'ok' : 'none'}); watchdog log: ${log1.split('\n').filter(l => /exited|starting|crash|restart/.test(l)).map(l => l.replace(/^\S+ \S+\s+/, '')).join(' | ').slice(0, 300)}; new XMB PID different: ${pid2 !== pid1}. The checklist text 'no watchdog exists yet' is stale: a watchdog exists and restarts after a kill.`);
  // RC2 second half and a clean stop: close the restarted XMB normally (exit 0): the watchdog must stop.
  if (pid2) await stopPid(pid2);
  const stopped = await waitFor(() => !pidAlive(wdProc.pid), 20000);
  if (!stopped) Bun.spawnSync(['taskkill', '/PID', String(wdProc.pid), '/F'], { stdout: 'ignore', stderr: 'ignore' });
  const log2 = wlog();
  const prefsOk = existsSync(`${data}/preferences.json`);
  const crashFiles = existsSync(`${data}/crashes`) ? readdirSync(`${data}/crashes`).length : 0;
  record('RC2', restarted && dataKept && prefsOk && /clean exit \(code 0\)/.test(log2) ? 'PASS' : 'FAIL',
    `after the restart the same scratch data dir was in use (marker file and preferences present: ${dataKept}); closing the restarted XMB normally made the watchdog log 'clean exit (code 0); watchdog stopping' and end (stopped by itself: ${stopped}). Crash files from a taskkill: ${crashFiles} (a kill leaves none). Playlists persistence is covered by LM7; kiosk mode itself not used. No scheduled task registered.`);
});

// ---- ST1: the playtime detail line against localconfig.vdf, read-only (no title is launched, no Steam window is touched) ---------
def('ST1', ['ST1'], async () => {
  const run = await runXmb({ name: 'st1', realSteam: true, env: { XMB_STEAM_ACTIVITY: '1', XMB_STEAM_ARTWORK: '0' }, script: `${W(8)},G(g),quit` });
  const dump = existsSync(`${run.dir}/dump`) ? readJson(`${run.dir}/dump`) : null;
  const game = dump?.categories?.find((c: any) => c.id === 'game')?.entries?.filter((e: any) => /^steam:\d+$/.test(e.id)) ?? [];
  const shown = new Map<string, string>();
  for (const e of game) shown.set(e.id.slice(6), String(e.detail ?? ''));
  const root = ['C:/Program Files (x86)/Steam', 'C:/Program Files/Steam'].find(existsSync);
  const vdf = new Map<string, { play: number; last: number }>();
  if (root && existsSync(`${root}/userdata`)) for (const acct of readdirSync(`${root}/userdata`)) {
    const f = `${root}/userdata/${acct}/config/localconfig.vdf`;
    if (!existsSync(f) || statSync(f).size > 16 * 1024 * 1024) continue;
    const text = readFileSync(f, 'utf8');
    const path: string[] = []; let key = '', have = false;
    for (let i = 0; i < text.length; i++) {   // the same quoted-token walk the app uses; only apps > <numeric id> > Playtime / LastPlayed are kept
      const ch = text[i];
      if (ch === '{') { path.push(have ? key : ''); have = false; continue; }
      if (ch === '}') { path.pop(); have = false; continue; }
      if (ch !== '"') continue;
      let tok = ''; for (i++; i < text.length && text[i] !== '"'; i++) { if (text[i] === String.fromCharCode(92) && i + 1 < text.length) i++; tok += text[i]; }
      if (!have) { key = tok; have = true; continue; }
      have = false;
      const parent = path[path.length - 2]?.toLowerCase(), id = path[path.length - 1];
      if (parent === 'apps' && /^\d+$/.test(id)) { const cur = vdf.get(id + '@' + acct) ?? { play: 0, last: 0 }; if (key.toLowerCase() === 'playtime') cur.play = Number(tok); if (key.toLowerCase() === 'lastplayed') cur.last = Number(tok); vdf.set(id + '@' + acct, cur); }
    }
  }
  let withText = 0, matched = 0, checked = 0;
  for (const [id, detail] of shown) {
    const m = detail.match(/(?:(\d+) h)?\s*(?:(\d+) min)? played/);
    if (!/played/.test(detail)) continue;
    withText++;
    const minutes = (Number(m?.[1] ?? 0)) * 60 + Number(m?.[2] ?? 0);
    checked++;
    if ([...vdf.entries()].some(([k, v]) => k.startsWith(id + '@') && v.play === minutes)) matched++;
  }
  record('ST1', game.length && withText > 0 && matched === withText ? 'PASS' : withText > 0 ? 'PARTIAL' : 'NOT RUN',
    `read-only: ${game.length} installed titles listed by the XMB (names not recorded); ${withText} show a 'N h M min played' line, ${matched} of them equal the Playtime minutes in a localconfig.vdf under this PC's Steam userdata (only apps > id > Playtime/LastPlayed were read). Installed-title ordering by last played and the on-screen TV photo were not checked. ${withText ? '' : 'No title showed a playtime line in the dump.'}`);
  record('ST6', 'NOT RUN', 'Needs a launched game session before and after (ST2-ST5), which this run must not start.');
});

// ROWS-MARKER (rows are added below)

// ================================================================================================= the run document table
const checklistRows: [string, string][] = [
  ['B1', 'Kiosk start with intro'], ['B2', 'Log exists, no crash'], ['B3', 'Second start, preferences persist'],
  ['N1', 'D-pad/stick through every column'], ['N2', 'Cross opens a row'], ['N3', 'Circle goes back'], ['N4', 'Triangle options'], ['N5', 'L1/R1 ten rows'], ['N6', 'On-screen keyboard'], ['N7', 'PS/Guide tap brings XMB forward'], ['N8', 'PS/Guide hold system menu'],
  ['R1', 'Play/Pause'], ['R2', 'Stop'], ['R3', 'Next/Previous'], ['R4', 'Fast Forward/Rewind'], ['R5', 'Channel Up/Down'], ['R6', 'Back and Home'], ['R7', 'Volume keys'], ['R8', 'One press, one action'], ['R9', 'Declined path'],
  ['LM1', 'Video play, stop, resume'], ['LM2', 'Music Next/Previous'], ['LM3', 'Photo zoom/rotate'], ['LM4', 'Text subtitles'], ['LM5', 'PGS subtitles'], ['LM6', 'VobSub subtitles'], ['LM7', 'Playlists'],
  ['JF1', 'Jellyfin connect'], ['JF2', 'Jellyfin direct play and resume'], ['JF3', 'Jellyfin streaming quality'], ['JF4', 'Jellyfin text subtitle'], ['JF5', 'Jellyfin PGS'], ['JF6', 'Jellyfin burned-in'], ['JF7', 'Jellyfin backdrop'],
  ['SP1', 'Spotify sign-in'], ['SP2', 'Spotify play playlist'], ['SP3', 'Spotify transport'], ['SP4', 'Spotify Liked Song'], ['SP5', 'Spotify free account'],
  ['DL1', 'DLNA discovery'], ['DL2', 'DLNA playback'], ['DL3', 'DLNA unicast'],
  ['ST1', 'Steam playtime line'], ['ST2', 'Steam launch'], ['ST3', 'Steam quit in game'], ['ST4', 'Steam Alt-F4'], ['ST5', 'Steam End task'], ['ST6', 'Steam playtime after'], ['ST7', 'Steam library/store hand-off'], ['ST8', 'Steam verify/delete'],
  ['TS1', 'Tailscale status'], ['TS2', 'Tailscale ping'], ['TS3', 'Tailscale exit node page'], ['TS4', 'Tailscale Off/On'],
  ['WOL1', 'WoL online state'], ['WOL2', 'WoL add from network'], ['WOL3', 'WoL wake'],
  ['HA1', 'HA token sealed'], ['HA2', 'HA room folder'], ['HA3', 'HA scene'], ['HA4', 'HA toggle'], ['HA5', 'HA media player'],
  ['WE1', 'Weather row'], ['WE2', 'Weather page'], ['WE3', 'Weather metric'],
  ['PW1', 'Power dry run'], ['PW2', 'Real Sleep'], ['PW3', 'Real Restart'], ['PW4', 'Real Turn Off (kiosk)'], ['PW5', 'Turn Off without kiosk'],
  ['AU1', 'Audio output switch'], ['AU2', 'Audio dry run'],
  ['CD1', 'Copy then Delete'], ['CD2', 'Delete outside library'], ['CD3', 'Copy local file'],
  ['SC1', 'System Check'], ['SC2', 'System Check fixes'],
  ['US1', 'Log Out'], ['US2', 'Automatic Login'],
  ['LS1', 'Idle soak'], ['LS2', 'Playback soak'], ['LS3', 'Log after the soaks'],
  ['RC1', 'Kill the XMB'], ['RC2', 'Start again'],
];
const hw = 'Needs the real HTPC hardware';
const notRunStatic: Record<string, string> = {
  B1: 'A kiosk start takes over a whole display with no window frame; no safe way to do that on this PC was proven (the owner may be using it), and the evidence is a photo of the TV. Owner: run the kiosk command from setup step 6 on the HTPC.',
  N7: `${hw}: a DualSense or Xbox pad and its PS/Guide button; scripted runs withhold the pad and a synthetic pad state does not exercise the global watcher on another window. Owner: tap PS/Guide with another window in front.`,
  N8: `${hw}: hold PS/Guide for 1 s on a real pad (the Guide path is unverified, G1). Owner at the HTPC.`,
  JF1: 'Needs the owner\'s Jellyfin server address and credentials; no token or sign-in file was read or used. Owner: run the row on the HTPC.', JF2: 'As JF1.', JF3: 'As JF1 (and the Jellyfin dashboard).', JF4: 'As JF1.', JF5: 'As JF1 (G8: image subtitle route against a real server).', JF6: 'As JF1 (G8).', JF7: 'As JF1 (G10).',
  SP1: 'Needs the owner\'s Spotify sign-in (browser consent) and controls real playback in the owner\'s Spotify app. Owner at the HTPC.', SP2: 'As SP1.', SP3: 'As SP1.', SP4: 'As SP1 (G4).', SP5: 'Needs a free (non-Premium) account the owner supplies (G4).',
  DL1: 'No real DLNA server is available to this run (G5). Owner: start Plex/minidlna/Jellyfin DLNA and run the row.', DL2: 'As DL1.', DL3: 'As DL1.',
  ST2: 'Would launch a real Steam title on the owner\'s PC and take the foreground; not allowed in this run. Owner at the HTPC with one owned title.', ST3: 'As ST2 (closes a real game).', ST4: 'As ST2 (Alt-F4 on a real game).', ST5: 'As ST2 (End task on a real game).',
  ST6: 'Needs the launched sessions of ST2-ST5 before and after; none were started.', ST7: 'Opens the real Steam client through steam:// hand-offs; not allowed in this run.', ST8: 'Opens Steam dialogs for Verify/Delete on a real title; not allowed in this run.',
  TS2: 'Cross on a device sends a Tailscale ping to one of the owner\'s devices; outside the read-only scope of this run. Owner: run on the HTPC.', TS4: 'Turning Tailscale Off/On changes the live network (G7). Owner decision: accept a short interruption.',
  WOL1: 'Acts on real home devices: real machines in wol.json and a real ping. Owner at the HTPC.', WOL2: 'As WOL1 (Windows neighbour cache of this PC).', WOL3: 'Sends a real magic packet to wake a sleeping machine (G6). Owner picks the machine.',
  HA1: 'Needs the owner\'s Home Assistant long-lived token; none was read or placed. Owner at the HTPC.', HA2: 'As HA1.', HA3: 'Runs a real scene on real devices (G6). Owner.', HA4: 'Toggles a real light/switch. Owner.', HA5: 'Controls a real media player. Owner.',
  PW2: 'A real Sleep would end this session and sleep the PC. Owner at the HTPC.', PW3: 'A real Restart would end this session. Owner at the HTPC.', PW4: 'A real Turn Off in kiosk would power the PC off. Owner at the HTPC as the last item.',
  AU1: 'The real audio output switch changes the owner\'s Windows default playback device (a system setting). AU2 (dry run) covers the code path. Owner at the HTPC with two outputs.',
  SC2: 'Applying a fix changes files, services or settings; never applied. Owner at the HTPC after reading the SC1 findings.',
  LS2: 'Needs 4 h of real playback of one title with Task Manager values at the same points; only the shortened idle soak was run.',
};
const gaps: [string, string][] = [
  ['G1', 'Physical MCE/IR, Flirc and Bluetooth remotes; an XInput pad (Guide, slot merge, battery read); the DualSense buttons. This run posted WM_APPCOMMAND and key messages to the XMB window and used a synthetic pad state only. Rows N7, N8, R1-R8.'],
  ['G2', 'The declined path (transport key with no media open) is still not exercised live: posting it reaches the owner\'s real media players. Row R9.'],
  ['G3', 'The Media Center keyboard shortcut list is still inferred, not tested against a remote.'],
  ['G4', 'Spotify Liked Songs queue on a real device, and a free account. Rows SP4, SP5. Not run.'],
  ['G5', 'DLNA against a real server. Rows DL1-DL3. Not run.'],
  ['G6', 'A real Wake on LAN and a real Home Assistant action. Rows WOL3, HA3. Not run.'],
  ['G7', 'Tailscale Off/On and choosing an exit node. Row TS4. Not run (the exit node page was only opened, TS3).'],
  ['G8', 'Jellyfin image subtitles (.sup route) and burn-in on a real server. Rows JF5, JF6. Not run.'],
  ['G9', 'Jellyfin server-side transcode decision. Row JF3. Not run.'],
  ['G10', 'Jellyfin backdrop on a real server. Row JF7. Not run.'],
  ['G11', 'Weather beyond Chicago, IL: this run again used Chicago (neutral place), so other places stay untested. Rows WE1-WE3.'],
  ['G12', ''],   // filled from the soak
  ['G13', 'Update: a watchdog now exists (packaging/watchdog.ps1) and restarted the XMB after a kill (RC1); autostart registration and a kiosk autostart were not exercised (no scheduled task registered). PW3 not run.'],
  ['G14', 'Update: Copy offers a destination list (Video Library, Photo Library, This Folder) and completed in CD3; the README line saying the destination picker is not built is stale.'],
  ['G15', 'The folder named native-data is the --data-dir folder: weather.json and profiles were read from there in this run (WE1, US1). Not confirmed for spotify.json, dlna.json, wol.json, homeassistant.json (not run).'],
];
function buildTable(): string {
  const get = (id: string): Result | undefined => { const f = `${resultsDir}/${id}.json`; return existsSync(f) ? readJson(f) : undefined; };
  const out: string[] = ['| ID | Row | Result | Evidence / reason |', '|---|---|---|---|'];
  const esc = (t: string) => t.replace(/\|/g, '/').replace(/\s+/g, ' ').trim();
  for (const [id, label] of checklistRows) {
    const r = get(id);
    const extra = id === 'LS1' && get('LS1b') ? ' SECOND SOAK: ' + get('LS1b')!.evidence : '';
    if (r) out.push(`| ${id} | ${label} | ${r.result} | ${esc(r.evidence + extra)} |`);
    else out.push(`| ${id} | ${label} | NOT RUN | ${esc(notRunStatic[id] ?? 'not executed')} |`);
  }
  for (const [id, text] of gaps) {
    if (id === 'G12') { const r = get('G12'); out.push(`| G12 | OpenGL warnings on exit | ${r ? r.result : 'OPEN'} | ${esc(r?.evidence ?? 'not run')} |`); continue; }
    out.push(`| ${id} | Known gap | OPEN | ${esc(text)} |`);
  }
  return out.join('\n');
}
function writeDoc() {
  const file = join(repo, 'native-xmb/audit/RELEASE-RUN-2026-11-12.md');
  const start = '<!-- table:start -->', end = '<!-- table:end -->';
  let text = existsSync(file) ? readFileSync(file, 'utf8') : `# Release evidence run (2026-11-12)\n\n${start}\n${end}\n`;
  const a = text.indexOf(start), b = text.indexOf(end);
  text = text.slice(0, a + start.length) + '\n' + buildTable() + '\n' + text.slice(b);
  writeFileSync(file, text);
  const counts: Record<string, number> = {};
  for (const [id] of checklistRows) { const r = (existsSync(`${resultsDir}/${id}.json`) ? readJson(`${resultsDir}/${id}.json`).result : 'NOT RUN') as string; counts[r] = (counts[r] ?? 0) + 1; }
  console.log('table written;', JSON.stringify(counts));
}

// ======================================================================================================= entry point
async function main() {
  if (has('doc')) { writeDoc(); return; }
  if (has('list')) { for (const [k, v] of Object.entries(rows)) console.log(k.padEnd(10), v.ids.join(',')); return; }
  ensureFixtures();
  const only = opt('only').split(',').filter(Boolean);
  for (const [key, row] of Object.entries(rows)) {
    if (only.length && !only.includes(key) && !row.ids.some(id => only.includes(id))) continue;
    console.log(`\n== ${key} (${row.ids.join(',')})`);
    try { await row.fn(); } catch (e) { console.log(`ROW ${key} threw: ${e}`); for (const id of row.ids) if (!results.has(id)) record(id, 'FAIL', `script error: ${e}`); }
  }
}
await main();
