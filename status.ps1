$Host.UI.RawUI.WindowTitle = 'XMB project status'
function SizeMB($p) { if (Test-Path $p) { [math]::Round(((Get-ChildItem $p -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum) / 1MB) } else { 0 } }
$tick = 0
while ($true) {
    $lines = @()
    $lines += "XMB project status   $(Get-Date -Format 'HH:mm:ss')   (refreshes every 2s)"
    $lines += ''
    $lines += '== RPCS3 clone  (C:\dev\rpcs3)'
    $total = (Select-String C:\dev\rpcs3\.gitmodules -Pattern '^\s*path' -ErrorAction SilentlyContinue).Count
    $cloned = (Select-String C:\dev\rpcs3-clone.log -Pattern "Cloning into 'C:/dev/rpcs3/" -ErrorAction SilentlyContinue).Count
    $git = @(Get-Process git -ErrorAction SilentlyContinue).Count
    # directory size walk is slow; refresh it every 5th tick
    if ($tick % 5 -eq 0) { $gitMB = SizeMB 'C:\dev\rpcs3\.git' }
    $lines += "   submodules started: $cloned / $total    downloaded: $gitMB MB    git processes: $git"
    $lines += "   current: $(Get-Content C:\dev\rpcs3-clone.log -Tail 1 -ErrorAction SilentlyContinue)"
    if ($git -eq 0 -and $cloned -gt 0) { $lines += '   clone finished' }
    $lines += ''
    $lines += '== vsh.elf'
    if (Test-Path C:\dev\xmb\rea\vsh_stats.txt) {
        $lines += '   analyzed: ' + ((Select-String C:\dev\xmb\rea\vsh_stats.txt -Pattern '^functions ').Line)
    }
    if (Test-Path C:\dev\xmb\rea\vsh_imports.csv) {
        $lines += "   imports resolved: $((Import-Csv C:\dev\xmb\rea\vsh_imports.csv).Count)"
    }
    $lines += ''
    $lines += '== Boot-path plugin analysis  (C:\dev\xmb\boot -> C:\dev\xmb\rea\plugins)'
    $plog = 'C:\dev\xmb\rea\plugins_headless.log'
    $staged = @(Get-ChildItem C:\dev\xmb\boot -Filter *.prx -ErrorAction SilentlyContinue).Count
    $done = @(Get-ChildItem C:\dev\xmb\rea\plugins -Filter *_stats.txt -ErrorAction SilentlyContinue)
    $lines += "   plugins analyzed: $($done.Count) / $staged"
    foreach ($d in $done) {
        $fn = (Select-String $d.FullName -Pattern '^functions ').Line
        $lines += "     $($d.Name -replace '_stats.txt','')  $fn"
    }
    if (Test-Path $plog) {
        $cur = Select-String $plog -Pattern 'IMPORTING: |REPORT: Analysis succeeded|Execute script' | Select-Object -Last 1
        if ($cur) { $lines += "   last: $($cur.Line.Trim())" }
    }
    $lines += ''
    $lines += '== RPCS3 Release build  (C:\dev\rpcs3\build)'
    $blog = 'C:\dev\rpcs3\build-release.log'
    if (Test-Path $blog) { Get-Content $blog -Tail 4 | ForEach-Object { $lines += "   $_" } } else { $lines += '   waiting for clone' }
    Clear-Host
    $lines | ForEach-Object { Write-Host $_ }
    $tick++
    Start-Sleep 2
}
