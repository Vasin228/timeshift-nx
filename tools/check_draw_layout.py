"""Decode draw ABI, default-clear bounds and tiling-worker evidence; never enable hooks."""
import hashlib
import json
import re
import capstone
from guest_code import GuestCode, ROOT

image=(ROOT/'analysis/default_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()=='67a052c61260f856ff87b79d13d04aab86668e6653a2d88619f7d6d61f72d27f'
code=GuestCode()
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
def normalized(s):
    return re.sub(r'0x[0-9a-f]+',lambda m:str(int(m[0],16)),s.lower()).replace(' ','')

anchors=[]
def anchor(function,op):
    pc=next(pc for pc,ins in code.instructions('sub_'+function) if ins==op)
    raw=image[pc-0x82000000:pc-0x82000000+4]
    decoded=list(decoder.disasm(raw,pc));assert len(decoded)==1
    ins=decoded[0]
    decoded_op={'rlwinm r9,r11,14,18,31':'srwi r9,r11,18'}.get(op,op)
    assert normalized(ins.mnemonic+' '+ins.op_str)==normalized(decoded_op),(function,op,ins.mnemonic,ins.op_str)
    anchors.append(dict(function='0x'+function,pc=f'0x{pc:08X}',instruction=op,
                        instruction_hex=raw.hex(),runtime_verified=False))

stack_specs=[('inline_stack_argument','82650020',240,324,'lwz r11,324(r1)'),
             ('inline_stack_argument','82650550',176,260,'lwz r30,260(r1)'),
             ('resolve_stencil','8264C8E8',384,476,'lwz r6,476(r1)'),
             ('resolve_parameters','8264C8E8',384,484,'lwz r29,484(r1)')]
header=(ROOT/'adapter/draw_abi.h').read_text()
stack=[]
for name,fn,frame,load,op in stack_specs:
    value=int(re.search(r'constexpr uint32_t '+name+r' = (\d+);',header)[1])
    assert value==load-frame
    anchor(fn,f'stwu r1,-{frame}(r1)');anchor(fn,op)
    stack.append(dict(name=name,function='0x'+fn,frame_bytes=frame,
                      framed_load_offset=load,caller_sp_offset=value))

for fn,ops in [
    ('82650550',['bl 0x82650020','neg r5,r29','add r4,r11,r24','bl 0x828e1800']),
    ('82650020',['stw r25,13432(r31)','stw r24,13436(r31)','stw r17,13444(r31)']),
    ('8264A810',['lwz r7,12432(r3)','lwz r11,12448(r3)','lbz r10,10940(r3)',
                 'lwz r11,36(r11)','rlwinm r9,r11,14,18,31','rlwinm r11,r11,29,17,31',
                 'lwz r11,12720(r3)','lwz r10,12724(r3)','lwz r10,12728(r3)',
                 'lwz r10,12732(r3)','lwz r10,12736(r3)',
                 'lwz r11,13172(r3)','lwz r11,13176(r3)']),
    ('8264B8C0',['stw r27,12740(r31)','stw r29,13172(r31)',
                 'stw r28,13176(r31)','stfs f31,13200(r31)','stw r24,13204(r31)']),
    ('8264BD58',['lis r11,-32155','addi r5,r11,9880','bl 0x8264f6b8']),
    ('82652890',['lis r11,-32155','addi r6,r11,9880','bl 0x8264ee08'])]:
    for op in ops:anchor(fn,op)
assert ((-32155<<16)+9880)&0xFFFFFFFF==0x82652698

hooks=[]
for name in ['826505F0','8264FFD8','82650550','8264FB38','82650020','8264A810','8264C8E8']:
    address=int(name,16);count=len(code.instructions('sub_'+name))
    raw=image[address-0x82000000:address-0x82000000+count*4]
    # Full functions contain Xenon's VMX128 extensions, which stock Capstone
    # cannot decode. Fingerprint those bytes; independently decode only anchors.
    assert len(raw)==count*4
    hooks.append(dict(address='0x'+name,instruction_count=count,
                      guest_code_sha256=hashlib.sha256(raw).hexdigest(),
                      installed_in_game=False,runtime_verified=False))
profile=dict(image_sha256=hashlib.sha256(image).hexdigest(),anchors=anchors,
             caller_stack_arguments=stack,dormant_compiled_hooks=hooks,
             gpu_worker='0x82652698',internal_segment_end='0x82652890',
             production_activation=False,tiling_integrated=False,
             limitation='Selected ABI/field/control-flow evidence; not full draw/command-buffer coverage or GPU execution')
(ROOT/'adapter/draw_profile.json').write_text(json.dumps(profile,indent=2)+'\n')
print('PASS:',len(hooks),'draw fingerprints;',len(anchors),'PPC anchors; four stack arguments; backend disabled')
