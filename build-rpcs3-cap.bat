@echo off
rem RPCS3 Release build with the capture patch (rpcs3-rsx-capture.patch) into C:\dev\rpcs3-cap\, a separate tree, so a
rem running bin\rpcs3.exe is untouched. The rpcs3 project links build\lib\Release-x64\emucore.lib rather than the OutDir
rem copy, so the fresh emucore.lib is swapped in for the link and the original put back afterwards.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set QTDIR=C:\Qt\6.12.0\msvc2022_64
set VULKAN_SDK=C:\VulkanSDK\1.4.341.1
set LIB_DIR=C:\dev\rpcs3\build\lib\Release-x64
set MSB=msbuild rpcs3.sln /t:rpcs3 /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /p:OutDir=C:\dev\rpcs3-cap\ /m /v:minimal /nologo
cd /d C:\dev\rpcs3
%MSB% > C:\dev\rpcs3-cap-build.log 2>&1 || exit /b 1
copy /y "%LIB_DIR%\emucore.lib" "%LIB_DIR%\emucore.lib.orig" >nul
copy /y C:\dev\rpcs3-cap\emucore.lib "%LIB_DIR%\emucore.lib" >nul
del C:\dev\rpcs3-cap\rpcs3.exe
%MSB% >> C:\dev\rpcs3-cap-build.log 2>&1
set RC=%ERRORLEVEL%
move /y "%LIB_DIR%\emucore.lib.orig" "%LIB_DIR%\emucore.lib" >nul
echo msbuild exit code %RC% >> C:\dev\rpcs3-cap-build.log
exit /b %RC%
