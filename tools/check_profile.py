"""Check recorded binary fingerprints/field anchors without installing hooks."""
from pathlib import Path
import hashlib, json, re
import capstone
from guest_code import ROOT, GuestCode
profile=json.loads((ROOT/'adapter/timeshift_profile.json').read_text())
image=(ROOT/'analysis/default_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()==profile['image_sha256']
assert profile['enabled'] is False and profile['device_layout_verified'] is False
code=GuestCode()
count=0
for hook in profile['hooks']:
    assert not hook['verified'] and not hook['runtime_verified'] and not hook['installed']
    if not hook['static_semantics_reviewed']:continue
    address=int(hook['address'],16)
    assert len(code.instructions(f'sub_{address:08X}'))==hook['instruction_count']
    raw=image[address-0x82000000:address-0x82000000+hook['instruction_count']*4]
    assert hashlib.sha256(raw).hexdigest()==hook['guest_code_sha256']
    count+=1
fields={entry['name']:entry for entry in profile['device_fields']}
decoder=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
proof=json.loads((ROOT/'analysis/field-proof.json').read_text())
def normalized(text):
    return re.sub(r'0x[0-9a-f]+',lambda match:str(int(match[0],16)),text.lower()).replace(' ','')
for entry in proof:
    pc=entry['pc']
    instructions=list(decoder.disasm(image[pc-0x82000000:pc-0x82000000+4],pc))
    assert len(instructions)==1,entry['name']
    instruction=instructions[0]
    assert normalized(instruction.mnemonic+' '+instruction.op_str)==normalized(entry['expected']),entry['name']
for entry in fields.values():
    pc=int(entry['evidence_pc'],16)
    assert image[pc-0x82000000:pc-0x82000000+4].hex()==entry['instruction_hex']
    assert not entry['runtime_verified']
    if entry['name'] not in ('device_bytes','device_alignment'):
        assert 0<=entry['value']<fields['device_bytes']['value']
header=(ROOT/'adapter/device_bootstrap.h').read_text(encoding='utf-8-sig')
for name in ['ring','ring_limit','ring_guarantee','fence_completion','presentation_scratch','flags','flags1',
             'critical_section0','critical_section1','hardware_sentinel0','hardware_sentinel1',
             'performance_frequency','behavior_flags']:
    value=int(re.search(r'constexpr uint32_t '+name+r' = (\d+);',header)[1])
    assert value==fields[name]['value'],name
layout_header=(ROOT/'adapter/timeshift_device_layout.h').read_text()
layout_aliases={'allocation_bytes':'device_bytes','alignment':'device_alignment','swap_flags':'flags1'}
layout_constants=re.findall(r'inline constexpr uint32_t (\w+) = (\d+);',layout_header)
for name,value in layout_constants:
    field=layout_aliases.get(name,name)
    assert int(value)==fields[field]['value'],name
assert fields['predication']['value']+4==12704
report=dict(success=True,static_hook_fingerprints=count,field_anchors=len(fields),
            renderer_layout_constants=len(layout_constants),
            original_instruction_anchors_decoded=len(proof),capstone_version=capstone.__version__,
            image_sha256=profile['image_sha256'],backend_enabled=False,
            limitation='Static record consistency only; not runtime or full rendering proof')
(ROOT/'analysis/profile-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',count,'hook fingerprints;',len(fields),'field anchors; bootstrap constants agree; backend disabled')
