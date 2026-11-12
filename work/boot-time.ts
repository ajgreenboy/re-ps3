// ZF4: time from process start to the first frame in --kiosk, several variants interleaved so a busy machine hits all of them alike.
//   bun work/boot-time.ts --variant base=C:/x/re-ps3-native.exe --variant new=C:/y/re-ps3-native.exe,XMB_BOOT_PREFETCH=0
//        [--runs 5] [--data C:/dev/out/zf4/data] [--profile]
// A variant is name=exe[,KEY=VALUE...]. Each run starts the exe with XMB_LOG, waits for "boot: first frame after N ms", stops THAT pid
// and keeps the boot lines. --profile sets XMB_BOOT_PROFILE=1 (the spans cost nothing measurable, but leave it off for the numbers).
import { mkdirSync, readFileSync, rmSync, existsSync } from "node:fs";

const args = process.argv.slice(2);
const opt = (name: string, fallback = "") => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const runs = Number(opt("runs", "5"));
const data = opt("data", "C:/dev/out/zf4/data");
const profile = args.includes("--profile");
const variants = args.flatMap((a, i) => a === "--variant" ? [args[i + 1]] : []).map(v => {
  const [head, ...envs] = v.split(",");
  const eq = head.indexOf("=");
  return { name: head.slice(0, eq), exe: head.slice(eq + 1), env: Object.fromEntries(envs.map(e => [e.slice(0, e.indexOf("=")), e.slice(e.indexOf("=") + 1)])) as Record<string, string> };
});
if (!variants.length) { console.error("at least one --variant name=exe[,K=V] is required"); process.exit(2); }
mkdirSync(data, { recursive: true });
mkdirSync("C:/dev/out/zf4/logs", { recursive: true });

const results = new Map<string, number[]>(variants.map(v => [v.name, []]));
for (let n = 1; n <= runs; n++) {
  for (const v of variants) {
    const log = `C:/dev/out/zf4/logs/${v.name}-${n}.log`;
    if (existsSync(log)) rmSync(log);
    const env: Record<string, string> = { ...process.env as Record<string, string>, XMB_LOG: log, XMB_SINGLE_INSTANCE: "0", ...v.env };
    if (profile) env.XMB_BOOT_PROFILE = "1";
    const proc = Bun.spawn([v.exe, "--kiosk", "--display", "0", "--dev-flash", "C:/dev/rpcs3/bin/dev_flash", "--data-dir", data], { env, stdout: "ignore", stderr: "ignore" });
    const started = Date.now();
    let value = NaN;
    while (Date.now() - started < 60000) {
      await Bun.sleep(200);
      if (!existsSync(log)) continue;
      const m = /boot: first frame after (\d+) ms/.exec(readFileSync(log, "utf8"));
      if (m) { value = Number(m[1]); break; }
    }
    await Bun.sleep(1200); // post-frame deferred work runs before the stop
    proc.kill();
    await proc.exited;
    results.get(v.name)!.push(value);
    console.log(`run ${n} ${v.name}: ${value} ms`);
    await Bun.sleep(800);
  }
}
for (const [name, values] of results) {
  const s = [...values].sort((a, b) => a - b);
  const median = s.length % 2 ? s[(s.length - 1) / 2] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2;
  console.log(`${name}: first frame median ${median} ms, range ${s[0]}-${s[s.length - 1]} ms, runs ${values.join(", ")}`);
}
