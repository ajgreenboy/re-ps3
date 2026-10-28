// Object-tree dumper over the pre-digested RCO inventory.
// Reads only out/native-reference/rco-behavior/resources.json (already-parsed firmware data).
//
// Usage:
//   bun work/rco-tree-dump.ts --list                       list resources + object counts
//   bun work/rco-tree-dump.ts --res system_plugin.rco      full tree of one resource
//   bun work/rco-tree-dump.ts --res system_plugin.rco --grep msgdialog
//   bun work/rco-tree-dump.ts --res system_plugin.rco --names   names only + type
//   bun work/rco-tree-dump.ts --grep tex_ --all            search every resource by name
//   bun work/rco-tree-dump.ts --grep x --all --fields       include parsed fields
import { readFileSync } from "node:fs";

const JSON_PATH = "C:/dev/xmb/out/native-reference/rco-behavior/resources.json";

type Obj = {
  offset: number;
  typeId: string;
  name: string;
  parent: string;
  children: number;
  extraBytes: number;
  schema: string;
  fields: Record<string, unknown>;
};
type Anim = Record<string, unknown> & { offset: number; typeId: string; name?: string; parent?: string; schema?: string };
type Res = { path: string; sha256?: string; objects: Obj[]; animations: Anim[]; error?: string };

const db = JSON.parse(readFileSync(JSON_PATH, "utf8")) as { counts: Record<string, number>; resources: Res[] };

const argv = process.argv.slice(2);
const flag = (n: string) => argv.includes(n);
const val = (n: string): string | undefined => {
  const i = argv.indexOf(n);
  return i >= 0 && i + 1 < argv.length && !argv[i + 1].startsWith("--") ? argv[i + 1] : undefined;
};

const byName = (a: Res, b: Res) => a.path.localeCompare(b.path);

if (flag("--list")) {
  console.log(JSON.stringify(db.counts));
  for (const r of [...db.resources].sort(byName))
    console.log(`${r.path}\tobjects=${r.objects.length}\tanims=${r.animations.length}${r.error ? "\tERR " + r.error : ""}`);
} else {
  const grep = val("--grep");
  const resName = val("--res");
  const showFields = flag("--fields");
  const re = grep ? new RegExp(grep, "i") : null;

  const resources = [...db.resources]
    .filter((r) => (resName ? r.path.toLowerCase().includes(resName.toLowerCase()) : true))
    .sort(byName);

  for (const r of resources) {
    const hits = r.objects.filter((o) => (re ? re.test(o.name) || re.test(o.schema) : true));
    if (!hits.length && !r.animations.length) continue;
    console.log(`\n=== ${r.path} (${r.objects.length} objects, sha256 ${r.sha256?.slice(0, 16)}) ===`);
    // depth by walking parents inside the resource
    const childrenOf = new Map<string, Obj[]>();
    for (const o of r.objects) {
      const k = o.parent || "";
      if (!childrenOf.has(k)) childrenOf.set(k, []);
      childrenOf.get(k)!.push(o);
    }
    const print = (o: Obj, depth: number) => {
      const pad = "  ".repeat(depth);
      const parentTag = o.parent ? ` <- ${o.parent}` : "";
      const f = showFields && Object.keys(o.fields).length ? ` ${JSON.stringify(o.fields)}` : "";
      console.log(`${pad}${o.name || "(anon)"}  [${o.schema} ${o.typeId} @${o.offset}${parentTag}]${f}`);
      for (const c of childrenOf.get(o.name) ?? []) if (c !== o) print(c, depth + 1);
    };
    // roots for the filtered set: those whose parent is empty or not itself a hit
    const hitNames = new Set(hits.map((h) => h.name).filter(Boolean));
    const printed = new Set<string>();
    const printOnce = (o: Obj, depth: number) => {
      if (!o.name) {
        print(o, depth);
        return;
      }
      if (printed.has(o.name)) return;
      printed.add(o.name);
      print(o, depth);
    };
    for (const o of hits) if (!o.parent || !hitNames.has(o.parent)) printOnce(o, 0);

    if (r.animations.length) {
      console.log(`  -- animations (${r.animations.length}) --`);
      for (const a of r.animations) {
        const { offset, typeId, name, parent, schema, ...rest } = a;
        const bits = Object.entries(rest)
          .filter(([, v]) => v !== "" && v != null)
          .map(([k, v]) => `${k}=${v}`)
          .join(" ");
        console.log(`  ${"  ".repeat(parent ? 1 : 0)}${name || "(anon)"} [${schema ?? ""} ${typeId} @${offset}]${parent ? ` <- ${parent}` : ""} ${bits}`);
      }
    }
  }
}
