"""Audit the completed Switch object probe; this is not an executable link."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / 'build/core-switch'
commands = json.loads((build / 'compile_commands.json').read_text())
core = [e for e in commands if 'CMakeFiles/timeshift_renut_core.dir/' in e['command']]
assert len(core) == 27, len(core)
objects = []
for entry in core:
    source = Path(entry['file'])
    assert '/d3d/hooks/' not in source.as_posix()
    assert source.name != 'd3d_native_state.cpp'
    assert '/private/app/' not in source.as_posix()
    assert '-DTIMESHIFT_RECOMP' in entry['command']
    # CMake's compile_commands output path is relative to its directory.
    match = re.search(r' -o (\S+) -c ', entry['command'])
    assert match
    path = Path(entry['directory']) / match[1]
    b = path.read_bytes()
    assert b[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', b, 18)[0] == 183, path
    objects.append(path)
nm = '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm'
result = subprocess.run([nm, '--defined-only', *map(str, objects)], capture_output=True, text=True, check=True)
foreign = re.findall(r'\b(?:sub_|__imp__sub_|renut_native_sub_)[0-9A-Fa-f]{8}\b', result.stdout)
assert not foreign, foreign
def audit_extra(target, expected_count):
    entries=[e for e in commands if 'CMakeFiles/'+target+'.dir/' in e['command']]
    assert len(entries)==expected_count,len(entries)
    paths=[]
    for entry in entries:
        assert '-DTIMESHIFT_RECOMP' in entry['command']
        match=re.search(r' -o (\S+) -c ',entry['command']);assert match
        path=Path(entry['directory'])/match[1]
        b=path.read_bytes()
        assert b[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',b,18)[0]==183,path
        paths.append(path)
    return paths
resource_objects=audit_extra('timeshift_renut_resource_hooks',4)
device_objects=audit_extra('timeshift_renut_device_calls',2)
draw_objects=audit_extra('timeshift_renut_draw_hooks',2)
tiling_objects=audit_extra('timeshift_renut_tiling',2)
source_objects=audit_extra('timeshift_renut_sources',2)
lifecycle_objects=audit_extra('timeshift_renut_lifecycle',3)
sdk_tiling_objects=audit_extra('timeshift_renut_sdk_tiling',2)
fence_query_objects=audit_extra('timeshift_renut_fence_query',1)
fence_wait_objects=audit_extra('timeshift_renut_fence_wait',2)
guest_wait_objects=audit_extra('timeshift_renut_guest_wait',3)
cleanup_objects=audit_extra('timeshift_renut_device_cleanup',2)
callback_objects=audit_extra('timeshift_renut_callback_hooks',1)
presentation_objects=audit_extra('timeshift_renut_presentation_hooks',1)
extra_nm=subprocess.run([nm,'--defined-only',*map(str,resource_objects+device_objects+draw_objects+tiling_objects+source_objects+lifecycle_objects+sdk_tiling_objects+fence_query_objects+fence_wait_objects+cleanup_objects+callback_objects+presentation_objects)],capture_output=True,text=True,check=True)
defined=set(re.findall(r'\b(?:sub_|__imp__sub_|renut_native_sub_)[0-9A-Fa-f]{8}\b',extra_nm.stdout))
expected={'sub_8263F7C8','sub_82640FF0','sub_8263FB98','sub_8264D7C0'}
expected.update({'sub_826505F0','sub_8264FFD8','sub_82650550','sub_8264FB38',
                 'sub_82650020','sub_8264A810','sub_8264C8E8'})
expected.update({'sub_826478E0','sub_82647600','sub_8264E6F0','sub_8264F3B8','sub_8264F5D8'})
expected.update({'sub_8264B8C0','sub_8264BD58','sub_8264B750'})
expected.add('sub_8264F4F0')
expected.update({'sub_8264ED40','sub_82647528','sub_82653648'})
expected.add('sub_8264F6B8')
expected.update({'sub_8264B1A8','sub_8264AD00'})
assert defined==expected,defined
wait_profile=json.loads((ROOT/'adapter/fence_read_profile.json').read_text())
for entry in wait_profile['private_app_files']:
    source=ROOT/'private/app'/entry['file']
    assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['sha256'],source
guest_nm=subprocess.run([nm,'--defined-only',*map(str,guest_wait_objects)],capture_output=True,text=True,check=True).stdout
guest_originals=set(re.findall(r'\b(?:__imp__)?sub_[0-9A-Fa-f]{8}\b',guest_nm))
index=json.loads((ROOT/'analysis/function-index.json').read_text())
wait_files={e['file'] for e in wait_profile['private_app_files']}
wait_symbols={s for s,e in index.items() if e['file'] in wait_files}
assert guest_originals==wait_symbols|{'__imp__'+s for s in wait_symbols},guest_originals^(wait_symbols|{'__imp__'+s for s in wait_symbols})
for symbol in ['8264ED40','82647390','82647528']:
    assert re.search(r'\bW sub_'+symbol+r'\b',guest_nm),'retained public alias is not weak: '+symbol
artifacts = []
for name in ['plume/libplume.a', 'plume/uam/libuamlib.a', 'plume/uam_local.o',
             'libts_renut_zstd.a', 'libts_renut_smolv.a', 'libts_renut_xxhash.a']:
    path = build / name
    b = path.read_bytes()
    artifacts.append(dict(path=name, bytes=len(b), sha256=hashlib.sha256(b).hexdigest()))
assert 'PLUME_SWITCH_NO_NVK:BOOL=ON' in (build / 'CMakeCache.txt').read_text()
report = dict(success=True, renderer_translation_units=len(core),
              resource_bridge_translation_units=4,device_call_translation_units=2,
              draw_bridge_translation_units=2,
              tiling_protocol_translation_units=2,
              source_owner_translation_units=2,
              lifecycle_translation_units=3,
              sdk_tiling_translation_units=2,
              fence_query_translation_units=1,
              fence_wait_translation_units=2,
              private_guest_wait_translation_units=3,
              device_cleanup_translation_units=2,
              callback_hook_translation_units=1,
              presentation_hook_translation_units=1,
              retained_guest_functions_in_wait_chunks=len(wait_symbols),
              dormant_timeshift_hooks=sorted(defined),production_activation=False,
              all_renderer_objects_aarch64=True, foreign_title_hook_definitions=0,
              foreign_native_title_setters_compiled=False, nvk_driver_link_requested=False,
              artifacts=artifacts,
              limitation='Object/static-library compilation and symbol audit only; no executable link, game boot, GPU execution, visual or performance result')
(ROOT / 'analysis/core-switch-audit.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS:',len(core),'core + 24 adapter + 3 retained guest AArch64 objects;',len(wait_symbols),'original guest functions; exactly 26 dormant/active TimeShift hooks; zero foreign title hooks')
