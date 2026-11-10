#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
translator=upstream/sa2-nx/app/thirdparty/reblue-XenosRecomp
clang++ -std=c++20 -O2 tests/switch_cache_fixture.cpp build/renut-switch/shader_cache_switch.cpp \
  "$translator/thirdparty/smol-v/source/smolv.cpp" \
  -Iupstream/sa2-nx/app/src/renut_engine/gpu/shaders \
  -I"$translator/thirdparty/smol-v/source" -I"$translator/thirdparty/zstd/lib" \
  -Wl,-l:libzstd.so.1 -o build/renut-switch/switch_cache_fixture
build/renut-switch/switch_cache_fixture | tee analysis/switch-cache-validation.txt
