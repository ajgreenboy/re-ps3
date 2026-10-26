const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
const u16=(p)=>b.readUInt16BE(p),u32=(p)=>b.readUInt32BE(p),u64=(p)=>Number(b.readBigUInt64BE(p));
const phnum=u16(0x38);const segs=[];
for(let i=0;i<phnum;i++){const p=u64(0x20)+i*56;if(u32(p)!==1)continue;segs.push({off:u64(p+8),vaddr:u64(p+16),filesz:u64(p+32)});}
const f=(va)=>{for(const s of segs)if(va>=s.vaddr&&va<s.vaddr+s.filesz)return s.off+(va-s.vaddr);return -1;};
for(let va=0x6eebd0; va<0x6eec20; va+=4){const o=f(va);console.log('0x'+va.toString(16), o<0?'unmapped':b.readFloatBE(o));}
