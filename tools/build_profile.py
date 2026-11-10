from guest_code import GuestCode, ROOT
import hashlib, json

code = GuestCode()
image = (ROOT/'analysis/default_image.bin').read_bytes()
profile_path = ROOT/'adapter/timeshift_profile.json'
profile = json.loads(profile_path.read_text())
profile['schema_version'] = 2
profile['image_sha256'] = hashlib.sha256(image).hexdigest()
profile['verification_scope'] = 'Static code review and offline CPU fixture only; no installed hooks or renderer execution'
roles = {
 'Direct3D_CreateDevice': ('826478E0', 'r3 adapter, r4 device type, r5 reserved, r6 behavior flags, r7 presentation parameters, r8 output device'),
 'KickOff': ('8264F3B8', 'r3 device; returns ring cursor'),
 'KickOffSegment': ('8264F230', 'r3 device'),
 'BeginRingBig': ('8264F5D8', 'r3 device, r4 dword count; returns ring cursor'),
 'RingBufferDeviceAllocate': ('8264E4E8', 'r3 device, r4 flags, r5 byte-size pointer, r6 alignment'),
 'AllocateInlineData': ('8264E6F0', 'r3 device, r4 dword count, r5 alignment'),
 'BlockOnFence': ('8264ED40', 'r3 device, r4 fence, r5 blocker type, r6 no-kick flag'),
 'IsFencePending': ('8264F4F0', 'r3 fence; uses VdGlobalDevice internally; no device argument'),
 'InsertCallback': ('8264F6B8', 'r3 device, r4 type, r5 callback, r6 context'),
 'SynchronizeToPresentationInterval': ('8264AD00', 'r3 device, r4 immediate flag'),
 'Swap': ('8264B1A8', 'r3 device, r4 front texture, r5 presentation parameters'),
 'DrawIndexedVertices': ('826505F0', 'r3 device, r4 primitive, r5 base vertex, r6 start index, r7 index count'),
 'DrawVerticesUP': ('8264FFD8', 'r3 device, r4 primitive, r5 vertex count, r6 vertex data, r7 stride'),
 'BeginVertices': ('8264FB38', 'r3 device, r4 primitive, r5 vertex count, r6 stride; returns writable vertex data'),
 'ClearF': ('8264A810', 'r3 device, r4 flags, r5 optional rectangle, r6 color vector, f1 depth, r8 stencil'),
 'Resolve': ('8264C8E8', 'r3 device, r4 flags, r5 source rect, r6 texture, r7 destination point, r8 level, r9 slice, r10 clear color; f1 depth; caller sp+92 stencil, sp+100 parameters'),
 'XGRegisterVertexShader': ('8263F7C8', 'r3 vertex shader object, r4 physical part'),
 'CreateVertexDeclaration': ('8263FB98', 'r3 elements; returns declaration object'),
 'D3DResource_Destroy': ('8264D7C0', 'r3 resource; waits on type-specific fences then frees'),
}
additional = {
 'D3DDevice_CreatePixelShader': ('82640FF0', 'r3 complete shader container; returns shader object; registration is inlined here'),
 'NewRingSegment': ('8264F0B8', 'r3 device, r4 dword request'),
 'CBlocker_Check': ('82647528', 'r3 blocker; original traps after about 5000 ms without progress'),
 'BeginTiling': ('8264B8C0', 'r3 device, r4 flags, r5 tile count, r6 rectangles, r7 clear color; f1 depth, r9 stencil'),
 'EndTiling': ('8264BD58', 'r3 device, r4 resolve flags, r5 rectangles, r6 destination texture, r7 clear color; f1 depth, r9 stencil, r10 parameters'),
 'SetPredication': ('8264B750', 'r3 device, r4 tile mask'),
}
for role in additional:
    if not any(h['role']==role for h in profile['hooks']):
        profile['hooks'].append({'role': role, 'address': None, 'verified': False})
for hook in profile['hooks']:
    role = hook['role']
    hook['runtime_verified'] = False
    hook['installed'] = False
    match = roles.get(role) or additional.get(role)
    if match:
        address, abi = match
        symbol = 'sub_'+address
        instructions = code.instructions(symbol)
        body = image[int(address,16)-0x82000000:int(address,16)-0x82000000+4*len(instructions)]
        hook.update(address='0x'+address, static_semantics_reviewed=True, abi=abi,
                    instruction_count=len(instructions), guest_code_sha256=hashlib.sha256(body).hexdigest())
    else:
        hook['static_semantics_reviewed'] = False
        if role == 'XGRegisterPixelShader':
            hook['resolution'] = 'No standalone registration call in the observed pixel-creation path; intercept D3DDevice_CreatePixelShader after original execution instead'
        elif role == 'DrawVertices':
            hook['resolution'] = 'Bound nonindexed draw entry not identified; UP/BeginVertices and indexed paths found; audit all draw coverage before enabling'
        else:
            hook['resolution'] = 'Unmapped; do not substitute InitializeRingBuffer without proving public entry ABI'
# Evidence locations name selected instructions, not complete guest functions.
field_specs = [
 ('device_bytes',22400,'826478E0','li r3,22400'),
 ('device_alignment',128,'826478E0','li r4,128'),
 ('ring',48,'8264E4C0','stw r11,48(r3)'),
 ('ring_limit',52,'8264E4C0','stw r10,52(r3)'),
 ('ring_guarantee',56,'8264E4C0','stw r11,56(r3)'),
 ('fence_completion',10896,'8264ED40','lwz r10,10896(r31)'),
 ('presentation_scratch',10900,'8264AD00','lwz r10,10900(r31)'),
 ('fence_tag',10908,'8264ED40','lwz r11,10908(r31)'),
 ('flags',10940,'826478E0','stb r11,10940(r31)'),
 ('flags1',10941,'8264E4C0','stb r10,10941(r3)'),
 ('flags2',10942,'82653E68','stb r11,10942(r31)'),
 ('vertex_declaration',11812,'8263FA90','stw r4,11812(r3)'),
 ('index_buffer',12428,'826505F0','lwz r22,12428(r31)'),
 ('render_targets',12432,'8263D140','addi r11,r4,3108'),
 ('depth_stencil',12448,'8263D4A8','stw r30,12448(r31)'),
 ('stream_sources',12452,'8263C958','addi r11,r29,3113'),
 ('stream_strides',12520,'8264FB38','stb r11,12520(r31)'),
 ('textures',12536,'82649548','addi r11,r4,3134'),
 ('viewport',12640,'8263CD90','stfs f28,12640(r31)'),
 ('scissor_rect',12668,'8263C858','stw r11,12668(r3)'),
 ('pixel_shader',12684,'8263F608','stw r29,12684(r30)'),
 ('vertex_shader',12688,'8263F8C0','stw r29,12688(r30)'),
 ('predication',12700,'8264B750','stw r8,12700(r31)'),
 ('tile_count',12740,'8264B8C0','stw r27,12740(r31)'),
 ('tile_rectangles',12744,'8264B8C0','addi r10,r31,12748'),
 ('inline_ring_end',13428,'8264FB38','stw r11,13428(r31)'),
 ('command_buffer',13500,'8264E6F0','lwz r3,13500(r31)'),
 ('presentation_parameters',13528,'826539A0','addi r3,r29,13528'),
 ('presentation_interval',13580,'8264AD00','lwz r10,13580(r31)'),
 ('auto_depth',14808,'826539A0','stw r4,14808(r29)'),
 ('auto_front',14812,'826539A0','stw r3,14812(r29)'),
 ('auto_back',14816,'826539A0','stw r5,14816(r29)'),
 ('gamma_ramp',14988,'8263CB08','addi r30,r31,14988'),
 ('present_fence_tag',14924,'8264B1A8','stw r30,14924(r31)'),
 ('swap_fence_tag',10928,'8264F4B8','stw r31,10928(r3)'),
 ('critical_section0',14928,'82653E68','addi r3,r31,14928'),
 ('critical_section1',14956,'82653E68','addi r3,r31,14956'),
 ('swap_count',16544,'8264B1A8','stw r11,16544(r31)'),
 ('side_buffer',16712,'82653DF8','stw r3,16712(r31)'),
 ('hardware_sentinel0',21540,'82653E68','stw r11,21540(r31)'),
 ('hardware_sentinel1',21544,'82653E68','stw r11,21544(r31)'),
 ('performance_frequency',21568,'82653E68','stfs f0,21568(r31)'),
 ('behavior_flags',22280,'826478E0','stw r30,22280(r31)'),
 ('point_size_packet',10596,'8263B318','sth r11,10596(r3)'),
 ('point_minmax_packet',10600,'8263B3D0','sth r11,10600(r3)'),
 ('program_wrap0',10540,'8263B428','stw r11,10540(r3)'),
 ('depth_control',10548,'8263ACB0','stw r11,10548(r3)'),
]
fields=[]
private_proof=[]
for name,value,address,expected in field_specs:
    symbol='sub_'+address
    matches=[(pc,op) for pc,op in code.instructions(symbol) if op==expected]
    if not matches:raise ValueError(f'Missing evidence for {name}: {expected}')
    pc,op=matches[0]
    raw=image[pc-0x82000000:pc-0x82000000+4]
    fields.append(dict(name=name,value=value,evidence_function='0x'+address,evidence_pc=f'0x{pc:08X}',instruction_hex=raw.hex(),runtime_verified=False))
    private_proof.append(dict(name=name,pc=pc,expected=op))
profile['device_fields']=fields
profile['unresolved_layout']=['Full public device/resource layout agreement with upstream',
 'All nonindexed/indexed-UP/tessellated draw coverage', 'Tiling hooks must preserve actual clear values and host event order',
 'Shader Bind path and recorded-command-buffer usage', 'Presentation pacing using the proven presentation_interval field',
 'Resource/shader container layout equivalence beyond reviewed fields']
profile_path.write_text(json.dumps(profile,indent=2)+'\n')
(ROOT/'analysis/field-proof.json').write_text(json.dumps(private_proof,indent=2)+'\n')
print('Static hook entries:',sum(bool(h.get('static_semantics_reviewed')) for h in profile['hooks']), '/',len(profile['hooks']))
print('Device field anchors:',len(fields),'; backend remains disabled')
