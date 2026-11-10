"""Extract six original draw/control functions; fail on unexpected GPU helpers."""
from guest_code import GuestCode,ROOT
import re,json
code=GuestCode()
symbols=['sub_826505F0','sub_8264FFD8','sub_82650550','sub_8264FB38','sub_82650020','sub_8264A810']
bodies={s:code.function(s) for s in symbols}
known={'sub_8264E6F0','sub_828E1800','sub_8264A180'}
unknown=set(call for s in symbols for call in code.index[s]['calls'])-set(symbols)-known
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
out=ROOT/'analysis/draw-fixture';out.mkdir(exist_ok=True)
(out/'guest_draw.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'unexpected.inc').write_text('\n'.join('REX_EXTERN('+s+'){throw std::runtime_error("unexpected '+s+'");}' for s in sorted(unknown))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=symbols,helpers=sorted(helpers),unexpected_helpers=sorted(unknown)),indent=2)+'\n')
print('Draw fixture:',len(symbols),'original functions,',len(helpers),'exact ABI helpers')
