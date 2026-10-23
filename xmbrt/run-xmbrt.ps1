param(
    [int]$TimeoutSeconds = 120,
    [string]$MaxCalls = '300000',
    [string]$Thread = 'T01000000'
)
# Runs xmbrt against the linked image, normalises its call log and diffs it against the RPCS3 reference
# trace (out\trace\rpcs3.norm, from xmbtrace.ts rpcs3). Prints the end of the log and the first divergence.
$out = 'C:\dev\xmb\out\xmbrt'
$trace = 'C:\dev\xmb\out\trace'
$bun = "$env:USERPROFILE\.bun\bin\bun.exe"
$env:XMBRT_LOG = "$out\xmbrt.log"
$env:XMBRT_MAX_CALLS = $MaxCalls
$env:XMBRT_IDLE_SECONDS = '15'
$p = Start-Process -FilePath "$out\xmbrt.exe" -ArgumentList 'C:\dev\xmb\out\boot493', 'C:\dev\xmb\fw493\vsh_module\vsh.elf' `
    -WorkingDirectory $out -NoNewWindow -PassThru -RedirectStandardError "$out\stderr.txt" -RedirectStandardOutput "$out\stdout.txt"
if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { $p.Kill(); "killed after $TimeoutSeconds s" }
"exit code: $($p.ExitCode)"
"log lines: $((Get-Content "$out\xmbrt.log" | Measure-Object -Line).Lines)"
Get-Content "$out\xmbrt.log" -Tail 12
& $bun "$PSScriptRoot\xmbtrace.ts" xmbrt "$out\xmbrt.log" 'C:\dev\xmb\out\nid_names.csv' "$trace\xmbrt.norm" | Out-Null
& $bun "$PSScriptRoot\xmbtrace.ts" diff "$trace\rpcs3.norm" "$trace\xmbrt.norm" $Thread
