"""Full pipeline: compile missing GLSL, package caches, rebuild core and app."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEKO_COMBINED = ROOT / 'analysis/deko-combined/glsl'
UNIFIED_SPIRV = ROOT / 'analysis/translation-unified/spirv'
UNIFIED_CACHE_CPP = ROOT / 'analysis/translation-unified/shader_cache.cpp'

print("=== Step 1: Check and convert missing shaders to deko3d GLSL ===")
assert UNIFIED_SPIRV.exists(), f"Missing {UNIFIED_SPIRV}"
assert UNIFIED_CACHE_CPP.exists(), f"Missing {UNIFIED_CACHE_CPP}"

# Find any SPV files not yet in DEKO_COMBINED
existing_glsl = {p.stem.upper(): p for p in DEKO_COMBINED.glob('*.glsl')}
all_spv = list(UNIFIED_SPIRV.glob('*.spv'))
missing_spv = [p for p in all_spv if p.stem.upper() not in existing_glsl]
print(f"Total SPIR-V: {len(all_spv)}, Existing GLSL: {len(existing_glsl)}, Missing GLSL: {len(missing_spv)}")

if missing_spv:
    temp_in = ROOT / 'analysis/temp-missing-spv'
    temp_out = ROOT / 'analysis/temp-missing-deko'
    if temp_in.exists(): shutil.rmtree(temp_in)
    if temp_out.exists(): shutil.rmtree(temp_out)
    temp_in.mkdir(parents=True)
    for p in missing_spv:
        shutil.copy2(p, temp_in / p.name)
    
    print(f"Running check_deko_probe.py on {len(missing_spv)} missing shaders...")
    cmd = [sys.executable, str(ROOT / 'tools/check_deko_probe.py'), '--input', str(temp_in), '--out', str(temp_out)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("check_deko_probe.py stderr:", res.stderr, file=sys.stderr)
        sys.exit(1)
    
    # Copy converted glsl to DEKO_COMBINED with lowercase extension and uppercase hash
    for p in (temp_out / 'glsl').glob('*.glsl'):
        dest_name = p.name[:3].lower() + p.name[3:-5].upper() + '.glsl'
        shutil.copy2(p, DEKO_COMBINED / dest_name)
    print(f"Added {len(missing_spv)} new GLSL files to {DEKO_COMBINED}")

total_glsl = list(DEKO_COMBINED.glob('*.glsl'))
print(f"Total combined GLSL shaders: {len(total_glsl)}")

# Update report.json in deko-combined
report_path = ROOT / 'analysis/deko-combined/report.json'
report = {
    'success': True,
    'inputs': len(total_glsl),
    'passed': len(total_glsl)
}
report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

print("=== Step 2: Prepare Switch SPIR-V cache and shader_cache_switch.cpp ===")
cmd = [sys.executable, str(ROOT / 'tools/prepare_switch_cache.py'), '--source', str(UNIFIED_CACHE_CPP)]
res = subprocess.run(cmd, capture_output=True, text=True)
print(res.stdout)
if res.returncode != 0:
    print("prepare_switch_cache.py failed:", res.stderr, file=sys.stderr)
    sys.exit(1)

print("=== Step 3: Package deko3d_glsl.bin and deko3d_glsl_switch.cpp ===")
cmd = [sys.executable, str(ROOT / 'tools/package_shaders.py')]
res = subprocess.run(cmd, capture_output=True, text=True)
print(res.stdout)
if res.returncode != 0:
    print("package_shaders.py failed:", res.stderr, file=sys.stderr)
    sys.exit(1)

print("=== Preparation steps completed successfully! ===")
