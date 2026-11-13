<#
.SYNOPSIS
    Guided HTPC release run: walks audit/RELEASE-CHECKLIST.md row by row on the living-room PC, runs the checks
    that can be scripted, records each result and writes the run report.

.DESCRIPTION
    Actions (first argument):

      preflight   Read-only readiness check: package, firmware, config, screens, pads, service files, Steam,
                  Tailscale, autostart, automatic sign-in, GitHub reachability. Writes <evidence>\preflight.txt.
      run         Walk the rows that have no result yet, sitting by sitting (-Sitting N, -Only ID,ID, -Redo).
      status      Show how many rows are recorded per sitting and what is next.
      launch      Quit the running XMB and start it again under the watchdog, with test switches
                  (-Set XMB_INPUT_TRACE=1,XMB_POWER_DRY_RUN=1), or directly with -NoIntro, -FirstRun, -Windowed.
      soak        Sample re-ps3-native.exe memory, handles, threads and CPU to <evidence>\soak-<Label>.csv
                  (-Hours 4 -IntervalSeconds 60 -Label LS1). Run it in a second PowerShell window.
      collect     Copy the XMB log, the watchdog log and the crash summaries into <evidence>: scrubbed copies in
                  logs\, untouched copies in raw\ (raw\ never leaves the HTPC).
      report      Write <evidence>\RELEASE-RUN-<date>-htpc.md from results.csv.
      selftest    Offline checks of this script (checklist parsing, sittings, scrubbing, results, report).

    Nothing here changes a Windows setting. Every live action (power, audio, network, Steam) is something the
    operator does by hand when the row says so. Logs are scrubbed with the rules of work/scrub-log.ts before they
    are copied into logs\.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 preflight -Package C:\PS3\re-ps3
    powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 run -Package C:\PS3\re-ps3
    powershell -ExecutionPolicy Bypass -File .\htpc-run.ps1 report
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('preflight', 'run', 'status', 'launch', 'soak', 'collect', 'report', 'selftest')]
    [string]$Action = 'status',
    [string]$Evidence,             # default C:\xmb-evidence\<yyyy-MM-dd>-htpc (remembered in %LOCALAPPDATA%)
    [string]$Package,              # the installed package folder (re-ps3-native.exe, watchdog.ps1, xmb-config.json)
    [string]$Checklist,            # default: ..\audit\RELEASE-CHECKLIST.md, or RELEASE-CHECKLIST.md next to this script
    [string]$Zip,                  # the downloaded re-ps3.zip (V1); its .sha256 is looked for next to it
    [string]$Version = '0.3.0',    # the release under test
    [int]$Display = 1,
    [int]$Sitting = 0,
    [string[]]$Only,
    [switch]$Redo,
    [string[]]$Set,                # launch: NAME=VALUE environment switches for this start only
    [switch]$NoIntro,
    [switch]$FirstRun,
    [switch]$Windowed,
    [switch]$NoWatchdog,
    [double]$Hours = 4,
    [int]$IntervalSeconds = 60,
    [string]$Label = 'LS1'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$Utf8 = New-Object System.Text.UTF8Encoding $false
$IsWin = ($env:OS -eq 'Windows_NT')
$ResultNames = @{ p = 'PASS'; f = 'FAIL'; a = 'PARTIAL'; n = 'NOT RUN' }

# ---------------------------------------------------------------------------------------------------------------
# Sittings: every checklist row belongs to exactly one, in the order the guide runs them (selftest enforces it).
# ---------------------------------------------------------------------------------------------------------------
$Sittings = @(
    @{ N = 1;  Title = 'Install, version and first start'; Launch = ''
       Needs = 'The downloaded re-ps3.zip and re-ps3.zip.sha256, dev_flash on the HTPC, a pad.'
       Ids = @('V1', 'FR1', 'UP1', 'UP2', 'V2', 'UP3') },
    @{ N = 2;  Title = 'Boot and kiosk'; Launch = 'launch (B1), then launch -NoIntro (B3)'
       Needs = 'The TV on display 1.'
       Ids = @('B1', 'B2', 'B3', 'AS1', 'AS2') },
    @{ N = 3;  Title = 'Controller only'; Launch = 'launch -Set XMB_INPUT_TRACE=1'
       Needs = 'Keyboard and mouse unplugged. An Xbox-layout pad, then a DualSense over USB if you have one.'
       Ids = @('N1', 'N2', 'N3', 'N4', 'N5', 'N6', 'N7', 'N8') },
    @{ N = 4;  Title = 'Remote and media keys'; Launch = 'launch -Set XMB_INPUT_TRACE=1'
       Needs = 'Your real remote. An album of 3+ tracks and a video in the libraries. No other media app running.'
       Ids = @('R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7', 'R8', 'R9') },
    @{ N = 5;  Title = 'Local media and files'; Launch = ''
       Needs = 'A video with a .srt, one with a PGS .sup, one with a VobSub .idx/.sub pair, some photos, a throwaway file to delete.'
       Ids = @('LM1', 'LM2', 'LM3', 'LM4', 'LM5', 'LM6', 'LM7', 'CD1', 'CD2', 'CD3') },
    @{ N = 6;  Title = 'Settings, users and dry runs'; Launch = 'launch -Set XMB_POWER_DRY_RUN=1,XMB_AUDIO_DRY_RUN=1 (PW1, AU2), then plain launch (AU1)'
       Needs = 'Two audio outputs (for example TV HDMI and a headset). Two XMB users for US1.'
       Ids = @('PW1', 'AU2', 'AU1', 'SC1', 'SC2', 'US1', 'US2') },
    @{ N = 7;  Title = 'Media accounts'; Launch = ''
       Needs = 'Jellyfin server address and login, Spotify Premium (and a free account if you have one), a DLNA server running, Discord app client id if Discord sign-in has shipped.'
       Ids = @('JF1', 'JF2', 'JF3', 'JF4', 'JF5', 'JF6', 'JF7', 'SP1', 'SP2', 'SP3', 'SP4', 'SP5', 'DL1', 'DL2', 'DL3', 'DC1', 'DC2') },
    @{ N = 8;  Title = 'Home network'; Launch = ''
       Needs = 'Tailscale signed in, wol.json with a machine you can sleep, homeassistant.json with a token, weather.json.'
       Ids = @('TS1', 'TS2', 'TS3', 'TS4', 'WOL1', 'WOL2', 'WOL3', 'HA1', 'HA2', 'HA3', 'HA4', 'HA5', 'WE1', 'WE2', 'WE3') },
    @{ N = 9;  Title = 'Steam, games and apps'; Launch = ''
       Needs = 'Steam running; one installed game you own; an exclusive-fullscreen game; an emulator; a web app (Plex or YouTube).'
       Ids = @('ST1', 'ST2', 'ST3', 'ST4', 'ST5', 'ST6', 'ST7', 'ST8', 'IG1', 'IG2', 'IG3', 'IG4', 'IG5', 'IG6') },
    @{ N = 10; Title = 'Desktop Mode, display and accessories'; Launch = 'launch -Set XMB_PHONE_REMOTE_LAN=1 (PR1)'
       Needs = 'Mouse for the UAC prompt in DM4. A Bluetooth pad or headset to pair. A phone on the same network. A USB-CEC adapter if you have one.'
       Ids = @('DM1', 'DM2', 'DM3', 'DM4', 'AS3', 'DP1', 'DP2', 'CE1', 'BL1', 'WF1', 'PR1', 'DM5') },
    @{ N = 11; Title = 'Long session'; Launch = 'soak -Label LS1 in a second window (then -Label LS2)'
       Needs = 'Four idle hours and a 4-hour playback (a long film or a playlist).'
       Ids = @('LS1', 'LS2', 'LS3') },
    @{ N = 12; Title = 'Recovery and power (last)'; Launch = 'launch (RC1), launch -Windowed (PW5)'
       Needs = 'Nothing new. This sitting sleeps, restarts and finally turns off the PC.'
       Ids = @('RC1', 'RC2', 'PW5', 'PW2', 'PW3', 'BT2', 'BT1', 'PW4') }
)

# Log lines worth keeping per row: new lines of the XMB log written while the row was being done.
$LogPatterns = @{
    B1 = 'boot:|first frame|kiosk'; N1 = 'source=pad'; N2 = 'source=pad'; N3 = 'source=pad'; N4 = 'source=pad|options'
    N5 = 'source=pad action=(l1|r1)|selection'; N6 = 'source=pad|osk'; N7 = 'ps:|PsWatcher|foreground'; N8 = 'ps:|system menu'
    R1 = 'remote:|appcommand'; R2 = 'remote:|stop'; R3 = 'remote:|next|previous'; R4 = 'remote:|fast-forward|rewind|seek'
    R5 = 'remote:|selection'; R6 = 'remote:|back|home|system menu'; R7 = 'remote:|volume|mute'; R8 = 'remote:|swallowed|duplicate'
    R9 = 'remote:|declined|DefWindowProc'; PW1 = 'dry run|confirmed'; AU1 = 'audio:'; AU2 = 'audio:|would set'
    JF1 = 'jellyfin'; JF2 = 'jellyfin|resume'; JF3 = 'jellyfin|bitrate|quality'; JF4 = 'jellyfin|subtitle'; JF5 = 'subtitle|\.sup|pgs'
    JF6 = 'subtitle|burn'; JF7 = 'backdrop|jellyfin'; SP1 = 'spotify'; SP2 = 'spotify'; SP3 = 'spotify|remote:'; SP4 = 'spotify'; SP5 = 'spotify|premium'
    DL1 = 'dlna|ssdp'; DL2 = 'dlna'; DL3 = 'dlna|unicast'; DC1 = 'discord'; DC2 = 'discord'
    TS1 = 'tailscale'; TS2 = 'tailscale|pong'; TS3 = 'tailscale|exit'; TS4 = 'tailscale'; WOL1 = 'wol|wake'; WOL2 = 'wol|neighbo'; WOL3 = 'wol|magic|wake'
    HA1 = 'homeassistant|home assistant|dpapi'; HA2 = 'homeassistant|home assistant'; HA3 = 'homeassistant|home assistant|scene'
    HA4 = 'homeassistant|home assistant'; HA5 = 'homeassistant|home assistant'; WE1 = 'weather'; WE2 = 'weather'; WE3 = 'weather'
    ST2 = 'steam|title|launch'; ST3 = 'steam|title|finished|foreground'; ST4 = 'title|finished|foreground'; ST5 = 'title|finished|foreground'; ST7 = 'steam://'; ST8 = 'steam://'
    IG1 = 'ingame|ps:'; IG2 = 'ingame|source=pad'; IG3 = 'ingame: return|return confirmed'; IG4 = 'quit|ingame'; IG5 = 'text|osk|ingame'; IG6 = 'title|foreground|finished'
    DM1 = 'desktop:'; DM2 = 'desktop:'; DM3 = 'desktop-pad|desktop:'; DM4 = 'elevat|desktop'; AS3 = 'desktop:|instance:'
    DP1 = 'display'; DP2 = 'display mode'; CE1 = 'cec'; BL1 = 'bluetooth'; WF1 = 'wi-?fi|wlan'; PR1 = 'phone remote'
    US1 = 'profiles:'; US2 = 'profiles:|autoLogin'; SC1 = 'system check|self-check'; SC2 = 'system check|self-check'
    CD1 = 'delete|recycle'; CD2 = 'delete|recycle'; CD3 = 'copy'; LM1 = 'resume|video'; LM4 = 'subtitle'; LM5 = 'subtitle|pgs|\.sup'; LM6 = 'subtitle|vobsub|\.idx'; LM7 = 'playlist'
    PW2 = 'power|sleep'; PW3 = 'power|restart'; PW4 = 'power|turn off'; PW5 = 'power|turn off|quits'; FR1 = 'setup|first-run'
}

# ---------------------------------------------------------------------------------------------------------------
# Scrubbing: a PowerShell port of work/scrub-log.ts (audit/DIAGNOSE.md section 3). Fail closed: a line that looks
# like a credential is dropped whole.
# ---------------------------------------------------------------------------------------------------------------
$ScrubDrop = @(
    '(?i)Authorization\s*[:=]', '(?i)\bBearer\s+\S', '(?i)X-Emby-Token|X-MediaBrowser-Token|AccessToken',
    '(?i)\b(api[_-]?key|apikey|password|passwd|pwd|secret|refresh[_-]?token|access[_-]?token|client[_-]?secret|npsso|token)\b"?\s*[:=]',
    '(?i)[?&](api_key|token|apikey|code)=', '-----BEGIN [A-Z ]*PRIVATE KEY-----', '\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.'
)
$ScrubMask = @(
    @('[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s"''`]+', '%USERPROFILE%'),
    @('(?i)(autologon: on for a different user \()([^)]*)\)', '$1<user>)'),
    @('(?i)(autologon: on for )(\S+)', '$1<user>'),
    @('(?i)(tailscale: status \S+ self )(\S+)', '$1<host>'),
    @('\b7656119\d{10}\b', '<steam-id>'),
    @('(?i)(account(?:name)?[=:]\s*)[^\s,;"]+', '$1<account>'),
    @('\b([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b', '<mac>'),
    @('\b(?:10|127)\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', '<ip>'),
    @('\b192\.168\.\d{1,3}\.\d{1,3}\b', '<ip>'),
    @('\b172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}\b', '<ip>'),
    @('\b100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}\b', '<tailnet-ip>')
)
function Protect-Text([string[]]$Lines) {
    $kept = New-Object System.Collections.Generic.List[string]
    $inKey = $false
    foreach($line in $Lines) {
        if($null -eq $line) { continue }
        if($line -match '-----BEGIN [A-Z ]*PRIVATE KEY-----') { $inKey = $true; continue }
        if($inKey) { if($line -match '-----END [A-Z ]*PRIVATE KEY-----') { $inKey = $false }; continue }
        $drop = $false
        foreach($re in $ScrubDrop) { if($line -match $re) { $drop = $true; break } }
        if($drop) { $kept.Add('[line withheld by scrub]'); continue }
        $next = $line
        foreach($pair in $ScrubMask) { $next = [regex]::Replace($next, $pair[0], $pair[1]) }
        if($env:USERNAME -and $env:USERNAME.Length -ge 3) {
            $next = [regex]::Replace($next, '(?i)\b' + [regex]::Escape($env:USERNAME) + '\b', '<user>')
        }
        if($env:COMPUTERNAME -and $env:COMPUTERNAME.Length -ge 3) {
            $next = [regex]::Replace($next, '(?i)\b' + [regex]::Escape($env:COMPUTERNAME) + '\b', '<pc>')
        }
        $kept.Add($next)
    }
    return $kept.ToArray()
}

# ---------------------------------------------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------------------------------------------
function Write-Utf8([string]$Path, [string]$Text) {
    $dir = Split-Path -Parent $Path
    if($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    [System.IO.File]::WriteAllText($Path, $Text, $Utf8)
}
function Say([string]$Text, [string]$Color = 'Gray') { Write-Host $Text -ForegroundColor $Color }
function Mark([bool]$Ok, [string]$Text) { if($Ok) { "[ok] $Text" } else { "[!!] $Text" } }
function Get-Prop($Object, [string]$Name) {
    if($null -eq $Object) { return $null }
    $p = $Object.PSObject.Properties[$Name]
    if($p) { return $p.Value } else { return $null }
}

function Find-Checklist {
    if($Checklist) { return $Checklist }
    foreach($candidate in @((Join-Path $PSScriptRoot '..\audit\RELEASE-CHECKLIST.md'), (Join-Path $PSScriptRoot 'RELEASE-CHECKLIST.md'))) {
        if(Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'RELEASE-CHECKLIST.md not found. Run this script from the repository (native-xmb\packaging) or pass -Checklist <path>.'
}

# Section 2 rows: | ID | What to do | Correct looks like | Evidence | Pass/Fail |
function Read-ChecklistRows([string]$Path) {
    $rows = New-Object System.Collections.Generic.List[object]
    $section = ''
    $inChecklist = $false
    foreach($line in [System.IO.File]::ReadAllLines($Path, $Utf8)) {
        if($line -match '^## 2\.') { $inChecklist = $true; continue }
        if($line -match '^## [^2]') { $inChecklist = $false; continue }
        if(-not $inChecklist) { continue }
        if($line -match '^### (2\.\d+ .*)$') { $section = $Matches[1].Trim(); continue }
        if($line -notmatch '^\|\s*([A-Z]{1,3}\d+)\s*\|') { continue }
        $cells = [regex]::Split($line.Trim().Trim('|'), '(?<!\\)\|') | ForEach-Object { $_.Trim() }
        if($cells.Count -lt 4) { continue }
        $rows.Add([pscustomobject]@{ Id = $cells[0]; Section = $section; Do = $cells[1]; Expect = $cells[2]; Evidence = $cells[3] })
    }
    return $rows.ToArray()
}

function Get-SittingOf([string]$Id) {
    foreach($s in $Sittings) { if($s.Ids -contains $Id) { return $s } }
    return $null
}

# ---------------------------------------------------------------------------------------------------------------
# Context: package, data folder, log, evidence folder
# ---------------------------------------------------------------------------------------------------------------
$StateFile = if($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'xmb-htpc-run.json' } else { Join-Path ([System.IO.Path]::GetTempPath()) 'xmb-htpc-run.json' }

function Get-Context {
    $saved = $null
    if(Test-Path -LiteralPath $StateFile) { try { $saved = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json } catch { $saved = $null } }
    $pkg = $Package
    if(-not $pkg -and $saved) { $pkg = Get-Prop $saved 'package' }
    if(-not $pkg -and (Test-Path (Join-Path $PSScriptRoot 're-ps3-native.exe'))) { $pkg = $PSScriptRoot }
    $ev = $Evidence
    if(-not $ev -and $saved) { $ev = Get-Prop $saved 'evidence' }
    if(-not $ev) { $ev = Join-Path 'C:\xmb-evidence' ((Get-Date -Format 'yyyy-MM-dd') + '-htpc') }
    $zipPath = $Zip
    if(-not $zipPath -and $saved) { $zipPath = Get-Prop $saved 'zip' }

    $config = $null; $flash = $null; $data = $null; $exe = $null
    if($pkg) {
        $exe = Join-Path $pkg 're-ps3-native.exe'
        $cfgPath = Join-Path $pkg 'xmb-config.json'
        if(Test-Path -LiteralPath $cfgPath) { try { $config = Get-Content -LiteralPath $cfgPath -Raw | ConvertFrom-Json } catch { $config = $null } }
        $flash = Get-Prop $config 'devFlash'
        if(-not $flash) { $flash = Join-Path (Split-Path -Parent $pkg) 'rpcs3\bin\dev_flash' }
        $data = Get-Prop $config 'dataDir'
        if(-not $data) { $data = Join-Path $pkg 'data' }
    }
    foreach($d in @($ev, (Join-Path $ev 'photos'), (Join-Path $ev 'logs'), (Join-Path $ev 'raw'))) {
        if(-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
    }
    $state = @{ package = $pkg; evidence = $ev; zip = $zipPath }
    try { Write-Utf8 $StateFile ($state | ConvertTo-Json) } catch { }
    $log = $null
    if($data) { $log = Join-Path $data 'logs\xmb.log' }
    return [pscustomobject]@{
        Package = $pkg; Exe = $exe; Config = $config; Flash = $flash; Data = $data; Log = $log; Evidence = $ev; Zip = $zipPath
        Watchdog = $(if($pkg) { Join-Path $pkg 'watchdog.log' } else { $null })
        Results = (Join-Path $ev 'results.csv')
    }
}

# ---------------------------------------------------------------------------------------------------------------
# Results: results.csv (ID, Sitting, Result, Note, Auto, When)
# ---------------------------------------------------------------------------------------------------------------
function Read-Results([string]$Path) {
    $map = @{}
    if(Test-Path -LiteralPath $Path) {
        foreach($r in (Import-Csv -LiteralPath $Path)) { $map[$r.ID] = $r }
    }
    return $map
}
function Save-Results([string]$Path, $Map, $Rows) {
    $ordered = New-Object System.Collections.Generic.List[object]
    foreach($s in $Sittings) { foreach($id in $s.Ids) { if($Map.ContainsKey($id)) { $ordered.Add($Map[$id]) } } }
    foreach($k in $Map.Keys) { if(-not (Get-SittingOf $k)) { $ordered.Add($Map[$k]) } }
    $tmp = "$Path.tmp"
    $ordered | Select-Object ID, Sitting, Result, Note, Auto, When | Export-Csv -LiteralPath $tmp -NoTypeInformation -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}

# ---------------------------------------------------------------------------------------------------------------
# Log reading: lines written after a byte offset (the XMB keeps the file open; share read/write)
# ---------------------------------------------------------------------------------------------------------------
function Get-LogLength([string]$Path) {
    if(-not $Path -or -not (Test-Path -LiteralPath $Path)) { return 0 }
    return (Get-Item -LiteralPath $Path).Length
}
function Read-LogFrom([string]$Path, [long]$Offset) {
    if(-not $Path -or -not (Test-Path -LiteralPath $Path)) { return @() }
    $fs = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
    try {
        if($Offset -gt $fs.Length) { $Offset = 0 }   # rotated since the mark: read the new file from the start
        $null = $fs.Seek($Offset, [System.IO.SeekOrigin]::Begin)
        $reader = New-Object System.IO.StreamReader($fs, $Utf8)
        $text = $reader.ReadToEnd()
    } finally { $fs.Dispose() }
    return @($text -split "`r?`n" | Where-Object { $_ -ne '' })
}

# ---------------------------------------------------------------------------------------------------------------
# Running the app in its report modes (they exit by themselves and skip the single-instance check)
# ---------------------------------------------------------------------------------------------------------------
function Invoke-Report($Ctx, [string[]]$Arguments, [int]$TimeoutSeconds = 90) {
    if(-not $Ctx.Exe -or -not (Test-Path -LiteralPath $Ctx.Exe)) { return [pscustomobject]@{ Ok = $false; Log = @(); Why = 're-ps3-native.exe not found (pass -Package)' } }
    $tmpLog = Join-Path ([System.IO.Path]::GetTempPath()) ('xmb-report-' + [guid]::NewGuid().ToString('N') + '.log')
    $old = $env:XMB_LOG
    $env:XMB_LOG = $tmpLog
    try {
        $all = @()
        if($Ctx.Flash -and (Test-Path -LiteralPath $Ctx.Flash)) { $all += @('--dev-flash', ('"' + $Ctx.Flash + '"')) }
        if($Ctx.Data) { $all += @('--data-dir', ('"' + $Ctx.Data + '"')) }
        $all += $Arguments
        $p = Start-Process -FilePath $Ctx.Exe -ArgumentList $all -WorkingDirectory $Ctx.Package -PassThru -WindowStyle Hidden
        $null = $p.Handle   # keeps the handle so ExitCode is readable after the exit
        if(-not $p.WaitForExit($TimeoutSeconds * 1000)) { try { $p.Kill() } catch { }; return [pscustomobject]@{ Ok = $false; Log = @(); Why = "timed out after $TimeoutSeconds s" } }
        $lines = @()
        if(Test-Path -LiteralPath $tmpLog) { $lines = [System.IO.File]::ReadAllLines($tmpLog, $Utf8) }
        return [pscustomobject]@{ Ok = ($p.ExitCode -eq 0); Log = $lines; Why = "exit $($p.ExitCode)" }
    } finally {
        $env:XMB_LOG = $old
        if(Test-Path -LiteralPath $tmpLog) { Remove-Item -LiteralPath $tmpLog -Force -ErrorAction SilentlyContinue }
    }
}

function Get-AppVersion($Ctx) {
    $json = Join-Path ([System.IO.Path]::GetTempPath()) ('xmb-settings-' + [guid]::NewGuid().ToString('N') + '.json')
    try {
        $r = Invoke-Report $Ctx @('--settings-report', ('"' + $json + '"'), '--settings-page', 'system')
        if(Test-Path -LiteralPath $json) {
            $text = [System.IO.File]::ReadAllText($json, $Utf8)
            $m = [regex]::Match($text, '"build"\s*:\s*\{[^{}]*?"version"\s*:\s*"([^"]+)"')
            if($m.Success) { return $m.Groups[1].Value }
            $m = [regex]::Match($text, 'Application: re-ps3-native ([0-9][^"\\ ]*)')
            if($m.Success) { return $m.Groups[1].Value }
        }
        return "unknown ($($r.Why))"
    } finally { if(Test-Path -LiteralPath $json) { Remove-Item -LiteralPath $json -Force -ErrorAction SilentlyContinue } }
}

# ---------------------------------------------------------------------------------------------------------------
# Automatic checks: each returns lines of evidence (already safe to share). They never decide PASS or FAIL;
# the operator does.
# ---------------------------------------------------------------------------------------------------------------
function Find-Zip($Ctx) {
    $candidates = @()
    if($Ctx.Zip) { $candidates += $Ctx.Zip }
    if($env:USERPROFILE) { $candidates += (Join-Path $env:USERPROFILE 'Downloads\re-ps3.zip') }
    foreach($c in $candidates) { if($c -and (Test-Path -LiteralPath $c)) { return $c } }
    return $null
}

function Get-SteamPlaytime([string]$AppId) {
    $steam = $null
    try { $steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath } catch { }
    if(-not $steam) { return @('Steam path not found in the registry (HKCU\Software\Valve\Steam).') }
    $out = @()
    $files = Get-ChildItem -Path (Join-Path $steam 'userdata') -Filter 'localconfig.vdf' -Recurse -ErrorAction SilentlyContinue
    $i = 0
    foreach($f in $files) {
        $i++
        $text = [System.IO.File]::ReadAllText($f.FullName)
        $m = [regex]::Match($text, '"' + [regex]::Escape($AppId) + '"\s*\{')
        while($m.Success) {
            $block = $text.Substring($m.Index, [Math]::Min(4000, $text.Length - $m.Index))
            $end = $block.IndexOf("`n`t`t`t`t}")
            $pt = [regex]::Match($block, '"Playtime"\s*"(\d+)"')
            if($pt.Success) {
                $min = [int]$pt.Groups[1].Value
                $lp = [regex]::Match($block, '"LastPlayed"\s*"(\d+)"')
                $last = ''
                if($lp.Success) { $last = ' - last played ' + ([DateTimeOffset]::FromUnixTimeSeconds([long]$lp.Groups[1].Value).LocalDateTime.ToString('yyyy-MM-dd HH:mm')) }
                $out += ('account #{0}: app {1} Playtime {2} min = {3} h {4} min{5}' -f $i, $AppId, $min, [Math]::Floor($min / 60), ($min % 60), $last)
                break
            }
            $m = $m.NextMatch()
        }
    }
    if(-not $out) { $out = @("app $AppId has no Playtime entry in any localconfig.vdf ($($files.Count) file(s)).") }
    return $out
}

function Get-TcpOnline([string]$HostName, [int[]]$Ports) {
    foreach($port in $Ports) {
        $c = New-Object System.Net.Sockets.TcpClient
        try {
            $task = $c.ConnectAsync($HostName, $port)
            if($task.Wait(1500) -and $c.Connected) { return $true }
        } catch { } finally { $c.Dispose() }
    }
    return $false
}

function Get-LogSummary([string[]]$Lines) {
    $gl = @($Lines | Where-Object { $_ -match 'QOpenGLTexturePrivate::destroy|Texture has not been destroyed' }).Count
    $crash = @($Lines | Where-Object { $_ -match '(?i)unhandled exception|qFatal|crash|minidump|access violation' }).Count
    $warn = @($Lines | Where-Object { $_ -match '(?i)\bwarn|\berror\b|\bfailed\b' -and $_ -notmatch 'QOpenGLTexturePrivate::destroy|Texture has not been destroyed' }).Count
    return @("log lines: $($Lines.Count)", "crash-looking lines: $crash", "known OpenGL exit warnings (G12): $gl", "other warning/error/failed lines: $warn")
}

function Invoke-AutoCheck([string]$Id, $Ctx) {
    $out = @()
    switch($Id) {
        'V1' {
            $z = Find-Zip $Ctx
            if(-not $z) { $out += 'No re-ps3.zip found: pass -Zip <path to the downloaded re-ps3.zip>.'; break }
            $hash = (Get-FileHash -LiteralPath $z -Algorithm SHA256).Hash.ToLowerInvariant()
            $out += "re-ps3.zip SHA-256: $hash"
            $shaFile = "$z.sha256"
            if(Test-Path -LiteralPath $shaFile) {
                $m = [regex]::Match([System.IO.File]::ReadAllText($shaFile), '[0-9a-fA-F]{64}')
                if($m.Success) { $out += (Mark ($m.Value.ToLowerInvariant() -eq $hash) "matches re-ps3.zip.sha256 ($($m.Value.ToLowerInvariant()))") }
                else { $out += '[!!] re-ps3.zip.sha256 holds no 64-digit hash.' }
            } else { $out += "[!!] re-ps3.zip.sha256 not found next to the zip ($shaFile)." }
        }
        'V2' { $v = Get-AppVersion $Ctx; $out += (Mark ($v -eq $Version) "settings report build.version = $v (expected $Version)") }
        'UP2' {
            if($Ctx.Package) {
                $out += (Mark (Test-Path -LiteralPath ($Ctx.Package + '.old')) "previous install kept at <package>.old")
                $out += (Mark (Test-Path -LiteralPath (Join-Path $Ctx.Package 'xmb-config.json')) 'xmb-config.json present in the new install')
            }
            if($Ctx.Data) { $out += (Mark (Test-Path -LiteralPath (Join-Path $Ctx.Data 'preferences.json')) 'preferences.json present in the data folder') }
            $out += "version now: $(Get-AppVersion $Ctx)"
            if($Ctx.Watchdog -and (Test-Path -LiteralPath $Ctx.Watchdog)) {
                $out += 'watchdog.log (update lines):'
                $out += @(Get-Content -LiteralPath $Ctx.Watchdog -Tail 200 | Where-Object { $_ -match '(?i)update|applied|42|restart' } | Select-Object -Last 12)
            }
        }
        'UP3' {
            $r = Invoke-Report $Ctx @('--check-update')
            $line = @($r.Log | Where-Object { $_ -match 'current=' }) | Select-Object -Last 1
            if($line) { $out += $line; $out += (Mark ($line -match 'newer=no') 'no newer release offered') } else { $out += "no check-update line ($($r.Why))" }
        }
        { $_ -in @('B2', 'LS3') } {
            if($Ctx.Log -and (Test-Path -LiteralPath $Ctx.Log)) { $out += Get-LogSummary @(Read-LogFrom $Ctx.Log 0) } else { $out += "[!!] no XMB log at $($Ctx.Log)" }
            if($Ctx.Data) {
                $crashDir = Join-Path $Ctx.Data 'crashes'
                $n = 0; if(Test-Path -LiteralPath $crashDir) { $n = @(Get-ChildItem -LiteralPath $crashDir -File).Count }
                $out += "files in <data>\crashes: $n"
            }
        }
        { $_ -in @('B3', 'RC2') } {
            if($Ctx.Data -and (Test-Path -LiteralPath $Ctx.Data)) {
                $out += '<data> listing: ' + ((Get-ChildItem -LiteralPath $Ctx.Data | ForEach-Object { $_.Name }) -join ', ')
                $pl = Join-Path $Ctx.Data 'playlists'
                if(Test-Path -LiteralPath $pl) { $out += "playlists: $(@(Get-ChildItem -LiteralPath $pl -File).Count) file(s)" }
            }
        }
        'HA1' {
            if(-not $Ctx.Data) { break }
            $f = Join-Path $Ctx.Data 'homeassistant.json'
            if(Test-Path -LiteralPath $f) {
                $j = $null; try { $j = Get-Content -LiteralPath $f -Raw | ConvertFrom-Json } catch { }
                $out += (Mark (-not (Get-Prop $j 'token')) 'homeassistant.json no longer holds a token key')
                $out += 'other keys: ' + ((@($j.PSObject.Properties | Where-Object { $_.Name -ne 'token' } | ForEach-Object { $_.Name })) -join ', ')
            } else { $out += '[!!] homeassistant.json not found in <data>' }
            $out += 'files in <data> that look sealed: ' + ((@(Get-ChildItem -LiteralPath $Ctx.Data -File | Where-Object { $_.Name -match '(?i)home|\.bin$|seal' } | ForEach-Object { $_.Name })) -join ', ')
        }
        'WOL1' {
            if(-not $Ctx.Data) { break }
            $f = Join-Path $Ctx.Data 'wol.json'
            if(-not (Test-Path -LiteralPath $f)) { $out += '[!!] wol.json not found in <data>'; break }
            $j = Get-Content -LiteralPath $f -Raw | ConvertFrom-Json
            foreach($d in @(Get-Prop $j 'devices')) {
                if(-not $d) { continue }
                $ports = @(3389, 22, 445, 80); $pp = Get-Prop $d 'probePort'; if($pp) { $ports = @([int]$pp) }
                $h = Get-Prop $d 'host'
                $state = 'no host'
                if($h) { if(Get-TcpOnline $h $ports) { $state = 'Online' } else { $state = 'Offline' } }
                $out += ('{0}: {1} (TCP probe from the HTPC; compare with the row)' -f (Get-Prop $d 'name'), $state)
            }
        }
        { $_ -in @('ST1', 'ST6') } {
            $appId = Read-Host "  Steam app id of the title for $Id (Enter to skip; it is the number in the store page address)"
            if($appId -match '^\d+$') {
                $lines = Get-SteamPlaytime $appId
                $out += $lines
                Write-Utf8 (Join-Path $Ctx.Evidence "raw\steam-$Id.txt") (($lines -join "`r`n") + "`r`n")
            }
        }
        'TS1' {
            $ts = Get-Command tailscale.exe -ErrorAction SilentlyContinue
            if(-not $ts) { foreach($c in @("$env:ProgramFiles\Tailscale\tailscale.exe")) { if(Test-Path $c) { $ts = $c } } } else { $ts = $ts.Source }
            if($ts) {
                try {
                    $j = (& $ts status --json) -join "`n" | ConvertFrom-Json
                    $peers = 0; $pp = Get-Prop $j 'Peer'; if($pp) { $peers = @($pp.PSObject.Properties).Count }
                    $out += "tailscale CLI: BackendState $(Get-Prop $j 'BackendState'), self online $(Get-Prop (Get-Prop $j 'Self') 'Online'), $peers peer(s)"
                } catch { $out += "tailscale status failed: $($_.Exception.Message)" }
            } else { $out += '[!!] tailscale.exe not found' }
        }
        'AS1' {
            try {
                $t = Get-ScheduledTask -TaskName 're-ps3-native-kiosk' -ErrorAction Stop
                $args0 = (@($t.Actions) | ForEach-Object { "$($_.Execute) $($_.Arguments)" }) -join ' '
                $out += "[ok] task re-ps3-native-kiosk: $($t.State)"
                $out += (Mark ($args0 -match 'watchdog\.ps1') 'task runs watchdog.ps1')
                if($Ctx.Package) { $out += (Mark ($args0 -like ('*' + $Ctx.Package + '*')) 'task points at this package folder') }
            } catch { $out += '[!!] scheduled task re-ps3-native-kiosk not found' }
        }
        'AS2' {
            try {
                $w = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon' -ErrorAction Stop
                $on = ("$(Get-Prop $w 'AutoAdminLogon')" -eq '1')
                $me = ("$(Get-Prop $w 'DefaultUserName')" -ieq $env:USERNAME)
                $out += (Mark $on 'AutoAdminLogon = 1')
                $out += (Mark $me 'DefaultUserName is this account (name not recorded)')
            } catch { $out += 'Winlogon key not readable' }
        }
        { $_ -in @('BT1', 'BT2', 'PW3') } {
            $boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
            $out += "Windows started: $($boot.ToString('yyyy-MM-dd HH:mm:ss'))"
            $ex = Get-Process explorer -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
            $xmb = Get-Process re-ps3-native -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
            if($ex) { $out += ('sign-in (explorer started): +{0:N1} s after boot' -f ($ex.StartTime - $boot).TotalSeconds) }
            if($xmb) { $out += ('XMB process started: +{0:N1} s after boot' -f ($xmb.StartTime - $boot).TotalSeconds) } else { $out += '[!!] re-ps3-native.exe is not running' }
            if($Ctx.Log) { $ff = @(Read-LogFrom $Ctx.Log 0 | Where-Object { $_ -match 'boot: first frame after' }) | Select-Object -Last 1; if($ff) { $out += $ff } }
        }
        'PW2' {
            try {
                $ev = Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-Power-Troubleshooter'; Id = 1 } -MaxEvents 1 -ErrorAction Stop
                $out += "last wake from sleep: $($ev.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss'))"
                $m = [regex]::Match($ev.Message, '(?s)Sleep Time:\s*(\S+ \S+).*?Wake Time:\s*(\S+ \S+)')
                if($m.Success) { $out += "sleep $($m.Groups[1].Value) -> wake $($m.Groups[2].Value) (UTC)" }
            } catch { $out += 'no Power-Troubleshooter wake event found' }
        }
        { $_ -in @('RC1', 'DM5') } {
            if($Ctx.Watchdog -and (Test-Path -LiteralPath $Ctx.Watchdog)) { $out += 'watchdog.log (last 15 lines):'; $out += @(Get-Content -LiteralPath $Ctx.Watchdog -Tail 15) }
            else { $out += '[!!] watchdog.log not found next to the exe' }
        }
        { $_ -in @('LS1', 'LS2') } {
            $csv = Join-Path $Ctx.Evidence "soak-$Id.csv"
            if(Test-Path -LiteralPath $csv) { $out += Get-SoakSummary $csv } else { $out += "No soak-$Id.csv yet: run 'htpc-run.ps1 soak -Label $Id' in a second PowerShell window first." }
        }
    }
    return @(Protect-Text $out)
}

# ---------------------------------------------------------------------------------------------------------------
# Soak sampling (LS1, LS2)
# ---------------------------------------------------------------------------------------------------------------
function Get-SoakSummary([string]$Csv) {
    $rows = @(Import-Csv -LiteralPath $Csv)
    if($rows.Count -eq 0) { return @('soak csv is empty') }
    $ws = $rows | ForEach-Object { [double]$_.WorkingSetMB } | Measure-Object -Minimum -Maximum
    $pb = $rows | ForEach-Object { [double]$_.PrivateMB } | Measure-Object -Minimum -Maximum
    $hd = $rows | ForEach-Object { [int]$_.Handles } | Measure-Object -Minimum -Maximum
    $cpu = @($rows | ForEach-Object { [double]$_.CpuPercent } | Sort-Object)
    $pids = @($rows | ForEach-Object { $_.Pid } | Select-Object -Unique)
    $first = [datetime]$rows[0].Time; $last = [datetime]$rows[-1].Time
    return @(
        ('{0} samples over {1:N1} h' -f $rows.Count, ($last - $first).TotalHours),
        ('working set {0:N0}-{1:N0} MB (first {2}, last {3})' -f $ws.Minimum, $ws.Maximum, $rows[0].WorkingSetMB, $rows[-1].WorkingSetMB),
        ('private bytes {0:N0}-{1:N0} MB (first {2}, last {3})' -f $pb.Minimum, $pb.Maximum, $rows[0].PrivateMB, $rows[-1].PrivateMB),
        ('handles {0}-{1}; CPU median {2:N1} % of one core' -f $hd.Minimum, $hd.Maximum, $cpu[[int][Math]::Floor($cpu.Count / 2)]),
        ('process ids seen: {0} (more than one means the XMB restarted)' -f $pids.Count)
    )
}

function Start-Soak($Ctx) {
    $csv = Join-Path $Ctx.Evidence "soak-$Label.csv"
    $end = (Get-Date).AddHours($Hours)
    $cores = [Environment]::ProcessorCount
    Say "Sampling re-ps3-native.exe every $IntervalSeconds s until $($end.ToString('HH:mm')) into $csv (Ctrl+C stops; the file keeps what was written)." 'Cyan'
    if(-not (Test-Path -LiteralPath $csv)) { Write-Utf8 $csv "Time,Pid,WorkingSetMB,PrivateMB,Handles,Threads,CpuPercent`r`n" }
    $lastCpu = $null; $lastPid = $null; $lastTime = $null
    while((Get-Date) -lt $end) {
        $p = Get-Process re-ps3-native -ErrorAction SilentlyContinue | Sort-Object StartTime | Select-Object -First 1
        $now = Get-Date
        if($p) {
            $cpu = ''
            if($lastPid -eq $p.Id -and $lastCpu) { $cpu = ((($p.TotalProcessorTime - $lastCpu).TotalSeconds / ($now - $lastTime).TotalSeconds) * 100).ToString('F1', [System.Globalization.CultureInfo]::InvariantCulture) }
            $lastCpu = $p.TotalProcessorTime; $lastPid = $p.Id; $lastTime = $now
            if($cpu -ne '') {
                $inv = [System.Globalization.CultureInfo]::InvariantCulture
                $line = [string]::Format($inv, '{0},{1},{2:F1},{3:F1},{4},{5},{6}', $now.ToString('s'), $p.Id, ($p.WorkingSet64 / 1MB), ($p.PrivateMemorySize64 / 1MB), $p.HandleCount, $p.Threads.Count, $cpu)
                [System.IO.File]::AppendAllText($csv, $line + "`r`n", $Utf8)
                Say $line
            }
        } else { Say "$($now.ToString('HH:mm:ss')) re-ps3-native.exe is not running" 'Yellow'; $lastPid = $null }
        Start-Sleep -Seconds $IntervalSeconds
    }
    Get-SoakSummary $csv | ForEach-Object { Say $_ 'Green' }
    Say "($cores logical cores; 100 % = one full core.)"
}

# ---------------------------------------------------------------------------------------------------------------
# Launch: quit the running XMB, then start it under the watchdog (default) or directly
# ---------------------------------------------------------------------------------------------------------------
function Start-Xmb($Ctx) {
    if(-not $Ctx.Exe -or -not (Test-Path -LiteralPath $Ctx.Exe)) { throw 're-ps3-native.exe not found: pass -Package <installed package folder>.' }
    if(Get-Process re-ps3-native -ErrorAction SilentlyContinue) {
        Say 'Asking the running XMB to quit (--quit, exit code 0, so its watchdog stops too)...'
        Start-Process -FilePath $Ctx.Exe -ArgumentList '--quit' -WorkingDirectory $Ctx.Package -Wait -WindowStyle Hidden
        for($i = 0; $i -lt 30 -and (Get-Process re-ps3-native -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
        if(Get-Process re-ps3-native -ErrorAction SilentlyContinue) { throw 'The XMB is still running after --quit; close it, then launch again.' }
    }
    $saved = @{}
    foreach($pair in @($Set | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) {
        $k, $v = $pair -split '=', 2
        if($null -eq $v) { $v = '1' }
        $saved[$k] = [Environment]::GetEnvironmentVariable($k)
        [Environment]::SetEnvironmentVariable($k, $v)
        Say "  $k=$v"
    }
    $saved['XMB_LOG'] = $env:XMB_LOG
    if($Ctx.Log) { New-Item -ItemType Directory -Path (Split-Path -Parent $Ctx.Log) -Force | Out-Null; $env:XMB_LOG = $Ctx.Log }
    try {
        $direct = $NoIntro -or $FirstRun -or $Windowed -or $NoWatchdog
        if($direct) {
            $a = @('--dev-flash', ('"' + $Ctx.Flash + '"'), '--data-dir', ('"' + $Ctx.Data + '"'))
            if(-not $Windowed) { $a += @('--kiosk', '--display', $Display) } else { $a += '--start-windowed' }
            if($NoIntro) { $a += '--no-intro' }
            if($FirstRun) { $a += '--first-run' }
            $vsh = Get-Prop $Ctx.Config 'vshModules'
            if($vsh -and (Test-Path -LiteralPath (Join-Path $vsh 'custom_render_plugin.prx'))) { $a += @('--vsh-modules', ('"' + $vsh + '"')) }
            Start-Process -FilePath $Ctx.Exe -ArgumentList $a -WorkingDirectory $Ctx.Package | Out-Null
            Say "Started directly (no watchdog): $($a -join ' ')" 'Green'
        } else {
            $wd = Join-Path $Ctx.Package 'watchdog.ps1'
            if(-not (Test-Path -LiteralPath $wd)) { throw "watchdog.ps1 not found in $($Ctx.Package)." }
            Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $wd + '"'), '-Display', $Display) -WorkingDirectory $Ctx.Package -WindowStyle Minimized | Out-Null
            Say "Started under the watchdog on display $Display. App log: $($Ctx.Log)" 'Green'
        }
    } finally {
        foreach($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
}

# ---------------------------------------------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------------------------------------------
function Invoke-Preflight($Ctx) {
    $o = New-Object System.Collections.Generic.List[string]
    $os = Get-CimInstance Win32_OperatingSystem
    $o.Add("Windows: $($os.Caption) $($os.Version); PowerShell $($PSVersionTable.PSVersion)")
    $o.Add("Evidence folder: $($Ctx.Evidence)")
    if(-not $Ctx.Package) { $o.Add('[!!] Package folder unknown: pass -Package <folder holding re-ps3-native.exe>.') }
    else {
        $o.Add((Mark (Test-Path -LiteralPath $Ctx.Exe) "re-ps3-native.exe in $($Ctx.Package)"))
        foreach($f in 'watchdog.ps1', 'run-kiosk.ps1', 'apply-update.ps1', 'install-autostart.ps1') { $o.Add((Mark (Test-Path -LiteralPath (Join-Path $Ctx.Package $f)) $f)) }
        $o.Add((Mark ($null -ne $Ctx.Config) 'xmb-config.json read'))
        $o.Add((Mark (Test-Path -LiteralPath (Join-Path "$($Ctx.Flash)" 'vsh\resource\explore_plugin_full.rco')) "firmware at devFlash"))
        if(Test-Path -LiteralPath $Ctx.Exe) { $o.Add("Version (settings report): $(Get-AppVersion $Ctx) - the run tests $Version") }
    }
    if($Ctx.Data) {
        $ok = $true
        try { New-Item -ItemType Directory -Path $Ctx.Data -Force | Out-Null; $probe = Join-Path $Ctx.Data '.htpc-run-probe'; Write-Utf8 $probe 'x'; Remove-Item -LiteralPath $probe -Force } catch { $ok = $false }
        $o.Add((Mark $ok 'data folder writable'))
        $drive = (Split-Path -Qualifier $Ctx.Data) -replace ':', ''
        try { $free = (Get-PSDrive -Name $drive).Free / 1GB; $o.Add((Mark ($free -gt 5) ('{0:N1} GB free on {1}:' -f $free, $drive))) } catch { }
        foreach($f in 'spotify.json', 'wol.json', 'homeassistant.json', 'weather.json', 'dlna.json', 'discord.json', 'jellyfin\session.bin', 'apps.json') {
            $p = Join-Path $Ctx.Data $f
            if(Test-Path -LiteralPath $p) { $o.Add("[ok] <data>\$f present") } else { $o.Add("[--] <data>\$f absent") }
        }
    }
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $i = 0
        foreach($s in [System.Windows.Forms.Screen]::AllScreens) {
            $tag = ''; if($i -eq $Display) { $tag = '  <- --display ' + $Display }
            $o.Add(('display {0}: {1}x{2}{3}{4}' -f $i, $s.Bounds.Width, $s.Bounds.Height, $(if($s.Primary) { ' (primary)' } else { '' }), $tag)); $i++
        }
        if($Display -ge $i) { $o.Add("[!!] --display $Display does not exist: only $i screen(s). Use -Display <n>.") }
    } catch { $o.Add('screens: not readable') }
    try {
        if(-not ('XInputProbe' -as [type])) {
            Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class XInputProbe{[DllImport("xinput1_4.dll")]public static extern int XInputGetState(int i,byte[] s);public static int Count(){int n=0;for(int i=0;i<4;i++){if(XInputGetState(i,new byte[16])==0)n++;}return n;}}'
        }
        $n = [XInputProbe]::Count(); $o.Add((Mark ($n -gt 0) "XInput pads connected: $n"))
    } catch { $o.Add('XInput: not readable') }
    if($Ctx.Exe -and (Test-Path -LiteralPath $Ctx.Exe)) {
        $pj = Join-Path ([System.IO.Path]::GetTempPath()) 'xmb-pad-report.json'
        $null = Invoke-Report $Ctx @('--pad-report', ('"' + $pj + '"')) 30
        if(Test-Path -LiteralPath $pj) { $o.Add('PlayStation pad report: ' + (([System.IO.File]::ReadAllText($pj) -replace '\s+', ' ').Trim())); Remove-Item -LiteralPath $pj -Force }
    }
    $steam = $null; try { $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath } catch { }
    $o.Add((Mark ([bool]$steam) 'Steam installed'))
    $o.Add((Mark ([bool](Get-Process steam -ErrorAction SilentlyContinue)) 'Steam running'))
    $ts = Get-Command tailscale.exe -ErrorAction SilentlyContinue
    if(-not $ts -and (Test-Path "$env:ProgramFiles\Tailscale\tailscale.exe")) { $ts = "$env:ProgramFiles\Tailscale\tailscale.exe" }
    $o.Add((Mark ([bool]$ts) 'tailscale.exe found'))
    $spot = Test-Path "$env:APPDATA\Spotify\Spotify.exe"
    if(-not $spot) { try { $spot = [bool](Get-AppxPackage -Name '*Spotify*' -ErrorAction Stop) } catch { } }
    $o.Add((Mark $spot 'Spotify app installed'))
    $edge = (Test-Path "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") -or (Test-Path "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe")
    $o.Add((Mark $edge 'Edge installed (web apps)'))
    $o.Add($(if(Get-Command cec-client.exe -ErrorAction SilentlyContinue) { '[ok] cec-client found (CE1 can run)' } else { '[--] cec-client not found (CE1 is NOT RUN unless you have a Pulse-Eight adapter)' }))
    try { $null = Get-ScheduledTask -TaskName 're-ps3-native-kiosk' -ErrorAction Stop; $o.Add('[ok] autostart task registered') } catch { $o.Add('[--] autostart task not registered yet (setup step 7)') }
    try { $w = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon' -ErrorAction Stop; if("$(Get-Prop $w 'AutoAdminLogon')" -eq '1') { $o.Add('[ok] Windows automatic sign-in on') } else { $o.Add('[--] Windows automatic sign-in off (needed for BT1)') } } catch { }
    try { $null = Invoke-WebRequest -Uri 'https://api.github.com/repos/ajgreenboy/XMB-Rewritten/releases/latest' -UseBasicParsing -TimeoutSec 15; $o.Add('[ok] GitHub releases reachable (updater)') } catch { $o.Add("[!!] GitHub releases not reachable: $($_.Exception.Message)") }
    $z = Find-Zip $Ctx
    $o.Add($(if($z) { "[ok] re-ps3.zip found for V1: $z" } else { '[--] re-ps3.zip not found; pass -Zip for V1' }))
    $safe = @(Protect-Text $o.ToArray())
    Write-Utf8 (Join-Path $Ctx.Evidence 'preflight.txt') (($safe -join "`r`n") + "`r`n")
    foreach($l in $safe) { $c = 'Gray'; if($l -like '`[ok`]*') { $c = 'Green' } elseif($l -like '`[!!`]*') { $c = 'Red' } elseif($l -like '`[--`]*') { $c = 'Yellow' }; Say $l $c }
    Say "Saved to $(Join-Path $Ctx.Evidence 'preflight.txt')" 'Cyan'
}

# ---------------------------------------------------------------------------------------------------------------
# Collect logs
# ---------------------------------------------------------------------------------------------------------------
function Invoke-Collect($Ctx) {
    $stamp = Get-Date -Format 'HHmmss'
    $sources = @()
    if($Ctx.Log) { $sources += @(Get-ChildItem -Path ($Ctx.Log + '*') -File -ErrorAction SilentlyContinue) }
    if($Ctx.Watchdog) { $sources += @(Get-ChildItem -Path ($Ctx.Watchdog + '*') -File -ErrorAction SilentlyContinue) }
    if($Ctx.Data -and (Test-Path -LiteralPath (Join-Path $Ctx.Data 'crashes'))) { $sources += @(Get-ChildItem -LiteralPath (Join-Path $Ctx.Data 'crashes') -File) }
    foreach($f in $sources) {
        $rawDest = Join-Path $Ctx.Evidence ("raw\$stamp-" + $f.Name)
        Copy-Item -LiteralPath $f.FullName -Destination $rawDest -Force
        if($f.Extension -in @('.dmp', '.mdmp')) { Say "  $($f.Name): raw\ only (binary)"; continue }
        $safe = @(Protect-Text ([System.IO.File]::ReadAllLines($f.FullName, $Utf8)))
        Write-Utf8 (Join-Path $Ctx.Evidence ("logs\$stamp-" + $f.Name + '.scrubbed.txt')) (($safe -join "`r`n") + "`r`n")
        Say "  $($f.Name): logs\$stamp-$($f.Name).scrubbed.txt"
    }
    if(-not $sources) { Say 'Nothing to collect: no XMB log, watchdog log or crash files found.' 'Yellow' }
    else { Say 'Read the scrubbed copies before sharing them: media paths and device names are not masked.' 'Cyan' }
}

# ---------------------------------------------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------------------------------------------
function Format-Cell([string]$Text) { return (($Text -replace '\|', '\|') -replace "`r?`n", ' / ').Trim() }

function New-Report($Ctx, $Rows, $Results) {
    $counts = @{ 'PASS' = 0; 'FAIL' = 0; 'PARTIAL' = 0; 'NOT RUN' = 0; 'open' = 0 }
    $photos = @()
    $pdir = Join-Path $Ctx.Evidence 'photos'
    if(Test-Path -LiteralPath $pdir) { $photos = @(Get-ChildItem -LiteralPath $pdir -File) }
    $sb = New-Object System.Text.StringBuilder
    $table = New-Object System.Text.StringBuilder
    foreach($s in $Sittings) {
        foreach($id in $s.Ids) {
            $row = $Rows | Where-Object { $_.Id -eq $id } | Select-Object -First 1
            $r = $null; if($Results.ContainsKey($id)) { $r = $Results[$id] }
            $res = 'open'; $note = ''; $auto = ''
            if($r) { $res = $r.Result; $note = $r.Note; $auto = $r.Auto }
            if($counts.ContainsKey($res)) { $counts[$res]++ } else { $counts['open']++ }
            $files = @($photos | Where-Object { $_.BaseName -match ('^' + [regex]::Escape($id) + '($|[^0-9])') } | ForEach-Object { 'photos/' + $_.Name })
            $parts = @()
            if($note) { $parts += $note }
            if($auto) { $parts += ('auto: ' + $auto) }
            if($files) { $parts += ('files: ' + ($files -join ', ')) }
            $title = ''; if($row) { $title = $row.Do; if($title.Length -gt 70) { $title = $title.Substring(0, 67) + '...' } }
            $null = $table.AppendLine(('| {0} | {1} | {2} | {3} |' -f $id, (Format-Cell $title), $res, (Format-Cell ($parts -join '; '))))
        }
    }
    $os = ''; try { $c = Get-CimInstance Win32_OperatingSystem; $os = "$($c.Caption) ($($c.Version))" } catch { $os = 'unknown' }
    $gpu = ''; try { $gpu = (@(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name }) -join ', ') } catch { }
    $total = 0; foreach($s in $Sittings) { $total += $s.Ids.Count }
    $null = $sb.AppendLine("# Release evidence run, $(Get-Date -Format 'yyyy-MM-dd') (HTPC)")
    $null = $sb.AppendLine()
    $null = $sb.AppendLine("The HTPC pass over ``audit/RELEASE-CHECKLIST.md``, recorded with ``packaging/htpc-run.ps1``. Release under test: v$Version.")
    $null = $sb.AppendLine()
    $null = $sb.AppendLine("- Machine: $os; GPU: $gpu")
    $null = $sb.AppendLine("- App version read from the install: $(if($Ctx.Exe -and (Test-Path -LiteralPath $Ctx.Exe)) { Get-AppVersion $Ctx } else { 'not read' })")
    $null = $sb.AppendLine('- Evidence: photos and scrubbed logs in the evidence folder (`photos/`, `logs/`); `raw/` stays on the HTPC.')
    $null = $sb.AppendLine()
    $null = $sb.AppendLine('## Result counts')
    $null = $sb.AppendLine()
    $null = $sb.AppendLine(("{0} checklist rows: **{1} PASS, {2} FAIL, {3} PARTIAL, {4} NOT RUN**, {5} not recorded yet." -f $total, $counts['PASS'], $counts['FAIL'], $counts['PARTIAL'], $counts['NOT RUN'], $counts['open']))
    $null = $sb.AppendLine()
    $null = $sb.AppendLine('## Table')
    $null = $sb.AppendLine()
    $null = $sb.AppendLine('<!-- table:start -->')
    $null = $sb.AppendLine('| ID | Row | Result | Evidence / reason |')
    $null = $sb.AppendLine('|---|---|---|---|')
    $null = $sb.Append($table.ToString())
    $null = $sb.AppendLine('<!-- table:end -->')
    $null = $sb.AppendLine()
    $null = $sb.AppendLine('## FAIL and PARTIAL: causes')
    $null = $sb.AppendLine()
    $any = $false
    foreach($k in $Results.Keys) { if($Results[$k].Result -in @('FAIL', 'PARTIAL')) { $any = $true; $null = $sb.AppendLine("- **$k**: $(Format-Cell $Results[$k].Note)") } }
    if(-not $any) { $null = $sb.AppendLine('None recorded.') }
    $text = ((Protect-Text ($sb.ToString() -split "`r?`n")) -join "`r`n")
    $path = Join-Path $Ctx.Evidence ("RELEASE-RUN-$(Get-Date -Format 'yyyy-MM-dd')-htpc.md")
    Write-Utf8 $path $text
    return $path
}

# ---------------------------------------------------------------------------------------------------------------
# Guided run
# ---------------------------------------------------------------------------------------------------------------
function Show-Status($Rows, $Results) {
    foreach($s in $Sittings) {
        $done = @($s.Ids | Where-Object { $Results.ContainsKey($_) }).Count
        $next = @($s.Ids | Where-Object { -not $Results.ContainsKey($_) }) | Select-Object -First 1
        $c = 'Gray'; if($done -eq $s.Ids.Count) { $c = 'Green' }
        Say ('{0,2}. {1,-42} {2,2}/{3,-2} {4}' -f $s.N, $s.Title, $done, $s.Ids.Count, $(if($next) { "next: $next" } else { 'done' })) $c
    }
}

function Invoke-Run($Ctx, $Rows, $Results) {
    $byId = @{}; foreach($r in $Rows) { $byId[$r.Id] = $r }
    foreach($s in $Sittings) {
        if($Sitting -gt 0 -and $s.N -ne $Sitting) { continue }
        $ids = @($s.Ids | Where-Object { (-not $Only -or $Only -contains $_) -and ($Redo -or -not $Results.ContainsKey($_)) })
        if($ids.Count -eq 0) { continue }
        Say ''
        Say ('=' * 78) 'Cyan'
        Say ("Sitting $($s.N): $($s.Title)") 'Cyan'
        Say ("Have ready: $($s.Needs)") 'Cyan'
        if($s.Launch) { Say ("Start the XMB with: htpc-run.ps1 $($s.Launch)") 'Cyan' }
        Say ('=' * 78) 'Cyan'
        foreach($id in $ids) {
            $row = $byId[$id]
            if(-not $row) { Say "$id is not in the checklist; skipped." 'Yellow'; continue }
            Say ''
            Say "[$id]  ($($row.Section))" 'White'
            Say "  Do:       $($row.Do)"
            Say "  Expect:   $($row.Expect)" 'Green'
            Say "  Evidence: $($row.Evidence)  -> save photos as photos\$id*.jpg" 'DarkGray'
            $mark = Get-LogLength $Ctx.Log
            $wdMark = Get-LogLength $Ctx.Watchdog
            $go = Read-Host '  Enter when done (s = later, q = stop here)'
            if($go -eq 'q') { return }
            if($go -eq 's') { continue }
            $evidenceLines = @()
            $auto = @(Invoke-AutoCheck $id $Ctx)
            if($auto.Count -gt 0) { $evidenceLines += $auto }
            if($LogPatterns.ContainsKey($id)) {
                $new = @(Read-LogFrom $Ctx.Log $mark)
                $hits = @($new | Where-Object { $_ -match ('(?i)' + $LogPatterns[$id]) })
                $evidenceLines += ("new XMB log lines: $($new.Count), matching /$($LogPatterns[$id])/: $($hits.Count)")
                $evidenceLines += @(Protect-Text ($hits | Select-Object -Last 40))
                $wdNew = @(Read-LogFrom $Ctx.Watchdog $wdMark)
                if($wdNew.Count) { $evidenceLines += 'watchdog.log:'; $evidenceLines += @(Protect-Text $wdNew) }
            }
            if($evidenceLines.Count -gt 0) {
                Write-Utf8 (Join-Path $Ctx.Evidence "logs\$id.txt") (($evidenceLines -join "`r`n") + "`r`n")
                $evidenceLines | Select-Object -First 25 | ForEach-Object { Say "    $_" 'DarkCyan' }
                if($evidenceLines.Count -gt 25) { Say "    ... (all $($evidenceLines.Count) lines in logs\$id.txt)" 'DarkCyan' }
            }
            $answer = ''
            while(-not $ResultNames.ContainsKey($answer)) { $answer = (Read-Host '  Result: p = PASS, f = FAIL, a = PARTIAL, n = NOT RUN').Trim().ToLowerInvariant() }
            $note = Read-Host '  Note (what you saw; for FAIL/PARTIAL/NOT RUN say why; Enter for none)'
            $summary = ''
            if($auto.Count -gt 0) { $summary = (@($auto | Where-Object { $_ -match '^\[(ok|!!)\]|=|:' }) | Select-Object -First 3) -join ' / ' }
            if($LogPatterns.ContainsKey($id)) { $summary = ($summary + ' (log lines in logs/' + $id + '.txt)').Trim() }
            $Results[$id] = [pscustomobject]@{ ID = $id; Sitting = $s.N; Result = $ResultNames[$answer]; Note = $note; Auto = $summary; When = (Get-Date).ToString('s') }
            Save-Results $Ctx.Results $Results $Rows
            Say "  recorded $id = $($ResultNames[$answer])" 'Green'
        }
    }
    Say ''
    Say 'Done for now. "htpc-run.ps1 status" shows what is left; "htpc-run.ps1 report" writes the report.' 'Cyan'
}

# ---------------------------------------------------------------------------------------------------------------
# Self-test (no Windows features needed; CI runs it on windows-latest)
# ---------------------------------------------------------------------------------------------------------------
function Invoke-SelfTest {
    function Check([bool]$Ok, [string]$What) { if($Ok) { Say "  ok   $What" 'Green' } else { Say "  FAIL $What" 'Red'; $script:SelfTestFailures++ } }
    $script:SelfTestFailures = 0
    $path = Find-Checklist
    $rows = @(Read-ChecklistRows $path)
    Say "checklist: $path ($($rows.Count) rows)"
    Check ($rows.Count -ge 100) 'checklist has at least 100 rows'
    $ids = @($rows | ForEach-Object { $_.Id })
    $dupes = @($ids | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    Check ($dupes.Count -eq 0) "row IDs are unique ($($dupes -join ', '))"
    $sitIds = @(); foreach($s in $Sittings) { $sitIds += $s.Ids }
    $missing = @($ids | Where-Object { $sitIds -notcontains $_ })
    $unknown = @($sitIds | Where-Object { $ids -notcontains $_ })
    $twice = @($sitIds | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    Check ($missing.Count -eq 0) "every checklist row is in a sitting (missing: $($missing -join ', '))"
    Check ($unknown.Count -eq 0) "every sitting row is in the checklist (unknown: $($unknown -join ', '))"
    Check ($twice.Count -eq 0) "no row is in two sittings ($($twice -join ', '))"
    $bad = @($rows | Where-Object { -not $_.Do -or -not $_.Expect -or -not $_.Evidence } | ForEach-Object { $_.Id })
    Check ($bad.Count -eq 0) "every row has do, expect and evidence text ($($bad -join ', '))"
    foreach($k in $LogPatterns.Keys) { try { $null = [regex]::new($LogPatterns[$k]) } catch { Check $false "log pattern $k compiles" } }
    Check ($sitIds[-1] -eq 'PW4') 'Turn Off (PW4) is the very last row'

    $in = @(
        'boot: first frame after 982 ms',
        'jellyfin: GET http://192.168.1.20:8096/Items?api_key=abc123',
        'Authorization: MediaBrowser Token="x"',
        'autologon: on for alice',
        'tailscale: status Running self htpc-box 8 peers',
        'path C:\Users\alice\Videos\film.mkv',
        'wol: AA:BB:CC:DD:EE:FF at 10.0.0.5 tailnet 100.101.102.103',
        'spotify: refresh_token = zzz',
        '{"token": "abc"}',
        'steam 76561198000000000 account=bob'
    )
    $out = @(Protect-Text $in)
    Check ($out[0] -eq $in[0]) 'scrub keeps a plain line'
    Check ($out[1] -eq '[line withheld by scrub]') 'scrub drops a line with api_key='
    Check ($out[2] -eq '[line withheld by scrub]') 'scrub drops an Authorization line'
    Check ($out[3] -eq 'autologon: on for <user>') 'scrub masks the autologon user'
    Check ($out[4] -match 'self <host>') 'scrub masks the tailscale host'
    Check ($out[5] -eq 'path %USERPROFILE%\Videos\film.mkv') 'scrub masks the profile path'
    Check ($out[6] -eq 'wol: <mac> at <ip> tailnet <tailnet-ip>') 'scrub masks MAC, LAN and tailnet addresses'
    Check ($out[7] -eq '[line withheld by scrub]') 'scrub drops a refresh token line'
    Check ($out[8] -eq '[line withheld by scrub]') 'scrub drops a JSON token line'
    Check ($out[9] -match '<steam-id>' -and $out[9] -match 'account=<account>') 'scrub masks Steam id and account'

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('htpc-run-selftest-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $tmp 'photos') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $tmp 'logs') -Force | Out-Null
    try {
        $log = Join-Path $tmp 'xmb.log'
        Write-Utf8 $log "one`r`ntwo`r`n"
        $mark = Get-LogLength $log
        [System.IO.File]::AppendAllText($log, "three`r`nremote: play`r`n", $Utf8)
        $new = @(Read-LogFrom $log $mark)
        Check ($new.Count -eq 2 -and $new[1] -eq 'remote: play') 'log reader returns only lines after the mark'
        Write-Utf8 $log "rotated`r`n"
        $new = @(Read-LogFrom $log 9999)
        Check ($new.Count -eq 1 -and $new[0] -eq 'rotated') 'log reader starts over after a rotation'

        $ctx = [pscustomobject]@{ Evidence = $tmp; Results = (Join-Path $tmp 'results.csv'); Exe = $null; Package = $null; Log = $log; Data = $null; Watchdog = $null; Flash = $null; Config = $null; Zip = $null }
        $res = @{}
        $res['B1'] = [pscustomobject]@{ ID = 'B1'; Sitting = 2; Result = 'PASS'; Note = 'intro played | fine'; Auto = 'boot: first frame after 982 ms'; When = '2026-11-13T10:00:00' }
        $res['R9'] = [pscustomobject]@{ ID = 'R9'; Sitting = 4; Result = 'FAIL'; Note = 'key reached C:\Users\alice\x'; Auto = ''; When = '2026-11-13T10:05:00' }
        Save-Results $ctx.Results $res $rows
        $back = Read-Results $ctx.Results
        Check ($back.Count -eq 2 -and $back['R9'].Result -eq 'FAIL' -and $back['B1'].Note -eq 'intro played | fine') 'results.csv round-trips'
        Write-Utf8 (Join-Path $tmp 'photos\B1.jpg') 'x'
        Write-Utf8 (Join-Path $tmp 'photos\B10-other.jpg') 'x'
        $report = New-Report $ctx $rows $back
        $text = [System.IO.File]::ReadAllText($report, $Utf8)
        Check ($text -match '\*\*1 PASS, 1 FAIL, 0 PARTIAL, 0 NOT RUN\*\*') 'report counts results'
        Check ($text -match '\| B1 \|[^\r\n]*photos/B1\.jpg' -and $text -notmatch 'B10-other') 'report links photos by row ID only'
        Check ($text -match 'intro played \\\| fine') 'report escapes pipes in notes'
        Check ($text -notmatch 'alice') 'report is scrubbed'

        $soak = Join-Path $tmp 'soak.csv'
        Write-Utf8 $soak "Time,Pid,WorkingSetMB,PrivateMB,Handles,Threads,CpuPercent`r`n2026-11-13T10:00:00,10,300.0,450.0,1600,14,12.0`r`n2026-11-13T12:00:00,10,301.5,451.0,1610,14,10.0`r`n"
        $sum = Get-SoakSummary $soak
        Check ($sum[0] -match '2 samples over 2\.0 h' -and $sum[4] -match ': 1 ') 'soak summary reads the csv'
    } finally { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }

    if($script:SelfTestFailures -gt 0) { Say "$($script:SelfTestFailures) check(s) failed" 'Red'; exit 1 }
    Say 'selftest passed' 'Green'
}

# ---------------------------------------------------------------------------------------------------------------
switch($Action) {
    'selftest' { Invoke-SelfTest; break }
    default {
        if(-not $IsWin) { throw 'This action needs Windows (the HTPC). Only selftest runs elsewhere.' }
        $ctx = Get-Context
        $rows = @(Read-ChecklistRows (Find-Checklist))
        $results = Read-Results $ctx.Results
        switch($Action) {
            'preflight' { Invoke-Preflight $ctx }
            'run'       { Invoke-Run $ctx $rows $results }
            'status'    { Say "Evidence: $($ctx.Evidence)   Package: $($ctx.Package)"; Show-Status $rows $results }
            'launch'    { Start-Xmb $ctx }
            'soak'      { Start-Soak $ctx }
            'collect'   { Invoke-Collect $ctx }
            'report'    { $p = New-Report $ctx $rows $results; Say "Report: $p" 'Green' }
        }
    }
}
