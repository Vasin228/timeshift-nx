"""Extract an owned, unmodified state-setter subset; never publish its output."""
import hashlib
import json
import re
import struct
from guest_code import GuestCode, ROOT

code = GuestCode()
image = (ROOT / 'analysis/default_image.bin').read_bytes()
out = ROOT / 'analysis/device-fixture'
out.mkdir(exist_ok=True)
tables = {}
roots = set()
for name, address, count in [('render', 0x82B2FC18, 101), ('sampler', 0x82B300D8, 20)]:
    rows = []
    for i in range(count):
        getter, setter, default = struct.unpack_from('>III', image, address - 0x82000000 + i * 12)
        roots.update([f'sub_{getter:08X}', f'sub_{setter:08X}'])
        rows.append((getter, setter, default))
    tables[name] = rows
roots.update('sub_' + s for s in ['8263FA90', '8263C958', '8263CA78', '82649548', '8263CD90'])
# This allocator is a recording-only slow path. Reaching it in the fixture is
# an error, never a successful mock of command-buffer/resource lifetime work.
forbidden = {'sub_82650B58', 'sub_8264F3B8'}
pending = list(roots)
bodies = {}
while pending:
    symbol = pending.pop()
    if symbol in bodies or symbol in forbidden:
        continue
    bodies[symbol] = code.function(symbol)
    pending.extend(code.index[symbol]['calls'])
# Save/restore helpers are generated in the same frozen source archive but
# deliberately absent from the address-based index. Extract their exact bodies.
helpers = set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)', '\n'.join(bodies.values())))
missing = set(helpers)
for name in code.archive.namelist():
    if not missing:
        break
    if not name.endswith('.cpp'):
        continue
    text = code.archive.read(name).decode()
    for symbol in list(missing):
        match = re.search(r'DEFINE_REX_FUNC\(' + re.escape(symbol) + r'\)\s*\{', text)
        if match:
            end = text.find('DEFINE_REX_FUNC(', match.end())
            bodies[symbol] = text[match.start():end if end >= 0 else len(text)].rstrip()
            missing.remove(symbol)
if missing:
    raise ValueError(missing)
(out / 'guest_state.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies)) + '\n')
text = 'struct StateEntry { PPCFunc *getter; PPCFunc *setter; uint32_t initial; };\n'
for name, rows in tables.items():
    text += f'constexpr StateEntry {name}_entries[] = {{\n'
    text += ''.join(f'  {{sub_{g:08X},sub_{s:08X},0x{v:08X}}},\n' for g,s,v in rows)
    text += '};\n'
(out / 'state_tables.inc').write_text(text)
report = dict(original_functions=len(bodies), original_helpers=len(helpers),
              render_states=len(tables['render']), sampler_states=len(tables['sampler']),
              forbidden_functions=sorted(forbidden),
              guest_subset_sha256=hashlib.sha256((out / 'guest_state.inc').read_bytes()).hexdigest(),
              limitation='Offline state/read-view fixture; no renderer or kernel execution')
(out / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
