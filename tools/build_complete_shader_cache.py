"""Build complete deko3d_shaders_f0.bin containing all 3,787 compiled DKSH binaries."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
GLSL_DIR = ROOT / 'analysis/deko-combined/glsl'
CACHE_OUT = Path('d:/Anti/deko3d_shaders_f0.bin')

def fnv1a64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

# 1. Read existing records from current cache to preserve any live variants
existing_records = {} # key -> bytes(dksh)
if CACHE_OUT.exists():
    bin_data = CACHE_OUT.read_bytes()
    pos = 24
    while pos + 12 <= len(bin_data):
        key, sz = struct.unpack('<QI', bin_data[pos:pos+12])
        pos += 12
        dksh = bin_data[pos:pos+sz]
        existing_records[key] = dksh
        pos += sz
    print(f"Preserving {len(existing_records)} existing Switch-generated records")

# 2. Collect all precompiled dksh files
dksh_pool = {} # stem_upper -> path
for d in [ROOT / 'analysis/deko-all/dksh',
          ROOT / 'analysis/temp-missing-deko/dksh',
          ROOT / 'analysis/deko-captured/dksh',
          ROOT / 'analysis/deko-probe/dksh']:
    if d.exists():
        for p in d.glob('*.dksh'):
            dksh_pool[p.stem.upper()] = p
print(f"Total available DKSH files in pool: {len(dksh_pool)}")

# 3. Add all 3,787 shaders into cache
all_records = dict(existing_records)
added_from_pool = 0

for p in sorted(GLSL_DIR.glob('*.glsl')):
    text = p.read_text(encoding='utf-8').replace('\r\n', '\n')
    key = fnv1a64(text.encode('utf-8'))
    if key in all_records:
        continue # Already present
    stem = p.stem.upper()
    if stem not in dksh_pool:
        print(f"ERROR: missing dksh for {stem}", file=sys.stderr)
        sys.exit(1)
    dksh_bytes = dksh_pool[stem].read_bytes()
    assert dksh_bytes[:4] == b'DKSH', f"Corrupt DKSH for {stem}"
    all_records[key] = dksh_bytes
    added_from_pool += 1

print(f"Added {added_from_pool} new shaders from pool")
print(f"Total unique records to write: {len(all_records)}")

# 4. Write binary BDKC v1 package
# Header: 4s ('BDKC'), I (version=1), 16s ('uam110-sw8-bnx4\0')
TAG = b'uam110-sw8-bnx4\x00'
header = struct.pack('<4sI16s', b'BDKC', 1, TAG)
assert len(header) == 24

out_bytes = bytearray(header)
for key, dksh in all_records.items():
    rec_head = struct.pack('<QI', key, len(dksh))
    out_bytes += rec_head
    out_bytes += dksh

CACHE_OUT.write_bytes(out_bytes)
print(f"Successfully wrote {CACHE_OUT} ({len(out_bytes):,} bytes, {len(all_records)} shaders)")
