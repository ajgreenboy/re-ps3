import {inflateSync} from 'node:zlib';
const file=Buffer.from(await Bun.file(process.argv[2]).arrayBuffer());
const filter=new RegExp(process.argv[3]||'msg_(thema|console|update|game_setting|video_setting|music_setting)');
const u32=(b:Buffer,p:number)=>b.readUInt32BE(p);
const tables=Buffer.concat([file.subarray(0,0xa4),inflateSync(file.subarray(0xb0,0xb0+u32(file,0xa4)))]);
const textRoot=u32(file,0x18),labels=u32(file,0x40);
console.log({textRoot,labels,textData:u32(file,0x38),root:tables.subarray(textRoot,textRoot+0x28).toString('hex')});
let chunk=u32(file,0x38),english:Buffer|undefined;
for(let i=0;i<64;i++) {
  const lang=file.readUInt16BE(chunk),next=u32(file,chunk+4),packed=u32(file,chunk+8),expanded=u32(file,chunk+12);
  if(lang===1){english=inflateSync(file.subarray(chunk+16,chunk+16+packed));console.log({lang,expanded,english:english.subarray(0,64).toString('hex')});break;}
  if(!next)break;chunk+=next;
}
let at=textRoot+0x28;
for(let i=0;i<u32(tables,textRoot+16);i++) {
  const info=at+0x28,lang=tables.readUInt16BE(info),format=tables.readUInt16BE(info+2),count=u32(tables,info+4);
  if(lang===1 && english) {
    console.log({lang,format,count});
    for(let j=0;j<count;j++) {
      const entry=info+8+j*12,label=labels+u32(tables,entry),length=u32(tables,entry+4),offset=u32(tables,entry+8);
      const name=tables.toString('utf8',label,tables.indexOf(0,label));
      if(filter.test(name)) {
        const text=Buffer.from(english.subarray(offset,offset+length));if(format===1)text.swap16();console.log(name,format===1?text.toString('utf16le'):text.toString('utf8'));
      }
    }
    break;
  }
  const next=u32(tables,at+20);if(!next)break;at+=next;
}
