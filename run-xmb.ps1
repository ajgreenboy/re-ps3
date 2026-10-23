param([int]$MaximumRestarts = 3, [switch]$TraceCalls)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$runtimeDirectory = Join-Path $projectRoot 'out\xmbrt'
$executable = Join-Path $runtimeDirectory 'xmbrt.exe'
$image = Join-Path $projectRoot 'out\boot493'
$firmware = Join-Path $projectRoot 'fw493\vsh_module\vsh.elf'
if (!(Test-Path -LiteralPath $executable)) { throw 'Build xmbrt before starting XMB.' }
$sessionDirectory = Join-Path $runtimeDirectory ('sessions\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $sessionDirectory -Force | Out-Null
$environmentNames = @('XMBRT_MAX_CALLS','XMBRT_IDLE_SECONDS','XMBRT_DUMP_AT','XMBRT_LOG','XMBRT_BUFFERED_TRACE','XMBRT_NO_CALL_TRACE')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:XMBRT_MAX_CALLS = '0'
    $env:XMBRT_IDLE_SECONDS = '0'
    $env:XMBRT_DUMP_AT = '0'
    $env:XMBRT_BUFFERED_TRACE = '1'
    $env:XMBRT_NO_CALL_TRACE = if($TraceCalls){$null}else{'1'}
    for ($attempt = 0; $attempt -le $MaximumRestarts; ++$attempt) {
        $env:XMBRT_LOG = Join-Path $sessionDirectory "boot-$attempt.log"
        Write-Host "Starting XMB (boot $attempt). Logs: $sessionDirectory"
        $process = Start-Process -FilePath $executable -ArgumentList ('"{0}" "{1}"' -f $image,$firmware) `
            -WorkingDirectory $runtimeDirectory -NoNewWindow -PassThru `
            -RedirectStandardError (Join-Path $sessionDirectory "boot-$attempt.stderr.txt") `
            -RedirectStandardOutput (Join-Path $sessionDirectory "boot-$attempt.stdout.txt")
        $process.WaitForExit()
        $runtimeExit = $process.ExitCode
        if ($runtimeExit -ne 75) {
            if ($runtimeExit -ne 0) { throw "XMB exited with code $runtimeExit. Logs: $sessionDirectory" }
            break
        }
        if ($attempt -eq $MaximumRestarts) { throw "XMB reached the restart limit ($MaximumRestarts). Logs: $sessionDirectory" }
        Write-Host 'XMB requested a guest reboot; restarting the runtime.'
    }
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
}
