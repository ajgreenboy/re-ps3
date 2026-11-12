// ZF6 / P6 end to end (audit/WEB-APPS.md): a web app launched from the XMB against a scratch data dir and a local Bun server,
// then ended three ways (window closed, browser process killed by PID, Alt-F4), each for a normally launched browser and for a
// stale one (a browser the test itself started on the same scratch profile before the XMB ran, which the XMB must adopt).
// Only processes whose command line names the scratch profile are ever touched.
//   bun work/zf6-web-test.ts [--exe C:/dev/out/p6b-build/re-ps3-native.exe] [--only normal-close,stale-kill,...]
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const arg = (name: string, fallback: string) => { const i = process.argv.indexOf(name); return i >= 0 ? process.argv[i + 1] : fallback; };
const exe = arg('--exe', 'C:/dev/out/p6b-build/re-ps3-native.exe');
const only = arg('--only', '').split(',').filter(Boolean);
const out = 'C:/dev/out/p6b/zf6';
const flash = 'C:/dev/rpcs3/bin/dev_flash';
const seed = 'C:/dev/xmb/out/native-reference/v2/game';   // read-only data seed
const edge = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(existsSync)!;
const win = (s: string) => s.split('/').join(String.fromCharCode(92));
const port = 18771;
let failures = 0;
const check = (label: string, ok: boolean, detail = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? '  ' + detail : ''}`); if (!ok) failures++; };
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const rep = (t: string, n: number) => Array(n).fill(t);

const server = Bun.serve({ port, hostname: '127.0.0.1', fetch: req => new Response(`<title>zf6 ${new URL(req.url).pathname}</title><body style="background:#123;color:#fff;font:40px sans-serif">zf6 ${new URL(req.url).pathname}</body>`, { headers: { 'content-type': 'text/html' } }) });

function ps(script: string): string {
  const r = Bun.spawnSync(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], { stdout: 'pipe', stderr: 'pipe' });
  return r.stdout.toString();
}
// Browser processes (all of them, children included) that name exactly this scratch profile.
function profilePids(profile: string): { pid: number; root: boolean }[] {
  const needle = win(profile).toLowerCase();
  const text = ps(`Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | ForEach-Object { "$($_.ProcessId)|$($_.CommandLine)" }`);
  return text.split('\n').map(l => l.trim()).filter(Boolean).flatMap(l => {
    const i = l.indexOf('|'); const cmd = l.slice(i + 1);
    if (!cmd.toLowerCase().includes(needle)) return [];
    return [{ pid: Number(l.slice(0, i)), root: !cmd.includes('--type=') }];
  });
}
const killProfile = (profile: string) => { for (const p of profilePids(profile)) Bun.spawnSync(['taskkill', '/PID', String(p.pid), '/F'], { stdout: 'ignore', stderr: 'ignore' }); };
const fgInTree = (rootPid: number) => `
Add-Type -Namespace W -Name F -MemberDefinition '[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);'
$h=[W.F]::GetForegroundWindow(); $fp=0; [void][W.F]::GetWindowThreadProcessId($h,[ref]$fp)
$all=Get-CimInstance Win32_Process; $set=@(${rootPid}); $grow=$true
while($grow){$grow=$false; foreach($p in $all){ if(($set -contains [int]$p.ParentProcessId) -and -not ($set -contains [int]$p.ProcessId)){$set+= [int]$p.ProcessId;$grow=$true} } }
if($set -contains [int]$fp){ "fg-in-tree" } else { "fg-other:$fp" }`;

async function waitFor(pred: () => boolean, ms: number) { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (pred()) return true; await sleep(150); } return false; }

async function runCase(name: string, stale: boolean, how: 'close' | 'kill' | 'altf4') {
  const dir = `${out}/${name}`;
  killProfile(`${dir}/data/browser-profile`);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(`${dir}/data`, { recursive: true });
  mkdirSync(`${dir}/no-steam`, { recursive: true });
  for (const f of ['preferences.json', 'profiles.json', 'settings.ini']) cpSync(`${seed}/${f}`, `${dir}/data/${f}`);
  writeFileSync(`${dir}/data/apps.json`, JSON.stringify({ version: 1, apps: [{ id: 'zf6web', title: 'ZF6 Web', kind: 'web', category: 'game', url: `http://127.0.0.1:${port}/new` }] }, null, 2));
  const profile = `${dir}/data/browser-profile`;
  let staleRoot = 0;
  if (stale) {
    mkdirSync(profile, { recursive: true });
    Bun.spawn([edge, `--user-data-dir=${win(profile)}`, '--no-first-run', '--no-default-browser-check', '--start-maximized', `--app=http://127.0.0.1:${port}/old`], { stdout: 'ignore', stderr: 'ignore' });
    await waitFor(() => profilePids(profile).some(p => p.root), 20000);
    await sleep(2500);
    staleRoot = profilePids(profile).find(p => p.root)?.pid ?? 0;
    check(`${name}: stale browser is running on the scratch profile`, staleRoot > 0, `pid ${staleRoot}`);
  }
  const log = `${dir}/xmb.log`;
  const script = ['full', ...rep('wait', 10), 'ENTER', 'pause=40000', 'wait', `grab=${dir}/end.png`, 'quit'];
  const p = Bun.spawn([exe, '--dev-flash', flash, '--data-dir', `${dir}/data`, '--focus-time', '2000', '--script', script.join(',')],
    { stdout: 'ignore', stderr: 'pipe', env: { ...process.env, XMB_SINGLE_INSTANCE: '0', XMB_STEAM_ROOT: `${dir}/no-steam`, XMB_NO_GAMEBOOT: '1', XMB_PS_LOG: '1', XMB_LOG: log } });
  const stderr = new Response(p.stderr).text();
  const timer = setTimeout(() => p.kill(), 120000);
  const text = () => existsSync(log) ? readFileSync(log, 'utf8') : '';
  const running = await waitFor(() => /title: render paused/.test(text()) || /title: session begin/.test(text()), 30000);
  check(`${name}: the XMB tracks the web app as the running title`, running);
  await sleep(4000);   // let the browser window settle and the XMB lose the foreground
  const root = profilePids(profile).filter(x => x.root);
  const rootPid = stale ? staleRoot : (root[0]?.pid ?? 0);
  check(`${name}: tracked browser is ${stale ? 'the stale pid (no second browser)' : 'the one the XMB started'}`, rootPid > 0 && (!stale || root.length === 1 && root[0].pid === staleRoot), `roots=${root.map(r => r.pid).join(',')}`);
  if (stale) check(`${name}: log shows adoption`, new RegExp(`adopting stale browser pid=${staleRoot}`).test(text()));
  const fg = ps(fgInTree(rootPid)).trim();
  console.log(`      foreground after launch: ${fg}`);
  // A stale browser still shows its old window as well as the newly forwarded one, so a stale run closes twice (one window each).
  const rounds = stale && how !== 'kill' ? 2 : 1;
  for (let round = 0; round < rounds; round++) {
  if (round) await sleep(2000);
  // After the first Alt-F4 the XMB (fullscreen) takes the foreground, so the second (old) window of a stale browser is closed by message.
  if (how === 'close' || (how === 'altf4' && round > 0)) ps(`$p=Get-Process -Id ${rootPid} -ErrorAction SilentlyContinue; if($p){[void]$p.CloseMainWindow()}`);
  else if (how === 'kill') Bun.spawnSync(['taskkill', '/PID', String(rootPid), '/F'], { stdout: 'ignore', stderr: 'ignore' });
  else {
    const guard = ps(fgInTree(rootPid)).trim();
    if (guard === 'fg-in-tree') ps(`Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.SendKeys]::SendWait('%{F4}')`);
    else { console.log(`      Alt-F4 skipped, foreground was not our browser (${guard})`); if (!round) check(`${name}: Alt-F4 reached our browser`, false); }
  }
  }
  const back = await waitFor(() => /returned from ZF6 Web/.test(text()), 15000);
  check(`${name}: the XMB saw the web app end`, back);
  await waitFor(() => /title: XMB foreground\+focus after/.test(text()), 6000);
  const t = text();
  check(`${name}: XMB regained foreground and focus`, /title: XMB foreground\+focus after/.test(t), (t.match(/title: XMB foreground\+focus after[^\n]*/) || [''])[0]);
  check(`${name}: launching row still selected`, /title: selection at return=([^ ]+) launch=\1 kept=1/.test(t), (t.match(/title: selection at return[^\n]*/) || [''])[0]);
  await waitFor(() => existsSync(`${dir}/end.png.json`), 60000);
  await p.exited; clearTimeout(timer); await stderr;
  const side = existsSync(`${dir}/end.png.json`) ? JSON.parse(readFileSync(`${dir}/end.png.json`, 'utf8')) : {};
  check(`${name}: at the end titleRunning is false`, side.titleRunning === false, `titleRunning=${side.titleRunning}`);
  for (const l of t.split('\n').filter(l => /^(web:|launch:|returned from|title: (session begin|render paused|XMB foreground|selection at return|restore end))/.test(l))) console.log('      ' + l.slice(0, 260));
  const left = profilePids(profile);
  console.log(`      scratch browser processes left: ${left.length}`);
  killProfile(profile);
}

const cases: [string, boolean, 'close' | 'kill' | 'altf4'][] = [
  ['normal-close', false, 'close'], ['normal-kill', false, 'kill'], ['normal-altf4', false, 'altf4'],
  ['stale-close', true, 'close'], ['stale-kill', true, 'kill'], ['stale-altf4', true, 'altf4'],
];
for (const [name, stale, how] of cases) if (!only.length || only.includes(name)) { console.log(`\n== ${name}`); await runCase(name, stale, how); }
server.stop(true);
console.log(failures ? `\n${failures} FAILED` : '\nall passed');
process.exit(failures ? 1 : 0);
