// Disassembles an SPU ELF (local firmware file, output stays local): every executable section, with labels on branch
// targets, function starts (brsl/brasl targets) marked, and loads of .rodata quadwords annotated with their floats.
// Usage: bun disasm-elf.ts <spu.elf> <out.txt>
import { readFileSync, writeFileSync } from "node:fs";
import { disassemble } from "./spu-disasm.generated.ts";
const [path, out] = process.argv.slice(2);
const b = readFileSync(path);
const shoff = b.readUInt32BE(32), shnum = b.readUInt16BE(48), shstr = b.readUInt16BE(50);
const sh = (i: number) => ({ name: b.readUInt32BE(shoff + i * 40), type: b.readUInt32BE(shoff + i * 40 + 4), flags: b.readUInt32BE(shoff + i * 40 + 8),
  addr: b.readUInt32BE(shoff + i * 40 + 12), off: b.readUInt32BE(shoff + i * 40 + 16), size: b.readUInt32BE(shoff + i * 40 + 20) });
const strtab = sh(shstr);
const nameOf = (o: number) => { let s = ""; for (let i = strtab.off + o; b[i]; i++) s += String.fromCharCode(b[i]); return s; };
const sections = [...Array(shnum).keys()].map(i => ({ ...sh(i), label: nameOf(sh(i).name) }));
// local store image
const ls = Buffer.alloc(0x40000);
for (const s of sections) if (s.type === 1 && s.addr) b.copy(ls, s.addr, s.off, s.off + s.size);
const code = sections.filter(s => s.flags & 4);
const targets = new Set<number>(), calls = new Set<number>();
for (const s of code) for (let pc = s.addr; pc < s.addr + s.size; pc += 4) {
  const d = disassemble(ls.readUInt32BE(pc), pc);
  if (d.target !== undefined) { targets.add(d.target); if (/^(BRSL|BRASL)$/.test(d.name)) calls.add(d.target); }
}
const rodata = sections.find(s => s.label === ".rodata");
let text = `; ${path}\n; sections: ${sections.filter(s => s.addr).map(s => `${s.label}@${s.addr.toString(16)}+${s.size}`).join(" ")}\n`;
for (const s of code) {
  text += `\n; ---- ${s.label}\n`;
  for (let pc = s.addr; pc < s.addr + s.size; pc += 4) {
    const raw = ls.readUInt32BE(pc), d = disassemble(raw, pc);
    if (calls.has(pc)) text += `\nfunc_${pc.toString(16)}:\n`; else if (targets.has(pc)) text += `L_${pc.toString(16)}:\n`;
    let note = "";
    // lqr/lqa of .rodata: show the quadword as floats and hex
    if ((d.name === "LQR" || d.name === "LQA") && d.target !== undefined && rodata && d.target >= rodata.addr && d.target < rodata.addr + rodata.size) {
      const q = d.target & ~15; const w = [0, 4, 8, 12].map(k => ls.readUInt32BE(q + k));
      note = `   ; [${w.map(x => x.toString(16).padStart(8, "0")).join(" ")}] = ${[0, 4, 8, 12].map(k => ls.readFloatBE(q + k).toPrecision(6)).join(" ")}`;
    }
    text += `  ${pc.toString(16).padStart(5, "0")}: ${raw.toString(16).padStart(8, "0")}  ${d.text}${note}\n`;
  }
}
writeFileSync(out, text);
console.log(`${code.reduce((n, s) => n + s.size / 4, 0)} instructions, ${calls.size} functions, ${targets.size} branch targets`);
