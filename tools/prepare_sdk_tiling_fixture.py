"""Owned bootstrap+state subset for the concrete SDK tiling host fixture."""
from guest_code import GuestCode,ROOT
import json,re
c=GuestCode()
symbols=['sub_826478E0','sub_8264E4C0','sub_82654078','sub_82647600','sub_82652DB8',
 'sub_8263D008','sub_8263D088','sub_8263CD90','sub_8263C858','sub_8263BD20','sub_8263CC98','sub_8263A240','sub_82645380']
bodies={s:c.function(s) for s in symbols}
helpers=set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)','\n'.join(bodies.values())));missing=set(helpers)
for name in c.archive.namelist():
 if not missing:break
 if not name.endswith('.cpp'):continue
 text=c.archive.read(name).decode()
 for symbol in list(missing):
  m=re.search(r'DEFINE_REX_FUNC\('+re.escape(symbol)+r'\)\s*\{',text)
  if m:
   end=text.find('DEFINE_REX_FUNC(',m.end());bodies[symbol]=text[m.start():end if end>=0 else len(text)].rstrip();missing.remove(symbol)
assert not missing
out=ROOT/'analysis/sdk-tiling-fixture';out.mkdir(exist_ok=True)
(out/'guest_sdk_tiling.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=symbols,helpers=sorted(helpers)),indent=2)+'\n')
print('SDK tiling fixture:',len(symbols),'original functions,',len(helpers),'ABI helpers')
