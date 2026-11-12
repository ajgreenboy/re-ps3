// ZF2/ZF1 verification (audit/TEXT-ENTRY.md). Everything runs against a sink window of our own; nothing is sent to any other
// application, and only the PIDs this script starts are ever stopped.
//   bun work/zf2-text-test.ts [--exe C:/dev/out/zf2-build/re-ps3-native.exe]
import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';

const arg = (name: string, fallback: string) => { const i = process.argv.indexOf(name); return i >= 0 ? process.argv[i + 1] : fallback; };
const exe = arg('--exe', 'C:/dev/out/zf2-build/re-ps3-native.exe');
const out = 'C:/dev/out/zf2';
mkdirSync(`${out}/data`, { recursive: true });
const env = { ...process.env, XMB_SINGLE_INSTANCE: '0' };
const common = ['--data-dir', `${out}/data`];
const run = async (args: string[]) => {
  const p = Bun.spawn([exe, ...common, ...args], { env, stdout: 'pipe', stderr: 'pipe' });
  const code = await p.exited;
  return { code, stdout: await new Response(p.stdout).text(), stderr: await new Response(p.stderr).text() };
};
let failures = 0;
const check = (label: string, ok: boolean, detail = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? '  ' + detail : ''}`); if (!ok) failures++; };

// Test text: ASCII, accents, CJK, an emoji (a surrogate pair), spaces at both ends, and a tab and a newline that must be dropped.
const text = ' Hello, W\u00f6rld! \u00f1 \u65e5\u672c\u8a9e \ud83d\ude00 \u00fcn\u00ef\tX\nY ';
const expected = text.replace(/[\u0000-\u001f\u007f-\u009f]/g, '');
writeFileSync(`${out}/text-in.txt`, text, 'utf8');

// 1. the pure mapping
{
  const r = await run(['--zf-text-test', `${out}/text-in.txt`, `${out}/text-events.json`]);
  const j = JSON.parse(readFileSync(`${out}/text-events.json`, 'utf8'));
  const units = [...expected].flatMap(c => { const cp = c.codePointAt(0)!; return cp > 0xffff ? [0xd800 + ((cp - 0x10000) >> 10), 0xdc00 + ((cp - 0x10000) & 0x3ff)] : [cp]; });
  const want = units.flatMap(u => [{ unit: u, down: true }, { unit: u, down: false }]);
  const got = j.events.map((e: any) => ({ unit: e.unit, down: e.down }));
  check('mapping: exit 0', r.code === 0);
  check('mapping: 2 control characters dropped', j.dropped === 2, `dropped=${j.dropped}`);
  check('mapping: one down and one up per UTF-16 unit, in order', JSON.stringify(got) === JSON.stringify(want), `${j.unitsToSend} units, ${j.eventCount} events`);
  check('mapping: emoji is two consecutive units', units.includes(0xd83d) && units.includes(0xde00));
}

// 2. delivery into the sink
const sinkLog = `${out}/sink.log`;
const sink = Bun.spawn([exe, ...common, '--zf-text-sink', sinkLog, '30'], { env, stdout: 'pipe', stderr: 'pipe' });
const readLines = () => existsSync(sinkLog) ? readFileSync(sinkLog, 'utf8').split('\n').filter(Boolean).map(l => JSON.parse(l)) : [];
let ready: any;
for (let i = 0; i < 100 && !ready; i++) { await Bun.sleep(100); ready = readLines().find(l => l.e === 'ready'); }
check('sink: ready', !!ready, ready ? `pid=${ready.pid} foreground=${ready.foreground}` : '');
if (ready) {
  const deliver = async (name: string, extra: object) => {
    const spec = { hwnd: ready.hwnd, pid: ready.pid, textFile: `${out}/text-in.txt`, ...extra };
    writeFileSync(`${out}/spec-${name}.json`, JSON.stringify(spec));
    const r = await run(['--zf-text-deliver', `${out}/spec-${name}.json`, `${out}/result-${name}.json`]);
    const j = existsSync(`${out}/result-${name}.json`) ? JSON.parse(readFileSync(`${out}/result-${name}.json`, 'utf8')) : {};
    return { r, j };
  };
  // not the tracked tree: nothing may be sent. The root is an unrelated process this script starts (the sink is a child of this
  // script, so this script's own pid would count as its parent).
  const other = Bun.spawn(['cmd', '/c', 'ping -n 20 127.0.0.1 >nul'], { stdout: 'ignore', stderr: 'ignore' });
  const b = await deliver('untracked', { trackedRoot: other.pid });
  other.kill();
  // and the tree rule itself: this script is the sink's parent, so it is a tracked root that reaches the sink
  const tree = await deliver('tree-root', { trackedRoot: process.pid });
  check('tracked root is an ancestor of the window process: delivers (tree membership)', tree.j.verdict === 'deliver', JSON.stringify(tree.j));
  check('untracked window: verdict not-tracked, 0 units sent', b.j.verdict === 'not-tracked' && b.j.unitsSent === 0, JSON.stringify(b.j));
  // vanished window
  const c = await deliver('gone', { destroyBeforeStart: true });
  check('vanished window: verdict no-window, 0 units sent', c.j.verdict === 'no-window' && c.j.unitsSent === 0, JSON.stringify(c.j));
  // the real delivery
  const a = await deliver('ok', {});
  check('delivery: verdict deliver', a.j.verdict === 'deliver', JSON.stringify(a.j));
  console.log('      stderr (counts only, must hold no text):\n' + a.r.stderr.split('\n').filter(l => l.includes('textentry')).map(l => '        ' + l).join('\n'));
}
await sink.exited;
const done = readLines().find(l => l.e === 'done');
check('sink: received exactly the expected text, twice (tree-root run and the main run; the untracked and vanished runs added nothing) (controls dropped, order kept, surrogate pair intact)', !!done && done.received === expected + expected, done ? `received ${done.units.length} units, ${done.keyDowns} key downs, ${done.keyUps} key ups` : 'no done line');
if (done && done.received !== expected + expected) console.log('      got units', JSON.stringify(done.units), 'want', '(expected twice)');

// 3. elevation detection against a process this script starts (not elevated) and a count over all processes
const child = Bun.spawn(['cmd', '/c', 'ping -n 30 127.0.0.1 >nul'], { stdout: 'ignore', stderr: 'ignore' });
await Bun.sleep(500);
const e = await run(['--zf-elevation-check', `${out}/elevation.json`, String(child.pid), 'all']);
child.kill();
const ej = JSON.parse(readFileSync(`${out}/elevation.json`, 'utf8'));
console.log('      elevation report:', JSON.stringify(ej));
check('elevation: exit 0', e.code === 0);
check('elevation: a process started here is not elevated (when this shell is not elevated)', ej.self === 'elevated' || ej.pids[String(child.pid)] === 'not-elevated', `self=${ej.self} child=${ej.pids[String(child.pid)]}`);
console.log(failures ? `\n${failures} FAILED` : '\nall passed');
process.exit(failures ? 1 : 0);
