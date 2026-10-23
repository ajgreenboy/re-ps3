param([string]$Label = 'run', [int]$Seconds = 75, [hashtable]$Environment = @{})
# Timed, input-free runtime launch for A/B measurements. Same environment as run-xmb.ps1
# (no call trace, no limits). A guest reboot (exit 75) restarts the runtime like run-xmb.ps1, logging
# boot-N; the last boot is killed after $Seconds. Prints the session directory.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root 'out\xmbrt'
$session = Join-Path $runtime ('sessions\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + "-$Label")
New-Item -ItemType Directory -Path $session -Force | Out-Null
$env:XMBRT_MAX_CALLS = '0'; $env:XMBRT_IDLE_SECONDS = '0'; $env:XMBRT_DUMP_AT = '0'
$env:XMBRT_BUFFERED_TRACE = '1'; $env:XMBRT_NO_CALL_TRACE = '1'
foreach ($k in $Environment.Keys) { [Environment]::SetEnvironmentVariable($k, $Environment[$k], 'Process') }
for ($boot = 0; $boot -le 3; $boot++) {
    $env:XMBRT_LOG = Join-Path $session "boot-$boot.log"
    $p = Start-Process -FilePath (Join-Path $runtime 'xmbrt.exe') `
        -ArgumentList ('"{0}" "{1}"' -f (Join-Path $root 'out\boot493'), (Join-Path $root 'fw493\vsh_module\vsh.elf')) `
        -WorkingDirectory $runtime -NoNewWindow -PassThru `
        -RedirectStandardError (Join-Path $session "boot-$boot.stderr.txt") -RedirectStandardOutput (Join-Path $session "boot-$boot.stdout.txt")
    if (-not $p.WaitForExit($Seconds * 1000)) { $p.Kill(); $p.WaitForExit(); break }
    if ($p.ExitCode -ne 75) { break }
}
foreach ($k in $Environment.Keys) { [Environment]::SetEnvironmentVariable($k, $null, 'Process') }
$session
