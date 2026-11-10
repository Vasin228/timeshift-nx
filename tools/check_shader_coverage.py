from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
GLSL_DIR = ROOT / 'analysis/deko-combined/glsl'
CACHE_BIN = Path('d:/Anti/deko3d_shaders_f0.bin')

def fnv1a64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

# 1. Read existing cache records
existing_records = {} # key -> bytes(dksh)
if CACHE_BIN.exists():
    bin_data = CACHE_BIN.read_bytes()
    pos = 24
    while pos + 12 <= len(bin_data):
        key, sz = struct.unpack('<QI', bin_data[pos:pos+12])
        pos += 12
        dksh = bin_data[pos:pos+sz]
        existing_records[key] = dksh
        pos += sz
    print(f"Loaded {len(existing_records)} existing records from {CACHE_BIN.name}")

# 2. Collect pre-existing dksh files across analysis directories
dksh_pool = {} # stem_upper -> path
for d in [ROOT / 'analysis/deko-all/dksh',
          ROOT / 'analysis/temp-missing-deko/dksh',
          ROOT / 'analysis/deko-captured/dksh',
          ROOT / 'analysis/deko-probe/dksh']:
    if d.exists():
        for p in d.glob('*.dksh'):
            dksh_pool[p.stem.upper()] = p
print(f"Collected {len(dksh_pool)} precompiled DKSH files from analysis directories")

# 3. Check coverage for all 3,787 shaders
glsl_files = sorted(GLSL_DIR.glob('*.glsl'))
print(f"Total GLSL shaders to cache: {len(glsl_files)}")

needs_compile = []
final_records = dict(existing_records) # key -> dksh

for p in glsl_files:
    text = p.read_text(encoding='utf-8').replace('\r\n', '\n')
    key = fnv1a64(text.encode('utf-8'))
    if key in final_records:
        continue
    # Check if dksh file exists
    stem = p.stem.upper()
    if stem in dksh_pool:
        dksh_bytes = dksh_pool[stem].read_bytes()
        if dksh_bytes[:4] == b'DKSH':
            final_records[key] = dksh_bytes
            continue
    needs_compile.append((p, key))

print(f"Resolved from existing/pool: {len(final_records)} shaders")
print(f"Shaders needing compilation via uam: {len(needs_compile)}")
