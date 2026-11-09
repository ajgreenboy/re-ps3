param(
    [string]$PackageDirectory,
    [int]$Display = 1,
    # Remove only the logon task (what the XMB's "Start at Sign-in" row does when switched Off).
    [switch]$Remove,
    # Remove the logon task AND the Start-menu shortcuts: the clean uninstall.
    [switch]$Uninstall,
    # Also create the Start-menu shortcuts "XMB" and "Return to XMB".
    [switch]$Shortcuts,
    # Create only the shortcuts, no logon task.
    [switch]$NoTask,
    # Where the shortcuts go. Default: the current user's Start menu Programs folder. Tests pass a scratch folder.
    [string]$ShortcutDirectory,
    # The scheduled task's name. XMB_AUTOSTART_TASK overrides the default so tests never touch the real task.
    [string]$TaskName
)
$ErrorActionPreference = 'Stop'
if(-not $PackageDirectory) { $PackageDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path }
if(-not $TaskName) { $TaskName = $env:XMB_AUTOSTART_TASK }
if(-not $TaskName) { $TaskName = 're-ps3-native-kiosk' }
if(-not $ShortcutDirectory) { $ShortcutDirectory = [Environment]::GetFolderPath('Programs') }

$watchdog = Join-Path $PackageDirectory 'watchdog.ps1'
$exe = Join-Path $PackageDirectory 're-ps3-native.exe'
$shortcutNames = @('XMB.lnk', 'Return to XMB.lnk')

function Remove-Shortcuts {
    foreach($name in $shortcutNames) {
        $path = Join-Path $ShortcutDirectory $name
        if(Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force; Write-Host "Removed shortcut '$path'." }
    }
}

if($Remove -or $Uninstall) {
    # Removing needs neither the watchdog nor the exe: a half-deleted package must still be uninstallable.
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed scheduled task '$TaskName'."
    if($Uninstall) { Remove-Shortcuts }
    exit 0
}

if(-not (Test-Path -LiteralPath $watchdog)) { throw "watchdog.ps1 not found in '$PackageDirectory'." }

if(-not $NoTask) {
    # Run the watchdog at logon, hidden, as the current user: no administrator rights and no stored password
    # (LogonType Interactive, so it only runs while this user is signed in). The watchdog loops the kiosk XMB
    # and restarts it after a crash; Task Scheduler's own RestartCount is a belt-and-suspenders guard.
    # The XMB's "Start at Sign-in" row calls this script, so the row and the script cannot disagree about what
    # "on" means: a task of this name whose action runs the watchdog.ps1 beside the exe.
    $action   = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$watchdog`" -Display $Display"
    # -User matters: an at-logon trigger for "any user" needs administrator rights to register, one for this user does not.
    $trigger  = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit (New-TimeSpan -Seconds 0)
    $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings `
        -Principal $principal -Description 'Start the PS3 XMB in kiosk mode (with crash watchdog) at logon.' -Force | Out-Null

    Write-Host "Registered scheduled task '$TaskName' to start the XMB kiosk (via watchdog.ps1) at logon."
    Write-Host "For a fully unattended TV, also enable Windows automatic sign-in for this account (netplwiz or"
    Write-Host "Sysinternals Autologon) - that step is documented in packaging/README.md, not automated."
}

if($Shortcuts -or $NoTask) {
    if(-not (Test-Path -LiteralPath $exe)) { throw "re-ps3-native.exe not found in '$PackageDirectory'." }
    New-Item -ItemType Directory -Path $ShortcutDirectory -Force | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    # "XMB" starts the kiosk through the watchdog (hidden console), exactly as the logon task does.
    $link = $shell.CreateShortcut((Join-Path $ShortcutDirectory 'XMB.lnk'))
    $link.TargetPath = (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe')
    $link.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$watchdog`" -Display $Display"
    $link.WorkingDirectory = $PackageDirectory
    $link.IconLocation = "$exe,0"
    $link.Description = 'Start the PS3 XMB in kiosk mode'
    $link.Save()
    # "Return to XMB" asks the running XMB to come back to the front (single-instance --show, PHASES D2).
    $link = $shell.CreateShortcut((Join-Path $ShortcutDirectory 'Return to XMB.lnk'))
    $link.TargetPath = $exe
    # The single-instance channel is named per data folder, so --show must name the folder the watchdog's XMB uses:
    # xmb-config.json's dataDir when set, else <package>\data (watchdog.ps1 resolves it the same way).
    $dataDirectory = Join-Path $PackageDirectory 'data'
    $configPath = Join-Path $PackageDirectory 'xmb-config.json'
    if(Test-Path -LiteralPath $configPath) {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        if($config.dataDir) { $dataDirectory = $config.dataDir }
    }
    $link.Arguments = '--show --data-dir "' + $dataDirectory + '"'
    $link.WorkingDirectory = $PackageDirectory
    $link.IconLocation = "$exe,0"
    $link.Description = 'Return to the PS3 XMB'
    $link.Save()
    Write-Host "Created shortcuts 'XMB' and 'Return to XMB' in '$ShortcutDirectory'."
}
