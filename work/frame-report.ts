import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Paint = { timestampNs: bigint; cpuNs: bigint; rebuilt: boolean; moving: boolean };
type Interval = { ms: number; moving: boolean };
type Stats = { count: number; meanMs: number | null; medianMs: number | null; p95Ms: number | null; p99Ms: number | null; maxMs: number | null; fps: number | string | null; overMedianCount: number; over25MsCount: number; slowCount: number };
const usage = 'usage: bun work/frame-report.ts <native.log> [--warmup-ms N] [--until-ms N] [--output file.json]';
const fail = (message: string): never => { throw new Error(message); };

function args(argv: string[]): { input: string; warmupMs: number; untilMs?: number; output?: string } {
  let input = '', output: string | undefined, warmupMs = 3000, untilMs: number | undefined;
  let warmupSet = false, untilSet = false;
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') { console.log(usage); process.exit(0); }
    if (arg === '--warmup-ms' || arg === '--until-ms' || arg === '--output') {
      const value = argv[++i];
      if (!value || value.startsWith('--')) fail(`missing value for ${arg}`);
      if (arg === '--output') { if (output !== undefined) fail('repeated --output'); output = value; }
      else if (arg === '--warmup-ms') { if (warmupSet) fail('repeated --warmup-ms'); warmupSet = true; warmupMs = Number(value); }
      else {
        if (untilSet) fail('repeated --until-ms');
        untilSet = true;
        untilMs = Number(value);
      }
    } else if (arg.startsWith('--')) fail(`unknown option: ${arg}`);
    else if (input) fail(`unexpected positional argument: ${arg}`);
    else input = arg;
  }
  if (!input) fail(usage);
  if (!Number.isFinite(warmupMs) || warmupMs < 0) fail('--warmup-ms must be a finite non-negative number');
  if (untilMs !== undefined && (!Number.isFinite(untilMs) || untilMs <= warmupMs)) fail('--until-ms must be finite and greater than --warmup-ms');
  return { input: resolve(input), warmupMs, ...(untilMs === undefined ? {} : { untilMs }), ...(output ? { output: resolve(output) } : {}) };
}

function quantile(sorted: number[], p: number): number | null {
  if (!sorted.length) return null;
  return sorted[Math.max(0, Math.ceil(p * sorted.length) - 1)];
}
function stats(values: number[]): Stats {
  if (!values.length) return { count: 0, meanMs: null, medianMs: null, p95Ms: null, p99Ms: null, maxMs: null, fps: null, overMedianCount: 0, over25MsCount: 0, slowCount: 0 };
  const sorted = [...values].sort((a, b) => a - b);
  const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
  const median = quantile(sorted, 0.5)!;
  const overMedianCount = values.filter(value => value > 1.5 * median).length;
  const over25MsCount = values.filter(value => value > 25).length;
  return { count: values.length, meanMs: mean, medianMs: median, p95Ms: quantile(sorted, 0.95), p99Ms: quantile(sorted, 0.99),
    maxMs: sorted[sorted.length - 1], fps: mean === 0 ? 'Infinity' : 1000 / mean,
    overMedianCount, over25MsCount, slowCount: values.filter(value => value > 1.5 * median && value > 25).length };
}
function durationStats(valuesNs: bigint[]): Pick<Stats, 'count' | 'meanMs' | 'medianMs' | 'p95Ms' | 'p99Ms' | 'maxMs'> {
  if (!valuesNs.length) return { count: 0, meanMs: null, medianMs: null, p95Ms: null, p99Ms: null, maxMs: null };
  const values = valuesNs.map(value => Number(value) / 1e6).sort((a, b) => a - b);
  return { count: values.length, meanMs: values.reduce((sum, value) => sum + value, 0) / values.length,
    medianMs: quantile(values, 0.5), p95Ms: quantile(values, 0.95), p99Ms: quantile(values, 0.99), maxMs: values[values.length - 1] };
}

try {
  const options = args(process.argv.slice(2));
  let text: string;
  try { text = readFileSync(options.input, 'utf8'); } catch (error) { fail(`cannot read log: ${String(error)}`); }
  if (!text.trim()) fail('log is empty');
  const paint: Paint[] = [], swaps: bigint[] = [];
  const paintPattern = /(?:^|\s)frame paint:\s*(\d+)\s+(\d+)\s+([01])\s+([01])\s*$/;
  const swapPattern = /(?:^|\s)frame swap:\s*(\d+)\s*$/;
  for (const line of text.split(/\r?\n/)) {
    const p = line.match(paintPattern);
    if (p) { paint.push({ timestampNs: BigInt(p[1]), cpuNs: BigInt(p[2]), rebuilt: p[3] === '1', moving: p[4] === '1' }); continue; }
    const s = line.match(swapPattern);
    if (s) swaps.push(BigInt(s[1]));
  }
  if (!paint.length && !swaps.length) fail('no frame paint or frame swap samples found');
  for (let i = 1; i < paint.length; i++) if (paint[i].timestampNs < paint[i - 1].timestampNs) fail(`frame paint timestamps go backwards at sample ${i + 1}`);
  for (let i = 1; i < swaps.length; i++) if (swaps[i] < swaps[i - 1]) fail(`frame swap timestamps go backwards at sample ${i + 1}`);
  const warmupProduct = options.warmupMs * 1e6;
  const warmupNs = Number.isFinite(warmupProduct) ? BigInt(Math.ceil(warmupProduct)) : null;
  const untilProduct = options.untilMs === undefined ? undefined : options.untilMs * 1e6;
  const untilNs = untilProduct === undefined || !Number.isFinite(untilProduct) ? undefined : BigInt(Math.floor(untilProduct));
  const withinWindow = (timestamp: bigint) => warmupNs !== null && timestamp >= warmupNs && (untilNs === undefined || timestamp <= untilNs);
  const keptPaint = paint.filter(sample => withinWindow(sample.timestampNs));
  const keptSwaps = swaps.filter(withinWindow);
  if (!keptPaint.length && !keptSwaps.length) fail('no samples remain after warmup filtering');
  const paintIntervals: Interval[] = [];
  for (let i = 1; i < keptPaint.length; i++) paintIntervals.push({ ms: Number(keptPaint[i].timestampNs - keptPaint[i - 1].timestampNs) / 1e6, moving: keptPaint[i - 1].moving || keptPaint[i].moving });
  const swapIntervals: number[] = [];
  for (let i = 1; i < keptSwaps.length; i++) swapIntervals.push(Number(keptSwaps[i] - keptSwaps[i - 1]) / 1e6);
  const report = {
    input: options.input, warmupMs: options.warmupMs, untilMs: options.untilMs ?? null,
    scope: 'paint and swap callback cadence; does not measure physical display presentation',
    samples: { paint: keptPaint.length, swap: keptSwaps.length },
    paintIntervals: { all: stats(paintIntervals.map(interval => interval.ms)), moving: stats(paintIntervals.filter(interval => interval.moving).map(interval => interval.ms)) },
    swapIntervals: stats(swapIntervals),
    paintCpuMs: { all: durationStats(keptPaint.map(sample => sample.cpuNs)), moving: durationStats(keptPaint.filter(sample => sample.moving).map(sample => sample.cpuNs)), uiRebuild: durationStats(keptPaint.filter(sample => sample.rebuilt).map(sample => sample.cpuNs)) },
  };
  const output = `${JSON.stringify(report, null, 2)}\n`;
  if (options.output) writeFileSync(options.output, output);
  else process.stdout.write(output);
  if (options.output) console.log(`report: ${options.output}`);
} catch (error) {
  console.error(`${error instanceof Error ? error.message : String(error)}\n${usage}`);
  process.exit(2);
}
