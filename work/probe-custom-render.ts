const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\custom_render_plugin.prx').arrayBuffer());
for (const s of ['text','label','glow','Glow','font','bloom','Blur']) {
  const pat = Buffer.from(s,'latin1'); let at=0,n=0,first=[];
  while(n<6){const i=b.indexOf(pat,at);if(i<0)break;first.push(i.toString(16)+':'+JSON.stringify(b.toString('latin1',Math.max(0,i-24),i+s.length+30).replace(/[^\x20-\x7e]/g,'.')));n++;at=i+1;}
  console.log('##',s,first.join('\n'));
}
