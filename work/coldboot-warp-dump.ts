// Prints a swizzled A8R8G8B8 texture dump (<series>.texN from the capture patch: 6 u32 LE info words - format, width,
// height, pitch, remap, mip count - then the texels) as rows of R values, deswizzled (Morton order, x bit first).
const u = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer()); const dv = new DataView(u.buffer);
const w = dv.getUint32(4, true), h = dv.getUint32(8, true);
const swz = (x: number, y: number) => { let r = 0, b = 0; for (let s = 1; s < Math.max(w, h); s <<= 1) { if (s < w) r |= (x & s ? 1 : 0) << b++; if (s < h) r |= (y & s ? 1 : 0) << b++; } return r; };
for (let y = 0; y < h; y += (process.argv[3] ? 1 : h - 1)) console.log(`row ${y}:`, [...Array(w)].map((_, x) => u[24 + swz(x, y) * 4 + 1]).join(" "));
