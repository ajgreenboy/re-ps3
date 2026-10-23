param(
    [string]$Gen = 'C:\dev\xmb\out\recomp_boot493',
    [string]$RecompObj = 'C:\dev\xmb\out\recomp_obj',
    [string]$Out = 'C:\dev\xmb\out\xmbrt'
)
# Builds xmbrt.exe: the runtime sources here plus the 224 XenonRecomp objects, linked with lld-link.
# The runtime compiles against the generated ppc_context.h with the same flags as compile-recomp.ps1,
# so after any ppc_context.h change: regenerate, run compile-recomp.ps1, then this.
$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$clang = 'C:\dev\deps\clang\bin'
New-Item -ItemType Directory -Force $Out | Out-Null

$recomp = @(Get-ChildItem $RecompObj -Filter *.obj)
$genCpp = @(Get-ChildItem $Gen -Filter *.cpp)
if ($recomp.Count -ne $genCpp.Count) { throw "recomp objects: $($recomp.Count) of $($genCpp.Count); run compile-recomp.ps1 first" }

$flags = "/nologo /c /std:c++20 /O2 /arch:AVX /EHsc /W3 -Wno-unused-function /D_CRT_SECURE_NO_WARNINGS /DPPC_FUNC_TABLE_FROM_RUNTIME /I $Gen /I C:\dev\deps\XenonRecomp\XenonUtils /I C:\dev\deps\XenonRecomp\thirdparty\simde /I C:\dev\rpcs3\3rdparty /I C:\dev\rpcs3\3rdparty\GL"
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$exportsFile=Join-Path (Split-Path -Parent $Gen) 'boot493\exports.csv'
$signalExport=Import-Csv -LiteralPath $exportsFile | Where-Object {$_.library -eq 'cellSpurs' -and $_.nid -eq '0xE0A6DBE4'} | Select-Object -First 1
$signalFlags=''
if($signalExport){
    $entrySuffix=([uint32]$signalExport.code).ToString('X')
    $signalFlags=" /DXMBRT_SPURS_SIGNAL_ENTRY=sub_$entrySuffix /DXMBRT_SPURS_SIGNAL_IMPL=__imp__sub_$entrySuffix"
}
$runtime = @()
foreach ($f in Get-ChildItem $src -Filter *.cpp) {
    $obj = "$Out\$($f.BaseName).obj"
    $runtime += $obj
    $fileFlags=if($f.BaseName -eq 'spurs_trace'){$flags+$signalFlags}else{$flags}
    cmd /c "call `"$vcvars`" >nul && `"$clang\clang-cl.exe`" $fileFlags `"$($f.FullName)`" /Fo:`"$obj`""
    if ($LASTEXITCODE) { throw "compile failed: $($f.Name)" }
}

# Response file: 224 object paths are too long for one command line.
$rsp = "$Out\link.rsp"
$lines = @('/nologo', '/subsystem:console', '/stack:0x4000000', "/out:$Out\xmbrt.exe", "/pdb:$Out\xmbrt.pdb", "/map:$Out\xmbrt.map", '/debug', 'kernel32.lib', 'setupapi.lib', 'hid.lib', 'xinput.lib', 'opengl32.lib', 'gdi32.lib', 'user32.lib')
$lines += $runtime | ForEach-Object { "`"$_`"" }
$lines += $recomp | ForEach-Object { "`"$($_.FullName)`"" }
Set-Content $rsp $lines -Encoding ascii
cmd /c "call `"$vcvars`" >nul && `"$clang\lld-link.exe`" @$rsp"
if ($LASTEXITCODE) { throw "link failed" }
"built $Out\xmbrt.exe ($($runtime.Count) runtime + $($recomp.Count) recomp objects)"
