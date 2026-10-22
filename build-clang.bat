@echo off
rem Builds clang + lld (X86 host only) from RPCS3's pinned llvm-project checkout, for XenonRecomp.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cmake -S C:\dev\rpcs3\3rdparty\llvm\llvm\llvm -B C:\dev\deps\clang-build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
  -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=X86 ^
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF ^
  -DCLANG_INCLUDE_TESTS=OFF -DLLVM_ENABLE_DIA_SDK=OFF -DCMAKE_INSTALL_PREFIX=C:\dev\deps\clang || exit /b 1
cmake --build C:\dev\deps\clang-build --target install-clang install-clang-resource-headers install-lld install-llvm-ar install-llvm-rc || exit /b 1
