#!/bin/sh
# Full pipeline: relink image, iterate jump-table extension, translate, compile, build runtime.
# usage (git bash, from anywhere): sh /c/dev/xmb/rebuild.sh
set -e
cd /c/dev/xmb
BUN=~/.bun/bin/bun.exe
link() { ./build/xmbld.exe --functions rea/functions_patched out/boot493 fw493/vsh_module/vsh.elf boot/*.prx boot_extra/*.prx > out/xmbld.log; }
link
for i in 1 2 3 4 5 6 7 8; do
  out=$(cd xenon && $BUN gen-switch-tables.ts ../out/boot493 ../rea/functions_patched xmb_switch_tables.toml ../rea/functions_patched)
  echo "$out" | tail -2
  echo "$out" | grep -q "functions to extend: 0" && break
  link
done
(cd xenon && /c/dev/deps/XenonRecomp/build-clang3/XenonRecomp/XenonRecomp.exe xmb_boot.toml /c/dev/deps/XenonRecomp/XenonUtils/ppc_context.h > ../out/xenonrecomp.log 2>&1) || { tail -20 out/xenonrecomp.log; exit 1; }
grep -c "ERROR" out/xenonrecomp.log || true
powershell -NoProfile -ExecutionPolicy Bypass -File xenon/compile-recomp.ps1 > out/compile.log 2>&1 || { tail -30 out/compile.log; exit 1; }
powershell -NoProfile -ExecutionPolicy Bypass -File xmbrt/build-xmbrt.ps1 > out/build-xmbrt.log 2>&1 || { tail -30 out/build-xmbrt.log; exit 1; }
echo rebuild ok
