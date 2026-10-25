# Saves a PNG of the main window of one process (by PID) - only that process, e.g. a scratch RPCS3 instance.
param([int]$ProcessId,[string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public class GrabWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public struct Rect { public int L, T, R, B; }
}
'@
$found=@()
[GrabWin]::EnumWindows({param($h,$l) $p=0; [GrabWin]::GetWindowThreadProcessId($h,[ref]$p)|Out-Null; if($p -eq $ProcessId -and [GrabWin]::IsWindowVisible($h)){ $sb=New-Object Text.StringBuilder 256; [GrabWin]::GetWindowText($h,$sb,256)|Out-Null; $script:found+=,@($h,$sb.ToString()) }; $true},[IntPtr]::Zero)|Out-Null
foreach($w in $found){ "window: $($w[1])" }
$w=$found | Where-Object { $_[1] -match 'FPS' } | Select-Object -First 1
if(-not $w){$w=$found | Select-Object -First 1}
if(-not $w){'no window';exit 1}
$r=New-Object GrabWin+Rect; [GrabWin]::GetWindowRect($w[0],[ref]$r)|Out-Null
$bmp=New-Object Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T); $g=[Drawing.Graphics]::FromImage($bmp)
$dc=$g.GetHdc(); [GrabWin]::PrintWindow($w[0],$dc,2)|Out-Null; $g.ReleaseHdc($dc)
$bmp.Save($Out,[Drawing.Imaging.ImageFormat]::Png); "saved $Out ($($r.R-$r.L)x$($r.B-$r.T)) from '$($w[1])'"
