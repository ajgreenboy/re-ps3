const input=process.argv[2];
const bytes=new Uint8Array(await Bun.file(input).arrayBuffer()),view=new DataView(bytes.buffer);
for(let off=0;off+52<=bytes.length;off+=4){
  if(view.getUint32(off)!==0x7f454c46||bytes[off+4]!==1||bytes[off+5]!==2||view.getUint16(off+18)!==23)continue;
  const phoff=view.getUint32(off+28),phsize=view.getUint16(off+42),count=view.getUint16(off+44),entry=view.getUint32(off+24);
  if(phsize<32||count>64||off+phoff+count*phsize>bytes.length)continue;
  let size=phoff+count*phsize;
  for(let i=0;i<count;++i){const p=off+phoff+i*phsize;size=Math.max(size,view.getUint32(p+4)+view.getUint32(p+16));}
  if(off+size>bytes.length)continue;
  const output=`work/libsre-spu-${off.toString(16)}.elf`;
  await Bun.write(output,bytes.slice(off,off+size));
  console.log(`offset=${off.toString(16)} entry=${entry.toString(16)} size=${size.toString(16)} -> ${output}`);
}
