import {inflateSync} from 'node:zlib';
const path=process.argv[2];
if(!path)throw new Error('Usage: bun work/qrc-inspect.ts file.qrc [name-filter]');
const filter=process.argv[3];
let bytes=Buffer.from(await Bun.file(path).arrayBuffer());
if(bytes.toString('ascii',0,4)==='QRCC')bytes=inflateSync(bytes.subarray(8));
console.log({size:bytes.length,header:bytes.subarray(0,0x80).toString('hex')});
const u32=(at:number)=>bytes.readUInt32BE(at);
const toc=u32(8),tocSize=u32(12),ids=u32(16),idsSize=u32(20),strings=u32(24),stringsSize=u32(28),data=u32(48),dataSize=u32(52);
const str=(offset:number)=>offset<stringsSize?bytes.toString('utf8',strings+offset,bytes.indexOf(0,strings+offset)):'?';
const id=(offset:number)=>offset+4<idsSize?bytes.toString('utf8',ids+offset+4,bytes.indexOf(0,ids+offset+4)):'?';
console.log({toc,tocSize,strings,stringsSize,data,dataSize});
for(let offset=0;offset<tocSize;) {
    const at=toc+offset,words=Array.from({length:7},(_,i)=>u32(at+i*4));
    const count=words[1];
    if(count>1000)throw new Error('attribute count');
    const attributes=Array.from({length:count},(_,i)=>{
        const p=at+28+i*16,type=u32(p+4),a=u32(p+8),b=u32(p+12);
        return {name:str(u32(p)),type,a,b,value:type===7?id(a):type===6?bytes.subarray(data+a,data+a+Math.min(b,24)).toString('hex'):undefined};
    });
    if(count){
        const source=attributes.find(a=>a.type===6),name=attributes.find(a=>a.type===7)?.value,size=attributes.find(a=>a.name==='size')?.a;
        let file=source?bytes.subarray(data+source.a,data+source.a+source.b):Buffer.alloc(0);
        if(size)file=inflateSync(file);
        if(filter && !name?.includes(filter)) {offset+=28+count*16;continue;}
        console.log({name,size:file.length,magic:file.subarray(0,64).toString('hex')});
        if(file.subarray(0,4).toString()==='#MNU')console.log(file.toString());
    }
    offset+=28+count*16;
}
