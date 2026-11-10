#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
clang++ -std=c++20 -O2 analysis/fixture/package_attach.cpp \
  -Iupstream/sa2-nx/app/thirdparty/reblue-XenosRecomp/thirdparty/zstd/lib \
  -Wl,-l:libzstd.so.1 -o build/renut-switch/package_attach_fixture
build/renut-switch/package_attach_fixture
