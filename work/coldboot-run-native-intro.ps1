# Runs the native XMB with a scratch data dir and saves intro frames at the given intro times (XMB_INTRO_GRAB), then
# stops it. Usage: coldboot-run-native-intro.ps1 -Out <dir> -Times 0.5,1.0,... [-Seconds 11] [-FlashRoot <dev_flash>]
param([string]$Out,[string]$Times,[int]$Seconds=11,[string]$FlashRoot='C:\dev\rpcs3\bin\dev_flash',[string]$DataDir)
if(-not $DataDir){$DataDir=Join-Path $Out 'data'}
Remove-Item -Recurse -Force (Join-Path $Out 'intro_*.png') -ErrorAction SilentlyContinue
$env:XMB_INTRO_GRAB="$Out@$Times"; $env:XMB_LOG=(Join-Path $Out 'native.log')
$exe=Join-Path (Split-Path -Parent $PSScriptRoot) 'out\native-xmb\re-ps3-native.exe'
$p=Start-Process -FilePath $exe -ArgumentList @('--dev-flash',$FlashRoot,'--data-dir',$DataDir) -WorkingDirectory (Split-Path $exe) -PassThru
Start-Sleep $Seconds; Stop-Process -Id $p.Id -Force
Get-ChildItem $Out -Filter 'intro_*.png' | ForEach-Object Name
