#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
cmake -S . -B build/shader-tools -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DTIMESHIFT_BUILD_SHADER_TRANSLATOR=ON
cmake --build build/shader-tools --target XenosRecomp -j2
