#!/bin/bash
set -euo pipefail
freeze=/mnt/d/Anti/timeshift-recomp/frozen/vulkan-part181
if [ -e "$freeze/manifest.json" ]; then
  echo "Frozen baseline already exists; refusing to overwrite" >&2
  exit 1
fi
mkdir -p "$freeze"
cd /root/lab/rexglue-sdk
git rev-parse HEAD > "$freeze/sdk-head.txt"
git status --porcelain=v1 > "$freeze/sdk-status.txt"
git diff --binary HEAD > "$freeze/sdk-tracked-changes.patch"
git submodule status --recursive > "$freeze/sdk-submodules.txt"
tar -czf "$freeze/sdk-source.tar.gz" src include cmake resources scripts tests CMakeLists.txt CMakePresets.json .gitmodules LICENSE
cp /root/lab/build_gpu.sh /root/lab/link109.sh "$freeze/"
cp /root/lab/switch-build-app/CMakeCache.txt "$freeze/switch-CMakeCache.txt"
cp /root/lab/switch-build-app/build.ninja "$freeze/switch-build.ninja"
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ --version > "$freeze/compiler-version.txt"
grep CMAKE_HOME_DIRECTORY /root/lab/switch-build-app/CMakeCache.txt
sha256sum "$freeze/sdk-source.tar.gz"
