// Find generated switch tail calls whose targets have no translated entry.
// These can arise when an OPD entry starts inside a larger Ghidra function.
// usage: bun scan-switch-targets.ts <generated dir> <translation log> <output>
import {join} from 'node:path';
const [generated,logFile,output]=process.argv.slice(2);
if(!output)throw new Error('usage: scan-switch-targets.ts <generated dir> <translation log> <output>');
const mapping=await Bun.file(join(generated,'ppc_func_mapping.cpp')).text();
const entries=new Set([...mapping.matchAll(/\{ 0x([\da-f]+),/gi)].map(m=>parseInt(m[1],16)));
const log=await Bun.file(logFile).text();
const missing=new Set<number>();
for(const m of log.matchAll(/ERROR: Switch case at [\da-f]+ is trying to jump outside function: ([\da-f]+)/gi)){
    const target=parseInt(m[1],16);if(!entries.has(target))missing.add(target);
}
const targets=[...missing].sort((a,b)=>a-b);
await Bun.write(output,targets.map(n=>n.toString(16).toUpperCase()).join('\n')+(targets.length?'\n':''));
console.log(`${targets.length} unresolved switch tail targets -> ${output}`);
