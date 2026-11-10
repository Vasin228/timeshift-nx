"""Extract original unbinding/reference release and no-hardware destructor CPU code."""
from guest_code import GuestCode,ROOT
import re,json
c=GuestCode();stop={'sub_8264F3B8','sub_8264D7C0','sub_8264E6F0','sub_8264F5D8'}
seen=set();todo=['sub_826477C8','sub_8263D140','sub_8263D4A8','sub_8264DCF0']
while todo:
 s=todo.pop()
 if s in seen or s in stop:continue
 seen.add(s);todo +=[t for t in c.index[s]['calls'] if t.startswith('sub_826')]
seen.update({'sub_826540C8','sub_82653C80','sub_828E1360'})
bodies={s:c.function(s) for s in seen}
helpers=set(re.findall(r'\b((?:__save|__rest)[A-Za-z0-9_]+)\(ctx, base\)','\n'.join(bodies.values())))
existing=set(json.loads((ROOT/'analysis/bootstrap-guest-fixture/manifest.json').read_text())['original_helpers'])
missing=helpers-existing
for name in c.archive.namelist():
 if not missing:break
 if not name.endswith('.cpp'):continue
 text=c.archive.read(name).decode()
 for s in list(missing):
  m=re.search(r'DEFINE_REX_FUNC\('+re.escape(s)+r'\)\s*\{',text)
  if m:
   end=text.find('DEFINE_REX_FUNC(',m.end());bodies[s]=text[m.start():end if end>=0 else len(text)].rstrip();missing.remove(s)
assert not missing
controlled={*stop,'sub_8280FA90','sub_828E1800','sub_828E27E0','sub_8280DE08'}
external={t for s in seen for t in c.index[s]['calls']}-seen-helpers
unexpected=external-controlled
out=ROOT/'analysis/device-cleanup-fixture';out.mkdir(exist_ok=True)
(out/'guest_cleanup.inc').write_text('\n\n'.join(bodies[s] for s in sorted(bodies))+'\n')
(out/'unexpected.inc').write_text('\n'.join('REX_EXTERN('+s+'){throw std::runtime_error("unexpected cleanup path '+s+'");}' for s in sorted(unexpected))+'\n')
def function_body(text,name):
 match=re.search(r'(?:bool|void|uint32_t) Video::'+name+r'\([^)]*\)\s*\{',text);assert match,name
 level=1;end=match.end()
 while level:
  if text[end]=='{':level+=1
  elif text[end]=='}':level-=1
  end+=1
 return text[match.start():end]
video=(ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/backend/video.cpp').read_text()
(out/'gpu_barrier.inc').write_text('\n'.join(function_body(video,s) for s in ['HostReady','QuiesceGuestWorkLocked','ClearGuestDeviceIf','SetGuestDevice','GuestDevice'])+'\n')
(out/'manifest.json').write_text(json.dumps(dict(original_functions=sorted(seen),original_helpers=sorted(helpers),
 external_services=sorted(external),forbidden_hardware_paths=sorted(unexpected),actual_gpu=False),indent=2)+'\n')
print('Cleanup fixture:',len(seen),'original CPU functions; forbidden/unexercised:',sorted(unexpected))
