const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
const u16=(p)=>b.readUInt16BE(p),u32=(p)=>b.readUInt32BE(p),u64=(p)=>Number(b.readBigUInt64BE(p));
const phnum=u16(0x38);const segs=[];
for(let i=0;i<phnum;i++){const p=u64(0x20)+i*56;if(u32(p)!==1)continue;segs.push({off:u64(p+8),vaddr:u64(p+16),filesz:u64(p+32)});}
const vaOf=(off)=>{for(const s of segs)if(off>=s.off&&off<s.off+s.filesz)return s.vaddr+(off-s.off);return -1;};
const findPtr=(va)=>{const hits=[];const p8=Buffer.alloc(8);p8.writeBigUInt64BE(BigInt(va));let at=0;while(true){const i=b.indexOf(p8,at);if(i<0)break;hits.push('8byte off='+i.toString(16)+' VA='+vaOf(i).toString(16));at=i+1;}
 const p4=Buffer.alloc(4);p4.writeUInt32BE(va);at=0;while(true){const i=b.indexOf(p4,at);if(i<0)break;hits.push('4byte off='+i.toString(16)+' VA='+vaOf(i).toString(16));at=i+1;}return hits;};
for(const va of [0x68e8b0,0x68e750,0x68e74c,0x68ee90,0x68f988]){console.log('refs to',va.toString(16),findPtr(va).slice(0,12));}
// TOC: find likely .toc section around 0x6f0000; print what is at 0x6fb268 area already known. Show strings immediately before 0x68e8b0.
const o=(()=>{for(const s of segs)if(0x68e8b0>=s.vaddr&&0x68e8b0<s.vaddr+s.filesz)return s.off+(0x68e8b0-s.vaddr);})();
console.log('bytes before:', [...b.subarray(o-32,o+16)].map(x=>x.toString(16).padStart(2,'0')).join(' '));
