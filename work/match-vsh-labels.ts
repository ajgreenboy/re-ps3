// Match source IDB code labels by unique, byte-identical 64-byte instruction windows.
// Output is analysis evidence only; no addresses/names are imported into the build.
import {readFileSync,writeFileSync} from 'node:fs';
const [idbPath,namesPath,elfPath,output,functionsPath]=process.argv.slice(2);
if(!output)throw new Error('usage: match-vsh-labels.ts source.idb names.json target.elf output.json');
const idb=readFileSync(idbPath),names=JSON.parse(readFileSync(namesPath,'utf8')) as {address:number,name:string}[];
const functionRanges=new Map<number,{start:number,end:number,flags:number}>();
if(functionsPath){
    const ranges=JSON.parse(readFileSync(functionsPath,'utf8')).functions as {start:number,end:number,flags:number}[];
    const named=new Set(names.map(n=>n.address));
    for(const range of ranges)if(!(range.flags&0x8000)){
        functionRanges.set(range.start,range);
        if(!named.has(range.start))names.push({address:range.start,name:`ida_${range.start.toString(16).padStart(8,'0')}`});
    }
}
const section=Number(idb.readBigUInt64LE(14));
if(idb.toString('ascii',0,4)!=='IDA1'||idb.readUInt16LE(30)!==6||idb[section]!==0)throw new Error('Expected uncompressed IDA1 v6 ID1');
const size=Number(idb.readBigUInt64LE(section+1));
if(section+9+size>idb.length)throw new Error('Truncated ID1');
const flags=idb.subarray(section+9,section+9+size);
if(flags.toString('ascii',0,4)!=='VA*\0')throw new Error('Unsupported ID1 layout');
const sources:{address:number,end:number,offset:number,bytes:Buffer}[]=[];
let position=8192;
for(let i=0;i<flags.readUInt32LE(8);++i){
    const address=flags.readUInt32LE(20+i*8),end=flags.readUInt32LE(24+i*8);
    if(end<address||position+4*(end-address)>flags.length)throw new Error('Invalid ID1 region');
    const bytes=Buffer.alloc(end-address);
    for(let j=0;j<bytes.length;++j)bytes[j]=flags[position+4*j];
    sources.push({address,end,offset:position,bytes});position+=4*(end-address);
}
const elf=readFileSync(elfPath);
if(elf.toString('hex',0,6)!=='7f454c460202')throw new Error('Expected ELF64 big-endian');
const segments:{address:number,bytes:Buffer}[]=[];
const phoff=Number(elf.readBigUInt64BE(32)),phsize=elf.readUInt16BE(54),phcount=elf.readUInt16BE(56);
for(let i=0;i<phcount;i++){
    const p=phoff+i*phsize;
    if(elf.readUInt32BE(p)!==1||!(elf.readUInt32BE(p+4)&1))continue;
    const offset=Number(elf.readBigUInt64BE(p+8)),address=Number(elf.readBigUInt64BE(p+16)),length=Number(elf.readBigUInt64BE(p+32));
    if(!Number.isSafeInteger(address)||offset+length>elf.length)throw new Error('Invalid ELF region');
    segments.push({address,bytes:elf.subarray(offset,offset+length)});
}
const width=64;
function hash(b:Buffer,o:number){let h=2166136261;for(let i=0;i<width;i++)h=Math.imul(h^b[o+i],16777619);return h>>>0;}
// Only index hashes required by a source code label.
const wanted=new Map<number,{address:number,name:string,bytes:Buffer}[]>();
let codeLabels=0;
for(const name of names){
    const source=sources.find(s=>name.address>=s.address&&name.address+width<=s.end);
    if(!source||name.address%4)continue;
    const offset=name.address-source.address;
    if((flags.readUInt32LE(source.offset+4*offset)&0x600)!==0x600)continue;
    let initialized=true;for(let j=0;j<width;j++)if(!(flags.readUInt32LE(source.offset+4*(offset+j))&0x100)){initialized=false;break;}
    if(!initialized)continue;
    codeLabels++;
    const bytes=source.bytes.subarray(offset,offset+width),key=hash(bytes,0);
    const list=wanted.get(key)??[];list.push({...name,bytes});wanted.set(key,list);
}
const candidates=new Map<number,Set<number>>();
for(const segment of segments)for(let offset=0;offset+width<=segment.bytes.length;offset+=4){
    const list=wanted.get(hash(segment.bytes,offset));if(!list)continue;
    for(const label of list)if(segment.bytes.subarray(offset,offset+width).equals(label.bytes)){
        const locations=candidates.get(label.address)??new Set<number>();locations.add(segment.address+offset);candidates.set(label.address,locations);
    }
}
const unique=names.flatMap(label=>{
    const matches=candidates.get(label.address);if(matches?.size!==1)return [];
    const targetAddress=[...matches][0],range=functionRanges.get(label.address);
    let fullFunctionIdentical=false;
    if(range){
        const source=sources.find(s=>range.start>=s.address&&range.end<=s.end);
        const target=segments.find(s=>targetAddress>=s.address&&targetAddress+range.end-range.start<=s.address+s.bytes.length);
        if(source&&target){
            let initialized=true;
            for(let a=range.start;a<range.end;a++)if(!(flags.readUInt32LE(source.offset+4*(a-source.address))&0x100)){initialized=false;break;}
            fullFunctionIdentical=initialized&&source.bytes.subarray(range.start-source.address,range.end-source.address).equals(target.bytes.subarray(targetAddress-target.address,targetAddress-target.address+range.end-range.start));
        }
    }
    return [{sourceAddress:label.address,targetAddress,name:label.name,method:'unique-exact-64-byte-code-window',sourceFunctionEnd:range?.end,fullFunctionIdentical}];
});
const result={source:idbPath,target:elfPath,windowBytes:width,sourceCodeLabels:codeLabels,uniqueMatches:unique.length,ambiguous:[...candidates].filter(([,v])=>v.size>1).length,limitations:['Identical bytes are a correspondence candidate, not proof of equivalent callers or complete function bodies','Unmatched/ambiguous labels are excluded','Source IDB bytes may include analyst patches'],matches:unique};
writeFileSync(output,JSON.stringify(result,null,2));
console.log(JSON.stringify({codeLabels,unique:unique.length,ambiguous:result.ambiguous,fullFunctionIdentical:unique.filter(m=>m.fullFunctionIdentical).length,sample:unique.filter(n=>/Rsx|rsxaudio|sys_prx|Widget|Shutdown|buffer_set_zero/.test(n.name)).slice(0,15)}));
