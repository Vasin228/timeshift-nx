"""Read-only frozen-source audit with independently reproduced managed app ports."""
from pathlib import Path
import hashlib,json,tarfile,zipfile
from port_fence_readers import verify_and_apply

ROOT=Path(__file__).resolve().parents[1]
verify_and_apply(False)
profile=json.loads((ROOT/'adapter/fence_read_profile.json').read_text())
managed={e['file']:e['sha256'] for e in profile['private_app_files']}
managed.update({
    'generated/default/timeshift_funcs.h': '101f678d8726200914bb4323f1fed60afe43bb79a378d9f9bf824081b18b0406',
    'generated/default/timeshift_init.cpp': '63abd75b5b571ab91103ac2c7494fbc416d15ca90c4cc4a2456898124ebe4581',
    'generated/default/timeshift_register.cpp': '6d2fa8961ffabdc248a43ae52074c3763c1f6f104f7a98c01f8eaa5367bcfd9c',
    'generated/default/timeshift_pch.h': '782d66f7f9b09ac22787c2ec1c28788ad0042ff6f02fa48584a690e99a7450c9',
})

sdk_managed = {
    'src/kernel/xboxkrnl/xboxkrnl_io.cpp': '25038f822e7f99ac46e7620fe3b0854b2317df5d010dcfae69f75b9c11b388ca',
    'src/audio/CMakeLists.txt': '212583c6d54ccc186fd45e4fddd3bebaa1a74651c9946b103ee59c75b801e076',
    'src/audio/audio_system.cpp': '806f52736525796f4185d585fb0d339758f42e4696fc1413803691b822c0b223',
    'src/audio/nop/nop_audio_system.cpp': '538efacccdddc4ba81a42858a644467081534abe064474bf8d1403bef0bdf56d',
    'src/core/threading_posix.cpp': '6f1b67ab692edaa0c35733c72e1e2ab954906736edfc1f8cc3b85156b24ec0c6',
    'src/audio/xma_context.cpp': 'a442cc2f5ceeedd5c7bea42ff9b9356b69ac0236e75201899f1b6f16e01b77aa',
    'src/audio/xma_decoder.cpp': '03517a16134b63672c6ee6982dc0e2d8bab75fcae61e7dd88df02b3afc264c9b',
    'src/audio/xma_decoder_switch.cpp': 'a5c95bdef00681ccbdd5bd273015446dd95c71ec664424ed525b80ee2bc15e85',
    'src/graphics/switch_native/native_textures.cpp': '8b6820aeecdd769851d930ca467986ffbed36795352403d97ed52782a810baac',
    'include/rex/audio/xma/context.h': '3cbe892635b066fafcaaceae7e9e7d10675f1a2416d3f97a2fd74dbaca7a7fed',
    'include/rex/system/xmemory.h': 'bce2cc8df17db59400d8906792c578b2b5d20f74cc59c7b47f962fe89fad58ba',
    'src/ui/switch/rex_app_switch.cpp': 'bc1add3af89d7cd10ad096fcf01127c047f5a7a1223f40a8cacb56aebeb4be63',
    'src/input/switch/switch_input_driver.cpp': '8b8342e147eb6ae1535f7b6cabdc9088d5e3c6cc7801df0842f75f2eb7687e4c',
    'src/input/switch/switch_input_driver.h': '5279b0c39440cbfef6b5c62f4919cc412d764b06636a5adf3aba931fbae68d1a',
    'src/core/guest_memory_switch.cpp': 'bb690032bbed965ae2fd68dc6ee2e2dff903595842ac7fa2e03eda47cd3adc22',
    'src/system/xmemory.cpp': '634e3e9045f3af8fa18c4bda09ab92cec7fd63f309ca65db586328c6c3853e07',
    'src/system/xthread.cpp': 'e5bd8de851086d91dfd65377707b5dcd649d4f46e900a352b439750768015b9d',
    'src/kernel/xboxkrnl/xboxkrnl_threading.cpp': 'a88c76e6ad44d6cc32447d74861e3eba3a69105cd664f36f0ec0c478cdb6d054',
    'src/ui/switch/windowed_app_main_switch.cpp': 'f91f9238c3820c3e255702d188302f4d65112084ecb6bd26244ca35c46cd5dec',
    'src/kernel/xam/xam_ui_switch.cpp': 'ec6e4981f4ce754b388a194516b65abead375cbde73232a505f3de13c7b5566c',
    'src/system/kernel_state.cpp': 'b77b24e1d9357165b00958d1e871c18b798d8ad7ca0f643c27be849c3411d9c5',
    'src/system/xam/content_manager.cpp': '114128f708144edd343bc265608f23850d28fa8ebb100d9b076d5c74a29c2797',
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
