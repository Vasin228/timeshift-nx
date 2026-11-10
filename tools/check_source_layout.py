"""Verify source ownership's initial fence convention against owned original bytes."""
import hashlib,json,re
import capstone
from guest_code import GuestCode,ROOT
image=(ROOT/'analysis/default_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
code=GuestCode();decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def normalize(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
specs={
 '8264F710':['li r11,3','stw r11,10908(r31)','addi r11,r11,-2','stw r11,0(r10)',
              'stw r11,4(r9)','stw r3,10896(r31)','stw r3,10900(r31)'],
 '8264EB80':['lwz r5,10908(r11)','stw r5,0(r10)','addi r10,r5,2',
              'stw r10,10908(r11)','lwz r9,21516(r11)'],
 '8264ED40':['subf r9,r30,r11','subf r11,r10,r11','cmplw cr6,r9,r11','lwz r11,13216(r31)'],
 '8264E6F0':['lwz r3,13500(r31)','ori r11,r11,32','stb r11,10941(r31)']}
anchors=[]
for fn,ops in specs.items():
 for op in ops:
  pc=next(pc for pc,ins in code.instructions('sub_'+fn) if ins==op)
  raw=image[pc-0x82000000:pc-0x82000000+4];decoded=list(decoder.disasm(raw,pc));assert len(decoded)==1
  ins=decoded[0];assert normalize(ins.mnemonic+' '+ins.op_str)==normalize(op),(op,ins.op_str)
  anchors.append(dict(function='0x'+fn,pc=f'0x{pc:08X}',instruction=op,bytes=raw.hex()))
profile=dict(image_sha256=hashlib.sha256(image).hexdigest(),anchors=anchors,
 initial_current_tag=3,initial_completed_tag=1,tag_increment=2,
 production_activation=False,
 limitation='Selected original fence/allocator semantics; no full ring/segment/kernel execution or live ownership integration')
(ROOT/'adapter/source_profile.json').write_text(json.dumps(profile,indent=2)+'\n')
print('PASS:',len(anchors),'original fence/allocator PPC anchors; initial3/1, stride2, backend disabled')
