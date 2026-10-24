import {mkdir} from 'node:fs/promises';
import {resolve,join} from 'node:path';
const source=await Bun.file('out/native-reference/guide-audit/catalog.json').json();
const destination=resolve('native-xmb/audit');await mkdir(destination,{recursive:true});
const categoryBackend:Record<string,string>={
    'About the XMB™ menu':'Menu state, input routing, overlays, text entry, concurrent activities',Users:'Profiles, session ownership, user-scoped storage, login lifecycle',
    Settings:'Typed settings, capability queries, transaction/rollback, device and service adapters',Photo:'Image decoder, metadata, transforms, image database, playlists, printer and storage adapters',
    Music:'Audio decoder/mixer, media database, queue, DSP, visualization, optical and storage adapters',Video:'Video decoder, tracks, subtitles, timeline/chapter/scene model, disc navigation and storage adapters',
    'TV / Video Services':'Region catalog, application install/launch, service availability',Game:'Content/save databases, emulator/process lifecycle, disc detection, memory cards, rights and parental controls',
    Network:'Browser engine, downloads, discovery, network status, remote-play session transport',
    'PlayStation™Network':'Authenticated account/session, network service adapters, trophies, entitlements and commerce',Friends:'Presence, profiles, relationships, messaging, chat and media sessions'};
function nativeStatus(f:any){
    const p=f.path.join(' / '),t=f.title.toLowerCase();
    if(f.category==='Users'&&/creating new users|logging in/.test(t))return 'Partial: local profiles implemented; PS3 session/account semantics incomplete';
    if(f.category==='Settings'&&/system name|system information|date format|time format|time zone|color \/ colour|font|background|key tone/.test(t))return 'Partial: native adapter implemented; exact PS3 behavior and workflow coverage open';
    if(f.category==='Settings'&&/system auto-off/.test(t))return 'Partial: closes native application; does not power off host';
    if(f.category==='Photo'&&/rotate left|rotate right|previous \/ next|slideshow|display mode/.test(t))return 'Partial: keyboard/native viewer route; original control panel and full workflow unobserved';
    if(f.category==='Music'&&/play$|pause$|stop$|repeat$|volume control/.test(t))return 'Partial: host player path; original panel and repeat-all/queue semantics incomplete';
    if(f.category==='Video'&&/play$|pause$|stop$|repeat$|volume control|fast reverse/.test(t))return 'Partial: host player path; original panel and format-specific semantics incomplete';
    if(f.category==='About the XMB™ menu'&&/xrossmediabar/.test(t))return 'Partial: category/page navigation; context menu and PS overlay missing';
    return f.hasChildren?'Group: child capabilities must be assessed independently':'Not implemented as a complete original workflow';
}
const webRead=new Set(['basicoperations/xmb.html','basicoperations/changeview.html','settings/themesettings.html','video/video.html','game/game.html','trophy/trophy.html','friends/friends.html','video/panel.html','music/panel.html','photo/panel.html','accountinfo/accountinfo.html','friends/panel.html'].map(p=>'https://manuals.playstation.net/document/en/ps3/current/'+p));
const features=source.features.map((f:any,i:number)=>{
    const local=source.pages.find((p:any)=>p.url===f.sourceUrl);
    return {...f,id:'XMB-'+String(i+1).padStart(3,'0'),backend:categoryBackend[f.category],nativeStatus:nativeStatus(f),referenceAccess:local&&!local.error?'Downloaded primary reference':webRead.has(f.sourceUrl)?'Primary reference read through web tool':'Index verified; detail page needs follow-up',visualStatus:'See VISUAL-BEHAVIOR.md; feature-specific transitions unmeasured unless explicitly listed'};
});
await Bun.write(join(destination,'feature-catalog.json'),JSON.stringify({scope:source.scope,counts:source.counts,features},null,2)+'\n');
const lines=['# XMB feature catalog','',
    'This inventory contains 307 indexed guide topics/actions, including group headings; 277 are leaves. It is a coverage baseline, not a claim that the complete firmware has only 307 features. Nested dialogs, state variants, dynamic content, device/plugin-specific actions and presentation requirements expand the test surface.',
    '', 'Source: [Sony PS3 manual index](https://manuals.playstation.net/document/en/ps3/current/manualindex.html). The guide covers 4.80 and later; local 4.93 visibility must be checked per model, region, account, connected devices and installed applications. Native extras such as a host folder picker are not counted as proof of original feature completion.',''];
for(const category of Object.keys(categoryBackend)){
    lines.push('## '+category,'','Required backend: '+categoryBackend[category]+'.','','| ID | Original topic or action | Native status | Reference |','|---|---|---|---|');
    for(const f of features.filter((x:any)=>x.category===category))lines.push('| '+f.id+' | '+f.path.slice(1).join(' → ').replaceAll('|','/')+' | '+f.nativeStatus+' | [Guide]('+f.sourceUrl+') |');
    lines.push('');
}
lines.push('## Reference coverage limits','','The bulk source collector retrieved 111 of 179 linked pages. The other 68 returned HTTP 403 even after a slower retry. Important overview, media-panel, trophy, account and Friends pages were additionally read through the web tool. A successful index link is not proof that every nested setting value or conditional behavior was verified. Per-feature `referenceAccess` records this distinction.','',
'No original media asset, firmware image, credential or console identity is included in this catalog. Raw downloaded HTML, original resource trees and frame extracts stay in ignored local output folders.','');
await Bun.write(join(destination,'FEATURE-CATALOG.md'),lines.join('\n'));
console.log({features:features.length,categories:Object.keys(categoryBackend).length,destination});
