"""Extract retained wait CPU code with only the selected locked-load interposition."""
from port_fence_readers import code,SITES,patched_function,verify_and_apply
from guest_code import ROOT
import json,re
verify_and_apply(False)
symbols=[*SITES,'sub_826473C0']
bodies={s:patched_function(s) if s in SITES else code.function(s) for s in symbols}
helpers=set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)','\n'.join(bodies.values())))
existing=set(json.loads((ROOT/'analysis/bootstrap-guest-fixture/manifest.json').read_text())['original_helpers'])
missing=helpers-existing
for name in code.archive.namelist():
 if not missing:break
 if not name.endswith('.cpp'):continue
 text=code.archive.read(name).decode()
 for symbol in list(missing):
  m=re.search(r'DEFINE_REX_FUNC\('+re.escape(symbol)+r'\)\s*\{',text)
  if m:
   end=text.find('DEFINE_REX_FUNC(',m.end());bodies[symbol]=text[m.start():end if end>=0 else len(text)].rstrip();missing.remove(symbol)
assert not missing
out=ROOT/'analysis/fence-wait-fixture';out.mkdir(exist_ok=True)
(out/'guest_wait.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=symbols,original_helpers=sorted(helpers),
 locked_completion_loads=6,unmodified_close=True,actual_gpu=False),indent=2)+'\n')
print('Wait fixture:',len(symbols),'retained CPU functions; original Close unchanged; six locked completion loads')
