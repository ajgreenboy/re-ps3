#!/bin/sh
# Adds every branch target XenonRecomp could not resolve ("// ERROR <addr>") as a function entry and rebuilds,
# until a translation reports none. usage: sh /c/dev/xmb/iterate-errors.sh [max iterations]
cd /c/dev/xmb
BUN=~/.bun/bin/bun.exe
for i in $(seq 1 ${1:-6}); do
  grep -ho "// ERROR [0-9A-Fa-fx]*" out/recomp_boot493/*.cpp | awk '{print $3}' | sed 's/^0x//' | sort -u > out/error_targets.txt
  n=$(wc -l < out/error_targets.txt)
  echo "iteration $i: $n unresolved targets"
  [ "$n" -eq 0 ] && break
  (cd xenon && $BUN gen-opd-functions.ts ../out/boot493 ../rea/functions_patched ../out/error_targets.txt | tail -1)
  sh rebuild.sh | tail -2
done
