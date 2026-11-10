param(
    [string]$FlashRoot,
    [string]$DataDirectory,
    [string]$VshModules,
    [int]$Display = 1,
    [int]$MinUptimeSeconds = 5,     # a run shorter than this counts as a failed start, not a crash
    [int]$MaxRapidRestarts = 5,     # give up after this many consecutive failed starts (safe-mode starts included)
    [int]$RestartDelaySeconds = 3,
    [int]$SafeAfterCrashes = 2,     # D6: this many crashes in a row within -CrashWindowSeconds start the XMB with --safe
    [int]$CrashWindowSeconds = 300, # D6: the window those crashes must fall in
    [int]$StableSeconds = 60,       # D6: a run this long ends the crash streak, and a safe run this long returns to normal
    [long]$MaxLogBytes = 5MB,       # D6: watchdog.log rotates at this size
    [int]$KeepLogs = 3              # D6: rotated copies kept (watchdog.log.1 ... .N)
)
$ErrorActionPreference = 'Stop'
# The watchdog runs the packaged XMB in kiosk mode and restarts it after a crash. It lives next to the exe.
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 're-ps3-native.exe'
if(-not (Test-Path $exe)) { throw "re-ps3-native.exe not found next to the watchdog (expected in $root)." }

$log = Join-Path $root 'watchdog.log'
function Rotate-Log([string]$path) {
    # watchdog.log -> .1 -> .2 -> ... keeping $KeepLogs old files, once the file reaches $MaxLogBytes.
    if(-not (Test-Path -LiteralPath $path)) { return }
    if((Get-Item -LiteralPath $path).Length -lt $MaxLogBytes) { return }
    $oldest = "$path.$KeepLogs"
    if(Test-Path -LiteralPath $oldest) { Remove-Item -LiteralPath $oldest -Force }
    for($i = $KeepLogs - 1; $i -ge 1; $i--) {
        if(Test-Path -LiteralPath "$path.$i") { Move-Item -LiteralPath "$path.$i" -Destination ("$path.{0}" -f ($i + 1)) -Force }
    }
    Move-Item -LiteralPath $path -Destination "$path.1" -Force
}
function Log($message) {
    $line = '{0}  {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $message
    # A log problem (another process reading the file at that instant, a full disk) must never stop the watchdog.
    for($try = 0; $try -lt 5; $try++) {
        try { Rotate-Log $log; Add-Content -LiteralPath $log -Value $line -ErrorAction Stop; break }
        catch { Start-Sleep -Milliseconds 150 }
    }
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

# D6: the app's own log goes to <data>\logs\xmb.log (the app rotates it at 5 MB, keeping 3), unless the caller already chose XMB_LOG.
if(-not $env:XMB_LOG) {
    New-Item -ItemType Directory -Path (Join-Path $DataDirectory 'logs') -Force | Out-Null
    $env:XMB_LOG = Join-Path $DataDirectory 'logs\xmb.log'
}

Log "watchdog started (exe $exe, display $Display, app log $env:XMB_LOG)"
Log "policy: safe mode after $SafeAfterCrashes crashes within $CrashWindowSeconds s; back to normal after a safe run of $StableSeconds s or more; give up after $MaxRapidRestarts rapid failed starts"
$consecutiveRapid = 0
$crashTimes = New-Object System.Collections.Generic.List[datetime]
$safe = $false
while($true) {
    $runArguments = $arguments
    if($safe) { $runArguments = $arguments + @('--safe') }
    Log ("starting the XMB in {0} mode" -f $(if($safe) { 'SAFE' } else { 'normal' }))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList $runArguments -WorkingDirectory $root -PassThru
    $process.WaitForExit()
    $sw.Stop()
    $code = $process.ExitCode
    $uptime = $sw.Elapsed.TotalSeconds
    Log "exited with code $code after $([math]::Round($uptime,1))s"

    if($code -eq 0) {
        Log "clean exit (code 0); watchdog stopping"
        break
    }

    # D6: a run that lasted long enough ends the crash streak; a safe run that long also returns the XMB to normal mode.
    if($uptime -ge $StableSeconds) {
        if($crashTimes.Count -gt 0) { Log "run lasted $([math]::Round($uptime,1))s (>= $StableSeconds s); the crash streak is cleared" }
        $crashTimes.Clear()
        if($safe) {
            Log "safe-mode run lasted $([math]::Round($uptime,1))s (>= $StableSeconds s); returning to normal mode"
            $safe = $false
        }
    }

    # Usage/config errors (code 2) and very short runs mean it never really started: don't loop forever.
    if($code -eq 2 -or $uptime -lt $MinUptimeSeconds) {
        $consecutiveRapid++
        Log ("failed {0}start #{1} (code {2}, uptime under {3} s)" -f $(if($safe) { 'SAFE-MODE ' } else { '' }), $consecutiveRapid, $code, $MinUptimeSeconds)
        if($consecutiveRapid -ge $MaxRapidRestarts) {
            Log "too many rapid restarts$(if($safe) { ' (the safe-mode start failed too)' }); giving up"
            break
        }
    } else {
        $consecutiveRapid = 0
    }

    # D6: count this exit as a crash (a usage error is not one) and decide the next start's mode.
    if($code -ne 2) {
        $now = Get-Date
        $crashTimes.Add($now)
        for($i = $crashTimes.Count - 1; $i -ge 0; $i--) {
            if(($now - $crashTimes[$i]).TotalSeconds -gt $CrashWindowSeconds) { $crashTimes.RemoveAt($i) }
        }
        Log "crash $($crashTimes.Count) of $SafeAfterCrashes within the last $CrashWindowSeconds s"
        if(-not $safe -and $crashTimes.Count -ge $SafeAfterCrashes) {
            $safe = $true
            Log "$($crashTimes.Count) crashes in a row; the next start is in SAFE MODE (--safe)"
        }
    }
    Log "restarting in $RestartDelaySeconds s"
    Start-Sleep -Seconds $RestartDelaySeconds
}
