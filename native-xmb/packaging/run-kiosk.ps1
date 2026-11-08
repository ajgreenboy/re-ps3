param(
    [string]$FlashRoot,
    [string]$DataDirectory,
    [string]$VshModules,
    [int]$Display = 1,
    [switch]$NoIntro
)
$ErrorActionPreference = 'Stop'
# The packaged folder is this script's own folder: the launcher, the exe and the Qt runtime live together.
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $root 're-ps3-native.exe'
if(-not (Test-Path $exe)) { throw "re-ps3-native.exe not found next to this launcher (expected in $root)." }

# Optional config next to the launcher: {"devFlash":"...","dataDir":"...","vshModules":"..."}
$config = $null
$configPath = Join-Path $root 'xmb-config.json'
if(Test-Path $configPath) { $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json }

if(-not $FlashRoot) { if($config.devFlash){ $FlashRoot = $config.devFlash } }
if(-not $FlashRoot) { $FlashRoot = Join-Path (Split-Path -Parent $root) 'rpcs3\bin\dev_flash' }
if(-not (Test-Path (Join-Path $FlashRoot 'vsh\resource\explore_plugin_full.rco'))) {
    throw "No firmware at '$FlashRoot'. Pass -FlashRoot, or write xmb-config.json next to this launcher."
}
if(-not $DataDirectory) { if($config.dataDir){ $DataDirectory = $config.dataDir } }
if(-not $DataDirectory) { $DataDirectory = Join-Path $root 'data' }
if(-not $VshModules) { if($config.vshModules){ $VshModules = $config.vshModules } }
if($VshModules -and -not (Test-Path (Join-Path $VshModules 'custom_render_plugin.prx'))) {
    Write-Warning "Decrypted VSH modules at '$VshModules' lack custom_render_plugin.prx; the ribbon uses its approximate fallback."
}

# Quote paths for Start-Process (it joins the list with spaces and does not quote).
$arguments = @('--dev-flash', ('"'+$FlashRoot+'"'), '--data-dir', ('"'+$DataDirectory+'"'), '--kiosk', '--display', $Display)
if($VshModules -and (Test-Path (Join-Path $VshModules 'custom_render_plugin.prx'))) {
    $arguments += @('--vsh-modules', ('"'+$VshModules+'"'))
}
if($NoIntro) { $arguments += '--no-intro' }

$process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -PassThru
$process.WaitForExit()
exit $process.ExitCode
