"""Compile actual SDK entry hooks/registry/bootstrap with private guest fixture support."""
from pathlib import Path
import subprocess,sys,json,re,shutil
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[1]
# check_source_memory.py produces the exact completion-body include; retain
# that prerequisite instead of introducing a second extraction implementation.
assert (ROOT/'analysis/source-fixture/completion.inc').is_file()
sanitize='--sanitize' in sys.argv
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []
exe=ROOT/('build/lifecycle-sanitized' if sanitize else 'build/lifecycle-fixture')
includes=['adapter','tests','analysis/source-fixture','analysis/bootstrap-guest-fixture','analysis/fence-query-fixture',
          'private/app/generated/default','private/sdk/include']
external='/root/lab/rexglue-sdk/thirdparty/'
includes +=[external+n for n in ['simde','fmt/include','spdlog/include','o1heap/o1heap']]
sources=['tests/device_lifecycle_fixture.cpp','adapter/device_registry.cpp','adapter/device_bootstrap.cpp',
         'adapter/device_guest_calls.cpp','adapter/source_memory.cpp','adapter/device_lifecycle_hooks.cpp','adapter/fence_query_hooks.cpp']
command=['clang++','-std=c++23','-O1','-g','-pthread','-DFMT_HEADER_ONLY',*flags]
command+=['-I'+str(ROOT/p if not p.startswith('/') else Path(p)) for p in includes]
objects_dir=ROOT/'build/host-lifecycle'/exe.name;objects_dir.mkdir(parents=True,exist_ok=True)
def compile_source(source):
 obj=objects_dir/(Path(source).stem+'.o')
 prefix=[shutil.which('ccache')] if shutil.which('ccache') else []
 result=subprocess.run(prefix+command+['-c',str(ROOT/source),'-o',str(obj)],capture_output=True,text=True)
 if result.returncode:
  print(result.stdout,end='');print(result.stderr,end='');result.check_returncode()
 return str(obj)
with ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_source,sources))
subprocess.run(['clang++','-pthread',*flags,*objects,'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True);print(result.stdout,end='');print(result.stderr,end='')
assert result.returncode==0,result.returncode
out=ROOT/'analysis/lifecycle-validation';out.mkdir(exist_ok=True)
(out/('sanitized.json' if sanitize else 'normal.json')).write_text(json.dumps(dict(success=True,
 scenarios=int(re.search(r'PASS: (\d+)',result.stdout)[1]),sanitizers=sanitize,
 limitation='Real registry/SDK hooks/bootstrap/original subset/completion bodies; controlled heap, presentation, destructor, kernel and callbacks; no actual GPU or executable integration'),indent=2)+'\n')
