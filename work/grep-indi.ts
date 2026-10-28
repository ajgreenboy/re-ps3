import {readFileSync} from "node:fs";
const db=JSON.parse(readFileSync("C:/dev/xmb/out/native-reference/rco-behavior/resources.json","utf8"));
const re=/^indi|indicator|_ws$|^tex_go|trophy_ind|download_ind|^busy|^clock/i;
for(const r of db.resources){
  const hits=r.objects.filter(o=>re.test(o.name||""));
  if(!hits.length) continue;
  console.log("== "+r.path);
  for(const o of hits){
    const f=o.fields||{};
    if(o.schema==="Plane"||o.schema==="Text") console.log("  "+o.name+" ["+o.schema+"] parent="+o.parent+" pos="+f.posX+","+f.posY+" wh="+f.width+"x"+f.height+" size="+(f.size??"")+" alpha="+f.alphaScale);
    else console.log("  "+o.name+" ["+o.schema+"] parent="+o.parent);
  }
}
