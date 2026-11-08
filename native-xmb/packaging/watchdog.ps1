param(
    [string]$FlashRoot,
    [string]$DataDirectory,
    [string]$VshModules,
    [int]$Display = 1,
    [int]$MinUptimeSeconds = 5,     # a run shorter than this counts as a failed start, not a crash
    [int]$MaxRapidRestarts = 5,     # give up after this many consecutive failed starts
    [int]$RestartDelaySeconds = 3
)
$ErrorActionPreference = 'Stop'
# The watchdog runs the packaged XMB in kiosk mode and restarts it after a crash. It lives next to the exe.
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 're-ps3-native.exe'
if(-not (Test-Path $exe)) { throw "re-ps3-native.exe not found next to the watchdog (expected in $root)." }

$log = Join-Path $root 'watchdog.log'
function Log($message) {
    $line = '{0}  {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $message
    Add-Content -LiteralPath $log -Value $line
    Write-Host $line
}

# Same path resolution as run-kiosk.ps1.
$config = $null
if(Test-Path (Join-Path $root 'xmb-config.json')) { $config = Get-Content -LiteralPath (Join-Path $root 'xmb-config.json') -Raw | ConvertFrom-Json }
if(-not $FlashRoot) { if($config.devFlash){ $FlashRoot = $config.devFlash } }
if(-not $FlashRoot) { $FlashRoot = Join-Path (Split-Path -Parent $root) 'rpcs3\bin\dev_flash' }
if(-not (Test-Path (Join-Path $FlashRoot 'vsh\resource\explore_plugin_full.rco'))) {
    throw "No firmware at '$FlashRoot'. Pass -FlashRoot, or write xmb-config.json next to the watchdog."
}
if(-not $DataDirectory) { if($config.dataDir){ $DataDirectory = $config.dataDir } }
if(-not $DataDirectory) { $DataDirectory = Join-Path $root 'data' }
if(-not $VshModules) { if($config.vshModules){ $VshModules = $config.vshModules } }

$arguments = @('--dev-flash', ('"'+$FlashRoot+'"'), '--data-dir', ('"'+$DataDirectory+'"'), '--kiosk', '--display', $Display)
if($VshModules -and (Test-Path (Join-Path $VshModules 'custom_render_plugin.prx'))) {
    $arguments += @('--vsh-modules', ('"'+$VshModules+'"'))
}

Log "watchdog started (exe $exe, display $Display)"
$consecutiveRapid = 0
while($true) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -PassThru
    $process.WaitForExit()
    $sw.Stop()
    $code = $process.ExitCode
    Log "exited with code $code after $([math]::Round($sw.Elapsed.TotalSeconds,1))s"

    if($code -eq 0) {
        Log "clean exit (code 0); watchdog stopping"
        break
    }
    # Usage/config errors (code 2) and very short runs mean it never really started: don't loop forever.
    if($code -eq 2 -or $sw.Elapsed.TotalSeconds -lt $MinUptimeSeconds) {
        $consecutiveRapid++
        Log "failed start #$consecutiveRapid (code $code, uptime under $MinUptimeSeconds s)"
        if($consecutiveRapid -ge $MaxRapidRestarts) {
            Log "too many rapid restarts; giving up"
            break
        }
    } else {
        $consecutiveRapid = 0
    }
    Log "restarting in $RestartDelaySeconds s"
    Start-Sleep -Seconds $RestartDelaySeconds
}
