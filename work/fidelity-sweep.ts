// Measured fidelity sweep: capture the extra surfaces that the parity catalogue does not hold a cell
// for, then score each native frame against the console frame beside it.
//
//   bun work/fidelity-sweep.ts capture   // launches the build once per surface, writes PNGs + sidecars
//   bun work/fidelity-sweep.ts score     // reads work/fidelity-pairs.json, writes metrics JSON + CSV
//
// Inputs are the console frames under C:/dev/out/rpcs3-allgraphics-2026-10-28/frames (frame integrity
// verified in the sweep document) and the canonical pin the parity harness uses: 1920x1080 fullscreen,
// render clock 30 s, background clock 2026-10-28T10:00:00 (day), UI clock 2026-10-25T12:32:00,
// focus age 2000 ms, the ui3-data seed copied the way work/parity.ts seeds it. Read-only over the
// build, the references and the harness; every output goes under C:/dev/out/fidelity-sweep.
import { mkdirSync, cpSync, existsSync, readFileSync, writeFileSync, statSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join } from 'node:path';
import { decodePng, psnr, ssim } from './ab-compare.ts';

const repo = 'C:/dev/xmb';
const outRoot = 'C:/dev/out/fidelity-sweep';
const nativeRoot = join(outRoot, 'native');
const exe = 'C:/dev/xmb/out/native-xmb/re-ps3-native.exe';
const flash = 'C:/dev/rpcs3/bin/dev_flash';
const seedDir = join(repo, 'out/native-reference/store/ui3-data');
const seedPaths = ['preferences.json', 'profiles.json', 'settings.ini', 'family-shared.json', 'artwork', 'cache/steam'];
const consoleFrames = 'C:/dev/out/rpcs3-allgraphics-2026-10-28/frames';
const renderClock = '30';
const backgroundClock = '2026-10-28T10:00:00';
const uiClock = '2026-10-25T12:32:00';
const focusTime = '2000';

const W = (n = 1) => Array(n).fill('wait');
const dirs = (n: number, token: string) => Array(n).fill(token);

// Navigation from the boot column (Game, the build's root state; the discovery dump reports the column
// order user|settings|photo|music|video|tv|game|network|psn|friends, so Game is offset 0).
const toColumn: Record<string, string[]> = {
  game: [], tv: dirs(1, 'LEFT'), video: dirs(2, 'LEFT'), music: dirs(3, 'LEFT'), photo: dirs(4, 'LEFT'),
  settings: dirs(5, 'LEFT'), user: dirs(6, 'LEFT'), network: dirs(1, 'RIGHT'), psn: dirs(2, 'RIGHT'),
  friends: dirs(3, 'RIGHT'),
};

type Job = { id: string; surface: string; note: string; tokens: string[] };

function columnWalk(column: string, rows: number): Job {
  const tokens: string[] = ['full', ...W(10), ...toColumn[column], ...W(2), ...dirs(12, 'UP'), 'wait'];
  for (let row = 0; row <= rows; row++) {
    tokens.push(`grab=$r${String(row).padStart(2, '0')}`);
    if (row < rows) tokens.push('DOWN', 'wait', 'wait');
  }
  tokens.push('quit');
  return {
    id: `col-${column}`, surface: `column:${column}`, note: `rows 0..${rows} of the ${column} column`,
    tokens,
  };
}

// Row counts: every console row that exists, plus one clamped row where the native list is shorter.
const columnRows: Record<string, number> = {
  user: 3, settings: 15, photo: 3, music: 2, video: 3, game: 6, network: 4, psn: 4, friends: 2,
};

const jobs: Job[] = [
  ...Object.keys(columnRows).map(column => columnWalk(column, columnRows[column])),
  {
    id: 'options', surface: 'options menu (Triangle over Video > Continue Watching)',
    note: 'panel at rest and its three focus moves',
    tokens: ['full', ...W(10), ...toColumn.video, ...W(2), ...dirs(12, 'UP'), 'wait',
      'TRIANGLE', ...W(3), 'grab=$open', 'DOWN', ...W(2), 'grab=$row2', 'DOWN', ...W(2), 'grab=$row3',
      'DOWN', ...W(2), 'grab=$row4', 'quit'],
  },
  {
    id: 'dialog', surface: 'delete-confirm dialog (Triangle > Delete > Cross)',
    note: 'the confirm plate, then the cancelled state',
    tokens: ['full', ...W(10), ...toColumn.video, ...W(2), ...dirs(12, 'UP'), 'wait',
      'TRIANGLE', ...W(3), 'DOWN', ...W(2), 'DOWN', ...W(2), 'ENTER', ...W(4), 'grab=$confirm',
      'ESC', ...W(3), 'grab=$cancelled', 'quit'],
  },
  {
    id: 'depth2-video', surface: 'Video > Video Library (depth-1 page)',
    note: 'open the Video Library row',
    tokens: ['full', ...W(10), ...toColumn.video, ...W(2), ...dirs(12, 'UP'), 'wait',
      'DOWN', 'wait', 'ENTER', ...W(5), 'grab=$page', 'quit'],
  },
  {
    id: 'depth2-photo', surface: 'Photo > Photo Library (depth-1 page)',
    note: 'open the Photo Library row',
    tokens: ['full', ...W(10), ...toColumn.photo, ...W(2), ...dirs(12, 'UP'), 'wait',
      'ENTER', ...W(5), 'grab=$page', 'quit'],
  },
  {
    id: 'depth2-music', surface: 'Music > Music Library (depth-1 page)',
    note: 'open the Music Library row',
    tokens: ['full', ...W(10), ...toColumn.music, ...W(2), ...dirs(12, 'UP'), 'wait',
      'ENTER', ...W(5), 'grab=$page', 'quit'],
  },
  {
    id: 'osk', surface: 'OSK over the user-name page',
    note: 'Users > Create New User, then the field edit that opens the keyboard',
    tokens: ['full', ...W(10), ...toColumn.user, ...W(2), ...dirs(12, 'UP'), 'wait',
      'ENTER', ...W(3), 'grab=$page', 'ENTER', ...W(4), 'grab=$osk', 'quit'],
  },
  ...['theme', 'color', 'background', 'font'].map((key, index) => ({
    id: `band-${key}`, surface: `choice band: ${key} (Theme Settings page row ${index})`,
    note: `Theme Settings page, row ${index}, opened with Cross`,
    tokens: ['full', ...W(10), ...toColumn.settings, ...W(2), ...dirs(12, 'UP'), 'wait',
      ...dirs(6, 'DOWN'), 'wait', 'ENTER', ...W(4), 'grab=$page',
      ...dirs(index, 'DOWN'), 'wait', 'ENTER', ...W(4), 'grab=$band',
      'DOWN', ...W(2), 'grab=$band-down1', 'DOWN', ...W(2), 'grab=$band-down2', 'quit'],
  })),
];

function seedDataDir(jobDir: string) {
  const dataDir = join(jobDir, 'data');
  mkdirSync(dataDir, { recursive: true });
  for (const seed of seedPaths) {
    const source = join(seedDir, seed);
    if (!existsSync(source)) throw new Error(`${jobDir}: seed missing: ${source}`);
    cpSync(source, join(dataDir, seed), { recursive: true });
  }
  return dataDir;
}

async function captureJob(job: Job) {
  const jobDir = join(nativeRoot, job.id);
  mkdirSync(jobDir, { recursive: true });
  // The grab blocks or dies when another full-screen capture instance owns the desktop, so a job is
  // attempted up to three times and the attempt that wrote the most frames is the one recorded.
  let best: Awaited<ReturnType<typeof attemptJob>> | null = null;
  for (let attempt = 1; attempt <= 3; attempt++) {
    const record = await attemptJob(job, jobDir);
    if (!best || record.frames.filter(f => f.exists).length > best.frames.filter(f => f.exists).length) best = record;
    const missing = best.frames.filter(f => !f.exists);
    if (!missing.length) break;
    if (attempt < 3) console.log(`${job.id.padEnd(18)} attempt ${attempt}: ${missing.length} frame(s) missing, retrying`);
  }
  return best!;
}

async function attemptJob(job: Job, jobDir: string) {
  const dataDir = seedDataDir(jobDir);
  const tokens = job.tokens.map(token =>
    token.startsWith('grab=$') ? `grab=${join(jobDir, `${token.slice(6)}.png`).replaceAll('\\', '/')}` : token);
  const env: Record<string, string> = {};
  for (const [key, value] of Object.entries(process.env))
    if (typeof value === 'string') env[key] = value;
  env.XMB_DUMP_ENTRIES = join(jobDir, 'entries.json');
  env.XMB_LOG = join(jobDir, 'native.log');
  env.XMB_STEAM_ARTWORK = '0';
  writeFileSync(env.XMB_LOG, '');
  const args = ['--dev-flash', flash, '--data-dir', dataDir, '--render-clock', renderClock,
    '--background-clock', backgroundClock, '--ui-clock', uiClock, '--focus-time', focusTime,
    '--script', tokens.join(',')];
  const started = Date.now();
  const proc = Bun.spawn([exe, ...args], { stdout: 'ignore', stderr: 'pipe', env });
  const stderrText = new Response(proc.stderr).text();
  const timer = setTimeout(() => proc.kill(), 90000);
  let code: number;
  try { code = await proc.exited; } finally { clearTimeout(timer); }
  const stderr = await stderrText;
  writeFileSync(join(jobDir, 'stderr.log'), stderr);
  const frames: { name: string; png: string; sidecar: any }[] = [];
  for (const token of job.tokens) {
    if (!token.startsWith('grab=$')) continue;
    const name = token.slice(6);
    const png = join(jobDir, `${name}.png`);
    let sidecar: any = null;
    try { sidecar = JSON.parse(readFileSync(`${png}.json`, 'utf8')); } catch { /* recorded as missing */ }
    frames.push({ name, png, sidecar });
  }
  const record = {
    id: job.id, surface: job.surface, note: job.note, dir: jobDir, code, seconds: (Date.now() - started) / 1000,
    tokens: tokens.join(','), frames: frames.map(f => ({
      name: f.name, png: f.png, exists: existsSync(f.png),
      bytes: existsSync(f.png) ? statSync(f.png).size : 0,
      category: f.sidecar?.category, depth: f.sidecar?.depth, selection: f.sidecar?.selection,
      entryTitle: f.sidecar?.entryTitle, entryCount: f.sidecar?.entryCount,
    })),
  };
  const missing = record.frames.filter(f => !f.exists).map(f => f.name);
  console.log(`${job.id.padEnd(18)} exit ${code} ${record.seconds.toFixed(1)}s  frames ${record.frames.length - missing.length}/${record.frames.length}`
    + `  camera ${record.frames.filter(f => f.exists).map(f => `${f.name}:${f.category}/${f.depth}/${f.selection}`).join(' ')}`
    + (missing.length ? `  MISSING ${missing.join(',')}` : ''));
  return record;
}

async function capture() {
  mkdirSync(nativeRoot, { recursive: true });
  const only = process.argv.slice(3).filter(arg => !arg.startsWith('--'));
  const selected = only.length ? jobs.filter(job => only.includes(job.id)) : jobs;
  if (only.length && selected.length !== only.length)
    throw new Error(`unknown job id(s): ${only.filter(id => !jobs.some(job => job.id === id)).join(', ')}`);
  const manifest: any[] = [];
  for (const job of selected) manifest.push(await captureJob(job));
  const path = join(outRoot, only.length ? 'native-manifest-partial.json' : 'native-manifest.json');
  writeFileSync(path, `${JSON.stringify({
    schema: 'xmb-fidelity-native-manifest/1', generatedAt: new Date().toISOString(), exe,
    exeSha256: createHash('sha256').update(readFileSync(exe)).digest('hex'),
    pin: { renderClock, backgroundClock, uiClock, focusTime, width: 1920, height: 1080 },
    seed: 'out/native-reference/store/ui3-data', jobs: manifest,
  }, null, 2)}\n`);
  console.log(`\nmanifest ${path}`);
}

type Pair = {
  id: string; surface: string; consoleFrame: string; native: string;
  crops: { name: string; rect: [number, number, number, number] }[];
  class: string; contentDiffers: boolean; note: string;
};

async function score() {
  const pairsPath = join(repo, 'work', 'fidelity-pairs.json');
  const doc = JSON.parse(readFileSync(pairsPath, 'utf8'));
  const cache = new Map<string, ReturnType<typeof decodePng>>();
  const load = (path: string) => {
    if (!cache.has(path)) cache.set(path, decodePng(path));
    return cache.get(path)!;
  };
  const rows: any[] = [];
  for (const pair of doc.pairs as Pair[]) {
    const refPath = join(consoleFrames, pair.consoleFrame);
    const nativePath = /^[A-Za-z]:[\\/]/.test(pair.native) ? pair.native : join(nativeRoot, pair.native);
    if (!existsSync(refPath) || !existsSync(nativePath)) {
      rows.push({ ...pair, error: `missing ${existsSync(refPath) ? 'native' : 'console'} frame`, consoleFrame: refPath, nativeFrame: nativePath });
      continue;
    }
    const ref = load(refPath), native = load(nativePath);
    if (ref.w !== native.w || ref.h !== native.h) {
      rows.push({ ...pair, error: `dimension mismatch ${ref.w}x${ref.h} vs ${native.w}x${native.h}`, consoleFrame: refPath, nativeFrame: nativePath });
      continue;
    }
    const measured: any = { frame: { db: psnr(ref, native), ssim: ssim(ref, native) } };
    for (const crop of pair.crops)
      measured[crop.name] = { rect: crop.rect, db: psnr(ref, native, crop.rect), ssim: ssim(ref, native, crop.rect) };
    rows.push({ ...pair, consoleFrame: refPath, nativeFrame: nativePath, ...measured });
    const frame = measured.frame;
    console.log(`${pair.id.padEnd(34)} ${frame.db.toFixed(2)} dB / ${frame.ssim.toFixed(4)}`
      + `   ${pair.crops.map(c => `${c.name} ${measured[c.name].db.toFixed(2)}`).join(' ')}`);
  }
  mkdirSync(outRoot, { recursive: true });
  writeFileSync(join(outRoot, 'extra-surfaces.json'), `${JSON.stringify({
    schema: 'xmb-fidelity-extra-surfaces/1', generatedAt: new Date().toISOString(),
    consoleRoot: consoleFrames, nativeRoot, pairs: rows,
  }, null, 2)}\n`);
  const header = ['surface_id', 'surface', 'class', 'console_frame', 'native_frame', 'frame_db', 'frame_ssim',
    ...doc.cropColumns.flatMap((name: string) => [`${name}_db`, `${name}_ssim`]),
    'content_differs', 'note', 'error'];
  const lines = [header.join(',')];
  for (const row of rows) {
    const cells = [row.id, row.surface, row.class, row.consoleFrame, row.nativeFrame,
      row.frame?.db?.toFixed(3) ?? '', row.frame?.ssim?.toFixed(4) ?? ''];
    for (const name of doc.cropColumns) {
      cells.push(row[name]?.db?.toFixed(3) ?? '', row[name]?.ssim?.toFixed(4) ?? '');
    }
    cells.push(String(row.contentDiffers), row.note, row.error ?? '');
    lines.push(cells.map(cell => `"${String(cell).replaceAll('"', '""')}"`).join(','));
  }
  writeFileSync(join(outRoot, 'extra-surfaces.csv'), `${lines.join('\n')}\n`);
  console.log(`\npairs ${rows.length} -> ${join(outRoot, 'extra-surfaces.json')} + extra-surfaces.csv`);
}

const command = process.argv[2];
if (command === 'capture') await capture();
else if (command === 'score') await score();
else {
  console.error('usage: bun work/fidelity-sweep.ts <capture|score>');
  process.exit(2);
}
