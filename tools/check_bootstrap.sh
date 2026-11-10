#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
cmake -S . -B build/cpu-bootstrap -DCMAKE_CXX_COMPILER=clang++ -DTIMESHIFT_BUILD_PRIVATE_FIXTURE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cpu-bootstrap -j2
ctest --test-dir build/cpu-bootstrap --output-on-failure
