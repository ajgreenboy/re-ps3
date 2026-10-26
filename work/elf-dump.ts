import { readFileSync } from "node:fs";
const b = readFileSync(process.argv[2]);
console.log("e_entry=0x"+b.readUInt32BE(24).toString(16), "e_phoff=0x"+b.readUInt32BE(28).toString(16), "e_shoff=0x"+b.readUInt32BE(32).toString(16));
// program headers
const phoff=b.readUInt32BE(28), phnum=b.readUInt16BE(44);
for (let i=0;i<phnum;i++){ const o=phoff+i*32; console.log(`PH${i} type=0x${b.readUInt32BE(o).toString(16)} off=0x${b.readUInt32BE(o+4).toString(16)} vaddr=0x${b.readUInt32BE(o+8).toString(16)} filesz=0x${b.readUInt32BE(o+16).toString(16)} memsz=0x${b.readUInt32BE(o+20).toString(16)} flags=0x${b.readUInt32BE(o+24).toString(16)}`);}
console.log("\n=== .data @0x8020 (16 quads) ===");
for (let i=0;i<8;i++){ const o=0x8020+i*16; const w=[0,4,8,12].map(k=>b.readUInt32BE(o+k)); console.log(`+${(i*16).toString(16)}: [${w.map(x=>x.toString(16).padStart(8,"0")).join(" ")}] = ${[0,4,8,12].map(k=>b.readFloatBE(o+k).toPrecision(6)).join(" ")}`);}
console.log("\n=== .rodata @0x7af0 size 0x4b0 ===");
for (let i=0;i<0x4b0/16;i++){ const o=0x7af0+i*16; const w=[0,4,8,12].map(k=>b.readUInt32BE(o+k)); console.log(`0x${(0xa9f0+i*16).toString(16)}: [${w.map(x=>x.toString(16).padStart(8,"0")).join(" ")}] = ${[0,4,8,12].map(k=>b.readFloatBE(o+k).toPrecision(6)).join(" ")}`);}
console.log("\n=== .bss nonzero check ===");
let nz=0; for (let i=0x8040;i<0x8040+0x2520;i++) if(b[i]) nz++; console.log("nonzero bytes in .bss file image:", nz);
