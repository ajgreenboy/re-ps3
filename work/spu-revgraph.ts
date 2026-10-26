import { readFileSync } from "node:fs";
const lines = readFileSync(process.argv[2],"utf8").split(/\r?\n/);
let cur=""; const funcs=new Map();
for (let i=0;i<lines.length;i++){ const L=lines[i];
  let m=L.match(/^func_([0-9a-f]+):/); if(m){ cur="0x"+m[1]; funcs.set(cur,{addr:cur,callees:new Set(),insns:0}); continue; }
  m=L.match(/^\s+([0-9a-f]{5,6}): [0-9a-f]{8}  (brsl|brasl) \$\d+, (\d+)/);
  if(m && cur){ const t="0x"+(+m[3]).toString(16); funcs.get(cur).callees.add(t); funcs.get(cur).insns++; }
}
const rev=new Map();
for(const [k,v] of funcs) for(const c of v.callees){ if(!rev.has(c))rev.set(c,[]); rev.get(c).push(k); }
for(const t of process.argv.slice(3)){ console.log(`${t}: called by ${(rev.get(t)||[]).join(", ")}`); }
