// Maps "exe+OFFSET" host addresses from xmbrt.log to the translated guest function, using the lld map.
// usage: bun host-symbol.ts <xmbrt.map> <hex offset>...
const [mapPath, ...offsets] = process.argv.slice(2);
const syms: { addr: bigint; name: string }[] = [];
for (const line of (await Bun.file(mapPath).text()).split("\n")) {
  const m = line.match(/^\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})/i);
  if (m) syms.push({ addr: BigInt("0x" + m[2]), name: m[1] });
}
syms.sort((a, b) => (a.addr < b.addr ? -1 : 1));
for (const o of offsets) {
  const target = 0x140000000n + BigInt("0x" + o.replace(/^exe\+/, ""));
  let lo = 0, hi = syms.length - 1, best = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (syms[mid].addr <= target) { best = mid; lo = mid + 1; } else hi = mid - 1;
  }
  console.log(o, best >= 0 ? `${syms[best].name}+0x${(target - syms[best].addr).toString(16)}` : "?");
}
