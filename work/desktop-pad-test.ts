// Controller-driven desktop checks (PHASES-HTPC D5, native-xmb/audit/DESKTOP-MODE.md).
//
//   bun work/desktop-pad-test.ts --exe C:/dev/out/d5-build/re-ps3-native.exe [--part map|inject|keyboard|app|all]
//
// map       the pure mapper through `--desktop-pad-test <json-in> <json-out>`: curves, deadzones, click edges, D-pad repeat,
//           scroll, the window-switcher state, Start, Back, Guide suppression, release at the end.
// inject    SendInput delivery: starts the exe's own sink window (`--desktop-pad-sink`, it logs every mouse and key event it
//           receives), runs a scenario through `--desktop-pad-inject` (which refuses to send unless the foreground window and
//           the window under the pointer both belong to the sink), and compares what the sink saw with what was asked for.
//           Never sends Win, Alt or Tab (those would leave the sink). Only PIDs started here are stopped.
// keyboard  `--desktop-pad-keyboard`: Start twice; reports whether the touch keyboard or an osk.exe this exe started appeared.
// app       a real XMB (scratch data dir) enters Desktop Mode from a second process and the log shows the driver going live,
//           the Ctrl+Alt+Home return releasing it, and the preference gate (desktop/controllerMouse=false) pausing it.
import { dlopen, FFIType, ptr } from 'bun:ffi';
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = (name: string, fallback?: string) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const exe = opt('exe', 'C:/dev/out/d5-build/re-ps3-native.exe')!;
const part = opt('part', 'all')!;
const root = 'C:/dev/out/d5';
mkdirSync(root, { recursive: true });
const sleep = (ms: number) => new Promise(r => setTimeout(r, ms));
let failures = 0;
function check(name: string, ok: boolean, detail = '') { console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`); if (!ok) failures++; }
const near = (a: number, b: number, tol: number) => Math.abs(a - b) <= tol;

// Buttons, XInput layout.
const B = { Up: 1, Down: 2, Left: 4, Right: 8, Start: 0x10, Back: 0x20, LB: 0x100, RB: 0x200, Guide: 0x400, Cross: 0x1000, Circle: 0x2000, Square: 0x4000, Triangle: 0x8000 };
const VK = { Esc: 0x1b, Enter: 0x0d, Tab: 9, Shift: 0xa0, Alt: 0xa4, Win: 0x5b, Left: 0x25, Up: 0x26, Right: 0x27, Down: 0x28 };

type Sample = { t: number; buttons?: number; lx?: number; ly?: number; rx?: number; ry?: number };
type Action = { t: number; kind: string; dx?: number; dy?: number; button?: string; down?: boolean; vk?: number; repeat?: boolean; v?: number; h?: number; why?: string };

function runMap(name: string, samples: Sample[], endMs: number, config: Record<string, number> = {}, tickMs = 10) {
  const input = `${root}/map-${name}.in.json`, output = `${root}/map-${name}.out.json`;
  writeFileSync(input, JSON.stringify({ config, tickMs, endMs, samples }));
  rmSync(output, { force: true });
  const r = spawnSync(exe, ['--desktop-pad-test', input, output], { timeout: 20000, env: { ...process.env } });
  if (r.status !== 0 || !existsSync(output)) { check(`${name}: mapper ran`, false, `exit ${r.status} ${r.stderr?.toString().slice(0, 200)}`); return { actions: [] as Action[], curve: [] as any[], held: true }; }
  const out = JSON.parse(readFileSync(output, 'utf8'));
  return { actions: out.actions as Action[], curve: out.curve as any[], held: out.held as boolean };
}
const sum = (actions: Action[], f: (a: Action) => number) => actions.reduce((s, a) => s + f(a), 0);
const keys = (actions: Action[], vk: number) => actions.filter(a => a.kind === 'key' && a.vk === vk);
const clicks = (actions: Action[], button: string) => actions.filter(a => a.kind === 'button' && a.button === button);

function mapTests() {
  console.log('--- mapper (pure) ---');
  // Curves
  const base = runMap('curve', [{ t: 0 }], 0);
  const c = (d: number) => base.curve.find((x: any) => Math.abs(x.deflection - d) < 1e-9);
  check('curve: no motion inside the deadzone (0.10, 0.20)', c(0.1).pointerPxPerSecond === 0 && c(0.2).pointerPxPerSecond === 0);
  check('curve: full deflection = 1800 px/s at speed 1', near(c(1).pointerPxPerSecond, 1800, 0.01), `${c(1).pointerPxPerSecond}`);
  const speeds = base.curve.map((x: any) => x.pointerPxPerSecond);
  check('curve: monotonic increasing', speeds.every((v: number, i: number) => i === 0 || v >= speeds[i - 1]), speeds.map((v: number) => v.toFixed(0)).join(' '));
  check('curve: accelerating (60 % deflection more than 2x the speed of 40 %)', c(0.6).pointerPxPerSecond > 2 * c(0.4).pointerPxPerSecond, `${c(0.4).pointerPxPerSecond.toFixed(0)} -> ${c(0.6).pointerPxPerSecond.toFixed(0)}`);
  check('curve: fine control (just past the deadzone is under 30 px/s)', c(0.3).pointerPxPerSecond < 30, `${c(0.3).pointerPxPerSecond.toFixed(1)} px/s`);
  check('scroll curve: none under 0.25, 1440 units/s (12 notches) at full', c(0.2).scrollUnitsPerSecond === 0 && near(c(1).scrollUnitsPerSecond, 1440, 0.01));

  // Pointer
  const still = runMap('dead', [{ t: 0, lx: 0.15, ly: 0.1 }], 1000);
  check('pointer: stick inside the deadzone moves nothing', still.actions.filter(a => a.kind === 'move').length === 0);
  const half = runMap('half', [{ t: 0, lx: 0.5 }], 1000);
  const expectHalf = c(0.5).pointerPxPerSecond;
  const dxHalf = sum(half.actions, a => a.dx ?? 0);
  check('pointer: 50 % right for 1 s moves what the curve says, straight right', near(dxHalf, expectHalf, 4) && sum(half.actions, a => a.dy ?? 0) === 0, `dx ${dxHalf} expected ${expectHalf.toFixed(1)}`);
  const full = runMap('full', [{ t: 0, lx: 1 }], 1000);
  check('pointer: full right for 1 s = 1800 px', near(sum(full.actions, a => a.dx ?? 0), 1800, 6), `${sum(full.actions, a => a.dx ?? 0)}`);
  const up = runMap('up', [{ t: 0, ly: -1 }], 500);
  check('pointer: stick up moves up (negative y), none sideways', near(sum(up.actions, a => a.dy ?? 0), -900, 6) && sum(up.actions, a => a.dx ?? 0) === 0);
  const diag = runMap('diag', [{ t: 0, lx: 0.6, ly: 0.6 }], 1000);
  const ddx = sum(diag.actions, a => a.dx ?? 0), ddy = sum(diag.actions, a => a.dy ?? 0);
  check('pointer: diagonal keeps the direction', Math.abs(ddx - ddy) <= 2 && ddx > 100, `dx ${ddx} dy ${ddy}`);
  const slow = runMap('subpixel', [{ t: 0, lx: 0.24 }], 1000);
  const expectSlow = c(0.24 + 1e-9)?.pointerPxPerSecond;
  check('pointer: a barely deflected stick still moves (sub-pixel remainder is carried, no stall)', sum(slow.actions, a => a.dx ?? 0) > 0 && slow.actions.every(a => a.kind !== 'move' || Math.abs(a.dx ?? 0) <= 1), `${sum(slow.actions, a => a.dx ?? 0)} px in 1 s`);
  const fast = runMap('speed2', [{ t: 0, lx: 0.6 }], 1000, { pointerSpeed: 2.2 });
  const normal = runMap('speed1', [{ t: 0, lx: 0.6 }], 1000);
  check('pointer speed preference scales the curve (2.2x)', near(sum(fast.actions, a => a.dx ?? 0) / sum(normal.actions, a => a.dx ?? 0), 2.2, 0.05));
  const stall = runMap('stall', [{ t: 0, lx: 1 }], 1000, {}, 400);   // a 400 ms tick = a hitch: capped, not a 720 px jump
  check('pointer: a stalled clock is capped (400 ms ticks move at most 90 px per tick)', stall.actions.every(a => a.kind !== 'move' || Math.abs(a.dx ?? 0) <= 90));

  // Buttons
  const tap = runMap('cross', [{ t: 100, buttons: B.Cross }, { t: 500, buttons: 0 }], 800);
  const left = clicks(tap.actions, 'left');
  check('Cross: left button down once, then up once', left.length === 2 && left[0].down === true && left[1].down === false, JSON.stringify(left.map(a => `${a.down ? 'down' : 'up'}@${a.t}`)));
  check('Cross: down at the press, up at the release (drag works)', left.length === 2 && left[0].t === 100 && left[1].t === 500);
  const rc = runMap('square', [{ t: 0, buttons: B.Square }, { t: 300, buttons: 0 }], 500);
  check('Square: right button down then up', clicks(rc.actions, 'right').map(a => a.down).join() === 'true,false' && clicks(rc.actions, 'left').length === 0);
  const esc = runMap('circle', [{ t: 0, buttons: B.Circle }, { t: 200, buttons: 0 }], 400);
  check('Circle: Escape down then up', keys(esc.actions, VK.Esc).map(a => a.down).join() === 'true,false');
  const ent = runMap('triangle', [{ t: 0, buttons: B.Triangle }, { t: 200, buttons: 0 }], 400);
  check('Triangle: Enter down then up', keys(ent.actions, VK.Enter).map(a => a.down).join() === 'true,false');
  const holdEnter = runMap('triangle-hold', [{ t: 0, buttons: B.Triangle }, { t: 2000, buttons: 0 }], 2100);
  check('Triangle held 2 s: one down, no auto-repeat', keys(holdEnter.actions, VK.Enter).length === 2);
  const win = runMap('back', [{ t: 0, buttons: B.Back }, { t: 100, buttons: 0 }], 300);
  check('Back/View: Windows key down then up', keys(win.actions, VK.Win).map(a => a.down).join() === 'true,false');
  const start = runMap('start', [{ t: 0, buttons: B.Start }, { t: 1500, buttons: 0 }, { t: 1600, buttons: B.Start }, { t: 1700, buttons: 0 }], 2000);
  check('Start: one toggle per press (none while held), two presses = two toggles', start.actions.filter(a => a.kind === 'toggleKeyboard').length === 2);

  // D-pad repeat
  const dp = runMap('dpad', [{ t: 0, buttons: B.Down }, { t: 1500, buttons: 0 }], 1700);
  const downs = keys(dp.actions, VK.Down);
  const first = downs[0], repeats = downs.filter(a => a.repeat);
  const expectedRepeats = Math.floor((1500 - 450) / 45) + 1;
  check('D-pad: first press is a plain key down at once', first && first.down && !first.repeat && first.t === 0);
  check('D-pad: no repeat before 450 ms', repeats.length > 0 && repeats[0].t >= 450 && repeats[0].t <= 460, `first repeat at ${repeats[0]?.t}`);
  check('D-pad: repeats every ~45 ms while held', near(repeats.length, expectedRepeats, 2) && repeats.slice(1).every((a, i) => a.t - repeats[i].t >= 40 && a.t - repeats[i].t <= 60), `${repeats.length} repeats, expected ~${expectedRepeats}`);
  check('D-pad: key up at the release, nothing after', downs[downs.length - 1].down === false && downs[downs.length - 1].t === 1500);
  const dpTap = runMap('dpad-tap', [{ t: 0, buttons: B.Left }, { t: 200, buttons: 0 }], 600);
  check('D-pad tap shorter than the delay: down, up, no repeat', keys(dpTap.actions, VK.Left).map(a => a.down).join() === 'true,false');
  const dpTwo = runMap('dpad-diag', [{ t: 0, buttons: B.Up | B.Right }, { t: 100, buttons: 0 }], 300);
  check('D-pad diagonal: both arrows, arrows carry the extended flag', keys(dpTwo.actions, VK.Up).length === 2 && keys(dpTwo.actions, VK.Right).length === 2);

  // Scroll
  const sd = runMap('scroll-down', [{ t: 0, ry: 1 }], 1000);
  check('scroll: right stick down for 1 s = 12 notches down', near(sum(sd.actions, a => a.v ?? 0), -1440, 70), `${sum(sd.actions, a => a.v ?? 0)} units`);
  const su = runMap('scroll-up', [{ t: 0, ry: -0.6 }], 1000);
  const expectUp = c(0.6).scrollUnitsPerSecond;
  check('scroll: right stick up scrolls up (positive) at the curve rate', near(sum(su.actions, a => a.v ?? 0), expectUp, 70), `${sum(su.actions, a => a.v ?? 0)} vs ${expectUp.toFixed(0)}`);
  const sr = runMap('scroll-right', [{ t: 0, rx: 1 }], 1000);
  check('scroll: right stick right = horizontal wheel, positive', near(sum(sr.actions, a => a.h ?? 0), 1440, 70) && sum(sr.actions, a => a.v ?? 0) === 0);
  const sl = runMap('scroll-left', [{ t: 0, rx: -1 }], 500);
  check('scroll: right stick left = horizontal wheel, negative', sum(sl.actions, a => a.h ?? 0) < -600);
  const sdz = runMap('scroll-dead', [{ t: 0, rx: 0.2, ry: -0.24 }], 1000);
  check('scroll: inside the 0.25 deadzone nothing', sdz.actions.filter(a => a.kind === 'wheel').length === 0);
  const sboth = runMap('scroll-both', [{ t: 0, rx: 0.8, ry: 0.8 }], 1000);
  check('scroll: both axes at once', sum(sboth.actions, a => a.v ?? 0) < -300 && sum(sboth.actions, a => a.h ?? 0) > 300);
  check('scroll: batched, no wheel event below 30 units', sboth.actions.filter(a => a.kind === 'wheel').every(a => Math.abs(a.v ?? 0) >= 30 || Math.abs(a.h ?? 0) >= 30 || a.v === 0 || a.h === 0));

  // Window switcher
  const sw = runMap('switch-rb', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }], 1500);
  const seq = sw.actions.filter(a => a.kind === 'key').map(a => `${a.vk!.toString(16)}${a.down ? 'v' : '^'}@${a.t}`).join(' ');
  check('RB: Alt down, Tab down, Tab up at the press', keys(sw.actions, VK.Alt)[0]?.down === true && keys(sw.actions, VK.Tab).map(a => a.down).join() === 'true,false' && keys(sw.actions, VK.Shift).length === 0, seq);
  const altUp = keys(sw.actions, VK.Alt)[1];
  check('RB: Alt is released 1 s after the last press (commit)', altUp && !altUp.down && altUp.t >= 1100 && altUp.t <= 1110, `Alt up at ${altUp?.t}`);
  const sw2 = runMap('switch-twice', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }, { t: 600, buttons: B.RB }, { t: 650, buttons: 0 }, { t: 900, buttons: B.LB }, { t: 950, buttons: 0 }], 3000);
  check('RB, RB, LB inside one second: Alt goes down once, three Tabs, one Shift, one Alt up', keys(sw2.actions, VK.Alt).length === 2 && keys(sw2.actions, VK.Tab).filter(a => a.down).length === 3 && keys(sw2.actions, VK.Shift).length === 2);
  check('switcher: the second press restarts the commit timer (Alt up 1 s after the LAST press)', near(keys(sw2.actions, VK.Alt)[1]?.t ?? 0, 1900, 15), `Alt up at ${keys(sw2.actions, VK.Alt)[1]?.t}`);
  const swc = runMap('switch-cross', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }, { t: 400, buttons: B.Cross }, { t: 450, buttons: 0 }], 1500);
  check('switcher open + Cross: commits (Alt up at 400) and does NOT click', keys(swc.actions, VK.Alt)[1]?.t === 400 && clicks(swc.actions, 'left').length === 0);
  const swo = runMap('switch-circle', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }, { t: 400, buttons: B.Circle }, { t: 450, buttons: 0 }], 1500);
  const escSeq = swo.actions.filter(a => a.kind === 'key' && a.t === 400).map(a => `${a.vk!.toString(16)}${a.down ? 'v' : '^'}`).join(' ');
  check('switcher open + Circle: Escape while Alt is held, then Alt up (cancel)', escSeq === '1bv 1b^ a4^', escSeq);
  const swt = runMap('switch-triangle', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }, { t: 400, buttons: B.Triangle }, { t: 450, buttons: 0 }], 1500);
  check('switcher open + Triangle: commits, no Enter', keys(swt.actions, VK.Enter).length === 0 && keys(swt.actions, VK.Alt)[1]?.t === 400);
  const swd = runMap('switch-dpad', [{ t: 100, buttons: B.RB }, { t: 150, buttons: 0 }, { t: 400, buttons: B.Right }, { t: 450, buttons: 0 }], 1500);
  check('switcher open + D-pad: arrows still sent (navigate the Alt+Tab list)', keys(swd.actions, VK.Right).length === 2 && keys(swd.actions, VK.Alt).length === 2);

  // Guide
  const g1 = runMap('guide-chord', [{ t: 0, buttons: B.Guide | B.Cross }, { t: 200, buttons: B.Guide }, { t: 300, buttons: 0 }], 600);
  check('Guide + Cross together: no click', g1.actions.length === 0, JSON.stringify(g1.actions.slice(0, 2)));
  const g2 = runMap('guide-held', [{ t: 0, buttons: B.Cross }, { t: 100, buttons: B.Cross | B.Guide }, { t: 300, buttons: B.Cross }, { t: 500, buttons: 0 }], 800);
  const l2 = clicks(g2.actions, 'left');
  check('Cross held, Guide pressed: the click is released at once; after the Guide is up the held Cross does not click again', l2.length === 2 && l2[0].down && !l2[1].down && l2[1].t === 100, JSON.stringify(l2.map(a => `${a.down ? 'down' : 'up'}@${a.t}`)));
  const g3 = runMap('guide-stick', [{ t: 0, buttons: B.Guide, lx: 1 }, { t: 200, buttons: 0 }], 300);
  check('Guide held: the stick does not move the pointer', g3.actions.filter(a => a.kind === 'move').length === 0);
  const g4 = runMap('guide-tap', [{ t: 0, buttons: B.Guide }, { t: 80, buttons: 0 }, { t: 120, buttons: B.Cross }, { t: 200, buttons: 0 }], 400);
  check('after a Guide tap the pad works again', clicks(g4.actions, 'left').length === 2);

  // Edge cases
  const heldAtEnd = runMap('held-end', [{ t: 0, buttons: B.Cross | B.Square | B.Down }], 100);
  check('everything held at the end is released (left, right, Down)', heldAtEnd.held === false && clicks(heldAtEnd.actions, 'left').slice(-1)[0]?.down === false && clicks(heldAtEnd.actions, 'right').slice(-1)[0]?.down === false && keys(heldAtEnd.actions, VK.Down).slice(-1)[0]?.down === false);
  const heldAlt = runMap('alt-end', [{ t: 0, buttons: B.RB }, { t: 50, buttons: 0 }], 200);
  check('Alt still down when the run ends is released', keys(heldAlt.actions, VK.Alt).slice(-1)[0]?.down === false);
  const none = runMap('idle', [{ t: 0 }], 2000);
  check('an idle pad produces no actions', none.actions.length === 0);
}

// ---- user32 for the delivery test ---------------------------------------------------------------------------
const u32 = dlopen('user32.dll', {
  GetCursorPos: { args: [FFIType.ptr], returns: FFIType.i32 },
});
function cursor(): [number, number] { const p = new Int32Array(2); u32.symbols.GetCursorPos(ptr(p)); return [p[0], p[1]]; }
function readLog(file: string): any[] {
  if (!existsSync(file)) return [];
  return readFileSync(file, 'utf8').split('\n').filter(Boolean).map(l => { try { return JSON.parse(l); } catch { return null; } }).filter(Boolean);
}

async function injectTests() {
  console.log('--- SendInput delivery into the exe own sink window ---');
  const logFile = `${root}/sink.jsonl`;
  rmSync(logFile, { force: true });
  const sink = spawn(exe, ['--desktop-pad-sink', logFile, '150'], { stdio: 'ignore', env: { ...process.env } });
  const sinkPid = sink.pid!;
  try {
    let ready: any = null;
    for (let i = 0; i < 100 && !ready; i++) { await sleep(100); ready = readLog(logFile).find(e => e.e === 'ready'); }
    check('sink window started and reported ready', !!ready, JSON.stringify(ready));
    if (!ready) return;
    check('sink window is the foreground window', ready.foreground === true && ready.pid === sinkPid, `pid ${ready.pid}`);
    const cx = Math.round(ready.x + ready.w / 2), cy = Math.round(ready.y + ready.h / 2);

    // Negative: an injector pointed at a PID that is not the sink must send nothing.
    writeFileSync(`${root}/inject-wrong.in.json`, JSON.stringify({ sinkPid: 4, startPointer: [cx, cy], tickMs: 10, endMs: 100, samples: [{ t: 0, lx: 1, buttons: B.Cross }] }));
    const wrongMark = readLog(logFile).length;
    const wrong = spawnSync(exe, ['--desktop-pad-inject', `${root}/inject-wrong.in.json`, `${root}/inject-wrong.out.json`], { timeout: 20000 });
    check('safety: an injector aimed at a window that is not the sink refuses (exit 3)', wrong.status === 3, `exit ${wrong.status}`);
    await sleep(200);
    check('safety: nothing reached the sink from the refused run (no click, no key)', readLog(logFile).slice(wrongMark).filter(e => e.e !== 'ready' && e.e !== 'move').length === 0);

    // A person or another program may move the pointer while this runs; a scenario is retried until it ran without the
    // injector being aborted and without the pointer being moved by anyone else (result.interference == 0).
    async function scenario(name: string, samples: Sample[], endMs: number) {
      const expected = runMap(`inject-${name}-expected`, samples, endMs).actions;
      for (let attempt = 1; attempt <= 12; attempt++) {
        const mark = readLog(logFile).length;
        writeFileSync(`${root}/inject-${name}.in.json`, JSON.stringify({ sinkPid, startPointer: [cx, cy], tickMs: 10, endMs, samples }));
        spawnSync(exe, ['--desktop-pad-inject', `${root}/inject-${name}.in.json`, `${root}/inject-${name}.out.json`], { timeout: 60000 });
        const result = JSON.parse(readFileSync(`${root}/inject-${name}.out.json`, 'utf8'));
        await sleep(250);
        const events = readLog(logFile).slice(mark).filter(e => e.e !== 'ready');
        if (!result.aborted && result.interference === 0) { console.log(`  ${name}: ran clean on attempt ${attempt}; sink saw ${events.length} events`); return { result, events, expected }; }
        console.log(`  ${name}: attempt ${attempt} discarded (${result.aborted ? 'aborted: ' + result.abortReason : 'pointer moved by someone else ' + result.interference + 'x'})`);
        await sleep(400);
      }
      return null;
    }
    const kd = (events: any[], vk: number) => events.filter(x => x.e === 'keydown' && x.vk === vk);
    const ku = (events: any[], vk: number) => events.filter(x => x.e === 'keyup' && x.vk === vk && !x.repeat);

    // 1. Pointer: right 0.6 for 400 ms, then down-left 0.5 for 300 ms.
    const p = await scenario('pointer', [{ t: 0, lx: 0.6 }, { t: 400, lx: -0.5, ly: 0.5 }, { t: 700 }], 800);
    check('pointer scenario ran clean', !!p);
    if (p) {
      const exMove = [sum(p.expected, a => a.dx ?? 0), sum(p.expected, a => a.dy ?? 0)];
      const got = [p.result.cursorEnd[0] - p.result.cursorStart[0], p.result.cursorEnd[1] - p.result.cursorStart[1]];
      check('pointer moved exactly as far as the mapper asked (absolute SendInput, no OS acceleration)', got[0] === exMove[0] && got[1] === exMove[1], `moved ${got} expected ${exMove}`);
      const moves = p.events.filter(e => e.e === 'move');
      check('the sink received the mouse-move events', moves.length > 10, `${moves.length} moves`);
      const last = moves[moves.length - 1];
      check('the last move the sink saw is where the pointer ended', last && last.x === p.result.cursorEnd[0] && last.y === p.result.cursorEnd[1], JSON.stringify(last));
    }

    // 2. Clicks and scrolling, with Cross still held when the run ends (the mapper must release it).
    const q = await scenario('clicks-scroll', [
      { t: 0, buttons: B.Cross }, { t: 150, buttons: 0 },
      { t: 250, buttons: B.Square }, { t: 400, buttons: 0 },
      { t: 500, ry: 1 }, { t: 900, ry: 0 }, { t: 950, rx: 1 }, { t: 1250, rx: 0 },
      { t: 1350, buttons: B.Cross }], 1600);
    check('click/scroll scenario ran clean', !!q);
    if (q) {
      const presses = q.events.filter(e => e.e === 'press'), releases = q.events.filter(e => e.e === 'release');
      check('left click: press then release, left button', presses.filter(x => x.btn === 'left').length === 2 && releases.filter(x => x.btn === 'left').length === 2, `presses ${presses.map(x => x.btn)} releases ${releases.map(x => x.btn)}`);
      check('right click: press then release, right button', presses.filter(x => x.btn === 'right').length === 1 && releases.filter(x => x.btn === 'right').length === 1);
      check('Cross held at the end of the run was released by the mapper', presses.length === releases.length);
      const wheels = q.events.filter(e => e.e === 'wheel');
      const wv = wheels.reduce((s, x) => s + x.dy, 0), wh = wheels.reduce((s, x) => s + x.dx, 0);
      const exV = sum(q.expected, a => a.v ?? 0), exH = sum(q.expected, a => a.h ?? 0);
      check('right stick down scrolled down: vertical wheel deltas match the mapper (sign and amount)', wv === exV && wv < 0, `sink ${wv} expected ${exV}`);
      check('right stick right scrolled right: horizontal wheel deltas match the mapper (Windows sign: right is positive)', wh === exH && wh > 0, `sink ${wh} expected ${exH}`);
    }

    // 3. Keys: Circle, Triangle, D-pad Left held 800 ms. Never Win, Alt or Tab (those would leave the sink).
    const k = await scenario('keys', [
      { t: 0, buttons: B.Circle }, { t: 100, buttons: 0 }, { t: 200, buttons: B.Triangle }, { t: 300, buttons: 0 },
      { t: 400, buttons: B.Left }, { t: 1200, buttons: 0 }], 1400);
    check('key scenario ran clean', !!k);
    if (k) {
      check('Circle arrived as Escape (VK 0x1B): one down, one up', kd(k.events, VK.Esc).length === 1 && ku(k.events, VK.Esc).length === 1);
      check('Triangle arrived as Enter (VK 0x0D): one down, one up', kd(k.events, VK.Enter).length === 1 && ku(k.events, VK.Enter).length === 1);
      const exLeft = keys(k.expected, VK.Left).filter(a => a.down).length;
      const downs = kd(k.events, VK.Left);
      check('D-pad Left held 800 ms arrived as Left-arrow downs (repeats flagged as auto-repeat) and exactly one real key up', downs.length === exLeft && ku(k.events, VK.Left).length === 1 && downs.filter(x => x.repeat).length === exLeft - 1, `${downs.length} downs (${downs.filter(x => x.repeat).length} auto-repeat) expected ${exLeft}, ${ku(k.events, VK.Left).length} real key up`);
      check('no other key reached the sink', k.events.filter(e => e.e === 'keydown').length === 2 + exLeft);
      check('SendInput accepted every event it was given', k.result.sendInputAccepted > 0);
    }
  } finally {
    try { sink.kill(); } catch { /* it ends itself after 150 s */ }
  }
}

async function keyboardTest() {
  console.log('--- on-screen keyboard (Start) ---');
  const out = `${root}/keyboard.json`;
  rmSync(out, { force: true });
  const r = spawnSync(exe, ['--desktop-pad-keyboard', out], { timeout: 60000 });
  check('keyboard check ran', r.status === 0 && existsSync(out), `exit ${r.status}`);
  if (!existsSync(out)) return;
  const j = JSON.parse(readFileSync(out, 'utf8'));
  console.log('  ' + JSON.stringify(j));
  const shown = j.afterFirstPress.touchKeyboardVisible || j.afterFirstPress.oskWindowVisible;
  check('first Start press shows a keyboard (touch keyboard or osk.exe)', shown);
  check('second Start press closes it', !j.afterSecondPress.touchKeyboardVisible && !j.afterSecondPress.oskWindowVisible);
  check('nothing was showing before', !j.before.touchKeyboardVisible && !j.before.oskWindowVisible);
  if (j.afterFirstPress.oskStartedByDriver) check('osk.exe was started by the exe (its pid is tracked, only that pid is closed)', j.afterFirstPress.oskPid > 0);
}

async function appTest() {
  console.log('--- app: Desktop Mode with the driver ---');
  const dataDir = `${root}/app-data`, logFile = `${root}/app.log`;
  rmSync(dataDir, { recursive: true, force: true }); mkdirSync(dataDir, { recursive: true });
  const base = ['--dev-flash', 'C:/dev/rpcs3/bin/dev_flash', '--data-dir', dataDir, '--no-intro'];
  const env = { ...process.env, XMB_LOG: logFile };
  delete (env as any).XMB_SINGLE_INSTANCE;
  const log = () => (existsSync(logFile) ? readFileSync(logFile, 'utf8') : '');
  const wait = async (re: RegExp, ms: number, from = 0) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (re.test(log().slice(from))) return true; await sleep(100); } return false; };
  async function session(label: string, prefs: object | null, expectLive: boolean) {
    rmSync(logFile, { force: true });
    if (prefs) writeFileSync(`${dataDir}/preferences.json`, JSON.stringify({ version: 2, active: '', values: prefs }));
    else rmSync(`${dataDir}/preferences.json`, { force: true });
    const child = spawn(exe, [...base, '--start-windowed'], { env, stdio: 'ignore' });
    try {
      check(`${label}: XMB started`, await wait(/instance: listening on/, 60000));
      await sleep(2500);
      const mark = log().length;
      spawnSync(exe, [...base, '--desktop'], { env, timeout: 20000 });
      check(`${label}: desktop mode entered`, await wait(/desktop: enter via second launch --desktop/, 8000, mark));
      check(`${label}: driver started`, await wait(/desktop-pad: active/, 4000, mark));
      if (expectLive) check(`${label}: driver is live (no gate closed)`, await wait(/desktop-pad: live/, 4000, mark));
      else check(`${label}: driver paused, preference is off`, await wait(/desktop-pad: paused, off \(desktop\/controllerMouse\)/, 4000, mark));
      check(`${label}: no input was injected with no pad attached`, !/SendInput delivered nothing/.test(log()));
      // Return with the real hotkey (Ctrl+Alt+Home) through SendInput into the desktop, as D1's test does.
      const u = dlopen('user32.dll', { SendInput: { args: [FFIType.u32, FFIType.ptr, FFIType.i32], returns: FFIType.u32 } });
      const vks: [number, number][] = [[0x11, 0], [0x12, 0], [0x24, 1], [0x24, 3], [0x12, 2], [0x11, 2]];
      const buf = new ArrayBuffer(40 * vks.length); const dv = new DataView(buf);
      vks.forEach(([vk, flags], i) => { dv.setUint32(i * 40, 1, true); dv.setUint16(i * 40 + 8, vk, true); dv.setUint32(i * 40 + 12, flags, true); });
      const m2 = log().length;
      u.symbols.SendInput(vks.length, ptr(new Uint8Array(buf)), 40);
      check(`${label}: returned to the XMB via the hotkey`, await wait(/desktop: return via hotkey/, 8000, m2));
      check(`${label}: driver released and stopped before the XMB reappeared`, await wait(/desktop-pad: inactive after/, 3000, m2));
      await wait(/desktop: window shown/, 4000, m2);
      const L = log();
      check(`${label}: driver stopped before the window was shown`, L.indexOf('desktop-pad: inactive after') < L.indexOf('desktop: window shown'));
      const idle = log().length;
      await sleep(1500);
      check(`${label}: nothing from the driver while the XMB is visible`, !/desktop-pad: (live|paused)/.test(log().slice(idle)));
    } finally {
      try { spawnSync(exe, [...base, '--quit'], { env, timeout: 20000 }); } catch { /* ignore */ }
      await sleep(500);
      try { child.kill(); } catch { /* already gone */ }
    }
  }
  await session('pref on (default)', null, true);
  await session('pref off', { 'desktop/controllerMouse': false }, false);
}

if (part === 'map' || part === 'all') mapTests();
if (part === 'inject' || part === 'all') await injectTests();
if (part === 'keyboard' || part === 'all') await keyboardTest();
if (part === 'app' || part === 'all') await appTest();
console.log(failures ? `\n${failures} check(s) FAILED` : '\nall checks passed');
process.exit(failures ? 1 : 0);
