param([int]$First = 15, [int]$Second = 50)
# Per-host-thread CPU profile of an unattended boot. Samples thread CPU times at $First and $Second
# seconds, then lets XMBRT_DUMP_AT dump guest threads (with host_tid) and exit. SPU host threads are
# named by XMBRT_LOG_HOST_TIDS. Prints the top threads by CPU used between the two samples.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root 'out\xmbrt'
$session = Join-Path $runtime ('sessions\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-profile')
New-Item -ItemType Directory -Path $session -Force | Out-Null
$env:XMBRT_MAX_CALLS = '0'; $env:XMBRT_IDLE_SECONDS = '0'; $env:XMBRT_DUMP_AT = [string]($Second + 3)
$env:XMBRT_BUFFERED_TRACE = '1'; $env:XMBRT_NO_CALL_TRACE = '1'; $env:XMBRT_LOG_HOST_TIDS = '1'
$log = Join-Path $session 'boot-0.log'
$env:XMBRT_LOG = $log
$p = Start-Process -FilePath (Join-Path $runtime 'xmbrt.exe') `
    -ArgumentList ('"{0}" "{1}"' -f (Join-Path $root 'out\boot493'), (Join-Path $root 'fw493\vsh_module\vsh.elf')) `
    -WorkingDirectory $runtime -NoNewWindow -PassThru `
    -RedirectStandardError (Join-Path $session 'boot-0.stderr.txt') -RedirectStandardOutput (Join-Path $session 'boot-0.stdout.txt')
function Sample { $process = Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if (-not $process) { return $null }; $h = @{}; foreach ($t in $process.Threads) { try { $h[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }; @{ Threads = $h; CpuMs = $process.TotalProcessorTime.TotalMilliseconds; At = [System.Diagnostics.Stopwatch]::GetTimestamp() / [double][System.Diagnostics.Stopwatch]::Frequency } }
Start-Sleep -Seconds $First; $a = Sample
if (-not $a) { $p.WaitForExit(); "session: $session"; "runtime exited before first sample: $($p.ExitCode)"; return }
Start-Sleep -Seconds ($Second - $First); $b = Sample
if (-not $b) { $p.WaitForExit(); "session: $session"; "runtime exited before second sample: $($p.ExitCode)"; return }
if (-not $p.WaitForExit(30000)) { $p.Kill(); $p.WaitForExit() }
Start-Sleep -Milliseconds 500
$names = @{}
foreach ($l in Get-Content $log) {
    if ($l -match "^  thread ([0-9A-F]+) '([^']*)' host_tid=(\d+)") { $names[[int]$Matches[3]] = "ppu $($Matches[1]) $($Matches[2])" }
    elseif ($l -match '^host tid (\d+): (.*)$') { $names[[int]$Matches[1]] = $Matches[2] }
}
$window = ($b.At - $a.At) * 1000
$rows = foreach ($id in $b.Threads.Keys) { $d = $b.Threads[$id] - $(if ($a.Threads.ContainsKey($id)) { $a.Threads[$id] } else { 0 }); [pscustomobject]@{ tid = $id; cpu_pct = [math]::Round(100 * $d / $window, 1); name = $(if ($names.ContainsKey($id)) { $names[$id] } else { '?' }) } }
"session: $session"
"process cpu over window (includes exited threads): {0:N0}% of one core" -f (100 * ($b.CpuMs - $a.CpuMs) / $window)
"live thread cpu over window: {0:N0}% of one core (omits threads that exited before the second sample)" -f (($rows | Measure-Object cpu_pct -Sum).Sum)
$rows | Sort-Object cpu_pct -Descending | Select-Object -First 15 | Format-Table -AutoSize | Out-String
