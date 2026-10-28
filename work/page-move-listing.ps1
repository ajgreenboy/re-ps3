# Instruction-level reads of the local decrypted firmware modules (read-only).
#
# Wraps the existing Ghidra headless project so the page-move call sites can be
# re-read without re-analysing or saving anything:
#   * the project is opened with -noanalysis and -readOnly, so every script below
#     works on the already-analysed program and nothing is written back;
#   * output files go to $env:TEMP (never into the repo).
#
# Usage (from the repo root):
#   powershell -File work/page-move-listing.ps1 list  3abc9c 3abcec
#       disassembly listing (ghidra_scripts/ListRange.java)
#   powershell -File work/page-move-listing.ps1 decomp 147e28
#       decompile the function containing the given address
#       (ghidra_scripts/DecompileAt.java)
#   powershell -File work/page-move-listing.ps1 scan opd 0x3981ec
#   powershell -File work/page-move-listing.ps1 scan word 0x6df3a0
#       word / OPD scans on the ELF itself (work/page-move-scan.ts)
#
# The Ghidra project is ghidra_work/vsh493w, program vsh.elf. The same programs
# exist in ghidra_proj/vsh493; vsh493w is used because it is the project named in
# the phase 14.1 brief.
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateSet('list', 'decomp', 'scan')][string]$Mode,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)

$repo = Split-Path -Parent $PSScriptRoot
$ghidra = 'C:\dev\deps\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat'
$project = Join-Path $repo 'ghidra_work'
$scripts = Join-Path $repo 'ghidra_scripts'
$scratch = Join-Path $env:TEMP 'page-move-14.1'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

# Runs one headless script, prints its tail, and reports whether the project was saved.
# Returns nothing (so the caller's $log stays a plain path).
function Invoke-Headless([string]$script, [string]$logPath, [string[]]$scriptArgs) {
    $headlessArgs = @($project, 'vsh493w', '-process', 'vsh.elf', '-noanalysis', '-readOnly',
        '-scriptPath', $scripts, '-preScript', $script) + $scriptArgs + @('-log', $logPath)
    & $ghidra @headlessArgs 2>&1 | Select-Object -Last 2 | ForEach-Object { Write-Host $_ }
    if (Select-String -Path $logPath -Pattern 'Saving' -Quiet) { Write-Host 'WARNING: project was saved' }
    else { Write-Host 'project not saved (read-only)' }
}

switch ($Mode) {
    'list' {
        $outFile = Join-Path $scratch 'listing.txt'
        $log = Join-Path $scratch 'headless-list.log'
        $start = $Rest[0]
        $end = if ($Rest.Count -gt 1) { $Rest[1] } else { '{0:x}' -f ([Convert]::ToInt64($start, 16) + 0x140) }
        Invoke-Headless 'ListRange.java' $log @($outFile, $start, $end)
        Write-Host "listing: $outFile"
        Get-Content $outFile
    }
    'decomp' {
        $outFile = Join-Path $scratch 'decompiled.c'
        $log = Join-Path $scratch 'headless-decomp.log'
        Invoke-Headless 'DecompileAt.java' $log (@($outFile) + $Rest)
        Write-Host "decompile: $outFile"
        Get-Content $outFile
    }
    'scan' {
        & 'C:\Users\Albert\.local\bin\bun.exe' (Join-Path $repo 'work\page-move-scan.ts') `
            (Join-Path $repo 'fw493\vsh_module\vsh.elf') @Rest
    }
}
