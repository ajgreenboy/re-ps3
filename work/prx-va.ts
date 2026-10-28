import {readFileSync} from "node:fs";
const p=process.argv[2];
const f=readFileSync(p);
const is64=f[4]===2;
const phoff=is64?Number(f.readBigUInt64BE(0x20)):f.readUInt32BE(0x1c);
const phentsize=is64?f.readUInt16BE(0x36):f.readUInt16BE(0x2a);
const phnum=is64?f.readUInt16BE(0x38):f.readUInt16BE(0x2c);
console.log(p,"class",is64?"64":"32","phoff",phoff,"num",phnum,"entsize",phentsize);
const segs=[];
for(let i=0;i<phnum;i++){
  const o=phoff+i*phentsize;
  let type,off,vaddr,paddr,filesz,memsz;
  if(is64){type=f.readUInt32BE(o);off=Number(f.readBigUInt64BE(o+8));vaddr=Number(f.readBigUInt64BE(o+16));filesz=Number(f.readBigUInt64BE(o+32));memsz=Number(f.readBigUInt64BE(o+40));}
  else {type=f.readUInt32BE(o);off=f.readUInt32BE(o+4);vaddr=f.readUInt32BE(o+8);filesz=f.readUInt32BE(o+16);memsz=f.readUInt32BE(o+20);}
  segs.push({type,off,vaddr,filesz,memsz});
  console.log(i,"type",type,"off 0x"+off.toString(16),"vaddr 0x"+vaddr.toString(16),"filesz 0x"+filesz.toString(16),"memsz 0x"+memsz.toString(16));
}
function va2off(va){for(const s of segs){if(s.type===1&&va>=s.vaddr&&va<s.vaddr+s.filesz)return s.off+(va-s.vaddr);}return -1;}
for(const va of [0x2709c0,0x2709d0,0x2709e0,0x2709e4,0x2709e8,0x2709f0,0x2709d8]){
  const off=va2off(va);
  console.log("va 0x"+va.toString(16),"off",off>=0?"0x"+off.toString(16):"unmapped", off>=0? f.readFloatBE(off)+" / "+f.readDoubleBE(off):"");
}
