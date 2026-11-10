#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
mkdir -p build/tiling
clang++ -std=c++23 -O1 -g -DFMT_HEADER_ONLY \
  -Iadapter -Ianalysis/tiling-fixture -Iprivate/app/generated/default -Iprivate/sdk/include \
  -I/root/lab/rexglue-sdk/thirdparty/simde -I/root/lab/rexglue-sdk/thirdparty/fmt/include \
  -I/root/lab/rexglue-sdk/thirdparty/spdlog/include -I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap \
  tests/tiling_fixture.cpp adapter/tiling.cpp adapter/tiling_guest_calls.cpp -o build/tiling/tiling_fixture
build/tiling/tiling_fixture | tee analysis/tiling-validation.txt
