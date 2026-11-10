#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
out=analysis/host-geometry
mkdir -p "$out/spirv"
export LD_LIBRARY_PATH="$PWD/build/deps/dxc-1.8.2505.1/lib"
for shader in point_sprite rectangle_list; do
  build/deps/dxc-1.8.2505.1/bin/dxc -T gs_6_0 -E main -HV 2021 -spirv -fvk-use-dx-layout \
    -DBANJO_DEKO3D -DTIMESHIFT_RECOMP \
    -Fo "$out/spirv/gs_${shader}.spv" \
    "upstream/sa2-nx/app/src/renut_engine/gpu/shaders/host/${shader}_gs.hlsl"
done
python3 tools/check_deko_probe.py --input "$out/spirv" --out "$out/deko"
