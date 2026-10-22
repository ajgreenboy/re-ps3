@echo off
rem Release build of RPCS3 main following BUILDING.md (sln solution) and .github/workflows/rpcs3.yml.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set QTDIR=C:\Qt\6.12.0\msvc2022_64
set VULKAN_SDK=C:\VulkanSDK\1.4.341.1
cd /d C:\dev\rpcs3
msbuild rpcs3.sln /t:rpcs3 /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /m /v:minimal /nologo > C:\dev\rpcs3\build-release.log 2>&1
set RC=%ERRORLEVEL%
echo msbuild exit code %RC% >> C:\dev\rpcs3\build-release.log
exit /b %RC%
