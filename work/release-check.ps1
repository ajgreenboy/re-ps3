#requires -Version 5.1
<#
release-check.ps1 - the native-xmb release gate (Phase 8.5).

Checks, in order:

  1 gate    bun work/ab-layers.ts --all      product gate: the stored per-layer pair, the
                                             canonical capture and every named state. This is
                                             the only harness check whose exit code fails the run.
  2 parity  bun work/parity.ts               the fidelity corpus report: which cells have no
            --references-missing             RPCS3 reference yet. Report only - it exits 1
                                             while cells are missing, which is the honest state
                                             today, so its exit code is never a FAIL.
  3 keys    credential scan: Steam Web API key (assignment, file value, 32-hex literal), bearer
            and Emby/MediaBrowser tokens, JWTs, api_key query values, private-key blocks,
            long password/token/secret assignments, NPSSO and PSN ids; plus the exact value
            of the local steam_api_key file, and credential file names a forced `git add`
            could have committed.
  4 data    firmware-derived data: no tracked or untracked file with a firmware extension
            (.rco, .prf, .gim, .p3t, .raf, .sprx, ...) and no embedded base64 asset.
  5 dump    analysis output that .gitignore hides under `work/*` (and keeps out of the root):
            a tracked .txt/.log/.dump/.err/.out/.bin/.dat file at the repo root or under work/.
  6 docs    every relative .md link in the current handoff set resolves.
  7 stale   every 7- or 40-character commit id cited by a current document exists here. A
            handoff or plan that quotes a dead commit is stale; this is the check that
            catches it without a network fetch.

Modes:

  (default)  run everything; exit 0 only when every executed check passes.
  -SkipBuild report mode: skip checks 1 and 2 (they need a local build and the local
             reference set). Checks 3-7 still run and still fail the run. Use this on a
             docs-only tree or a fresh clone.
  -WhatIf    plan mode: print every check and the exact harness command, execute nothing,
             write nothing. Always exits 0.

Safety: this script never deletes, moves or renames a file, performs no fetch of its own (the two
harness commands are local scripts; the gate starts the local build), and writes nothing except an
optional report file named by -ReportPath. It never prints a secret value: a finding names the file and
the pattern, never the matched text.

Usage (Windows PowerShell 5.1 or PowerShell 7), from the repository root:

  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -SkipBuild
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -WhatIf
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -GateExe C:\dev\out\native-xmb\re-ps3-native.exe
  powershell -NoProfile -ExecutionPolicy Bypass -File work\release-check.ps1 -ReportPath $env:TEMP\xmb-release-check.txt
#>
[CmdletBinding()]
param(
    # A built re-ps3-native.exe to score instead of the one the thresholds file names.
    [string]$GateExe,
    # Bun executable. Default: `bun` on PATH, then %USERPROFILE%\.bun\bin\bun.exe.
    [string]$BunPath,
    # Write the run's lines to this file as ASCII. The only write this script performs.
    [string]$ReportPath,
    # Report mode: skip the two harness checks that need a local build.
    [switch]$SkipBuild,
    # Plan mode: print what would run and run nothing.
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$checks = [System.Collections.Generic.List[object]]::new()
$lines = [System.Collections.Generic.List[string]]::new()

function Add-Line {
    param([string]$Text)
    Write-Host $Text
    $lines.Add($Text)
}

# State is PASS, FAIL, SKIP or INFO. A SKIP is not a pass and an INFO is not a gate (the parity
# report is the only INFO check); the RESULT line reports every count separately.
function Add-Check {
    param([string]$Name, [string]$State, [string]$Detail)
    $checks.Add([pscustomobject]@{ Name = $Name; State = $State; Detail = $Detail })
    $line = '{0,-5} {1,-7} {2}' -f $State, $Name, $Detail
    Write-Host $line
    $lines.Add($line)
}

# One bounded read of a tracked/untracked file: text only, <= 2 MB, no NUL in the first 4 KB.
function Get-TextSample {
    param([string]$Path)
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
        $item = Get-Item -LiteralPath $Path
        if ($item.Length -gt 2MB) { return $null }
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -gt 0) {
            $limit = [Math]::Min(4095, $bytes.Length - 1)
            for ($i = 0; $i -le $limit; $i++) {
                if ($bytes[$i] -eq 0) { return $null }
            }
        }
        return [System.IO.File]::ReadAllText($Path)
    } catch {
        return $null
    }
}

Push-Location -LiteralPath $repo
try {
    Add-Line ''
    Add-Line '== native-xmb release check (Phase 8.5) =='
    Add-Line ("repo    {0}" -f $repo)
    $mode = if ($WhatIf) { 'plan (-WhatIf: nothing is executed)' }
            elseif ($SkipBuild) { 'report (-SkipBuild: the harness gate and the parity report are skipped)' }
            else { 'full (harness gate + parity report + static scans)' }
    Add-Line ("mode    {0}" -f $mode)

    # --- repository identity (informational, not a gate) ----------------------
    $haveGit = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
    if ($haveGit) {
        try {
            $head = (& git rev-parse --short HEAD 2>$null | Select-Object -First 1)
            $branch = (& git rev-parse --abbrev-ref HEAD 2>$null | Select-Object -First 1)
            $count = (& git rev-list --count HEAD 2>$null | Select-Object -First 1)
            $origin = (& git rev-parse --short origin/main 2>$null | Select-Object -First 1)
            $ahead = (& git rev-list --count origin/main..HEAD 2>$null | Select-Object -First 1)
            $tags = @(& git tag --list 'native-xmb-*' 2>$null)
            $tagText = if ($tags.Count) { $tags -join ' ' } else { 'none' }
            Add-Line ("git     {0} on {1}; {2} commit(s); origin/main {3}; {4} ahead; release tags: {5}" -f `
                $head, $branch, $count, $origin, $ahead, $tagText)
        } catch {
            Add-Line 'git     unavailable'
        }
        $statusLines = @(& git status --porcelain --untracked-files=all 2>$null)
        Add-Line ("tree    {0} changed/untracked entr(y/ies); the release commit must contain only intended files" -f $statusLines.Count)
    } else {
        Add-Line 'git     not found; the commit, tree and stale-document checks cannot run'
    }
    Add-Line ''

    $tracked = @()
    if ($haveGit) { $tracked = @(& git ls-files 2>$null) }

    # --- 1/2. harness gate and parity report ----------------------------------
    $bun = $null
    if ($BunPath) {
        if (Test-Path -LiteralPath $BunPath) { $bun = (Resolve-Path -LiteralPath $BunPath).Path }
    }
    if (-not $bun) {
        $bunCommand = Get-Command bun -ErrorAction SilentlyContinue
        if ($bunCommand) { $bun = $bunCommand.Path }
    }
    if (-not $bun) {
        $fallback = Join-Path $env:USERPROFILE '.bun\bin\bun.exe'
        if (Test-Path -LiteralPath $fallback) { $bun = $fallback }
    }

    $gateArgs = @('work/ab-layers.ts', '--all')
    if ($GateExe) { $gateArgs += @('--exe', $GateExe) }
    $parityArgs = @('work/parity.ts', '--references-missing')

    if ($SkipBuild -or $WhatIf) {
        $why = if ($WhatIf) { 'plan only' } else { 'report mode: needs a local build and the local reference set' }
        Add-Check 'gate' 'SKIP' ("{0}; would run: bun {1}" -f $why, ($gateArgs -join ' '))
        Add-Check 'parity' 'SKIP' ("{0}; would run: bun {1}" -f $why, ($parityArgs -join ' '))
    } elseif (-not $bun) {
        Add-Check 'gate' 'FAIL' 'bun not found; install Bun, put it on PATH or pass -BunPath'
        Add-Check 'parity' 'SKIP' 'bun not found'
    } else {
        # Precondition: a configured capture executable that is absent makes the gate red for
        # a configuration reason, not a fidelity reason. Diagnose it without starting a run.
        $gateBlocked = $null
        if (-not $GateExe) {
            $thresholds = Join-Path $repo 'work\ab-layers.thresholds.json'
            if (Test-Path -LiteralPath $thresholds) {
                $match = Select-String -LiteralPath $thresholds -Pattern '"exe"\s*:\s*"([^"]+)"' |
                    Select-Object -First 1
                if ($match) {
                    $captureExe = $match.Matches[0].Groups[1].Value
                    if (-not (Test-Path -LiteralPath $captureExe)) {
                        $gateBlocked = ("configured capture exe is absent: {0} (Phase 8.3; pass -GateExe to score another build)" -f $captureExe)
                    }
                }
            }
        }
        if ($gateBlocked) {
            Add-Check 'gate' 'FAIL' $gateBlocked
        } else {
            Add-Line ('-- gate: bun ' + ($gateArgs -join ' ') + ' (this can take minutes) --')
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
            if ($ok) { Add-Check 'gate' 'PASS' $detail } else { Add-Check 'gate' 'FAIL' $detail }
            Add-Line ''
        }

        # Report only. parity.ts --references-missing exits 1 while cells are missing; that is
        # the number this report exists to publish, never a release blocker on its own.
        Add-Line ('-- parity: bun ' + ($parityArgs -join ' ') + ' (report only; never gates) --')
        $parityLines = @()
        $parityExit = 0
        try {
            $parityLines = @(& $bun @parityArgs 2>&1 | ForEach-Object { [string]$_ })
            $parityExit = $LASTEXITCODE
        } catch {
            $parityLines = @([string]$_)
            $parityExit = -1
        }
        $missingLine = @($parityLines | Where-Object { $_ -match 'cell\(s\) have no reference' } | Select-Object -Last 1)
        $parityDetail = if ($missingLine.Count) { $missingLine[-1].Trim() } else { 'no summary line' }
        if ($parityDetail.Length -gt 200) { $parityDetail = $parityDetail.Substring(0, 200) + '...' }
        Add-Check 'parity' 'INFO' ("report only, never gates (exit {0}): {1}" -f $parityExit, $parityDetail)
        Add-Line ''
    }

    if ($WhatIf) {
        Add-Check 'keys' 'SKIP' 'plan only; would scan the tracked text tree for credentials'
        Add-Check 'data' 'SKIP' 'plan only; would scan tracked, untracked and embedded data'
        Add-Check 'dump' 'SKIP' 'plan only; would scan for analysis output outside the ignore rules'
        Add-Check 'docs' 'SKIP' 'plan only; would resolve the relative .md links of the handoff set'
        Add-Check 'stale' 'SKIP' 'plan only; would verify every commit id the handoff set cites'
        Add-Line ''
    } else {

    # --- 3. credential scan ---------------------------------------------------
    $secretPatterns = @(
        [pscustomobject]@{ Name = 'steam-key-assignment'; Pattern = 'STEAM_WEB_API_KEY\s*=\s*[A-Za-z0-9]{16,}' },
        [pscustomobject]@{ Name = 'steam-key-literal'; Pattern = '(?i)(steam[_-]?(web[_-]?)?api[_-]?key)["'']?\s*[:=]\s*["'']?[0-9A-Fa-f]{32}\b' },
        [pscustomobject]@{ Name = 'steam-key-file-value'; Pattern = 'steam_api_key\s*[=:]\s*[A-Za-z0-9]{16,}' },
        [pscustomobject]@{ Name = 'bearer-token'; Pattern = 'Authorization\s*:\s*Bearer\s+[A-Za-z0-9._\-]{12,}' },
        [pscustomobject]@{ Name = 'emby-token'; Pattern = 'X-(Emby|MediaBrowser)-Token\s*:\s*[A-Za-z0-9]{8,}' },
        [pscustomobject]@{ Name = 'jwt'; Pattern = '\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}' },
        [pscustomobject]@{ Name = 'query-api-key'; Pattern = '[?&](api_key|ApiKey|apiKey)=[A-Za-z0-9]{8,}' },
        [pscustomobject]@{ Name = 'secret-assignment'; Pattern = '(?i)\b(api[_-]?key|access[_-]?token|refresh[_-]?token|client[_-]?secret|password|passwd|pwd)\b["'']?\s*[:=]\s*["''][^"''\r\n]{12,}["'']' },
        [pscustomobject]@{ Name = 'npsso-token'; Pattern = '(?i)\bnpsso\b\s*[:=]\s*[A-Za-z0-9]{16,}' },
        [pscustomobject]@{ Name = 'psn-id'; Pattern = '(?i)\b(psn[_-]?(id|account[_-]?id|online[_-]?id|sso[_-]?token)|online[_-]?id)\b["'']?\s*[:=]\s*["'']?[A-Za-z0-9_-]{4,}' },
        [pscustomobject]@{ Name = 'private-key-block'; Pattern = '-----BEGIN [A-Z ]*PRIVATE KEY-----' }
    )
    # File names .gitignore already covers. A forced `git add` defeats the ignore rules, so the
    # tracked list is checked by name as well as by content.
    $credentialNames = '^(steam_api_key(\..+)?|\.env(\..+)?|.*\.pem|.*\.key|console-psid.*|idps.*)$'

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

    $keyFindings = [System.Collections.Generic.List[string]]::new()
    $scanned = 0
    foreach ($rel in $tracked) {
        $name = Split-Path -Leaf $rel
        if ($name -match $credentialNames) {
            $keyFindings.Add(("{0} [credential file name]" -f $rel))
            continue
        }
        $text = Get-TextSample -Path (Join-Path $repo $rel)
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
    }
    if ($keyFindings.Count -eq 0) {
        Add-Check 'keys' 'PASS' ("0 findings in {0} tracked text files ({1} content patterns, 1 file-name pattern)" -f $scanned, $secretPatterns.Count)
    } else {
        Add-Check 'keys' 'FAIL' ("{0} finding(s): {1}" -f $keyFindings.Count, (($keyFindings | Select-Object -First 5) -join '; '))
    }

    # --- 4. firmware / raw data scan ------------------------------------------
    $forbidden = '\.(sprx|self|prx|rco|qrc|p3t|prf|raf|sfo|pkg|pup|bin|elf|vpo|fpo|gim|dds|vag|ac3|at3|iso|exe|dll)$'
    $trackedLeaks = @($tracked | Where-Object { $_ -match $forbidden })
    $untrackedLeaks = @()
    if ($haveGit) {
        $statusLines = @(& git status --porcelain --untracked-files=all 2>$null)
        foreach ($line in $statusLines) {
            if ($line.StartsWith('?? ')) {
                $candidate = $line.Substring(3).Trim().Trim('"')
                if ($candidate -match $forbidden) { $untrackedLeaks += $candidate }
            }
        }
    }
    # A long single-line base64 blob in a tracked text file is an embedded asset, not prose.
    $blobHits = [System.Collections.Generic.List[string]]::new()
    foreach ($rel in $tracked) {
        $path = Join-Path $repo $rel
        try {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
            if ((Get-Item -LiteralPath $path).Length -gt 512KB) { continue }
            $text = Get-TextSample -Path $path
            if ($text -and [regex]::IsMatch($text, '(?m)^[A-Za-z0-9+/]{512,}={0,2}\r?$')) {
                $blobHits.Add($rel)
            }
        } catch {
            continue
        }
    }
    $dataCount = $trackedLeaks.Count + $untrackedLeaks.Count + $blobHits.Count
    if ($dataCount -eq 0) {
        Add-Check 'data' 'PASS' 'no tracked or untracked firmware/raw-data file; no embedded base64 blob'
    } else {
        $examples = @()
        $examples += $trackedLeaks
        $examples += $untrackedLeaks
        $examples += ($blobHits | ForEach-Object { "$_ [base64 blob]" })
        Add-Check 'data' 'FAIL' ("{0} finding(s): {1}" -f $dataCount, (($examples | Select-Object -First 5) -join '; '))
    }

    # --- 5. analysis output that should not be in git -------------------------
    # .gitignore hides /work/* (keeping only .ts, .ps1 and the thresholds/contract JSON) and
    # ignores *.log/*.err. A forced add is the way a dump lands in the tree, so a tracked
    # analysis file is worth a named check of its own.
    $dumpExt = '\.(txt|log|dump|err|out|bin|dat)$'
    $dumpHits = @()
    foreach ($rel in $tracked) {
        $segments = @($rel -split '/')
        $atRoot = ($segments.Count -eq 1)
        $inWork = ($segments.Count -gt 1 -and $segments[0] -eq 'work')
        if (($atRoot -or $inWork) -and $rel -match $dumpExt) { $dumpHits += $rel }
    }
    if ($dumpHits.Count -eq 0) {
        Add-Check 'dump' 'PASS' 'no analysis output (.txt/.log/.dump) tracked at the root or under work/'
    } else {
        Add-Check 'dump' 'FAIL' ("{0} tracked analysis file(s): {1}" -f $dumpHits.Count, (($dumpHits | Select-Object -First 5) -join '; '))
    }

    # --- 6. documentation links ----------------------------------------------
    $docs = @(
        'native-xmb/README.md',
        'native-xmb/audit/README.md',
        'native-xmb/HANDOFF-2026-10-28.md',
        'native-xmb/HANDOFF-2026-10-27.md',
        'native-xmb/INSTALL.md',
        'native-xmb/PHASES-2026-10-28.md',
        'native-xmb/ISA.md',
        'native-xmb/audit/DIAGNOSE.md'
    )
    $missingLinks = [System.Collections.Generic.List[string]]::new()
    $linkCount = 0
    foreach ($doc in $docs) {
        $docPath = Join-Path $repo $doc
        if (-not (Test-Path -LiteralPath $docPath)) {
            $missingLinks.Add(("{0} (document missing)" -f $doc))
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
                $missingLinks.Add(("{0} -> {1}" -f $doc, $target))
            }
        }
    }
    if ($missingLinks.Count -eq 0) {
        Add-Check 'docs' 'PASS' ("{0} .md links across {1} documents resolve" -f $linkCount, $docs.Count)
    } else {
        Add-Check 'docs' 'FAIL' ("{0} broken link(s): {1}" -f $missingLinks.Count, (($missingLinks | Select-Object -First 5) -join '; '))
    }

    # --- 7. stale documents: cited commits that do not exist ------------------
    if (-not $haveGit) {
        Add-Check 'stale' 'SKIP' 'git not found; commit ids cannot be verified'
    } else {
        $hashPattern = '`([0-9a-f]{7}|[0-9a-f]{40})`'
        $cited = [System.Collections.Generic.List[string]]::new()
        foreach ($doc in $docs) {
            $docPath = Join-Path $repo $doc
            if (-not (Test-Path -LiteralPath $docPath)) { continue }
            $text = Get-Content -LiteralPath $docPath -Raw
            foreach ($m in [regex]::Matches($text, $hashPattern)) {
                $hash = $m.Groups[1].Value
                if (-not $cited.Contains($hash)) { $cited.Add($hash) }
                if ($cited.Count -ge 500) { break }
            }
            if ($cited.Count -ge 500) { break }
        }
        $deadHashes = @()
        foreach ($hash in $cited) {
            & git cat-file -e ("{0}^{{commit}}" -f $hash) 2>$null
            if ($LASTEXITCODE -ne 0) { $deadHashes += $hash }
        }
        if ($deadHashes.Count -eq 0) {
            Add-Check 'stale' 'PASS' ("{0} cited commit id(s) across {1} documents resolve in this repository" -f $cited.Count, $docs.Count)
        } else {
            Add-Check 'stale' 'FAIL' ("{0} cited commit(s) do not exist: {1}" -f $deadHashes.Count, (($deadHashes | Select-Object -First 8) -join '; '))
        }
    }
    Add-Line ''
    }

    # --- summary --------------------------------------------------------------
    $failed = @($checks | Where-Object { $_.State -eq 'FAIL' }).Count
    $skipped = @($checks | Where-Object { $_.State -eq 'SKIP' }).Count
    $info = @($checks | Where-Object { $_.State -eq 'INFO' }).Count
    $passed = @($checks | Where-Object { $_.State -eq 'PASS' }).Count
    $overall = if ($failed -eq 0) { 'PASS' } else { 'FAIL' }
    $counts = "{0} passed, {1} failed, {2} skipped" -f $passed, $failed, $skipped
    if ($info) { $counts += ", {0} report-only" -f $info }
    $resultLine = ("RESULT {0} ({1})" -f $overall, $counts)
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

    if ($WhatIf) { exit 0 }
    if ($failed -gt 0) { exit 1 }
    exit 0
} finally {
    Pop-Location
}
