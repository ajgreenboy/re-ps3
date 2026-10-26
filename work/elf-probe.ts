const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
console.log('size', b.length, 'magic', b.subarray(0,16).toString('hex'));
const u16=(p)=>b.readUInt16BE(p), u32=(p)=>b.readUInt32BE(p);
console.log('class/data', b[4], b[5], 'type', u16(0x10), 'machine', u16(0x12), 'version', u32(0x14));
console.log('entry', u32(0x18).toString(16), 'phoff', u32(0x1c), 'shoff', u32(0x20), 'flags', u32(0x24).toString(16));
console.log('ehsize', u16(0x28), 'phentsize', u16(0x2a), 'phnum', u16(0x2c), 'shentsize', u16(0x2e), 'shnum', u16(0x30), 'shstrndx', u16(0x32));
const phoff=u32(0x1c), phnum=u16(0x2c), phes=u16(0x2a);
for(let i=0;i<Math.min(phnum,12);i++){const p=phoff+i*phes;console.log(i,'type',u32(p),'off',u32(p+4).toString(16),'vaddr',u32(p+8).toString(16),'paddr',u32(p+12).toString(16),'filesz',u32(p+16).toString(16),'memsz',u32(p+20).toString(16),'flags',u32(p+24).toString(16),'align',u32(p+28));}
