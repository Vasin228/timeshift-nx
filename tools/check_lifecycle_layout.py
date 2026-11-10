"""Decode lifecycle/allocator ABI anchors without activating the native backend."""
import hashlib,json,re
import capstone
from guest_code import GuestCode,ROOT
image=(ROOT/'analysis/default_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
code=GuestCode();decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def norm(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
specs={
 '826478E0':['mr r27,r8','mr r29,r4','stw r31,0(r27)','bl 0x826474d8'],
 '82647600':['lwz r11,60(r31)','cmplwi cr6,r11,1','bl 0x826540c8','stw r3,60(r31)'],
 '8264E6F0':['mr r6,r5','lwz r3,13500(r31)','stb r11,10941(r31)'],
 '8264F3B8':['mr r31,r3','bl 0x8264f230','lwz r3,48(r31)'],
 '8264F5D8':['mr r29,r4','lwz r11,48(r31)','lwz r10,52(r31)','bl 0x8264f3b8'],
 '826474D8':['mr r5,r30','li r4,0','bl 0x828e27e0'],
 '826540C8':['lwz r11,48(r31)','lbz r11,10940(r31)','lwz r3,16712(r31)','lwz r3,21552(r31)']}
anchors=[]
for fn,ops in specs.items():
 for op in ops:
  pc=next(pc for pc,ins in code.instructions('sub_'+fn) if ins==op)
  raw=image[pc-0x82000000:pc-0x82000000+4];ins=list(decoder.disasm(raw,pc));assert len(ins)==1
  assert norm(ins[0].mnemonic+' '+ins[0].op_str)==norm(op),(op,ins[0].op_str)
  anchors.append(dict(function='0x'+fn,pc=f'0x{pc:08X}',instruction=op,bytes=raw.hex()))
hooks=[]
for fn in ['826478E0','82647600','8264E6F0','8264F3B8','8264F5D8']:
 pc=int(fn,16);count=len(code.instructions('sub_'+fn));raw=image[pc-0x82000000:pc-0x82000000+count*4]
 hooks.append(dict(address='0x'+fn,instructions=count,sha256=hashlib.sha256(raw).hexdigest(),installed_in_game=False))
(ROOT/'adapter/lifecycle_profile.json').write_text(json.dumps(dict(image_sha256=hashlib.sha256(image).hexdigest(),
 anchors=anchors,dormant_hooks=hooks,production_activation=False,
 limitation='Selected original ABI/fields; native registry/source routing still lacks full resource teardown, fence/wait/segment/draw/presentation integration'),indent=2)+'\n')
print('PASS:',len(anchors),'lifecycle/allocator PPC anchors; five dormant entry hashes; backend disabled')
