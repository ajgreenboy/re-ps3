#requires -Version 5.1
<#
release-check.ps1 - Phase 7.5 release gate for native-xmb.

Checks, in order:
  1. the maintained harness passes: bun work/ab-layers.ts --all
  2. the tracked tree carries no credential or key material
  3. the tree carries no firmware/raw-data file (tracked or untracked)
  4. the documentation links in the current handoff set resolve

Prints PASS/FAIL per check and exits 0 only when all checks pass.
Writes nothing except an optional report file named by -ReportPath; the gate itself
uses its own temporary capture directories. Never prints a secret value.

Usage (Windows PowerShell 5.1 or PowerShell 7):
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -GateExe C:\dev\out\native-xmb\re-ps3-native.exe
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -ReportPath $env:TEMP\xmb-release-check.txt
#>
[CmdletBinding()]
param(
    [string]$GateExe,
    [string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$checks = [System.Collections.Generic.List[object]]::new()
$lines = [System.Collections.Generic.List[string]]::new()

function Add-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $checks.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
    $state = if ($Ok) { 'PASS' } else { 'FAIL' }
    $line = '{0,-5} {1,-10} {2}' -f $state, $Name, $Detail
    Write-Host $line
    $lines.Add($line)
}

function Add-Line {
    param([string]$Text)
    Write-Host $Text
    $lines.Add($Text)
}

Set-Location -LiteralPath $repo
Add-Line ''
Add-Line '== native-xmb release check =='
Add-Line ("repo    {0}" -f $repo)

# --- repository identity (informational, not a gate) -------------------------
try {
    $head = (& git rev-parse --short HEAD 2>$null | Select-Object -First 1)
    $branch = (& git rev-parse --abbrev-ref HEAD 2>$null | Select-Object -First 1)
    $origin = (& git rev-parse --short origin/main 2>$null | Select-Object -First 1)
    $ahead = (& git rev-list --count origin/main..HEAD 2>$null | Select-Object -First 1)
    Add-Line ("git     {0} on {1}; origin/main {2}; {3} commit(s) ahead (local ref, no fetch)" -f $head, $branch, $origin, $ahead)
} catch {
    Add-Line 'git     unavailable'
}
$dirty = @(& git status --porcelain --untracked-files=all 2>$null).Count
Add-Line ("tree    {0} changed/untracked entr(y/ies) - release commit must contain only intended files" -f $dirty)
Add-Line ''

# --- 1. harness gate ---------------------------------------------------------
$bun = $null
$bunCommand = Get-Command bun -ErrorAction SilentlyContinue
if ($bunCommand) { $bun = $bunCommand.Path }
if (-not $bun) { $bun = $bunCommand.Source }
if (-not $bun) {
    $fallback = Join-Path $env:USERPROFILE '.bun\bin\bun.exe'
    if (Test-Path -LiteralPath $fallback) { $bun = $fallback }
}
if (-not $bun) {
    Add-Check 'gate' $false 'bun not found; install Bun or put it on PATH'
} else {
    $gateArgs = @('work/ab-layers.ts', '--all')
    if ($GateExe) {
        if (-not (Test-Path -LiteralPath $GateExe)) {
            Add-Check 'gate' $false ("-GateExe not found: {0}" -f $GateExe)
            $gateArgs = $null
        } else {
            $gateArgs += @('--exe', (Resolve-Path -LiteralPath $GateExe).Path)
        }
    }
    if ($gateArgs) {
        Add-Line '-- gate output (tail) --'
        $gateLines = @()
        try {
            $gateLines = @(& $bun @gateArgs 2>&1 | ForEach-Object { [string]$_ })
            $gateExit = $LASTEXITCODE
        } catch {
            $gateLines = @([string]$_)
            $gateExit = 2
        }
        $gateLines | Select-Object -Last 8 | ForEach-Object { Add-Line ("   {0}" -f $_) }
        $whole = @($gateLines | Where-Object { $_ -match '^whole set:' } | Select-Object -Last 1)
        $states = @($gateLines | Where-Object { $_ -match '^states:' } | Select-Object -Last 1)
        $summaryText = if ($whole.Count) { $whole[-1] } elseif ($states.Count) { $states[-1] } else { 'no summary line' }
        $ok = ($gateExit -eq 0)
        $detail = if ($ok) { 'exit 0; ' + $summaryText } else { ("exit {0}; " -f $gateExit) + $summaryText }
        if ($detail.Length -gt 240) { $detail = $detail.Substring(0, 240) + '...' }
        Add-Check 'gate' $ok $detail
        Add-Line ''
    }
}

# --- 2. credential scan ------------------------------------------------------
$secretPatterns = @(
    [pscustomobject]@{ Name = 'steam-key-assignment'; Pattern = 'STEAM_WEB_API_KEY\s*=\s*[A-Za-z0-9]{16,}' },
    [pscustomobject]@{ Name = 'steam-key-file-value'; Pattern = 'steam_api_key\s*[=:]\s*[A-Za-z0-9]{16,}' },
    [pscustomobject]@{ Name = 'bearer-token'; Pattern = 'Authorization\s*:\s*Bearer\s+[A-Za-z0-9._\-]{12,}' },
    [pscustomobject]@{ Name = 'emby-token'; Pattern = 'X-Emby-Token\s*:\s*[A-Za-z0-9]{8,}' },
    [pscustomobject]@{ Name = 'query-api-key'; Pattern = '[?&](api_key|ApiKey|apiKey)=[A-Za-z0-9]{8,}' },
    [pscustomobject]@{ Name = 'private-key-block'; Pattern = '-----BEGIN [A-Z ]*PRIVATE KEY-----' }
)

$keyValue = $null
$keyFile = Join-Path $env:APPDATA 're-ps3-native\steam_api_key'
if (Test-Path -LiteralPath $keyFile) {
    try {
        $candidate = (Get-Content -LiteralPath $keyFile -Raw).Trim()
        if ($candidate.Length -ge 8) { $keyValue = $candidate }
    } catch {
        $keyValue = $null
    }
}

$tracked = @(& git ls-files 2>$null)
$keyFindings = [System.Collections.Generic.List[string]]::new()
$scanned = 0
foreach ($rel in $tracked) {
    $path = Join-Path $repo $rel
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    try {
        $item = Get-Item -LiteralPath $path
        if ($item.Length -gt 2MB) { continue }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $hasNul = $false
        if ($bytes.Length -gt 0) {
            $limit = [Math]::Min(4095, $bytes.Length - 1)
            for ($i = 0; $i -le $limit; $i++) {
                if ($bytes[$i] -eq 0) { $hasNul = $true; break }
            }
        }
        if ($hasNul) { continue }
        $text = [System.IO.File]::ReadAllText($path)
        if ($null -eq $text) { continue }
        $scanned++
        foreach ($pattern in $secretPatterns) {
            if ([regex]::IsMatch($text, $pattern.Pattern)) {
                $keyFindings.Add(("{0} [{1}]" -f $rel, $pattern.Name))
            }
        }
        if ($keyValue -and $text.Contains($keyValue)) {
            $keyFindings.Add(("{0} [configured steam key value]" -f $rel))
        }
    } catch {
        continue
    }
}
if ($keyFindings.Count -eq 0) {
    Add-Check 'keys' $true ("0 findings in {0} tracked text files" -f $scanned)
} else {
    Add-Check 'keys' $false ("{0} finding(s): {1}" -f $keyFindings.Count, (($keyFindings | Select-Object -First 5) -join '; '))
}

# --- 3. firmware / raw data scan --------------------------------------------
$forbidden = '\.(sprx|self|prx|rco|qrc|p3t|raf|sfo|pkg|pup|bin|elf|vpo|fpo|gim|dds|vag|ac3|at3|iso|exe|dll)$'
$trackedLeaks = @($tracked | Where-Object { $_ -match $forbidden })
$untrackedLeaks = @()
$statusLines = @(& git status --porcelain --untracked-files=all 2>$null)
foreach ($line in $statusLines) {
    if ($line.StartsWith('?? ')) {
        $candidate = $line.Substring(3).Trim().Trim('"')
        if ($candidate -match $forbidden) { $untrackedLeaks += $candidate }
    }
}
# A long single-line base64 blob in a tracked text file is an embedded asset, not prose.
$blobHits = [System.Collections.Generic.List[string]]::new()
foreach ($rel in $tracked) {
    $path = Join-Path $repo $rel
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    try {
        $item = Get-Item -LiteralPath $path
        if ($item.Length -gt 512KB) { continue }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $hasNul = $false
        if ($bytes.Length -gt 0) {
            $limit = [Math]::Min(4095, $bytes.Length - 1)
            for ($i = 0; $i -le $limit; $i++) {
                if ($bytes[$i] -eq 0) { $hasNul = $true; break }
            }
        }
        if ($hasNul) { continue }
        $text = [System.IO.File]::ReadAllText($path)
        if ($text -and [regex]::IsMatch($text, '(?m)^[A-Za-z0-9+/]{512,}={0,2}\r?$')) {
            $blobHits.Add($rel)
        }
    } catch {
        continue
    }
}
$dataCount = $trackedLeaks.Count + $untrackedLeaks.Count + $blobHits.Count
if ($dataCount -eq 0) {
    Add-Check 'data' $true 'no tracked or untracked firmware/raw-data files; no base64 blobs'
} else {
    $examples = @()
    $examples += $trackedLeaks
    $examples += $untrackedLeaks
    $examples += ($blobHits | ForEach-Object { "$_ [base64 blob]" })
    Add-Check 'data' $false ("{0} finding(s): {1}" -f $dataCount, (($examples | Select-Object -First 5) -join '; '))
}

# --- 4. documentation links --------------------------------------------------
$docs = @(
    'native-xmb/README.md',
    'native-xmb/audit/README.md',
    'native-xmb/HANDOFF-2026-10-27.md',
    'native-xmb/INSTALL.md',
    'native-xmb/audit/DIAGNOSE.md'
)
$missing = [System.Collections.Generic.List[string]]::new()
$linkCount = 0
foreach ($doc in $docs) {
    $docPath = Join-Path $repo $doc
    if (-not (Test-Path -LiteralPath $docPath)) {
        $missing.Add(("{0} (document missing)" -f $doc))
        continue
    }
    $text = Get-Content -LiteralPath $docPath -Raw
    foreach ($m in [regex]::Matches($text, '\]\(([^)\s]+\.md(?:#[^)\s]*)?)\)')) {
        $target = $m.Groups[1].Value
        if ($target -match '^(https?:|mailto:|#)') { continue }
        $target = ($target -split '#')[0]
        if (-not $target) { continue }
        $linkCount++
        $resolved = Join-Path (Split-Path -Parent $docPath) $target
        if (-not (Test-Path -LiteralPath $resolved)) {
            $missing.Add(("{0} -> {1}" -f $doc, $target))
        }
    }
}
if ($missing.Count -eq 0) {
    Add-Check 'docs' $true ("{0} .md links across {1} documents resolve" -f $linkCount, $docs.Count)
} else {
    Add-Check 'docs' $false ("{0} broken link(s): {1}" -f $missing.Count, (($missing | Select-Object -First 5) -join '; '))
}

# --- summary -----------------------------------------------------------------
$failed = @($checks | Where-Object { -not $_.Ok }).Count
Add-Line ''
$overall = if ($failed -eq 0) { 'PASS' } else { 'FAIL' }
$resultLine = ("RESULT {0} ({1} of {2} checks passed)" -f $overall, ($checks.Count - $failed), $checks.Count)
Write-Host $resultLine
$lines.Add($resultLine)

if ($ReportPath) {
    try {
        $reportDir = Split-Path -Parent $ReportPath
        if ($reportDir -and -not (Test-Path -LiteralPath $reportDir)) {
            New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
        }
        Set-Content -LiteralPath $ReportPath -Value $lines -Encoding ascii
        Write-Host ("report  {0}" -f $ReportPath)
    } catch {
        Write-Host ("report  could not write {0}: {1}" -f $ReportPath, $_.Exception.Message)
    }
}

if ($failed -gt 0) { exit 1 }
exit 0
