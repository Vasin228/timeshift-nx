#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
mkdir -p build/draw-hooks
clang++ -std=c++23 -O1 -g -DFMT_HEADER_ONLY \
  -Iadapter -Ianalysis/draw-fixture -Iprivate/app/generated/default -Iprivate/sdk/include \
  -I/root/lab/rexglue-sdk/thirdparty/simde -I/root/lab/rexglue-sdk/thirdparty/fmt/include \
  -I/root/lab/rexglue-sdk/thirdparty/spdlog/include -I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap \
  tests/draw_hooks_fixture.cpp adapter/draw_hooks.cpp -o build/draw-hooks/draw_hooks_fixture
build/draw-hooks/draw_hooks_fixture | tee analysis/draw-hooks-validation.txt
