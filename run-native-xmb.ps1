param([string]$FlashRoot,[switch]$Build)
$ErrorActionPreference='Stop'
if($Build){& (Join-Path $PSScriptRoot 'native-xmb\build.ps1')}
if(-not $FlashRoot){$FlashRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'rpcs3\bin\dev_flash'}
if(-not (Test-Path (Join-Path $FlashRoot 'vsh\resource\explore_plugin_full.rco'))){throw 'Pass -FlashRoot pointing to your local PS3 dev_flash directory.'}
$output=Join-Path $PSScriptRoot 'out\native-xmb'
$exe=Join-Path $output 're-ps3-native.exe'
if(-not (Test-Path $exe)){throw 'Build native-xmb first, or run with -Build.'}
$session=Join-Path $output ('sessions\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$data=Join-Path $output 'data'
$process=Start-Process -FilePath $exe -ArgumentList ('--dev-flash "{0}" --data-dir "{1}"' -f $FlashRoot,$data) -WorkingDirectory $output -PassThru -WindowStyle Normal -RedirectStandardError (Join-Path $session 'native.stderr.txt') -RedirectStandardOutput (Join-Path $session 'native.stdout.txt')
Write-Host "Native XMB running. Logs: $session"
$process.WaitForExit()
if($process.ExitCode){throw "Native XMB exited with code $($process.ExitCode). Logs: $session"}
