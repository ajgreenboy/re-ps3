// Desktop Mode + single instance checks (PHASES-HTPC D1/D2, native-xmb/audit/DESKTOP-MODE.md).
//
//   bun work/desktop-mode-test.ts --exe C:/dev/out/d12-build/re-ps3-native.exe --mode windowed|kiosk [--display 1]
//   bun work/desktop-mode-test.ts --exe ... --mode script        # Guide button path through --script tokens
//
// Interactive modes start ONE XMB (scratch data directory under C:/dev/out/d12, XMB_LOG to a file in there) and drive it
// from second processes (`--desktop`, `--show`, `--quit`) and one real Ctrl+Alt+Home through SendInput. CPU time of the
// started PID is sampled with GetProcessTimes while the XMB is shown and while it is hidden. Only PIDs this script started
// are ever stopped. The hotkey is registered by the XMB process only while it is hidden and dies with the process.
import { dlopen, FFIType, ptr } from 'bun:ffi';
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = (name: string, fallback?: string) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const exe = opt('exe', 'C:/dev/out/d12-build/re-ps3-native.exe')!;
const mode = opt('mode', 'windowed')!;
const display = opt('display', '1')!;
const flash = 'C:/dev/rpcs3/bin/dev_flash';
const root = 'C:/dev/out/d12';
const dataDir = `${root}/data-${mode}`;
const logFile = `${root}/${mode}.log`;
mkdirSync(dataDir, { recursive: true });
rmSync(logFile, { force: true });

const k32 = dlopen('kernel32.dll', {
  OpenProcess: { args: [FFIType.u32, FFIType.i32, FFIType.u32], returns: FFIType.ptr },
  GetProcessTimes: { args: [FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
  CloseHandle: { args: [FFIType.ptr], returns: FFIType.i32 },
});
const u32 = dlopen('user32.dll', {
  SendInput: { args: [FFIType.u32, FFIType.ptr, FFIType.i32], returns: FFIType.u32 },
  GetForegroundWindow: { args: [], returns: FFIType.ptr },
  GetWindowThreadProcessId: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.u32 },
  GetWindowRect: { args: [FFIType.ptr, FFIType.ptr], returns: FFIType.i32 },
  IsWindowVisible: { args: [FFIType.ptr], returns: FFIType.i32 },
});

function cpuSeconds(pid: number): number {
  const h = k32.symbols.OpenProcess(0x1000, 0, pid);
  if (!h) return NaN;
  const b = new BigUint64Array(4);
  k32.symbols.GetProcessTimes(h, ptr(b), ptr(b.subarray(1)), ptr(b.subarray(2)), ptr(b.subarray(3)));
  k32.symbols.CloseHandle(h);
  return Number(b[2] + b[3]) / 1e7;   // kernel + user, 100 ns units
}
async function sample(pid: number, seconds: number, label: string) {
  const a = cpuSeconds(pid);
  await sleep(seconds * 1000);
  const b = cpuSeconds(pid);
  const pct = ((b - a) / seconds) * 100;
  console.log(`CPU ${label}: ${(b - a).toFixed(3)} s of CPU in ${seconds} s = ${pct.toFixed(2)} % of one core`);
  return pct;
}
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
const log = () => (existsSync(logFile) ? readFileSync(logFile, 'utf8') : '');
async function waitLog(re: RegExp, timeoutMs: number, from = 0): Promise<string | null> {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const m = log().slice(from).split('\n').find(l => re.test(l));
    if (m) return m;
    await sleep(100);
  }
  return null;
}
function foreground() {
  const hwnd = u32.symbols.GetForegroundWindow();
  const pid = new Uint32Array(1);
  u32.symbols.GetWindowThreadProcessId(hwnd, ptr(pid));
  const r = new Int32Array(4);
  u32.symbols.GetWindowRect(hwnd, ptr(r));
  return { pid: pid[0], rect: `${r[2] - r[0]}x${r[3] - r[1]}+${r[0]}+${r[1]}` };
}
function pressHotkey() {
  // Ctrl+Alt+Home. Each INPUT is 40 bytes on x64: type(4)+pad(4)+KEYBDINPUT{wVk(2),wScan(2),dwFlags(4),time(4),pad(4),dwExtraInfo(8)}.
  const vks: [number, number][] = [[0x11, 0], [0x12, 0], [0x24, 0x1], [0x24, 0x3], [0x12, 0x2], [0x11, 0x2]];
  // flags: 0 = down, 2 = up, 1 = extended key (Home is the dedicated extended key); 3 = extended up
  const buf = new ArrayBuffer(40 * vks.length);
  const dv = new DataView(buf);
  vks.forEach(([vk, flags], i) => { dv.setUint32(i * 40, 1, true); dv.setUint16(i * 40 + 8, vk, true); dv.setUint32(i * 40 + 12, flags, true); });
  return u32.symbols.SendInput(vks.length, ptr(new Uint8Array(buf)), 40);
}

const base = ['--dev-flash', flash, '--data-dir', dataDir, '--no-intro'];
const modeArgs = mode === 'kiosk' ? ['--kiosk', '--display', display] : ['--start-windowed'];
const env = { ...process.env, XMB_LOG: logFile, XMB_PS_LOG: '1' };
delete (env as any).XMB_SINGLE_INSTANCE;

function second(extra: string[]) {
  // The second process: same data directory, no kiosk flags needed. It must exit 0 on its own.
  const r = spawnSync(exe, [...base, ...extra], { env, timeout: 20000 });
  return r.status;
}
let failures = 0;
function check(name: string, ok: boolean, detail = '') { console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`); if (!ok) failures++; }

async function interactive() {
  const child = spawn(exe, [...base, ...modeArgs], { env, stdio: 'ignore' });
  const pid = child.pid!;
  let exitCode: number | null = null;
  child.on('exit', c => { exitCode = c; });
  console.log(`started XMB pid ${pid} (${mode}); log ${logFile}`);
  try {
    check('XMB started and listens', !!(await waitLog(/instance: listening on/, 60000)));
    check('window up', !!(await waitLog(/native display refresh/, 60000)));
    await sleep(4000);
    const visible = await sample(pid, 10, 'visible (XMB showing)');

    // 1. second launch, plain: nothing to leave, brings the running XMB to the front and exits 0
    check('plain second launch exits 0', second([]) === 0);
    check('plain second launch reached the running XMB', !!(await waitLog(/instance: command show received/, 8000)));

    // 2. --desktop from a second process
    const mark = log().length;
    check('--desktop exits 0', second(['--desktop']) === 0);
    const enter = await waitLog(/desktop: enter via second launch --desktop/, 8000, mark);
    check('desktop: enter logged', !!enter, enter ?? '');
    await sleep(3000);
    const hidden = await sample(pid, 10, 'hidden (Desktop Mode)');
    const idle = log().split('\n').filter(l => l.startsWith('desktop: idle frames=')).slice(-2);
    idle.forEach(l => console.log('  ' + l));
    check('no frames rendered while hidden', idle.length > 0 && idle.every(l => /frames=0 /.test(l)));
    check('hidden CPU is near idle', hidden < 1.0, `${hidden.toFixed(2)} % (visible ${visible.toFixed(2)} %)`);
    check('hotkey registered while hidden', /desktop: hotkey Ctrl\+Alt\+Home registered/.test(log()));
    console.log(`  foreground while hidden: pid ${foreground().pid} (XMB pid ${pid})`);
    check('XMB is not the foreground while hidden', foreground().pid !== pid);

    // 3. way back 1: the real global hotkey
    const m3 = log().length;
    const sent = pressHotkey();
    check('hotkey keystrokes sent', sent === 6, `${sent}/6`);
    const back = await waitLog(/desktop: return via hotkey/, 8000, m3);
    check('return via hotkey logged', !!back, back ?? '');
    const fg = await waitLog(/desktop: XMB foreground\+focus after/, 8000, m3);
    check('foreground + focus after hotkey return', !!fg, fg ?? '');
    check('hotkey unregistered after return', !!(await waitLog(/desktop: hotkey unregistered/, 2000, m3)));
    const shown = (await waitLog(/desktop: window shown/, 2000, m3)) ?? '';
    console.log('  ' + shown);
    check('selection kept', /kept=1/.test(shown));
    const f = foreground();
    console.log(`  foreground now: pid ${f.pid} rect ${f.rect}`);
    check('XMB is the foreground window', f.pid === pid);
    if (mode === 'kiosk') check('kiosk window is back fullscreen on its display', /fullscreen=1/.test(shown) && !/geometry=960x540/.test(shown));

    // 4. way back 2: second launch (--show)
    second(['--desktop']);
    await waitLog(/desktop: enter via second launch --desktop/, 8000, log().length - 1);
    await sleep(1500);
    const m4 = log().length;
    check('--show exits 0', second(['--show']) === 0);
    check('return via second launch logged', !!(await waitLog(/desktop: return via second launch/, 8000, m4)));
    check('foreground + focus after --show return', !!(await waitLog(/desktop: XMB foreground\+focus after/, 8000, m4)));
    check('XMB is the foreground window again', foreground().pid === pid, JSON.stringify(foreground()));

    // 5. --quit while hidden: exit code 0 (the watchdog would stop)
    second(['--desktop']);
    await sleep(2500);
    const m5 = log().length;
    check('--quit exits 0', second(['--quit']) === 0);
    const t0 = Date.now();
    while (exitCode === null && Date.now() - t0 < 10000) await sleep(100);
    check('XMB exited with code 0 on --quit', exitCode === 0, `code ${exitCode}`);
    check('quit logged', /instance: --quit from a second launch/.test(log().slice(m5)));
    check('--quit with nothing running exits 0', second(['--quit']) === 0);
  } finally {
    if (exitCode === null) { console.log(`stopping my own XMB pid ${pid}`); try { process.kill(pid); } catch {} }
  }
}

async function scripted() {
  // The Guide path through the global watcher's own state machine (the `guide` token injects the PS bit).
  // Users column: Turn Off(0) Restart(1) Sleep(2) Switch to Desktop(3) Exit to Desktop(4); the cursor starts on the user row.
  const up3 = 'UP,UP,UP';
  const script = ['LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT,LEFT', up3, 'ENTER', 'LEFT', 'ENTER',      // row -> confirm -> Yes
    'pause=6000', 'guide', 'pause=2500', 'quit'].join(',');
  const child = spawn(exe, [...base, ...(opt('kiosk') ? ['--kiosk', '--display', display] : ['--start-windowed']), '--script', script], { env, stdio: 'ignore' });
  const pid = child.pid!;
  let code: number | null = null; child.on('exit', c => { code = c; });
  const t0 = Date.now(); while (code === null && Date.now() - t0 < 60000) await sleep(200);
  check('scripted run ended 0', code === 0, `code ${code}`);
  const l = log();
  check('Switch to Desktop confirmed', /power: Switch to Desktop confirmed/.test(l));
  check('desktop: enter via Switch to Desktop row', /desktop: enter via Switch to Desktop row/.test(l));
  check('desktop: return via Guide button', /desktop: return via Guide button/.test(l));
  check('selection kept', /desktop: window shown .* kept=1/.test(l));
  void pid;
}

if (mode === 'script') await scripted(); else await interactive();
console.log('--- log lines (desktop:/instance:) ---');
log().split('\n').filter(l => /^(desktop:|instance:|power: (Switch|Exit))/.test(l)).forEach(l => console.log(l));
console.log(failures ? `FAILURES: ${failures}` : 'ALL PASS');
process.exit(failures ? 1 : 0);
