param([string]$Src = 'C:\dev\xmb\out\recomp_boot493', [string]$Obj = 'C:\dev\xmb\out\recomp_obj')
# Compiles XenonRecomp's generated C++ with the locally built clang-cl, via a generated build.ninja.
New-Item -ItemType Directory -Force $Obj | Out-Null
# XenonRecomp writes 256 functions per ppc_recomp.N.cpp and only rewrites files that changed, so files left
# from a run with more functions would define functions twice. Drop them (and their objects).
$functions = @(Select-String -Path "$Src\ppc_func_mapping.cpp" -Pattern '\{ 0x').Count
$files = [math]::Ceiling($functions / 256)
foreach ($f in Get-ChildItem $Src -Filter 'ppc_recomp.*.cpp') {
    if ([int]($f.BaseName -replace 'ppc_recomp\.', '') -ge $files) {
        Remove-Item $f.FullName
        Remove-Item "$Obj\$($f.BaseName).obj" -ErrorAction SilentlyContinue
        "removed stale $($f.Name)"
    }
}
$flags = "/nologo /c /std:c++20 /O2 /arch:AVX /EHsc /w /DPPC_FUNC_TABLE_FROM_RUNTIME /I $Src /I C:\dev\deps\XenonRecomp\XenonUtils /I C:\dev\deps\XenonRecomp\thirdparty\simde"
$lines = @(
    "rule cxx",
    "  command = C:\dev\deps\clang\bin\clang-cl.exe $flags `$in /Fo:`$out",
    "  description = $([char]36)in"
)
# ninja paths escape ':' as '$:' (drive letters)
function NinjaPath($p) { $p -replace ':', '$:' }
foreach ($f in Get-ChildItem $Src -Filter *.cpp) {
    $lines += "build $(NinjaPath "$Obj\$($f.BaseName).obj"): cxx $(NinjaPath $f.FullName) | $(NinjaPath "$Src\ppc_config.h") $(NinjaPath "$Src\ppc_context.h") $(NinjaPath "$Src\ppc_recomp_shared.h")"
}
Set-Content "$Obj\build.ninja" $lines -Encoding ascii
$ninja = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
cmd /c "call `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" >nul && `"$ninja`" -C $Obj -k 0 -j 6"
"ninja exit $LASTEXITCODE"
"objects: $(@(Get-ChildItem $Obj -Filter *.obj).Count) of $(@(Get-ChildItem $Src -Filter *.cpp).Count)"
