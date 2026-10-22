param([int]$DelaySeconds = 0, [string]$Out = 'C:\dev\xmb\xmb_on_rpcs3_build.png')
# Captures the RPCS3 game window (title contains "FPS") to a PNG after an optional delay.
Start-Sleep $DelaySeconds
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class RpcsWin {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    public struct Rect { public int L, T, R, B; }
}
'@
$p = Get-Process rpcs3 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -match 'FPS' } | Select-Object -First 1
if (-not $p) { 'no RPCS3 game window'; Get-Process rpcs3 -ErrorAction SilentlyContinue | ForEach-Object MainWindowTitle; exit 1 }
$p.MainWindowTitle
[RpcsWin]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep 2
$r = New-Object RpcsWin+Rect
[RpcsWin]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object Drawing.Bitmap $w, $h
[Drawing.Graphics]::FromImage($bmp).CopyFromScreen($r.L, $r.T, 0, 0, (New-Object Drawing.Size $w, $h))
$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
"saved $Out"
