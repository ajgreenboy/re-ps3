import { decodePng } from './ab-compare';

// Frame-space proxy, not isolated ribbon energy: horizontal median rejects tiny
// particle glints but not broad particles or a switch between bright folds.
// The windows intentionally avoid the root page's item text and icons.
const columns = [0, 300, 400, 1520, 1700, 1900];
for (const path of process.argv.slice(2)) {
  const image = decodePng(path);
  if (image.w !== 1920 || image.h !== 1080) throw new Error('expected a 1920x1080 capture');
  const luma = (x: number, y: number) => {
    const i = (y * image.w + x) * 4;
    return .2126 * image.rgba[i] + .7152 * image.rgba[i + 1] + .0722 * image.rgba[i + 2];
  };
  const peaks = columns.map(x => {
    let peak = -Infinity, crestY = 0;
    for (let y = 330; y < 860; ++y) {
      const row = Array.from({ length: 9 }, (_, i) => luma(Math.max(0, Math.min(1919, x + i - 4)), y));
      row.sort((a, b) => a - b);
      if (row[4] > peak) { peak = row[4]; crestY = y; }
    }
    return { x, y: crestY, luma: Number(peak.toFixed(2)) };
  });
  console.log(JSON.stringify({ path, peaks }));
}
