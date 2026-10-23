import {resolve} from 'node:path';
const root=resolve(process.argv[2]??'.');
const mapping=await Bun.file(resolve(root,'out/recomp_boot493/ppc_func_mapping.cpp')).text();
const entries=new Set([...mapping.matchAll(/\{ 0x([\da-f]+),/gi)].map(m=>parseInt(m[1],16)));
const log=await Bun.file(resolve(root,'out/xenonrecomp.log')).text();
const missing=new Map<number,number[]>();
for(const m of log.matchAll(/ERROR: Switch case at ([\da-f]+) is trying to jump outside function: ([\da-f]+)/gi)){
  const branch=parseInt(m[1],16),target=parseInt(m[2],16);
  if(!entries.has(target))missing.set(target,[...(missing.get(target)??[]),branch]);
}
console.log(`outside-function switch targets without entries: ${missing.size}`);
for(const [target,branches] of missing)console.log(target.toString(16),[...new Set(branches)].map(n=>n.toString(16)).join(','));
