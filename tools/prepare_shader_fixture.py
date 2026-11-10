"""Extract four original shader/declaration functions plus exact ABI helpers."""
from guest_code import GuestCode, ROOT
import hashlib
import json
import re

code = GuestCode()
symbols = ['sub_8263F7C8', 'sub_82640FF0', 'sub_8263FB98', 'sub_8263FAA8']
bodies = {s: code.function(s) for s in symbols}
helpers = set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)', '\n'.join(bodies.values())))
missing = set(helpers)
for name in code.archive.namelist():
    if not missing: break
    if not name.endswith('.cpp'): continue
    text = code.archive.read(name).decode()
    for symbol in list(missing):
        match = re.search(r'DEFINE_REX_FUNC\(' + re.escape(symbol) + r'\)\s*\{', text)
        if match:
            end = text.find('DEFINE_REX_FUNC(', match.end())
            bodies[symbol] = text[match.start():end if end >= 0 else len(text)].rstrip()
            missing.remove(symbol)
if missing: raise ValueError(missing)
out = ROOT / 'analysis/shader-fixture'
out.mkdir(exist_ok=True)
payload = '\n\n'.join(bodies[s] for s in sorted(bodies)) + '\n'
(out / 'guest_shaders.inc').write_text(payload)
manifest = dict(original_functions=symbols, original_helpers=sorted(helpers),
                sha256=hashlib.sha256(payload.encode()).hexdigest(),
                controlled_services=['allocator/free', 'memcpy/memmove/memset', 'resource destructor', 'indirect resolver'])
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Private shader fixture:', len(symbols), 'original functions,', len(helpers), 'original ABI helpers')
