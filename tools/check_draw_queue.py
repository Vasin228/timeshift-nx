"""Execute actual queue bodies without a renderer; old dirty-tag consumption must fail."""
from pathlib import Path
import subprocess,json
ROOT=Path(__file__).resolve().parents[1]
gpu=ROOT/'upstream/sa2-nx/app/src/renut_engine/gpu'
out=ROOT/'analysis/draw-queue-fixture';out.mkdir(exist_ok=True)
text=(gpu/'backend/queued.cpp').read_text()
def function(source,signature):
    start=source.index(signature);brace=source.index('{',start);level=1;end=brace+1
    while level:
        level+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
current_capture=function(text,'DrawCapture *Capture(u32 device_va, const DrawCall &call)')
functions='\n'.join(function(text,s) for s in ['void QueueDeferredDraw(u32 device_va','void FlushDeferredVertices(u32 device_va'])
(out/'queue.inc').write_text(functions+'\n')
(out/'capture.inc').write_text(current_capture+'\n')
base=subprocess.run(['git','show','HEAD:app/src/renut_engine/gpu/backend/queued.cpp'],cwd=ROOT/'upstream/sa2-nx',capture_output=True,text=True,check=True).stdout
(out/'capture-old.inc').write_text(function(base,'DrawCapture *Capture(u32 device_va, const DrawCall &call)')+'\n')
deferred=function(text,'struct DeferredDraw {')+';'
(out/'deferred.inc').write_text(deferred+'\n')
source=out/'fixture.cpp'
source.write_text('''#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using u8=uint8_t;using u32=uint32_t;
namespace d3d{constexpr u32 kDeviceSnapshotSize=12704;struct Device{uint64_t pending[5];u8 tail[22400-40];};}
d3d::Device devices[3]{};
template<class T>T *GuestPtr(u32 va){return va==1||va==2?reinterpret_cast<T*>(&devices[va]):nullptr;}
void ClearPending(d3d::Device*){throw std::runtime_error("original dirty tags were consumed before original draw");}
enum class SnapshotKind{Whole,Rebase,Delta};
struct DrawCall{u32 primitive=4;const u8 *up_vertices=nullptr,*up_indices=nullptr;};
struct DrawCapture{std::vector<u8> state,vertices,indices,verify;DrawCall call;SnapshotKind snapshot=SnapshotKind::Whole;};
unsigned live=0;
struct Pool{DrawCapture *Take(){++live;return new DrawCapture;}void Give(DrawCapture *p){if(p){--live;delete p;}}}g_capture_pool;
using ChainGuard=std::unique_lock<std::mutex>;
struct Delta{std::mutex mutex;void *device=nullptr;uint64_t whole=0;}g_delta;
bool DeltasOn(){return false;}bool DeferredChainOn(){return false;}bool HookFastPathsOn(){return false;}
void RunReadyCallbacks(){}void FlushRecordedVertices(u32){}
DrawCapture *CaptureDelta(u32,const DrawCall&){throw std::runtime_error("unported delta path entered");}
void FlushDeferredVertices(u32);
#include "deferred.inc"
std::mutex g_deferred_mutex;std::unordered_map<u32,DeferredDraw>g_deferred;
std::atomic<size_t>g_deferred_count{0};std::vector<DrawCapture>queued;
void QueueCapture(DrawCapture *p){queued.push_back(*p);g_capture_pool.Give(p);}
#ifdef NEGATIVE_CONTROL
#include "capture-old.inc"
#else
#include "capture.inc"
#endif
#include "queue.inc"
void Check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main(){try{
  std::vector<u8> vertices(48,0),indices(12,0);DrawCall call{};
  devices[1].pending[0]=0x123456789ABCDEF0ull;devices[1].tail[100]=0x42;
  QueueDeferredDraw(1,call,vertices.data(),48,indices.data(),12);
  Check(queued.empty(),"unfilled Begin data queued early");
  Check(devices[1].pending[0]==0x123456789ABCDEF0ull,"original tags changed");
  vertices.assign(48,0xA1);indices.assign(12,0xB2);devices[1].tail[100]=0x55;
  FlushDeferredVertices(1);
  Check(queued.size()==1&&queued[0].vertices==vertices&&queued[0].indices==indices,"deferred streams not copied together");
  Check(queued[0].state[40+100]==0x42,"draw state not captured at Begin");
  vertices.assign(48,0xC3);indices.assign(12,0xD4);
  Check(queued[0].vertices[0]==0xA1&&queued[0].indices[0]==0xB2,"queued data not owned");
  QueueDeferredDraw(1,call,vertices.data(),48,indices.data(),12);
  QueueDeferredDraw(1,call,vertices.data(),48,nullptr,0);
  Check(queued.size()==2&&queued[1].indices[0]==0xD4,"previous Begin not flushed before scratch reuse");
  vertices.assign(48,0xE5);FlushDeferredVertices(1);
  Check(queued.size()==3&&queued[2].vertices[0]==0xE5&&queued[2].indices.empty(),"legacy vertex-only Begin");
  QueueDeferredDraw(1,call,vertices.data(),48,nullptr,0);
  QueueDeferredDraw(2,call,vertices.data(),48,indices.data(),12);
  FlushDeferredVertices(2);Check(queued.size()==4,"per-device deferred isolation");
  FlushDeferredVertices(1);Check(queued.size()==5,"other device draw lost");
  QueueDeferredDraw(1,call,nullptr,48,nullptr,0);
  QueueDeferredDraw(0,call,vertices.data(),48,nullptr,0);
  Check(live==0&&queued.size()==5&&g_deferred.empty(),"failed/cancelled capture leaked");
  std::puts("PASS: actual deferred queue bodies preserve dirty state and immutable vertex/index ownership");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\\n",e.what());return 1;}}
''')
for label,extra,expected in [('ported',[],0),('negative-control',['-DNEGATIVE_CONTROL'],1)]:
    exe=out/label
    subprocess.run(['clang++','-std=c++20','-O1','-DTIMESHIFT_RECOMP',*extra,str(source),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    assert result.returncode==expected,(label,result.stdout,result.stderr)
    if label=='negative-control':assert 'dirty tags' in result.stderr
(out/'report.json').write_text(json.dumps(dict(success=True,actual_queue_bodies=True,original_dirty_clear_negative_control=True,
 limitation='Whole-capture path with controlled queue/pool/SDK; no render thread or GPU'),indent=2)+'\n')
print('PASS: actual deferred vertex/index queue; original dirty-clear path fails negative control')
