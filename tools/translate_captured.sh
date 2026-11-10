#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
out=analysis/translation-captured
mkdir -p "$out/hlsl" "$out/spirv"
unset BANJO_HLSL_OVERRIDE BANJO_HLSL_DUMP_ONLY
export BANJO_DEKO3D_SPIRV_DIR="$PWD/$out/spirv"
build/shader-tools/xenos-recomp/XenosRecomp/XenosRecomp \
  analysis/captured_shaders "$out/shader_cache.cpp" \
  upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/XenosRecomp/shader_common.h \
  "$out/hlsl" > "$out/translator.log" 2>&1 || true

echo "Done running XenosRecomp. Checking results:"
count=$(ls -1 "$out/spirv" 2>/dev/null | wc -l || echo 0)
echo "Generated SPIR-V count: $count"
tail -n 20 "$out/translator.log" || true
