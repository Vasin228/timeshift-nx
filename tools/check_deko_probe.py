"""Run the selected real shaders through pinned SPIRV-Cross, conversion and uam.

Uses separate uam processes sequentially; uam is not reentrant. This checks
code generation only. No renderer, game, console or emulator is launched.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
upstream=ROOT/'upstream/sa2-nx'
spec=importlib.util.spec_from_file_location('deko3d_glsl',upstream/'tools/deko3d_glsl.py')
converter=importlib.util.module_from_spec(spec);spec.loader.exec_module(converter)
parser=argparse.ArgumentParser()
parser.add_argument('--input',type=Path,default=ROOT/'analysis/translation-probe/spirv')
parser.add_argument('--out',type=Path,default=ROOT/'analysis/deko-probe')
args=parser.parse_args()
out=args.out
for name in ('vk','glsl','dksh','logs'): (out/name).mkdir(parents=True,exist_ok=True)
rows=[]
started=time.monotonic()
for source in sorted(args.input.glob('*.spv')):
    stage='geom' if source.name.startswith('gs_') else ('vert' if source.name.startswith('vs_') else 'frag')
    stem=source.stem
    vk=out/'vk'/(stem+'.glsl');glsl=out/'glsl'/(stem+'.glsl');dksh=out/'dksh'/(stem+'.dksh')
    row=dict(shader=source.name,stage=stage,input_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),success=False)
    try:
        result=subprocess.run([str(ROOT/'build/spirv-cross/spirv-cross'),str(source),'--version','460','--vulkan-semantics','--relax-nan-checks','--output',str(vk)],capture_output=True,text=True,timeout=30)
        if result.returncode:raise ValueError('SPIRV-Cross: '+result.stdout+result.stderr)
        text,_=converter.convert(vk.read_text(),stage,converter.DEFAULT_UBO_MAP,'handle')
        glsl.write_text(text)
        result=subprocess.run([str(ROOT/'build/uam-host/uam'),'-s',stage,'-o',str(dksh),str(glsl)],capture_output=True,text=True,timeout=30)
        log=result.stdout+result.stderr
        (out/'logs'/(stem+'.txt')).write_text(log)
        if result.returncode or not dksh.exists():raise ValueError('uam: '+log)
        data=dksh.read_bytes()
        if data[:4]!=b'DKSH':raise ValueError('Invalid native shader magic')
        row.update(success=True,dksh_bytes=len(data),dksh_sha256=hashlib.sha256(data).hexdigest())
    except Exception as error:
        row['error']=str(error)[:1200]
    rows.append(row)
    print(len(rows),stem,'PASS' if row['success'] else row['error'].splitlines()[0],flush=True)
report=dict(success=all(row['success'] for row in rows),inputs=len(rows),passed=sum(row['success'] for row in rows),
            elapsed_seconds=time.monotonic()-started,shader_optimization_passes='none',
            uam_source='pinned sa2-nx app/thirdparty/uam; patched 1.1.0',rows=rows,
            limitation='Default variants only; no runtime pipeline/link/image/performance validation')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Native default-variant results:',report['passed'],'/',len(rows),'in',round(report['elapsed_seconds'],2),'seconds')
sys.exit(0 if report['success'] else 1)
