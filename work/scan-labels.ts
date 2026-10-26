const fs = require('fs');
const p = 'C:\\\\dev\\\\xmb\\\\fw493\\\\vsh_module\\\\explore_plugin.prx';
const b = fs.readFileSync(p);
const hits = [];
const pat = Buffer.from('label', 'latin1');
let at = 0;
while (true) { const i = b.indexOf(pat, at); if (i < 0) break; const ctx = b.toString('latin1', Math.max(0,i-40), i+60).replace(/[^\x20-\x7e]/g,'.'); hits.push(i.toString(16)+': '+ctx); at = i + 1; if (hits.length > 200) break; }
console.log('label hits', hits.length);
console.log(hits.slice(0,80).join('\n'));
