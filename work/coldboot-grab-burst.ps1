# Starts the scratch RPCS3 (capture build, scratch config) and saves its game window as fast as PrintWindow allows for
# -Seconds, as <Out>\f_<ms since launch>.jpg at half size. Only the started process is touched; it is stopped after.
param([string]$Exe,[string]$Config,[string]$Out,[int]$Seconds=30,[int]$Width=960)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public class Burst {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public struct Rect { public int L, T, R, B; }
}
'@
New-Item -ItemType Directory -Force $Out | Out-Null
$sw=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath $Exe -ArgumentList @('--config',$Config,(Join-Path (Split-Path $Exe) 'dev_flash\vsh\module\vsh.self')) -WorkingDirectory (Split-Path $Exe) -PassThru
$h=[IntPtr]::Zero; $n=0
while($sw.Elapsed.TotalSeconds -lt $Seconds) {
    if($h -eq [IntPtr]::Zero) {
        [Burst]::EnumWindows({param($w,$l) $q=0; [Burst]::GetWindowThreadProcessId($w,[ref]$q)|Out-Null; if($q -eq $p.Id -and [Burst]::IsWindowVisible($w)){ $sb=New-Object Text.StringBuilder 256; [Burst]::GetWindowText($w,$sb,256)|Out-Null; if($sb.ToString() -match 'FPS'){ $script:h=$w } }; $true},[IntPtr]::Zero)|Out-Null
        if($h -eq [IntPtr]::Zero){ Start-Sleep -Milliseconds 20; continue }
    }
    $r=New-Object Burst+Rect; [Burst]::GetClientRect($h,[ref]$r)|Out-Null
    $w=$r.R-$r.L; $ht=$r.B-$r.T; if($w -le 0){continue}
    $t=$sw.ElapsedMilliseconds
    $bmp=New-Object Drawing.Bitmap $w,$ht; $g=[Drawing.Graphics]::FromImage($bmp); $dc=$g.GetHdc(); [Burst]::PrintWindow($h,$dc,3)|Out-Null; $g.ReleaseHdc($dc); $g.Dispose()
    $small=New-Object Drawing.Bitmap $Width,([int]($ht*$Width/$w)); $g2=[Drawing.Graphics]::FromImage($small); $g2.DrawImage($bmp,0,0,$small.Width,$small.Height); $g2.Dispose(); $bmp.Dispose()
    $small.Save((Join-Path $Out ("f_{0:D6}.jpg" -f $t)),[Drawing.Imaging.ImageFormat]::Jpeg); $small.Dispose(); $n++
}
Stop-Process -Id $p.Id -Force
"$n frames"
