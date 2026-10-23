// Lists import stub entry points in the xmbld image (one hex address per line). PS3 import stubs start
//   li r12,0 ; oris r12,r12,hi ; lwz r12,lo(r12) ; std r2,40(r1)
// Ghidra misses some (nothing branches to them until runtime), so they are fed to gen-opd-functions.ts.
// usage: bun find-import-stubs.ts <boot493 dir> <out file>
import { join } from "node:path";
const [imageDir, outPath] = process.argv.slice(2);
const img = new Uint8Array(await Bun.file(join(imageDir, "image.bin")).arrayBuffer());
const dv = new DataView(img.buffer);
const found: string[] = [];
for (let i = 0, pos = 12, n = dv.getUint32(8); i < n; i++) {
  const addr = dv.getUint32(pos), size = dv.getUint32(pos + 4), flags = dv.getUint32(pos + 8);
  if (flags & 1)
    for (let o = 0; o + 16 <= size; o += 4) {
      const w = (k: number) => dv.getUint32(pos + 12 + o + 4 * k);
      if (w(0) === 0x39800000 && (w(1) >>> 16) === 0x658c && (w(2) >>> 16) === 0x818c && w(3) === 0xf8410028)
        found.push((addr + o).toString(16).toUpperCase());
    }
  pos += 12 + size;
}
await Bun.write(outPath, found.join("\n") + "\n");
console.log(`${found.length} import stubs -> ${outPath}`);
