#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
out=analysis/translation-all
mkdir -p "$out/hlsl" "$out/spirv"
unset BANJO_HLSL_OVERRIDE BANJO_HLSL_DUMP_ONLY
export BANJO_DEKO3D_SPIRV_DIR="$PWD/$out/spirv"
/usr/bin/time -o "$out/timing.txt" -f 'elapsed_seconds=%e peak_kib=%M exit=%x' \
  stdbuf -oL -eL build/shader-tools/xenos-recomp/XenosRecomp/XenosRecomp \
  analysis/shader-dump/containers "$out/shader_cache.cpp" \
  upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/XenosRecomp/shader_common.h \
  "$out/hlsl" > "$out/translator.log" 2>&1
