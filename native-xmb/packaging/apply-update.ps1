param(
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [Parameter(Mandatory=$true)][string]$Marker
)
$ErrorActionPreference = 'Stop'
$install = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\','/')
$markerPath = [IO.Path]::GetFullPath($Marker)
$staged = $markerPath -replace '\.pending$', ''
$recorded = [IO.File]::ReadAllText($markerPath).Trim()
if($markerPath -notmatch '\.pending$' -or
   -not $staged.StartsWith($install + '.update.', [StringComparison]::OrdinalIgnoreCase) -or
   (Split-Path -Parent $staged) -ne (Split-Path -Parent $install) -or
   $recorded -ne $install -or
   -not (Test-Path -LiteralPath (Join-Path $staged 're-ps3-native.exe') -PathType Leaf) -or
   -not (Test-Path -LiteralPath $install -PathType Container)) {
    throw 'Invalid pending update marker or staging folder.'
}
if($env:XMB_UPDATER_DRY_RUN -eq '1') {
    Write-Host "would apply staged path $staged and restart"
    exit 0
}
$old = $install + '.old'
if(Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
Move-Item -LiteralPath $install -Destination $old
try {
    Move-Item -LiteralPath $staged -Destination $install
} catch {
    Move-Item -LiteralPath $old -Destination $install
    throw
}
Remove-Item -LiteralPath $markerPath -Force
Write-Host "applied $staged; previous installation kept at $old"
