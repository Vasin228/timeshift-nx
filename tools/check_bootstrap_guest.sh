#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
mkdir -p build/bootstrap-guest
clang++ -std=c++23 -O1 -g -DFMT_HEADER_ONLY \
  -Iadapter -Ianalysis/bootstrap-guest-fixture -Iprivate/app/generated/default -Iprivate/sdk/include \
  -I/root/lab/rexglue-sdk/thirdparty/simde -I/root/lab/rexglue-sdk/thirdparty/fmt/include \
  -I/root/lab/rexglue-sdk/thirdparty/spdlog/include -I/root/lab/rexglue-sdk/thirdparty/o1heap/o1heap \
  tests/bootstrap_guest_fixture.cpp tests/bootstrap_recursion_sentinel.cpp \
  adapter/device_bootstrap.cpp adapter/device_guest_calls.cpp -o build/bootstrap-guest/bootstrap_guest_fixture
build/bootstrap-guest/bootstrap_guest_fixture | tee analysis/bootstrap-guest-validation.txt
