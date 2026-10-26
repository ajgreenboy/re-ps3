import { readFileSync } from "node:fs";
const b = readFileSync(process.argv[2]);
const shoff = b.readUInt32BE(32), shnum = b.readUInt16BE(48), shstr = b.readUInt16BE(50);
const sh = (i) => ({ name: b.readUInt32BE(shoff+i*40), type: b.readUInt32BE(shoff+i*40+4), flags: b.readUInt32BE(shoff+i*40+8), addr: b.readUInt32BE(shoff+i*40+12), off: b.readUInt32BE(shoff+i*40+16), size: b.readUInt32BE(shoff+i*40+20), link: b.readUInt32BE(shoff+i*40+24), info: b.readUInt32BE(shoff+i*40+28), align: b.readUInt32BE(shoff+i*40+32), entsize: b.readUInt32BE(shoff+i*40+36) });
const strtab = sh(shstr);
const nameOf = (o) => { let s=""; for(let i=strtab.off+o;b[i];i++) s+=String.fromCharCode(b[i]); return s; };
const sections = [...Array(shnum).keys()].map(i => ({...sh(i), label: nameOf(sh(i).name)}));
console.log("=== sections ===");
for (const s of sections) console.log(`${s.label.padEnd(16)} type=${s.type} flags=${s.flags.toString(16)} addr=0x${s.addr.toString(16)} off=0x${s.off.toString(16)} size=0x${s.size.toString(16)} link=${s.link} info=${s.info} entsize=${s.entsize}`);
for (const s of sections) {
  if (s.type === 2 || s.type === 11) { // SYMTAB/DYNSYM
    const strsec = sections[s.link];
    const n = s.size / (s.entsize||16);
    console.log(`\n=== symbols ${s.label} (${n}) ===`);
    for (let i=0;i<n;i++) {
      const o = s.off + i*(s.entsize||16);
      const nm = b.readUInt32BE(o), val = b.readUInt32BE(o+4), sz = b.readUInt32BE(o+8), info = b.readUInt8(o+12), other=b.readUInt8(o+13), shndx = b.readUInt16BE(o+14);
      let str=""; for(let k=strsec.off+nm; b[k]; k++) str+=String.fromCharCode(b[k]);
      if (str) console.log(`${str.padEnd(40)} val=0x${val.toString(16)} size=0x${sz.toString(16)} type=${info&15} bind=${info>>4} shndx=${shndx}`);
    }
  }
}
