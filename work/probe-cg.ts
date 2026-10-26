const b = Buffer.from(await Bun.file('C:\\dev\\xmb\\fw493\\vsh_module\\vsh.elf').arrayBuffer());
for (const s of ['lib/glutils/BloomCone.fpo','lib/glutils/BlurOutput.fpo','lib/glutils/GaussBloomAndBlur.fpo','lib/glutils']) {
  const i = b.indexOf(Buffer.from(s,'latin1'));
  console.log(s, i >= 0 ? 'found at ' + i.toString(16) : 'not found');
}
