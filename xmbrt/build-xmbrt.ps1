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
$runtime = @()
foreach ($f in Get-ChildItem $src -Filter *.cpp) {
    $obj = "$Out\$($f.BaseName).obj"
    $runtime += $obj
    cmd /c "call `"$vcvars`" >nul && `"$clang\clang-cl.exe`" $flags `"$($f.FullName)`" /Fo:`"$obj`""
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
