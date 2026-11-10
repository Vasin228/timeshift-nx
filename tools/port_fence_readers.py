"""Route six verified completion loads through the owner; preserve retained CPU code."""
from guest_code import GuestCode,ROOT
import capstone,hashlib,json,re,sys

code=GuestCode()
SITES={
 'sub_8264ED40':[(0x8264ED6C,'lwz r10,0(r10)','r31'),
                  (0x8264EDA4,'lwz r10,0(r10)','r31'),
                  (0x8264EDE4,'lwz r10,0(r10)','r31')],
 'sub_82647390':[(0x826473A4,'lwz r10,0(r10)','r4')],
 'sub_82647528':[(0x82647598,'lwz r9,0(r11)','r29'),
                  (0x826475A4,'lwz r11,0(r11)','r29')],
}
image=(ROOT/'analysis/default_image.bin').read_bytes()
digest=hashlib.sha256(image).hexdigest()
assert digest=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def norm(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')

def patched_function(symbol):
 sites={pc:(op,device) for pc,op,device in SITES[symbol]}
 instructions=dict(code.instructions(symbol));lines=code.function(symbol).splitlines()
 pc=int(symbol[4:],16);replaced=0;result=[]
 for index,line in enumerate(lines):
  if line.strip().startswith('// '):
   at=pc;pc+=4
   if at in sites:
    op,device=sites[at];assert instructions[at]==op
    statement=lines[index+1]
    match=re.fullmatch(r'(\s*ctx\.r\d+\.u64 = )REX_LOAD_U32\(([^()]*)\);',statement)
    assert match,(symbol,at,statement)
    expression=match[2]
    replacement=match[1]+f'timeshift::renderer::ReadCompletionOr(ctx.{device}.u32, {expression}, [&]{{return REX_LOAD_U32({expression});}});'
    lines[index+1]='\n'.join(['#if defined(TIMESHIFT_RECOMP)',replacement,'#else',statement,'#endif'])
    replaced+=1
  result.append(line)
 assert replaced==len(sites)
 return '\n'.join(result)

def verify_and_apply(apply=False):
 anchors=[];files={}
 for symbol,sites in SITES.items():
  for pc,op,device in sites:
   raw=image[pc-0x82000000:pc-0x82000000+4]
   ins=list(decoder.disasm(raw,pc));assert len(ins)==1
   assert norm(ins[0].mnemonic+' '+ins[0].op_str)==norm(op)
   anchors.append(dict(function=symbol,pc=f'0x{pc:08X}',instruction=op,device_register=device,bytes=raw.hex()))
  name=code.index[symbol]['file']
  if name not in files:files[name]=code.archive.read(name).decode().replace('\r\n','\n')
  original=code.function(symbol).replace('\r\n','\n');assert files[name].count(original)==1
  files[name]=files[name].replace(original,patched_function(symbol))
 report=[]
 for name,body in files.items():
  include='#if defined(TIMESHIFT_RECOMP)\n#include "fence_read.h"\n#endif\n'
  body=include+body
  original=code.archive.read(name).decode().replace('\r\n','\n')
  destination=ROOT/'private/app'/name
  current=destination.read_text()
  assert current in (original,body),'refuse to overwrite unrelated private app edits: '+name
  changed=current!=body
  if apply and changed:destination.write_text(body,newline='\n')
  report.append(dict(file=name,sha256=hashlib.sha256(body.encode()).hexdigest(),bytes=len(body.encode())))
 (ROOT/'adapter/fence_read_profile.json').write_text(json.dumps(dict(image_sha256=digest,anchors=anchors,
   private_app_files=report,production_activation=False,
   limitation='Six selected completion loads, not all fence readers; retained CBlocker/BlockOnFence CPU semantics'),indent=2)+'\n')
 print('PASS:',len(anchors),'original completion-load PPC anchors;',len(files),'reproducible private app files;',
       'applied' if apply else 'check only')

if __name__=='__main__':verify_and_apply('--apply-private' in sys.argv)
