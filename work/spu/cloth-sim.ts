// Model of custom_render_plugin's ribbon cloth (FUN_0004e624 init, FUN_0004bed0 step, FUN_0004bce0 edge driver,
// FUN_0004f814 per-frame interpolation and y/z swell) checked against the lattices the PPU hands spline.elf
// (RPCS3 XMB_SPU_DUMP log: the 5776-byte gets from 0x20401f00, one per frame from boot).
// Usage: bun cloth-sim.ts <prx> <dump dir> [frames]
import { readFileSync } from "node:fs";
const [prxPath, dir, framesArg] = process.argv.slice(2);
const elf = readFileSync(prxPath);
const phoff = Number(elf.readBigUInt64BE(0x20)), phentsize = elf.readUInt16BE(0x36), phnum = elf.readUInt16BE(0x38);
const segs = [...Array(phnum)].map((_, i) => { const o = phoff + i * phentsize; return { type: elf.readUInt32BE(o), offset: Number(elf.readBigUInt64BE(o + 8)), vaddr: Number(elf.readBigUInt64BE(o + 16)), filesz: Number(elf.readBigUInt64BE(o + 32)) }; });
const at = (va: number) => { const s = segs.find(s => s.type === 1 && va >= s.vaddr && va < s.vaddr + s.filesz)!; return s.offset + va - s.vaddr; };
const table = (va: number, i: number) => [0, 1, 2].map(k => elf.readFloatBE(at(va + (i * 3 + k) * 4)));

export type Params = { damping: number, length: number, tension: number, timestep: number, perturbation: number, endY: number, endZ: number, ffdParam: number };
const f = Math.fround;
export class Cloth {
  cols = 19; rows = 19; n = 361;
  pos: number[][]; prev: number[][]; vel: number[][];
  target = 0; phase = 0; acc = 0; seed = 0;
  constructor(preset: 0 | 1 | 2) {
    const [p, v] = ([[0x95a40, 0x96b2c], [0x97c18, 0x98d04], [0x99df0, 0x9aedc]] as const)[preset];
    this.pos = [...Array(this.n)].map((_, i) => [...table(p, i), 1]);
    this.vel = [...Array(this.n)].map((_, i) => [...table(v, i), 0]);
    this.prev = this.pos.map(q => [...q]);
  }
  random() { this.seed = (this.seed + 1) | 0; let n = ((this.seed << 13) ^ this.seed) | 0;
    n = (Math.imul(n, (Math.imul(Math.imul(n, n), 15731) + 789221) | 0) + 1376312589) | 0;
    return f(1 - f(n & 0x7fffffff) * f(9.313225746154785e-10)); }
  spring(i: number, j: number, rest: number, k: number) {
    const d = [0, 1, 2].map(c => this.pos[j][c] - this.pos[i][c]), len = Math.hypot(...d);
    const s = (len - rest) * k / len;
    for (let c = 0; c < 3; c++) { this.vel[i][c] += d[c] * s; this.vel[j][c] -= d[c] * s; }
  }
  step(p: Params) {
    this.prev = this.pos.map(q => [...q]);
    this.target += p.timestep * 1e-4; if (this.target > 10) this.target = 0;
    const k = p.tension, kb = p.tension * 10;
    for (let r = 0; r < this.rows; r++) for (let c = 0; c < this.cols; c++) {
      const i = r * this.cols + c;
      if (c >= 1) this.spring(i, i - 1, p.length, k);
      if (c >= 2) this.spring(i, i - 2, p.length * 2, kb);
      if (r >= 1) this.spring(i, i - this.cols, p.length, k);
      if (r >= 2) this.spring(i, i - 2 * this.cols, p.length * 2, kb);
      const rx = this.random(), ry = this.random(), rz = this.random();
      this.vel[i][0] += p.perturbation * rx; this.vel[i][1] += p.perturbation * ry; this.vel[i][2] += p.perturbation * rz;
    }
    const dt = p.timestep * 1e-4, damp = p.damping * p.timestep;
    for (let i = 0; i < this.n; i++) for (let c = 0; c < 3; c++) { this.pos[i][c] += this.vel[i][c] * dt; this.vel[i][c] -= this.vel[i][c] * damp; }
    this.phase += (this.target - this.phase) * 0.1;
    for (let r = 0; r < this.rows; r++) {
      const t = r / this.rows + this.phase;
      this.vel[r * this.cols][0] += 0.02 * p.timestep;
      const e = (r + 1) * this.cols - 1;
      this.pos[e] = [0, (Math.sin(11 * t) * 0.5 + 0.5) * p.endY, (Math.cos(15 * t) + 1) * 0.5 * p.endZ, 1];
      this.vel[e] = [0, 0, 0, 0];
    }
  }
  // FUN_0004f814: advance by dt seconds, return the lattice handed to the SPU.
  frame(dt: number, p: Params) {
    this.acc += dt * 60;
    while (this.acc > 1) { this.step(p); this.acc -= 1; }
    return this.pos.map((q, i) => {
      const l = [0, 1, 2].map(c => this.prev[i][c] * (1 - this.acc) + q[c] * this.acc);
      const d = l[0] - p.ffdParam, s = Math.min(1, Math.max(0, d / 5)), fall = d < 0 ? 1 : Math.cos(d * Math.PI / 2), keep = (1 - s) * (1 - s) * (1 + 2 * s);
      const scale = 1.3 - (d < 0 ? 1 : fall) * keep;
      return [l[0], l[1] * scale, l[2] * scale, 1];
    });
  }
}

if (import.meta.main) {
  const coldboot1: Params = { damping: 0.0001, length: 0.306001, tension: 0.25, timestep: +(process.env.TS ?? 2), perturbation: +(process.env.PERT ?? 0), endY: 0.4, endZ: 0.2, ffdParam: +(process.env.FFDP ?? -2) };
  const gets = readFileSync(`${dir}/dma.txt`, "utf8").split("\n").filter(l => l.includes("ea 0x20401f00 size 5776")).map(l => l.split(" ")[0].padStart(5, "0"));
  const frames = +(framesArg ?? 12);
  const cloth = new Cloth(1);
  for (let k = 0; k < frames && k < gets.length; k++) {
    const lat = cloth.frame(+(process.env.DT ?? 1 / 60), coldboot1);
    const cap = readFileSync(`${dir}/dma_${gets[k]}_get.bin`);
    let worst = 0, wi = 0, wx = 0;
    for (let i = 0; i < 361; i++) for (let c = 0; c < 3; c++) { const e = Math.abs(cap.readFloatBE((i * 4 + c) * 4) - lat[i][c]); if (c === 0) wx = Math.max(wx, e); if (e > worst) { worst = e; wi = i; } }
    console.log(`frame ${k} (get ${gets[k]}): worst ${worst.toExponential(2)} at point ${wi}, x only ${wx.toExponential(2)}`);
  }
}
