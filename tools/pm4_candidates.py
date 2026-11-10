from pathlib import Path
import json,re,zipfile
root=Path(__file__).resolve().parents[1];index=json.loads((root/'analysis/function-index.json').read_text());found=[]
with zipfile.ZipFile('D:/Anti/timeshift-recomp/frozen/vulkan-part181/actual-app-source.zip') as z:
 for name in z.namelist():
  if not re.search(r'_recomp\.\d+\.cpp$',name):continue
  text=z.read(name).decode(errors='replace');matches=list(re.finditer(r'DEFINE_REX_FUNC\((sub_[0-9A-Fa-f]{8})\)\s*\{',text))
  for n,m in enumerate(matches):
   body=text[m.end():matches[n+1].start() if n+1<len(matches) else len(text)]
   instructions=[v.strip()[3:] for v in body.splitlines() if v.strip().startswith('// ')]
   for i in range(len(instructions)-1):
    a=re.fullmatch(r'lis r(\d+),(-?\d+)',instructions[i]);b=re.fullmatch(r'ori r(\d+),r(\d+),(\d+)',instructions[i+1])
    if not a or not b or a[1]!=b[1] or a[1]!=b[2]:continue
    value=((int(a[2])<<16)|int(b[3]))&0xFFFFFFFF;opcode=(value>>8)&127
    if value>>30!=3 or opcode not in (0x22,0x36,0x27,0x2D):continue
    found.append(dict(symbol=m[1],instruction_index=i,value=f'{value:08X}',opcode=f'{opcode:02X}',instruction_count=len(instructions),calls=index[m[1]]['calls'],status='literal anchor only; instruction PC and D3D role not verified'))
(root/'analysis/pm4-candidates.json').write_text(json.dumps(found,indent=2)+'\n')
for v in found:print(v['symbol'],v['value'],'instructions',v['instruction_count'])
print('Literal candidates:',len(found))
