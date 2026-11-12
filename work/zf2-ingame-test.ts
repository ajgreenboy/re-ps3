// ZF2 end to end (audit/TEXT-ENTRY.md): the XMB launches a sink window of our own as its tracked title (the XMB's own exe in
// --zf-text-sink mode, imported as an app), a scripted PS tap opens the in-game XMB, Cross on "Enter Text" opens the OSK, keys are
// pressed on it, and Enter confirms (case A) or Circle cancels (case B). Nothing touches any other application.
//   bun work/zf2-ingame-test.ts [--exe C:/dev/out/zf2-build/re-ps3-native.exe]
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const arg = (name: string, fallback: string) => { const i = process.argv.indexOf(name); return i >= 0 ? process.argv[i + 1] : fallback; };
const exe = arg('--exe', 'C:/dev/out/zf2-build/re-ps3-native.exe');
const out = 'C:/dev/out/zf2/e2e';
const flash = 'C:/dev/rpcs3/bin/dev_flash';
const seed = 'C:/dev/xmb/out/native-reference/v2/game';   // read-only data seed of the gate's ingame-xmb state
let failures = 0;
const check = (label: string, ok: boolean, detail = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? '  ' + detail : ''}`); if (!ok) failures++; };
const win = (s: string) => s.split('/').join(String.fromCharCode(92));
const rep = (t: string, n: number) => Array(n).fill(t);

async function runCase(name: string, tail: string[]) {
  const dir = `${out}/${name}`;
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(`${dir}/data`, { recursive: true });
  mkdirSync(`${dir}/no-steam`, { recursive: true });
  for (const f of ['preferences.json', 'profiles.json', 'settings.ini']) cpSync(`${seed}/${f}`, `${dir}/data/${f}`);
  const sinkLog = `${dir}/sink.log`;
  writeFileSync(`${dir}/data/apps.json`, JSON.stringify({ version: 1, apps: [{ id: 'v2-stub', title: 'Gate Stub', kind: 'game', category: 'game',
    executable: win(exe), arguments: ['--zf-text-sink', win(sinkLog), '60', '--data-dir', win(`${dir}/sink-data`)] }] }, null, 2));
  const grab = `${dir}/end.png`;
  const script = ['full', ...rep('wait', 10), 'ENTER', 'wait', 'pause=3000', 'ps', 'pause=3000', 'wait', 'wait',
    ...rep('DOWN', 8), 'wait', 'ENTER', 'wait', 'wait', ...tail, 'pause=2500', 'wait', `grab=${grab}`, 'quit'];
  const log = `${dir}/xmb.log`;
  const p = Bun.spawn([exe, '--dev-flash', flash, '--data-dir', `${dir}/data`, '--focus-time', '2000', '--script', script.join(',')],
    { stdout: 'ignore', stderr: 'pipe', env: { ...process.env, XMB_SINGLE_INSTANCE: '0', XMB_STEAM_ROOT: `${dir}/no-steam`, XMB_NO_GAMEBOOT: '1', XMB_PS_LOG: '1', XMB_LOG: log } });
  const stderr = new Response(p.stderr).text();
  const timer = setTimeout(() => p.kill(), 120000);
  await p.exited; clearTimeout(timer); await stderr;
  const lines = existsSync(sinkLog) ? readFileSync(sinkLog, 'utf8').split('\n').filter(Boolean).map(l => JSON.parse(l)) : [];
  const ready = lines.find(l => l.e === 'ready');
  const snaps = lines.filter(l => l.e === 'snap' || l.e === 'done');
  const received: string = snaps.length ? snaps[snaps.length - 1].received : '';
  if (ready) Bun.spawnSync(['taskkill', '/PID', String(ready.pid), '/F'], { stdout: 'ignore', stderr: 'ignore' });   // the sink this run started, by pid
  const text = existsSync(log) ? readFileSync(log, 'utf8') : '';
  const side = existsSync(`${grab}.json`) ? JSON.parse(readFileSync(`${grab}.json`, 'utf8')) : {};
  return { text, received, ready, side };
}

// A: Cross on Enter Text, 'g' (default focus), RIGHT, 'h', then down to the Enter key (row 5, column 8) and Cross.
const a = await runCase('confirm', ['ENTER', 'RIGHT', 'ENTER', ...rep('DOWN', 3), 'RIGHT', 'RIGHT', 'RIGHT', 'ENTER']);
check('A: the title (sink) started and is the tracked window', !!a.ready);
check('A: the in-game row exists', /ingame: row ingame:quit inserted in game/.test(a.text));
check('A: Cross on Enter Text opened the keyboard', /ingame: Enter Text opens the keyboard/.test(a.text));
check('A: delivery ran and ended with verdict deliver, 2 units', /textentry: finished verdict=deliver unitsSent=2/.test(a.text), (a.text.match(/textentry: [^\n]*/g) || []).join(' | '));
check('A: the sink received exactly the typed text', a.received === 'gh', `received ${JSON.stringify(a.received.length)} chars`);
check('A: no log line carries the typed text', !/\bgh\b/i.test(a.text.split('\n').filter(l => !l.startsWith('textentry:')).join('\n')) && !/text=|typed/i.test(a.text.match(/textentry:[^\n]*/g)?.join('') || ''));

// B: open the keyboard, type 'g', cancel with Circle (ESC).
const b = await runCase('cancel', ['ENTER', 'ESC']);
check('B: Enter Text opened the keyboard', /ingame: Enter Text opens the keyboard/.test(b.text));
check('B: cancel sent nothing (no textentry line, sink received nothing)', !/textentry:/.test(b.text) && b.received === '', `received ${b.received.length} chars`);
check('B: back in the in-game XMB after cancel', b.side.inGame === true, `inGame=${b.side.inGame}`);
console.log(failures ? `\n${failures} FAILED` : '\nall passed');
process.exit(failures ? 1 : 0);
