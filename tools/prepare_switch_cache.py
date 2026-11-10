"""Embed the unified translation's SPIR-V bytes without GCC byte initializers.

Uses the same cache ABI as the pinned upstream renut_switch_shader_cache.py.
Only local private outputs are written. No translation is rerun here.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=None, help='Path to shader_cache.cpp')
args = parser.parse_args()

if args.source:
    source = args.source
elif (ROOT / 'analysis/translation-unified/shader_cache.cpp').exists():
    source = ROOT / 'analysis/translation-unified/shader_cache.cpp'
else:
    source = ROOT / 'analysis/translation-all/shader_cache.cpp'

print(f"Reading shader cache from: {source}")
out = ROOT / 'build/renut-switch'
out.mkdir(exist_ok=True, parents=True)
text = source.read_text(encoding='utf-8')
entry_count = int(re.search(r'g_shaderCacheEntryCount\s*=\s*(\d+)', text)[1])
print(f"Discovered {entry_count} shader cache entries")
assert entry_count >= 2720, f"Expected at least 2720 entries, found {entry_count}"

keys = re.findall(r'^\s*\{ 0x([0-9A-Fa-f]+),', text.split('};', 1)[0], re.M)
assert len(keys) == entry_count and len(set(keys)) == entry_count
assert [int(k, 16) for k in keys] == sorted(int(k, 16) for k in keys)

for stage in ('Dxil', 'Spirv'):
    match = re.search(r'const uint8_t g_compressed' + stage + r'Cache\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not match:
        if stage == 'Dxil' and 'g_dxilCache' not in text:
            continue  # Linux translator was built with XENOS_RECOMP_DXIL=OFF.
        raise ValueError(stage)
    blob = bytes(int(v) for v in match[1].replace('\n', '').split(',') if v.strip())
    count = int(re.search(r'g_' + stage.lower() + r'CacheCompressedSize\s*=\s*(\d+)', text)[1])
    if stage == 'Dxil':
        assert count == 0
        continue
    assert len(blob) == count
    path = out / 'spirv_cache.zst'
    path.write_bytes(blob)
    assembly = ('__asm__(".section .rodata.timeshift_renut_spirv,\\\"a\\\"\\n"\n'
                '        ".global g_compressedSpirvCache\\n"\n'
                '        ".type g_compressedSpirvCache, %object\\n"\n'
                '        ".balign 16\\n"\n'
                '        "g_compressedSpirvCache:\\n"\n'
                '        ".incbin \\\"' + path.as_posix() + '\\\"\\n"\n'
                '        ".size g_compressedSpirvCache, .-g_compressedSpirvCache\\n"\n'
                '        ".previous\\n");')
    text = text[:match.start()] + assembly + text[match.end():]

(out / 'shader_cache_switch.cpp').write_text(text, newline='\n', encoding='utf-8')
report = dict(entries=entry_count, compressed_spirv_bytes=count,
              compressed_spirv_sha256=hashlib.sha256(blob).hexdigest(),
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              metadata_cpp_bytes=len(text.encode()), limitation='Embedding only, not a runtime lookup or render test')
(ROOT / 'analysis/switch-spirv-cache.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
