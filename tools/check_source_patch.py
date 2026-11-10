"""Export/check completion-reader patch after Part184 render-thread compatibility."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1];up=ROOT/'upstream/sa2-nx'
names=['app/src/renut_engine/gpu/backend/render_thread.cpp',
       'app/src/renut_engine/gpu/backend/render_thread.h']
patch=ROOT/'patches/timeshift-source-completion.patch'
p=argparse.ArgumentParser();p.add_argument('--export',action='store_true');args=p.parse_args()
def git(cwd,*cmd):return subprocess.run(['git','-c','core.autocrlf=false',*cmd],cwd=cwd,capture_output=True,check=True).stdout
assert git(up,'rev-parse','HEAD').decode().strip()=='32a81a83aafd83e25d3a3cccae34f567766402c6'
parent=ROOT/'analysis/patch-reproduction';parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=parent) as directory:
 temp=Path(directory).resolve();assert temp.is_relative_to(parent.resolve()) and temp!=parent.resolve()
 git(temp,'init','-q')
 for name in names:
  path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(git(up,'show','HEAD:'+name))
 git(temp,'apply','--include='+names[0],str(ROOT/'patches/timeshift-device-view.patch'))
 base={n:(temp/n).read_bytes() for n in names};git(temp,'add','--',*names)
 current={n:(up/n).read_text().replace('\r\n','\n').encode() for n in names}
 if args.export:
  for n,data in current.items():(temp/n).write_bytes(data)
  patch.write_bytes(git(temp,'diff','--binary','--',*names))
  lock=json.loads((ROOT/'dependencies.lock.json').read_text())
  lock['source_completion_patch']=dict(file=patch.relative_to(ROOT).as_posix(),
   sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),apply_after='device_view_patch')
  (ROOT/'dependencies.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
 lock=json.loads((ROOT/'dependencies.lock.json').read_text())
 assert hashlib.sha256(patch.read_bytes()).hexdigest()==lock['source_completion_patch']['sha256']
 for n,data in base.items():(temp/n).write_bytes(data)
 git(temp,'apply','--check',str(patch));git(temp,'apply',str(patch))
 for n in names:assert (temp/n).read_bytes()==current[n],n
 git(temp,'apply','--reverse','--check',str(patch));git(temp,'apply','--reverse',str(patch))
 for n in names:assert (temp/n).read_bytes()==base[n],n
git(up,'apply','--reverse','--check',str(patch))
before={n:((up/n).read_bytes(),(up/n).stat().st_mtime_ns) for n in names}
subprocess.run([sys.executable,str(ROOT/'tools/port_source_completion.py')],check=True)
assert before=={n:((up/n).read_bytes(),(up/n).stat().st_mtime_ns) for n in names}
print('PASS: two-file completion patch clean reproduction/reverse/content+mtime idempotence')
