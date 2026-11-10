"""Extract owned tiling/state functions into ignored private test inputs."""
from guest_code import GuestCode,ROOT
import json,re
code=GuestCode()
symbols=['sub_8264B8C0','sub_8264BD58','sub_8264B750','sub_8263D008','sub_8263D088',
         'sub_8263CD90','sub_8263C858','sub_8263BD20','sub_8263CC98','sub_8263A240','sub_82645380']
bodies={s:code.function(s) for s in symbols}
controlled={'sub_828E1800','sub_8264A810','sub_8264C8E8','sub_82652720','sub_82652890',
            'sub_8264F3B8','sub_8264F6B8','sub_826571A0','sub_8264EFB8'}
unknown=set(call for s in symbols for call in code.index[s]['calls'])-set(symbols)-controlled
assert not unknown,unknown
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
            bodies[symbol]=text[match.start():end if end>=0 else len(text)].rstrip();missing.remove(symbol)
assert not missing
out=ROOT/'analysis/tiling-fixture';out.mkdir(exist_ok=True)
(out/'guest_tiling.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=symbols,helpers=sorted(helpers),controlled=sorted(controlled)),indent=2)+'\n')
print('Tiling fixture:',len(symbols),'original functions,',len(helpers),'original helpers')
