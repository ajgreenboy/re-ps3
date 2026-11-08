param(
    [string]$PackageDirectory,
    [int]$Display = 1,
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
if(-not $PackageDirectory) { $PackageDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path }
$watchdog = Join-Path $PackageDirectory 'watchdog.ps1'
if(-not (Test-Path $watchdog)) { throw "watchdog.ps1 not found in '$PackageDirectory'." }

$taskName = 're-ps3-native-kiosk'

if($Remove) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$taskName'."
    exit 0
}

# Run the watchdog at logon, hidden, as the current user. The watchdog loops the kiosk XMB and restarts
# it after a crash; Task Scheduler's own RestartCount is a belt-and-suspenders guard for a watchdog failure.
$action   = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$watchdog`" -Display $Display"
$trigger  = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit (New-TimeSpan -Seconds 0)
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings `
    -Principal $principal -Description 'Start the PS3 XMB in kiosk mode (with crash watchdog) at logon.' -Force | Out-Null

Write-Host "Registered scheduled task '$taskName' to start the XMB kiosk (via watchdog.ps1) at logon."
Write-Host "For a fully unattended TV, also enable Windows auto-logon for this account (netplwiz or the"
Write-Host "AutoAdminLogon registry keys) - that step is documented in packaging/README.md, not automated."
