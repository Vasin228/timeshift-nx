"""Extract the bootstrap subset with actual generated ABI helpers for the SDK bridge."""
from guest_code import GuestCode, ROOT
import json
import re

code=GuestCode()
symbols=['sub_826478E0','sub_8264E4C0','sub_82654078','sub_82647600','sub_82652DB8']
bodies={s:code.function(s) for s in symbols}
helpers=set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)','\n'.join(bodies.values())))
missing=set(helpers)
for name in code.archive.namelist():
    if not missing:break
    if not name.endswith('.cpp'):continue
    text=code.archive.read(name).decode()
    for symbol in list(missing):
        match=re.search(r'DEFINE_REX_FUNC\('+re.escape(symbol)+r'\)\s*\{',text)
        if match:
            end=text.find('DEFINE_REX_FUNC(',match.end())
            bodies[symbol]=text[match.start():end if end>=0 else len(text)].rstrip()
            missing.remove(symbol)
assert not missing
out=ROOT/'analysis/bootstrap-guest-fixture';out.mkdir(exist_ok=True)
(out/'guest_bootstrap.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=symbols,original_helpers=sorted(helpers)),indent=2)+'\n')
print('Real-SDK bootstrap fixture:',len(symbols),'original functions,',len(helpers),'original ABI helpers')
