param([string]$QtRoot=$env:QTDIR,[string]$OutputDirectory,[string]$SteamworksHeaders=$env:STEAMWORKS_HEADERS,[string]$SourceRevision=$env:XMB_SOURCE_REVISION)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-metadata.ps1')
$sourceFingerprint=Get-NativeSourceFingerprint -ProjectRoot $root
if(-not $QtRoot) {
    $qtBase=Join-Path ([IO.Path]::GetPathRoot($root)) 'Qt'
    $kits=@(Get-ChildItem $qtBase -Directory | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'msvc2022_64'} | Where-Object {Test-Path (Join-Path $_ 'lib\cmake\Qt6\Qt6Config.cmake')})
    if(-not $kits.Count){throw 'Pass -QtRoot pointing to a Qt 6.8+ MSVC kit.'}
    $QtRoot=$kits[0]
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(-not $vs){throw 'Visual Studio C++ Build Tools are required.'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$output=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $root 'out\integration'}
$configure='"'+$cmake+'" -S "'+$PSScriptRoot+'" -B "'+$output+'" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="'+$QtRoot+'" -DCMAKE_MAKE_PROGRAM="'+$ninja+'"'
if($SteamworksHeaders){$configure+=' -DXMB_STEAMWORKS_HEADERS="'+[IO.Path]::GetFullPath($SteamworksHeaders)+'"'}
cmd /d /c ('call "'+$vcvars+'" >nul && '+$configure)
if($LASTEXITCODE){throw 'Native CMake configuration failed.'}
cmd /d /c ('call "'+$vcvars+'" >nul && "'+$cmake+'" --build "'+$output+'" --parallel')
if($LASTEXITCODE){throw 'Native C++ build failed.'}
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-compiler-runtime (Join-Path $output 're-ps3-native.exe')
if($LASTEXITCODE){throw 'Qt runtime deployment failed.'}
if((Get-NativeSourceFingerprint -ProjectRoot $root) -ne $sourceFingerprint){throw 'Sources changed during the build. Rebuild before using this output.'}
if(-not $SourceRevision) {
    try { $SourceRevision=(& git -C $root rev-parse HEAD 2>$null | Select-Object -First 1) } catch { $SourceRevision=$null }
}
$exe=Join-Path $output 're-ps3-native.exe'
$metadata=[ordered]@{
    schema='xmb-build/1'; sourceRevision=$SourceRevision; sourceFingerprint=$sourceFingerprint
    executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    builtAtUtc=[DateTime]::UtcNow.ToString('o'); configuration='Release'
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build-info.json') -Encoding UTF8
Write-Host ('Built '+(Join-Path $output 're-ps3-native.exe'))
