// vsh-data-floats.ts - list the round-valued float32 constants in the 0x310-byte vsh.elf data block at 0x68eec0.
//   bun work/vsh-data-floats.ts
// Read-only; reads fw493/vsh_module/vsh.elf from the maintainer's checkout (C:/dev/xmb). Cited by
// native-xmb/audit/ITEM-MENU-INFO-FIRMWARE.md.
import {readFileSync} from "node:fs";
const f=readFileSync("C:/dev/xmb/fw493/vsh_module/vsh.elf");
const base=0x68eec0-0x10000;
const seen=[];
for(let off=base; off<base+0x310; off+=4){
  const u=f.readUInt32BE(off), fl=f.readFloatBE(off);
  if(Number.isFinite(fl) && Math.abs(fl)>1e-6 && Math.abs(fl)<10000 && Math.abs(fl-Math.round(fl*10)/10)<1e-6){
    seen.push("vaddr 0x"+(off+0x10000).toString(16)+" = "+fl);
  }
}
console.log(seen.join("\n"));
