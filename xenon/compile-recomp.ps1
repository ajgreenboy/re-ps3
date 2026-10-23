param([string]$Src = 'C:\dev\xmb\out\recomp_boot493', [string]$Obj = 'C:\dev\xmb\out\recomp_obj')
# Compiles XenonRecomp's generated C++ with the locally built clang-cl, via a generated build.ninja.
New-Item -ItemType Directory -Force $Obj | Out-Null
$flags = "/nologo /c /std:c++20 /O2 /arch:AVX /EHsc /w /I $Src /I C:\dev\deps\XenonRecomp\XenonUtils /I C:\dev\deps\XenonRecomp\thirdparty\simde"
$lines = @(
    "rule cxx",
    "  command = C:\dev\deps\clang\bin\clang-cl.exe $flags `$in /Fo:`$out",
    "  description = $([char]36)in"
)
# ninja paths escape ':' as '$:' (drive letters)
function NinjaPath($p) { $p -replace ':', '$:' }
foreach ($f in Get-ChildItem $Src -Filter *.cpp) {
    $lines += "build $(NinjaPath "$Obj\$($f.BaseName).obj"): cxx $(NinjaPath $f.FullName)"
}
Set-Content "$Obj\build.ninja" $lines -Encoding ascii
$ninja = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
cmd /c "call `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" >nul && `"$ninja`" -C $Obj -k 0 -j 6"
"ninja exit $LASTEXITCODE"
"objects: $(@(Get-ChildItem $Obj -Filter *.obj).Count) of $(@(Get-ChildItem $Src -Filter *.cpp).Count)"
