import { inflateSync } from 'node:zlib';
import { resolve } from 'node:path';
const path = process.argv[2];
if (!path) throw new Error('Usage: bun work/rco-inspect.ts file.rco');
const file = Buffer.from(await Bun.file(resolve(path)).arrayBuffer());
const u32 = (b: Buffer, p: number) => b.readUInt32BE(p);
const packed = u32(file, 0xa4), unpacked = u32(file, 0xa8);
const tables = (u32(file,12) >>> 4) === 1 ? Buffer.concat([file.subarray(0,0xa4), inflateSync(file.subarray(0xb0,0xb0+packed))]) : file;
const labels = u32(file,0x40), images = u32(file,0x24), imageData = u32(file,0x80);
console.log({path,packed,unpacked,images,labels,imageData,root:tables.subarray(images,images+0x40).toString('hex')});
const count=u32(tables,images+0x10);
let at=images+0x28;
for(let i=0;i<count;i++) {
  const label=labels+u32(tables,at+4), end=tables.indexOf(0,label);
  const name=tables.toString('utf8',label,end);
  const extra=at+0x28, format=tables.readUInt16BE(extra), compression=tables.readUInt16BE(extra+2)&255;
  const size=u32(tables,extra+4), offset=u32(tables,extra+8);
  const data=file.subarray(imageData+offset,imageData+offset+size);
  let decoded=data;
  try { if(compression===1) decoded=inflateSync(data); } catch {}
  console.log({i,at,name,format,compression,size,offset,signature:decoded.subarray(0,32).toString('hex'),gimHeader:decoded.subarray(0x30,0x80).toString('hex'),pixels:decoded.subarray(0x80,0xa0).toString('hex')});
  const next=u32(tables,at+0x14);
  if(!next)break;
  at+=next;
}
