"""Independently decode the owned binary anchors used by the native tiling protocol."""
import hashlib,json,re
import capstone
from guest_code import GuestCode,ROOT

image=(ROOT/'analysis/default_image.bin').read_bytes()
digest=hashlib.sha256(image).hexdigest()
assert digest=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
code=GuestCode()
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def normalized(s):return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')
specs={
 '8264B8C0':['stw r27,12740(r31)','addi r9,r31,12988','addi r10,r31,12748',
  'lhz r11,10368(r31)','addi r8,r31,13104','addi r11,r31,12984',
  'stw r5,13164(r31)','stw r11,13168(r31)','stw r29,13172(r31)','stw r28,13176(r31)',
  'stw r23,13180(r31)','mr r8,r24','fmr f1,f31','bl 0x8264a810',
  'stfs f31,13200(r31)','stw r24,13204(r31)','stw r9,12716(r31)',
  'stw r10,12720(r31)','stw r5,12712(r31)','bl 0x82652720'],
 '8264BD58':['mr r24,r9','mr r23,r10','addi r5,r31,12744','lwz r10,12716(r31)',
  'ori r4,r26,256','ori r4,r4,512','lfs f1,13200(r31)','lwz r11,13204(r31)',
  'stw r22,100(r1)','stw r23,100(r1)','stw r24,92(r1)',
  'stw r22,12716(r31)','andi. r11,r11,223','andi. r10,r10,207',
  'bl 0x82652890'],
 '8264B750':['stw r4,12700(r31)','stw r10,12708(r31)',
  'stw r4,10932(r31)','stw r9,10936(r31)','stw r9,12704(r31)']}
anchors=[]
for fn,ops in specs.items():
    instructions=code.instructions('sub_'+fn)
    for op in ops:
        pc=next(pc for pc,ins in instructions if ins==op)
        raw=image[pc-0x82000000:pc-0x82000000+4]
        decoded=list(decoder.disasm(raw,pc));assert len(decoded)==1
        ins=decoded[0]
        assert normalized(ins.mnemonic+' '+ins.op_str)==normalized(op),(op,ins.mnemonic,ins.op_str)
        anchors.append(dict(function='0x'+fn,pc=f'0x{pc:08X}',instruction=op,bytes=raw.hex()))
functions=[]
manifest=json.loads((ROOT/'analysis/tiling-fixture/manifest.json').read_text())
for fn in manifest['original_functions']:
    pc=int(fn[4:],16);count=len(code.instructions(fn));raw=image[pc-0x82000000:pc-0x82000000+count*4]
    assert len(raw)==count*4
    functions.append(dict(address=f'0x{pc:08X}',instructions=count,sha256=hashlib.sha256(raw).hexdigest()))
assert 12744+15*16==12984 and 12984+15*8==13104
assert 'constexpr uint32_t max_tiles=15;' in (ROOT/'adapter/tiling.cpp').read_text()
profile=dict(image_sha256=digest,anchors=anchors,functions=functions,
 supported_tile_capacity=15,capacity_basis='Selected arrays: rects12744..12983, origins12984..13103',
 installed_in_game=False,production_activation=False,
 limitation='Selected native CPU protocol; segment allocation/retirement and Clear/Resolve services remain required, no GPU/runtime proof')
(ROOT/'adapter/tiling_profile.json').write_text(json.dumps(profile,indent=2)+'\n')
print('PASS:',len(anchors),'PPC anchors;',len(functions),'function hashes; native tiling not installed')
