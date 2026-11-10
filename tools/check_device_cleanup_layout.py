"""Independently decode cleanup ownership/destructor anchors in the owned image."""
from guest_code import GuestCode,ROOT
import capstone,hashlib,json,re
c=GuestCode();image=(ROOT/'analysis/default_image.bin').read_bytes();digest=hashlib.sha256(image).hexdigest()
assert digest=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def norm(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
specs={
 '826540C8':['lwz r11,48(r31)','bl 0x826477c8','lbz r11,10940(r31)',
  'rlwinm. r11,r11,0,0,24','bne 0x82654268','lwz r3,16712(r31)','lwz r3,21552(r31)','bl 0x82653c80'],
 '826477C8':['addi r29,r31,12432','lwz r10,14816(r31)','lwz r10,14808(r31)','bl 0x8263f8c0',
  'bl 0x8263f608','bl 0x8263fa90','bl 0x8263ca78','bl 0x8263c958','bl 0x82649548'],
 '82653BE0':['bl 0x8265a828','lwz r3,14812(r31)','lwz r3,14816(r31)','lwz r3,14808(r31)',
  'stw r29,14812(r31)','stw r29,14816(r31)','stw r29,14808(r31)','bl 0x8264dcf0'],
 '8264DCF0':['addi r11,r31,4','lwarx r10,0,r11','addi r10,r10,-1','stwcx. r10,0,r11','bl 0x8264d7c0'],
 '82653C80':['lwz r3,12(r31)','bl 0x82653c80','lwz r3,8(r31)','stw r11,12(r31)','bl 0x8280fa90']}
anchors=[]
for fn,ops in specs.items():
 instructions=c.instructions('sub_'+fn)
 for op in ops:
  pc=next(pc for pc,ins in instructions if ins==op);raw=image[pc-0x82000000:pc-0x82000000+4]
  decoded=list(decoder.disasm(raw,pc));assert len(decoded)==1
  expected={'rlwinm. r11,r11,0,0,24':'clrrwi. r11,r11,7'}.get(op,op)
  assert norm(decoded[0].mnemonic+' '+decoded[0].op_str) in {norm(op),norm(expected)},(op,decoded[0].op_str)
  anchors.append(dict(function='0x'+fn,pc=f'0x{pc:08X}',instruction=op,bytes=raw.hex()))
assert 'fields{14812,14816,14808}' in (ROOT/'adapter/device_cleanup.cpp').read_text()
profile=dict(image_sha256=digest,anchors=anchors,automatic_release_order=[14812,14816,14808],
 retained_cpu_destructor_fields=[16712,21552],forbidden_legacy_presentation_shutdown='0x8265A828',
 no_hardware_flag=128,production_activation=False,
 limitation='Selected CPU ownership evidence; GPU protocol fixture controls backend/kernel services')
(ROOT/'adapter/device_cleanup_profile.json').write_text(json.dumps(profile,indent=2)+'\n')
print('PASS:',len(anchors),'original cleanup PPC anchors; actual auto-reference order and retained CPU helper ownership')
