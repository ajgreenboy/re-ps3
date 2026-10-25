// Symbolic decompiler for VSMX (Sony's compiled JavaScript, VSMX version 0x20000 as in coldboot.raf's coldboot.jsx).
// Header (LE u32): "VSMX", version, code off/len, text off/len/count (UTF-16), prop off/len/count (UTF-16),
// names off/len/count (8-bit). Code: 8-byte (id, value) pairs; id & 0xff is the opcode (numbering as RCOMage's
// vsmx.c documents it), id >> 8 carries argument counts for functions.
// Usage: bun coldboot-vsmx-decompile.ts <file.jsx>
const u = new Uint8Array(await Bun.file(process.argv[2]).arrayBuffer());
const dv = new DataView(u.buffer);
const le = (o: number) => dv.getUint32(o, true);
const tab = (o: number, len: number, enc: string) => new TextDecoder(enc).decode(u.subarray(o, o + len)).split("\0");
const text = tab(le(0x10), le(0x14), "utf-16le"), prop = tab(le(0x1c), le(0x20), "utf-16le"), names = tab(le(0x28), le(0x2c), "latin1");
const code = [...Array(le(0xc) / 8)].map((_, i) => ({ id: le(0x34 + i * 8), v: le(0x38 + i * 8), f: dv.getFloat32(0x38 + i * 8, true) }));
const bin: Record<number, string> = { 1: "=", 2: "+", 3: "-", 4: "*", 5: "/", 6: "%", 0xe: "==", 0xf: "!=", 0x10: "===", 0x11: "!==", 0x12: "<", 0x13: "<=", 0x14: ">=", 0x15: ">", 0x19: "&", 0x1a: "^", 0x1b: "|", 0x1d: "<<", 0x1e: ">>", 0x1f: ">>>" };
const un: Record<number, string> = { 7: "+", 8: "-", 9: "!", 0x1c: "~", 0x18: "typeof " };
let st: string[] = [];
const out: string[] = [];
const pop = () => st.pop() ?? "?";
for (let i = 0; i < code.length; i++) {
  const { id, v, f } = code[i], op = id & 0xff;
  const emit = (s: string) => out.push(`${String(i).padStart(4)}  ${s}`);
  if (bin[op]) { const b = pop(), a = pop(); st.push(op === 1 ? `${a} = ${b}` : `(${a} ${bin[op]} ${b})`); continue; }
  if (un[op]) { st.push(`${un[op]}${pop()}`); continue; }
  switch (op) {
    case 0xa: st.push(`++${pop()}`); break; case 0xb: st.push(`--${pop()}`); break;
    case 0xc: st.push(`${pop()}++`); break; case 0xd: st.push(`${pop()}--`); break;
    case 0x20: st.push(st[st.length - 1]); break;
    case 0x22: if (st.length) emit(st.join(" ; ") + ";"); st = []; break;
    case 0x23: st.push("null"); break; case 0x24: st.push("[]"); break;
    case 0x25: st.push(v ? "true" : "false"); break; case 0x26: st.push(String(v | 0)); break;
    case 0x27: st.push(String(+f.toPrecision(7))); break; case 0x28: st.push(JSON.stringify(text[v])); break;
    case 0x29: st.push("{}"); break;
    case 0x2a: st.push(`function(args=${(id >> 8) & 0xff}, locals=${id >>> 24}) @${v}`); break;
    case 0x2b: st.push("["); break; case 0x2c: st.push("this"); break;
    case 0x2d: st.push(`arg${v}`); break; case 0x2e: st.push(names[v]); break;
    case 0x2f: case 0x30: st.push(`${pop()}.${prop[v]}`); break;
    case 0x31: { const b = pop(), a = pop(); st.push(`${a}.${prop[v]} = ${b}`); break; }
    case 0x32: st.push(`delete ${pop()}.${prop[v]}`); break;
    case 0x33: { const b = pop(), a = pop(); st.push(`${a}+{${prop[v]}: ${b}}`); break; }
    case 0x34: { const b = pop(), a = pop(); st.push(`${a}[${b}]`); break; }
    case 0x36: { const c = pop(), b = pop(), a = pop(); st.push(`${a}[${b}] = ${c}`); break; }
    case 0x38: { const b = pop(), a = pop(); st.push(`${a}${a.endsWith("[") ? "" : ", "}${b}`); break; }
    case 0x39: emit(`${st.length ? st.join(" ; ") + " ; " : ""}goto ${v}`); st = []; break;
    case 0x3a: emit(`if (${pop()}) goto ${v}`); break;
    case 0x3b: emit(`if (!${pop()}) goto ${v}`); break;
    case 0x3c: case 0x3d: case 0x3e: {
      const args = [...Array(v)].map(pop).reverse(), fn = pop();
      st.push(`${op === 0x3e ? "new " : ""}${fn}(${args.join(", ")})`); break;
    }
    case 0x3f: emit(`return ${pop()};`); st = []; break;
    case 0x45: emit("end"); break;
    case 0x46: case 0x47: break;
    case 0x49: { const items = [...Array(v)].map(pop).reverse(); st.push(`[${items.join(", ")}]`); break; }
    default: emit(`?op${op.toString(16)} ${v}`);
  }
}
console.log(out.join("\n"));
