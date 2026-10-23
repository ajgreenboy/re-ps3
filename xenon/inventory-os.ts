// Static evidence inventory. Binding/registration is never a claim of correct semantics.
// Run from the project root: bun xenon/inventory-os.ts [output-directory]
import { readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve, join, basename } from 'node:path';
import { createHash } from 'node:crypto';

const root = resolve(import.meta.dir, '..');
const output = resolve(process.argv[2] ?? join(root, 'out', 'os-inventory'));
const read = (p: string) => readFileSync(join(root, p), 'utf8').replaceAll('\r', '');
const csv = (p: string) => {
  const all = read(p).trim().split('\n');
  const hasHeader = !p.endsWith('_functions.csv') || all[0].startsWith('entry,');
  const keys = hasHeader ? all[0].split(',') : ['entry', 'ranges', 'name'];
  const lines = hasHeader ? all.slice(1) : all;
  return lines.filter(Boolean).map(line => Object.fromEntries(line.split(',').map((v, i) => [keys[i], v])));
};
const modules = csv('out/boot493/modules.csv');
const imports = csv('out/boot493/imports.csv');
const exports = csv('out/boot493/exports.csv');
const hle = csv('out/boot493/hle.csv');
// Resolve names from the checked-out primary reference, using its actual NID hash suffix.
const referenceRoot = resolve(process.argv[3] ?? join(root, '..', 'rpcs3'));
const ppuSource = readFileSync(join(referenceRoot, 'rpcs3/Emu/Cell/PPUModule.cpp'), 'utf8');
const suffixLiteral = ppuSource.match(/constexpr auto suffix = "([^"]+)"/);
if (!suffixLiteral) throw new Error('RPCS3 NID suffix definition not found');
const suffix = Buffer.from([...suffixLiteral[1].matchAll(/\\x([0-9a-f]{2})/gi)].map(m=>parseInt(m[1],16)));
if (suffix.length !== 16) throw new Error('unexpected RPCS3 NID suffix layout');
const nidFor = (name: string) => createHash('sha1').update(name).update(suffix).digest().readUInt32LE(0);
type Symbol = { name: string; registrationLibrary: string; source: string; line: number };
const symbols = new Map<number, Symbol[]>();
const wanted = new Set(hle.map(h=>Number(h.nid)));
const referenceModules = join(referenceRoot, 'rpcs3/Emu/Cell/Modules');
for (const file of readdirSync(referenceModules).filter(f=>f.endsWith('.cpp'))) {
  const source = readFileSync(join(referenceModules,file),'utf8');
  const registrations = /REG_(FUNC|FNID)\(\s*(\w+)\s*,\s*(?:(0x[0-9a-f]+)|"([^"]+)"|(\w+))/gi;
  for (const match of source.matchAll(registrations)) {
    const name = match[4] ?? match[5];
    const nid = match[3] ? Number(match[3]) : name ? nidFor(name) : NaN;
    if (!wanted.has(nid)) continue;
    const candidateName = name ?? source.slice(match.index!+match[0].length).match(/^\s*,\s*(\w+)/)?.[1] ?? '';
    const entry = { name: candidateName, registrationLibrary: match[2],
      source: `rpcs3/Emu/Cell/Modules/${file}`, line: source.slice(0,match.index).split('\n').length };
    const matches = symbols.get(nid) ?? [];
    if (!matches.some(s=>s.name===entry.name && s.source===entry.source)) matches.push(entry);
    symbols.set(nid,matches);
  }
}
const hostImports = hle.map(h=>({ ...h, referenceSymbols: symbols.get(Number(h.nid)) ?? [],
  callers: [...new Set(imports.filter(i=>i.provider==='HLE'&&i.library===h.library&&i.nid===h.nid).map(i=>i.module))] }));
const normalized = (name: string) => name.toLowerCase().replace(/\.(s?prx|elf)$/, '');
const linked = new Set(modules.map(m => normalized(m.module)));
const observed = [...new Set(read('out/trace/modules_loaded.txt').split('\n').filter(Boolean)
  .map(line => basename(line.slice(line.indexOf(' ') + 1))))];
const moduleRows = modules.map(m => {
  const functions = csv(`rea/functions_patched/${m.module}_functions.csv`);
  const imported = imports.filter(i => i.module === m.module);
  return { module: m.module, base: m.base, functions: functions.length,
    oneByteBodies: functions.filter(f => f.ranges.split(';').every(range => {
      const [s, e] = range.split('-'); return s === e;
    })).length,
    imports: imported.length, firmwareBindings: imported.filter(i => i.provider !== 'HLE').length,
    hleBindings: imported.filter(i => i.provider === 'HLE').length,
    exports: exports.filter(e => e.module === m.module).length,
    seenInReferenceBoot: observed.some(o => normalized(o) === normalized(m.module)) };
});
const referenceRows = observed.map(module => ({ module, linked: linked.has(normalized(module)) }));
// Preserve registration order from lv2_init: later entries replace earlier placeholders.
const lv2 = read('xmbrt/lv2.cpp');
const initBody = lv2.slice(lv2.indexOf('void lv2_init()'), lv2.indexOf('void lv2_syscall('));
const registrationOrder = [...initBody.matchAll(/\b(register_\w+_syscalls)\(\);/g)].map(m => m[1]);
const sources = readdirSync(join(root, 'xmbrt')).filter(f => f.endsWith('.cpp'))
  .map(file => ({ file, source: read(`xmbrt/${file}`) }));
const syscallNames = new Map([...read('xmbrt/syscall_names.inc').matchAll(/\/\* (\d+) \*\/ "([^"]+)"/g)]
  .map(m => [Number(m[1]), m[2]]));
const registrations = new Map<number, object>();
const unparsedRegistrations: string[] = [];
for (const registration of registrationOrder) {
  const owner = sources.find(s => s.source.includes(`void ${registration}()`));
  if (!owner) { unparsedRegistrations.push(registration); continue; }
  const body = owner.source.slice(owner.source.indexOf(`void ${registration}()`));
  for (const match of body.matchAll(/register_syscall\(\s*(\d+)\s*,\s*([^\n;]+)/g)) {
    const number = Number(match[1]);
    const handler = match[2].trim();
    const nearby = handler.slice(0, handler.indexOf('//') < 0 ? undefined : handler.indexOf('//'));
    const status = /CELL_ENOSYS/.test(nearby) ? 'explicit ENOSYS' :
      /^(sys_ok|hle_ok)\b/.test(nearby) || /return\s+0\s*;?\s*\}/.test(nearby)
        ? 'success placeholder candidate' : 'registered; semantics unverified';
    registrations.set(number, { number, name: syscallNames.get(number) ?? '', source: owner.file,
      line: owner.source.slice(0, owner.source.indexOf(body) + match.index!).split('\n').length,
      handler, status });
  }
}
const libraries = [...new Set(hle.map(h => h.library))].sort().map(library => ({ library,
  hostRoutedNids: hle.filter(h => h.library === library).length,
  callers: [...new Set(imports.filter(i => i.provider === 'HLE' && i.library === library).map(i => i.module))],
  status: 'host dispatcher; inspect hle.cpp per NID for handler/fallback/ENOSYS' }));
const referenceCalls = new Map<string, number>();
for (const match of read('out/trace/rpcs3.norm').matchAll(/\bsys\s+(\w+)\s+lr=/g))
  referenceCalls.set(match[1], (referenceCalls.get(match[1]) ?? 0) + 1);
const registeredNames = new Set([...registrations.keys()].map(n => syscallNames.get(n)));
const referenceSyscalls = [...referenceCalls].map(([name, calls]) => ({ name, calls,
  hasLiteralRegistration: registeredNames.has(name) })).sort((a,b)=>b.calls-a.calls);
mkdirSync(output, { recursive: true });
const result = { generated: new Date().toISOString(), modules: moduleRows, referenceModules: referenceRows,
  hleLibraries: libraries, hostImports, syscalls: [...registrations.values()], referenceSyscalls, unparsedRegistrations,
  limitations: ['Static binding and registration do not prove correct execution.',
    'Reference module list covers one recorded boot, not every XMB feature.',
    'Syscall scan recognizes literal registrations only; inline lambda bodies may be truncated. Missing registrations can still use RPCS3 null_func compatibility defaults.',
    'One-byte function metadata flags analysis candidates; it does not establish a runtime failure.'] };
writeFileSync(join(output, 'inventory.json'), JSON.stringify(result, null, 2) + '\n');
const escapeCsv = (v: unknown) => '"' + String(v).replaceAll('"','""') + '"';
writeFileSync(join(output,'host-imports.csv'), 'library,nid,reference_names,reference_sources,callers\n' +
  hostImports.map(h=>[h.library,h.nid,h.referenceSymbols.map(s=>s.name).join(';'),
    h.referenceSymbols.map(s=>`${s.source}:${s.line}`).join(';'),h.callers.join(';')].map(escapeCsv).join(',')).join('\n')+'\n');
const missing = referenceRows.filter(m => !m.linked);
const suspect = moduleRows.filter(m => m.oneByteBodies);
const md = ['# Native XMB OS inventory', '', `Generated ${result.generated}.`, '',
  `Linked firmware: **${modules.length} modules**, **${moduleRows.reduce((n,m)=>n+m.functions,0)} function entries**.`,
  `Recorded reference boot: **${observed.length} modules**, **${missing.length} absent from linked image**.`,
  `Host bindings: **${hle.length} NIDs across ${libraries.length} libraries**.`,
  `Literal syscall registrations after overrides: **${registrations.size}**. Correctness remains unverified.`, '',
  '## Reference modules absent from linked image', '', ...missing.map(m => `- ${m.module}`), '',
  '## Modules with one-byte function bodies', '',
  '| Module | One-byte bodies | Function entries | Host import bindings |', '|---|---:|---:|---:|',
  ...suspect.map(m => `| ${m.module} | ${m.oneByteBodies} | ${m.functions} | ${m.hleBindings} |`), '',
  '## Recorded syscalls without literal registrations', '',
  '| Syscall | Reference calls |', '|---|---:|',
  ...referenceSyscalls.filter(s=>!s.hasLiteralRegistration).map(s=>`| ${s.name} | ${s.calls} |`), '',
  '## Host import libraries', '', '| Library | Host-routed NIDs |', '|---|---:|',
  ...libraries.map(l => `| ${l.library} | ${l.hostRoutedNids} |`), '', '## Evidence limits', '',
  ...result.limitations.map(l => `- ${l}`), ''];
writeFileSync(join(output, 'inventory.md'), md.join('\n'));
console.log(JSON.stringify({ modules: modules.length, reference: observed.length, missingReference: missing.map(m=>m.module),
  oneByteModules: suspect.map(m=>[m.module,m.oneByteBodies]), hle: hle.length, syscalls: registrations.size, output }, null, 2));
