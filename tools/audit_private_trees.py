"""Read-only frozen-source audit with independently reproduced managed app ports."""
from pathlib import Path
import hashlib,json,tarfile,zipfile
from port_fence_readers import verify_and_apply

ROOT=Path(__file__).resolve().parents[1]
verify_and_apply(False)
profile=json.loads((ROOT/'adapter/fence_read_profile.json').read_text())
managed={e['file']:e['sha256'] for e in profile['private_app_files']}
managed.update({
    'generated/default/timeshift_funcs.h': '6b9e29d8bb1ca04a1966d4d44dc2ba7d19cc7789658ca61b91817f29e05334fd',
    'generated/default/timeshift_init.cpp': 'bbdcae4fb99217414d768e3e074f124a884ad19b9a9b14c9bd0f8e72c4907961',
    'generated/default/timeshift_register.cpp': '077aa47857220e69b49c5b29f9f90ce133c5e0407d5cbdb885e3b275f0ed1d29',
})

sdk_managed = {
    'src/kernel/xboxkrnl/xboxkrnl_io.cpp': '25038f822e7f99ac46e7620fe3b0854b2317df5d010dcfae69f75b9c11b388ca',
    'src/audio/CMakeLists.txt': '212583c6d54ccc186fd45e4fddd3bebaa1a74651c9946b103ee59c75b801e076',
    'src/audio/audio_system.cpp': '366b12852d9bd188b8c0f09e5283b59a0ed24cce64450c6f5db2f523c4905e10',
    'src/audio/nop/nop_audio_system.cpp': 'c1c25549abe986c01c7237efefa6a0ecd9293234698be7dd28cdcc1df41f5674',
    'src/audio/xma_context.cpp': 'a442cc2f5ceeedd5c7bea42ff9b9356b69ac0236e75201899f1b6f16e01b77aa',
    'src/audio/xma_decoder.cpp': '8cdce2eab101c3c6a7ddf2eea448b3a4529cf8520f49c738f87d15572051b23b',
    'src/audio/xma_decoder_switch.cpp': 'a5c95bdef00681ccbdd5bd273015446dd95c71ec664424ed525b80ee2bc15e85',
    'src/graphics/switch_native/native_textures.cpp': '8b6820aeecdd769851d930ca467986ffbed36795352403d97ed52782a810baac',
    'include/rex/audio/xma/context.h': '3cbe892635b066fafcaaceae7e9e7d10675f1a2416d3f97a2fd74dbaca7a7fed',
    'src/ui/switch/rex_app_switch.cpp': 'a959054684740369661240b228a131feab5574ec5005009ba15bbb4f362db9e4',
}

frozen=ROOT.parent/'timeshift-recomp/frozen/vulkan-part181'
sdk=frozen/'sdk-source.tar.gz'
assert hashlib.sha256(sdk.read_bytes()).hexdigest()=='b39f2c383853c4832683f11c951117b22bcb5c066b5b720b799da2081c69e342'
sdk_count=0
with tarfile.open(sdk) as source:
 for member in source:
  if member.isdir():continue
  assert member.isfile()
  actual = (ROOT/'private/sdk'/member.name).read_bytes()
  if member.name in sdk_managed:
   assert hashlib.sha256(actual).hexdigest() == sdk_managed[member.name], member.name
  else:
   assert actual == source.extractfile(member).read(), member.name
  sdk_count+=1
app=frozen/'actual-app-source.zip'
assert hashlib.sha256(app.read_bytes()).hexdigest()=='0ad150023558aed5e9601268cd22f49e7c706db1a74b171cf401ebf0fc9a43f2'
app_count=0
with zipfile.ZipFile(app) as source:
 for member in source.infolist():
  if member.is_dir():continue
  actual=(ROOT/'private/app'/member.filename).read_bytes()
  if member.filename in managed:assert hashlib.sha256(actual).hexdigest()==managed[member.filename],member.filename
  else:assert actual==source.read(member),member.filename
  app_count+=1
assert hashlib.sha256((ROOT/'private/dump360/default.xex').read_bytes()).hexdigest()=='0eb9b13de4b44b14f89907b91973209112bca8f229cf7b55e26cc5daa0841050'
(ROOT/'analysis/private-tree-audit.json').write_text(json.dumps(dict(success=True,sdk_unchanged_files=sdk_count-len(sdk_managed),
 managed_sdk_files=len(sdk_managed),
 app_unchanged_files=app_count-len(managed),managed_app_files=len(managed),owned_xex=True),indent=2)+'\n')
print('PASS:',sdk_count-len(sdk_managed),'unchanged SDK files;',len(sdk_managed),'reproduced managed SDK files;',app_count-len(managed),'unchanged app files;',len(managed),'reproduced managed app files; owned XEX identity')
