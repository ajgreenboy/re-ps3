// Splits an XMB_FP_SERIES capture (RPCS3 capture patch, RSXThread.cpp thread::end) into <dir>/r_NNNNN.ucode files
// for `re-ps3-native --decode-fp raf:lib/raf/sce01.fpo@<dir>` and <dir>/records.tsv with each record's vblank, host
// time, draw index in the frame, texture unit 0 (offset WxH), vertex attribute 3 (register and array), the MVP/Model
// rows and _UVTransform/_Eye. Usage: bun coldboot-series-split.ts <series.bin> <dir>
import { mkdirSync, writeFileSync } from "node:fs";
const u = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer()); const dv = new DataView(u.buffer);
const dir = process.argv[3]; mkdirSync(dir, { recursive: true });
const be = (o: number) => dv.getFloat32(o, false), le32 = (o: number) => dv.getUint32(o, true);
const rows: string[] = ["i\tvblank\ttime_us\tin_frame\ttex0\tattr3_reg\tattr3_array\tmvp\tmodel\tuv\teye\ttex1\ttex2"];
let o = 0, i = 0;
const vec = (p: number, n: number, f: (o: number) => number) => [...Array(n)].map((_, k) => +f(p + k * 4).toFixed(5)).join(",");
while (o + 24 <= u.length && le32(o) === 0x52455358) {
  const vblank = le32(o + 4), time = le32(o + 8) + le32(o + 12) * 2 ** 32, inFrame = le32(o + 16), length = le32(o + 20);
  let p = o + 24;
  writeFileSync(`${dir}/r_${String(i).padStart(5, "0")}.ucode`, u.subarray(p, p + length)); p += length;
  // transform constants are stored little-endian float4 by RPCS3 (host order)
  const lef = (q: number) => dv.getFloat32(q, true);
  const mvp = vec(p, 16, lef), model = vec(p + 64, 16, lef); p += 128;
  const uv = vec(p, 4, lef), eye = vec(p + 16, 4, lef); p += 32;
  const regs: string[] = []; for (let a = 0; a < 16; a++) regs.push(vec(p + a * 16, 4, lef)); p += 256;
  const array3 = vec(p, 4, be); p += 16;
  const tex: string[] = []; for (let t = 0; t < 4; t++) { tex.push(`${le32(p).toString(16)}:${le32(p + 4)}x${le32(p + 8)}`); p += 12; }
  rows.push([i, vblank, time, inFrame, tex[0], regs[3], array3, mvp, model, uv, eye, tex[1], tex[2]].join("\t"));
  o = p; i++;
}
writeFileSync(`${dir}/records.tsv`, rows.join("\n") + "\n");
console.log(`${i} records, ${o} of ${u.length} bytes`);
