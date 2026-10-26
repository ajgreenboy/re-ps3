import { readFileSync } from "node:fs";
const lines = readFileSync(process.argv[2],"utf8").split(/\r?\n/);
// track ilhu/iohl per register within a 40-instruction window; output candidate 32-bit constants
let pend = new Map(); // reg -> {val, line, func}
let func = "?";
const out = [];
for (let i=0;i<lines.length;i++){
  const L = lines[i];
  let m = L.match(/^func_([0-9a-f]+):/); if (m) { func = m[1]; pend.clear(); continue; }
  m = L.match(/^\s+([0-9a-f]{5,6}): ([0-9a-f]{8})  (ilhu|iohl|ila|il) \$(\d+), (0x[0-9a-f]+|-?\d+)/);
  if (!m) continue;
  const pc = parseInt(m[1],16), op=m[3], reg=+m[4], imm=parseInt(m[5]);
  if (op==="ilhu") { pend.set(reg,{val:(imm>>>0)<<16, pc, op}); }
  else if (op==="iohl") { const p=pend.get(reg); if(p && pc-p.pc<40 && pc>p.pc){ const v=(p.val | (imm>>>0))>>>0; const f=Buffer.alloc(4); f.writeUInt32BE(v); const fv=f.readFloatBE(0); if (isFinite(fv)&&Math.abs(fv)>1e-6&&Math.abs(fv)<1e7){ out.push([func, p.pc, v, fv]); } } }
  else if (op==="ila"||op==="il") { const v=(imm>>>0)>>>0; const f=Buffer.alloc(4); f.writeUInt32BE(v); const fv=f.readFloatBE(0); }
}
const seen = new Set();
for (const [fn,pc,v,fv] of out){ const k=fn+":"+fv.toFixed(7); if(seen.has(k))continue; seen.add(k); console.log(`func_${fn} @0x${pc.toString(16)} 0x${v.toString(16).padStart(8,"0")} = ${fv.toPrecision(7)}`); }
