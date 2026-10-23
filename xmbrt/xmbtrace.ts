// Trace tooling for the xmbrt phases: name NIDs, normalise RPCS3 and xmbrt call logs to one format, diff them.
//
//   bun xmbtrace.ts names <rpcs3 repo> <out nid_names.csv>
//   bun xmbtrace.ts rpcs3 <xmb_trace.txt> <imports.csv> <nid_names.csv> <out.norm>
//   bun xmbtrace.ts xmbrt <xmbrt.log> <nid_names.csv> <out.norm>
//   bun xmbtrace.ts diff <reference.norm> <candidate.norm> [thread]
//
// Normalised line: "<seq> <thread> <kind> <name> lr=<xmbld addr> r3=.. r4=.. r5=.. r6=.. -> <r3 or ?>"
// kind is "sys" (name = syscall name) or "imp" (name = library.function, or library.0xNID when unnamed).
import { createHash } from "node:crypto";
import { readdirSync } from "node:fs";
import { join } from "node:path";

const read = async (p: string) => (await Bun.file(p).text()).replaceAll("\r", "");

// RPCS3 ppu_generate_id: little-endian first word of SHA-1(name + suffix).
const NID_SUFFIX = Buffer.from([0x67, 0x59, 0x65, 0x99, 0x04, 0x25, 0x04, 0x90, 0x56, 0x64, 0x27, 0x49, 0x94, 0x89, 0x74, 0x1a]);
function nidOf(name: string): number {
  return createHash("sha1").update(Buffer.concat([Buffer.from(name), NID_SUFFIX])).digest().readUInt32LE(0);
}
const hex8 = (n: number) => n.toString(16).toUpperCase().padStart(8, "0");

async function names(rpcs3: string, out: string) {
  const rows = new Map<string, string>(); // "lib,nid" -> name
  const add = (lib: string, nid: number, name: string) => rows.set(`${lib},0x${hex8(nid)}`, name);
  const modules = join(rpcs3, "rpcs3", "Emu", "Cell", "Modules");
  for (const f of readdirSync(modules).filter((f) => f.endsWith(".cpp"))) {
    const text = await read(join(modules, f));
    for (const m of text.matchAll(/REG_FUNC\(\s*(\w+)\s*,\s*(\w+)\s*\)/g)) add(m[1], nidOf(m[2]), m[2]);
    for (const m of text.matchAll(/REG_FNID\(\s*(\w+)\s*,\s*("([^"]+)"|0x[0-9a-fA-F]+)\s*,\s*(\w+)\s*\)/g)) {
      if (m[3]) add(m[1], nidOf(m[3]), m[3]);
      else add(m[1], Number(m[2]), m[4]);
    }
  }
  // Hard-coded tables in PPUFunction.cpp: `if (_module == "a" || _module == "b") switch (fnid)` + `case 0x..: return "name";`
  const fn = await read(join(rpcs3, "rpcs3", "Emu", "Cell", "PPUFunction.cpp"));
  const fnEnd = fn.indexOf("ppu_get_variable_name");
  let libs: string[] = [];
  for (const line of fn.slice(0, fnEnd).split("\n")) {
    const header = line.match(/if \((_module == .*)\) switch/);
    if (header) libs = [...header[1].matchAll(/"(\w+)"/g)].map((m) => m[1]);
    const c = line.match(/case (0x[0-9a-fA-F]+): return "([^"]+)"/);
    if (c && libs.length) for (const lib of libs) add(lib, Number(c[1]), c[2]);
  }
  const lines = [...rows].map(([k, v]) => `${k},${v}`).sort();
  await Bun.write(out, `library,nid,name\n${lines.join("\n")}\n`);
  console.log(`${lines.length} named functions -> ${out}`);
}

async function loadNames(path: string) {
  const map = new Map<string, string>();
  for (const line of (await read(path)).split("\n").slice(1)) {
    const [lib, nid, name] = line.split(",");
    if (name) map.set(`${lib},${nid.toUpperCase().replace("0X", "0x")}`, name);
  }
  return map;
}
const impName = (names: Map<string, string>, lib: string, nid: string) =>
  `${lib}.${names.get(`${lib},${nid}`) ?? nid}`;

// Collapses back-to-back repeats of the same call (thread, kind, name, lr) into one line ending " xN",
// so polling loops (sys_timer_usleep etc.) don't drown the sequence. Lines must be in seq order.
function collapse(lines: string[]): string[] {
  const out: { line: string; count: number }[] = [];
  const last = new Map<string, { key: string; entry: { line: string; count: number } }>();
  for (const line of lines) {
    const [, tid, kind, name, lr] = line.split(" ");
    const key = `${kind} ${name} ${lr}`;
    const prev = last.get(tid);
    if (prev && prev.key === key) {
      prev.entry.count++;
      continue;
    }
    const entry = { line, count: 1 };
    out.push(entry);
    last.set(tid, { key, entry });
  }
  return out.map((e) => (e.count > 1 ? `${e.line} x${e.count}` : e.line));
}

async function rpcs3(tracePath: string, importsPath: string, namesPath: string, out: string) {
  const names = await loadNames(namesPath);
  const slots = new Map<number, [string, string]>(); // xmbld slot -> [library, nid]
  for (const line of (await read(importsPath)).split("\n").slice(1)) {
    const [, lib, nid, slot] = line.split(",");
    if (slot) slots.set(Number(slot), [lib, nid.toUpperCase().replace("0X", "0x")]);
  }
  const calls = new Map<number, { text: string; ret: string }>();
  // Loaded modules ("# prx <path> seq=N <addr>+<size>/v<vaddr> ..."), to label calls through function pointers.
  type Seg = { addr: number; size: number; vaddr: number; module: string; live: boolean };
  const segs: Seg[] = [];
  const where = (addr: number) => {
    const s = segs.findLast((s) => s.live && addr - s.addr >= 0 && addr - s.addr < s.size);
    return s ? `${s.module}+${(addr - s.addr + s.vaddr).toString(16).toUpperCase()}` : `unknown_${addr.toString(16).toUpperCase()}`;
  };
  for (const line of (await read(tracePath)).split("\n")) {
    const prx = line.match(/^# prx (\S+) seq=\d+(.*)$/);
    if (prx) {
      const module = prx[1].slice(prx[1].lastIndexOf("/") + 1).replace(/\.sprx$/, "");
      for (const s of prx[2].matchAll(/([0-9A-F]+)\+([0-9A-F]+)\/v([0-9A-F]+)/g))
        segs.push({ addr: parseInt(s[1], 16), size: parseInt(s[2], 16), vaddr: parseInt(s[3], 16), module, live: true });
      continue;
    }
    const unload = line.match(/^# unload (\S+)/);
    if (unload) {
      const module = unload[1].slice(unload[1].lastIndexOf("/") + 1).replace(/\.sprx$/, "");
      for (const s of segs) if (s.module === module) s.live = false;
      continue;
    }
    if (!line || line.startsWith("#")) continue;
    const ret = line.match(/^(\d+) \S+ ret r3=(\w+)/);
    if (ret) {
      const c = calls.get(Number(ret[1]));
      if (c) c.ret = ret[2];
      continue;
    }
    const sys = line.match(/^(\d+) (\S+) sys \d+ (\S+) cia=\w+ (lr=\w+ r3=\w+ r4=\w+ r5=\w+ r6=\w+)/);
    if (sys) {
      calls.set(Number(sys[1]), { text: `${sys[2]} sys ${sys[3]} ${sys[4]}`, ret: "?" });
      continue;
    }
    const imp = line.match(/^(\d+) (\S+) imp slot=(\w+) (lr=\w+ r3=\w+ r4=\w+ r5=\w+ r6=\w+) .*tgt=(\w+)/);
    if (imp) {
      const s = slots.get(parseInt(imp[3], 16));
      const name = s ? impName(names, s[0], s[1]) : `ptr.${where(parseInt(imp[5], 16))}`;
      calls.set(Number(imp[1]), { text: `${imp[2]} imp ${name} ${imp[4]}`, ret: "?" });
    }
  }
  const all = [...calls].sort((a, b) => a[0] - b[0]).map(([seq, c]) => `${seq} ${c.text} -> ${c.ret}`);
  const lines = collapse(all);
  await Bun.write(out, lines.join("\n") + "\n");
  console.log(`${all.length} calls, ${lines.length} after collapsing repeats -> ${out}`);
}

async function xmbrt(logPath: string, namesPath: string, out: string) {
  const names = await loadNames(namesPath);
  const lines: string[] = [];
  for (const line of (await read(logPath)).split("\n")) {
    const sys = line.match(/^(\d+) (\S+) sys \d+ (\S+) (lr=\w+ r3=\w+ r4=\w+ r5=\w+ r6=\w+) -> (\S+)/);
    if (sys) {
      lines.push(`${sys[1]} ${sys[2]} sys ${sys[3]} ${sys[4]} -> ${sys[5]}`);
      continue;
    }
    const hle = line.match(/^(\d+) (\S+) hle (\S+) (0x\w+) (lr=\w+ r3=\w+ r4=\w+ r5=\w+ r6=\w+) -> (\S+)/);
    if (hle) lines.push(`${hle[1]} ${hle[2]} imp ${impName(names, hle[3], hle[4])} ${hle[5]} -> ${hle[6]}`);
  }
  const collapsed = collapse(lines);
  await Bun.write(out, collapsed.join("\n") + "\n");
  console.log(`${lines.length} calls, ${collapsed.length} after collapsing repeats -> ${out}`);
}

// Compares the call sequence of one thread (default: the first thread in the reference) by kind, name and lr.
async function diff(refPath: string, candPath: string, thread?: string) {
  const parse = async (p: string) =>
    (await read(p)).split("\n").filter(Boolean).map((l) => {
      const [seq, tid, kind, name, lr] = l.split(" ");
      return { seq, tid, key: `${kind} ${name} ${lr}`, line: l };
    });
  const ref = await parse(refPath), cand = await parse(candPath);
  const tid = thread ?? ref[0]?.tid;
  const a = ref.filter((c) => c.tid === tid), b = cand.filter((c) => c.tid === tid);
  let i = 0;
  while (i < a.length && i < b.length && a[i].key === b[i].key) i++;
  console.log(`thread ${tid}: ${i} matching calls (reference ${a.length}, candidate ${b.length})`);
  if (i === a.length && i === b.length) return console.log("identical");
  for (let k = Math.max(0, i - 3); k < i; k++) console.log(`  same  ${a[k].line}`);
  console.log(`  ref   ${a[i]?.line ?? "(end)"}`);
  console.log(`  cand  ${b[i]?.line ?? "(end)"}`);
}

const [cmd, ...args] = process.argv.slice(2);
const commands: Record<string, (...a: string[]) => Promise<unknown>> = { names, rpcs3, xmbrt, diff };
if (!commands[cmd]) {
  console.error("usage: bun xmbtrace.ts names|rpcs3|xmbrt|diff ...");
  process.exit(64);
}
await commands[cmd](...args);
