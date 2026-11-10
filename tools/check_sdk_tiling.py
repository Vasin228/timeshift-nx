"""Compile concrete native tiling SDK bridge with controlled renderer/CPU resolve services."""
from pathlib import Path
import subprocess,sys,json,shutil
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[1]
sanitize='--sanitize' in sys.argv;flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []
real_cpu='--real-cpu' in sys.argv
name='sdk-tiling'+('-real-cpu' if real_cpu else '')+('-sanitized' if sanitize else '-fixture')
exe=ROOT/'build'/name
if real_cpu:flags+=['-DTIMESHIFT_REAL_FRAME_CPU']
includes=['adapter','tests','analysis/sdk-tiling-fixture','analysis/frame-cpu-fixture','analysis/source-fixture','private/app/generated/default','private/sdk/include',
 'upstream/sa2-nx/app/src','upstream/sa2-nx/app/thirdparty/plume']
external='/root/lab/rexglue-sdk/thirdparty/'
includes +=[external+n for n in ['simde','fmt/include','spdlog/include','o1heap/o1heap']]
sources=['tests/sdk_tiling_fixture.cpp','adapter/sdk_tiling_host.cpp','adapter/tiling_hooks.cpp','adapter/tiling.cpp','adapter/tiling_guest_calls.cpp',
 'adapter/device_registry.cpp','adapter/device_lifecycle_hooks.cpp','adapter/device_bootstrap.cpp','adapter/device_guest_calls.cpp','adapter/source_memory.cpp']
cmd=['clang++','-std=c++23','-O1','-g','-pthread','-DFMT_HEADER_ONLY','-DTIMESHIFT_RECOMP',*flags]
cmd+=['-I'+str(ROOT/p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir=ROOT/'build/host-tiling'/name;objects_dir.mkdir(parents=True,exist_ok=True)
def compile_source(source):
 obj=objects_dir/(Path(source).stem+'.o')
 prefix=[shutil.which('ccache')] if shutil.which('ccache') else []
 # Cache dependency-aware individual objects; a multi-source compiler command
 # bypasses ccache. Limit parallel work to avoid starving other lab tasks.
 result=subprocess.run(prefix+cmd+['-c',str(ROOT/source),'-o',str(obj)],capture_output=True,text=True)
 if result.returncode:
  print(result.stdout,end='');print(result.stderr,end='');result.check_returncode()
 return str(obj)
with ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_source,sources))
subprocess.run(['clang++','-pthread',*flags,*objects,'-o',str(exe)],check=True)
cases=[[],['--depth'],['--oom'],['--depth','--oom']] if real_cpu else [[]]
for args in cases:
 r=subprocess.run([str(exe),*args],capture_output=True,text=True)
 print('Scenario:',args or ['color']);print(r.stdout,end='');print(r.stderr,end='');assert r.returncode==0,r.returncode
out=ROOT/'analysis/sdk-tiling-validation';out.mkdir(exist_ok=True)
(out/(('real-cpu-' if real_cpu else '')+('sanitized.json' if sanitize else 'normal.json'))).write_text(json.dumps(dict(success=True,sanitizers=sanitize,
 scenarios=len(cases),original_bootstrap_state_functions=13,additional_frame_cpu_functions=23 if real_cpu else 0,
 actual_cpu_clear_resolve=real_cpu,actual_backend_or_gpu=False),indent=2)+'\n')
