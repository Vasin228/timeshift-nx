"""Decode owned binary anchors for the shader/declaration layouts actually used."""
import hashlib
import json
import re
import capstone
from guest_code import GuestCode, ROOT

image=(ROOT/'analysis/default_image.bin').read_bytes()
code=GuestCode()
specs=[
 ('kVertexContainerOffset',872,'8263F7C8','li r5,872'),
 ('kVertexPhysicalOffset',32,'8263F7C8','stw r30,32(r31)'),
 ('kPixelContainerOffset',40,'82640FF0','addi r3,r31,40'),
 ('kPixelPhysicalOffset',24,'82640FF0','stw r29,24(r31)'),
 ('kVertexElementBytes',12,'8263FB98','mulli r11,r11,12'),
 ('declaration_elements',52,'8263FAA8','addi r10,r31,52'),
]
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def normalized(s):
    return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
header=(ROOT/'adapter/shader_capture.h').read_text()
records=[]
for name,value,function,expected in specs:
    pc=next(pc for pc,op in code.instructions('sub_'+function) if op==expected)
    raw=image[pc-0x82000000:pc-0x82000000+4]
    decoded=list(decoder.disasm(raw,pc));assert len(decoded)==1
    ins=decoded[0]
    assert normalized(ins.mnemonic+' '+ins.op_str)==normalized(expected),name
    if name.startswith('k'):
        assert int(re.search(r'constexpr uint32_t '+name+r' = (\d+);',header)[1])==value
    records.append(dict(name=name,value=value,function=f'0x{function}',pc=f'0x{pc:08X}',
                        instruction_hex=raw.hex(),runtime_verified=False))
profile=dict(image_sha256=hashlib.sha256(image).hexdigest(),anchors=records,
             compiled_hooks=['0x8263F7C8','0x82640FF0','0x8263FB98','0x8264D7C0'],
             installed_in_game=False,production_activation=False,
             limitation='Selected shader/declaration fields; no full device/resource/command-buffer layout proof')
(ROOT/'adapter/shader_object_profile.json').write_text(json.dumps(profile,indent=2)+'\n')
print('PASS:',len(records),'original PPC shader/declaration anchors; constants agree; game integration inactive')
