"""Verify public fence-query ABI and extract an ignored original reference."""
import hashlib,json,re
import capstone
from guest_code import GuestCode,ROOT
code=GuestCode();name='sub_8264F4F0'
image=(ROOT/'analysis/default_image.bin').read_bytes()
digest=hashlib.sha256(image).hexdigest()
assert digest=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def norm(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
ops=['mr r30,r3','cmplwi cr6,r30,0','lis r11,-32256','lwz r11,2324(r11)',
 'lwz r31,0(r11)','lwz r11,10908(r31)','cmplw cr6,r30,r11','bl 0x8264f3b8',
 'lwz r10,10896(r31)','subf r9,r30,r11','subf r11,r10,r11','clrlwi r3,r11,31']
anchors=[]
instructions=code.instructions(name)
for op in ops:
 pc=next(pc for pc,ins in instructions if ins==op)
 raw=image[pc-0x82000000:pc-0x82000000+4];ins=list(decoder.disasm(raw,pc));assert len(ins)==1
 assert norm(ins[0].mnemonic+' '+ins[0].op_str)==norm(op),(op,ins[0].op_str)
 anchors.append(dict(pc=f'0x{pc:08X}',instruction=op,bytes=raw.hex()))
assert ((-32256<<16)+2324)&0xFFFFFFFF==0x82000914
assert re.search(r'kGlobalDeviceImport\s*=\s*0x82000914', (ROOT/'adapter/device_bootstrap.h').read_text())
address=int(name[4:],16);raw=image[address-0x82000000:address-0x82000000+4*len(instructions)]
(ROOT/'adapter/fence_query_profile.json').write_text(json.dumps(dict(image_sha256=digest,
 address='0x8264F4F0',arguments=['fence:r3'],device_source='VdGlobalDevice via import82000914',
 anchors=anchors,function_sha256=hashlib.sha256(raw).hexdigest(),production_activation=False,
 limitation='Selected static ABI and CPU reference; no GPU or complete fence/wait routing'),indent=2)+'\n')
out=ROOT/'analysis/fence-query-fixture';out.mkdir(exist_ok=True)
(out/'guest_fence_query.inc').write_text(code.function(name).replace('DEFINE_REX_FUNC('+name+')',
 'DEFINE_REX_FUNC(lifecycle_original_Query)')+'\n')
print('PASS:',len(anchors),'public fence-query PPC anchors; one-fence argument/global device; dormant entry fingerprint')
