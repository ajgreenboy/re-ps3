import { readFileSync } from "node:fs";
const lines = readFileSync(process.argv[2],"utf8").split(/\r?\n/);
// section map
const sec = {};
const secRe = /^; ---- (\S+)/;
let cur = "?";
const funcs = [];
let curF = null;
for (let i=0;i<lines.length;i++){
  const L = lines[i];
  let m = L.match(secRe); if (m){ cur = m[1]; continue; }
  m = L.match(/^func_([0-9a-f]+):/);
  if (m){ curF = { name:"func_"+m[1], addr:parseInt(m[1],16), sec:cur, insns:0, callees:new Set(), calls:0, dma:0, name2:0 }; funcs.push(curF); continue; }
  m = L.match(/^\s+([0-9a-f]{5,6}): ([0-9a-f]{8})  (\S+)(.*)$/);
  if (m && curF){
    curF.insns++;
    const op = m[3];
    if (op==="brsl"||op==="brasl"){ const t=m[4].match(/[0-9a-fx]+/i); /* target printed decimal */ const num=parseInt(m[4].trim().replace(/[^0-9]/g,""),10); if(!isNaN(num)) curF.callees.add("0x"+num.toString(16)); curF.calls++; }
    if (op.startsWith("get")||op.startsWith("put")){ curF.dma++; }
    if (op==="wrch"||op==="rdch"){ curF.name2++; }
  }
}
console.log("total funcs:", funcs.length);
for (const f of funcs){
  if (f.calls>0 || f.dma>0 || f.insns>200){
    console.log(`${f.name} @0x${f.addr.toString(16)} [${f.sec}] insns=${f.insns} calls=${f.calls} dma=${f.dma} ch=${f.name2} -> ${[...f.callees].join(",")}`);
  }
}
