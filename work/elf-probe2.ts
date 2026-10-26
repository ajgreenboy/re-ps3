const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
const u32=(p)=>b.readUInt32BE(p), u64=(p)=>Number(b.readBigUInt64BE(p));
const phoff=u64(0x20), phes=u16(0x36)==null?56:56;
function u16(p){return b.readUInt16BE(p);}
const segs=[];const phnum=u16(0x38);
for(let i=0;i<phnum;i++){const p=phoff+i*56;const t=u32(p);if(t!==1)continue;segs.push({off:u64(p+8),vaddr:u64(p+16),filesz:u64(p+32)});}
const f=(va)=>{for(const s of segs)if(va>=s.vaddr&&va<s.vaddr+s.filesz)return s.off+(va-s.vaddr);return -1;};
// validate base: PhLabelText pointer at 0x6fb254
for(const va of [0x6fb254,0x6fb214,0x6fb21c,0x6fb258]){
  const o=f(va); const p=u32(o); const po=f(p);
  console.log('ptr@'+va.toString(16),'->',p.toString(16),'mapped',po>=0, po>=0?JSON.stringify(b.toString('latin1',po,po+24)):'');
}
const show=(va,len)=>{const o=f(va);if(o<0){console.log('unmapped',va.toString(16));return;}
 const out=[];for(let r=0;r<len;r+=16){const row=b.subarray(o+r,o+r+16);out.push('+'+(r).toString(16).padStart(3,'0')+' '+[...row].map(x=>x.toString(16).padStart(2,'0')).join(' '));}
 console.log('--',va.toString(16));console.log(out.join('\n'));
 // utf16 decode
 let s='';for(let i=0;i<len;i+=2){const c=b.readUInt16BE(o+i);s+= (c>=32&&c<127)?String.fromCharCode(c):(c===0?'.':(c<0x3000?'['+c.toString(16)+']':'~'));}
 console.log('utf16:',s);
};
show(0x68e700,0x140);
