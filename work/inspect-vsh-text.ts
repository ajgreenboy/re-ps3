// Read-only inspection of local vsh.elf (firmware 4.93, ELF64 PPC64) for text-metric constants.
// No writes to firmware, no network. Output is local evidence only.
const path = 'C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf';
const b = Buffer.from(await Bun.file(path).arrayBuffer());
const u16 = (p: number) => b.readUInt16BE(p);
const u32 = (p: number) => b.readUInt32BE(p);
const u64 = (p: number) => Number(b.readBigUInt64BE(p));

if (b.readUInt32BE(0) !== 0x7f454c46) throw new Error('not ELF');
const e_phoff = u64(0x20), e_phentsize = u16(0x36), e_phnum = u16(0x38);
console.log({ e_phoff, e_phentsize, e_phnum, e_shoff: u64(0x28) });

const segs: Array<{ off: number, vaddr: number, filesz: number, memsz: number, flags: number }> = [];
for (let i = 0; i < e_phnum; i++) {
  const p = e_phoff + i * e_phentsize;
  const type = u32(p), flags = u32(p + 4), off = u64(p + 8), vaddr = u64(p + 16);
  const filesz = u64(p + 32), memsz = u64(p + 40);
  if (type === 1) segs.push({ off, vaddr, filesz, memsz, flags });
}
console.log(segs.map(s => `VA ${s.vaddr.toString(16)}-${(s.vaddr + s.memsz).toString(16)} off ${s.off.toString(16)} filesz ${s.filesz.toString(16)} memsz ${s.memsz.toString(16)} flags ${s.flags}`).join('\n'));

const fileOff = (va: number): number => {
  for (const s of segs) if (va >= s.vaddr && va < s.vaddr + s.filesz) return s.off + (va - s.vaddr);
  return -1;
};
const hexdump = (va: number, len = 64) => {
  const o = fileOff(va);
  if (o < 0) return 'UNMAPPED';
  const out: string[] = [];
  for (let r = 0; r < len; r += 16) {
    const row = b.subarray(o + r, o + r + 16);
    out.push(`+${r.toString(16).padStart(3, '0')} ${[...row].map(x => x.toString(16).padStart(2, '0')).join(' ')}  ${[...row].map(x => x >= 32 && x < 127 ? String.fromCharCode(x) : '.').join('')}`);
  }
  return out.join('\n');
};

const targets: Array<[string, number, number]> = [
  ['DAT_0068e74c (flags&2, end-of-string suffix)', 0x68e74c, 32],
  ['DAT_0068e750 (flags&1, trailing suffix)', 0x68e750, 32],
  ['DAT_0068e74c-16', 0x68e73c, 64],
  ['DAT_0068e79c..7a4', 0x68e79c, 16],
  ['DAT_0068e0a0 (26.6 scale helpers)', 0x68e0a0, 64],
  ['DAT_0068e0b0 (style effects)', 0x68e0b0, 32],
  ['DAT_0068e0d0..e0f8', 0x68e0d0, 48],
  ['DAT_0068e104..0x128', 0x68e104, 48],
];
for (const [label, va, len] of targets) {
  console.log(`\n== ${label} @ ${va.toString(16)}`);
  console.log(hexdump(va, len));
  const o = fileOff(va);
  if (o >= 0) {
    const words: string[] = [];
    for (let i = 0; i + 4 <= len; i += 4) words.push(`${b.readFloatBE(o + i)}`);
    console.log('floats:', words.join(', '));
  }
}

// Scan the whole mapped file for ASCII/UTF-16 'glow', and for U+2026.
const scan = (needle: Buffer, label: string, utf16 = false) => {
  const hits: string[] = [];
  let at = 0;
  while (true) {
    const i = b.indexOf(needle, at);
    if (i < 0) break;
    const va = (() => { for (const s of segs) if (i >= s.off && i < s.off + s.filesz) return s.vaddr + (i - s.off); return -1; })();
    if (va >= 0) {
      const ctx = b.toString('latin1', Math.max(0, i - 24), i + 44).replace(/[^\x20-\x7e]/g, '.');
      hits.push(`offs=${i.toString(16)} VA=${va.toString(16)} ctx="${ctx}"`);
    }
    at = i + 1;
  }
  console.log(`\n== ${label}: ${hits.length}`);
  console.log(hits.slice(0, 25).join('\n'));
};
scan(Buffer.from('glow', 'latin1'), 'ascii glow');
scan(Buffer.from([0x00, 0x67, 0x00, 0x6c, 0x00, 0x6f, 0x00, 0x77]), 'utf16be glow');
scan(Buffer.from([0x20, 0x26]), 'U+2026 utf16be');
scan(Buffer.from('...', 'latin1'), 'ascii ...');
scan(Buffer.from([0x00, 0x2e, 0x00, 0x2e, 0x00, 0x2e]), 'utf16be ...');

// PhFont style tables used by PhFont::SetStyle (DAT_006fb268 / DAT_006fb428).
for (const base of [0x6fb268, 0x6fb428]) {
  const o = fileOff(base);
  console.log(`\n== style table @ ${base.toString(16)} mapped=${o >= 0}`);
  if (o < 0) continue;
  for (let i = 0; i < 16; i++) {
    const row: string[] = [];
    for (let j = 0; j < 7; j++) {
      const p = o + (i * 7 + j) * 4;
      row.push(`${u32(p).toString(16)}(${b.readFloatBE(p).toFixed(4)})`);
    }
    console.log(`[${i.toString().padStart(2)}] ${row.join(' ')}`);
  }
}
