const fs = require('fs');
const dirs = ['C:\\\\dev\\\\xmb\\\\fw493\\\\vsh_module'];
const needles = ['glow','Glow','halo','Halo','bloom','gaussian','Gaussian','ellipsis','Ellipsis'];
for (const d of dirs) {
  for (const name of fs.readdirSync(d)) {
    if (!/\.(prx|elf)$/i.test(name)) continue;
    const p = d + '\\\\' + name;
    let b; try { b = fs.readFileSync(p); } catch { continue; }
    const hits = [];
    for (const n of needles) {
      const pat = Buffer.from(n, 'latin1');
      let at = 0; while (true) { const i = b.indexOf(pat, at); if (i < 0) break; hits.push(n + '@' + i.toString(16) + ' ctx=' + JSON.stringify(b.toString('latin1', Math.max(0,i-20), i+n.length+24).replace(/[^\x20-\x7e]/g,'.'))); at = i + 1; if (hits.length > 12) break; }
    }
    if (hits.length) console.log('###', name, b.length, '\n' + hits.slice(0,8).join('\n'));
  }
}
