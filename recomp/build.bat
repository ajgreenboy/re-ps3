@echo off
rem Builds the recompiler tools with the MSVC toolchain from VS 2022 Build Tools.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%~dp0..\build" mkdir "%~dp0..\build"
cd /d "%~dp0..\build"
cl /nologo /O2 /W4 /wd4996 /std:c11 /Fe:xmbld.exe "%~dp0xmbld.c" "%~dp0elf_be.c" || exit /b 1
