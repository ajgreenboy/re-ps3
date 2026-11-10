// Idle-cost measurement (D12): process CPU time over a window at rest on the XMB root, the per-thread split, the
// frame rate the build reached and the GPU engine utilisation for the PID. Each run uses a scratch data directory
// under C:\dev\out\d12i, XMB_SINGLE_INSTANCE=0, and stops only the PID it started.
//
//   bun work/idle-cost.ts --exe C:/dev/out/d12i-build/re-ps3-native.exe --mode windowed|kiosk
//        [--runs 3] [--settle 25] [--window 10] [--env K=V,K=V] [--label before]
//
// Output: one JSON line per run and a summary line with the medians. cpuPctOfCore is the process CPU time over the
// window divided by the window (100 = one core). msPerFrame divides the same CPU time by the frames the app swapped.
import {spawn} from "node:child_process";
import {mkdirSync,rmSync,readFileSync,existsSync,openSync} from "node:fs";
const args=process.argv.slice(2);
const opt=(n:string,d:string)=>{const i=args.indexOf("--"+n);return i>=0?args[i+1]:d;};
const exe=opt("exe","C:/dev/out/d12i-build/re-ps3-native.exe");
const mode=opt("mode","windowed");
const runs=Number(opt("runs","1"));
const settle=Number(opt("settle","25")),windowSec=Number(opt("window","10"));
const label=opt("label","");
const extraEnv=Object.fromEntries(opt("env","").split(",").filter(Boolean).map(kv=>{const i=kv.indexOf("=");return [kv.slice(0,i),kv.slice(i+1)] as [string,string];}));
const ps=(cmd:string)=>new Promise<string>(res=>{const p=spawn("powershell",["-NoProfile","-Command",cmd]);let o="";p.stdout.on("data",d=>o+=d);p.stderr.on("data",d=>{if(process.env.IDLE_DEBUG)console.error(String(d).slice(0,600));});p.on("close",()=>res(o));});
const median=(v:number[])=>{const s=[...v].sort((a,b)=>a-b);return s[Math.floor(s.length/2)];};

// Once a second for the window: the GPU engine utilisation of the PID, and whether another window was on top of the
// centre of the app's window (a covered window is not presented, which would make the run look cheaper than it is).
const sampler=(pid:number)=>`
Add-Type @'
using System;using System.Runtime.InteropServices;
public class Hit{[StructLayout(LayoutKind.Sequential)]public struct R{public int l,t,r,b;}
[StructLayout(LayoutKind.Sequential)]public struct P{public int x,y;}
[DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out R r);
[DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(P p);
[DllImport("user32.dll")]public static extern IntPtr GetAncestor(IntPtr h,uint f);}
'@
$sum=@();$covered=0;$proc=Get-Process -Id ${pid}
1..${windowSec}|ForEach-Object{
 $proc.Refresh();$h=$proc.MainWindowHandle;$r=New-Object Hit+R
 if($h -ne [IntPtr]::Zero -and [Hit]::GetWindowRect($h,[ref]$r)){$pt=New-Object Hit+P;$pt.x=[int](($r.l+$r.r)/2);$pt.y=[int](($r.t+$r.b)/2);$top=[Hit]::GetAncestor([Hit]::WindowFromPoint($pt),2);if($top -ne $h){$covered++}}
 $s=(Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue).CounterSamples|Where-Object{$_.InstanceName -like 'pid_${pid}_*'};$by=@{};foreach($x in $s){$k=($x.InstanceName -replace '.*engtype_','');$by[$k]=[double]$by[$k]+$x.CookedValue};$sum+=,$by}
$keys=$sum|ForEach-Object{$_.Keys}|Sort-Object -Unique;$r=@{covered=$covered};foreach($k in $keys){$r[$k]=[math]::Round((($sum|ForEach-Object{[double]$_[$k]})|Measure-Object -Average).Average,2)};$r|ConvertTo-Json -Compress`;
async function once(run:number){
  const data=`C:/dev/out/d12i/data-${mode}`;
  const errPath=`C:/dev/out/d12i/idle-${mode}-${run}.err`;
  rmSync(data,{recursive:true,force:true});mkdirSync(data,{recursive:true});
  const err=openSync(errPath,"w"),outFd=openSync("C:/dev/out/d12i/idle.out","w");
  const child=spawn(exe,["--data-dir",data,"--dev-flash","C:/dev/rpcs3/bin/dev_flash","--no-intro",...(mode==="kiosk"?["--kiosk"]:[])],
    {env:{...process.env,XMB_SINGLE_INSTANCE:"0",...extraEnv},stdio:["ignore",outFd,err]});
  const pid=child.pid!;
  await new Promise(r=>setTimeout(r,settle*1000));
  const snap=`$p=Get-Process -Id ${pid};$h=@{};foreach($t in $p.Threads){try{$h[[string]$t.Id]=$t.TotalProcessorTime.TotalMilliseconds}catch{}};@{cpu=$p.TotalProcessorTime.TotalMilliseconds;th=$h}|ConvertTo-Json -Compress`;
  const a=JSON.parse(await ps(snap));
  const t0=Date.now();
  const gpuP=ps(sampler(pid));
  await new Promise(r=>setTimeout(r,windowSec*1000));
  const b=JSON.parse(await ps(snap));
  const wall=(Date.now()-t0)/1000;
  const gpu=(await gpuP).trim();
  await ps(`Stop-Process -Id ${pid} -Force`);
  await new Promise(r=>setTimeout(r,500));
  let fps=0;
  if(existsSync(errPath)){
    const m=[...readFileSync(errPath,"utf8").matchAll(/native frames: ([0-9.]+) fps/g)];
    if(m.length)fps=Number(m[m.length-1][1]);
  }
  const top=Object.keys(b.th).map(k=>[k,b.th[k]-(a.th[k]??0)] as [string,number]).filter(x=>x[1]>1).sort((x,y)=>y[1]-x[1]).slice(0,4);
  const cpuMs=b.cpu-a.cpu;
  return {label,mode,run,pid,wallSec:+wall.toFixed(2),cpuMs:Math.round(cpuMs),cpuPctOfCore:+(cpuMs/10/wall).toFixed(1),fps,
    msPerFrame:fps?+(cpuMs/(fps*wall)).toFixed(2):null,topThreadsMs:top.map(([k,v])=>`${k}:${v.toFixed(0)}`),gpuPctByEngine:gpu};
}
const results=[];
for(let i=1;i<=runs;i++){const r=await once(i);results.push(r);console.log(JSON.stringify(r));}
console.log(JSON.stringify({summary:true,label,mode,runs,medianCpuPctOfCore:median(results.map(r=>r.cpuPctOfCore)),medianFps:median(results.map(r=>r.fps)),
  medianMsPerFrame:median(results.map(r=>r.msPerFrame??0))}));
