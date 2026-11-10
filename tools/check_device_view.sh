#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
mkdir -p build/device-view
clang++ -std=c++23 -O1 -g -DTIMESHIFT_RECOMP -DFMT_HEADER_ONLY \
  -Iadapter -Ianalysis/device-fixture -Iprivate/app/generated/default \
  -Iprivate/sdk/include -Iupstream/sa2-nx/app/src \
  -I/root/lab/rexglue-sdk/thirdparty/simde \
  -I/root/lab/rexglue-sdk/thirdparty/fmt/include \
  -I/root/lab/rexglue-sdk/thirdparty/spdlog/include \
  -I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap \
  tests/device_view_fixture.cpp -o build/device-view/device_view_fixture
build/device-view/device_view_fixture | tee analysis/device-view-validation.txt
# Check the same real SDK + ReNut structure with the console compiler too.
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ -std=c++23 -O2 \
  -D__SWITCH__ -DTIMESHIFT_RECOMP -Iadapter -Iprivate/sdk/include \
  -Iupstream/sa2-nx/app/src -x c++ -c -o build/device-view/layout-aarch64.o - <<'CPP'
#include "renut_engine/gpu/d3d/d3d.h"
static_assert(sizeof(renut::gpu::d3d::Device) == 22400);
static_assert(renut::gpu::d3d::kDeviceOffsetVertexDeclaration == 11812);
static_assert(renut::gpu::d3d::kDeviceOffsetViewport == 12640);
CPP
