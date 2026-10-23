$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot
$bun=Join-Path $env:USERPROFILE '.bun\bin\bun.exe'
$modules=@(Get-ChildItem boot\*.prx)+@(Get-ChildItem boot_extra\*.prx)
function Prepare-Image {
 $stable=$false
for($iteration=1;$iteration -le 20;++$iteration){
    & .\build\xmbld.exe --functions rea/functions_patched out/boot493 fw493/vsh_module/vsh.elf @($modules.FullName) *> out\xmbld.log
    if($LASTEXITCODE){throw 'image link failed'}
    & $bun xenon\gen-opd-functions.ts out\boot493 rea\functions_patched > work\component-descriptors.log
    if($LASTEXITCODE){throw 'descriptor discovery failed'}
    $descriptorSummary=Get-Content work\component-descriptors.log | Select-Object -Last 1
    & $bun xenon\scan-branch-targets.ts out\boot493 rea\functions_patched work\component-branches.txt
    if($LASTEXITCODE){throw 'branch scan failed'}
    $targets=@(Get-Content work\component-branches.txt | Where-Object {$_})
    if($targets.Count){
        & $bun xenon\gen-opd-functions.ts out\boot493 rea\functions_patched work\component-branches.txt > work\component-opd.log
        if($LASTEXITCODE){throw 'function extension failed'}
    }
    & $bun xenon\gen-switch-tables.ts out\boot493 rea\functions_patched xenon\xmb_switch_tables.toml rea\functions_patched > work\switch-scan.log
    if($LASTEXITCODE){throw 'switch scan failed'}
    $scan=Get-Content work\switch-scan.log -TotalCount 1
    Write-Output "iteration ${iteration}: $scan"
    if(!$targets.Count -and $scan -match 'functions to extend: 0' -and $descriptorSummary -eq 'added 0 functions'){$stable=$true;break}
}
if(!$stable){throw 'function discovery did not converge'}
}
$translator=Join-Path (Split-Path -Parent $projectRoot) 'deps\XenonRecomp\build-clang3\XenonRecomp\XenonRecomp.exe'
$context=Join-Path (Split-Path -Parent $projectRoot) 'deps\XenonRecomp\XenonUtils\ppc_context.h'
for($pass=1;$pass -le 8;++$pass){
    Prepare-Image
    Push-Location xenon
    try{& $translator xmb_boot.toml $context *> ..\out\xenonrecomp.log;if($LASTEXITCODE){throw 'translation failed'}}finally{Pop-Location}
    & $bun xenon\scan-switch-targets.ts out\recomp_boot493 out\xenonrecomp.log work\component-switch-targets.txt
    if($LASTEXITCODE){throw 'switch target scan failed'}
    if(!(Get-Content work\component-switch-targets.txt | Where-Object {$_})){break}
    if($pass -eq 8){throw 'switch entries did not converge'}
    & $bun xenon\gen-opd-functions.ts out\boot493 rea\functions_patched work\component-switch-targets.txt
    if($LASTEXITCODE){throw 'switch entry extension failed'}
}
Push-Location xenon
try{
    & .\compile-recomp.ps1 *> ..\out\compile.log
    if(!(Select-String -Path ..\out\compile.log -SimpleMatch 'ninja exit 0')){throw 'generated compile failed; see compile.log'}
}finally{Pop-Location}
& .\xmbrt\build-xmbrt.ps1 *> out\build-xmbrt.log
if($LASTEXITCODE){throw 'runtime build failed'}
Write-Output 'component rebuild complete'
