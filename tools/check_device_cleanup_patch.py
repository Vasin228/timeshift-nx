"""Export/reproduce the independent two-file native device cleanup API patch."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1];up=ROOT/'upstream/sa2-nx'
names=['app/src/renut_engine/gpu/backend/video.cpp','app/src/renut_engine/gpu/backend/video.h']
patch=ROOT/'patches/timeshift-device-cleanup.patch'
parser=argparse.ArgumentParser();parser.add_argument('--export',action='store_true');args=parser.parse_args()
def git(cwd,*args):return subprocess.run(['git','-c','core.autocrlf=false',*args],cwd=cwd,capture_output=True,check=True).stdout
assert git(up,'rev-parse','HEAD').decode().strip()=='32a81a83aafd83e25d3a3cccae34f567766402c6'
parent=ROOT/'analysis/patch-reproduction';parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=parent) as directory:
 temp=Path(directory).resolve();assert temp.is_relative_to(parent.resolve()) and temp!=parent.resolve()
 git(temp,'init','-q')
 base={name:git(up,'show','HEAD:'+name) for name in names}
 for name,content in base.items():
  path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(content)
 git(temp,'add','--',*names)
 current={name:(up/name).read_text().encode() for name in names}
 if args.export:
  for name,content in current.items():(temp/name).write_bytes(content)
  patch.write_bytes(git(temp,'diff','--binary','--',*names))
  lock=json.loads((ROOT/'dependencies.lock.json').read_text())
  lock['device_cleanup_patch']=dict(file=patch.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
   scope='TIMESHIFT_RECOMP: actual GPU-idle barrier and conditional video retirement')
  (ROOT/'dependencies.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
 lock=json.loads((ROOT/'dependencies.lock.json').read_text());assert hashlib.sha256(patch.read_bytes()).hexdigest()==lock['device_cleanup_patch']['sha256']
 for name,content in base.items():(temp/name).write_bytes(content)
 git(temp,'apply','--check',str(patch));git(temp,'apply',str(patch))
 for name in names:assert (temp/name).read_bytes()==current[name],name
 git(temp,'apply','--reverse','--check',str(patch));git(temp,'apply','--reverse',str(patch))
 for name in names:assert (temp/name).read_bytes()==base[name],name
git(up,'apply','--reverse','--check',str(patch))
before={name:((up/name).read_bytes(),(up/name).stat().st_mtime_ns) for name in names}
subprocess.run([sys.executable,str(ROOT/'tools/port_device_cleanup.py')],check=True)
assert before=={name:((up/name).read_bytes(),(up/name).stat().st_mtime_ns) for name in names}
print('PASS: two-file device cleanup API patch; clean/reverse/content+mtime idempotence')
