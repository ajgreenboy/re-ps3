// Read-only probe over the pre-digested RCO inventory for the Settings screens audit.
// Reads out/native-reference/rco-behavior/resources.json only; writes nothing.
// Usage: bun work/settings-probe.ts pages [resFilter]
//        bun work/settings-probe.ts tree <resFilter> <namePrefix>
//        bun work/settings-probe.ts fields <resFilter> <exactName>
//        bun work/settings-probe.ts schemas <resFilter>
import { readFileSync } from "node:fs";

const JSON_PATH = "C:/dev/xmb/out/native-reference/rco-behavior/resources.json";
type Obj = { offset: number; typeId: string; name: string; parent: string; children: number; extraBytes: number; schema: string; fields: Record<string, unknown> };
type Res = { path: string; sha256?: string; objects: Obj[]; animations: Record<string, unknown>[]; error?: string };
const db = JSON.parse(readFileSync(JSON_PATH, "utf8")) as { counts: Record<string, number>; resources: Res[] };

const [mode, a1, a2] = process.argv.slice(2);
const res = (f?: string) => db.resources.filter((r) => (f ? r.path.toLowerCase().includes(f.toLowerCase()) : true));

if (mode === "pages") {
  for (const r of res(a1)) {
    const pages = r.objects.filter((o) => o.schema === "Page");
    if (!pages.length) continue;
    console.log(`\n=== ${r.path} (${r.objects.length} objects) pages=${pages.length} ===`);
    for (const p of pages) console.log(p.name);
  }
} else if (mode === "schemas") {
  for (const r of res(a1)) {
    const m = new Map<string, number>();
    for (const o of r.objects) m.set(o.schema, (m.get(o.schema) ?? 0) + 1);
    console.log(`\n=== ${r.path} ===`);
    console.log([...m.entries()].sort((x, y) => y[1] - x[1]).map(([k, v]) => `${k}=${v}`).join(" "));
  }
} else if (mode === "tree") {
  const r = res(a1)[0];
  if (!r) throw new Error("no resource matches " + a1);
  const kids = new Map<string, Obj[]>();
  for (const o of r.objects) {
    const k = o.parent || "";
    if (!kids.has(k)) kids.set(k, []);
    kids.get(k)!.push(o);
  }
  const seen = new Set<string>();
  const walk = (o: Obj, d: number) => {
    if (o.name && seen.has(o.name)) return;
    if (o.name) seen.add(o.name);
    const f = Object.keys(o.fields).length ? " " + JSON.stringify(o.fields) : "";
    console.log(`${"  ".repeat(d)}${o.name || "(anon)"} [${o.schema}]${f}`);
    for (const c of kids.get(o.name) ?? []) walk(c, d + 1);
  };
  const roots = r.objects.filter((o) => o.name && o.name.startsWith(a2) && (!o.parent || !o.parent.startsWith(a2)));
  for (const o of roots) walk(o, 0);
} else if (mode === "sub") {
  const r = res(a1)[0];
  const kids = new Map<string, Obj[]>();
  for (const o of r.objects) {
    const k = o.parent || "";
    if (!kids.has(k)) kids.set(k, []);
    kids.get(k)!.push(o);
  }
  const root = r.objects.find((o) => o.name === a2);
  if (!root) throw new Error("no object named " + a2);
  const walk = (o: Obj, d: number) => {
    const f = Object.keys(o.fields).length ? " " + JSON.stringify(o.fields) : "";
    console.log(`${"  ".repeat(d)}${o.name || "(anon)"} [${o.schema}]${f}`);
    for (const c of kids.get(o.name) ?? []) walk(c, d + 1);
  };
  walk(root, 0);
} else if (mode === "fields") {
  const r = res(a1)[0];
  for (const o of r.objects) if (o.name === a2) console.log(`${o.name} [${o.schema}] ${JSON.stringify(o.fields, null, 1)}`);
} else {
  console.log("usage: pages|tree|fields|schemas");
}
