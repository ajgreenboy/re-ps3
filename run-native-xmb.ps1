param([string]$FlashRoot,[string]$VshModules=$env:XMB_VSH_MODULES,[string]$OutputDirectory,[string]$DataDirectory,[switch]$Build)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'native-xmb\build-metadata.ps1')
if(-not $FlashRoot){$FlashRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'rpcs3\bin\dev_flash'}
if(-not (Test-Path (Join-Path $FlashRoot 'vsh\resource\explore_plugin_full.rco'))){throw 'Pass -FlashRoot pointing to your local PS3 dev_flash directory.'}
if(-not $VshModules) {
    $candidate=Join-Path $PSScriptRoot 'fw493\vsh_module'
    if(Test-Path (Join-Path $candidate 'custom_render_plugin.prx')){$VshModules=$candidate}
}
if($VshModules -and -not (Test-Path (Join-Path $VshModules 'custom_render_plugin.prx'))){throw 'Pass -VshModules pointing to the local decrypted VSH modules (custom_render_plugin.prx is required).'}
if(-not $VshModules){Write-Warning 'Decrypted VSH modules are absent; the ribbon will use its approximate fallback rest state.'}
$output=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $PSScriptRoot 'out\integration'}
$exe=Join-Path $output 're-ps3-native.exe'
$infoPath=Join-Path $output 'build-info.json'
$current=$false
if((Test-Path $exe) -and (Test-Path $infoPath)) {
    try {
        $info=Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
        $current=$info.sourceFingerprint -eq (Get-NativeSourceFingerprint -ProjectRoot $PSScriptRoot) -and
                 $info.executableSha256 -eq (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    } catch { $current=$false }
}
if($Build -or -not $current) {
    Write-Host 'Building the current native XMB sources...'
    & (Join-Path $PSScriptRoot 'native-xmb\build.ps1') -OutputDirectory $output
}
$session=Join-Path $output ('sessions\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
# Keep existing user data when the executable moves to integration/.
$data=if($DataDirectory){[IO.Path]::GetFullPath($DataDirectory)}else{Join-Path $PSScriptRoot 'out\native-xmb\data'}
$arguments=@('--dev-flash',('"'+$FlashRoot+'"'),'--data-dir',('"'+$data+'"'))
if($VshModules){$arguments+=@('--vsh-modules',('"'+$VshModules+'"'))}
Copy-Item -LiteralPath $infoPath -Destination (Join-Path $session 'build-info.json')
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $output -PassThru -WindowStyle Normal -RedirectStandardError (Join-Path $session 'native.stderr.txt') -RedirectStandardOutput (Join-Path $session 'native.stdout.txt')
Write-Host "Native XMB running. Logs: $session"
$process.WaitForExit()
if($process.ExitCode){throw "Native XMB exited with code $($process.ExitCode). Logs: $session"}
