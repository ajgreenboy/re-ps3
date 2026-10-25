// Reads coldboot.raf's plane.edge (Edge geometry, one segment): vertex/index counts at segment +8/+10, positions
// (float3 BE) at the offset in segment +0x18, uvs (half2 BE) at segment +0x2c; checks the uv against the position.
// Usage: bun coldboot-edge-plane.ts <plane.edge>
const u = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer()); const dv = new DataView(u.buffer);
const half = (h: number) => { const s = h >> 15 ? -1 : 1, e = (h >> 10) & 31, m = h & 1023; return e ? s * 2 ** (e - 15) * (1 + m / 1024) : s * 2 ** -14 * (m / 1024); };
const seg = dv.getUint32(4), count = dv.getUint16(seg + 8), indices = dv.getUint16(seg + 10);
const pos = dv.getUint32(seg + 0x18), uv = dv.getUint32(seg + 0x2c);
console.log({ seg, count, indices, pos: pos.toString(16), uv: uv.toString(16) });
for (let i = 0; i < count; i++) {
  const p = [0, 1, 2].map(k => dv.getFloat32(pos + i * 12 + k * 4)), t = [0, 1].map(k => half(dv.getUint16(uv + i * 4 + k * 2)));
  console.log(i, p.map(v => v.toFixed(3)).join(","), "uv", t.map(v => v.toFixed(3)).join(","), "expect", ((p[0] + 1) / 2).toFixed(3), ((1 - p[2]) / 2).toFixed(3));
}
