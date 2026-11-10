#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Anti/timeshift-renut
python3 tools/check_deko_probe.py \
  --input /mnt/d/Anti/timeshift-renut/analysis/translation-captured/spirv \
  --out /mnt/d/Anti/timeshift-renut/analysis/deko-captured
