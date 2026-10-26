const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
const u16=(p)=>b.readUInt16BE(p),u32=(p)=>b.readUInt32BE(p),u64=(p)=>Number(b.readBigUInt64BE(p));
const phnum=u16(0x38);const segs=[];
for(let i=0;i<phnum;i++){const p=u64(0x20)+i*56;if(u32(p)!==1)continue;segs.push({off:u64(p+8),vaddr:u64(p+16),filesz:u64(p+32)});}
const f=(va)=>{for(const s of segs)if(va>=s.vaddr&&va<s.vaddr+s.filesz)return s.off+(va-s.vaddr);return -1;};
const utf=(va,len)=>{const o=f(va);if(o<0)return 'unmapped';let s='';for(let i=0;i<len;i+=2){const c=b.readUInt16BE(o+i);s+=String.fromCharCode(c);}return s;};
for(const va of [0x68e74c,0x69e74c,0x67e74c,0x68e750,0x69e750,0x68e8b0,0x68e880,0x68e8c0]) console.log(va.toString(16), JSON.stringify(utf(va,48)));
// Search for 20 26 00 00 and 00 2e 00 2e 00 2e 00 00 patterns across whole file
const find=(pat,label)=>{let at=0,n=0;const hits=[];while(n<20){const i=b.indexOf(pat,at);if(i<0)break;const va=(()=>{for(const s of segs)if(i>=s.off&&i<s.off+s.filesz)return s.vaddr+(i-s.off);return -1;})();hits.push('off='+i.toString(16)+' VA='+va.toString(16)+' ctx='+JSON.stringify(utf(va-16,48)));n++;at=i+1;}console.log('== '+label+' hits '+hits.length);console.log(hits.join('\n'));};
find(Buffer.from([0x20,0x26,0x00,0x00]),'U+2026 NUL');
find(Buffer.from([0x00,0x2e,0x00,0x2e,0x00,0x2e,0x00,0x00]),'utf16 ... NUL');
// dump 0x68e8a0-0x68e940 as utf16
const o=f(0x68e8a0);console.log('--- 68e8a0 hex');console.log([...b.subarray(o,o+160)].map(x=>x.toString(16).padStart(2,'0')).join(' '));
