// Read-only probe of explore_plugin.prx constants (ELF64 PPC64 PRX container).
const path = 'C:\\dev\\xmb\\fw493\\vsh_module\\explore_plugin.prx';
const b = Buffer.from(await Bun.file(path).arrayBuffer());
const u16 = (p: number) => b.readUInt16BE(p);
const u32 = (p: number) => b.readUInt32BE(p);
const u64 = (p: number) => Number(b.readBigUInt64BE(p));
if (b.readUInt32BE(0) !== 0x7f454c46) throw new Error('not ELF');
const e_phoff = u64(0x20), e_phentsize = u16(0x36), e_phnum = u16(0x38);
const segs: Array<{ off: number, vaddr: number, filesz: number, memsz: number, flags: number }> = [];
for (let i = 0; i < e_phnum; i++) {
  const p = e_phoff + i * e_phentsize;
  const type = u32(p), flags = u32(p + 4), off = u64(p + 8), vaddr = u64(p + 16);
  const filesz = u64(p + 32), memsz = u64(p + 40);
  segs.push({ off, vaddr, filesz, memsz, flags });
}
console.log(segs.map(s => `type/flags ${s.flags} VA ${s.vaddr.toString(16)}-${(s.vaddr + s.memsz).toString(16)} off ${s.off.toString(16)} filesz ${s.filesz.toString(16)}`).join('\n'));
const fileOff = (va: number): number => {
  for (const s of segs) if (va >= s.vaddr && va < s.vaddr + s.filesz) return s.off + (va - s.vaddr);
  return -1;
};
const f32 = (va: number) => { const o = fileOff(va); return o < 0 ? null : b.readFloatBE(o); };
const names = ['1ca4', '1ca8', '1cac', '1cb0', '1cb4', '1ccc', '1cd0', '1cd4', '1ce0', '1ce4', '1ce8', '1cf0'];
for (const n of names) {
  const va = 0x271000 + parseInt(n, 16);
  console.log('0x' + va.toString(16), f32(va));
}
console.log('\n-- dump 0x271c80..0x271d20 as floats --');
for (let va = 0x271c80; va < 0x271d20; va += 4) console.log('0x' + va.toString(16), f32(va));
