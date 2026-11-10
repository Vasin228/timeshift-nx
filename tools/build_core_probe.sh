#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
export DEVKITPRO=/opt/devkitpro
cmake -G Ninja -S integration -B build/core-switch \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release
if ! cmake --build build/core-switch --target timeshift_renut_core timeshift_renut_resource_hooks \
     timeshift_renut_device_calls timeshift_renut_draw_hooks timeshift_renut_tiling timeshift_renut_sources \
     timeshift_renut_lifecycle timeshift_renut_sdk_tiling timeshift_renut_fence_query \
     timeshift_renut_fence_wait timeshift_renut_guest_wait timeshift_renut_device_cleanup \
     timeshift_renut_callback_hooks timeshift_renut_presentation_hooks plume \
     ts_renut_zstd ts_renut_smolv ts_renut_xxhash -j4 > analysis/core-switch-build.log 2>&1; then
  tail -n 90 analysis/core-switch-build.log
  exit 1
fi
tail -n 6 analysis/core-switch-build.log
