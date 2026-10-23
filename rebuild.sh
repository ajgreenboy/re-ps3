#!/bin/sh
# Full pipeline: relink image, iterate jump-table extension, translate, compile, build runtime.
# usage (git bash, from anywhere): sh /c/dev/xmb/rebuild.sh
set -e
cd "$(dirname "$0")"
BUN=~/.bun/bin/bun.exe
XENON_RECOMP=$(cd ../deps/XenonRecomp && pwd)
mkdir -p work
link() { ./build/xmbld.exe --functions rea/functions_patched out/boot493 fw493/vsh_module/vsh.elf boot/*.prx boot_extra/*.prx > out/xmbld.log; }
discover() {
  discovery_stable=no
  for round in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    link
    $BUN xenon/scan-branch-targets.ts out/boot493 rea/functions_patched work/component-branches.txt
    if test -s work/component-branches.txt; then
      $BUN xenon/gen-opd-functions.ts out/boot493 rea/functions_patched work/component-branches.txt
    fi
    $BUN xenon/gen-switch-tables.ts out/boot493 rea/functions_patched xenon/xmb_switch_tables.toml rea/functions_patched > work/switch-scan.log
    head -1 work/switch-scan.log
    if ! test -s work/component-branches.txt && grep -q 'functions to extend: 0' work/switch-scan.log; then
      discovery_stable=yes
      break
    fi
  done
  test "$discovery_stable" = yes || { echo 'function discovery did not converge'; exit 1; }
}
link
$BUN xenon/gen-opd-functions.ts out/boot493 rea/functions_patched
for pass in 1 2 3 4 5 6 7 8; do
  discover
  (cd xenon && "$XENON_RECOMP/build-clang3/XenonRecomp/XenonRecomp.exe" xmb_boot.toml "$XENON_RECOMP/XenonUtils/ppc_context.h" > ../out/xenonrecomp.log 2>&1) || { tail -20 out/xenonrecomp.log; exit 1; }
  $BUN xenon/scan-switch-targets.ts out/recomp_boot493 out/xenonrecomp.log work/component-switch-targets.txt
  if ! test -s work/component-switch-targets.txt; then break; fi
  test "$pass" != 8 || { echo 'switch entries did not converge'; exit 1; }
  $BUN xenon/gen-opd-functions.ts out/boot493 rea/functions_patched work/component-switch-targets.txt
done
grep -c "ERROR" out/xenonrecomp.log || true
powershell -NoProfile -ExecutionPolicy Bypass -File xenon/compile-recomp.ps1 > out/compile.log 2>&1 || { tail -30 out/compile.log; exit 1; }
powershell -NoProfile -ExecutionPolicy Bypass -File xmbrt/build-xmbrt.ps1 > out/build-xmbrt.log 2>&1 || { tail -30 out/build-xmbrt.log; exit 1; }
echo rebuild ok
