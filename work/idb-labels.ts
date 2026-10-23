// Read-only IDB v6 / B-tree 2.0 label and comment export. No target execution.
// Format reference: https://github.com/nlitsme/idbutil/blob/master/IDB-FORMAT.md
// Layout cross-check: Willem Hengeveld's https://github.com/nlitsme/pyidbutil
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {inflateSync} from 'node:zlib';
const [input,output,expectedBlob]=process.argv.slice(2);
if(!input||!output)throw new Error('usage: bun idb-labels.ts input.idb output-dir [git-blob-sha1]');
const file=readFileSync(input);
const blob=createHash('sha1').update(`blob ${file.length}\0`).update(file).digest('hex');
if(expectedBlob&&blob!==expectedBlob)throw new Error('Git blob digest mismatch');
if(file.toString('ascii',0,4)!=='IDA1'||file.readUInt16LE(30)!==6)throw new Error('Only IDA1 container version 6 is supported');
const section=Number(file.readBigUInt64LE(6));
if(!Number.isSafeInteger(section)||section+9>file.length)throw new Error('Invalid ID0 section');
const size=Number(file.readBigUInt64LE(section+1));
if(!Number.isSafeInteger(size)||section+9+size>file.length)throw new Error('Truncated ID0 section');
const payload=file.subarray(section+9,section+9+size);
const compression=file[section];
if(compression!==0&&compression!==2)throw new Error('Unsupported compression');
const data=compression===2?inflateSync(payload,{maxOutputLength:512*1024*1024}):payload;
if(data.toString('ascii',19,28)!=='B-tree v2')throw new Error('Only B-tree 2.0 is supported');
const pageSize=data.readUInt16LE(4),root=data.readUInt32LE(6),expectedRecords=data.readUInt32LE(10);
if(pageSize<512||pageSize>65536||data.length%pageSize)throw new Error('Invalid page geometry');
const names:{address:number,name:string}[]=[],comments:{address:number,index:number,text:string}[]=[],internal:{id:number,name:string}[]=[];
const visited=new Set<number>();
let functionsNode:number|undefined;
const functions:{start:number,end:number,flags:number}[]=[];
const functionUnknowns:{index:number,valueHex:string,error:string}[]=[];
let records=0,previous:Buffer|undefined;
const text=(b:Buffer)=>b.toString('utf8').replace(/\0+$/,'');
function accept(key:Buffer,value:Buffer){
    if(previous&&Buffer.compare(previous,key)>=0)throw new Error('Unsorted/duplicate tree keys');
    previous=key;records++;
    if(key[0]!==0x2e||key.length<6)return;
    const address=key.readUInt32BE(1),tag=key[5];
    if(address===functionsNode&&tag===0x53&&key.length===10){
        const index=key.readUInt32BE(6);let offset=0;
        function packed(width:2|4):number{
            if(offset>=value.length)throw new Error('Truncated packed integer');
            const first=value[offset++];if(first<0x80)return first;
            if(first<0xc0){if(offset>=value.length)throw new Error('Truncated packed pair');return ((first&0x3f)<<8)|value[offset++];}
            if(width===4&&first<0xe0){if(offset+3>value.length)throw new Error('Truncated packed dword');const n=((first&0x1f)*0x1000000)+value.readUIntBE(offset,3);offset+=3;return n;}
            if(first===0xff){if(offset+width>value.length)throw new Error('Truncated full integer');const n=value.readUIntBE(offset,width);offset+=width;return n;}
            throw new Error('Unsupported packed integer');
        }
        try{
            const start=packed(4),length=packed(4),flags=packed(2);
            if(start!==index||!length||start+length>0xffffffff)throw new Error('Invalid function range/index');
            functions.push({start,end:start+length,flags});
        }catch(error){functionUnknowns.push({index,valueHex:value.toString('hex'),error:String(error)});}
    }
    if(key.length===6&&tag===0x4e){
        if(address<0xff000000)names.push({address,name:text(value)});
        else {internal.push({id:address,name:text(value)});if(text(value)==='$ funcs')functionsNode=address;}
    }else if(address<0xff000000&&key.length===10&&tag===0x53){
        const index=key.readUInt32BE(6);
        if(index===0||index===1||(index>=1000&&index<1100)){
            const valueText=text(value);
            if(valueText&&!/[\x01-\x08\x0b\x0c\x0e-\x1f]/.test(valueText))comments.push({address,index,text:valueText});
        }
    }
}
function walk(number:number,depth=0){
    if(depth>64||number===0||visited.has(number)||number*pageSize+pageSize>data.length)throw new Error('Invalid page reference/cycle');
    visited.add(number);
    const page=data.subarray(number*pageSize,(number+1)*pageSize);
    const preceding=page.readUInt32LE(0),count=page.readUInt16LE(4);
    if((count+2)*6>pageSize)throw new Error('Invalid page record count');
    if(preceding)walk(preceding,depth+1);
    let previousPageKey=Buffer.alloc(0);
    for(let i=0;i<count;i++){
        const slot=(i+1)*6,child=preceding?page.readUInt32LE(slot):0;
        const prefix=preceding?0:page.readUInt16LE(slot);
        let position=page.readUInt16LE(slot+4);
        if(position<(count+2)*6||position+2>pageSize||prefix>previousPageKey.length)throw new Error('Invalid record offset/prefix');
        const keySize=page.readUInt16LE(position);position+=2;
        if(position+keySize+2>pageSize)throw new Error('Truncated key');
        const key=Buffer.concat([previousPageKey.subarray(0,prefix),page.subarray(position,position+keySize)]);position+=keySize;
        const valueSize=page.readUInt16LE(position);position+=2;
        if(position+valueSize>pageSize)throw new Error('Truncated value');
        accept(key,page.subarray(position,position+valueSize));
        previousPageKey=key;
        if(child)walk(child,depth+1);
    }
}
walk(root);
if(records!==expectedRecords)throw new Error(`Record count mismatch: ${records} / ${expectedRecords}`);
mkdirSync(output,{recursive:true});
const csv=(v:string)=>'"'+v.replaceAll('"','""')+'"';
writeFileSync(join(output,'names.csv'),'address,name\n'+names.map(n=>`${n.address.toString(16).padStart(8,'0')},${csv(n.name)}`).join('\n')+'\n');
writeFileSync(join(output,'names.json'),JSON.stringify(names));
writeFileSync(join(output,'comments.json'),JSON.stringify(comments,null,2));
writeFileSync(join(output,'internal-nodes.json'),JSON.stringify(internal,null,2));
writeFileSync(join(output,'functions.json'),JSON.stringify({functions,unknowns:functionUnknowns},null,2));
const summary={input,sha256:createHash('sha256').update(file).digest('hex'),gitBlobSha1:blob,containerVersion:6,btreeVersion:'2.0',pageSize,pages:visited.size,records,expectedRecords,names:names.length,comments:comments.length,internalNames:internal.length,functionRanges:functions.length,functionUnknowns:functionUnknowns.length,limitations:['32-bit node addresses only','Function chunk owners, types and complete discontiguous bodies are not decoded','4.88 addresses must be matched against 4.93 before use']};
writeFileSync(join(output,'summary.json'),JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary));
