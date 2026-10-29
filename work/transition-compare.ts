// transition-compare.ts - frame-by-frame instrument for XMB transitions.
//
// Two sequences (console reference vs native capture) are reduced to the same
// named probes, aligned by matched progress, and scored against tolerance bands.
// It also writes the human-facing recordings (GIF + contact sheet via the
// bundled Python) and can drive the native launcher to capture its own bursts.
//
//   bun work/transition-compare.ts --list
//   bun work/transition-compare.ts                      # compare every transition
//   bun work/transition-compare.ts --transition page-open --measure
//   bun work/transition-compare.ts --capture            # drive native-xmb
//   bun work/transition-compare.ts --render             # GIF + contact sheet
//
// Measurement primitives are the ones the audits already use: a high-pass ink
// mask (pixel brighter than its surrounding box), ink runs merged into blobs,
// ink-weighted glyph centroids, luma percentiles in named bands. See
// native-xmb/audit/TRANSITION-HARNESS.md for the probe definitions and the
// provenance of every tolerance.

import { spawnSync } from 'node:child_process';
import { deflateSync } from 'node:zlib';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { decodePng, type PngImage } from './ab-compare.ts';

const BUN = 'C:\\Users\\Albert\\.bun\\bin\\bun.exe';
const PYTHON = 'C:\\Users\\Albert\\.dsh\\dsh-runtimes\\dsh-primary-runtime\\dependencies\\python\\python.exe';
const ROOT = 'C:\\dev';
const DEFAULT_EXE = 'C:\\dev\\out\\native-iconwave1\\re-ps3-native.exe';
const DEFAULT_PROBES = join(ROOT, 'xmb', 'work', 'transition-probes.json');
const DEV_FLASH = 'C:/dev/rpcs3/bin/dev_flash';

type Rect = [number, number, number, number];

// ---------------------------------------------------------------- CLI ----

type Options = {
  probes: string;
  out: string;
  transitions: string[];
  list: boolean;
  measure: boolean;
  capture: boolean;
  render: boolean;
  compare: boolean;
  exe: string;
  json: string;
  grid: number;
  attempts: number;
  gifFrames: number;
  verbose: boolean;
  force: boolean;
};

function parseArgs(argv: string[]): Options {
  const o: Options = {
    probes: DEFAULT_PROBES, out: '', transitions: [], list: false, measure: false,
    capture: false, render: false, compare: false, exe: DEFAULT_EXE, json: '',
    grid: 16, gifFrames: 24, verbose: false, force: false, attempts: 3,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      const v = argv[++i];
      if (v === undefined) fail(`missing value for ${a}`);
      return v;
    };
    if (a === '--list') o.list = true;
    else if (a === '--measure') o.measure = true;
    else if (a === '--capture') o.capture = true;
    else if (a === '--render') o.render = true;
    else if (a === '--compare') o.compare = true;
    else if (a === '--verbose' || a === '-v') o.verbose = true;
    else if (a === '--force') o.force = true;
    else if (a === '--probes') o.probes = next();
    else if (a === '--out') o.out = next();
    else if (a === '--exe') o.exe = next();
    else if (a === '--json') o.json = next();
    else if (a === '--transition' || a === '-t') o.transitions.push(next());
    else if (a === '--grid') o.grid = Number(next());
    else if (a === '--gif-frames') o.gifFrames = Number(next());
    else if (a === '--attempts') o.attempts = Number(next());
    else if (a === '--help' || a === '-h') usage(0);
    else fail(`unknown option ${a}`);
  }
  if (!o.transitions.length) o.transitions = ['all'];
  if (!o.measure && !o.capture && !o.render) o.compare = true;
  return o;
}

function usage(code: number): never {
  console.log(`transition-compare.ts - frame-by-frame compare of XMB transitions

  --list                    print the transitions defined in the probe JSON
  -t, --transition <id>     transition id, repeatable (default: all)
  --measure                 print the per-frame table for each side
  --compare                 alignment + verdict table (default action)
  --capture                 drive the native launcher for each capture recipe
  --render                  side-by-side GIF + contact sheet (+ mp4 with ffmpeg)
  --probes <file>           probe definitions (default work/transition-probes.json)
  --out <dir>               recording output directory (default: from the JSON)
  --exe <path>              native build to capture (default ${DEFAULT_EXE})
  --json <file>             write machine-readable results
  --grid <n>                alignment grid points (default 16)
  --gif-frames <n>          frames per recording (default 24)
  --force                   re-capture even when frames already exist
  --attempts <n>            native burst attempts to run, best coverage kept (default 3)
  -v, --verbose             per-frame probe records
`);
  process.exit(code);
}

function fail(message: string): never {
  console.error(`error: ${message}`);
  process.exit(2);
}

// ------------------------------------------------------------- probes ----

type Probe = {
  id: string;
  kind: 'bar' | 'ink' | 'label' | 'rows' | 'luma' | 'track';
  region: Rect;
  params?: Record<string, number | string | number[] | string[]>;
  note?: string;
};

type Sequence = {
  dir: string;
  match: string;
  timeIndex?: number;
  timeUnit?: 'ms' | 'index' | 'fps';
  fps?: number;
  crop?: Rect;
  timeRange?: [number, number];
  stagger?: number;
  offsetMs?: number;
  source?: string;
  keyMs?: number;
  limit?: number;
  // An at-rest frame captured before the key, so the driver's start value is
  // measured rather than extrapolated from the first burst frame.
  // Per-probe geometry for this side (a band crop and a full frame never share y).
  params?: Record<string, Record<string, unknown>>;
  prepend?: { file: string; t: number }[];
  // pinned sequences (XMB_DEPTH_PIN) state the progress of each frame instead
  pins?: { file: string; p: number }[];
};

type Capture = {
  tokens: string[];
  expect?: Record<string, string | number>;
  note?: string;
  skip?: string;
};

type Transition = {
  id: string;
  label: string;
  // endpoint: one settled frame per side (no console burst on disk).
  mode?: 'sequence' | 'endpoint';
  endpoint?: {
    console?: { file: string; crop?: Rect };
    native?: { file: string; crop?: Rect };
  };
  driver: string;
  driverSign?: number;
  // The metrics the printed table carries; every measured key still gets a
  // verdict in the max|delta| table and in the JSON.
  headline?: string[];
  view?: Rect;
  knownMs?: { console?: number; native?: number };
  tolerance?: Record<string, { pass: number; warn: number; unit?: string; source?: string }>;
  probes: Probe[];
  console?: Sequence;
  native?: Sequence;
  capture?: Capture;
  notes?: string[];
};

type ProbeFile = {
  version: number;
  outDir: string;
  toleranceDefaults: Record<string, { pass: number; warn: number; unit: string; source: string }>;
  transitions: Transition[];
};

function loadProbes(path: string): ProbeFile {
  if (!existsSync(path)) fail(`probe file not found: ${path}`);
  const text = readFileSync(path, 'utf8');
  // Notepad and PowerShell add a BOM; JSON.parse rejects it.
  const raw = JSON.parse(text.charCodeAt(0) === 0xfeff ? text.slice(1) : text) as ProbeFile;
  if (!raw.transitions?.length) fail(`${path}: no transitions`);
  for (const t of raw.transitions) {
    if (!t.id || !t.driver || !t.probes?.length) fail(`${path}: transition ${t.id ?? '?'} is incomplete`);
    if (t.mode !== 'endpoint' && !t.console && !t.native) fail(`${path}: transition ${t.id} has no sequence`);
  }
  return raw;
}

// ------------------------------------------------------------- frames ----

type Frame = { path: string; t: number; p?: number; name: string };
type SequenceFrames = { frames: Frame[]; crop: Rect; side: string; notes: string[] };

function discover(seq: Sequence, side: string): SequenceFrames {
  const notes: string[] = [];
  if (!existsSync(seq.dir)) fail(`${side}: directory not found ${seq.dir}`);
  const crop: Rect = seq.crop ?? [0, 0, 1920, 1080];
  const frames: Frame[] = [];
  if (seq.pins?.length) {
    for (const pin of seq.pins) {
      const path = resolve(seq.dir, pin.file);
      if (!existsSync(path)) fail(`${side}: pinned frame missing ${path}`);
      frames.push({ path, t: 0, p: pin.p, name: pin.file });
    }
    notes.push(`${frames.length} pinned frames (progress stated by the recipe, not measured)`);
    return { frames, crop, side, notes };
  }
  for (const pre of seq.prepend ?? []) {
    const path = resolve(seq.dir, pre.file);
    if (!existsSync(path)) fail(`${side}: preamble frame missing ${path}`);
    frames.push({ path, t: pre.t, name: pre.file });
  }
  if (frames.length) notes.push(`${frames.length} preamble frame(s) at rest`);
  const re = new RegExp(seq.match);
  const names = readdirSync(seq.dir).filter((f) => f.toLowerCase().endsWith('.png') && re.test(f));
  const unit = seq.timeUnit ?? 'ms';
  for (const name of names) {
    const m = re.exec(name);
    if (!m) continue;
    let t: number;
    if (unit === 'index') t = Number(m[Number(seq.timeIndex ?? 1)]);
    else if (unit === 'fps') t = (Number(m[Number(seq.timeIndex ?? 1)]) * 1000) / (seq.fps ?? 60);
    else t = Number(m[Number(seq.timeIndex ?? 1)]);
    if (!Number.isFinite(t)) continue;
    if (seq.stagger) t = t * seq.stagger;
    frames.push({ path: join(seq.dir, name), t, name });
  }
  frames.sort((a, b) => a.t - b.t);
  if (!frames.length) fail(`${side}: no frames matched ${seq.match} in ${seq.dir}`);
  let picked = frames;
  if (seq.timeRange) {
    picked = picked.filter((f) => f.t >= seq.timeRange![0] && f.t <= seq.timeRange![1]);
    if (!picked.length) fail(`${side}: time range ${seq.timeRange.join('..')} selected no frames`);
    notes.push(`window ${seq.timeRange[0]}..${seq.timeRange[1]} kept ${picked.length} of ${frames.length} frames`);
  }
  // Time is reported relative to the key that starts the transition: the console
  // key logs and the native burst both name it, so the two clocks share an origin.
  const shift = seq.offsetMs ?? seq.keyMs ?? 0;
  if (shift) for (const f of picked) f.t -= shift;
  if (seq.limit && picked.length > seq.limit) {
    const step = picked.length / seq.limit;
    picked = Array.from({ length: seq.limit }, (_, i) => picked[Math.floor(i * step)]);
    notes.push(`subsampled to ${seq.limit} frames`);
  }
  const span = picked.length > 1 ? picked[picked.length - 1].t - picked[0].t : 0;
  notes.push(`${picked.length} frames, ${picked[0].t.toFixed(0)}..${picked[picked.length - 1].t.toFixed(0)} ms (${span.toFixed(0)} ms span)`);
  return { frames: picked, crop, side, notes };
}

const imageCache = new Map<string, PngImage>();
function loadImage(path: string): PngImage {
  let img = imageCache.get(path);
  if (!img) {
    img = decodePng(path);
    imageCache.set(path, img);
    if (imageCache.size > 24) imageCache.delete(imageCache.keys().next().value as string);
  }
  return img;
}

// Region of a frame's own pixels: full-frame rect minus the capture crop.
function localRect(region: Rect, crop: Rect): Rect | null {
  const x0 = Math.max(region[0], crop[0]);
  const y0 = Math.max(region[1], crop[1]);
  const x1 = Math.min(region[0] + region[2], crop[0] + crop[2]);
  const y1 = Math.min(region[1] + region[3], crop[1] + crop[3]);
  if (x1 <= x0 || y1 <= y0) return null;
  return [x0 - crop[0], y0 - crop[1], x1 - x0, y1 - y0];
}

function coverage(region: Rect, crop: Rect): number {
  const w = Math.min(region[2], Math.max(0, Math.min(region[0] + region[2], crop[0] + crop[2]) - Math.max(region[0], crop[0])));
  const h = Math.min(region[3], Math.max(0, Math.min(region[1] + region[3], crop[1] + crop[3]) - Math.max(region[1], crop[1])));
  return (w * h) / (region[2] * region[3]);
}

// ---------------------------------------------------------- primitives ----

const LUMA_R = 0.2126, LUMA_G = 0.7152, LUMA_B = 0.0722;

function lumaRegion(img: PngImage, rect: Rect): Float32Array {
  const [x, y, w, h] = rect;
  const out = new Float32Array(w * h);
  for (let row = 0; row < h; row++) {
    let src = ((y + row) * img.w + x) * 4;
    let dst = row * w;
    for (let col = 0; col < w; col++, src += 4, dst++) {
      out[dst] = LUMA_R * img.rgba[src] + LUMA_G * img.rgba[src + 1] + LUMA_B * img.rgba[src + 2];
    }
  }
  return out;
}

// Box mean via an integral image, reflect padding at the borders (as the
// Python audits do with np.pad(mode='reflect')).
function boxMean(L: Float32Array, w: number, h: number, r: number): Float32Array {
  const pw = w + 2 * r, ph = h + 2 * r;
  const I = new Float64Array((pw + 1) * (ph + 1));
  for (let y = 0; y < ph; y++) {
    const sy = Math.min(h - 1, Math.max(0, y - r));
    const syRef = reflect(y - r, h);
    let rowSum = 0;
    for (let x = 0; x < pw; x++) {
      const sx = reflect(x - r, w);
      rowSum += L[syRef * w + sx];
      I[(y + 1) * (pw + 1) + (x + 1)] = I[y * (pw + 1) + (x + 1)] + rowSum;
    }
    void sy;
  }
  const out = new Float32Array(w * h);
  const k = 2 * r + 1;
  const kk = k * k;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const x0 = x, y0 = y, x1 = x + k, y1 = y + k;
      const sum = I[y1 * (pw + 1) + x1] - I[y0 * (pw + 1) + x1] - I[y1 * (pw + 1) + x0] + I[y0 * (pw + 1) + x0];
      out[y * w + x] = sum / kk;
    }
  }
  return out;
}

function reflect(i: number, n: number): number {
  if (n === 1) return 0;
  while (i < 0 || i >= n) {
    if (i < 0) i = -i;
    if (i >= n) i = 2 * (n - 1) - i;
  }
  return i;
}

type Mask = { m: Uint8Array; w: number; h: number };

function hpMask(L: Float32Array, w: number, h: number, r: number, k: number): Mask {
  const box = boxMean(L, w, h, r);
  const m = new Uint8Array(w * h);
  for (let i = 0; i < m.length; i++) m[i] = L[i] - box[i] > k ? 1 : 0;
  return { m, w, h };
}

function absMask(L: Float32Array, w: number, h: number, thr: number): Mask {
  const m = new Uint8Array(w * h);
  for (let i = 0; i < m.length; i++) m[i] = L[i] > thr ? 1 : 0;
  return { m, w, h };
}

function rowProfile(mask: Mask): Float64Array {
  const p = new Float64Array(mask.h);
  for (let y = 0; y < mask.h; y++) {
    let s = 0;
    for (let x = 0; x < mask.w; x++) s += mask.m[y * mask.w + x];
    p[y] = s;
  }
  return p;
}

function colProfile(mask: Mask): Float64Array {
  const p = new Float64Array(mask.w);
  for (let y = 0; y < mask.h; y++) for (let x = 0; x < mask.w; x++) p[x] += mask.m[y * mask.w + x];
  return p;
}

function runs(profile: Float64Array | number[], minLen: number, minGap = 1): [number, number][] {
  const idx: number[] = [];
  for (let i = 0; i < profile.length; i++) if (profile[i] >= minLen) idx.push(i);
  if (!idx.length) return [];
  const out: [number, number][] = [];
  let s = idx[0], p = idx[0];
  for (let i = 1; i < idx.length; i++) {
    if (idx[i] !== p + 1) {
      out.push([s, p]);
      s = idx[i];
    }
    p = idx[i];
  }
  out.push([s, p]);
  if (minGap <= 1 || out.length < 2) return out;
  const merged: [number, number][] = [out[0]];
  for (let i = 1; i < out.length; i++) {
    const last = merged[merged.length - 1];
    if (out[i][0] - last[1] - 1 <= minGap) last[1] = out[i][1];
    else merged.push(out[i]);
  }
  return merged;
}

function centroid(mask: Mask, s: number, e: number, y0 = 0, y1 = mask.h): { cx: number; cy: number; mass: number; top: number; bottom: number; w: number; h: number } {
  let mass = 0, sx = 0, sy = 0, top = Infinity, bottom = -Infinity, left = Infinity, right = -Infinity;
  for (let y = Math.max(0, y0); y < Math.min(mask.h, y1); y++) {
    for (let x = Math.max(0, s); x <= Math.min(mask.w - 1, e); x++) {
      if (!mask.m[y * mask.w + x]) continue;
      mass++; sx += x; sy += y;
      if (y < top) top = y;
      if (y > bottom) bottom = y;
      if (x < left) left = x;
      if (x > right) right = x;
    }
  }
  if (!mass) return { cx: NaN, cy: NaN, mass: 0, top: NaN, bottom: NaN, w: NaN, h: NaN };
  return { cx: sx / mass, cy: sy / mass, mass, top, bottom, w: right - left + 1, h: bottom - top + 1 };
}

function pct(values: number[], q: number): number {
  if (!values.length) return NaN;
  const s = [...values].sort((a, b) => a - b);
  const i = Math.min(s.length - 1, Math.max(0, Math.round((q / 100) * (s.length - 1))));
  return s[i];
}

function mean(values: number[]): number {
  if (!values.length) return NaN;
  let s = 0;
  for (const v of values) s += v;
  return s / values.length;
}

function median(values: number[]): number {
  return pct(values, 50);
}

function std(values: number[]): number {
  if (values.length < 2) return 0;
  const m = mean(values);
  let s = 0;
  for (const v of values) s += (v - m) * (v - m);
  return Math.sqrt(s / (values.length - 1));
}

// ---------------------------------------------------------- extractors ----

type Row = Record<string, number>;
type ExtractCtx = { img: PngImage; rect: Rect; crop: Rect; sequence: SequenceFrames; frameIndex: number };

function extract(probe: Probe, ctx: ExtractCtx, override?: Record<string, unknown>): Row | null {
  const region = ((override?.region as Rect) ?? probe.region) as Rect;
  const local = localRect(region, ctx.crop);
  if (!local) return null;
  const [rx, ry, rw, rh] = local;
  if (rw < 4 || rh < 4) return null;
  const L = lumaRegion(ctx.img, local);
  const p = { ...(probe.params ?? {}), ...(override ?? {}) } as unknown as Record<string, number>;
  switch (probe.kind) {
    case 'bar': return barProbe(L, rw, rh, p);
    case 'ink': return inkProbe(L, rw, rh, p);
    case 'label': return labelProbe(L, rw, rh, p);
    case 'rows': return rowsProbe(L, rw, rh, p);
    case 'luma': return lumaProbe(L, rw, rh, p);
    case 'track': return trackProbe(L, rw, rh, p, ctx, region);
    default: return null;
  }
}

// Category bar: ink blobs across the icon band, merged runs, focus by mass or
// by continuity with the previous frame (a merged pair of icons would otherwise
// pull the centroid between them).
function barProbe(L: Float32Array, w: number, h: number, p: Record<string, number>): Row {
  const r = p.r ?? 5, k = p.k ?? 10;
  const mask = hpMask(L, w, h, r, k);
  const col = colProfile(mask);
  const segs = runs(col, p.minCol ?? 2, p.gap ?? 6).filter(([s, e]) => e - s + 1 >= (p.minRun ?? 8));
  const blobs = segs.map(([s, e]) => {
    const c = centroid(mask, s, e);
    return { s, e, ...c };
  });
  const out: Row = { 'count': blobs.length, 'pitch': NaN, 'focusX': NaN, 'focusY': NaN, 'focusW': NaN, 'focusH': NaN, 'focusMass': NaN, 'merged': 0 };
  if (!blobs.length) return out;
  const medW = median(blobs.map((b) => b.w));
  const pick = p.focusAt !== undefined
    ? blobs.reduce((a, b) => (Math.abs(b.cx - p.focusAt) < Math.abs(a.cx - p.focusAt) ? b : a))
    : blobs.reduce((a, b) => (b.mass > a.mass ? b : a));
  out['focusX'] = pick.cx + (p.originX ?? 0);
  out['focusY'] = pick.cy + (p.originY ?? 0);
  out['focusW'] = pick.w;
  out['focusH'] = pick.h;
  out['focusMass'] = pick.mass;
  out['merged'] = pick.w > 1.6 * medW ? 1 : 0;
  out['leftX'] = NaN;
  out['rightX'] = NaN;
  out['leftMass'] = NaN;
  out['rightMass'] = NaN;
  out['leftLuma'] = NaN;
  out['rightLuma'] = NaN;
  const i = blobs.indexOf(pick);
  const lumaIn = (b: { s: number; e: number }) => {
    const vals: number[] = [];
    for (let y = 0; y < h; y++) for (let x = b.s; x <= b.e; x++) vals.push(L[y * w + x]);
    return pct(vals, 99);
  };
  if (i > 0) {
    out['leftX'] = blobs[i - 1].cx + (p.originX ?? 0);
    out['leftMass'] = blobs[i - 1].mass;
    out['leftLuma'] = lumaIn(blobs[i - 1]);
  }
  if (i >= 0 && i < blobs.length - 1) {
    out['rightX'] = blobs[i + 1].cx + (p.originX ?? 0);
    out['rightMass'] = blobs[i + 1].mass;
    out['rightLuma'] = lumaIn(blobs[i + 1]);
  }
  const centres = blobs.map((b) => b.cx).sort((a, b) => a - b);
  const gaps = centres.slice(1).map((c, j) => c - centres[j]).filter((d) => d >= (p.pitchMin ?? 130) && d <= (p.pitchMax ?? 280));
  if (gaps.length) out['pitch'] = median(gaps);
  return out;
}

function inkProbe(L: Float32Array, w: number, h: number, p: Record<string, number>): Row {
  const mask = p.mode === 'abs'
    ? absMask(L, w, h, p.thr ?? 150)
    : hpMask(L, w, h, p.r ?? 5, p.k ?? 10);
  const col = colProfile(mask);
  const row = rowProfile(mask);
  const segs = runs(col, 1, p.gap ?? 0);
  const out: Row = { 'mass': 0, 'cx': NaN, 'cy': NaN, 'w': NaN, 'h': NaN, 'left': NaN, 'right': NaN, 'top': NaN, 'bottom': NaN, 'p99': NaN };
  const vals: number[] = [];
  for (let i = 0; i < L.length; i++) if (mask.m[i]) vals.push(L[i]);
  out['mass'] = vals.length;
  out['p99'] = vals.length ? pct(vals, 99) : NaN;
  if (!segs.length) return out;
  const first = segs[0], last = segs[segs.length - 1];
  const c = centroid(mask, first[0], last[1]);
  out['cx'] = c.cx;
  out['cy'] = c.cy;
  out['w'] = c.w;
  out['h'] = c.h;
  out['left'] = first[0];
  out['right'] = last[1];
  let top = -1, bottom = -1;
  for (let y = 0; y < h; y++) if (row[y] > 0) { if (top < 0) top = y; bottom = y; }
  out['top'] = top;
  out['bottom'] = bottom;
  return out;
}

// A text line: the densest ink row band inside the region, its left edge, and
// the text blocks in it (gaps < blockGap belong to one label).
function labelProbe(L: Float32Array, w: number, h: number, p: Record<string, number>): Row {
  const mask = hpMask(L, w, h, p.r ?? 6, p.k ?? 14);
  const row = rowProfile(mask);
  const pad = p.rowPad ?? 10;
  let peak = 0;
  for (let y = 0; y < h; y++) if (row[y] > row[peak]) peak = y;
  const y0 = Math.max(0, peak - pad), y1 = Math.min(h - 1, peak + pad);
  let mass = 0;
  for (let y = y0; y <= y1; y++) mass += row[y];
  const col = new Float64Array(w);
  for (let y = y0; y <= y1; y++) for (let x = 0; x < w; x++) col[x] += mask.m[y * w + x];
  const minRows = p.minRows ?? 2;
  const segs = runs(col, minRows, p.blockGap ?? 22).filter(([s, e]) => e - s + 1 >= (p.minBlock ?? 20));
  const out: Row = { 'left': NaN, 'y': peak, 'mass': mass, 'blocks': segs.length, 'peak': NaN };
  if (!segs.length) return out;
  out['left'] = segs[0][0];
  const lums: number[] = [];
  for (let y = y0; y <= y1; y++) for (let x = segs[0][0]; x <= segs[0][1]; x++) lums.push(L[y * w + x]);
  out['peak'] = pct(lums, 99);
  segs.slice(0, 4).forEach(([s, e], i) => {
    out[`block${i}`] = s;
    out[`block${i}End`] = e;
  });
  return out;
}

// Item rows: horizontal ink bands in the row block, each with its label left
// edge and brightness. This is the metric a band-cropped console burst cannot
// carry; the tool reports it as unavailable rather than guessing.
function rowsProbe(L: Float32Array, w: number, h: number, p: Record<string, number>): Row {
  const mask = hpMask(L, w, h, p.r ?? 5, p.k ?? 10);
  const row = rowProfile(mask);
  const bands = runs(row, p.minRowInk ?? 4, p.rowGap ?? 6).filter(([s, e]) => e - s + 1 >= (p.minRowH ?? 5));
  const out: Row = { 'count': bands.length, 'blockLeft': NaN, 'blockTop': NaN, 'blockBottom': NaN };
  if (!bands.length) return out;
  let blockLeft = Infinity;
  bands.slice(0, 6).forEach(([s, e], i) => {
    const col = new Float64Array(w);
    for (let y = s; y <= e; y++) for (let x = 0; x < w; x++) col[x] += mask.m[y * w + x];
    const segs = runs(col, 1, p.labelGap ?? 12);
    const firstLabel = segs.find(([a, b]) => b - a >= (p.minLabelWidth ?? 30));
    out[`y${i}`] = (s + e) / 2;
    out[`left${i}`] = firstLabel ? firstLabel[0] : NaN;
    if (firstLabel && firstLabel[0] < blockLeft) blockLeft = firstLabel[0];
  });
  out['blockLeft'] = blockLeft === Infinity ? NaN : blockLeft;
  out['blockTop'] = bands[0][0];
  out['blockBottom'] = bands[Math.min(bands.length, 6) - 1][1];
  return out;
}

function lumaProbe(L: Float32Array, w: number, h: number, p: Record<string, number>): Row {
  const vals: number[] = [];
  const stride = Math.max(1, Math.round(p.sample ?? 1));
  for (let y = 0; y < h; y += stride) for (let x = 0; x < w; x += stride) vals.push(L[y * w + x]);
  return { 'mean': mean(vals), 'p99': pct(vals, 99), 'std': std(vals), 'min': Math.min(...vals), 'max': Math.max(...vals) };
}

// Normalised cross-correlation of a template taken from a settled frame against
// every candidate offset and scale in the search window. This is the audit's
// template track: it returns the incoming column's x/y and its scale.
function bilinear(L: Float32Array, w: number, h: number, x: number, y: number): number {
  const x0 = Math.max(0, Math.min(w - 1, Math.floor(x)));
  const y0 = Math.max(0, Math.min(h - 1, Math.floor(y)));
  const x1 = Math.min(w - 1, x0 + 1), y1 = Math.min(h - 1, y0 + 1);
  const fx = Math.max(0, Math.min(1, x - x0)), fy = Math.max(0, Math.min(1, y - y0));
  const a = L[y0 * w + x0], b = L[y0 * w + x1], c = L[y1 * w + x0], d = L[y1 * w + x1];
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

function resizeTemplate(ref: Float32Array, tw: number, th: number, scale: number): Float32Array {
  const sw = Math.max(6, Math.round(tw * scale)), sh = Math.max(6, Math.round(th * scale));
  const out = new Float32Array(sw * sh);
  for (let y = 0; y < sh; y++) for (let x = 0; x < sw; x++)
    out[y * sw + x] = bilinear(ref, tw, th, (x + 0.5) / scale - 0.5, (y + 0.5) / scale - 0.5);
  return out;
}

function normalizeTemplate(a: Float32Array): { v: Float32Array; ss: number } {
  let m = 0;
  for (const v of a) m += v;
  m /= a.length;
  const v = new Float32Array(a.length);
  let ss = 0;
  for (let i = 0; i < a.length; i++) { v[i] = a[i] - m; ss += v[i] * v[i]; }
  return { v, ss };
}

function ncc(L: Float32Array, w: number, h: number, sw: number, sh: number, px: number, py: number, tpl: Float32Array, tss: number): number {
  if (px < 0 || py < 0 || px + sw > w || py + sh > h) return -2;
  let s = 0;
  for (let y = 0; y < sh; y++) for (let x = 0; x < sw; x++) s += L[(py + y) * w + px + x];
  const mu = s / (sw * sh);
  let fss = 0, dot = 0;
  for (let y = 0; y < sh; y++) for (let x = 0; x < sw; x++) {
    const f = L[(py + y) * w + px + x] - mu;
    fss += f * f;
    dot += f * tpl[y * sw + x];
  }
  if (fss <= 1e-9 || tss <= 1e-9) return -2;
  return dot / Math.sqrt(fss * tss);
}

function trackProbe(L: Float32Array, w: number, h: number, p: Record<string, number>, ctx: ExtractCtx, region: Rect): Row {
  const tplRect = (p.template as unknown as Rect) ?? [region[0], region[1], 48, 48];
  const tplLocal = localRect(tplRect, ctx.crop);
  if (!tplLocal) return { 'x': NaN, 'y': NaN, 'scale': NaN, 'corr': NaN };
  // the search runs on this probe's region pixels, so the template origin has to
  // be expressed relative to the region, not to the frame
  const rl = localRect(region, ctx.crop);
  if (!rl) return { 'x': NaN, 'y': NaN, 'scale': NaN, 'corr': NaN };
  const relX = tplLocal[0] - rl[0], relY = tplLocal[1] - rl[1];
  const from = String(p.templateFrom ?? 'last');
  const pool = ctx.sequence.frames;
  const refFrame = from === 'first' ? pool[0] : pool[pool.length - 1];
  if (!refFrame || refFrame.path === ctx.img.path) return { 'x': NaN, 'y': NaN, 'scale': NaN, 'corr': NaN };
  const refL = lumaRegion(loadImage(refFrame.path), tplLocal);
  const tw = tplLocal[2], th = tplLocal[3];
  const scales = ((p.scales as unknown as number[]) ?? [1]).slice(0, 5);
  const rangeX = p.dx ?? 60, rangeY = p.dy ?? 12;
  const step = Math.max(1, Math.round(p.step ?? 4));
  const baseX = relX, baseY = relY;
  let best = { c: -2, x: NaN, y: NaN, s: 0, sw: 0, sh: 0 };
  for (const sc of scales) {
    const tpl = resizeTemplate(refL, tw, th, sc);
    const sw = Math.max(6, Math.round(tw * sc)), sh = Math.max(6, Math.round(th * sc));
    const t = normalizeTemplate(tpl);
    for (let dy = -rangeY; dy <= rangeY; dy += step) {
      for (let dx = -rangeX; dx <= rangeX; dx += step) {
        const c = ncc(L, w, h, sw, sh, baseX + dx, baseY + dy, t.v, t.ss);
        if (c > best.c) best = { c, x: baseX + dx + sw / 2, y: baseY + dy + sh / 2, s: sc, sw, sh };
      }
    }
  }
  if (best.s > 0) {
    const tpl = resizeTemplate(refL, tw, th, best.s);
    const t = normalizeTemplate(tpl);
    const cx = Math.round(best.x - best.sw / 2), cy = Math.round(best.y - best.sh / 2);
    for (let dy = -step; dy <= step; dy++) {
      for (let dx = -step; dx <= step; dx++) {
        const c = ncc(L, w, h, best.sw, best.sh, cx + dx, cy + dy, t.v, t.ss);
        if (c > best.c) best = { ...best, c, x: cx + dx + best.sw / 2, y: cy + dy + best.sh / 2 };
      }
    }
  }
  return { 'x': Number.isFinite(best.x) ? best.x + region[0] : NaN, 'y': Number.isFinite(best.y) ? best.y + region[1] : NaN, 'scale': best.s, 'corr': best.c };
}

// ------------------------------------------------------------ measure ----

type FrameRecord = { t: number; p: number; values: Record<string, number>; frame: Frame };
type SideMeasure = {
  side: string;
  frames: FrameRecord[];
  missing: Map<string, number>;
  coverage: Map<string, number>;
  crop: Rect;
  notes: string[];
  source: string;
};

function measure(side: 'console' | 'native', seq: Sequence, probes: Probe[], driver: string): SideMeasure {
  const sf = discover(seq, side);
  const missing = new Map<string, number>();
  const cov = new Map<string, number>();
  for (const probe of probes) {
    cov.set(probe.id, coverage(probe.region, sf.crop));
    missing.set(probe.id, 0);
  }
  const raw: { t: number; values: Record<string, number>; frame: Frame }[] = [];
  for (const frame of sf.frames) {
    const img = loadImage(frame.path);
    const values: Record<string, number> = {};
    for (const probe of probes) {
      const row = extract(probe, { img, rect: probe.region, crop: sf.crop, sequence: sf, frameIndex: 0 }, seq.params?.[probe.id]);
      if (!row) { missing.set(probe.id, (missing.get(probe.id) ?? 0) + 1); continue; }
      for (const [k, v] of Object.entries(row)) values[`${probe.id}.${k}`] = v;
    }
    raw.push({ t: frame.t, values, frame });
  }
  // Motion energy per frame: the fraction of sampled pixels whose luma moved by
  // more than a threshold since the previous frame, per 100 ms. A change count
  // ignores the slow wave gradient that a mean difference would swamp the real
  // motion with. Used as the @motion driver and to find the motion window.
  {
    let prevL: Float32Array | null = null;
    let prevT = 0;
    for (let i = 0; i < raw.length; i++) {
      const f = raw[i];
      const img = loadImage(f.frame.path);
      const L = lumaRegion(img, [0, 0, img.w, img.h]);
      if (prevL && prevL.length === L.length) {
        let changed = 0, n = 0;
        for (let p = 0; p < L.length; p += 4) { n++; if (Math.abs(L[p] - prevL[p]) > 10) changed++; }
        const dtv = Math.max(1, f.t - prevT);
        f.values['motion.rate'] = (changed / Math.max(1, n)) * (100 / dtv);
      } else f.values['motion.rate'] = 0;
      prevL = L; prevT = f.t;
    }
  }
  // Pinned sequences state their progress; there is no timing to derive.
  if (seq.pins?.length) {
    const frames: FrameRecord[] = raw.map((r, i) => ({
      t: i, p: sf.frames[i].p ?? 0, values: { ...r.values, p: sf.frames[i].p ?? 0, pSigned: sf.frames[i].p ?? 0 }, frame: r.frame,
    }));
    const known = frames.map((f) => f.p);
    return {
      side, frames, missing, coverage: cov, crop: sf.crop,
      notes: [...sf.notes, `progress pinned (${known.map((v) => v.toFixed(2)).join(', ')})`],
      source: seq.source ?? seq.dir,
    };
  }
  // @motion: progress from the measured motion window (first to last moving
  // frame) instead of a content metric. Used where one side pins the geometry
  // the other slides, so no x/y is a shared progress axis.
  if (driver === '@motion') {
    const rates = raw.map((r) => r.values['motion.rate'] ?? 0);
    // A frame is 'moving' when its motion energy is above a quarter of this
    // sequence's own peak: a relative test, because a sequence that is entirely
    // mid-move (a burst that starts late) has no still frame to calibrate on.
    const peak = Math.max(...rates);
    const thr = Math.max(peak * 0.25, 0.02);
    const moving = rates.map((v, i) => (i > 0 && v > thr ? i : -1)).filter((i) => i >= 0);
    if (!moving.length) fail(`${side}: no motion detected above ${thr.toFixed(3)} (baseline ${baseline.toFixed(3)})`);
    const onset = raw[moving[0]].t, settle = raw[moving[moving.length - 1]].t;
    for (const r of raw) {
      const p = (r.t - onset) / Math.max(1, settle - onset);
      r.values['p'] = Math.max(0, Math.min(1, p));
      r.values['pSigned'] = p;
    }
    return {
      side,
      frames: raw.map((r) => ({ t: r.t, p: r.values['p'], values: r.values, frame: r.frame })),
      missing, coverage: cov, crop: sf.crop,
      notes: [...sf.notes, `driver @motion: window ${onset}..${settle} ms (threshold ${thr.toFixed(3)})`],
      source: seq.source ?? seq.dir,
    };
  }
  // Progress: the driver normalised between its settled start and settled end.
  const driverValues = raw.map((r) => r.values[driver]).filter((v) => Number.isFinite(v));
  if (driverValues.length < 3) fail(`${side}: driver metric ${driver} is not measurable (${driverValues.length} samples)`);
  const head = raw.slice(0, Math.max(2, Math.round(raw.length * 0.1)));
  const tail = raw.slice(-Math.max(2, Math.round(raw.length * 0.1)));
  const v0 = median(head.map((r) => r.values[driver]).filter((v) => Number.isFinite(v)));
  const v1 = median(tail.map((r) => r.values[driver]).filter((v) => Number.isFinite(v)));
  if (!Number.isFinite(v0) || !Number.isFinite(v1) || Math.abs(v1 - v0) < 1e-6)
    fail(`${side}: driver ${driver} does not move (${v0} -> ${v1})`);
  const sign = Math.sign(v1 - v0);
  for (const r of raw) {
    const v = r.values[driver];
    r.values['p'] = Number.isFinite(v) ? (v - v0) / (v1 - v0) : NaN;
    r.values['pSigned'] = r.values['p'];
    void sign;
  }
  for (const r of raw) {
    const pv = r.values['p'];
    r.values['p'] = Number.isFinite(pv) ? Math.max(0, Math.min(1, pv)) : NaN;
  }
  return {
    side,
    frames: raw.map((r) => ({ t: r.t, p: r.values['p'], values: r.values, frame: r.frame })),
    missing, coverage: cov, crop: sf.crop,
    notes: [...sf.notes, `driver ${driver}: ${v0.toFixed(2)} -> ${v1.toFixed(2)}`],
    source: seq.source ?? seq.dir,
  };
}

// One settled frame per side, measured with the same probes: the endpoint mode.
function measureEndpoint(side: 'console' | 'native', spec: { file: string; crop?: Rect; params?: Record<string, Record<string, unknown>> }, probes: Probe[]): SideMeasure {
  const crop: Rect = spec.crop ?? [0, 0, 1920, 1080];
  const paramOverride = spec.params;
  if (!existsSync(spec.file)) fail(`${side}: endpoint frame missing ${spec.file}`);
  const img = loadImage(spec.file);
  const frame: Frame = { path: spec.file, t: 0, name: basename(spec.file) };
  const values: Record<string, number> = {};
  const missing = new Map<string, number>();
  const cov = new Map<string, number>();
  for (const probe of probes) {
    cov.set(probe.id, coverage(probe.region, crop));
    const row = extract(probe, { img, rect: probe.region, crop, sequence: { frames: [frame], crop, side, notes: [] }, frameIndex: 0 }, paramOverride?.[probe.id]);
    if (!row) { missing.set(probe.id, 1); continue; }
    for (const [k, v] of Object.entries(row)) values[`${probe.id}.${k}`] = v;
  }
  values['p'] = 1;
  values['pSigned'] = 1;
  return {
    side, frames: [{ t: 0, p: 1, values, frame }], missing, coverage: cov, crop,
    notes: [`endpoint still ${basename(spec.file)} (${crop[2]}x${crop[3]} of the capture)`],
    source: spec.file,
  };
}

// ----------------------------------------------------------- alignment ----

type GridPoint = { p: number; tConsole: number; tNative: number; deltas: Record<string, number>; flags: Record<string, string> };
type Comparison = {
  id: string;
  label: string;
  headline: string[];
  console: SideMeasure;
  native: SideMeasure;
  metrics: string[];
  grid: GridPoint[];
  bestOffsetMs: number;
  lagSpreadMs: number;
  maxDelta: Record<string, number>;
  noise: Record<string, number>;
  verdicts: Record<string, 'PASS' | 'WARN' | 'FAIL' | 'n/a'>;
  verdict: 'PASS' | 'WARN' | 'FAIL' | 'NATIVE-ONLY' | 'NO-DATA';
  duration: { consoleMs: number; nativeMs: number; consoleSettleMs: number; nativeSettleMs: number };
  scores: { console: number; native: number; detail: Record<string, Record<string, number>> };
  tolerance: Record<string, { pass: number; warn: number; unit: string; source: string }>;
  notes: string[];
};

function timeAtP(side: SideMeasure, p: number): number {
  const fs = side.frames.filter((f) => Number.isFinite(f.p));
  if (!fs.length) return NaN;
  for (let i = 1; i < fs.length; i++) {
    const a = fs[i - 1], b = fs[i];
    if ((a.p - p) * (b.p - p) <= 0 && a.p !== b.p) {
      const u = (p - a.p) / (b.p - a.p);
      return a.t + u * (b.t - a.t);
    }
  }
  // no bracket: extrapolate nothing, clamp to the nearest end
  return p <= fs[0].p ? fs[0].t : fs[fs.length - 1].t;
}

function valueAtP(side: SideMeasure, metric: string, p: number): number {
  const fs = side.frames.filter((f) => Number.isFinite(f.p) && Number.isFinite(f.values[metric]));
  if (!fs.length) return NaN;
  for (let i = 1; i < fs.length; i++) {
    const a = fs[i - 1], b = fs[i];
    if ((a.p - p) * (b.p - p) <= 0 && a.p !== b.p) {
      const u = (p - a.p) / (b.p - a.p);
      return a.values[metric] + u * (b.values[metric] - a.values[metric]);
    }
  }
  return p <= fs[0].p ? fs[0].values[metric] : fs[fs.length - 1].values[metric];
}

function motionWindow(side: SideMeasure): { firstMs: number; lastMs: number; settleMs: number } {
  const fs = side.frames.filter((f) => Number.isFinite(f.p));
  const first = fs.find((f) => f.p > 0.02);
  const last = [...fs].reverse().find((f) => f.p < 0.98);
  const settle = fs.find((f) => f.p >= 0.98);
  if (!first) return { firstMs: NaN, lastMs: NaN, settleMs: NaN };
  return {
    firstMs: first.t,
    lastMs: last ? last.t : fs[fs.length - 1].t,
    settleMs: settle ? settle.t : fs[fs.length - 1].t,
  };
}

function motionScore(side: SideMeasure): { score: number; detail: Record<string, number> } {
  const fs = side.frames.filter((f) => Number.isFinite(f.p));
  if (fs.length < 3) return { score: 0, detail: { coverage: 0, monotonic: 0, smoothness: 0, overshoot: 0 } };
  const t0 = fs[0].t, t1 = fs[fs.length - 1].t;
  let covered = 0;
  const N = 16;
  for (let i = 0; i <= N; i++) {
    const p = i / N;
    const t = timeAtP(side, p);
    if (Number.isFinite(t) && (i === 0 || i === N || (t > t0 && t < t1))) covered++;
  }
  const dp: number[] = [];
  let monotonic = 0;
  for (let i = 1; i < fs.length; i++) {
    const d = fs[i].p - fs[i - 1].p;
    dp.push(d);
    if (d >= -1e-9) monotonic++;
  }
  let jerk = 0;
  for (let i = 1; i < fs.length - 1; i++) jerk += Math.abs(fs[i + 1].p - 2 * fs[i].p + fs[i - 1].p);
  jerk = fs.length > 2 ? jerk / (fs.length - 2) : 0;
  const step = mean(dp.map(Math.abs));
  const overshoot = Math.max(0, ...fs.map((f) => Math.abs(f.values['pSigned'] ?? f.p) > 1 ? Math.abs(f.values['pSigned'] ?? f.p) - 1 : 0));
  const coverageS = covered / (N + 1);
  const monotonicS = monotonic / Math.max(1, fs.length - 1);
  const smoothS = step > 1e-9 ? Math.max(0, Math.min(1, 1 - jerk / (step * 1.5))) : 0;
  const score = 100 * (0.35 * coverageS + 0.35 * monotonicS + 0.3 * smoothS) * (1 - Math.min(1, overshoot * 4));
  return {
    score: Math.round(score * 10) / 10,
    detail: { coverage: coverageS, monotonic: monotonicS, smoothness: smoothS, overshoot, jerk, meanStep: step },
  };
}

function compare(t: Transition, probes: ProbeFile, consoleMeasure: SideMeasure | null, nativeMeasure: SideMeasure | null, gridN: number): Comparison {
  const tolerance: Record<string, { pass: number; warn: number; unit: string; source: string }> = {};
  for (const [metric, tbl] of Object.entries(probes.toleranceDefaults)) tolerance[metric] = tbl;
  for (const [metric, tbl] of Object.entries(t.tolerance ?? {}))
    tolerance[metric] = { pass: tbl.pass, warn: tbl.warn, unit: tbl.unit ?? 'px', source: tbl.source ?? 'transition override' };

  const metrics = t.probes.map((pr) => pr.id);
  // Every probe base name is expanded to the scalar keys the side produced.
  const baseKeys = new Set<string>();
  for (const side of [consoleMeasure, nativeMeasure]) {
    if (!side) continue;
    for (const f of side.frames) for (const k of Object.keys(f.values)) baseKeys.add(k);
  }
  const grid: GridPoint[] = [];
  const notes: string[] = [];
  const dur = { consoleMs: NaN, nativeMs: NaN, consoleSettleMs: NaN, nativeSettleMs: NaN };
  const scores = { console: 0, native: 0, detail: {} as Record<string, Record<string, number>> };
  const noise: Record<string, number> = {};
  if (consoleMeasure) {
    const tail = consoleMeasure.frames.slice(-Math.max(3, Math.round(consoleMeasure.frames.length * 0.2)));
    for (const key of baseKeys) {
      const vals = tail.map((f) => f.values[key]).filter((v) => Number.isFinite(v));
      if (vals.length > 2) noise[key] = std(vals);
    }
  }
  // Endpoint: both sides are one settled frame. There is no time axis, so the
  // comparison is pure geometry and the durations stay unavailable.
  if (t.mode === 'endpoint') {
    const deltas: Record<string, number> = {}, flags: Record<string, string> = {};
    const maxDelta: Record<string, number> = {};
    const verdicts: Record<string, 'PASS' | 'WARN' | 'FAIL' | 'n/a'> = {};
    if (!consoleMeasure || !nativeMeasure) {
      notes.push('single-side endpoint: no counterpart still on disk, so no delta and no verdict');
      return {
        id: t.id, label: t.label, headline: t.headline ?? [],
        console: consoleMeasure ?? ({} as SideMeasure), native: nativeMeasure ?? ({} as SideMeasure),
        metrics, grid: [], bestOffsetMs: NaN, lagSpreadMs: NaN, maxDelta: {}, noise, verdicts: {},
        verdict: 'NATIVE-ONLY', duration: dur, scores, tolerance, notes,
      };
    }
    if (consoleMeasure && nativeMeasure) {
      for (const key of baseKeys) {
        if (key === 'p' || key === 'pSigned') continue;
        const a = consoleMeasure.frames[0].values[key], b = nativeMeasure.frames[0].values[key];
        deltas[key] = Number.isFinite(a) && Number.isFinite(b) ? b - a : NaN;
        if (!Number.isFinite(deltas[key])) { verdicts[key] = 'n/a'; continue; }
        const band = tolerance[key];
        if (!band) { verdicts[key] = 'n/a'; flags[key] = 'no-band'; continue; }
        maxDelta[key] = Math.abs(deltas[key]);
        verdicts[key] = maxDelta[key] <= band.pass ? 'PASS' : maxDelta[key] <= band.warn ? 'WARN' : 'FAIL';
        flags[key] = verdicts[key];
      }
      notes.push('endpoint comparison: console still vs settled native frame, no duration');
      if (consoleMeasure.frames[0]) scores.console = 0;
      if (nativeMeasure.frames[0]) scores.native = 0;
    }
    const rank = { PASS: 0, 'n/a': 1, WARN: 2, FAIL: 3 } as Record<string, number>;
    // The transition verdict is the worst of the HEADLINE metrics: the ones the
    // transition asserts. Content-dependent extras still get per-key verdicts.
    const headKeys = Object.keys(verdicts).filter((k) => !t.headline?.length || t.headline.includes(k));
    // No measurable headline metric means the transition is not comparable, not
    // that everything failed: report NO-DATA rather than the worst of the extras.
    const worst = headKeys.length
      ? headKeys.map((k) => verdicts[k]).reduce((a, b) => (rank[b] > rank[a] ? b : a), 'PASS' as 'PASS' | 'WARN' | 'FAIL' | 'n/a')
      : null;
    return {
      id: t.id, label: t.label, headline: t.headline ?? [], console: consoleMeasure ?? ({} as SideMeasure),
      native: nativeMeasure ?? ({} as SideMeasure), metrics, grid: [{ p: 1, tConsole: 0, tNative: 0, deltas, flags }],
      bestOffsetMs: NaN, lagSpreadMs: NaN, maxDelta, noise, verdicts,
      verdict: worst === 'n/a' ? 'NO-DATA' : worst, duration: dur, scores, tolerance, notes,
    };
  }

  if (consoleMeasure && nativeMeasure) {
    const cw = motionWindow(consoleMeasure), nw = motionWindow(nativeMeasure);
    dur.consoleMs = cw.settleMs - cw.firstMs;
    dur.nativeMs = nw.settleMs - nw.firstMs;
    dur.consoleSettleMs = cw.settleMs;
    dur.nativeSettleMs = nw.settleMs;
    const cs = motionScore(consoleMeasure), ns = motionScore(nativeMeasure);
    scores.console = cs.score; scores.native = ns.score;
    scores.detail = { console: cs.detail, native: ns.detail };
    const cps = consoleMeasure.frames.map((f) => f.p).filter((v) => Number.isFinite(v));
    const nps = nativeMeasure.frames.map((f) => f.p).filter((v) => Number.isFinite(v));
    const lo = Math.max(Math.min(...cps), Math.min(...nps));
    const hi = Math.min(Math.max(...cps), Math.max(...nps));
    notes.push(`progress overlap p ${lo.toFixed(2)}..${hi.toFixed(2)}`);
    for (let i = 0; i <= gridN; i++) {
      const p = i / gridN;
      const tC = timeAtP(consoleMeasure, p), tN = timeAtP(nativeMeasure, p);
      const deltas: Record<string, number> = {}, flags: Record<string, string> = {};
      for (const pr of t.probes) {
        for (const key of [...baseKeys].filter((k) => k.startsWith(`${pr.id}.`) && !k.startsWith(`${pr.id}.p`))) {
          const a = valueAtP(consoleMeasure, key, p), b = valueAtP(nativeMeasure, key, p);
          const d = Number.isFinite(a) && Number.isFinite(b) ? b - a : NaN;
          deltas[key] = d;
          const band = tolerance[key] ?? tolerance[pr.id];
          if (!band) flags[key] = 'no-band';
          else if (!Number.isFinite(d)) flags[key] = 'n/a';
          else if (Math.abs(d) <= band.pass) flags[key] = 'PASS';
          else if (Math.abs(d) <= band.warn) flags[key] = 'WARN';
          else flags[key] = 'FAIL';
        }
      }
      if (p < lo - 1e-6 || p > hi + 1e-6) for (const k of Object.keys(deltas)) { deltas[k] = NaN; flags[k] = 'n/a'; }
      grid.push({ p, tConsole: tC, tNative: tN, deltas, flags });
    }
    const lags = grid.map((g) => g.tNative - g.tConsole).filter((v) => Number.isFinite(v));
    const bestOffset = median(lags);
    const spread = std(lags);
    const maxDelta: Record<string, number> = {};
    for (const g of grid) for (const [k, d] of Object.entries(g.deltas)) {
      if (!Number.isFinite(d)) continue;
      maxDelta[k] = Math.max(maxDelta[k] ?? 0, Math.abs(d));
    }
    const verdicts: Record<string, 'PASS' | 'WARN' | 'FAIL' | 'n/a'> = {};
    for (const [k, v] of Object.entries(maxDelta)) {
      const band = tolerance[k];
      if (!band) verdicts[k] = 'n/a';
      else verdicts[k] = v <= band.pass ? 'PASS' : v <= band.warn ? 'WARN' : 'FAIL';
    }
    const rank = { PASS: 0, 'n/a': 1, WARN: 2, FAIL: 3 } as Record<string, number>;
    // The transition verdict is the worst of the HEADLINE metrics: the ones the
    // transition asserts. Content-dependent extras still get per-key verdicts.
    const headKeys = Object.keys(verdicts).filter((k) => !t.headline?.length || t.headline.includes(k));
    // No measurable headline metric means the transition is not comparable, not
    // that everything failed: report NO-DATA rather than the worst of the extras.
    const worst = headKeys.length
      ? headKeys.map((k) => verdicts[k]).reduce((a, b) => (rank[b] > rank[a] ? b : a), 'PASS' as 'PASS' | 'WARN' | 'FAIL' | 'n/a')
      : null;
    return {
      id: t.id, label: t.label, headline: t.headline ?? [], console: consoleMeasure, native: nativeMeasure, metrics, grid,
      bestOffsetMs: bestOffset, lagSpreadMs: spread, maxDelta, noise, verdicts,
      verdict: worst === null || worst === 'n/a' ? 'NO-DATA' : worst, duration: dur, scores, tolerance, notes,
    };
  }

  // One side only: still measure, still report, never invent a delta.
  const side = consoleMeasure ?? nativeMeasure!;
  const mw = motionWindow(side);
  if (consoleMeasure) dur.consoleMs = mw.settleMs - mw.firstMs; else dur.nativeMs = mw.settleMs - mw.firstMs;
  const sc = motionScore(side);
  if (consoleMeasure) { scores.console = sc.score; scores.detail = { console: sc.detail }; }
  else { scores.native = sc.score; scores.detail = { native: sc.detail }; }
  const verdicts: Record<string, 'PASS' | 'WARN' | 'FAIL' | 'n/a'> = {};
  for (const key of baseKeys) if (!key.startsWith('p')) verdicts[key] = 'n/a';
  notes.push(`no ${consoleMeasure ? 'native' : 'console'} sequence: single-side measurement only`);
  return {
    id: t.id, label: t.label, headline: t.headline ?? [], console: consoleMeasure ?? ({} as SideMeasure), native: nativeMeasure ?? ({} as SideMeasure),
    metrics, grid: [], bestOffsetMs: NaN, lagSpreadMs: NaN, maxDelta: {}, noise, verdicts,
    verdict: 'NATIVE-ONLY', duration: dur, scores, tolerance, notes,
  };
}

// ------------------------------------------------------------- output ----

function fmt(v: number, unit = '', width = 0): string {
  if (!Number.isFinite(v)) return 'n/a'.padStart(width);
  const s = `${v.toFixed(1)}${unit}`;
  return width ? s.padStart(width) : s;
}

function printMeasure(cmp: Comparison): void {
  for (const side of [cmp.console, cmp.native]) {
    if (!side.frames) continue;
    console.log(`\n  ${side.side.toUpperCase()} frames (${side.source})`);
    for (const n of side.notes) console.log(`    note: ${n}`);
    const keys = [...new Set(side.frames.flatMap((f) => Object.keys(f.values)))]
      .filter((k) => !k.startsWith('p') && cmp.metrics.some((id) => k.startsWith(`${id}.`)))
      .sort((a, b) => {
        const ia = cmp.metrics.findIndex((id) => a.startsWith(`${id}.`));
        const ib = cmp.metrics.findIndex((id) => b.startsWith(`${id}.`));
        return ia !== ib ? ia - ib : a.localeCompare(b);
      });
    console.log(`    ${'t(ms)'.padStart(8)} ${'p'.padStart(5)}  ${keys.map((k) => k.split('.')[1].slice(0, 8).padStart(8)).join(' ')}`);
    for (const f of side.frames) {
      if (!Number.isFinite(f.t)) continue;
      console.log(`    ${fmt(f.t, '', 8)} ${fmt(f.p, '', 5)}  ${keys.map((k) => fmt(f.values[k], '', 8)).join(' ')}`);
    }
  }
}

function headlineFilter(cmp: Comparison, key: string): boolean {
  return cmp.headline.length > 0 && !cmp.headline.includes(key);
}

function printCompare(cmp: Comparison): void {
  console.log(`\n=== ${cmp.id}: ${cmp.label}`);
  if (cmp.notes.length) for (const n of cmp.notes) console.log(`  note: ${n}`);
  if (cmp.verdict === 'NATIVE-ONLY') {
    console.log(`  capture: native only (${fmt(cmp.duration.nativeMs, ' ms')} measured), no console sequence on disk`);
    console.log(`  score: native ${cmp.scores.native}`);
    return;
  }
  console.log(`  duration: console ${fmt(cmp.duration.consoleMs, ' ms')}   native ${fmt(cmp.duration.nativeMs, ' ms')}   delta ${fmt(cmp.duration.nativeMs - cmp.duration.consoleMs, ' ms')}`);
  console.log(`  best alignment offset: native ${fmt(cmp.bestOffsetMs, ' ms')} vs console (spread ${fmt(cmp.lagSpreadMs, ' ms')})`);
  console.log(`  score: console ${cmp.scores.console} / native ${cmp.scores.native}`);
  const nz = Object.entries(cmp.noise).filter(([k]) => !cmp.headline.length || cmp.headline.includes(k)).slice(0, 8);
  if (nz.length) console.log(`  console noise (settled-tail sigma): ${nz.map(([k, v]) => `${k}=${v.toFixed(2)}`).join(' ')}`);
  console.log('');
  const endpoint = !Number.isFinite(cmp.duration.consoleMs) && cmp.grid.length === 1;
  if (endpoint) {
    console.log(`  ${'metric'.padEnd(22)} ${'console'.padStart(9)} ${'native'.padStart(9)} ${'delta'.padStart(8)}  band  verdict`);
    for (const [metric, d] of Object.entries(cmp.grid[0].deltas)) {
      if (cmp.headline.length && !cmp.headline.includes(metric)) continue;
      const band = cmp.tolerance[metric];
      console.log(`  ${metric.padEnd(22)} ${fmt(valueAtP(cmp.console, metric, 1), '', 9)} ${fmt(valueAtP(cmp.native, metric, 1), '', 9)} ${fmt(d, '', 8)}  ${band ? `+-${band.pass}/${band.warn}` : '-'} ${cmp.verdicts[metric] ?? 'n/a'}`);
    }
    console.log(`  TRANSITION VERDICT: ${cmp.verdict}`);
    return;
  }
  console.log(`  ${'p'.padStart(5)} ${'t_con'.padStart(7)} ${'t_nat'.padStart(7)}  ${'metric'.padEnd(22)} ${'console'.padStart(9)} ${'native'.padStart(9)} ${'delta'.padStart(8)}  band`);
  const headline = cmp.headline;
  for (const g of cmp.grid) {
    const keys = Object.keys(g.deltas).filter((k) => !headline || headline.includes(k));
    for (const metric of keys) {
      const band = cmp.tolerance[metric];
      const flag = g.flags[metric];
      if (flag === 'n/a') continue;
      const a = valueAtP(cmp.console, metric, g.p), b = valueAtP(cmp.native, metric, g.p);
      console.log(`  ${fmt(g.p, '', 5)} ${fmt(g.tConsole, '', 7)} ${fmt(g.tNative, '', 7)}  ${metric.padEnd(22)} ${fmt(a, '', 9)} ${fmt(b, '', 9)} ${fmt(g.deltas[metric], '', 8)}  ${band ? `+-${band.pass}/${band.warn}` : '-'} ${flag}`);
    }
  }
  console.log('');
  console.log(`  ${'metric'.padEnd(24)} ${'max|delta|'.padStart(10)}  ${'pass'.padStart(6)} ${'warn'.padStart(6)}  verdict`);
  for (const [metric, v] of Object.entries(cmp.maxDelta)) {
    const band = cmp.tolerance[metric];
    console.log(`  ${metric.padEnd(24)} ${fmt(v, '', 10)}  ${band ? fmt(band.pass, '', 6) : 'n/a'.padStart(6)} ${band ? fmt(band.warn, '', 6) : 'n/a'.padStart(6)}  ${cmp.verdicts[metric] ?? 'n/a'}`);
  }
  console.log(`  TRANSITION VERDICT: ${cmp.verdict}`);
}

// ------------------------------------------------------------ capture ----

function captureNative(t: Transition, o: Options, probes: ProbeFile): { ok: boolean; message: string } {
  if (!t.capture) return { ok: false, message: 'no capture recipe' };
  if (t.capture.skip) return { ok: false, message: `skipped: ${t.capture.skip}` };
  const dir = t.native ? resolve(t.native.dir) : join(probes.outDir, 'native', t.id);
  mkdirSync(dir, { recursive: true });
  const wantsBurst = t.capture.tokens.some((tok) => tok.startsWith('burst='));
  const existing = readdirSync(dir).filter((f) => f.startsWith(`${t.id}_`) && f.endsWith('.png'));
  if (existing.length && !o.force) return { ok: true, message: `${existing.length} frames already present (use --force)` };
  for (const f of readdirSync(dir)) if (f.startsWith(t.id)) try { rmSync(join(dir, f), { force: true }); } catch { /* ignore */ }
  const dataDir = join(dir, 'data');
  mkdirSync(dataDir, { recursive: true });
  mkdirSync(join(dir, 'empty-steam'), { recursive: true });
  const env = { ...process.env, XMB_STEAM_ROOT: join(dir, 'empty-steam'), XMB_INPUT_SCRIPT_ONLY: '1' };
  // The burst's first saved frame lands anywhere from 25 to 160 ms after the key
  // (grabFramebuffer costs a frame or three, and the cost varies per run), so a
  // single attempt can miss most of a 200 ms move. Run the recipe several times
  // and keep the attempt whose first frame is earliest: that is a coverage
  // choice about the instrument, not a selection on the motion.
  const attempts = wantsBurst ? Math.max(1, o.attempts) : 1;
  let best: { dir: string; firstMs: number; frames: number; log: string } | null = null;
  for (let i = 1; i <= attempts; i++) {
    const runDir = attempts > 1 ? join(dir, `attempt-${i}`) : dir;
    mkdirSync(runDir, { recursive: true });
    const prefix = join(runDir, t.id).split('\\').join('/');
    const tokens = t.capture.tokens.map((tok) => tok.split('<prefix>').join(prefix));
    const res = spawnSync(o.exe, [
      '--dev-flash', DEV_FLASH, '--data-dir', dataDir,
      '--render-clock', '30', '--background-clock', '2026-10-28T10:00:00',
      '--ui-clock', '2026-10-25T12:32:00', '--focus-time', '2000',
      '--script', tokens.join(','),
    ], { env, encoding: 'utf8', timeout: 180000, maxBuffer: 32 * 1024 * 1024 });
    const log = `${res.stdout ?? ''}\n${res.stderr ?? ''}`;
    const frames = readdirSync(runDir).filter((f) => f.startsWith(`${t.id}_`) && f.endsWith('.png'));
    const times = frames.map((f) => Number((/_(\d+)ms\.png$/.exec(f) ?? [])[1])).filter((v) => Number.isFinite(v));
    const firstMs = times.length ? Math.min(...times) : Infinity;
    if (!best || firstMs < best.firstMs) best = { dir: runDir, firstMs, frames: frames.length, log };
  }
  if (!best) return { ok: false, message: 'capture produced nothing' };
  writeFileSync(join(dir, 'capture.log'), best.log);
  // the chosen attempt's files become the sequence; the others stay as evidence
  const produced = readdirSync(best.dir).filter((f) => f.startsWith(t.id));
  for (const f of produced) copyFileSync(join(best.dir, f), join(dir, f));
  const frames = readdirSync(dir).filter((f) => f.startsWith(`${t.id}_`) && f.endsWith('.png'));
  let stateNote = '';
  const grabs = readdirSync(dir).filter((f) => f.endsWith('.png.json') && f.startsWith(`${t.id}`))
    .map((f) => ({ f, m: statSync(join(dir, f)).mtimeMs })).sort((a, b) => a.m - b.m).map((e) => e.f);
  if (t.capture.expect && grabs.length) {
    const last = grabs[grabs.length - 1];
    try {
      const st = JSON.parse(readFileSync(join(dir, last), 'utf8')) as Record<string, unknown>;
      const bad: string[] = [];
      for (const [k, v] of Object.entries(t.capture.expect)) if (String(st[k]) !== String(v)) bad.push(`${k}=${String(st[k])} (want ${v})`);
      stateNote = bad.length ? `state mismatch: ${bad.join(', ')}` : 'end state as expected';
    } catch { stateNote = 'state sidecar unreadable'; }
  }
  if (wantsBurst && !frames.length) return { ok: false, message: `no burst frames (${stateNote || 'see capture.log'})` };
  if (!wantsBurst) {
    const endpointFile = t.endpoint?.native?.file;
    if (endpointFile && !existsSync(endpointFile)) return { ok: false, message: `endpoint grab missing: ${endpointFile} (${stateNote})` };
    return { ok: true, message: `endpoint grab, ${stateNote || 'no state check'}` };
  }
  return { ok: true, message: `${frames.length} frames from ${attempts} attempt(s), first at ${best.firstMs} ms, ${stateNote || 'no state check'}` };
}


// ------------------------------------------------------------- render ----

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(bytes: Uint8Array): number {
  let c = 0xffffffff;
  for (const b of bytes) c = crcTable[(c ^ b) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Buffer {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), Buffer.from(data)]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

// Minimal 8-bit RGB PNG writer (filter 0), enough for the render tiles.
function writePng(path: string, w: number, h: number, rgb: Uint8Array): void {
  const stride = w * 3;
  const raw = Buffer.alloc((stride + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (stride + 1)] = 0;
    Buffer.from(rgb.buffer, rgb.byteOffset + y * stride, stride).copy(raw, y * (stride + 1) + 1);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 6 })),
    chunk('IEND', new Uint8Array(0)),
  ]);
  writeFileSync(path, png);
}

// Crop the view rect out of the frame, letterboxed into a fixed tile, so the
// console band and the native full frame sit at the same scale.
function tileOf(img: PngImage, crop: Rect, view: Rect, tw: number, th: number): Uint8Array {
  const out = new Uint8Array(tw * th * 3);
  const vx0 = Math.max(view[0], crop[0]), vy0 = Math.max(view[1], crop[1]);
  const vx1 = Math.min(view[0] + view[2], crop[0] + crop[2]);
  const vy1 = Math.min(view[1] + view[3], crop[1] + crop[3]);
  out.fill(18);
  if (vx1 <= vx0 || vy1 <= vy0) return out;
  const vw = vx1 - vx0, vh = vy1 - vy0;
  const scale = Math.min(tw / view[2], th / view[3]);
  const dw = Math.round(vw * scale), dh = Math.round(vh * scale);
  const ox = Math.round((tw - dw) / 2), oy = Math.round((th - dh) / 2);
  for (let y = 0; y < dh; y++) {
    const sy = vy0 + Math.min(vh - 1, Math.floor(y / scale));
    for (let x = 0; x < dw; x++) {
      const sx = vx0 + Math.min(vw - 1, Math.floor(x / scale));
      const s = ((sy - crop[1]) * img.w + (sx - crop[0])) * 4;
      const d = ((oy + y) * tw + ox + x) * 3;
      out[d] = img.rgba[s];
      out[d + 1] = img.rgba[s + 1];
      out[d + 2] = img.rgba[s + 2];
    }
  }
  return out;
}

function renderRecordings(t: Transition, cmp: Comparison, o: Options, probes: ProbeFile): string[] {
  const outDir = probes.outDir;
  mkdirSync(outDir, { recursive: true });
  const view: Rect = t.view ?? [0, 180, 1920, 700];
  const tw = 640;
  const th = Math.max(120, Math.min(420, Math.round((tw * view[3]) / view[2])));
  const sides: { name: 'console' | 'native'; side: SideMeasure }[] = [];
  if (cmp.console.frames) sides.push({ name: 'console', side: cmp.console });
  if (cmp.native.frames) sides.push({ name: 'native', side: cmp.native });
  const written: string[] = [];
  const tileDir = join(outDir, '.tiles', t.id);
  mkdirSync(tileDir, { recursive: true });
  const N = t.mode === 'endpoint' ? 1 : o.gifFrames;
  const manifest: { index: number; tiles: Record<string, string>; labels: Record<string, string> }[] = [];
  for (let i = 0; i <= N; i++) {
    const p = i / N;
    const entry: { index: number; tiles: Record<string, string>; labels: Record<string, string> } = { index: i, tiles: {}, labels: {} };
    for (const { name, side } of sides) {
      const tAt = timeAtP(side, p);
      const frame = nearestFrame(side, p);
      if (!frame) continue;
      const img = loadImage(frame.frame.path);
      const rgb = tileOf(img, side.crop, view, tw, th);
      const file = join(tileDir, `${name}-${String(i).padStart(3, '0')}.png`);
      writePng(file, tw, th, rgb);
      entry.tiles[name] = file;
      entry.labels[name] = `${frame.frame.name}  p=${p.toFixed(2)}  t=${Number.isFinite(tAt) ? tAt.toFixed(0) : '?'} ms`;
    }
    manifest.push(entry);
  }
  const manifestPath = join(tileDir, 'manifest.json');
  writeFileSync(manifestPath, JSON.stringify({
    id: t.id, label: t.label, mode: t.mode ?? 'sequence', view, tile: [tw, th], frames: manifest,
    duration: cmp.duration, verdict: cmp.verdict,
    scores: { console: cmp.scores.console, native: cmp.scores.native },
    sources: { console: cmp.console.source ?? '', native: cmp.native.source ?? '' },
  }, null, 1));
  const res = spawnSync(PYTHON, [join(ROOT, 'xmb', 'work', 'transition-render.py'), manifestPath, outDir], { encoding: 'utf8', timeout: 300000 });
  if (res.status !== 0) {
    console.error(`  render failed: ${(res.stderr ?? '').trim().split('\n').slice(-3).join(' | ')}`);
    return written;
  }
  for (const line of (res.stdout ?? '').trim().split('\n')) if (line.trim()) written.push(line.trim());
  return written;
}

function nearestFrame(side: SideMeasure, p: number): FrameRecord | null {
  let best: FrameRecord | null = null;
  let bd = Infinity;
  for (const f of side.frames) {
    if (!Number.isFinite(f.p)) continue;
    const d = Math.abs(f.p - p);
    if (d < bd) { bd = d; best = f; }
  }
  return best;
}

// ---------------------------------------------------------------- main ----

function main(): void {
  const o = parseArgs(process.argv.slice(2));
  const probes = loadProbes(o.probes);
  if (o.out) probes.outDir = o.out;
  const ids = o.transitions.includes('all') ? probes.transitions.map((t) => t.id) : o.transitions;
  const chosen = probes.transitions.filter((t) => ids.includes(t.id));
  if (!chosen.length) fail(`no transition matched ${ids.join(', ')}`);

  if (o.list) {
    for (const t of probes.transitions) {
      const sides = [t.console ? 'console' : '', t.native ? 'native' : ''].filter(Boolean).join('+');
      console.log(`${t.id.padEnd(16)} ${sides.padEnd(16)} ${t.probes.length} probes  ${t.label}`);
      if (t.notes) for (const n of t.notes) console.log(`${' '.repeat(18)}${n}`);
      if (t.capture?.skip) console.log(`${' '.repeat(18)}capture: skipped - ${t.capture.skip}`);
      if (t.mode === 'endpoint') console.log(`${' '.repeat(18)}endpoint mode: ${t.endpoint?.console ? 'console still vs native grab' : 'native only, no console still'}`);
      else if (!t.console) console.log(`${' '.repeat(18)}no console sequence: single-side measurement`);
    }
    return;
  }

  if (o.capture) {
    console.log('== capture');
    for (const t of chosen) {
      const r = captureNative(t, o, probes);
      console.log(`  ${t.id.padEnd(14)} ${r.ok ? 'ok  ' : 'FAIL'} ${r.message}`);
    }
  }

  const results: Comparison[] = [];
  let json: Record<string, unknown> = {};
  for (const t of chosen) {
    let cm: SideMeasure | null = null, nm: SideMeasure | null = null;
    if (t.mode === 'endpoint') {
      if (t.endpoint?.console) cm = measureEndpoint('console', t.endpoint.console, t.probes);
      if (t.endpoint?.native) nm = measureEndpoint('native', t.endpoint.native, t.probes);
    } else {
      if (t.console) cm = measure('console', t.console, t.probes, t.driver);
      if (t.native) {
        try {
          nm = measure('native', t.native, t.probes, t.driver);
        } catch (error) {
          console.error(`  ${t.id}: native sequence unavailable (${error instanceof Error ? error.message : String(error)})`);
          nm = null;
        }
      }
    }
    const cmp = compare(t, probes, cm, nm, o.grid);
    results.push(cmp);
    if (o.measure) printMeasure(cmp);
    if (o.compare) printCompare(cmp);
    if (o.render) {
      const files = renderRecordings(t, cmp, o, probes);
      for (const f of files) console.log(`  recorded: ${f}`);
    }
  }

  if (o.compare) {
    console.log('\n== summary');
    console.log(`  ${'transition'.padEnd(15)} ${'console ms'.padStart(11)} ${'native ms'.padStart(10)} ${'max|d|'.padStart(8)} ${'offset'.padStart(8)}  verdict`);
    for (const c of results) {
      const headVals = Object.entries(c.maxDelta).filter(([k]) => !c.headline.length || c.headline.includes(k)).map(([, v]) => v);
      const maxd = headVals.length ? Math.max(...headVals) : (c.headline.length ? NaN : Math.max(0, ...Object.values(c.maxDelta)));
      console.log(`  ${c.id.padEnd(15)} ${fmt(c.duration.consoleMs, '', 11)} ${fmt(c.duration.nativeMs, '', 10)} ${fmt(maxd, '', 8)} ${fmt(c.bestOffsetMs, '', 8)}  ${c.verdict}`);
    }
  }

  if (o.json) {
    json = {
      generated: new Date().toISOString(),
      probes: o.probes,
      exe: o.exe,
      transitions: results.map((c) => ({
        id: c.id, label: c.label, verdict: c.verdict, headline: c.headline,
        duration: c.duration, bestOffsetMs: c.bestOffsetMs, lagSpreadMs: c.lagSpreadMs,
        scores: c.scores, maxDelta: c.maxDelta, verdicts: c.verdicts,
        grid: c.grid.map((g) => ({ p: g.p, tConsole: g.tConsole, tNative: g.tNative, ...g.deltas })),
        console: c.console.frames ? { source: c.console.source, notes: c.console.notes, frames: c.console.frames.map((f) => ({ t: f.t, p: f.p, file: f.frame.name })) } : null,
        native: c.native.frames ? { source: c.native.source, notes: c.native.notes, frames: c.native.frames.map((f) => ({ t: f.t, p: f.p, file: f.frame.name })) } : null,
      })),
    };
    writeFileSync(o.json, JSON.stringify(json, null, 2));
    console.log(`\nwrote ${o.json}`);
  }
}

main();
