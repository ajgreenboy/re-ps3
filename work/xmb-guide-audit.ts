// Read-only catalog of Sony's public PS3 guide. Raw source stays in ignored out/.
import {mkdir} from 'node:fs/promises';
import {join,resolve} from 'node:path';
import {createHash} from 'node:crypto';
const base='https://manuals.playstation.net/document/en/ps3/current/';
const output=resolve(process.argv[2]||'out/native-reference/guide-audit');
await mkdir(join(output,'pages'),{recursive:true});
type Node={tag:string,attrs:Record<string,string>,children:(Node|string)[]};
const entities:Record<string,string>={amp:'&',lt:'<',gt:'>',quot:'"',apos:"'",nbsp:' ',trade:'™',reg:'®',copy:'©',ndash:'–',mdash:'—',rsquo:'’',lsquo:'‘',rdquo:'”',ldquo:'“',hellip:'…',times:'×'};
function decode(s:string){return s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi,(_,e:string)=>e[0]==='#'?String.fromCodePoint(parseInt(e.slice(e[1].toLowerCase()==='x'?2:1),e[1].toLowerCase()==='x'?16:10)):entities[e]??'&'+e+';');}
function parse(html:string):Node {
    const root:Node={tag:'root',attrs:{},children:[]},stack=[root];
    html=html.replace(/<!--[\s\S]*?-->/g,'').replace(/<(script|style)\b[^>]*>[\s\S]*?<\/\1\s*>/gi,'');
    for(const token of html.match(/<[^>]*>|[^<]+/g)||[]) {
        if(token.startsWith('</')){const tag=token.slice(2).replace(/[\s>].*/,'').toLowerCase();let i=stack.length-1;while(i>0&&stack[i].tag!==tag)i--;if(i>0)stack.length=i;}
        else if(token.startsWith('<')) {
            const match=token.match(/^<([a-z][\w-]*)\b/i);if(!match)continue;
            const tag=match[1].toLowerCase(),attrs:Record<string,string>={};
            for(const attr of token.matchAll(/([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g))attrs[attr[1].toLowerCase()]=decode(attr[2]??attr[3]);
            const node:Node={tag,attrs,children:[]};stack.at(-1)!.children.push(node);
            if(!/\/$/.test(token.slice(0,-1))&&!['meta','link','img','br','hr','input','source','wbr'].includes(tag))stack.push(node);
        } else stack.at(-1)!.children.push(decode(token));
    }
    return root;
}
function nodes(node:Node,fn:(n:Node)=>boolean):Node[]{return [fn(node)?node:[],...node.children.filter((x):x is Node=>typeof x!=='string').map(x=>nodes(x,fn))].flat(2) as Node[];}
function text(node:Node,skipLists=false):string{return node.children.map(x=>typeof x==='string'?x:(skipLists&&['ul','ol'].includes(x.tag))?'':text(x,skipLists)).join(' ').replace(/\s+/g,' ').trim();}
function ownLinks(node:Node):{title:string,url:string}[]{return nodes(node,n=>n.tag==='a'&&!!n.attrs.href).map(n=>({title:text(n),url:new URL(n.attrs.href,base).href}));}
const indexFile=Bun.file(join(output,'manualindex.html'));
const indexHtml=await indexFile.exists()?await indexFile.text():await (async()=>{const r=await fetch(base+'manualindex.html');if(!r.ok)throw new Error('Index HTTP '+r.status);return r.text();})();
await Bun.write(join(output,'manualindex.html'),indexHtml);
const index=parse(indexHtml),indexNode=nodes(index,n=>n.attrs.id==='index')[0];if(!indexNode)throw new Error('Missing index');
const categories=['About the XMB™ menu','Users','Settings','Photo','Music','Video','TV / Video Services','Game','Network','PlayStation™Network','Friends'];
const settingsGroups=['System Update','Game Settings','Video Settings','Music Settings','Chat Settings','Theme Settings','System Settings','Date and Time Settings','Power Save Settings','Accessory Settings','Printer Settings','Display Settings','Sound Settings','Security Settings','Remote Play Settings','Network Settings'];
type Feature={id:string,category:string,path:string[],title:string,sourceUrl:string,hasChildren:boolean,status:string};
const features:Feature[]=[];let category='';const unique=new Set<string>();
function addList(list:Node,parents:string[],inherited:string){
    for(const li of list.children.filter((x):x is Node=>typeof x!=='string'&&x.tag==='li')) {
        const title=text(li,true);if(!title)continue;
        const direct=li.children.filter((x):x is Node=>typeof x!=='string'&&x.tag!=='ul'&&x.tag!=='ol');
        const link=direct.flatMap(ownLinks)[0];let url=link?.url||inherited;
        let path=[...parents,title];
        if(category==='Settings'&&settingsGroups.includes(title))path=[category,title];
        if(path.length>2&&path.at(-2)===title)path=path.slice(0,-1);
        const id=path.join(' / ');
        const children=li.children.filter((x):x is Node=>typeof x!=='string'&&['ul','ol'].includes(x.tag));
        if(!unique.has(id)){features.push({id,category,path,title,sourceUrl:url,hasChildren:children.length>0,status:'requires-feature-specific-native-verification'});unique.add(id);}
        for(const child of children)addList(child,path,url);
    }
}
// The published index has unbalanced list markup. Section boundaries come from
// its explicit headings rather than a browser-repaired ancestor hierarchy.
const sections=[...indexHtml.matchAll(/<h4\b[^>]*>[\s\S]*?<\/h4>/gi)];
for(let i=0;i<sections.length;++i) {
    const heading=parse(sections[i][0]),title=text(heading);
    const newCategory=categories.find(c=>c.replace(/[™®]/g,'')===title.replace(/[™®]/g,''));
    if(newCategory)category=newCategory;else if(title==='Top')category='';
    if(!category)continue;
    const fragment=parse(indexHtml.slice(sections[i].index!+sections[i][0].length,sections[i+1]?.index));
    const list=nodes(fragment,n=>n.tag==='ul')[0];
    if(list)addList(list,newCategory?[category]:[category,title],ownLinks(heading)[0]?.url||base);
}
const urls=[...new Set(ownLinks(indexNode).map(l=>l.url).filter(url=>url.startsWith(base)&&!url.endsWith('manualindex.html')))];
let completed=0;const pages:any[]=[];
const previousFile=Bun.file(join(output,'catalog.json'));
const previous=await previousFile.exists()?await previousFile.json():{pages:[]};
const cached=new Map<string,any>(previous.pages.map((p:any)=>[p.url,p]));
// Four requests at a time; public guide only, no forms or account access.
const batch=process.argv.includes('--retry-errors')?1:4;
for(let start=0;start<urls.length;start+=batch) {
    const results=await Promise.allSettled(urls.slice(start,start+batch).map(async url=>{
        if(cached.has(url)&&(!cached.get(url).error||!process.argv.includes('--retry-errors')))return cached.get(url);
        if(process.argv.includes('--retry-errors'))await new Promise(resolve=>setTimeout(resolve,1500));
        const r=await fetch(url,{signal:AbortSignal.timeout(20000)});if(!r.ok)throw new Error('HTTP '+r.status);
        const html=await r.text();if(html.length>2_000_000)throw new Error('Oversized guide page');
        const dom=parse(html),main=nodes(dom,n=>n.attrs.id==='main')[0]||dom;
        const file=new URL(url).pathname.slice(new URL(base).pathname.length).replaceAll('/','__');
        await Bun.write(join(output,'pages',file),html);
        const headings=nodes(main,n=>/^h[1-6]$/.test(n.tag)).map(n=>({level:Number(n.tag[1]),title:text(n)}));
        const rows=nodes(main,n=>n.tag==='tr').map(n=>n.children.filter((x):x is Node=>typeof x!=='string'&&['td','th'].includes(x.tag)).map(x=>text(x))).filter(r=>r.length);
        return {url,title:headings[0]?.title||'',headings,rows,sha256:createHash('sha256').update(html).digest('hex'),fetchedAt:new Date().toISOString(),sourceFile:'pages/'+file};
    }));
    results.forEach((r,i)=>pages.push(r.status==='fulfilled'?r.value:{url:urls[start+i],error:String(r.reason)}));
    completed+=results.length;if(completed%20<4)console.log('Guide pages',completed,'/',urls.length);
}
const report={scope:'Sony PS3 current guide (4.80 or later), cross-check required for local 4.93 availability',generatedAt:new Date().toISOString(),features,pages,counts:{features:features.length,leafFeatures:features.filter(x=>!x.hasChildren).length,pages:pages.length,errors:pages.filter(x=>x.error).length,categories:Object.fromEntries(categories.map(c=>[c,features.filter(f=>f.category===c).length]))}};
await Bun.write(join(output,'catalog.json'),JSON.stringify(report,null,2));
const lines=['# Official XMB guide feature index','',`Collected ${report.counts.features} indexed topics/actions across ${report.counts.pages} guide pages. This is an index, not proof of native implementation.`, ''];
for(const c of categories){lines.push('## '+c,'','| Feature path | Reference |','|---|---|');for(const f of features.filter(x=>x.category===c))lines.push('| '+f.path.slice(1).join(' → ').replaceAll('|','/')+' | [Sony guide]('+f.sourceUrl+') |');lines.push('');}
await Bun.write(join(output,'FEATURE-INDEX.md'),lines.join('\n')+'\n');
console.log(JSON.stringify(report.counts));
