"""Compile the real source owner with actual ReNut completion bodies; controlled heap/FIFO."""
from pathlib import Path
import subprocess,json,sys,re
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu/backend/render_thread.cpp').read_text()
out=ROOT/'analysis/source-fixture';out.mkdir(exist_ok=True)
def function(signature):
    start=source.index(signature);brace=source.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
names=['void WriteFence(u32 completion_va','void QueueFenceCompletion(u32 completion_va',
       'u32 ReadFenceCompletion(u32 completion_va','void QueueCallbackMarker(uint64_t seq)',
       'uint64_t CallbacksReached()','void CallbacksRun(uint64_t seq)']
(out/'completion.inc').write_text('\n'.join(function(s) for s in names)+'\n')
sanitize='--sanitize' in sys.argv
exe=ROOT/('build/source-fixture-sanitized' if sanitize else 'build/source-fixture');exe.parent.mkdir(exist_ok=True)
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []
subprocess.run(['clang++','-std=c++23','-O1','-g','-pthread',*flags,
 '-I'+str(ROOT/'adapter'),'-I'+str(ROOT/'private/sdk/include'),'-I'+str(out),
 str(ROOT/'tests/source_memory_fixture.cpp'),str(ROOT/'adapter/source_memory.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True)
print(result.stdout,end='');print(result.stderr,end='')
assert result.returncode==0,result.returncode
(out/('report-sanitized.json' if sanitize else 'report.json')).write_text(json.dumps(dict(success=True,real_owner=True,
 scenarios=int(re.search(r'PASS: (\d+)',result.stdout)[1]),sanitizers=sanitize,
 actual_renut_completion_bodies=6,sdk_endian_words=True,
 limitation='Controlled heap/guest mappings/FIFO/callback acknowledgements; SDK runtime compiled separately, no actual kernel, GPU or source-lifecycle hooks'),indent=2)+'\n')
