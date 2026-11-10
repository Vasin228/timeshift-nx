"""Export/check the incremental queue patch after Part184; no private game inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
up=ROOT/'upstream/sa2-nx'
names=['app/src/renut_engine/gpu/backend/queued.cpp',
       'app/src/renut_engine/gpu/backend/queued.h']
patch=ROOT/'patches/timeshift-draw-queue.patch'
parser=argparse.ArgumentParser();parser.add_argument('--export',action='store_true');args=parser.parse_args()
def git(cwd,*command):
    return subprocess.run(['git','-c','core.autocrlf=false',*command],cwd=cwd,
                          capture_output=True,check=True).stdout

assert git(up,'rev-parse','HEAD').decode().strip()=='32a81a83aafd83e25d3a3cccae34f567766402c6'
parent=ROOT/'analysis/patch-reproduction';parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=parent) as directory:
    temp=Path(directory).resolve()
    assert temp.is_relative_to(parent.resolve()) and temp!=parent.resolve()
    git(temp,'init','-q')
    for name in names:
        p=temp/name;p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(git(up,'show','HEAD:'+name))
    git(temp,'apply','--include='+names[0],str(ROOT/'patches/timeshift-device-view.patch'))
    base={name:(temp/name).read_bytes() for name in names}
    git(temp,'add','--',*names)
    current={name:(up/name).read_text().replace('\r\n','\n').encode() for name in names}
    if args.export:
        for name,data in current.items():(temp/name).write_bytes(data)
        patch.write_bytes(git(temp,'diff','--binary','--',*names))
        lock=json.loads((ROOT/'dependencies.lock.json').read_text())
        lock['draw_queue_patch']=dict(file=patch.relative_to(ROOT).as_posix(),
            sha256=hashlib.sha256(patch.read_bytes()).hexdigest(),
            apply_after='device_view_patch')
        (ROOT/'dependencies.lock.json').write_text(json.dumps(lock,indent=2)+'\n')
    lock=json.loads((ROOT/'dependencies.lock.json').read_text())
    assert hashlib.sha256(patch.read_bytes()).hexdigest()==lock['draw_queue_patch']['sha256']
    for name,data in base.items():(temp/name).write_bytes(data)
    git(temp,'apply','--check',str(patch));git(temp,'apply',str(patch))
    for name in names:assert (temp/name).read_bytes()==current[name],name
    git(temp,'apply','--reverse','--check',str(patch))
    git(temp,'apply','--reverse',str(patch))
    for name in names:assert (temp/name).read_bytes()==base[name],name
git(up,'apply','--reverse','--check',str(patch))
before={name:((up/name).read_bytes(),(up/name).stat().st_mtime_ns) for name in names}
subprocess.run([sys.executable,str(ROOT/'tools/port_draw_queue.py')],check=True)
after={name:((up/name).read_bytes(),(up/name).stat().st_mtime_ns) for name in names}
assert before==after,'Port script changed content/mtime on an already ported tree'
print('PASS: incremental two-file draw queue patch, clean reproduction/reverse and content/mtime idempotence')
