@echo off
rem Builds RPCS3's pinned LLVM with the local VS 2022 toolset, as in .github/workflows/llvm.yml.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d C:\dev\rpcs3
msbuild 3rdparty\llvm\llvm_build.vcxproj /p:SolutionDir="C:\dev\rpcs3\\" /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal /nologo > C:\dev\rpcs3\build-llvm.log 2>&1
set RC=%ERRORLEVEL%
echo msbuild exit code %RC% >> C:\dev\rpcs3\build-llvm.log
exit /b %RC%
