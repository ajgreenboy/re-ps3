import {inflateSync} from 'node:zlib';
import {readdir,mkdir} from 'node:fs/promises';
import {join,resolve,relative} from 'node:path';
import {createHash} from 'node:crypto';
const root=resolve(process.argv[2]||''),output=resolve(process.argv[3]||'out/native-reference/rco-behavior');
if(!process.argv[2])throw new Error('Usage: bun work/rco-behavior-audit.ts dev_flash [output-directory]');
await mkdir(output,{recursive:true});
async function files(path:string):Promise<string[]>{const entries=await readdir(path,{withFileTypes:true});return (await Promise.all(entries.map(e=>e.isDirectory()?files(join(path,e.name)):e.name.endsWith('.rco')?[join(path,e.name)]:[]))).flat();}
const resources=[];const types=['AnimTree','Animation','MoveTo','Recolour','Rotate','Resize','Fade','Delay','FireEvent','Lock','Unlock','SlideOut'];
async function definition(file:string){
    const source=await Bun.file(resolve('out/native-reference',file)).text();const rows:{name:string,fields:{name:string,type:string,offset:number}[],bytes:number}[]=[];
    for(const line of source.split(/\r?\n/)){
        const section=line.match(/^\[([^\]]+)\]/);if(section){rows.push({name:section[1],fields:[],bytes:0});continue;}
        const field=line.match(/^([\w]+)\s*=\s*(\w+)/);if(!field||!rows.length)continue;
        const row=rows.at(-1)!;row.fields.push({name:field[1],type:field[2],offset:row.bytes});row.bytes+=['ref','event','image','text','model'].includes(field[2])?8:4;
    }return rows;
}
const objDefs=await definition('objattribdef-ps3.ini'),animDefs=await definition('animattribdef-ps3.ini');
for(const path of await files(join(root,'vsh/resource'))) {
    const result:any={path:relative(root,path).replaceAll('\\','/'),sha256:'',objects:[],animations:[],error:''};
    try {
        const file=Buffer.from(await Bun.file(path).arrayBuffer());result.sha256=createHash('sha256').update(file).digest('hex');
        if(file.length>64*1024*1024||file.readUInt32BE(0)!==0x46525000)throw new Error('Unsupported RCO');
        const compression=file.readUInt32BE(12)>>>4;
        const tables=compression===0?file:compression===1?Buffer.concat([file.subarray(0,0xa4),inflateSync(file.subarray(0xb0,0xb0+file.readUInt32BE(0xa4)),{maxOutputLength:32*1024*1024})]):(()=>{throw new Error('Unsupported compression');})();
        const u32=(p:number)=>{if(p<0||p+4>tables.length)throw new Error('Range');return tables.readUInt32BE(p);};
        const labels=file.readUInt32BE(0x40),labelLength=file.readUInt32BE(0x44);let count=0;
        function walk(at:number,depth:number,target:any[],parent:string){
            if(++count>100000||depth>128||at+40>tables.length)throw new Error('Tree limit');
            const typeId=tables.readUInt16BE(at),label=u32(at+4),rawSize=u32(at+12),children=u32(at+16);
            const schema=(typeId>>>8)===8?objDefs[typeId&255]:animDefs[typeId&255];
            if(!rawSize&&!schema)throw new Error('Unknown entry type without an explicit size');
            const size=rawSize||40+schema.bytes;
            if(size<40||at+size>tables.length||children>65536)throw new Error('Invalid entry');
            let name='';if(label!==0xffffffff){if(label>=labelLength)throw new Error('Label range');const end=tables.indexOf(0,labels+label);if(end<0||end>=labels+labelLength)throw new Error('Label terminator');name=tables.toString('utf8',labels+label,end);}
            const extra=tables.subarray(at+40,at+size),id=typeId&255;
            const entry:any={offset:at,typeId:'0x'+typeId.toString(16),name,parent,children,extraBytes:extra.length,schema:schema?.name};
            if((typeId>>>8)===8&&schema){entry.fields={};for(const field of schema.fields){if(field.offset+(field.type==='float'?4:8)>extra.length)continue;if(field.type==='float')entry.fields[field.name]=extra.readFloatBE(field.offset);}}
            if((typeId>>>8)===9) {
                entry.operation=types[id]||'Unknown';
                if(id>=2&&id<=6||id===11){if(extra.length>=16){entry.durationRaw=extra.readFloatBE(8);entry.accelModeRaw=extra.readUInt32BE(12);entry.targetRefHex=extra.subarray(0,8).toString('hex');}}
                if(id===7&&extra.length>=4)entry.delayRaw=extra.readFloatBE(0);
                // Units and easing enum interpretation must be established per
                // resource/runtime; do not relabel raw floats as milliseconds.
                entry.extraHex=extra.subarray(0,128).toString('hex');
            }
            target.push(entry);
            let child=at+size;
            for(let i=0;i<children;++i){const end=walk(child,depth+1,target,name||parent);const next=u32(child+20);if(i+1<children&&next&&next<40)throw new Error('Sibling offset');child=next?child+next:end;}
            return Math.max(at+size,child);
        }
        for(const [offset,key] of [[0x30,'objects'],[0x34,'animations']] as const){const at=file.readUInt32BE(offset);if(at!==0xffffffff)walk(at,0,result[key],'');}
    }catch(e){result.error=String(e);}
    resources.push(result);
}
const counts={resources:resources.length,errors:resources.filter(r=>r.error).length,objects:resources.reduce((n,r)=>n+r.objects.length,0),animationEntries:resources.reduce((n,r)=>n+r.animations.length,0)};
await Bun.write(join(output,'resources.json'),JSON.stringify({counts,resources},null,2));
console.log(counts);
for(const r of resources.filter(r=>/\/(system_plugin|explore_plugin_full|sysconf_plugin)\.rco$/.test(r.path)))console.log(r.path,{objects:r.objects.length,animations:r.animations.length,error:r.error},r.animations.slice(0,16));
