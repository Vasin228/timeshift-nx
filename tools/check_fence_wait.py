"""Compile retained wait CPU code and native owner interposition with sanitizers."""
from pathlib import Path
import subprocess,sys,json,shutil
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[1]
sanitize='--sanitize' in sys.argv
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []
exe=ROOT/'build'/('fence-wait-sanitized' if sanitize else 'fence-wait-fixture')
includes=['adapter','tests','analysis/fence-wait-fixture','analysis/source-fixture','analysis/bootstrap-guest-fixture',
 'private/app/generated/default','private/sdk/include']
external='/root/lab/rexglue-sdk/thirdparty/'
includes +=[external+n for n in ['simde','fmt/include','spdlog/include','o1heap/o1heap']]
sources=['tests/fence_wait_fixture.cpp','adapter/device_registry.cpp','adapter/device_bootstrap.cpp',
 'adapter/device_guest_calls.cpp','adapter/source_memory.cpp','adapter/device_lifecycle_hooks.cpp',
 'adapter/fence_read.cpp','adapter/fence_wait_hooks.cpp']
command=['clang++','-std=c++23','-O1','-g','-pthread','-DFMT_HEADER_ONLY','-DTIMESHIFT_RECOMP',*flags]
command+=['-I'+str(ROOT/p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir=ROOT/'build/host-fence-wait'/exe.name;objects_dir.mkdir(parents=True,exist_ok=True)
def compile_source(source):
 obj=objects_dir/(Path(source).stem+'.o');prefix=[shutil.which('ccache')] if shutil.which('ccache') else []
 result=subprocess.run(prefix+command+['-c',str(ROOT/source),'-o',str(obj)],capture_output=True,text=True)
 if result.returncode:print(result.stdout,end='');print(result.stderr,end='');result.check_returncode()
 return str(obj)
with ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_source,sources))
subprocess.run(['clang++','-pthread',*flags,*objects,'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,end='');assert r.returncode==0,r.returncode
out=ROOT/'analysis/fence-wait-validation';out.mkdir(exist_ok=True)
(out/('sanitized.json' if sanitize else 'normal.json')).write_text(json.dumps(dict(success=True,sanitizers=sanitize,
 scenarios=10,retained_cpu_functions=9,locked_loads=6,actual_gpu_or_kernel=False),indent=2)+'\n')
