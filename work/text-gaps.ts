// Word-gap positions along one text line: columns in [x0,x1) whose band [y0,y1) holds no pixel brighter than the
// band's local background by `threshold`, grouped into runs of at least `minGap` empty columns. Used to compare
// one string's layout between a console frame and the native (same string, so gap positions scale with size).
// Usage: bun work/text-gaps.ts <png> x0 y0 x1 y1 [threshold=60] [minGap=5]
import { decodePng } from './ab-compare';
const [path, ...nums] = process.argv.slice(2);
const [x0, y0, x1, y1, threshold = 60, minGap = 5] = nums.map(Number);
const img = decodePng(path);
const luma = (x: number, y: number) => { const i = (y * img.w + x) * 4; return 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2]; };
const ink: boolean[] = [];
for (let x = x0; x < x1; x++) {
  const bg = Math.min(luma(x, y0 - 3), luma(x, y1 + 2));
  let any = false;
  for (let y = y0; y < y1; y++) if (luma(x, y) - bg > threshold) { any = true; break; }
  ink.push(any);
}
const first = ink.indexOf(true), last = ink.lastIndexOf(true);
const gaps: string[] = [];
for (let i = first; i <= last;) {
  if (ink[i]) { i++; continue; }
  let j = i; while (j <= last && !ink[j]) j++;
  if (j - i >= minGap) gaps.push(`${x0 + i}-${x0 + j - 1}`);
  i = j;
}
console.log(`ink ${x0 + first}..${x0 + last} gaps ${gaps.length}: ${gaps.join(' ')}`);
