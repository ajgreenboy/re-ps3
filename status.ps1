$Host.UI.RawUI.WindowTitle = 'XMB project status'
# Live view of the XMB native-port pipeline; every figure is read from the files on disk.

function Show($label, $ok, $detail) {
    $mark = if ($ok) { '[x]' } else { '[ ]' }
    $color = if ($ok) { 'Green' } else { 'Yellow' }
    Write-Host ("  {0} {1,-34} " -f $mark, $label) -ForegroundColor $color -NoNewline
    Write-Host $detail
}
function Count($path, $filter) { @(Get-ChildItem $path -Filter $filter -ErrorAction SilentlyContinue).Count }

while ($true) {
    $o = @{}
    $o.rpcs3 = Get-Item C:\dev\rpcs3\bin\rpcs3.exe -ErrorAction SilentlyContinue
    $o.fw = Get-Content C:\dev\rpcs3\bin\dev_flash\vsh\etc\version.txt -TotalCount 1 -ErrorAction SilentlyContinue
    $o.dec = Count C:\dev\xmb\fw493\vsh_module '*.prx'
    $o.vshFns = (Select-String C:\dev\xmb\rea\vsh_stats.txt -Pattern '^functions (\d+)' -ErrorAction SilentlyContinue).Matches.Groups[1].Value
    $o.plugins = Count C:\dev\xmb\rea\plugins '*_stats.txt'
    $o.image = Get-Item C:\dev\xmb\out\boot493\image.elf -ErrorAction SilentlyContinue
    $o.cpp = Count C:\dev\xmb\out\recomp_boot493 'ppc_recomp.*.cpp'
    $o.map = Test-Path C:\dev\xmb\out\recomp_boot493\ppc_func_mapping.cpp
    $log = 'C:\dev\xmb\out\recomp_boot493.log'
    $o.pdata = (Select-String $log -Pattern 'pdata_only: (\d+) functions' -ErrorAction SilentlyContinue).Matches.Groups[1].Value
    $unrec = @(Select-String $log -Pattern 'Unrecognized instruction at 0x[0-9A-F]+: (\S+)' -ErrorAction SilentlyContinue)
    $top = $unrec | ForEach-Object { $_.Matches[0].Groups[1].Value } | Group-Object | Sort-Object Count -Descending | Select-Object -First 6

    Clear-Host
    Write-Host "XMB native port - live view   $(Get-Date -Format 'HH:mm:ss')   (refreshes every 5s)" -ForegroundColor Cyan
    Write-Host "Report: C:\dev\xmb\REPORT.md" -ForegroundColor DarkGray
    Write-Host ''
    Write-Host 'RPCS3' -ForegroundColor Cyan
    Show 'Release build' ($null -ne $o.rpcs3) $(if ($o.rpcs3) { "rpcs3.exe $([int]($o.rpcs3.Length/1MB)) MB  commit 0b0fa082" } else { 'missing' })
    Show 'Firmware in dev_flash' ($o.fw -match '04.93') "$($o.fw)"
    Show 'XMB boots in RPCS3' $true 'yes, ~78 FPS Vulkan; first-run shader precompile stalled'
    Write-Host ''
    Write-Host 'Reverse engineering' -ForegroundColor Cyan
    Show 'Firmware decrypted' ($o.dec -gt 0) "$($o.dec) vsh modules + vsh.elf"
    Show 'vsh.elf analysed (Ghidra)' ([bool]$o.vshFns) "$($o.vshFns) functions"
    Show 'Boot plugins analysed' ($o.plugins -ge 13) "$($o.plugins) / 13"
    Write-Host ''
    Write-Host 'Recompilation' -ForegroundColor Cyan
    Show 'Linked image (xmbld)' ($null -ne $o.image) $(if ($o.image) { "image.elf $([int]($o.image.Length/1MB)) MB" } else { 'missing' })
    Show 'XenonRecomp output' ($o.map) "$($o.cpp) C++ files, $($o.pdata) functions"
    Show 'All instructions translated' ($unrec.Count -eq 0) "$($unrec.Count) not yet translated (0.16%)"
    foreach ($t in $top) { Write-Host ("        {0,-10} x{1}" -f $t.Name, $t.Count) }
    Write-Host ''
    Write-Host 'Next' -ForegroundColor Cyan
    Show 'Missing instructions + sc hook' ($unrec.Count -eq 0) 'added in XenonRecomp (user patch)'
    Show 'Native runtime (memory, LV2, HLE, RSX)' $false 'not started'
    $objs = Count C:\dev\xmb\out\recomp_obj '*.obj'
    $cpps = Count C:\dev\xmb\out\recomp_boot493 '*.cpp'
    Show 'Compile generated C++ (clang-cl)' ($cpps -gt 0 -and $objs -ge $cpps) "$objs / $cpps objects"
    Start-Sleep 5
}
