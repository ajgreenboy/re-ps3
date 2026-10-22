@echo off
rem Builds XenonRecomp/XenonAnalyse (hedge-dev) with clang-cl (built by build-clang.bat) + Ninja.
rem MSVC lib.exe/link.exe from vcvars handle the clang-cl objects.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem forward slashes: CMake treats backslashes in -D values as escapes
set CLANG=C:/dev/deps/clang/bin
cmake -S C:/dev/deps/XenonRecomp -B C:/dev/deps/XenonRecomp/build-clang3 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=%CLANG%/clang-cl.exe -DCMAKE_CXX_COMPILER=%CLANG%/clang-cl.exe || exit /b 1
cmake --build C:/dev/deps/XenonRecomp/build-clang3 --target XenonRecomp XenonAnalyse || exit /b 1
