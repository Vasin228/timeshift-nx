#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
mkdir -p build/shader-hooks
clang++ -std=c++23 -O1 -g -DFMT_HEADER_ONLY \
  -Iadapter -Ianalysis/shader-fixture -Iprivate/app/generated/default -Iprivate/sdk/include \
  -I/root/lab/rexglue-sdk/thirdparty/simde -I/root/lab/rexglue-sdk/thirdparty/fmt/include \
  -I/root/lab/rexglue-sdk/thirdparty/spdlog/include -I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap \
  tests/shader_hooks_fixture.cpp adapter/shader_capture.cpp adapter/shader_resource_hooks.cpp \
  -o build/shader-hooks/shader_hooks_fixture
build/shader-hooks/shader_hooks_fixture | tee analysis/shader-hooks-validation.txt
