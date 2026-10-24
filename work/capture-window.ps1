# Capture the native XMB window to PNGs: brings it to the front, then grabs its client area N times.
# Usage: powershell -File work/capture-window.ps1 -OutDir <dir> [-Count 6] [-IntervalMs 1000] [-Keys "RIGHT,RIGHT,DOWN"]
param([string]$OutDir, [int]$Count = 6, [int]$IntervalMs = 1000, [string]$Keys = "")
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[Win]::SetProcessDPIAware() | Out-Null
$proc = Get-Process re-ps3-native -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $proc.MainWindowHandle
[Win]::ShowWindow($h, 9) | Out-Null
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600
New-Item -ItemType Directory -Force $OutDir | Out-Null
$keyList = if ($Keys) { $Keys.Split(',') } else { @() }
for ($i = 0; $i -lt $Count; $i++) {
  if ($i -lt $keyList.Count -and $keyList[$i]) { [System.Windows.Forms.SendKeys]::SendWait("{$($keyList[$i])}"); Start-Sleep -Milliseconds 400 }
  $r = New-Object Win+RECT; [Win]::GetClientRect($h, [ref]$r) | Out-Null
  $p = New-Object Win+POINT; [Win]::ClientToScreen($h, [ref]$p) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($p.X, $p.Y, 0, 0, $bmp.Size); $g.Dispose()
  $file = Join-Path $OutDir ("client-{0:D2}.png" -f $i)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  "$file ${w}x$hh"
  Start-Sleep -Milliseconds $IntervalMs
}
