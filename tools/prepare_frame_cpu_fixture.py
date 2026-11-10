"""Extract the owned ClearF/Resolve CPU closure; never distribute its output."""
from guest_code import GuestCode, ROOT
import json, re

code = GuestCode()
stop = {'sub_8264F3B8', 'sub_8264E6F0'}
seen, todo = set(), ['sub_8264A810', 'sub_8264C8E8']
while todo:
    symbol = todo.pop()
    if symbol in seen or symbol in stop:
        continue
    seen.add(symbol)
    todo += [s for s in code.index[symbol]['calls'] if s.startswith('sub_826')]
existing = json.loads((ROOT/'analysis/sdk-tiling-fixture/manifest.json').read_text())
symbols = seen - set(existing['original_functions'])
bodies = {s: code.function(s) for s in symbols}
helpers = set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)', '\n'.join(bodies.values())))
missing = helpers - set(existing['helpers'])
for name in code.archive.namelist():
    if not missing:
        break
    if not name.endswith('.cpp'):
        continue
    text = code.archive.read(name).decode()
    for symbol in list(missing):
        match = re.search(r'DEFINE_REX_FUNC\('+re.escape(symbol)+r'\)\s*\{', text)
        if match:
            end = text.find('DEFINE_REX_FUNC(', match.end())
            bodies[symbol] = text[match.start():end if end >= 0 else len(text)].rstrip()
            missing.remove(symbol)
assert not missing
external = sorted({c for s in symbols for c in code.index[s]['calls']} - seen - helpers)
renames = {'sub_8264A810':'frame_original_ClearF', 'sub_8264C8E8':'frame_original_Resolve'}
for old, new in renames.items():
    bodies[old] = bodies[old].replace('DEFINE_REX_FUNC('+old+')', 'DEFINE_REX_FUNC('+new+')')

# Conservative direct-call stack bound, including ABI save helpers' red zone.
# Reentrant callbacks and arbitrary indirect calls are not certified here.
frames = {}
for s in seen:
    instructions = code.instructions(s)
    sizes = [int(m[1]) for _, op in instructions if (m := re.fullmatch(r'stwu r1,-(\d+)\(r1\)', op))]
    frames[s] = max(sizes, default=0)
def bound(s, path=()):
    assert s not in path, (s, path)
    return frames[s] + max([256] + [bound(c, path+(s,)) for c in code.index[s]['calls'] if c in seen])
bounds = {s:bound(s) for s in renames}
host_source=(ROOT/'adapter/sdk_tiling_host.cpp').read_text()
stack_bytes=int(re.search(r'constexpr uint32_t stack_bytes=(\d+);',host_source)[1])
assert stack_bytes >= 256+max(bounds.values()), (stack_bytes,bounds)
out = ROOT/'analysis/frame-cpu-fixture'
out.mkdir(exist_ok=True)
(out/'guest_frame_cpu.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=sorted(symbols), helpers=sorted(helpers),
    external_services=external, conservative_stack_bytes=bounds, sdk_reserved_stack_bytes=stack_bytes,
    stack_limitation='Direct selected closure with256-byte service/red-zone allowance; not arbitrary indirect callbacks or floating-format CRT closure',
    gpu_execution=False), indent=2)+'\n')
print('Frame CPU fixture:',len(symbols),'additional originals; external services:',external,'stack bounds:',bounds)
